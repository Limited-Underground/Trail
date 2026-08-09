"""Run a bounded bidirectional sample on a temporary private MeshCore channel.

The channel secret is generated in memory, never printed or written. A
non-secret lease journal makes cleanup recoverable if the process loses a
device response or is interrupted.
"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
import secrets
import statistics
import time
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType

from meshcore_channel_lease import (
    cleanup_lease,
    configure_lease,
    load_journal,
    new_channel_name,
)


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


async def run(
    port_a: str,
    port_b: str,
    count: int,
    timeout: float,
    journal_path: Path,
) -> dict[str, Any]:
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    temporary_index: int | None = None
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

        temporary_secret = secrets.token_bytes(16)
        nodes = {port_a: node_a, port_b: node_b}
        lease = await configure_lease(
            nodes,
            journal_path,
            new_channel_name(),
            temporary_secret,
        )
        temporary_index = lease.channel_index

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
        if journal_path.exists():
            try:
                record = load_journal(journal_path)
                connected_nodes = {
                    port: node
                    for port, node in ((port_a, node_a), (port_b, node_b))
                    if node is not None
                }
                cleanup = await cleanup_lease(
                    connected_nodes, record, journal_path
                )
            except Exception:
                cleanup = {port_a: False, port_b: False}
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()

    result["cleanup"] = cleanup
    result["success"] = result["success"] and all(cleanup.values())
    return result


async def recover_only(
    port_a: str, port_b: str, journal_path: Path
) -> dict[str, Any]:
    record = load_journal(journal_path)
    if set(record.ports) != {port_a, port_b}:
        raise RuntimeError(
            "Journal ports do not match --port-a and --port-b; no channel changed"
        )
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    try:
        node_a = await MeshCore.create_serial(
            port=port_a, only_error=True, default_timeout=10
        )
        node_b = await MeshCore.create_serial(
            port=port_b, only_error=True, default_timeout=10
        )
        if node_a is None or node_b is None:
            raise ConnectionError("One or both MeshCore serial connections failed")
        cleanup = await cleanup_lease(
            {port_a: node_a, port_b: node_b}, record, journal_path
        )
        return {
            "success": all(cleanup.values()),
            "recovery_only": True,
            "cleanup": cleanup,
            "journal_removed": not journal_path.exists(),
        }
    finally:
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--journal",
        type=Path,
        default=project_root
        / "build"
        / "hardware-test-state"
        / "meshcore-private-channel.json",
    )
    parser.add_argument("--recover-only", action="store_true")
    args = parser.parse_args()
    if args.count < 1 or args.count > 20:
        parser.error("--count must be between 1 and 20")

    try:
        result = asyncio.run(
            recover_only(args.port_a, args.port_b, args.journal)
            if args.recover_only
            else run(
                args.port_a,
                args.port_b,
                args.count,
                args.timeout,
                args.journal,
            )
        )
    except Exception as exc:
        print(json.dumps({"success": False, "error": f"{type(exc).__name__}: {exc}"}))
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result.get("success") else 2


if __name__ == "__main__":
    raise SystemExit(main())
