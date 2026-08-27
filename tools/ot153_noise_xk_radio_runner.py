#!/usr/bin/env python3
"""Strict host runner for the future OT-153 two-node Noise XK radio attempt.

The runner owns only the serial command/receipt protocol. Flashing, authority
validation, private endpoint custody, and exact Trail restoration are composed
by the separately bound hardware adapter. Public results contain aggregate
frame evidence but never raw payloads, keys, sessions, attempts, ports, or
device identifiers.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
import secrets
from dataclasses import dataclass
from typing import Any, Callable, Protocol


SCHEMA = "OT153NXR0"
PROFILE = {
    "frequency_hz": 915_000_000,
    "bandwidth_hz": 125_000,
    "spreading_factor": 7,
    "coding_rate_denominator": 5,
    "explicit_header": True,
    "crc_enabled": True,
    "low_data_rate_optimization": False,
    "sync_word": "0x12",
    "preamble_symbols": 8,
    "tx_power_command_setpoint_dbm": 2,
}
PROFILE_RECEIPT = {
    "configured": "yes", "begin_result": "0",
    "explicit_header_result": "0", "crc_result": "0", "ldro_result": "0",
    "frequency_hz": "915000000", "bandwidth_hz": "125000", "sf": "7",
    "cr_denom": "5", "power_dbm": "2", "preamble": "8",
    "explicit": "yes", "crc_enabled": "yes", "ldro": "off",
    "sync": "0x12", "radiolib": "7.7.1", "calibrated": "no",
}
COMMANDS = "prepare,arm-tx,send,abort,end,profile,status,restart"
WIRE_BYTES = {"m1": 48, "m2": 48, "m3": 64}
THEORETICAL_US = {"m1": 97_536, "m2": 97_536, "m3": 118_016}
M2_TIMEOUT_MS = 2_196
M3_TIMEOUT_MS = 2_216
HASH64 = re.compile(r"^[0-9a-f]{64}$")
U64_MAX = (1 << 64) - 1
COUNTERS = (
    "tx_attempted", "tx_sent", "tx_failed", "rx_accepted", "rx_rejected",
    "lost", "duplicates", "corrupt", "unexpected", "forced_timeouts",
)


class RunnerError(RuntimeError):
    """Stable fail-closed runner error without private diagnostics."""


class Endpoint(Protocol):
    def write_command(self, command: str) -> None: ...

    def expect(self, kind: str, timeout_ms: int) -> "Receipt": ...


@dataclass(frozen=True)
class Receipt:
    kind: str
    fields: dict[str, str]


@dataclass(frozen=True)
class RxWindow:
    message: str
    start_us: int
    deadline_ms: int | None
    deadline_us: int | None


def _u64(value: str, label: str) -> int:
    if not value.isascii() or not value.isdigit():
        raise RunnerError(f"{label}_invalid")
    parsed = int(value)
    if parsed > U64_MAX:
        raise RunnerError(f"{label}_invalid")
    return parsed


def _i16(value: str, label: str) -> int:
    if re.fullmatch(r"-?[0-9]+", value) is None:
        raise RunnerError(f"{label}_invalid")
    parsed = int(value)
    if parsed < -32_768 or parsed > 32_767:
        raise RunnerError(f"{label}_invalid")
    return parsed


def parse_receipt(line: str) -> Receipt | None:
    if type(line) is not str or len(line) > 1_024 or "\x00" in line:
        return None
    value = line.rstrip("\r\n")
    if not value.startswith("OT153 "):
        return None
    tokens = value.split(" ")
    if len(tokens) < 3 or any(not token for token in tokens):
        return None
    fields: dict[str, str] = {}
    for token in tokens[2:]:
        if token.count("=") != 1:
            return None
        key, item = token.split("=", 1)
        if not key or not item or key in fields or re.fullmatch(r"[a-z_]+", key) is None:
            return None
        if not item.isascii() or any(character.isspace() for character in item):
            return None
        fields[key] = item
    return Receipt(tokens[1], fields)


def _exact(receipt: Receipt, kind: str, expected: dict[str, str]) -> None:
    if receipt.kind != kind or receipt.fields != expected:
        raise RunnerError("receipt_contract_invalid")


def _token() -> str:
    return secrets.token_hex(8)


def _label_hash(token: str) -> str:
    if re.fullmatch(r"[0-9a-f]{16}", token) is None:
        raise RunnerError("private_identity_invalid")
    return hashlib.sha256(bytes.fromhex(token)).hexdigest()[:16]


def _expect(endpoint: Endpoint, kind: str, timeout_ms: int) -> Receipt:
    try:
        receipt = endpoint.expect(kind, timeout_ms)
    except BaseException as exc:
        raise RunnerError("endpoint_timeout") from exc
    if not isinstance(receipt, Receipt):
        raise RunnerError("endpoint_receipt_invalid")
    return receipt


def _profile(endpoint: Endpoint) -> None:
    _exact(_expect(endpoint, "PROFILE", 5_000), "PROFILE", PROFILE_RECEIPT)


def _status(endpoint: Endpoint, counters: dict[str, int], *, command: bool) -> dict[str, int]:
    if command:
        endpoint.write_command("status")
    receipt = _expect(endpoint, "STATUS", 5_000)
    expected = {
        "ready": "yes", "rx": "armed", "active": "no",
        "session_hash": "none", "attempt_hash": "none", "role": "none",
        "scenario": "none", "stage": "0", "tx_armed": "no",
        **{key: str(counters[key]) for key in COUNTERS}, "last_radio": "0",
    }
    _exact(receipt, "STATUS", expected)
    observed = {key: _u64(receipt.fields[key], key) for key in COUNTERS}
    if _i16(receipt.fields["last_radio"], "last_radio") != 0:
        raise RunnerError("radio_status_invalid")
    return observed


def _restart(endpoint: Endpoint) -> None:
    endpoint.write_command("restart")
    _exact(_expect(endpoint, "RESTART", 5_000), "RESTART", {
        "accepted": "yes", "wiped": "yes", "tx": "no",
    })
    _exact(_expect(endpoint, "STALE_SELFTEST", 5_000), "STALE_SELFTEST", {
        "passed": "yes", "stale_rejected": "yes", "radio_frames": "0",
    })
    _exact(_expect(endpoint, "BOOT", 10_000), "BOOT", {
        "schema": "OT153FW0/v0", "target": "heltec-v4.2",
        "candidate": "libsodium-1.0.22", "noise": "OTNXK0/v0",
        "radio": "sx1262", "tx_at_boot": "no", "rx": "armed",
        "raw_logging": "no",
    })
    _profile(endpoint)
    _status(endpoint, {key: 0 for key in COUNTERS}, command=False)
    _exact(_expect(endpoint, "COMMANDS", 5_000), "COMMANDS", {"commands": COMMANDS})
    endpoint.write_command("profile")
    _profile(endpoint)


def _rx_start(
    endpoint: Endpoint,
    session: str,
    attempt: str,
    role: str,
    scenario: str,
    message: str,
    deadline_ms: int | None,
    *,
    anchor_us: int | None = None,
) -> RxWindow:
    receipt = _expect(endpoint, "RX_START", 5_000)
    expected = {
        "session_hash": _label_hash(session), "attempt_hash": _label_hash(attempt),
        "role": role, "scenario": scenario, "message": message,
        "start_us": receipt.fields.get("start_us", ""),
        "deadline_ms": "none" if deadline_ms is None else str(deadline_ms),
    }
    if deadline_ms is not None:
        expected["deadline_us"] = receipt.fields.get("deadline_us", "")
    expected["rx"] = "armed"
    _exact(receipt, "RX_START", expected)
    start_us = _u64(receipt.fields["start_us"], "rx_start")
    if anchor_us is not None and start_us != anchor_us:
        raise RunnerError("rx_deadline_anchor_invalid")
    deadline_us = None
    if deadline_ms is not None:
        deadline_us = _u64(receipt.fields["deadline_us"], "rx_deadline")
        if deadline_us != start_us + deadline_ms * 1_000:
            raise RunnerError("rx_deadline_invalid")
    return RxWindow(message, start_us, deadline_ms, deadline_us)


def _prepare(
    endpoint: Endpoint, session: str, attempt: str, role: str, scenario: str,
) -> RxWindow | None:
    endpoint.write_command(f"prepare {session} {attempt} {role} {scenario}")
    _exact(_expect(endpoint, "PREPARED", 5_000), "PREPARED", {
        "accepted": "yes", "session_hash": _label_hash(session),
        "attempt_hash": _label_hash(attempt), "role": role,
        "scenario": scenario, "tx": "no",
    })
    if role == "R":
        return _rx_start(endpoint, session, attempt, role, scenario, "m1", None)
    return None


def _arm(endpoint: Endpoint, session: str, attempt: str, message: str) -> None:
    endpoint.write_command(f"arm-tx {session} {attempt} {message}")
    _exact(_expect(endpoint, "TX_ARM", 5_000), "TX_ARM", {
        "accepted": "yes", "session_hash": _label_hash(session),
        "attempt_hash": _label_hash(attempt), "message": message,
        "uses": "1", "expires_ms": "30000",
    })


def _send(
    sender: Endpoint,
    receiver: Endpoint,
    session: str,
    attempt: str,
    message: str,
    receiver_window: RxWindow,
    *,
    cycle: int,
    public_scenario: str,
    firmware_scenario: str,
    attempt_number: int,
    direction: str,
) -> tuple[dict[str, Any], RxWindow | None]:
    if receiver_window.message != message:
        raise RunnerError("rx_start_order_invalid")
    sender_role = "R" if message == "m2" else "I"
    receiver_role = "I" if sender_role == "R" else "R"
    _arm(sender, session, attempt, message)
    sender.write_command(f"send {session} {attempt} {message}")
    expected_common = {
        "session_hash": _label_hash(session), "attempt_hash": _label_hash(attempt),
        "role": sender_role, "scenario": firmware_scenario, "message": message,
        "wire": str(WIRE_BYTES[message]),
    }
    start = _expect(sender, "TX_START", 5_000)
    expected_start = {
        **expected_common, "payload_sha256": start.fields.get("payload_sha256", ""),
        "start_us": start.fields.get("start_us", ""),
    }
    _exact(start, "TX_START", expected_start)
    payload_sha256 = start.fields["payload_sha256"]
    if HASH64.fullmatch(payload_sha256) is None:
        raise RunnerError("payload_digest_invalid")
    start_us = _u64(start.fields["start_us"], "tx_start")
    done = _expect(sender, "TX_DONE", 5_000)
    expected_done = {
        **expected_common, "result": "0", "start_us": str(start_us),
        "done_us": done.fields.get("done_us", ""),
        "measured_us": done.fields.get("measured_us", ""),
        "payload_sha256": payload_sha256, "rx_restart": "0",
        "permit_consumed": "yes",
    }
    _exact(done, "TX_DONE", expected_done)
    done_us = _u64(done.fields["done_us"], "tx_done")
    measured_us = _u64(done.fields["measured_us"], "measured_airtime")
    if done_us < start_us or measured_us != done_us - start_us or measured_us == 0:
        raise RunnerError("tx_timing_invalid")
    next_window = None
    if message == "m1":
        next_window = _rx_start(
            sender, session, attempt, sender_role, firmware_scenario, "m2",
            M2_TIMEOUT_MS, anchor_us=done_us)
    elif message == "m2":
        next_window = _rx_start(
            sender, session, attempt, sender_role, firmware_scenario, "m3",
            M3_TIMEOUT_MS, anchor_us=done_us)
    received = _expect(receiver, "RX", 5_000)
    _exact(received, "RX", {
        "accepted": "yes", "reason": "authenticated",
        "session_hash": _label_hash(session), "attempt_hash": _label_hash(attempt),
        "role": receiver_role, "scenario": firmware_scenario, "message": message,
        "wire": str(WIRE_BYTES[message]), "payload_sha256": payload_sha256,
        "mono_us": received.fields.get("mono_us", ""), "wiped": "no",
    })
    _u64(received.fields["mono_us"], "rx_time")
    stage = {"m1": ("m2", "3"), "m2": ("m3", "5"), "m3": ("end", "7")}[message]
    _exact(_expect(receiver, "STAGE_ACCEPT", 5_000), "STAGE_ACCEPT", {
        "session_hash": _label_hash(session), "attempt_hash": _label_hash(attempt),
        "role": receiver_role, "scenario": firmware_scenario, "message": message,
        "next": stage[0], "stage": stage[1],
    })
    return ({
        "cycle": cycle, "scenario": public_scenario, "attempt": attempt_number,
        "message": message, "direction": direction,
        "wire_bytes": WIRE_BYTES[message], "payload_sha256": payload_sha256,
        "tx_start_mono_us": start_us, "tx_done_mono_us": done_us,
        "measured_airtime_us": measured_us,
        "theoretical_airtime_us": THEORETICAL_US[message],
    }, next_window)


def _withhold(endpoint: Endpoint, session: str, attempt: str) -> None:
    _arm(endpoint, session, attempt, "m2")
    endpoint.write_command(f"send {session} {attempt} m2")
    receipt = _expect(endpoint, "WITHHELD", 5_000)
    _exact(receipt, "WITHHELD", {
        "accepted": "yes", "session_hash": _label_hash(session),
        "attempt_hash": _label_hash(attempt), "role": "R",
        "scenario": "retry-m2-withheld", "message": "m2", "wire": "48",
        "payload_sha256": receipt.fields.get("payload_sha256", ""),
        "transmitted": "no", "permit_consumed": "yes",
    })
    if HASH64.fullmatch(receipt.fields["payload_sha256"]) is None:
        raise RunnerError("payload_digest_invalid")


def _timeout(endpoint: Endpoint, session: str, attempt: str, window: RxWindow) -> None:
    if window.message != "m2" or window.deadline_ms != M2_TIMEOUT_MS or window.deadline_us is None:
        raise RunnerError("timeout_window_invalid")
    receipt = _expect(endpoint, "TIMEOUT", M2_TIMEOUT_MS + 5_000)
    _exact(receipt, "TIMEOUT", {
        "session_hash": _label_hash(session), "attempt_hash": _label_hash(attempt),
        "role": "I", "scenario": "retry-m2-withheld", "message": "m2",
        "start_us": str(window.start_us), "deadline_ms": str(M2_TIMEOUT_MS),
        "timeout_us": receipt.fields.get("timeout_us", ""),
        "measured_us": receipt.fields.get("measured_us", ""),
        "forced": "yes", "received": "no", "transmitted": "no", "wiped": "yes",
    })
    timeout_us = _u64(receipt.fields["timeout_us"], "timeout")
    measured_us = _u64(receipt.fields["measured_us"], "timeout_measured")
    if timeout_us < window.deadline_us or measured_us != timeout_us - window.start_us:
        raise RunnerError("timeout_timing_invalid")


def _abort(endpoint: Endpoint, session: str, attempt: str) -> None:
    endpoint.write_command(f"abort {session} {attempt}")
    _exact(_expect(endpoint, "ABORT", 5_000), "ABORT", {
        "accepted": "yes", "session_hash": _label_hash(session),
        "attempt_hash": _label_hash(attempt), "wiped": "yes", "tx": "no",
    })


def _end(
    endpoint: Endpoint, session: str, attempt: str, role: str, scenario: str,
) -> tuple[str, str]:
    endpoint.write_command(f"end {session} {attempt}")
    receipt = _expect(endpoint, "END", 5_000)
    _exact(receipt, "END", {
        "accepted": "yes", "session_hash": _label_hash(session),
        "attempt_hash": _label_hash(attempt), "role": role, "scenario": scenario,
        "complete": "yes", "wiped": "yes", "tx": "no",
        "tx_key_sha256": receipt.fields.get("tx_key_sha256", ""),
        "rx_key_sha256": receipt.fields.get("rx_key_sha256", ""),
    })
    tx_hash = receipt.fields["tx_key_sha256"]
    rx_hash = receipt.fields["rx_key_sha256"]
    if HASH64.fullmatch(tx_hash) is None or HASH64.fullmatch(rx_hash) is None or tx_hash == rx_hash:
        raise RunnerError("split_key_proof_invalid")
    return tx_hash, rx_hash


def _complete_handshake(
    initiator: Endpoint,
    responder: Endpoint,
    session: str,
    attempt: str,
    responder_window: RxWindow,
    *,
    cycle: int,
    public_scenario: str,
    firmware_scenario: str,
    attempt_number: int,
    directions: tuple[str, str, str],
) -> list[dict[str, Any]]:
    first, initiator_window = _send(
        initiator, responder, session, attempt, "m1", responder_window,
        cycle=cycle, public_scenario=public_scenario,
        firmware_scenario=firmware_scenario, attempt_number=attempt_number,
        direction=directions[0])
    if initiator_window is None:
        raise RunnerError("m2_window_missing")
    second, responder_window_m3 = _send(
        responder, initiator, session, attempt, "m2", initiator_window,
        cycle=cycle, public_scenario=public_scenario,
        firmware_scenario=firmware_scenario, attempt_number=attempt_number,
        direction=directions[1])
    if responder_window_m3 is None:
        raise RunnerError("m3_window_missing")
    third, no_window = _send(
        initiator, responder, session, attempt, "m3", responder_window_m3,
        cycle=cycle, public_scenario=public_scenario,
        firmware_scenario=firmware_scenario, attempt_number=attempt_number,
        direction=directions[2])
    if no_window is not None:
        raise RunnerError("unexpected_rx_window")
    initiator_keys = _end(initiator, session, attempt, "I", firmware_scenario)
    responder_keys = _end(responder, session, attempt, "R", firmware_scenario)
    if initiator_keys[0] != responder_keys[1] or initiator_keys[1] != responder_keys[0]:
        raise RunnerError("split_key_cross_match_failed")
    return [first, second, third]


def _summary(frames: list[dict[str, Any]], bounded_retry_result: str) -> dict[str, Any]:
    return {
        "handshake_total_wire_bytes": sum(frame["wire_bytes"] for frame in frames),
        "fragments": len(frames),
        "measured_airtime_us": sum(frame["measured_airtime_us"] for frame in frames),
        "theoretical_airtime_us": sum(frame["theoretical_airtime_us"] for frame in frames),
        "bounded_retry_result": bounded_retry_result,
    }


def _privacy_check(value: Any, depth: int = 0) -> None:
    if depth > 12:
        raise RunnerError("result_depth_invalid")
    forbidden = (
        "port", "path", "endpoint", "device", "mac", "session", "secret",
        "private", "raw_payload", "key_sha",
    )
    if isinstance(value, dict):
        for key, item in value.items():
            if any(token in str(key).lower() for token in forbidden):
                raise RunnerError("result_privacy_invalid")
            _privacy_check(item, depth + 1)
    elif isinstance(value, list):
        if len(value) > 100:
            raise RunnerError("result_items_invalid")
        for item in value:
            _privacy_check(item, depth + 1)
    elif isinstance(value, float) and not math.isfinite(value):
        raise RunnerError("result_number_invalid")


def _keys(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise RunnerError(f"result_{label}_shape_invalid")
    return value


def _strict_values(actual: Any, expected: dict[str, Any], label: str) -> dict[str, Any]:
    mapping = _keys(actual, set(expected), label)
    if any(type(mapping[key]) is not type(item) or mapping[key] != item
           for key, item in expected.items()):
        raise RunnerError(f"result_{label}_invalid")
    return mapping


def _positive_int(value: Any, label: str) -> int:
    if type(value) is not int or value <= 0:
        raise RunnerError(f"result_{label}_invalid")
    return value


def validate_public_result(value: dict[str, Any]) -> dict[str, Any]:
    _privacy_check(value)
    top = _keys(value, {"schema", "version", "result", "radio_profile", "cycles", "totals", "claims"}, "top")
    if top["schema"] != SCHEMA or type(top["version"]) is not int or top["version"] != 0:
        raise RunnerError("result_schema_invalid")
    if top["result"] != "noise_xk_radio_cost_measurement_passed":
        raise RunnerError("result_identity_invalid")
    _strict_values(top["radio_profile"], PROFILE, "profile")
    expected_claims = {
        "packet_v1_selected": False, "candidate_selected": False,
        "suite_selected": False, "phase_two_complete": False,
        "regulatory_acceptance_proven": False, "production_ready": False,
        "score_credit_added": False,
    }
    _strict_values(top["claims"], expected_claims, "claims")
    cycles = top["cycles"]
    if not isinstance(cycles, list) or len(cycles) != 2:
        raise RunnerError("result_cycles_invalid")
    all_frames: list[dict[str, Any]] = []
    for cycle_number, cycle_value in enumerate(cycles, 1):
        cycle = _keys(cycle_value, {"cycle", "baseline", "bounded_retry"}, "cycle")
        if type(cycle["cycle"]) is not int or cycle["cycle"] != cycle_number:
            raise RunnerError("result_cycle_invalid")
        direction_forward = "A_to_B" if cycle_number == 1 else "B_to_A"
        direction_reverse = "B_to_A" if cycle_number == 1 else "A_to_B"
        expected_sequences = {
            "baseline": [
                (1, "m1", direction_forward), (1, "m2", direction_reverse),
                (1, "m3", direction_forward),
            ],
            "bounded_retry": [
                (1, "m1", direction_forward), (2, "m1", direction_forward),
                (2, "m2", direction_reverse), (2, "m3", direction_forward),
            ],
        }
        for scenario_name in ("baseline", "bounded_retry"):
            scenario = _keys(cycle[scenario_name], {"frames", "summary"}, "scenario")
            frames = scenario["frames"]
            if not isinstance(frames, list) or len(frames) != len(expected_sequences[scenario_name]):
                raise RunnerError("result_frame_count_invalid")
            for frame_value, expected_frame in zip(frames, expected_sequences[scenario_name]):
                frame = _keys(frame_value, {
                    "cycle", "scenario", "attempt", "message", "direction",
                    "wire_bytes", "payload_sha256", "tx_start_mono_us",
                    "tx_done_mono_us", "measured_airtime_us", "theoretical_airtime_us",
                }, "frame")
                attempt_number, message, direction = expected_frame
                if (type(frame["cycle"]) is not int or
                        type(frame["attempt"]) is not int or
                        not all(type(frame[key]) is str
                                for key in ("scenario", "message", "direction")) or
                        (frame["cycle"], frame["scenario"], frame["attempt"],
                         frame["message"], frame["direction"]) != (
                            cycle_number, scenario_name, attempt_number, message, direction)):
                    raise RunnerError("result_frame_sequence_invalid")
                if (type(frame["wire_bytes"]) is not int or
                        type(frame["theoretical_airtime_us"]) is not int or
                        frame["wire_bytes"] != WIRE_BYTES[message] or
                        frame["theoretical_airtime_us"] != THEORETICAL_US[message]):
                    raise RunnerError("result_frame_contract_invalid")
                if not isinstance(frame["payload_sha256"], str) or HASH64.fullmatch(frame["payload_sha256"]) is None:
                    raise RunnerError("result_frame_digest_invalid")
                start_us = _positive_int(frame["tx_start_mono_us"], "frame_start")
                done_us = _positive_int(frame["tx_done_mono_us"], "frame_done")
                measured_us = _positive_int(frame["measured_airtime_us"], "frame_measured")
                if done_us < start_us or measured_us != done_us - start_us:
                    raise RunnerError("result_frame_timing_invalid")
                all_frames.append(frame)
            summary = _keys(scenario["summary"], {
                "handshake_total_wire_bytes", "fragments", "measured_airtime_us",
                "theoretical_airtime_us", "bounded_retry_result",
            }, "summary")
            expected_result = "not_applicable" if scenario_name == "baseline" else "one_timeout_one_retry_final_success"
            _strict_values(summary, _summary(frames, expected_result), "summary")
    totals = _keys(top["totals"], {
        "role_cycles", "baseline_handshakes", "bounded_retry_handshakes",
        "forced_timeouts", "successful_final_handshakes",
        "radio_payload_wire_bytes", "fragments", "theoretical_airtime_us",
        "measured_airtime_us", "lost", "duplicates", "corrupt", "unexpected",
    }, "totals")
    expected_totals = {
        "role_cycles": 2, "baseline_handshakes": 2, "bounded_retry_handshakes": 2,
        "forced_timeouts": 2, "successful_final_handshakes": 4,
        "radio_payload_wire_bytes": 736, "fragments": 14,
        "theoretical_airtime_us": 1_447_424,
        "measured_airtime_us": sum(frame["measured_airtime_us"] for frame in all_frames),
        "lost": 0, "duplicates": 0, "corrupt": 0, "unexpected": 0,
    }
    _strict_values(totals, expected_totals, "totals")
    return value


def run(
    node_a: Endpoint,
    node_b: Endpoint,
    *,
    token_factory: Callable[[], str] = _token,
) -> dict[str, Any]:
    """Run both role cycles with one device-observed bounded restart each."""
    _restart(node_a)
    _restart(node_b)
    seen: set[str] = set()

    def fresh_token() -> str:
        token = token_factory()
        _label_hash(token)
        if token in seen:
            raise RunnerError("private_identity_reused")
        seen.add(token)
        return token

    cycles: list[dict[str, Any]] = []
    all_frames: list[dict[str, Any]] = []
    for cycle, (initiator, responder, directions) in enumerate((
        (node_a, node_b, ("A_to_B", "B_to_A", "A_to_B")),
        (node_b, node_a, ("B_to_A", "A_to_B", "B_to_A")),
    ), 1):
        session = fresh_token()
        baseline_attempt = fresh_token()
        _prepare(initiator, session, baseline_attempt, "I", "baseline")
        baseline_responder_window = _prepare(responder, session, baseline_attempt, "R", "baseline")
        if baseline_responder_window is None:
            raise RunnerError("m1_window_missing")
        baseline_frames = _complete_handshake(
            initiator, responder, session, baseline_attempt, baseline_responder_window,
            cycle=cycle, public_scenario="baseline", firmware_scenario="baseline",
            attempt_number=1, directions=directions)

        retry_attempt_1 = fresh_token()
        _prepare(initiator, session, retry_attempt_1, "I", "retry-m2-withheld")
        retry_responder_window = _prepare(responder, session, retry_attempt_1, "R", "retry-m2-withheld")
        if retry_responder_window is None:
            raise RunnerError("retry_m1_window_missing")
        first, initiator_timeout_window = _send(
            initiator, responder, session, retry_attempt_1, "m1", retry_responder_window,
            cycle=cycle, public_scenario="bounded_retry",
            firmware_scenario="retry-m2-withheld", attempt_number=1,
            direction=directions[0])
        if initiator_timeout_window is None:
            raise RunnerError("timeout_window_missing")
        _withhold(responder, session, retry_attempt_1)
        _timeout(initiator, session, retry_attempt_1, initiator_timeout_window)
        _abort(responder, session, retry_attempt_1)

        retry_attempt_2 = fresh_token()
        _prepare(initiator, session, retry_attempt_2, "I", "retry-restart")
        restart_responder_window = _prepare(responder, session, retry_attempt_2, "R", "retry-restart")
        if restart_responder_window is None:
            raise RunnerError("restart_m1_window_missing")
        retry_frames = [first, *_complete_handshake(
            initiator, responder, session, retry_attempt_2, restart_responder_window,
            cycle=cycle, public_scenario="bounded_retry",
            firmware_scenario="retry-restart", attempt_number=2, directions=directions)]
        cycle_result = {
            "cycle": cycle,
            "baseline": {"frames": baseline_frames,
                         "summary": _summary(baseline_frames, "not_applicable")},
            "bounded_retry": {"frames": retry_frames,
                              "summary": _summary(retry_frames, "one_timeout_one_retry_final_success")},
        }
        cycles.append(cycle_result)
        all_frames.extend(baseline_frames)
        all_frames.extend(retry_frames)
    expected_counters = {
        "tx_attempted": 7, "tx_sent": 7, "tx_failed": 0,
        "rx_accepted": 7, "rx_rejected": 0, "lost": 0, "duplicates": 0,
        "corrupt": 0, "unexpected": 0, "forced_timeouts": 1,
    }
    observed_a = _status(node_a, expected_counters, command=True)
    observed_b = _status(node_b, expected_counters, command=True)
    result = {
        "schema": SCHEMA, "version": 0,
        "result": "noise_xk_radio_cost_measurement_passed",
        "radio_profile": PROFILE, "cycles": cycles,
        "totals": {
            "role_cycles": 2, "baseline_handshakes": 2,
            "bounded_retry_handshakes": 2,
            "forced_timeouts": observed_a["forced_timeouts"] + observed_b["forced_timeouts"],
            "successful_final_handshakes": 4,
            "radio_payload_wire_bytes": sum(frame["wire_bytes"] for frame in all_frames),
            "fragments": len(all_frames),
            "theoretical_airtime_us": sum(frame["theoretical_airtime_us"] for frame in all_frames),
            "measured_airtime_us": sum(frame["measured_airtime_us"] for frame in all_frames),
            "lost": observed_a["lost"] + observed_b["lost"],
            "duplicates": observed_a["duplicates"] + observed_b["duplicates"],
            "corrupt": observed_a["corrupt"] + observed_b["corrupt"],
            "unexpected": observed_a["unexpected"] + observed_b["unexpected"],
        },
        "claims": {
            "packet_v1_selected": False, "candidate_selected": False,
            "suite_selected": False, "phase_two_complete": False,
            "regulatory_acceptance_proven": False, "production_ready": False,
            "score_credit_added": False,
        },
    }
    return validate_public_result(result)


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
        allow_nan=False).encode("ascii")
