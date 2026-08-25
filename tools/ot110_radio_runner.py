#!/usr/bin/env python3
"""Privacy-safe two-node runner for the frozen OT-110 radio sequence.

The emitted OTRER0 receipt is an execution receipt, not OTRPE0 physical
evidence and not an admission decision.  Private serial-port values are used
only to open the two endpoints and are never copied to output or errors.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import queue
import re
import secrets
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Mapping, Protocol


SCHEMA = "OTRER0"
VERSION = 0
ARTIFACT_KIND = "ot110_direct_radio_execution_receipt"
PROFILE = {
    "frequency_hz": 915_000_000,
    "bandwidth_hz": 125_000,
    "spreading_factor": 7,
    "coding_rate_denominator": 5,
    "power_dbm": 2,
    "preamble_symbols": 8,
    "explicit_header": True,
    "crc_enabled": True,
    "low_data_rate_optimization": False,
    "sync_word": "0x12",
    "scope": "driver_command_acceptance",
    "calibrated": False,
}
PRIVATE_TEXT = re.compile(
    r"(?i)(?:\bCOM\d+\b|(?:[0-9a-f]{2}:){5}[0-9a-f]{2}|"
    r"\b(?:device|ble|radio)[-_ ]?(?:id|mac)\b|"
    r"[a-z]:[\\/]|/(?:home|users|mnt)/|\\\\|"
    r"\b(?:latitude|longitude|coordinates?|private[_ -]?key|radio[_ -]?key)\b)"
)
HASH8 = re.compile(r"^[0-9a-f]{8}$")
TOKEN_KEY = re.compile(r"^[a-z][a-z0-9_]*$")
TOKEN_VALUE = re.compile(r"^[A-Za-z0-9_.+>:-]+$")
ALLOWED_KINDS = {
    "BOOT", "PROFILE", "STATUS", "ARM", "ACK_ARM", "TX", "RX",
    "REJECT", "RX_ERROR", "RX_ARM", "RESTART", "SESSION_START",
    "SESSION_END", "COMMANDS",
}
RESPONDER_TURNAROUND_BOUND_MS = 500
SCHEDULING_MARGIN_MS = 1500


class RunnerError(RuntimeError):
    """A deliberately detail-free fail-closed runner error."""


@dataclass(frozen=True)
class Receipt:
    kind: str
    fields: Mapping[str, str]
    observed_at: float


class Endpoint(Protocol):
    def write_command(self, command: str) -> None: ...
    def next_receipt(self, timeout_seconds: float) -> Receipt: ...
    def reopen(self) -> None: ...
    def close(self) -> None: ...


_KEY_ORDERS = {
    frozenset(("run", "reset", "tx_at_boot")): ("run", "reset", "tx_at_boot"),
    frozenset(("run", "configured", "begin", "header", "crc", "ldro", "frequency_hz", "bandwidth_hz", "sf", "cr_denom", "power_dbm", "preamble", "explicit", "crc_enabled", "ldro_mode", "sync", "scope", "calibrated")): ("run", "configured", "begin", "header", "crc", "ldro", "frequency_hz", "bandwidth_hz", "sf", "cr_denom", "power_dbm", "preamble", "explicit", "crc_enabled", "ldro_mode", "sync", "scope", "calibrated"),
    frozenset(("run", "ready", "armed", "ack_armed", "ack_remaining", "attempted", "sent", "tx_fail", "rx_valid", "rx_invalid", "rx_read_error", "rx_restart_fail", "last", "max_wire")): ("run", "ready", "armed", "ack_armed", "ack_remaining", "attempted", "sent", "tx_fail", "rx_valid", "rx_invalid", "rx_read_error", "rx_restart_fail", "last", "max_wire"),
    frozenset(("accepted", "session", "uses", "expires_ms")): ("accepted", "session", "uses", "expires_ms"),
    frozenset(("accepted", "role", "session", "remaining", "expires_ms")): ("accepted", "role", "session", "remaining", "expires_ms"),
    frozenset(("kind", "result", "mono_us", "role", "session", "dir", "seq", "wire", "hash", "rx_restart")): ("kind", "result", "mono_us", "role", "session", "dir", "seq", "wire", "hash", "rx_restart"),
    frozenset(("kind", "valid", "mono_us", "wire", "hash", "rssi_dbm", "snr_db", "role", "session", "dir", "seq")): ("kind", "valid", "mono_us", "wire", "hash", "rssi_dbm", "snr_db", "role", "session", "dir", "seq"),
    frozenset(("kind", "valid", "mono_us", "wire", "hash", "rssi_dbm", "snr_db")): ("kind", "valid", "mono_us", "wire", "hash", "rssi_dbm", "snr_db"),
    frozenset(("kind", "reason", "requested", "transmitted", "arm_consumed")): ("kind", "reason", "requested", "transmitted", "arm_consumed"),
    frozenset(("result", "mono_us", "wire")): ("result", "mono_us", "wire"),
    frozenset(("result",)): ("result",),
    frozenset(("accepted", "tx")): ("accepted", "tx"),
    frozenset(("run", "session", "accepted", "tx", "permits_cleared")): ("run", "session", "accepted", "tx", "permits_cleared"),
}


def _exact_keys(fields: Mapping[str, str], expected: set[str]) -> None:
    order = _KEY_ORDERS.get(frozenset(expected))
    if order is None or tuple(fields) != order:
        raise RunnerError("receipt shape or order mismatch")


def _validate_receipt_shape(receipt: Receipt) -> None:
    f = receipt.fields
    kind = receipt.kind
    if kind == "BOOT":
        _exact_keys(f, {"run", "reset", "tx_at_boot"})
    elif kind == "PROFILE":
        _exact_keys(f, {"run", "configured", "begin", "header", "crc", "ldro",
                        "frequency_hz", "bandwidth_hz", "sf", "cr_denom",
                        "power_dbm", "preamble", "explicit", "crc_enabled",
                        "ldro_mode", "sync", "scope", "calibrated"})
    elif kind == "STATUS":
        _exact_keys(f, {"run", "ready", "armed", "ack_armed", "ack_remaining",
                        "attempted", "sent", "tx_fail", "rx_valid", "rx_invalid",
                        "rx_read_error", "rx_restart_fail", "last", "max_wire"})
    elif kind == "ARM":
        _exact_keys(f, {"accepted", "session", "uses", "expires_ms"})
    elif kind == "ACK_ARM":
        _exact_keys(f, {"accepted", "role", "session", "remaining", "expires_ms"})
    elif kind == "TX":
        _exact_keys(f, {"kind", "result", "mono_us", "role", "session", "dir",
                        "seq", "wire", "hash", "rx_restart"})
    elif kind == "RX":
        frame_kind = f.get("kind")
        if frame_kind in {"data", "ack"}:
            _exact_keys(f, {"kind", "valid", "mono_us", "wire", "hash", "rssi_dbm",
                            "snr_db", "role", "session", "dir", "seq"})
        elif frame_kind in {"probe", "unknown"}:
            _exact_keys(f, {"kind", "valid", "mono_us", "wire", "hash", "rssi_dbm", "snr_db"})
        else:
            raise RunnerError("unknown RX kind")
    elif kind == "REJECT":
        # The runner accepts only the dedicated 256-byte rejection shape.
        _exact_keys(f, {"kind", "reason", "requested", "transmitted", "arm_consumed"})
    elif kind in {"RX_ERROR", "RX_ARM"}:
        _exact_keys(f, {"result", "mono_us", "wire"} if kind == "RX_ERROR" else {"result"})
    elif kind == "RESTART":
        _exact_keys(f, {"accepted", "tx"})
    elif kind in {"SESSION_START", "SESSION_END"}:
        _exact_keys(f, {"run", "session", "accepted", "tx", "permits_cleared"})
    elif kind == "COMMANDS":
        return
    else:
        raise RunnerError("unknown receipt kind")


def parse_receipt(line: str, observed_at: float | None = None) -> Receipt | None:
    """Parse one privacy-safe OTD receipt; non-OTD boot noise is ignored."""
    if len(line) > 1024:
        raise RunnerError("serial line too long")
    marker = line.find("OTD ")
    if marker < 0:
        return None
    body = line[marker + 4 :].strip()
    pieces = body.split()
    if not pieces or pieces[0] not in ALLOWED_KINDS:
        raise RunnerError("invalid receipt")
    kind = pieces[0]
    if kind == "COMMANDS":
        return None
    fields: dict[str, str] = {}
    for piece in pieces[1:]:
        if piece.count("=") != 1:
            raise RunnerError("invalid receipt token")
        key, value = piece.split("=", 1)
        if not TOKEN_KEY.fullmatch(key) or not value or not TOKEN_VALUE.fullmatch(value):
            raise RunnerError("invalid receipt token")
        if key in fields:
            raise RunnerError("duplicate receipt key")
        fields[key] = value
    receipt = Receipt(kind, fields, time.monotonic() if observed_at is None else observed_at)
    _validate_receipt_shape(receipt)
    return receipt


def _u32(value: str) -> int:
    try:
        parsed = int(value, 10)
    except (TypeError, ValueError) as exc:
        raise RunnerError("invalid integer") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise RunnerError("integer out of range")
    return parsed


def _u64(value: str) -> int:
    try:
        parsed = int(value, 10)
    except (TypeError, ValueError) as exc:
        raise RunnerError("invalid integer") from exc
    if not 0 <= parsed <= 0xFFFFFFFFFFFFFFFF:
        raise RunnerError("integer out of range")
    return parsed


def _i16(value: str) -> int:
    try:
        parsed = int(value, 10)
    except (TypeError, ValueError) as exc:
        raise RunnerError("invalid integer") from exc
    if not -32768 <= parsed <= 32767:
        raise RunnerError("integer out of range")
    return parsed


def _float(value: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise RunnerError("invalid float") from exc
    if not math.isfinite(parsed):
        raise RunnerError("non-finite float")
    return parsed


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


class ReceiptChain:
    def __init__(self) -> None:
        self._state = hashlib.sha256(b"OTRER0/v0\0").digest()

    def add(self, node: str, receipt: Receipt) -> None:
        normalized = {"node": node, "kind": receipt.kind,
                      "fields": list(receipt.fields.items())}
        self._state = hashlib.sha256(self._state + _canonical_bytes(normalized)).digest()

    @property
    def hexdigest(self) -> str:
        return self._state.hex()


def theoretical_lora_airtime_ms(payload_bytes: int) -> float:
    if not 1 <= payload_bytes <= 255:
        raise RunnerError("payload length out of range")
    sf = PROFILE["spreading_factor"]
    bw = PROFILE["bandwidth_hz"]
    cr = PROFILE["coding_rate_denominator"] - 4
    crc = 1
    implicit_header = 0
    low_data_rate = 0
    symbol_seconds = (2**sf) / bw
    numerator = 8 * payload_bytes - 4 * sf + 28 + 16 * crc - 20 * implicit_header
    denominator = 4 * (sf - 2 * low_data_rate)
    payload_symbols = 8 + max(math.ceil(numerator / denominator) * (cr + 4), 0)
    preamble_symbols = PROFILE["preamble_symbols"] + 4.25
    return round((preamble_symbols + payload_symbols) * symbol_seconds * 1000.0, 6)


def timeout_policy() -> dict[str, Any]:
    airtime = {str(size): theoretical_lora_airtime_ms(size) for size in (1, 16, 163, 255)}
    by_wire = {
        size: math.ceil(
            airtime[size] + airtime["16"]
            + RESPONDER_TURNAROUND_BOUND_MS + SCHEDULING_MARGIN_MS
        )
        for size in ("163", "255")
    }
    return {
        "classification": "exact_data_plus_ota1_airtime_plus_fixed_turnaround_and_margin",
        "receipt_timeout_ms": max(by_wire.values()),
        "receipt_timeout_ms_by_data_wire_bytes": by_wire,
        "responder_turnaround_bound_ms": RESPONDER_TURNAROUND_BOUND_MS,
        "scheduling_margin_ms": SCHEDULING_MARGIN_MS,
        "theoretical_airtime_ms": airtime,
    }


def _fnv1a32(payload: bytes) -> str:
    value = 0x811C9DC5
    for byte in payload:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return f"{value:08x}"


def _fill_byte(role: str, direction: str, session: int, sequence: int, index: int) -> int:
    role_value = 1 if role == "A" else 2
    direction_value = 1 if direction == "A>B" else 2
    value = (session ^ ((sequence * 0x9E3779B9) & 0xFFFFFFFF)
             ^ (role_value << 24) ^ (direction_value << 16) ^ index) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    return (value ^ (value >> 8)) & 0xFF


def _data_payload(role: str, direction: str, session: int, sequence: int,
                  wire_bytes: int) -> bytes:
    if not 17 <= wire_bytes <= 255:
        raise RunnerError("data wire length out of range")
    role_value = 1 if role == "A" else 2
    direction_value = 1 if direction == "A>B" else 2
    header = (b"OTD1" + bytes((1, role_value, direction_value, 16))
              + session.to_bytes(4, "little") + sequence.to_bytes(4, "little"))
    fill = bytes(_fill_byte(role, direction, session, sequence, index)
                 for index in range(wire_bytes - 16))
    return header + fill


def _ack_payload(role: str, direction: str, session: int, sequence: int) -> bytes:
    role_value = 1 if role == "A" else 2
    direction_value = 1 if direction == "A>B" else 2
    return (b"OTA1" + bytes((1, role_value, direction_value, 16))
            + session.to_bytes(4, "little") + sequence.to_bytes(4, "little"))


def _probe_payload(role: str, direction: str, session: int, sequence: int) -> bytes:
    del role, direction, session, sequence
    return b"\xA5"


def _expect(endpoint: Endpoint, node: str, chain: ReceiptChain, kind: str,
            timeout_seconds: float, expected: Mapping[str, str] | None = None,
            predicate: Callable[[Receipt], bool] | None = None) -> Receipt:
    deadline = time.monotonic() + timeout_seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RunnerError("receipt timeout")
        receipt = endpoint.next_receipt(remaining)
        chain.add(node, receipt)
        if receipt.kind in {"RX_ERROR"}:
            raise RunnerError("radio receive error")
        if receipt.kind == "REJECT" and kind != "REJECT":
            raise RunnerError("unexpected rejection")
        if receipt.kind != kind:
            raise RunnerError("unexpected receipt order")
        if expected is not None:
            for key, value in expected.items():
                if receipt.fields.get(key) != value:
                    raise RunnerError("receipt value mismatch")
        if predicate is not None and not predicate(receipt):
            raise RunnerError("receipt predicate mismatch")
        return receipt


def _command_expect(endpoint: Endpoint, node: str, chain: ReceiptChain,
                    command: str, kind: str, timeout_seconds: float,
                    expected: Mapping[str, str] | None = None) -> Receipt:
    endpoint.write_command(command)
    return _expect(endpoint, node, chain, kind, timeout_seconds, expected)


def _profile_values(receipt: Receipt) -> dict[str, Any]:
    f = receipt.fields
    if (f["configured"] != "yes" or any(_i16(f[name]) != 0 for name in
        ("begin", "header", "crc", "ldro")) or f["explicit"] != "yes" or
        f["crc_enabled"] != "yes" or f["ldro_mode"] != "off" or
        f["scope"] != PROFILE["scope"] or f["calibrated"] != "no"):
        raise RunnerError("profile not accepted")
    values = {
        "frequency_hz": _u32(f["frequency_hz"]),
        "bandwidth_hz": _u32(f["bandwidth_hz"]),
        "spreading_factor": _u32(f["sf"]),
        "coding_rate_denominator": _u32(f["cr_denom"]),
        "power_dbm": int(f["power_dbm"]),
        "preamble_symbols": _u32(f["preamble"]),
        "explicit_header": True,
        "crc_enabled": True,
        "low_data_rate_optimization": False,
        "sync_word": f["sync"],
        "scope": f["scope"],
        "calibrated": False,
    }
    if values != PROFILE:
        raise RunnerError("profile mismatch")
    return values


COUNTER_KEYS = ("attempted", "sent", "tx_fail", "rx_valid", "rx_invalid",
                "rx_read_error", "rx_restart_fail")


def _status_values(receipt: Receipt, run: int, *, require_idle: bool = True) -> dict[str, int]:
    f = receipt.fields
    if (_u32(f["run"]) != run or f["ready"] != "yes" or f["max_wire"] != "255"
            or _i16(f["last"]) != 0):
        raise RunnerError("status mismatch")
    if require_idle and (f["armed"] != "no" or f["ack_armed"] != "no"
                         or _u32(f["ack_remaining"]) != 0):
        raise RunnerError("status not idle")
    return {key: _u32(f[key]) for key in COUNTER_KEYS}


def _start_session(endpoint: Endpoint, node: str, chain: ReceiptChain, session: int,
                   timeout_seconds: float) -> tuple[int, dict[str, Any], dict[str, int]]:
    endpoint.write_command(f"session-start {session}")
    deadline = time.monotonic() + timeout_seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RunnerError("session-start timeout")
        receipt = endpoint.next_receipt(remaining)
        if receipt.kind in {"BOOT", "PROFILE", "STATUS"}:
            continue
        if receipt.kind != "SESSION_START":
            raise RunnerError("unexpected pre-session receipt")
        chain.add(node, receipt)
        if (receipt.fields.get("session") != str(session)
                or receipt.fields.get("accepted") != "yes"
                or receipt.fields.get("tx") != "no"
                or receipt.fields.get("permits_cleared") != "yes"):
            raise RunnerError("session-start mismatch")
        run = _u32(receipt.fields["run"])
        if run == 0:
            raise RunnerError("zero run nonce")
        break
    profile_receipt = _expect(endpoint, node, chain, "PROFILE", timeout_seconds,
                              {"run": str(run)})
    profile = _profile_values(profile_receipt)
    status = _expect(endpoint, node, chain, "STATUS", timeout_seconds,
                     {"run": str(run), "armed": "no", "ack_armed": "no",
                      "ack_remaining": "0"})
    counters = _status_values(status, run)
    return run, profile, counters


def _end_session(endpoint: Endpoint, node: str, chain: ReceiptChain, run: int,
                 session: int, timeout_seconds: float) -> None:
    _command_expect(endpoint, node, chain, f"session-end {session}", "SESSION_END",
                    timeout_seconds,
                    {"run": str(run), "session": str(session), "accepted": "yes",
                     "tx": "no", "permits_cleared": "yes"})


def _query_status(endpoint: Endpoint, node: str, chain: ReceiptChain, run: int,
                  timeout_seconds: float, *, require_idle: bool = True) -> dict[str, int]:
    receipt = _command_expect(endpoint, node, chain, "status", "STATUS",
                              timeout_seconds, {"run": str(run)})
    return _status_values(receipt, run, require_idle=require_idle)


def _assert_counters(actual: Mapping[str, int], expected: Mapping[str, int]) -> None:
    if dict(actual) != dict(expected):
        raise RunnerError("counter reconciliation failed")


def _zero_counters() -> dict[str, int]:
    return {key: 0 for key in COUNTER_KEYS}


def _arm_rx(endpoint: Endpoint, node: str, chain: ReceiptChain,
            timeout_seconds: float) -> None:
    _command_expect(endpoint, node, chain, "rx", "RX_ARM", timeout_seconds,
                    {"result": "0"})


def _arm_tx(endpoint: Endpoint, node: str, chain: ReceiptChain, session: int,
            timeout_seconds: float) -> None:
    _command_expect(endpoint, node, chain, "arm", "ARM", timeout_seconds,
                    {"accepted": "yes", "session": str(session),
                     "uses": "1", "expires_ms": "30000"})


def _tx_expected(kind: str, role: str, session: int, direction: str,
                 sequence: int, wire: int, hash8: str) -> dict[str, str]:
    return {"kind": kind, "result": "0", "role": role, "session": str(session),
            "dir": direction, "seq": str(sequence), "wire": str(wire),
            "hash": hash8, "rx_restart": "0"}


def _rx_expected(kind: str, role: str, session: int, direction: str,
                 sequence: int, wire: int, hash8: str) -> dict[str, str]:
    return {"kind": kind, "valid": "yes", "role": role, "session": str(session),
            "dir": direction, "seq": str(sequence), "wire": str(wire), "hash": hash8}


def _signal(receipt: Receipt) -> tuple[float, float]:
    return _float(receipt.fields["rssi_dbm"]), _float(receipt.fields["snr_db"])


def _probe(sender: Endpoint, receiver: Endpoint, sender_node: str, receiver_node: str,
           chain: ReceiptChain, session: int, sequence: int, direction: str,
           timeout_seconds: float) -> dict[str, Any]:
    role = sender_node
    payload = _probe_payload(role, direction, session, sequence)
    hash8 = _fnv1a32(payload)
    _arm_tx(sender, sender_node, chain, session, timeout_seconds)
    sender.write_command(f"probe {role} {session} {direction} {sequence}")
    _expect(sender, sender_node, chain, "TX", timeout_seconds,
            _tx_expected("probe", role, session, direction, sequence, 1, hash8))
    rx = _expect(receiver, receiver_node, chain, "RX", timeout_seconds,
                 {"kind": "probe", "valid": "yes", "wire": "1", "hash": hash8})
    rssi, snr = _signal(rx)
    return {"phase": "one_byte_probe", "session": session, "direction": direction, "sequence": sequence,
            "wire_bytes": 1, "data_wire_sha256": hashlib.sha256(payload).hexdigest(),
            "ack_wire_sha256": None, "ack_timeout_ms": None, "rtt_ms": None,
            "data_rssi_dbm": rssi, "data_snr_db": snr,
            "ack_rssi_dbm": None, "ack_snr_db": None}


def _data_batch(sender: Endpoint, receiver: Endpoint, sender_node: str,
                receiver_node: str, chain: ReceiptChain, session: int,
                sequence_start: int, direction: str, wire_bytes: int, count: int,
                phase: str, timeout_seconds: float) -> list[dict[str, Any]]:
    _command_expect(receiver, receiver_node, chain,
                    f"ack-arm {receiver_node} {session} {count}", "ACK_ARM",
                    timeout_seconds,
                    {"accepted": "yes", "role": receiver_node, "session": str(session),
                     "remaining": str(count), "expires_ms": "120000"})
    records: list[dict[str, Any]] = []
    reverse = "B>A" if direction == "A>B" else "A>B"
    for offset in range(count):
        sequence = sequence_start + offset
        payload = _data_payload(sender_node, direction, session, sequence, wire_bytes)
        data_hash = _fnv1a32(payload)
        _arm_tx(sender, sender_node, chain, session, timeout_seconds)
        sender.write_command(
            f"send {sender_node} {session} {direction} {sequence} {wire_bytes}"
        )
        tx = _expect(sender, sender_node, chain, "TX", timeout_seconds,
                     _tx_expected("data", sender_node, session, direction,
                                  sequence, wire_bytes, data_hash))
        rx = _expect(receiver, receiver_node, chain, "RX", timeout_seconds,
                     _rx_expected("data", sender_node, session, direction,
                                  sequence, wire_bytes, data_hash))
        ack_tx = _expect(receiver, receiver_node, chain, "TX", timeout_seconds,
                         predicate=lambda item: (
                             item.fields.get("kind") == "ack"
                             and item.fields.get("result") == "0"
                             and item.fields.get("role") == receiver_node
                             and item.fields.get("session") == str(session)
                             and item.fields.get("dir") == reverse
                             and item.fields.get("seq") == str(sequence)
                             and item.fields.get("wire") == "16"
                             and item.fields.get("rx_restart") == "0"
                             and HASH8.fullmatch(item.fields.get("hash", "")) is not None
                         ))
        ack_payload = _ack_payload(receiver_node, reverse, session, sequence)
        ack_hash = _fnv1a32(ack_payload)
        if ack_tx.fields["hash"] != ack_hash:
            raise RunnerError("ACK payload hash mismatch")
        ack_rx = _expect(sender, sender_node, chain, "RX", timeout_seconds,
                         _rx_expected("ack", receiver_node, session, reverse,
                                      sequence, 16, ack_hash))
        data_rssi, data_snr = _signal(rx)
        ack_rssi, ack_snr = _signal(ack_rx)
        tx_mono_us = _u64(tx.fields["mono_us"])
        ack_rx_mono_us = _u64(ack_rx.fields["mono_us"])
        rtt_ms = round((ack_rx_mono_us - tx_mono_us) / 1000.0, 3)
        if rtt_ms < 0 or rtt_ms > timeout_seconds * 1000.0:
            raise RunnerError("RTT outside timeout")
        records.append({
            "phase": phase,
            "session": session,
            "direction": direction,
            "sequence": sequence,
            "wire_bytes": wire_bytes,
            "data_wire_sha256": hashlib.sha256(payload).hexdigest(),
            "ack_wire_sha256": hashlib.sha256(ack_payload).hexdigest(),
            "ack_timeout_ms": math.ceil(timeout_seconds * 1000.0),
            "rtt_ms": rtt_ms,
            "data_rssi_dbm": data_rssi,
            "data_snr_db": data_snr,
            "ack_rssi_dbm": ack_rssi,
            "ack_snr_db": ack_snr,
        })
    return records


def _metrics(records: list[dict[str, Any]], attempted: int) -> dict[str, Any]:
    rtts = sorted(record["rtt_ms"] for record in records if record["rtt_ms"] is not None)
    data_rssis = [record["data_rssi_dbm"] for record in records]
    data_snrs = [record["data_snr_db"] for record in records]
    ack_rssis = [record["ack_rssi_dbm"] for record in records if record["ack_rssi_dbm"] is not None]
    ack_snrs = [record["ack_snr_db"] for record in records if record["ack_snr_db"] is not None]
    def percentile(values: list[float], fraction: float) -> float | None:
        if not values:
            return None
        return values[max(0, math.ceil(len(values) * fraction) - 1)]
    return {
        "attempted": attempted,
        "sent": len(records),
        "received": len(records),
        "lost": attempted - len(records),
        "duplicates": 0,
        "corrupt": 0,
        "unexpected": 0,
        "acknowledgements_sent": len(rtts),
        "acknowledgements_received": len(rtts),
        "radio_transmissions_including_acks": len(records) + len(rtts),
        "rtt_ms_p50": percentile(rtts, 0.50),
        "rtt_ms_p95": percentile(rtts, 0.95),
        "rtt_ms_max": max(rtts) if rtts else None,
        "data_rssi_dbm_min": min(data_rssis),
        "data_rssi_dbm_max": max(data_rssis),
        "data_snr_db_min": min(data_snrs),
        "data_snr_db_max": max(data_snrs),
        "ack_rssi_dbm_min": min(ack_rssis) if ack_rssis else None,
        "ack_rssi_dbm_max": max(ack_rssis) if ack_rssis else None,
        "ack_snr_db_min": min(ack_snrs) if ack_snrs else None,
        "ack_snr_db_max": max(ack_snrs) if ack_snrs else None,
    }


def _privacy_check(value: Any, depth: int = 0) -> None:
    if depth > 12:
        raise RunnerError("output too deep")
    if isinstance(value, dict):
        for child in value.values():
            _privacy_check(child, depth + 1)
    elif isinstance(value, list):
        for child in value:
            _privacy_check(child, depth + 1)
    elif isinstance(value, str) and PRIVATE_TEXT.search(value):
        raise RunnerError("private output text")


def run_acceptance(node_a: Endpoint, node_b: Endpoint, *, session: int | None = None) -> dict[str, Any]:
    """Execute the exact bounded sequence against two already-open endpoints."""
    session_value = session if session is not None else secrets.randbelow(0xFFFFFFFF) + 1
    if not 1 <= session_value <= 0xFFFFFFFF:
        raise RunnerError("invalid session")
    policy = timeout_policy()
    timeout_seconds = policy["receipt_timeout_ms"] / 1000.0
    timeout_163_seconds = policy["receipt_timeout_ms_by_data_wire_bytes"]["163"] / 1000.0
    timeout_255_seconds = policy["receipt_timeout_ms_by_data_wire_bytes"]["255"] / 1000.0
    chain = ReceiptChain()
    run_a, profile_a, initial_a = _start_session(
        node_a, "A", chain, session_value, timeout_seconds * 5
    )
    run_b, profile_b, initial_b = _start_session(
        node_b, "B", chain, session_value, timeout_seconds * 5
    )
    if profile_a != profile_b or profile_a != PROFILE:
        raise RunnerError("node profiles differ")
    _assert_counters(initial_a, _zero_counters())
    _assert_counters(initial_b, _zero_counters())

    # Step 1: one off-air/status success per physical node.
    _assert_counters(_query_status(node_a, "A", chain, run_a, timeout_seconds), _zero_counters())
    _assert_counters(_query_status(node_b, "B", chain, run_b, timeout_seconds), _zero_counters())

    # Step 2: configure both receivers before any transmit command.
    _arm_rx(node_a, "A", chain, timeout_seconds)
    _arm_rx(node_b, "B", chain, timeout_seconds)

    frames: list[dict[str, Any]] = []
    # Step 3: exact one-byte probes.
    frames.append(_probe(node_a, node_b, "A", "B", chain, session_value, 1,
                         "A>B", timeout_seconds))
    frames.append(_probe(node_b, node_a, "B", "A", chain, session_value, 2,
                         "B>A", timeout_seconds))
    expected = {**_zero_counters(), "attempted": 1, "sent": 1, "rx_valid": 1}
    _assert_counters(_query_status(node_a, "A", chain, run_a, timeout_seconds), expected)
    _assert_counters(_query_status(node_b, "B", chain, run_b, timeout_seconds), expected)

    # Step 4: 100 exact 163-byte total-wire DATA frames per direction.
    mtu_records = _data_batch(node_a, node_b, "A", "B", chain, session_value,
                              1000, "A>B", 163, 100, "benchmark_mtu",
                              timeout_163_seconds)
    mtu_records += _data_batch(node_b, node_a, "B", "A", chain, session_value,
                               2000, "B>A", 163, 100, "benchmark_mtu",
                               timeout_163_seconds)
    frames.extend(mtu_records)
    expected = {**_zero_counters(), "attempted": 201, "sent": 201, "rx_valid": 201}
    _assert_counters(_query_status(node_a, "A", chain, run_a, timeout_seconds), expected)
    _assert_counters(_query_status(node_b, "B", chain, run_b, timeout_seconds), expected)

    # Step 5: the firmware-supported direct ceiling, 255 total wire bytes.
    ceiling_records = _data_batch(node_a, node_b, "A", "B", chain, session_value,
                                  3000, "A>B", 255, 10, "direct_ceiling",
                                  timeout_255_seconds)
    ceiling_records += _data_batch(node_b, node_a, "B", "A", chain, session_value,
                                   4000, "B>A", 255, 10, "direct_ceiling",
                                   timeout_255_seconds)
    frames.extend(ceiling_records)
    expected = {**_zero_counters(), "attempted": 221, "sent": 221, "rx_valid": 221}
    before_reject_a = _query_status(node_a, "A", chain, run_a, timeout_seconds)
    before_reject_b = _query_status(node_b, "B", chain, run_b, timeout_seconds)
    _assert_counters(before_reject_a, expected)
    _assert_counters(before_reject_b, expected)

    # Step 6: explicit local 256-byte rejection, arm unconsumed, no peer RX.
    for endpoint, node in ((node_a, "A"), (node_b, "B")):
        _command_expect(endpoint, node, chain,
                        f"send {node} {session_value} {'A>B' if node == 'A' else 'B>A'} 5000 256",
                        "REJECT", timeout_seconds,
                        {"kind": "send", "reason": "wire_too_long", "requested": "256",
                         "transmitted": "no", "arm_consumed": "no"})
    _assert_counters(_query_status(node_a, "A", chain, run_a, timeout_seconds), before_reject_a)
    _assert_counters(_query_status(node_b, "B", chain, run_b, timeout_seconds), before_reject_b)

    # Step 7: both nodes explicitly acknowledge restart, then reopen and prove a new run.
    _command_expect(node_a, "A", chain, "restart", "RESTART", timeout_seconds,
                    {"accepted": "yes", "tx": "no"})
    _command_expect(node_b, "B", chain, "restart", "RESTART", timeout_seconds,
                    {"accepted": "yes", "tx": "no"})
    node_a.reopen()
    node_b.reopen()
    new_run_a, restart_profile_a, restart_a = _start_session(
        node_a, "A", chain, session_value, timeout_seconds * 5
    )
    new_run_b, restart_profile_b, restart_b = _start_session(
        node_b, "B", chain, session_value, timeout_seconds * 5
    )
    if new_run_a == run_a or new_run_b == run_b:
        raise RunnerError("restart nonce did not change")
    if restart_profile_a != profile_a or restart_profile_b != profile_b:
        raise RunnerError("profile changed after restart")
    _assert_counters(restart_a, _zero_counters())
    _assert_counters(restart_b, _zero_counters())
    _arm_rx(node_a, "A", chain, timeout_seconds)
    _arm_rx(node_b, "B", chain, timeout_seconds)

    # Step 8: 10 exact 163-byte total-wire DATA frames per direction.
    restart_records = _data_batch(node_a, node_b, "A", "B", chain, session_value,
                                  6000, "A>B", 163, 10, "post_restart_mtu",
                                  timeout_163_seconds)
    restart_records += _data_batch(node_b, node_a, "B", "A", chain, session_value,
                                   7000, "B>A", 163, 10, "post_restart_mtu",
                                   timeout_163_seconds)
    frames.extend(restart_records)
    expected_restart = {**_zero_counters(), "attempted": 20, "sent": 20, "rx_valid": 20}
    _assert_counters(_query_status(node_a, "A", chain, new_run_a, timeout_seconds), expected_restart)
    _assert_counters(_query_status(node_b, "B", chain, new_run_b, timeout_seconds), expected_restart)
    _end_session(node_a, "A", chain, new_run_a, session_value, timeout_seconds)
    _end_session(node_b, "B", chain, new_run_b, session_value, timeout_seconds)

    receipt = {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": ARTIFACT_KIND,
        "result": "exact_ot110_radio_sequence_passed_execution_receipt_only",
        "profile": PROFILE,
        "timeout_policy": policy,
        "steps": [
            {"step": 1, "operation": "off_air_status_preflight", "status": "passed", "successes": 2},
            {"step": 2, "operation": "configure_receivers", "status": "passed", "successes": 2},
            {"step": 3, "operation": "one_byte_probe_each_direction", "status": "passed",
             "metrics": _metrics(frames[:2], 2)},
            {"step": 4, "operation": "benchmark_mtu_each_direction", "status": "passed",
             "wire_bytes": 163, "frames_per_direction": 100,
             "metrics": _metrics(mtu_records, 200)},
            {"step": 5, "operation": "direct_ceiling_each_direction", "status": "passed",
             "wire_bytes": 255, "frames_per_direction": 10,
             "metrics": _metrics(ceiling_records, 20)},
            {"step": 6, "operation": "oversize_local_reject", "status": "passed",
             "wire_bytes": 256, "attempts_per_node": 1, "transmitted": 0},
            {"step": 7, "operation": "restart_both_nodes", "status": "passed", "successes": 2,
             "exact_profile_retained": True},
            {"step": 8, "operation": "post_restart_benchmark_mtu_each_direction",
             "status": "passed", "wire_bytes": 163, "frames_per_direction": 10,
             "metrics": _metrics(restart_records, 20)},
        ],
        "frames": frames,
        "summary": {
            "distance_class": "close_bench_no_coordinates",
            "radio_frames_attempted": 242,
            "radio_frames_received": 242,
            "ack_frames_attempted": 240,
            "ack_frames_received": 240,
            "radio_transmissions_including_acks": 482,
            "session_start_receipts": 4,
            "session_end_receipts": 2,
            "per_test_direction": {
                "A>B": {"radio_frames": 121, "ack_frames": 120,
                         "radio_transmissions_including_acks": 241},
                "B>A": {"radio_frames": 121, "ack_frames": 120,
                         "radio_transmissions_including_acks": 241},
            },
            "lost": 0,
            "duplicates": 0,
            "corrupt": 0,
            "unexpected": 0,
            "direct_payload_ceiling_bytes": 255,
            "receipt_chain_sha256": chain.hexdigest,
        },
        "privacy": {
            "serial_ports_recorded": False,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "coordinates_recorded": False,
            "raw_payload_recorded": False,
        },
        "claims": {
            "execution_receipt_generated": True,
            "physical_evidence_admitted": False,
            "blocker_closed": False,
            "benchmark_readiness_advanced": False,
            "regulatory_compliance_proven": False,
            "range_proven": False,
            "production_support_proven": False,
        },
    }
    _privacy_check(receipt)
    if len(_canonical_bytes(receipt)) > 512_000:
        raise RunnerError("output too large")
    return receipt


class SerialEndpoint:
    """Background-reader wrapper that timestamps receipts on host arrival."""

    def __init__(self, private_port: str, baud: int) -> None:
        self._private_port = private_port
        self._baud = baud
        self._serial: Any = None
        self._receipts: queue.Queue[Receipt | None] = queue.Queue(maxsize=4096)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._reconnect(10.0)

    def _open(self) -> None:
        try:
            import serial  # type: ignore
            self._serial = serial.Serial(self._private_port, self._baud, timeout=0.1,
                                         write_timeout=1.0)
        except Exception as exc:
            raise RunnerError("serial open failed") from exc
        self._stop.clear()
        self._receipts = queue.Queue(maxsize=4096)
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        while not self._stop.is_set():
            try:
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("ascii", errors="ignore").rstrip("\r\n")
                receipt = parse_receipt(line, time.monotonic())
                if receipt is not None:
                    self._receipts.put(receipt, timeout=0.2)
            except Exception:

                try:
                    self._receipts.put(None, timeout=0.2)
                except queue.Full:
                    pass
                return

    def write_command(self, command: str) -> None:
        if not re.fullmatch(r"[A-Za-z0-9 >-]+", command) or len(command) > 128:
            raise RunnerError("unsafe command")
        try:
            self._serial.write((command + "\n").encode("ascii"))
            self._serial.flush()
        except Exception as exc:
            raise RunnerError("serial write failed") from exc

    def next_receipt(self, timeout_seconds: float) -> Receipt:
        try:
            receipt = self._receipts.get(timeout=timeout_seconds)
        except queue.Empty as exc:
            raise RunnerError("receipt timeout") from exc
        if receipt is None:
            raise RunnerError("serial read failed")
        return receipt

    def _reconnect(self, timeout_seconds: float) -> None:
        deadline = time.monotonic() + timeout_seconds
        while True:
            try:
                self._open()
                return
            except RunnerError:
                if time.monotonic() >= deadline:
                    raise RunnerError("serial reconnect failed")
                time.sleep(0.25)

    def reopen(self) -> None:
        self.close()
        self._reconnect(15.0)

    def close(self) -> None:
        self._stop.set()
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
        if self._thread is not None:
            self._thread.join(timeout=1.0)
        self._thread = None
        self._serial = None


class ArgumentError(RunnerError):
    """Sanitized command-line argument failure."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ArgumentError("invalid arguments")


def _parser() -> SafeArgumentParser:
    parser = SafeArgumentParser(description="Run the bounded OT-110 two-node sequence")
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    return parser


def main(argv: list[str] | None = None) -> int:
    node_a: SerialEndpoint | None = None
    node_b: SerialEndpoint | None = None
    try:
        args = _parser().parse_args(argv)
        if not args.port_a or not args.port_b or args.port_a == args.port_b:
            raise ArgumentError("invalid arguments")
        if not 1200 <= args.baud <= 3_000_000:
            raise ArgumentError("invalid arguments")
        output_path = Path(args.output)
        if output_path.exists() or not output_path.parent.is_dir():
            raise RunnerError("invalid output")
        node_a = SerialEndpoint(args.port_a, args.baud)
        node_b = SerialEndpoint(args.port_b, args.baud)
        receipt = run_acceptance(node_a, node_b)
        output = _canonical_bytes(receipt) + b"\n"
        with output_path.open("xb") as stream:
            stream.write(output)
        return 0
    except ArgumentError:
        print("ERROR: invalid arguments", file=sys.stderr)
        return 2
    except RunnerError:
        print("ERROR: execution failed", file=sys.stderr)
        return 2
    except Exception:
        print("ERROR: execution failed", file=sys.stderr)
        return 2
    finally:
        if node_a is not None:
            node_a.close()
        if node_b is not None:
            node_b.close()


if __name__ == "__main__":
    raise SystemExit(main())
