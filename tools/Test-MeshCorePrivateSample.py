"""Run a bounded bidirectional sample on a temporary private MeshCore channel.

The channel secret is generated in memory, never printed or written, and the
temporary channel is removed from both companions in a finally block.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import secrets
import statistics
import time
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType


async def snapshot(node: MeshCore) -> dict[str, Any]:
    core = (await node.commands.get_stats_core()).payload
    radio = (await node.commands.get_stats_radio()).payload
    packets = (await node.commands.get_stats_packets()).payload
    return {"core": core, "radio": radio, "packets": packets}


def delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    return {
        "core_errors": after["core"]["errors"] - before["core"]["errors"],
        "queue_length": after["core"]["queue_len"],
        "sent": after["packets"]["sent"] - before["packets"]["sent"],
        "received": after["packets"]["recv"] - before["packets"]["recv"],
        "receive_errors": after["packets"]["recv_errors"]
        - before["packets"]["recv_errors"],
        "flood_tx": after["packets"]["flood_tx"] - before["packets"]["flood_tx"],
        "flood_rx": after["packets"]["flood_rx"] - before["packets"]["flood_rx"],
        "tx_airtime_seconds": after["radio"]["tx_air_secs"]
        - before["radio"]["tx_air_secs"],
        "rx_airtime_seconds": after["radio"]["rx_air_secs"]
        - before["radio"]["rx_air_secs"],
        "last_rssi_dbm": after["radio"]["last_rssi"],
        "last_snr_db": after["radio"]["last_snr"],
    }


async def read_channels(node: MeshCore) -> list[dict[str, Any]]:
    channels = []
    index = 0
    while True:
        event = await node.commands.get_channel(index)
        if event is None or event.is_error():
            return channels
        channels.append(event.payload)
        index += 1


async def wait_for_marker(
    receiver: MeshCore, marker: str, timeout: float
) -> tuple[float, Any]:
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        remaining = deadline - time.perf_counter()
        event = await receiver.commands.get_msg(timeout=min(remaining, 2.0))
        received_at = time.perf_counter()
        if (
            event is not None
            and event.type == EventType.CHANNEL_MSG_RECV
            and marker in event.payload.get("text", "")
        ):
            return received_at, event
        await asyncio.sleep(0.05)
    raise TimeoutError("Timed out waiting for the private-channel marker")


async def drain_marker_duplicates(receiver: MeshCore, marker: str) -> int:
    duplicates = 0
    while True:
        event = await receiver.commands.get_msg(timeout=1.0)
        if event is None or event.type in (EventType.NO_MORE_MSGS, EventType.ERROR):
            return duplicates
        if (
            event.type == EventType.CHANNEL_MSG_RECV
            and marker in event.payload.get("text", "")
        ):
            duplicates += 1


def summarize(samples: list[dict[str, Any]]) -> dict[str, Any]:
    delivered = [sample for sample in samples if sample["delivered"]]
    latencies = [sample["latency_ms"] for sample in delivered]
    snrs = [sample["snr_db"] for sample in delivered if sample["snr_db"] is not None]
    return {
        "attempted": len(samples),
        "delivered": len(delivered),
        "lost": len(samples) - len(delivered),
        "duplicates": sum(sample.get("duplicates", 0) for sample in delivered),
        "latency_ms": None
        if not latencies
        else {
            "min": round(min(latencies), 1),
            "median": round(statistics.median(latencies), 1),
            "max": round(max(latencies), 1),
        },
        "snr_db": None
        if not snrs
        else {
            "min": round(min(snrs), 2),
            "median": round(statistics.median(snrs), 2),
            "max": round(max(snrs), 2),
        },
    }


async def run(port_a: str, port_b: str, count: int, timeout: float) -> dict[str, Any]:
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    temporary_index: int | None = None
    configured_a = False
    configured_b = False
    cleanup = {port_a: False, port_b: False}
    result: dict[str, Any] = {"success": False, "cleanup": cleanup}

    try:
        node_a = await MeshCore.create_serial(
            port=port_a, only_error=True, default_timeout=10
        )
        node_b = await MeshCore.create_serial(
            port=port_b, only_error=True, default_timeout=10
        )
        if node_a is None or node_b is None:
            raise ConnectionError("One or both MeshCore serial connections failed")

        channels_a = await read_channels(node_a)
        channels_b = await read_channels(node_b)
        shared_slots = min(len(channels_a), len(channels_b))
        for index in range(1, shared_slots):
            if (
                channels_a[index].get("channel_name", "") == ""
                and channels_b[index].get("channel_name", "") == ""
            ):
                temporary_index = index
                break
        if temporary_index is None:
            raise RuntimeError("No shared empty channel slot is available")

        temporary_secret = secrets.token_bytes(16)
        channel_name = "OpenTrailBench"
        event_a = await node_a.commands.set_channel(
            temporary_index, channel_name, temporary_secret
        )
        if event_a is None or event_a.is_error():
            raise RuntimeError("Failed to configure the temporary channel on first node")
        configured_a = True
        event_b = await node_b.commands.set_channel(
            temporary_index, channel_name, temporary_secret
        )
        if event_b is None or event_b.is_error():
            raise RuntimeError("Failed to configure the temporary channel on second node")
        configured_b = True

        verify_a = (await node_a.commands.get_channel(temporary_index)).payload
        verify_b = (await node_b.commands.get_channel(temporary_index)).payload
        if (
            verify_a.get("channel_name") != channel_name
            or verify_b.get("channel_name") != channel_name
            or verify_a.get("channel_secret") != temporary_secret
            or verify_b.get("channel_secret") != temporary_secret
        ):
            raise RuntimeError("Temporary channel verification failed")

        before_a = await snapshot(node_a)
        before_b = await snapshot(node_b)
        samples: dict[str, list[dict[str, Any]]] = {
            f"{port_a}->{port_b}": [],
            f"{port_b}->{port_a}": [],
        }

        async def send_sample(
            sender: MeshCore,
            receiver: MeshCore,
            direction: str,
            number: int,
        ) -> None:
            marker = f"OT7A-{number:02d}-{secrets.token_hex(3)}"
            sent_at = time.perf_counter()
            send_event = await sender.commands.send_chan_msg(temporary_index, marker)
            if send_event is None or send_event.is_error():
                samples[direction].append({"delivered": False, "send_error": True})
                return
            try:
                received_at, receive_event = await wait_for_marker(
                    receiver, marker, timeout
                )
            except TimeoutError:
                samples[direction].append({"delivered": False, "timeout": True})
                return
            await asyncio.sleep(0.2)
            samples[direction].append(
                {
                    "delivered": True,
                    "latency_ms": round((received_at - sent_at) * 1000, 1),
                    "duplicates": await drain_marker_duplicates(receiver, marker),
                    "snr_db": receive_event.payload.get("SNR"),
                }
            )

        for number in range(1, count + 1):
            await send_sample(node_a, node_b, f"{port_a}->{port_b}", number)
            await send_sample(node_b, node_a, f"{port_b}->{port_a}", number)

        after_a = await snapshot(node_a)
        after_b = await snapshot(node_b)
        summaries = {
            direction: summarize(direction_samples)
            for direction, direction_samples in samples.items()
        }
        result.update(
            {
                "success": all(
                    summary["delivered"] == summary["attempted"]
                    for summary in summaries.values()
                ),
                "temporary_channel_index": temporary_index,
                "count_per_direction": count,
                "directions": summaries,
                "node_deltas": {
                    port_a: delta(before_a, after_a),
                    port_b: delta(before_b, after_b),
                },
            }
        )
    finally:
        zero_secret = bytes(16)
        if temporary_index is not None:
            for port, node, configured in (
                (port_a, node_a, configured_a),
                (port_b, node_b, configured_b),
            ):
                if node is not None and configured:
                    try:
                        cleared = await node.commands.set_channel(
                            temporary_index, "", zero_secret
                        )
                        verified = await node.commands.get_channel(temporary_index)
                        cleanup[port] = (
                            cleared is not None
                            and not cleared.is_error()
                            and verified is not None
                            and not verified.is_error()
                            and verified.payload.get("channel_name", "") == ""
                        )
                    except Exception:
                        cleanup[port] = False
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()

    result["success"] = result["success"] and all(cleanup.values())
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    if args.count < 1 or args.count > 20:
        parser.error("--count must be between 1 and 20")

    try:
        result = asyncio.run(
            run(args.port_a, args.port_b, args.count, args.timeout)
        )
    except Exception as exc:
        print(json.dumps({"success": False, "error": f"{type(exc).__name__}: {exc}"}))
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result.get("success") else 2


if __name__ == "__main__":
    raise SystemExit(main())
