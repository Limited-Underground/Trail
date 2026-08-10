"""Diagnose one MeshCore channel packet without exposing sensitive material.

The script sends one short non-sensitive channel-0 marker, observes the receiving
companion's raw RX log, validates/decrypts it using the receiver's own channel
table, and checks the firmware message queue. It prints only boolean outcomes
and non-sensitive radio metadata.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import secrets
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType


async def run(sender_port: str, receiver_port: str, timeout: float) -> dict[str, Any]:
    sender: MeshCore | None = None
    receiver: MeshCore | None = None
    try:
        sender = await MeshCore.create_serial(
            port=sender_port, only_error=True, default_timeout=10
        )
        receiver = await MeshCore.create_serial(
            port=receiver_port, only_error=True, default_timeout=10
        )
        if sender is None or receiver is None:
            raise ConnectionError("One or both MeshCore serial connections failed")

        sender_channel = (await sender.commands.get_channel(0)).payload
        receiver_channel = (await receiver.commands.get_channel(0)).payload
        channel_match = (
            sender_channel.get("channel_name") == receiver_channel.get("channel_name")
            and sender_channel.get("channel_secret")
            == receiver_channel.get("channel_secret")
        )
        if not channel_match:
            raise RuntimeError("Channel 0 configuration does not match; no packet sent")

        receiver.set_decrypt_channel_logs(True)
        message = f"MC-RX-{secrets.token_hex(4)}"

        raw_task = asyncio.create_task(
            receiver.wait_for_event(EventType.RX_LOG_DATA, timeout=timeout)
        )
        waiting_task = asyncio.create_task(
            receiver.wait_for_event(EventType.MESSAGES_WAITING, timeout=timeout)
        )
        await asyncio.sleep(0)

        send_event = await sender.commands.send_chan_msg(0, message)
        if send_event is None or send_event.is_error():
            raise RuntimeError("Companion rejected the channel send command")

        raw_event = await raw_task
        waiting_event = await waiting_task
        queue_event = await receiver.commands.get_msg(timeout=3)

        raw_payload = {} if raw_event is None else raw_event.payload
        queue_payload = {} if queue_event is None else queue_event.payload
        return {
            "success": True,
            "sender_port": sender_port,
            "receiver_port": receiver_port,
            "channel_0_match": channel_match,
            "raw_frame_observed": raw_event is not None,
            "raw_payload_type": raw_payload.get("payload_typename"),
            "raw_route_type": raw_payload.get("route_typename"),
            "raw_channel_hash_matched_receiver_table": "chan_name" in raw_payload,
            "raw_channel_mac_valid": "chan_name" in raw_payload,
            "raw_payload_decrypted": "message" in raw_payload,
            "raw_decrypted_payload_equal": raw_payload.get("message") == message,
            "raw_decrypted_payload_contains_marker": message
            in raw_payload.get("message", ""),
            "raw_snr_db": raw_payload.get("snr"),
            "raw_rssi_dbm": raw_payload.get("rssi"),
            "messages_waiting_push_observed": waiting_event is not None,
            "queue_event_type": None if queue_event is None else queue_event.type.value,
            "queue_payload_equal": queue_payload.get("text") == message,
            "queue_payload_contains_marker": message
            in queue_payload.get("text", ""),
        }
    finally:
        for node in (sender, receiver):
            if node is not None:
                await node.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sender", required=True)
    parser.add_argument("--receiver", required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    try:
        result = asyncio.run(run(args.sender, args.receiver, args.timeout))
    except Exception as exc:
        print(json.dumps({"success": False, "error": f"{type(exc).__name__}: {exc}"}))
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
