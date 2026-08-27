#!/usr/bin/env python3
"""Focused adversarial tests for the OT-153 Noise XK radio runner."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import sys
import unittest
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/ot153_noise_xk_radio_runner.py"
SPEC = importlib.util.spec_from_file_location("ot153_noise_xk_radio_runner", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def label_hash(token: str) -> str:
    return hashlib.sha256(bytes.fromhex(token)).hexdigest()[:16]


class FakeEndpoint:
    def __init__(self, label: str) -> None:
        self.label = label
        self.peer: "FakeEndpoint | None" = None
        self.queue: deque[object] = deque()
        self.current: dict[str, str] = {}
        self.active = False
        self.arm: str | None = None
        self.keys: tuple[str, str] | None = None
        self.clock = 1_000_000 if label == "A" else 2_000_000
        self.rx_windows: dict[str, tuple[int, int | None]] = {}
        self.commands: list[str] = []
        self.counters = {key: 0 for key in MODULE.COUNTERS}

    def receipt(self, kind: str, fields: dict[str, str]) -> None:
        self.queue.append(MODULE.Receipt(kind, fields))

    def status_fields(self) -> dict[str, str]:
        return {
            "ready": "yes", "rx": "armed", "active": "no",
            "session_hash": "none", "attempt_hash": "none", "role": "none",
            "scenario": "none", "stage": "0", "tx_armed": "no",
            **{key: str(self.counters[key]) for key in MODULE.COUNTERS},
            "last_radio": "0",
        }

    def emit_rx_start(self, message: str, start: int, deadline: int | None) -> None:
        assert self.current
        fields = {
            "session_hash": label_hash(self.current["session"]),
            "attempt_hash": label_hash(self.current["attempt"]),
            "role": self.current["role"], "scenario": self.current["scenario"],
            "message": message, "start_us": str(start),
            "deadline_ms": "none" if deadline is None else str(deadline),
        }
        if deadline is not None:
            fields["deadline_us"] = str(start + deadline * 1_000)
        fields["rx"] = "armed"
        self.rx_windows[message] = (start, deadline)
        self.receipt("RX_START", fields)

    def emit_timeout(self) -> None:
        start, deadline = self.rx_windows["m2"]
        assert deadline == MODULE.M2_TIMEOUT_MS and self.current
        timeout = start + deadline * 1_000 + 10_000
        self.counters["forced_timeouts"] += 1
        self.receipt("TIMEOUT", {
            "session_hash": label_hash(self.current["session"]),
            "attempt_hash": label_hash(self.current["attempt"]),
            "role": "I", "scenario": "retry-m2-withheld", "message": "m2",
            "start_us": str(start), "deadline_ms": str(deadline),
            "timeout_us": str(timeout), "measured_us": str(timeout - start),
            "forced": "yes", "received": "no", "transmitted": "no", "wiped": "yes",
        })
        self.active = False
        self.keys = None

    def write_command(self, command: str) -> None:
        self.commands.append(command)
        parts = command.split(" ")
        if parts[0] == "restart":
            self.current = {}
            self.active = False
            self.arm = None
            self.keys = None
            self.rx_windows = {}
            self.counters = {key: 0 for key in MODULE.COUNTERS}
            self.receipt("RESTART", {"accepted": "yes", "wiped": "yes", "tx": "no"})
            self.receipt("STALE_SELFTEST", {"passed": "yes", "stale_rejected": "yes", "radio_frames": "0"})
            self.receipt("BOOT", {"schema": "OT153FW0/v0", "target": "heltec-v4.2",
                "candidate": "libsodium-1.0.22", "noise": "OTNXK0/v0", "radio": "sx1262",
                "tx_at_boot": "no", "rx": "armed", "raw_logging": "no"})
            self.receipt("PROFILE", MODULE.PROFILE_RECEIPT.copy())
            self.receipt("STATUS", self.status_fields())
            self.receipt("COMMANDS", {"commands": MODULE.COMMANDS})
        elif parts[0] == "profile":
            self.receipt("PROFILE", MODULE.PROFILE_RECEIPT.copy())
        elif parts[0] == "status":
            assert not self.active
            self.receipt("STATUS", self.status_fields())
        elif parts[0] == "prepare":
            _, session, attempt, role, scenario = parts
            self.current = {"session": session, "attempt": attempt, "role": role, "scenario": scenario}
            self.active = True
            seed = hashlib.sha256((session + attempt).encode("ascii")).hexdigest()
            self.keys = (("a" if role == "I" else "b") + seed[1:],
                         ("b" if role == "I" else "a") + seed[1:])
            self.receipt("PREPARED", {"accepted": "yes", "session_hash": label_hash(session),
                "attempt_hash": label_hash(attempt), "role": role, "scenario": scenario, "tx": "no"})
            if role == "R":
                self.emit_rx_start("m1", self.clock, None)
        elif parts[0] == "arm-tx":
            _, session, attempt, message = parts
            self.arm = message
            self.receipt("TX_ARM", {"accepted": "yes", "session_hash": label_hash(session),
                "attempt_hash": label_hash(attempt), "message": message, "uses": "1", "expires_ms": "30000"})
        elif parts[0] == "send":
            _, session, attempt, message = parts
            assert self.arm == message and self.peer is not None and self.current
            self.arm = None
            payload_hash = hashlib.sha256((session + attempt + message).encode("ascii")).hexdigest()
            if self.current["scenario"] == "retry-m2-withheld" and self.current["role"] == "R" and message == "m2":
                self.receipt("WITHHELD", {"accepted": "yes", "session_hash": label_hash(session),
                    "attempt_hash": label_hash(attempt), "role": "R", "scenario": "retry-m2-withheld",
                    "message": "m2", "wire": "48", "payload_sha256": payload_hash,
                    "transmitted": "no", "permit_consumed": "yes"})
                self.peer.emit_timeout()
                return
            wire = MODULE.WIRE_BYTES[message]
            start = self.clock
            done = start + MODULE.THEORETICAL_US[message]
            self.clock = done + 10_000
            common = {"session_hash": label_hash(session), "attempt_hash": label_hash(attempt),
                      "role": self.current["role"], "scenario": self.current["scenario"],
                      "message": message, "wire": str(wire), "payload_sha256": payload_hash}
            self.counters["tx_attempted"] += 1
            self.counters["tx_sent"] += 1
            self.receipt("TX_START", {**common, "start_us": str(start)})
            self.receipt("TX_DONE", {**common, "result": "0", "start_us": str(start),
                "done_us": str(done), "measured_us": str(done - start), "rx_restart": "0",
                "permit_consumed": "yes"})
            if message == "m1":
                self.emit_rx_start("m2", done, MODULE.M2_TIMEOUT_MS)
            elif message == "m2":
                self.emit_rx_start("m3", done, MODULE.M3_TIMEOUT_MS)
            self.peer.counters["rx_accepted"] += 1
            peer_common = {"session_hash": label_hash(session), "attempt_hash": label_hash(attempt),
                "role": self.peer.current["role"], "scenario": self.peer.current["scenario"],
                "message": message}
            self.peer.receipt("RX", {"accepted": "yes", "reason": "authenticated", **peer_common,
                "wire": str(wire), "payload_sha256": payload_hash, "mono_us": str(done + 100), "wiped": "no"})
            next_stage = {"m1": ("m2", "3"), "m2": ("m3", "5"), "m3": ("end", "7")}[message]
            self.peer.receipt("STAGE_ACCEPT", {**peer_common, "next": next_stage[0], "stage": next_stage[1]})
        elif parts[0] == "abort":
            _, session, attempt = parts
            self.active = False
            self.keys = None
            self.receipt("ABORT", {"accepted": "yes", "session_hash": label_hash(session),
                "attempt_hash": label_hash(attempt), "wiped": "yes", "tx": "no"})
        elif parts[0] == "end":
            _, session, attempt = parts
            assert self.keys is not None and self.current
            tx, rx = self.keys
            role, scenario = self.current["role"], self.current["scenario"]
            self.keys = None
            self.active = False
            self.receipt("END", {"accepted": "yes", "session_hash": label_hash(session),
                "attempt_hash": label_hash(attempt), "role": role, "scenario": scenario,
                "complete": "yes", "wiped": "yes", "tx": "no",
                "tx_key_sha256": tx, "rx_key_sha256": rx})
        else:
            raise AssertionError(command)

    def expect(self, kind: str, timeout_ms: int) -> object:
        del timeout_ms
        value = self.queue.popleft()
        if isinstance(value, BaseException):
            raise value
        assert value.kind == kind, (value.kind, kind)
        return value


class Tests(unittest.TestCase):
    def endpoints(self) -> tuple[FakeEndpoint, FakeEndpoint]:
        a, b = FakeEndpoint("A"), FakeEndpoint("B")
        a.peer, b.peer = b, a
        return a, b

    def tokens(self):
        return iter(f"{index:016x}" for index in range(1, 30))

    def run_result(self) -> tuple[dict, FakeEndpoint, FakeEndpoint]:
        a, b = self.endpoints()
        tokens = self.tokens()
        return MODULE.run(a, b, token_factory=lambda: next(tokens)), a, b

    @staticmethod
    def corrupt(endpoint: FakeEndpoint, kind: str, field: str, value: str, occurrence: int = 1) -> None:
        original = endpoint.expect
        seen = 0
        def wrapped(receipt_kind: str, timeout_ms: int):
            nonlocal seen
            receipt = original(receipt_kind, timeout_ms)
            if receipt_kind == kind:
                seen += 1
                if seen == occurrence:
                    receipt = MODULE.Receipt(receipt.kind, {**receipt.fields, field: value})
            return receipt
        endpoint.expect = wrapped  # type: ignore[method-assign]

    def test_full_two_role_contract_and_observed_counters(self) -> None:
        result, a, b = self.run_result()
        self.assertEqual(result["totals"]["radio_payload_wire_bytes"], 736)
        self.assertEqual(result["totals"]["fragments"], 14)
        self.assertEqual(result["totals"]["theoretical_airtime_us"], 1_447_424)
        self.assertEqual(result["totals"]["measured_airtime_us"], 1_447_424)
        for endpoint in (a, b):
            self.assertEqual(endpoint.counters, {"tx_attempted": 7, "tx_sent": 7,
                "tx_failed": 0, "rx_accepted": 7, "rx_rejected": 0, "lost": 0,
                "duplicates": 0, "corrupt": 0, "unexpected": 0, "forced_timeouts": 1})

    def test_restart_boot_profile_status_commands_and_abort_boundary(self) -> None:
        _, a, b = self.run_result()
        for endpoint in (a, b):
            self.assertEqual(endpoint.commands[0], "restart")
            self.assertIn("profile", endpoint.commands)
            self.assertEqual(sum(command.startswith("abort ") for command in endpoint.commands), 1)

    def test_exact_role_reversal_retry_frame_sequence(self) -> None:
        result, _, _ = self.run_result()
        expected = [
            [(1, "m1", "A_to_B"), (1, "m2", "B_to_A"), (1, "m3", "A_to_B")],
            [(1, "m1", "B_to_A"), (1, "m2", "A_to_B"), (1, "m3", "B_to_A")],
        ]
        for index, cycle in enumerate(result["cycles"]):
            baseline = [(f["attempt"], f["message"], f["direction"]) for f in cycle["baseline"]["frames"]]
            self.assertEqual(baseline, expected[index])
            self.assertEqual([f["attempt"] for f in cycle["bounded_retry"]["frames"]], [1, 2, 2, 2])

    def test_receipt_parser_is_strict(self) -> None:
        self.assertIsNone(MODULE.parse_receipt("OT153 RX a=1 a=2"))
        self.assertIsNone(MODULE.parse_receipt("OT153 RX reason=accépted"))
        self.assertIsNone(MODULE.parse_receipt("OT153 RX reason accepted"))
        self.assertIsNone(MODULE.parse_receipt("OT153 COMMANDS prepare send"))

    def test_stale_selftest_failure_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "STALE_SELFTEST", "radio_frames", "1")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_profile_command_result_drift_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "PROFILE", "crc_result", "-2")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_initial_status_counter_drift_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "STATUS", "lost", "1")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_rx_reason_must_be_authenticated(self) -> None:
        a, b = self.endpoints()
        self.corrupt(b, "RX", "reason", "accepted")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_stage_accept_exact_stage_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(b, "STAGE_ACCEPT", "stage", "4")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_m2_timeout_anchor_and_policy_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "TIMEOUT", "deadline_ms", "2195")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_m3_rx_start_deadline_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(b, "RX_START", "deadline_ms", "2215", occurrence=2)
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_tx_role_scenario_drift_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "TX_START", "role", "R")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_end_role_drift_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "END", "role", "R")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_final_status_must_be_observed(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "STATUS", "tx_sent", "6", occurrence=2)
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "receipt_contract_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_private_identity_reuse_rejected(self) -> None:
        a, b = self.endpoints()
        values = iter(["0000000000000001", "0000000000000002",
                       "0000000000000003", "0000000000000003"])
        with self.assertRaisesRegex(MODULE.RunnerError, "private_identity_reused"):
            MODULE.run(a, b, token_factory=lambda: next(values))

    def test_endpoint_failure_is_closed(self) -> None:
        a, b = self.endpoints()
        a.queue.append(RuntimeError("private COM detail"))
        with self.assertRaisesRegex(MODULE.RunnerError, "endpoint_timeout"):
            MODULE.run(a, b, token_factory=lambda: "0000000000000001")

    def test_timing_mismatch_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(a, "TX_DONE", "measured_us", "1")
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "tx_timing_invalid"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_cross_match_failure_rejected(self) -> None:
        a, b = self.endpoints()
        self.corrupt(b, "END", "rx_key_sha256", "c" * 64)
        tokens = self.tokens()
        with self.assertRaisesRegex(MODULE.RunnerError, "split_key_cross_match_failed"):
            MODULE.run(a, b, token_factory=lambda: next(tokens))

    def test_public_result_rejects_extra_top_frame_and_claim_drift(self) -> None:
        result, _, _ = self.run_result()
        for mutation in ("top", "frame", "claim"):
            changed = copy.deepcopy(result)
            if mutation == "top":
                changed["extra"] = False
            elif mutation == "frame":
                changed["cycles"][0]["baseline"]["frames"][0]["extra"] = 0
            else:
                changed["claims"]["phase_two_complete"] = True
            with self.assertRaises(MODULE.RunnerError):
                MODULE.validate_public_result(changed)

    def test_public_result_rejects_bool_int_type_confusion(self) -> None:
        result, _, _ = self.run_result()
        for mutate in (
            lambda value: value.__setitem__("version", False),
            lambda value: value["radio_profile"].__setitem__("explicit_header", 1),
            lambda value: value["claims"].__setitem__("score_credit_added", 0),
            lambda value: value["totals"].__setitem__("role_cycles", True),
            lambda value: value["cycles"][0]["baseline"]["frames"][0].__setitem__("cycle", True),
        ):
            changed = copy.deepcopy(result)
            mutate(changed)
            with self.assertRaises(MODULE.RunnerError):
                MODULE.validate_public_result(changed)

    def test_public_result_privacy_rejects_private_fields(self) -> None:
        result, _, _ = self.run_result()
        changed = copy.deepcopy(result)
        changed["serial_port"] = "COM1"
        with self.assertRaisesRegex(MODULE.RunnerError, "result_privacy_invalid"):
            MODULE.validate_public_result(changed)


if __name__ == "__main__":
    unittest.main()
