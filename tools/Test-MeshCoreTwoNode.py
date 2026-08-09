"""Run a bounded, read-only-except-for-two-messages MeshCore bench test.

The script verifies that two USB Companion nodes share channel 0, captures
baseline counters, sends one non-sensitive channel marker in each direction, and
reports delivery, latency, RSSI/SNR, duplicates, errors, and airtime deltas.
It never prints node names, public keys, coordinates, or channel secrets.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import secrets
import time
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType


async def snapshot(node: MeshCore) -> dict[str, Any]:
    core = (await node.commands.get_stats_core()).payload
    radio = (await node.commands.get_stats_radio()).payload
    packets = (await node.commands.get_stats_packets()).payload
    return {"core": core, "radio": radio, "packets": packets}


def deltas(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    return {
        "errors": after["core"]["errors"] - before["core"]["errors"],
        "queue_length": after["core"]["queue_len"],
        "packets_sent": after["packets"]["sent"] - before["packets"]["sent"],
        "packets_received": after["packets"]["recv"] - before["packets"]["recv"],
        "receive_errors": after["packets"]["recv_errors"]
        - before["packets"]["recv_errors"],
        "flood_tx": after["packets"]["flood_tx"] - before["packets"]["flood_tx"],
        "flood_rx": after["packets"]["flood_rx"] - before["packets"]["flood_rx"],
        "direct_tx": after["packets"]["direct_tx"] - before["packets"]["direct_tx"],
        "direct_rx": after["packets"]["direct_rx"] - before["packets"]["direct_rx"],
        "tx_airtime_seconds": after["radio"]["tx_air_secs"]
        - before["radio"]["tx_air_secs"],
        "rx_airtime_seconds": after["radio"]["rx_air_secs"]
        - before["radio"]["rx_air_secs"],
        "last_rssi_dbm": after["radio"]["last_rssi"],
        "last_snr_db": after["radio"]["last_snr"],
        "noise_floor_dbm": after["radio"]["noise_floor"],
    }


async def receive_matching(
    receiver: MeshCore, expected: str, timeout: float
) -> tuple[float, Any]:
    deadline = time.perf_counter() + timeout
    while True:
        remaining = deadline - time.perf_counter()
        if remaining <= 0:
            raise TimeoutError(f"Timed out waiting for {expected}")
        event = await receiver.commands.get_msg(timeout=min(remaining, 2.0))
        received_at = time.perf_counter()
        if event is not None and event.type == EventType.CHANNEL_MSG_RECV:
            # MeshCore prepends sender display text to channel
            # messages. Match the unique marker within the returned display
            # string instead of requiring the entire string to be identical.
            if expected in event.payload.get("text", ""):
                return received_at, event
        await asyncio.sleep(0.1)


async def drain_duplicates(receiver: MeshCore, expected: str) -> int:
    duplicates = 0
    while True:
        event = await receiver.commands.get_msg(timeout=2.0)
        if event is None or event.type in (EventType.NO_MORE_MSGS, EventType.ERROR):
            break
        if (
            event.type == EventType.CHANNEL_MSG_RECV
            and expected in event.payload.get("text", "")
        ):
            duplicates += 1
    return duplicates


async def run(port_a: str, port_b: str, timeout: float) -> dict[str, Any]:
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    result: dict[str, Any] = {
        "ports": [port_a, port_b],
        "messages_attempted": 0,
        "messages_delivered": 0,
        "directions": [],
    }

    try:
        node_a = await MeshCore.create_serial(
            port=port_a, only_error=True, default_timeout=10
        )
        node_b = await MeshCore.create_serial(
            port=port_b, only_error=True, default_timeout=10
        )
        if node_a is None or node_b is None:
            raise ConnectionError("One or both MeshCore serial connections failed")

        channel_a = (await node_a.commands.get_channel(0)).payload
        channel_b = (await node_b.commands.get_channel(0)).payload
        channel_match = (
            channel_a["channel_name"] == channel_b["channel_name"]
            and channel_a["channel_secret"] == channel_b["channel_secret"]
        )
        result["channel_0_match"] = channel_match
        if not channel_match:
            raise RuntimeError("Channel 0 configuration does not match; no message sent")

        before_a = await snapshot(node_a)
        before_b = await snapshot(node_b)

        async def send_one(
            sender: MeshCore,
            receiver: MeshCore,
            direction: str,
        ) -> None:
            message = f"MC-TST-{secrets.token_hex(4)}"
            result["messages_attempted"] += 1
            sent_at = time.perf_counter()
            send_event = await sender.commands.send_chan_msg(0, message)
            if send_event.type == EventType.ERROR:
                result["directions"].append(
                    {"direction": direction, "delivered": False, "send_error": True}
                )
                return

            try:
                received_at, event = await receive_matching(
                    receiver, message, timeout
                )
            except TimeoutError:
                result["directions"].append(
                    {"direction": direction, "delivered": False, "timeout": True}
                )
                return

            result["messages_delivered"] += 1
            await asyncio.sleep(1.0)
            receiver_radio = (await receiver.commands.get_stats_radio()).payload
            result["directions"].append(
                {
                    "direction": direction,
                    "delivered": True,
                    "latency_ms": round((received_at - sent_at) * 1000, 1),
                    "duplicates": await drain_duplicates(receiver, message),
                    "reported_snr_db": event.payload.get("SNR"),
                    "last_rssi_dbm": receiver_radio.get("last_rssi"),
                    "last_snr_db": receiver_radio.get("last_snr"),
                }
            )

        await send_one(node_a, node_b, f"{port_a}->{port_b}")
        await send_one(node_b, node_a, f"{port_b}->{port_a}")
        await asyncio.sleep(1.0)

        after_a = await snapshot(node_a)
        after_b = await snapshot(node_b)
        result["node_deltas"] = {
            port_a: deltas(before_a, after_a),
            port_b: deltas(before_b, after_b),
        }
        result["success"] = result["messages_delivered"] == 2
        return result
    finally:
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    try:
        result = asyncio.run(run(args.port_a, args.port_b, args.timeout))
    except Exception as exc:
        print(json.dumps({"success": False, "error": f"{type(exc).__name__}: {exc}"}))
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result.get("success") else 2


if __name__ == "__main__":
    raise SystemExit(main())
