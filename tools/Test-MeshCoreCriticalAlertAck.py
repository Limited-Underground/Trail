"""Run one host-mediated OGA0/OGK0 round trip over two MeshCore companions.

The SenseCAP repeater is observed read-only. The temporary channel secret is
generated in memory, never printed or written, and an exact-name non-secret
lease journal makes cleanup recoverable. This is transport evidence only: the
bench host supplies the authenticated/authorized ingress context.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import csv
import json
import os
from pathlib import Path
import secrets
import subprocess
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


ALERT_PREFIX = "OGA0:"
ACK_PREFIX = "OGK0:"


class AlertBridgeCodec:
    def __init__(self, executable: Path) -> None:
        if not executable.is_file():
            raise FileNotFoundError(f"Alert bridge CLI was not found: {executable}")
        self.executable = executable
        self.environment = os.environ.copy()
        runtime = r"C:\msys64\ucrt64\bin"
        self.environment["PATH"] = (
            runtime + os.pathsep + self.environment.get("PATH", "")
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
            raise RuntimeError(f"Alert bridge codec failed: {reason}")
        return completed.stdout.strip()

    def respond_accepted(
        self,
        alert: bytes,
        producer_id: int,
        consumer_id: int,
        boot_session: int,
        ack_sequence: int,
    ) -> bytes:
        output = self._run(
            "respond-accepted",
            alert.hex(),
            str(producer_id),
            str(consumer_id),
            str(boot_session),
            str(ack_sequence),
            "5000",
            "5005",
        )
        return bytes.fromhex(output)

    def respond_stale(
        self,
        alert: bytes,
        producer_id: int,
        consumer_id: int,
        boot_session: int,
        ack_sequence: int,
    ) -> bytes:
        output = self._run(
            "respond-stale",
            alert.hex(),
            str(producer_id),
            str(consumer_id),
            str(boot_session),
            str(ack_sequence),
            "5000",
            "5005",
        )
        return bytes.fromhex(output)

    def respond_rate_limited(
        self,
        alert: bytes,
        producer_id: int,
        consumer_id: int,
        boot_session: int,
        ack_sequence: int,
    ) -> bytes:
        output = self._run(
            "respond-rate-limited",
            alert.hex(),
            str(producer_id),
            str(consumer_id),
            str(boot_session),
            str(ack_sequence),
            "5000",
            "5005",
        )
        return bytes.fromhex(output)

    def decode_ack(self, acknowledgement: bytes) -> dict[str, Any]:
        return json.loads(self._run("decode-ack", acknowledgement.hex()))


class OpenGaugeRoundTripVerifier:
    def __init__(self, executable: Path) -> None:
        if not executable.is_file():
            raise FileNotFoundError(
                f"OpenGauge round-trip CLI was not found: {executable}"
            )
        self.executable = executable
        self.environment = os.environ.copy()
        runtime = r"C:\msys64\ucrt64\bin"
        self.environment["PATH"] = (
            runtime + os.pathsep + self.environment.get("PATH", "")
        )

    def verify(
        self,
        outcome: str,
        alert: bytes,
        acknowledgement: bytes,
        consumer_id: int,
        boot_session: int,
        logical_peer_id: int,
        key_handle: int,
        channel: int,
    ) -> dict[str, Any]:
        completed = subprocess.run(
            [
                str(self.executable),
                f"verify-{outcome}",
                alert.hex(),
                acknowledgement.hex(),
                str(consumer_id),
                str(boot_session),
                str(logical_peer_id),
                str(key_handle),
                str(channel),
            ],
            check=False,
            capture_output=True,
            text=True,
            env=self.environment,
            timeout=10,
        )
        if completed.returncode != 0:
            reason = completed.stderr.strip() or completed.stdout.strip()
            raise RuntimeError(f"OpenGauge round-trip verification failed: {reason}")
        return json.loads(completed.stdout)

    def verify_retry_then_accepted(
        self,
        alert: bytes,
        rejection: bytes,
        accepted: bytes,
        consumer_id: int,
        boot_session: int,
        logical_peer_id: int,
        key_handle: int,
        channel: int,
    ) -> dict[str, Any]:
        completed = subprocess.run(
            [
                str(self.executable),
                "verify-retry-then-accepted",
                alert.hex(),
                rejection.hex(),
                accepted.hex(),
                str(consumer_id),
                str(boot_session),
                str(logical_peer_id),
                str(key_handle),
                str(channel),
            ],
            check=False,
            capture_output=True,
            text=True,
            env=self.environment,
            timeout=10,
        )
        if completed.returncode != 0:
            reason = completed.stderr.strip() or completed.stdout.strip()
            raise RuntimeError(f"OpenGauge retry verification failed: {reason}")
        return json.loads(completed.stdout)

    def start_live_retry(
        self,
        alert: bytes,
        consumer_id: int,
        boot_session: int,
        logical_peer_id: int,
        key_handle: int,
        channel: int,
    ) -> "OpenGaugeLiveRetrySession":
        return OpenGaugeLiveRetrySession(
            self.executable,
            self.environment,
            alert,
            consumer_id,
            boot_session,
            logical_peer_id,
            key_handle,
            channel,
        )


class OpenGaugeLiveRetrySession:
    def __init__(
        self,
        executable: Path,
        environment: dict[str, str],
        alert: bytes,
        consumer_id: int,
        boot_session: int,
        logical_peer_id: int,
        key_handle: int,
        channel: int,
    ) -> None:
        self.process = subprocess.Popen(
            [
                str(executable),
                "live-retry-session",
                alert.hex(),
                str(consumer_id),
                str(boot_session),
                str(logical_peer_id),
                str(key_handle),
                str(channel),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )
        self.ready = self._read()
        if self.ready.get("ready") is not True:
            self.close()
            raise RuntimeError("OpenGauge live retry session was not ready")

    def _read(self) -> dict[str, Any]:
        if self.process.stdout is None:
            raise RuntimeError("OpenGauge live session has no output pipe")
        line = self.process.stdout.readline()
        if not line:
            reason = ""
            if self.process.stderr is not None:
                reason = self.process.stderr.read().strip()
            raise RuntimeError(f"OpenGauge live session stopped: {reason}")
        return json.loads(line)

    def _send(self, command: str, acknowledgement: bytes) -> dict[str, Any]:
        if self.process.stdin is None:
            raise RuntimeError("OpenGauge live session has no input pipe")
        self.process.stdin.write(f"{command} {acknowledgement.hex()}\n")
        self.process.stdin.flush()
        return self._read()

    def reject(self, acknowledgement: bytes) -> dict[str, Any]:
        return self._send("reject", acknowledgement)

    def accept(self, acknowledgement: bytes) -> dict[str, Any]:
        result = self._send("accept", acknowledgement)
        if self.process.stdin is not None:
            self.process.stdin.close()
        return_code = self.process.wait(timeout=10)
        if return_code != 0:
            reason = ""
            if self.process.stderr is not None:
                reason = self.process.stderr.read().strip()
            raise RuntimeError(f"OpenGauge live session failed: {reason}")
        return result

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)


def wrap(prefix: str, frame: bytes) -> str:
    encoded = base64.urlsafe_b64encode(frame).decode("ascii").rstrip("=")
    return prefix + encoded


def unwrap(prefix: str, text: str) -> bytes:
    encoded = text[len(prefix) :]
    encoded += "=" * ((4 - len(encoded) % 4) % 4)
    return base64.urlsafe_b64decode(encoded)


async def receive_exact(
    node: MeshCore,
    prefix: str,
    expected_text: str,
    timeout: float,
) -> tuple[float, bytes, Any]:
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        remaining = deadline - time.perf_counter()
        event = await node.commands.get_msg(timeout=min(remaining, 2.0))
        received_at = time.perf_counter()
        if (
            event is not None
            and event.type == EventType.CHANNEL_MSG_RECV
            and expected_text in event.payload.get("text", "")
        ):
            return received_at, unwrap(prefix, expected_text), event
        await asyncio.sleep(0.05)
    raise TimeoutError(f"Timed out waiting for {prefix[:4]} frame")


async def drain_exact(node: MeshCore, expected_text: str) -> int:
    duplicates = 0
    while True:
        event = await node.commands.get_msg(timeout=1.0)
        if event is None or event.type in (EventType.NO_MORE_MSGS, EventType.ERROR):
            return duplicates
        if (
            event.type == EventType.CHANNEL_MSG_RECV
            and expected_text in event.payload.get("text", "")
        ):
            duplicates += 1


async def companion_snapshot(node: MeshCore) -> dict[str, Any]:
    core = (await node.commands.get_stats_core()).payload
    radio = (await node.commands.get_stats_radio()).payload
    packets = (await node.commands.get_stats_packets()).payload
    return {"core": core, "radio": radio, "packets": packets}


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
        "packets": json.loads(repeater_command(port, "stats-packets")),
    }


def companion_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, int]:
    return {
        "core_errors": after["core"]["errors"] - before["core"]["errors"],
        "queue_length": after["core"]["queue_len"],
        "sent": after["packets"]["sent"] - before["packets"]["sent"],
        "received": after["packets"]["recv"] - before["packets"]["recv"],
        "receive_errors": after["packets"]["recv_errors"]
        - before["packets"]["recv_errors"],
        "flood_tx": after["packets"]["flood_tx"] - before["packets"]["flood_tx"],
        "flood_rx": after["packets"]["flood_rx"] - before["packets"]["flood_rx"],
    }


def repeater_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, int]:
    return {
        "core_errors": after["core"]["errors"] - before["core"]["errors"],
        "receive_errors": after["packets"]["recv_errors"]
        - before["packets"]["recv_errors"],
        "flood_tx": after["packets"]["flood_tx"] - before["packets"]["flood_tx"],
        "flood_rx": after["packets"]["flood_rx"] - before["packets"]["flood_rx"],
    }


def load_fixture(path: Path) -> dict[str, Any]:
    with path.open(newline="", encoding="utf-8") as handle:
        row = next(csv.DictReader(handle))
    return {
        "name": row["name"],
        "frame": bytes.fromhex(row["hex"]),
        "producer_id": int(row["producer_id"]),
        "event_id": int(row["event_id"]),
        "condition_id": int(row["condition_id"]),
        "state": int(row["state"]),
    }


async def run(
    port_a: str,
    port_b: str,
    repeater_port: str,
    timeout: float,
    codec_path: Path,
    opengauge_cli_path: Path | None,
    ack_outcome: str,
    fixture_path: Path,
    journal_path: Path,
) -> dict[str, Any]:
    codec = AlertBridgeCodec(codec_path)
    opengauge = (
        None
        if opengauge_cli_path is None
        else OpenGaugeRoundTripVerifier(opengauge_cli_path)
    )
    fixture = load_fixture(fixture_path)
    node_a: MeshCore | None = None
    node_b: MeshCore | None = None
    live_session: OpenGaugeLiveRetrySession | None = None
    cleanup = {port_a: False, port_b: False}
    result: dict[str, Any] = {
        "success": False,
        "test": "host-mediated OGA0 alert and correlated OGK0 ACK over MeshCore",
        "fixture": fixture["name"],
        "cleanup": cleanup,
        "limitations": [
            "host supplies authenticated and authorized ingress context",
            "MeshCore channel text uses URL-safe base64 adapter",
            "close-bench flood traffic does not prove the route required the repeater",
        ],
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
        nodes = {port_a: node_a, port_b: node_b}
        lease = await configure_lease(
            nodes,
            journal_path,
            new_channel_name("OTACK"),
            secrets.token_bytes(16),
        )

        companion_before = {
            port_a: await companion_snapshot(node_a),
            port_b: await companion_snapshot(node_b),
        }
        repeater_before = repeater_snapshot(repeater_port)

        consumer_id = secrets.randbits(64) or 1
        boot_session = secrets.randbits(32) or 1
        ack_sequence = secrets.randbits(32)
        logical_peer_id = secrets.randbits(32) or 1
        key_handle = secrets.randbits(32) or 1
        if ack_outcome == "retry-then-accepted" and opengauge is not None:
            live_session = opengauge.start_live_retry(
                fixture["frame"],
                consumer_id,
                boot_session,
                logical_peer_id,
                key_handle,
                lease.channel_index,
            )

        alert_text = wrap(ALERT_PREFIX, fixture["frame"])
        alert_sent = time.perf_counter()
        sent = await node_a.commands.send_chan_msg(lease.channel_index, alert_text)
        if sent is None or sent.is_error():
            raise RuntimeError("MeshCore rejected the OGA0 alert")
        alert_received, alert_frame, alert_event = await receive_exact(
            node_b, ALERT_PREFIX, alert_text, timeout
        )
        if alert_frame != fixture["frame"]:
            raise RuntimeError("Received OGA0 bytes differ from the sent fixture")

        policy_outcome = (
            "rate-limited"
            if ack_outcome == "retry-then-accepted"
            else ack_outcome
        )
        responder = {
            "accepted": codec.respond_accepted,
            "stale": codec.respond_stale,
            "rate-limited": codec.respond_rate_limited,
        }[policy_outcome]
        ack_frame = responder(
            alert_frame,
            fixture["producer_id"],
            consumer_id,
            boot_session,
            ack_sequence,
        )
        ack_text = wrap(ACK_PREFIX, ack_frame)
        ack_sent = time.perf_counter()
        sent = await node_b.commands.send_chan_msg(lease.channel_index, ack_text)
        if sent is None or sent.is_error():
            raise RuntimeError("MeshCore rejected the OGK0 acknowledgement")
        ack_received, returned_ack, ack_event = await receive_exact(
            node_a, ACK_PREFIX, ack_text, timeout
        )
        decoded_ack = codec.decode_ack(returned_ack)
        opengauge_result = (
            None
            if opengauge is None
            else opengauge.verify(
                policy_outcome,
                alert_frame,
                returned_ack,
                consumer_id,
                boot_session,
                logical_peer_id,
                key_handle,
                lease.channel_index,
            )
        )

        expected_result = {
            "accepted": {
                "accepted_none": decoded_ack["disposition"] == 1
                and decoded_ack["reason"] == 0
            },
            "stale": {
                "rejected_stale": decoded_ack["disposition"] == 2
                and decoded_ack["reason"] == 2
            },
            "rate-limited": {
                "rejected_rate_limited": decoded_ack["disposition"] == 2
                and decoded_ack["reason"] == 5
            },
        }[policy_outcome]
        correlation = {
            **expected_result,
            "consumer": decoded_ack["consumer_id"] == consumer_id,
            "producer": decoded_ack["producer_id"] == fixture["producer_id"],
            "event": decoded_ack["event_id"] == fixture["event_id"],
            "condition": decoded_ack["condition_id"] == fixture["condition_id"],
            "state": decoded_ack["state"] == fixture["state"],
            "session_sequence": decoded_ack["boot_session"] == boot_session
            and decoded_ack["ack_sequence"] == ack_sequence,
            "age": decoded_ack["observed_age_ms"] == 1255,
        }

        retry_evidence: dict[str, Any] | None = None
        sequence_composition: dict[str, Any] | None = None
        live_rejection: dict[str, Any] | None = None
        live_final: dict[str, Any] | None = None
        retry_success = True
        if ack_outcome == "retry-then-accepted":
            live_rejection = (
                None if live_session is None else live_session.reject(returned_ack)
            )
            retry_alert_sent = time.perf_counter()
            sent = await node_a.commands.send_chan_msg(
                lease.channel_index, alert_text
            )
            if sent is None or sent.is_error():
                raise RuntimeError("MeshCore rejected the retry OGA0 alert")
            retry_alert_received, retry_alert_frame, retry_alert_event = (
                await receive_exact(node_b, ALERT_PREFIX, alert_text, timeout)
            )
            if retry_alert_frame != alert_frame:
                raise RuntimeError("Retry OGA0 bytes differ from the original")

            retry_sequence = (ack_sequence + 1) & 0xFFFFFFFF
            accepted_frame = codec.respond_accepted(
                retry_alert_frame,
                fixture["producer_id"],
                consumer_id,
                boot_session,
                retry_sequence,
            )
            accepted_text = wrap(ACK_PREFIX, accepted_frame)
            accepted_sent = time.perf_counter()
            sent = await node_b.commands.send_chan_msg(
                lease.channel_index, accepted_text
            )
            if sent is None or sent.is_error():
                raise RuntimeError("MeshCore rejected the retry OGK0 ACK")
            accepted_received, returned_accepted, accepted_event = (
                await receive_exact(node_a, ACK_PREFIX, accepted_text, timeout)
            )
            decoded_accepted = codec.decode_ack(returned_accepted)
            retry_correlation = {
                "accepted_none": decoded_accepted["disposition"] == 1
                and decoded_accepted["reason"] == 0,
                "consumer": decoded_accepted["consumer_id"] == consumer_id,
                "producer": decoded_accepted["producer_id"]
                == fixture["producer_id"],
                "event": decoded_accepted["event_id"] == fixture["event_id"],
                "condition": decoded_accepted["condition_id"]
                == fixture["condition_id"],
                "state": decoded_accepted["state"] == fixture["state"],
                "session_sequence": decoded_accepted["boot_session"]
                == boot_session
                and decoded_accepted["ack_sequence"] == retry_sequence,
                "age": decoded_accepted["observed_age_ms"] == 1255,
            }
            sequence_composition = (
                None
                if opengauge is None
                else opengauge.verify_retry_then_accepted(
                    alert_frame,
                    returned_ack,
                    returned_accepted,
                    consumer_id,
                    boot_session,
                    logical_peer_id,
                    key_handle,
                    lease.channel_index,
                )
            )
            live_final = (
                None
                if live_session is None
                else live_session.accept(returned_accepted)
            )
            live_success = (
                live_session is None
                or (
                    live_rejection is not None
                    and live_rejection.get("rejection_processed") is True
                    and live_rejection.get("retry_released") is True
                    and live_rejection.get("not_ready_before_backoff") is True
                    and live_rejection.get("retry_prepared_same_frame") is True
                    and live_rejection.get("in_flight_count") == 1
                    and live_final is not None
                    and live_final.get("accepted_processed") is True
                    and live_final.get("outbox_completed") is True
                    and live_final.get("queued_count") == 0
                    and live_final.get("in_flight_count") == 0
                    and live_final.get("acknowledgements") == 1
                    and live_final.get("remote_retries") == 1
                    and live_final.get("terminal_failures") == 0
                )
            )
            retry_success = all(retry_correlation.values()) and live_success and (
                sequence_composition is None
                or (
                    sequence_composition.get("rejection_processed") is True
                    and sequence_composition.get("retry_released") is True
                    and sequence_composition.get("not_ready_before_backoff") is True
                    and sequence_composition.get("retry_prepared_same_frame") is True
                    and sequence_composition.get("accepted_processed") is True
                    and sequence_composition.get("outbox_completed") is True
                    and sequence_composition.get("queued_count") == 0
                    and sequence_composition.get("in_flight_count") == 0
                    and sequence_composition.get("acknowledgements") == 1
                    and sequence_composition.get("remote_retries") == 1
                    and sequence_composition.get("terminal_failures") == 0
                )
            )
            retry_evidence = {
                "correlation": retry_correlation,
                "opengauge_sequence": sequence_composition,
                "opengauge_live": {
                    "ready": None if live_session is None else live_session.ready,
                    "rejection": live_rejection,
                    "final": live_final,
                },
                "latency_ms": {
                    "alert": round(
                        (retry_alert_received - retry_alert_sent) * 1000, 1
                    ),
                    "ack": round((accepted_received - accepted_sent) * 1000, 1),
                    "round_trip": round(
                        (accepted_received - retry_alert_sent) * 1000, 1
                    ),
                },
                "snr_db": {
                    "alert": retry_alert_event.payload.get("SNR"),
                    "ack": accepted_event.payload.get("SNR"),
                },
            }
        alert_duplicates = await drain_exact(node_b, alert_text)
        ack_duplicates = await drain_exact(node_a, ack_text)

        companion_after = {
            port_a: await companion_snapshot(node_a),
            port_b: await companion_snapshot(node_b),
        }
        repeater_after = repeater_snapshot(repeater_port)
        companion_deltas = {
            port_a: companion_delta(companion_before[port_a], companion_after[port_a]),
            port_b: companion_delta(companion_before[port_b], companion_after[port_b]),
        }
        relay_delta = repeater_delta(repeater_before, repeater_after)
        result.update(
            {
                "temporary_channel_index": lease.channel_index,
                "ack_outcome": ack_outcome,
                "frame_bytes": {"alert": len(alert_frame), "ack": len(returned_ack)},
                "adapter_text_chars": {"alert": len(alert_text), "ack": len(ack_text)},
                "latency_ms": {
                    "alert": round((alert_received - alert_sent) * 1000, 1),
                    "ack": round((ack_received - ack_sent) * 1000, 1),
                    "round_trip": round((ack_received - alert_sent) * 1000, 1),
                },
                "snr_db": {
                    "alert": alert_event.payload.get("SNR"),
                    "ack": ack_event.payload.get("SNR"),
                },
                "duplicates": {"alert": alert_duplicates, "ack": ack_duplicates},
                "correlation": correlation,
                "opengauge_composition": opengauge_result,
                "retry": retry_evidence,
                "companion_deltas": companion_deltas,
                "repeater": {
                    "repeat_start": repeater_before["repeat"],
                    "repeat_end": repeater_after["repeat"],
                    "delta": relay_delta,
                },
            }
        )
        result["success"] = (
            all(correlation.values())
            and (
                opengauge_result is None
                or (
                    opengauge_result.get("processed") is True
                    and (
                        (
                            policy_outcome == "accepted"
                            and opengauge_result.get("outbox_completed") is True
                            and opengauge_result.get("accepted_none") is True
                            and opengauge_result.get("acknowledgements") == 1
                            and opengauge_result.get("queued_count") == 0
                            and opengauge_result.get("in_flight_count") == 0
                        )
                        or (
                            policy_outcome == "stale"
                            and opengauge_result.get("outbox_completed") is False
                            and opengauge_result.get("rejected_stale") is True
                            and opengauge_result.get("retry_released") is False
                            and opengauge_result.get("terminal_failure") is True
                            and opengauge_result.get("acknowledgements") == 0
                            and opengauge_result.get("queued_count") == 0
                            and opengauge_result.get("in_flight_count") == 0
                        )
                        or (
                            policy_outcome == "rate-limited"
                            and opengauge_result.get("outbox_completed") is False
                            and opengauge_result.get("rejected_rate_limited") is True
                            and opengauge_result.get("retry_released") is True
                            and opengauge_result.get("terminal_failure") is False
                            and opengauge_result.get("acknowledgements") == 0
                            and opengauge_result.get("queued_count") == 1
                            and opengauge_result.get("in_flight_count") == 0
                        )
                    )
                )
            )
            and retry_success
            and alert_duplicates == 0
            and ack_duplicates == 0
            and all(
                delta["core_errors"] == 0
                and delta["receive_errors"] == 0
                and delta["queue_length"] == 0
                for delta in companion_deltas.values()
            )
            and relay_delta["core_errors"] == 0
            and relay_delta["receive_errors"] == 0
            and repeater_after["repeat"] == "on"
        )
    finally:
        if live_session is not None:
            live_session.close()
        if journal_path.exists():
            try:
                record = load_journal(journal_path)
                connected = {
                    port: node
                    for port, node in ((port_a, node_a), (port_b, node_b))
                    if node is not None
                }
                cleanup = await cleanup_lease(connected, record, journal_path)
            except Exception:
                cleanup = {port_a: False, port_b: False}
        for node in (node_a, node_b):
            if node is not None:
                await node.disconnect()

    result["cleanup"] = cleanup
    result["journal_removed"] = not journal_path.exists()
    result["success"] = (
        bool(result.get("success"))
        and all(cleanup.values())
        and result["journal_removed"]
    )
    return result


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--repeater-port", required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--codec", type=Path, required=True)
    parser.add_argument(
        "--opengauge-cli",
        type=Path,
        help="Optional OpenGauge outbox/authorization/ACK-ingress verifier",
    )
    parser.add_argument(
        "--ack-outcome",
        choices=(
            "accepted",
            "stale",
            "rate-limited",
            "retry-then-accepted",
        ),
        default="accepted",
        help="OpenTrail response outcome to exercise (default: accepted)",
    )
    parser.add_argument(
        "--fixture",
        type=Path,
        default=root / "tests" / "fixtures" / "critical_alert_v0_vectors.csv",
    )
    parser.add_argument(
        "--journal",
        type=Path,
        default=root
        / "build"
        / "hardware-test-state"
        / "meshcore-critical-alert-ack-channel.json",
    )
    args = parser.parse_args()

    try:
        result = asyncio.run(
            run(
                args.port_a,
                args.port_b,
                args.repeater_port,
                args.timeout,
                args.codec,
                args.opengauge_cli,
                args.ack_outcome,
                args.fixture,
                args.journal,
            )
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
