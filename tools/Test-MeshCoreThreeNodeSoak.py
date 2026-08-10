"""Run a low-duty, crash-recoverable three-node MeshCore bench soak.

Two USB Companions exchange ephemeral private-channel markers. A repeater is
queried through its serial CLI before, during, and after the run. No identity,
public key, coordinate, channel secret, or message marker is persisted.
"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import secrets
import statistics
import time
from typing import Any

from meshcore import MeshCore
from meshcore.events import EventType
import serial

from meshcore_channel_lease import (
    cleanup_lease,
    configure_lease,
    load_journal,
    new_channel_name,
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


async def companion_snapshot(node: MeshCore) -> dict[str, Any]:
    core = (await node.commands.get_stats_core()).payload
    radio = (await node.commands.get_stats_radio()).payload
    packets = (await node.commands.get_stats_packets()).payload
    return {"core": core, "radio": radio, "packets": packets}


def counter_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    return {
        "core_errors": after["core"]["errors"] - before["core"]["errors"],
        "queue_length": after["core"]["queue_len"],
        "sent": after["packets"]["sent"] - before["packets"]["sent"],
        "received": after["packets"]["recv"] - before["packets"]["recv"],
        "receive_errors": after["packets"]["recv_errors"]
        - before["packets"]["recv_errors"],
        "flood_tx": after["packets"]["flood_tx"]
        - before["packets"]["flood_tx"],
        "flood_rx": after["packets"]["flood_rx"]
        - before["packets"]["flood_rx"],
        "direct_tx": after["packets"]["direct_tx"]
        - before["packets"]["direct_tx"],
        "direct_rx": after["packets"]["direct_rx"]
        - before["packets"]["direct_rx"],
        "tx_airtime_seconds": after["radio"]["tx_air_secs"]
        - before["radio"]["tx_air_secs"],
        "rx_airtime_seconds": after["radio"]["rx_air_secs"]
        - before["radio"]["rx_air_secs"],
        "last_rssi_dbm": after["radio"]["last_rssi"],
        "last_snr_db": after["radio"]["last_snr"],
    }


def repeater_command(port: str, command: str) -> str:
    with serial.Serial(port, 115200, timeout=0.2) as connection:
        time.sleep(0.25)
        connection.reset_input_buffer()
        connection.write((command + "\r\n").encode("ascii"))
        connection.flush()
        time.sleep(0.45)
        data = bytearray()
        while connection.in_waiting:
            data.extend(connection.read(connection.in_waiting))
            time.sleep(0.05)
    lines = [
        line.strip()
        for line in data.decode("utf-8", "replace").replace("\r", "").split("\n")
        if line.strip() and line.strip() != command
    ]
    if len(lines) != 1 or "->" not in lines[0]:
        raise RuntimeError(f"Unexpected repeater response to {command}")
    value = lines[0].split("->", 1)[1].strip()
    return value[1:].strip() if value.startswith(">") else value


def repeater_snapshot(port: str) -> dict[str, Any]:
    return {
        "repeat": repeater_command(port, "get repeat"),
        "core": json.loads(repeater_command(port, "stats-core")),
        "radio": json.loads(repeater_command(port, "stats-radio")),
        "packets": json.loads(repeater_command(port, "stats-packets")),
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
    raise TimeoutError("Timed out waiting for a soak marker")


async def drain_duplicates(receiver: MeshCore, marker: str) -> int:
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


def latency_summary(samples: list[dict[str, Any]]) -> dict[str, Any] | None:
    values = sorted(
        sample["latency_ms"] for sample in samples if sample.get("delivered")
    )
    if not values:
        return None
    p95_index = max(0, (95 * len(values) + 99) // 100 - 1)
    return {
        "min": round(values[0], 1),
        "median": round(statistics.median(values), 1),
        "p95": round(values[p95_index], 1),
        "max": round(values[-1], 1),
    }


def summarized_result(
    result: dict[str, Any], samples: list[dict[str, Any]]
) -> dict[str, Any]:
    attempted = len(samples)
    delivered = sum(bool(sample.get("delivered")) for sample in samples)
    result["traffic"] = {
        "attempted": attempted,
        "delivered": delivered,
        "lost": attempted - delivered,
        "duplicates": sum(sample.get("duplicates", 0) for sample in samples),
        "directions": {
            direction: {
                "attempted": sum(
                    sample["direction"] == direction for sample in samples
                ),
                "delivered": sum(
                    sample["direction"] == direction
                    and bool(sample.get("delivered"))
                    for sample in samples
                ),
            }
            for direction in sorted({sample["direction"] for sample in samples})
        },
        "latency_ms": latency_summary(samples),
        "snr_db": None
        if not any(sample.get("snr_db") is not None for sample in samples)
        else {
            "min": min(
                sample["snr_db"]
                for sample in samples
                if sample.get("snr_db") is not None
            ),
            "max": max(
                sample["snr_db"]
                for sample in samples
                if sample.get("snr_db") is not None
            ),
        },
    }
    return result


def write_checkpoint(
    path: Path, result: dict[str, Any], samples: list[dict[str, Any]]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = dict(result)
    summarized_result(payload, samples)
    payload["checkpoint_utc"] = utc_now()
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    temporary.replace(path)


async def run(
    port_a: str,
    port_b: str,
    repeater_port: str,
    duration_minutes: float,
    interval_seconds: float,
    timeout: float,
    journal_path: Path,
    output_path: Path,
) -> dict[str, Any]:
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    cleanup = {port_a: False, port_b: False}
    samples: list[dict[str, Any]] = []
    result: dict[str, Any] = {
        "success": False,
        "test": "low-duty three-node MeshCore bench soak",
        "started_utc": utc_now(),
        "requested_duration_minutes": duration_minutes,
        "interval_seconds": interval_seconds,
        "ports": {
            "companion_a": port_a,
            "companion_b": port_b,
            "repeater": repeater_port,
        },
        "cleanup": cleanup,
    }
    baseline_companions: dict[str, Any] = {}
    baseline_repeater: dict[str, Any] = {}
    started = time.monotonic()

    try:
        node_a = await MeshCore.create_serial(
            port=port_a, only_error=True, default_timeout=10
        )
        node_b = await MeshCore.create_serial(
            port=port_b, only_error=True, default_timeout=10
        )
        if node_a is None or node_b is None:
            raise ConnectionError("One or both MeshCore serial connections failed")
        nodes = {port_a: node_a, port_b: node_b}
        lease = await configure_lease(
            nodes,
            journal_path,
            new_channel_name("OTSoak"),
            secrets.token_bytes(16),
        )
        baseline_companions = {
            port_a: await companion_snapshot(node_a),
            port_b: await companion_snapshot(node_b),
        }
        baseline_repeater = repeater_snapshot(repeater_port)
        result["radio_profile"] = {
            "repeater_repeat_at_start": baseline_repeater["repeat"],
            "temporary_channel_index": lease.channel_index,
        }
        write_checkpoint(output_path, result, samples)

        traffic_started = time.monotonic()
        deadline = traffic_started + duration_minutes * 60.0
        next_send = traffic_started
        sequence = 1
        while time.monotonic() < deadline:
            await asyncio.sleep(max(0.0, next_send - time.monotonic()))
            if time.monotonic() >= deadline:
                break
            if sequence % 2:
                sender, receiver = node_a, node_b
                direction = f"{port_a}->{port_b}"
            else:
                sender, receiver = node_b, node_a
                direction = f"{port_b}->{port_a}"
            marker = f"OTS-{sequence:04d}-{secrets.token_hex(3)}"
            sample: dict[str, Any] = {
                "sequence": sequence,
                "direction": direction,
                "delivered": False,
            }
            try:
                sent_at = time.perf_counter()
                send_event = await sender.commands.send_chan_msg(
                    lease.channel_index, marker
                )
                if send_event is None or send_event.is_error():
                    sample["send_error"] = True
                else:
                    received_at, event = await wait_for_marker(
                        receiver, marker, timeout
                    )
                    sample.update(
                        {
                            "delivered": True,
                            "latency_ms": round(
                                (received_at - sent_at) * 1000, 1
                            ),
                            "snr_db": event.payload.get("SNR"),
                            "duplicates": await drain_duplicates(receiver, marker),
                        }
                    )
            except TimeoutError:
                sample["timeout"] = True
            except Exception as error:
                sample["error_type"] = type(error).__name__
            samples.append(sample)
            print(
                json.dumps(
                    {
                        "progress": sequence,
                        "elapsed_minutes": round(
                            (time.monotonic() - traffic_started) / 60.0, 1
                        ),
                        "direction": direction,
                        "delivered": sample["delivered"],
                    }
                ),
                flush=True,
            )
            if sequence % 10 == 0:
                result["latest_repeater_checkpoint"] = repeater_snapshot(
                    repeater_port
                )
            write_checkpoint(output_path, result, samples)
            sequence += 1
            next_send += interval_seconds

        final_companions = {
            port_a: await companion_snapshot(node_a),
            port_b: await companion_snapshot(node_b),
        }
        final_repeater = repeater_snapshot(repeater_port)
        result["companion_deltas"] = {
            port: counter_delta(baseline_companions[port], final_companions[port])
            for port in (port_a, port_b)
        }
        result["repeater_delta"] = counter_delta(
            baseline_repeater, final_repeater
        )
        result["radio_profile"]["repeater_repeat_at_end"] = final_repeater[
            "repeat"
        ]
    finally:
        if journal_path.exists():
            try:
                record = load_journal(journal_path)
                connected = {
                    port: node
                    for port, node in ((port_a, node_a), (port_b, node_b))
                    if node is not None
                }
                cleanup = await cleanup_lease(
                    connected, record, journal_path
                )
            except Exception:
                cleanup = {port_a: False, port_b: False}
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()

    result["cleanup"] = cleanup
    result["completed_utc"] = utc_now()
    result["elapsed_minutes"] = round((time.monotonic() - started) / 60.0, 2)
    summarized_result(result, samples)
    deltas = result.get("companion_deltas", {})
    result["success"] = bool(
        samples
        and result["traffic"]["lost"] == 0
        and result["traffic"]["duplicates"] == 0
        and all(cleanup.values())
        and all(delta.get("core_errors") == 0 for delta in deltas.values())
        and all(delta.get("receive_errors") == 0 for delta in deltas.values())
        and result.get("repeater_delta", {}).get("core_errors") == 0
        and result.get("repeater_delta", {}).get("receive_errors") == 0
        and result.get("radio_profile", {}).get("repeater_repeat_at_end") == "on"
    )
    write_checkpoint(output_path, result, samples)
    return result


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--repeater-port", required=True)
    parser.add_argument("--duration-minutes", type=float, default=60.0)
    parser.add_argument("--interval-seconds", type=float, default=60.0)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--journal",
        type=Path,
        default=project_root
        / "build"
        / "hardware-test-state"
        / "meshcore-soak-channel.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=project_root
        / "build"
        / "hardware-test-state"
        / "meshcore-three-node-soak.json",
    )
    args = parser.parse_args()
    if not 0.1 <= args.duration_minutes <= 480:
        parser.error("--duration-minutes must be between 0.1 and 480")
    if not 2 <= args.interval_seconds <= 3600:
        parser.error("--interval-seconds must be between 2 and 3600")
    if not 2 <= args.timeout <= 60:
        parser.error("--timeout must be between 2 and 60")

    try:
        result = asyncio.run(
            run(
                args.port_a,
                args.port_b,
                args.repeater_port,
                args.duration_minutes,
                args.interval_seconds,
                args.timeout,
                args.journal,
                args.output,
            )
        )
    except Exception as error:
        print(
            json.dumps(
                {"success": False, "error": f"{type(error).__name__}: {error}"}
            ),
            flush=True,
        )
        return 1

    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return 0 if result.get("success") else 2


if __name__ == "__main__":
    raise SystemExit(main())
