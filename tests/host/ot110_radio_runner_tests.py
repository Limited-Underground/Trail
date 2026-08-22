#!/usr/bin/env python3
"""Deterministic fake-serial coverage for the privacy-safe OT-110 runner."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import sys
import unittest
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ot110_radio_runner", ROOT / "tools" / "ot110_radio_runner.py"
)
assert SPEC is not None and SPEC.loader is not None
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


def ack_payload(role: str, direction: str, session: int, sequence: int) -> bytes:
    return (
        b"OTA1"
        + bytes((1, 1 if role == "A" else 2, 1 if direction == "A>B" else 2, 16))
        + session.to_bytes(4, "little")
        + sequence.to_bytes(4, "little")
    )


class FakeEndpoint:
    def __init__(self, world: "FakeWorld", role: str) -> None:
        self.world = world
        self.role = role
        self.receipts: deque[runner.Receipt] = deque()
        self.run = 100 if role == "A" else 200
        self.counters = {key: 0 for key in runner.COUNTER_KEYS}
        self.ack_role: str | None = None
        self.ack_session = 0
        self.ack_remaining = 0
        self.control_session = 0
        self.closed = False
        self._queue_boot()

    @property
    def peer(self) -> "FakeEndpoint":
        return self.world.b if self.role == "A" else self.world.a

    def _emit(self, event_kind: str, **fields: object) -> None:
        self.world.clock += 0.001
        mono_us = int(self.world.clock * 1_000_000)
        if event_kind == "TX":
            fields = {"kind": fields.pop("kind"), "result": fields.pop("result"),
                      "mono_us": mono_us, **fields}
        elif event_kind == "RX":
            fields = {"kind": fields.pop("kind"), "valid": fields.pop("valid"),
                      "mono_us": mono_us, **fields}
        elif event_kind == "RX_ERROR":
            fields = {"result": fields.pop("result"), "mono_us": mono_us, **fields}
        ordered = " ".join(f"{key}={value}" for key, value in fields.items())
        receipt = runner.parse_receipt(f"I (1) fake: OTD {event_kind} {ordered}", self.world.clock)
        assert receipt is not None
        self.receipts.append(receipt)

    def _queue_boot(self) -> None:
        self._emit("BOOT", run=self.run, reset=1, tx_at_boot="no")
        self._emit_profile_status()

    def _emit_profile_status(self) -> None:
        profile = dict(
            run=self.run, configured="yes", begin=0, header=0, crc=0, ldro=0,
            frequency_hz=915000000, bandwidth_hz=125000, sf=7, cr_denom=5,
            power_dbm=2, preamble=8, explicit="yes", crc_enabled="yes",
            ldro_mode="off", sync="0x12", scope="driver_command_acceptance",
            calibrated="no",
        )
        if self.world.mode == "profile" and self.role == "B":
            profile["sf"] = 8
        if self.world.mode == "restart_profile" and self.world.restarted and self.role == "B":
            profile["sf"] = 8
        self._emit("PROFILE", **profile)
        self._emit_status()

    def _emit_status(self) -> None:
        self._emit(
            "STATUS", run=self.run, ready="yes", armed="no",
            ack_armed="yes" if self.ack_remaining else "no",
            ack_remaining=self.ack_remaining, **self.counters, last=0, max_wire=255,
        )

    def write_command(self, command: str) -> None:
        parts = command.split()
        if len(parts) == 2 and parts[0] == "session-start":
            session = int(parts[1])
            self.ack_role = None
            self.ack_session = 0
            self.ack_remaining = 0
            self.control_session = session
            self._emit("SESSION_START", run=self.run, session=session, accepted="yes",
                       tx="no", permits_cleared="yes")
            self._emit_profile_status()
            return
        if len(parts) == 2 and parts[0] == "session-end":
            session = int(parts[1])
            if self.world.mode == "session_end":
                session += 1
            self._emit("SESSION_END", run=self.run, session=session, accepted="yes",
                       tx="no", permits_cleared="yes")
            self.ack_role = None
            self.ack_session = 0
            self.ack_remaining = 0
            return
        if parts == ["status"]:
            self._emit_status()
            return
        if parts == ["rx"]:
            self._emit("RX_ARM", result=0)
            return
        if parts == ["arm"]:
            self._emit("ARM", accepted="yes", session=self.control_session, uses=1, expires_ms=30000)
            return
        if len(parts) == 4 and parts[0] == "ack-arm":
            role, session, count = parts[1], int(parts[2]), int(parts[3])
            self.ack_role = role
            self.ack_session = session
            self.ack_remaining = count
            self._emit("ACK_ARM", accepted="yes", role=role, session=session,
                       remaining=count, expires_ms=120000)
            return
        if len(parts) == 5 and parts[0] == "probe":
            role, session, direction, sequence = parts[1], int(parts[2]), parts[3], int(parts[4])
            payload = runner._probe_payload(role, direction, session, sequence)
            hash8 = runner._fnv1a32(payload)
            self.counters["attempted"] += 1
            self.counters["sent"] += 1
            self._emit("TX", kind="probe", result=0, role=role, session=session,
                       dir=direction, seq=sequence, wire=1, hash=hash8, rx_restart=0)
            if self.world.mode != "loss":
                self.peer.counters["rx_valid"] += 1
                peer_hash = "00000000" if self.world.mode == "corrupt" else hash8
                self.peer._emit("RX", kind="probe", valid="yes", wire=1,
                                hash=peer_hash, rssi_dbm="-45.0", snr_db="9.5")
                if self.world.mode == "duplicate":
                    self.peer._emit("RX", kind="probe", valid="yes", wire=1,
                                    hash=peer_hash, rssi_dbm="-45.0", snr_db="9.5")
            return
        if len(parts) == 6 and parts[0] == "send":
            role, session, direction, sequence, wire = (
                parts[1], int(parts[2]), parts[3], int(parts[4]), int(parts[5])
            )
            if wire == 256:
                if self.world.mode == "reject":
                    self.counters["attempted"] += 1
                    self._emit("REJECT", kind="send", reason="wire_too_long",
                               requested=256, transmitted="yes", arm_consumed="yes")
                else:
                    self._emit("REJECT", kind="send", reason="wire_too_long",
                               requested=256, transmitted="no", arm_consumed="no")
                return
            payload = runner._data_payload(role, direction, session, sequence, wire)
            hash8 = runner._fnv1a32(payload)
            self.counters["attempted"] += 1
            self.counters["sent"] += 1
            self._emit("TX", kind="data", result=0, role=role, session=session,
                       dir=direction, seq=sequence, wire=wire, hash=hash8, rx_restart=0)
            if self.world.mode == "loss":
                return
            received_session = session + 1 if self.world.mode == "wrong_session" else session
            peer_hash = "00000000" if self.world.mode == "corrupt" else hash8
            self.peer.counters["rx_valid"] += 1
            self.peer._emit("RX", kind="data", valid="yes", wire=wire, hash=peer_hash,
                            rssi_dbm="-46.0", snr_db="8.5", role=role,
                            session=received_session, dir=direction, seq=sequence)
            if self.world.mode == "duplicate":
                self.peer._emit("RX", kind="data", valid="yes", wire=wire, hash=peer_hash,
                                rssi_dbm="-46.0", snr_db="8.5", role=role,
                                session=received_session, dir=direction, seq=sequence)
            if self.peer.ack_remaining > 0 and self.peer.ack_session == session:
                self.peer.ack_remaining -= 1
                reverse = "B>A" if direction == "A>B" else "A>B"
                ack = ack_payload(self.peer.role, reverse, session, sequence)
                ack_hash = runner._fnv1a32(ack)
                self.peer.counters["attempted"] += 1
                self.peer.counters["sent"] += 1
                self.peer._emit("TX", kind="ack", result=0, role=self.peer.role,
                                session=session, dir=reverse, seq=sequence, wire=16,
                                hash=ack_hash, rx_restart=0)
                self.counters["rx_valid"] += 1
                self._emit("RX", kind="ack", valid="yes", wire=16, hash=ack_hash,
                           rssi_dbm="-44.0", snr_db="10.0", role=self.peer.role,
                           session=session, dir=reverse, seq=sequence)
            return
        if parts == ["restart"]:
            self._emit("RESTART", accepted="yes", tx="no")
            return
        raise AssertionError("unexpected fake command")

    def next_receipt(self, timeout_seconds: float) -> runner.Receipt:
        if not self.receipts:
            raise runner.RunnerError("fake timeout")
        return self.receipts.popleft()

    def reopen(self) -> None:
        self.world.restarted = True
        if self.world.mode != "restart_nonce":
            self.run += 1000
        self.counters = {key: 0 for key in runner.COUNTER_KEYS}
        self.ack_role = None
        self.ack_session = 0
        self.ack_remaining = 0
        self.receipts.clear()
        self._queue_boot()

    def close(self) -> None:
        self.closed = True


class FakeWorld:
    def __init__(self, mode: str = "happy") -> None:
        self.mode = mode
        self.clock = 1000.0
        self.restarted = False
        self.a = FakeEndpoint(self, "A")
        self.b = FakeEndpoint(self, "B")


class Ot110RadioRunnerTests(unittest.TestCase):
    def run_world(self, mode: str = "happy") -> dict[str, object]:
        world = FakeWorld(mode)
        return runner.run_acceptance(world.a, world.b, session=0x12345678)

    def assert_fails(self, mode: str) -> None:
        with self.assertRaises(runner.RunnerError):
            self.run_world(mode)

    def test_01_parser_is_strict(self) -> None:
        self.assertIsNone(runner.parse_receipt("ordinary boot noise"))
        with self.assertRaises(runner.RunnerError):
            runner.parse_receipt("OTD ARM accepted=yes accepted=yes uses=1 expires_ms=30000")
        with self.assertRaises(runner.RunnerError):
            runner.parse_receipt("OTD ARM accepted=yes uses=1 expires_ms=30000 extra=x")
        with self.assertRaises(runner.RunnerError):
            runner.parse_receipt("OTD ARM uses=1 accepted=yes expires_ms=30000")
        with self.assertRaises(runner.RunnerError):
            runner.parse_receipt("OTD UNKNOWN value=x")
        with self.assertRaises(runner.RunnerError):
            runner.parse_receipt("x" * 1025)

    def test_02_airtime_and_payload_vectors(self) -> None:
        self.assertGreater(runner.theoretical_lora_airtime_ms(255),
                           runner.theoretical_lora_airtime_ms(163))
        policy = runner.timeout_policy()
        self.assertEqual(policy["responder_turnaround_bound_ms"], 500)
        self.assertEqual(policy["scheduling_margin_ms"], 1500)
        payload = runner._data_payload("A", "A>B", 0x12345678, 7, 163)
        self.assertEqual(len(payload), 163)
        self.assertEqual(payload[:4], b"OTD1")
        self.assertEqual(runner._fnv1a32(payload), "22467aef")
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         "5da77f6771fd8e8125fc3eb1615df620de0d6a7d2c836c419a140bfb83bb77d1")
        ack = runner._ack_payload("B", "B>A", 0x12345678, 7)
        self.assertEqual(ack, ack_payload("B", "B>A", 0x12345678, 7))
        self.assertEqual(runner._fnv1a32(ack), "85cbcf3a")
        self.assertEqual(hashlib.sha256(ack).hexdigest(),
                         "3d7f6210349d485635d324a86a393180b52b70e5ed690aa39e368ce05b1672e1")
        self.assertEqual(runner._probe_payload("A", "A>B", 1, 1), b"\xA5")
        self.assertEqual(runner._fnv1a32(b"\xA5"), "a00bbe20")
        self.assertEqual(hashlib.sha256(b"\xA5").hexdigest(),
                         "6922e93e3827642ce4b883c756b31abf80036649d3614bf5fcb3adda43b8ea32")

    def test_03_exact_happy_path(self) -> None:
        receipt = self.run_world()
        self.assertEqual(receipt["schema"], "OTRER0")
        self.assertEqual(receipt["version"], 0)
        self.assertEqual(len(receipt["frames"]), 242)
        self.assertEqual(receipt["summary"]["radio_frames_attempted"], 242)
        self.assertEqual(receipt["summary"]["radio_frames_received"], 242)
        self.assertEqual(receipt["summary"]["ack_frames_attempted"], 240)
        self.assertEqual(receipt["summary"]["ack_frames_received"], 240)
        self.assertEqual(receipt["summary"]["radio_transmissions_including_acks"], 482)
        self.assertEqual(receipt["summary"]["session_start_receipts"], 4)
        self.assertEqual(receipt["summary"]["session_end_receipts"], 2)
        self.assertEqual(receipt["summary"]["per_test_direction"], {
            "A>B": {"radio_frames": 121, "ack_frames": 120,
                    "radio_transmissions_including_acks": 241},
            "B>A": {"radio_frames": 121, "ack_frames": 120,
                    "radio_transmissions_including_acks": 241},
        })
        self.assertEqual(receipt["summary"]["direct_payload_ceiling_bytes"], 255)
        self.assertRegex(receipt["summary"]["receipt_chain_sha256"], r"^[0-9a-f]{64}$")
        phases = [step["operation"] for step in receipt["steps"]]
        self.assertEqual(phases, [
            "off_air_status_preflight", "configure_receivers",
            "one_byte_probe_each_direction", "benchmark_mtu_each_direction",
            "direct_ceiling_each_direction", "oversize_local_reject",
            "restart_both_nodes", "post_restart_benchmark_mtu_each_direction",
        ])
        for frame in receipt["frames"]:
            self.assertEqual(set(frame), {
                "phase", "session", "direction", "sequence", "wire_bytes", "data_wire_sha256",
                "ack_wire_sha256", "ack_timeout_ms", "rtt_ms", "data_rssi_dbm", "data_snr_db",
                "ack_rssi_dbm", "ack_snr_db",
            })
            self.assertRegex(frame["data_wire_sha256"], r"^[0-9a-f]{64}$")
            if frame["wire_bytes"] == 1:
                self.assertIsNone(frame["ack_wire_sha256"])
            else:
                self.assertRegex(frame["ack_wire_sha256"], r"^[0-9a-f]{64}$")

    def test_04_loss_fails_closed(self) -> None:
        self.assert_fails("loss")

    def test_05_duplicate_fails_closed(self) -> None:
        self.assert_fails("duplicate")

    def test_06_wrong_session_fails_closed(self) -> None:
        self.assert_fails("wrong_session")

    def test_07_corrupt_hash_fails_closed(self) -> None:
        self.assert_fails("corrupt")

    def test_08_reject_must_not_transmit_or_consume_arm(self) -> None:
        self.assert_fails("reject")

    def test_09_restart_and_profile_are_exact(self) -> None:
        self.assert_fails("restart_nonce")
        self.assert_fails("restart_profile")
        self.assert_fails("profile")
        self.assert_fails("session_end")

    def test_10_privacy_and_sanitized_cli(self) -> None:
        receipt = self.run_world()
        rendered = runner._canonical_bytes(receipt).decode("ascii")
        fake_mac = ":".join(("aa", "bb", "cc", "dd", "ee", "ff"))
        for forbidden in ("COM77", "COM88", "C:\\\\private", fake_mac,
                          "latitude", "longitude", "12345678"):
            self.assertNotIn(forbidden, rendered)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            result = runner.main([
                "--port-a", "COM77", "--port-b", "COM88",
                "--output", "C:\\private\\receipt.json", "--baud", "1",
            ])
        self.assertEqual(result, 2)
        error = stderr.getvalue()
        self.assertNotIn("COM77", error)
        self.assertNotIn("COM88", error)
        self.assertNotIn("receipt.json", error)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Ot110RadioRunnerTests)
    outcome = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if outcome.wasSuccessful() else 1)
