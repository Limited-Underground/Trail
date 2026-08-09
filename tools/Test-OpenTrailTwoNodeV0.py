"""Send C++-encoded OpenTrail v0 probes across two MeshCore USB companions.

The adapter wraps each opaque binary frame in URL-safe base64 because the
current MeshCore companion API exposes channel text, not a stable OpenTrail raw
radio binding. A temporary private channel and ephemeral test identifiers are
created in memory, then erased from both devices in a finally block.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import os
from pathlib import Path
import secrets
import statistics
import subprocess
import time
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType


TEXT_PREFIX = "OT0:"
MAX_ADAPTER_FRAME_BYTES = 96


class PacketCodec:
    def __init__(self, executable: Path) -> None:
        if not executable.is_file():
            raise FileNotFoundError(f"Packet codec CLI was not found: {executable}")
        self.executable = executable
        self.environment = os.environ.copy()
        compiler_runtime = r"C:\msys64\ucrt64\bin"
        self.environment["PATH"] = (
            compiler_runtime + os.pathsep + self.environment.get("PATH", "")
        )

    def _run(self, *arguments: str) -> str:
        completed = subprocess.run(
            [str(self.executable), *arguments],
            check=False,
            capture_output=True,
            text=True,
            env=self.environment,
            timeout=10,
        )
        if completed.returncode != 0:
            reason = completed.stderr.strip() or completed.stdout.strip()
            raise RuntimeError(f"Packet codec failed: {reason}")
        return completed.stdout.strip()

    def encode(
        self,
        source_node_id: int,
        network_id: int,
        message_id: int,
        payload: str,
    ) -> bytes:
        encoded_hex = self._run(
            "encode",
            str(source_node_id),
            str(network_id),
            str(message_id),
            payload,
        )
        return bytes.fromhex(encoded_hex)

    def decode(self, frame: bytes) -> dict[str, Any]:
        return json.loads(self._run("decode", frame.hex()))


class MeshCoreUsbTextTransport:
    def __init__(self, node: MeshCore, channel_index: int) -> None:
        self.node = node
        self.channel_index = channel_index

    @staticmethod
    def wrap(frame: bytes) -> str:
        if not frame or len(frame) > MAX_ADAPTER_FRAME_BYTES:
            raise ValueError(
                f"Adapter frame must contain 1-{MAX_ADAPTER_FRAME_BYTES} bytes"
            )
        encoded = base64.urlsafe_b64encode(frame).decode("ascii").rstrip("=")
        return TEXT_PREFIX + encoded

    @staticmethod
    def unwrap(text: str) -> bytes:
        encoded = text[len(TEXT_PREFIX) :]
        encoded += "=" * ((4 - len(encoded) % 4) % 4)
        return base64.urlsafe_b64decode(encoded)

    async def send(self, frame: bytes) -> str:
        wrapped = self.wrap(frame)
        event = await self.node.commands.send_chan_msg(self.channel_index, wrapped)
        if event is None or event.is_error():
            reason = None if event is None else event.payload.get("reason")
            raise RuntimeError(f"MeshCore rejected frame: {reason or 'no response'}")
        return wrapped

    async def receive_matching(
        self,
        wrapped: str,
        timeout: float,
    ) -> tuple[float, bytes, Any]:
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            remaining = deadline - time.perf_counter()
            event = await self.node.commands.get_msg(timeout=min(remaining, 2.0))
            received_at = time.perf_counter()
            if (
                event is not None
                and event.type == EventType.CHANNEL_MSG_RECV
                and wrapped in event.payload.get("text", "")
            ):
                return received_at, self.unwrap(wrapped), event
            await asyncio.sleep(0.05)
        raise TimeoutError("Timed out waiting for an OpenTrail v0 frame")

    async def drain_duplicates(self, wrapped: str) -> int:
        duplicates = 0
        while True:
            event = await self.node.commands.get_msg(timeout=1.0)
            if event is None or event.type in (EventType.NO_MORE_MSGS, EventType.ERROR):
                return duplicates
            if (
                event.type == EventType.CHANNEL_MSG_RECV
                and wrapped in event.payload.get("text", "")
            ):
                duplicates += 1


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
    codec_path: Path,
) -> dict[str, Any]:
    codec = PacketCodec(codec_path)
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
        for index in range(1, min(len(channels_a), len(channels_b))):
            if (
                channels_a[index].get("channel_name", "") == ""
                and channels_b[index].get("channel_name", "") == ""
            ):
                temporary_index = index
                break
        if temporary_index is None:
            raise RuntimeError("No shared empty channel slot is available")

        temporary_secret = secrets.token_bytes(16)
        channel_name = "OpenTrailV0"
        event_a = await node_a.commands.set_channel(
            temporary_index, channel_name, temporary_secret
        )
        if event_a is None or event_a.is_error():
            raise RuntimeError("Failed to configure temporary channel on first node")
        configured_a = True
        event_b = await node_b.commands.set_channel(
            temporary_index, channel_name, temporary_secret
        )
        if event_b is None or event_b.is_error():
            raise RuntimeError("Failed to configure temporary channel on second node")
        configured_b = True

        verify_a = (await node_a.commands.get_channel(temporary_index)).payload
        verify_b = (await node_b.commands.get_channel(temporary_index)).payload
        if (
            verify_a.get("channel_name") != channel_name
            or verify_b.get("channel_name") != channel_name
            or verify_a.get("channel_secret") != temporary_secret
            or verify_b.get("channel_secret") != temporary_secret
        ):
            raise RuntimeError("Temporary private channel verification failed")

        transports = {
            port_a: MeshCoreUsbTextTransport(node_a, temporary_index),
            port_b: MeshCoreUsbTextTransport(node_b, temporary_index),
        }
        network_id = secrets.randbits(32) or 1
        node_ids = {port_a: secrets.randbits(32) or 1, port_b: secrets.randbits(32) or 1}
        before = {port_a: await snapshot(node_a), port_b: await snapshot(node_b)}
        samples: dict[str, list[dict[str, Any]]] = {
            f"{port_a}->{port_b}": [],
            f"{port_b}->{port_a}": [],
        }

        async def send_probe(
            source_port: str,
            destination_port: str,
            message_id: int,
        ) -> None:
            direction = f"{source_port}->{destination_port}"
            payload = f"OT7-{message_id:02d}-{secrets.token_hex(3)}"
            frame = codec.encode(
                node_ids[source_port], network_id, message_id, payload
            )
            sent_at = time.perf_counter()
            wrapped = await transports[source_port].send(frame)
            try:
                received_at, received_frame, event = await transports[
                    destination_port
                ].receive_matching(wrapped, timeout)
            except TimeoutError:
                samples[direction].append({"delivered": False, "timeout": True})
                return

            decoded = codec.decode(received_frame)
            decoded_payload = bytes.fromhex(decoded["payload_hex"]).decode("utf-8")
            valid = (
                decoded["source_node_id"] == node_ids[source_port]
                and decoded["network_id"] == network_id
                and decoded["message_id"] == message_id
                and decoded["type"] == 0xF0
                and decoded_payload == payload
            )
            await asyncio.sleep(0.2)
            samples[direction].append(
                {
                    "delivered": valid,
                    "latency_ms": round((received_at - sent_at) * 1000, 1),
                    "duplicates": await transports[
                        destination_port
                    ].drain_duplicates(wrapped),
                    "snr_db": event.payload.get("SNR"),
                    "frame_bytes": len(frame),
                    "adapter_text_chars": len(wrapped),
                    "codec_verified": valid,
                }
            )

        for number in range(1, count + 1):
            await send_probe(port_a, port_b, number)
            await send_probe(port_b, port_a, count + number)

        after = {port_a: await snapshot(node_a), port_b: await snapshot(node_b)}
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
                "transport": "MeshCore USB channel text with URL-safe base64",
                "experimental_packet_version": 0,
                "packet_overhead_bytes": 22,
                "count_per_direction": count,
                "directions": summaries,
                "sample_frame_bytes": sorted(
                    {
                        sample["frame_bytes"]
                        for direction_samples in samples.values()
                        for sample in direction_samples
                        if sample.get("delivered")
                    }
                ),
                "sample_adapter_text_chars": sorted(
                    {
                        sample["adapter_text_chars"]
                        for direction_samples in samples.values()
                        for sample in direction_samples
                        if sample.get("delivered")
                    }
                ),
                "node_deltas": {
                    port_a: delta(before[port_a], after[port_a]),
                    port_b: delta(before[port_b], after[port_b]),
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
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--codec",
        type=Path,
        default=project_root / "build" / "host-tests" / "packet_codec_cli.exe",
    )
    args = parser.parse_args()
    if args.count < 1 or args.count > 10:
        parser.error("--count must be between 1 and 10")

    try:
        result = asyncio.run(
            run(args.port_a, args.port_b, args.count, args.timeout, args.codec)
        )
    except Exception as error:
        print(
            json.dumps(
                {"success": False, "error": f"{type(error).__name__}: {error}"}
            )
        )
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result.get("success") else 2


if __name__ == "__main__":
    raise SystemExit(main())
