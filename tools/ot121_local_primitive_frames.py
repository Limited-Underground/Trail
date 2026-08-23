#!/usr/bin/env python3
"""Strict host-only parser for OT-121 local primitive serial frames."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


PREFIX = b"OTCBXRF2 "
SCHEMA = "OTCBXRF2"
VERSION = 2
CANDIDATE_ID = "espressif_libsodium"
SCOPE = "candidate_local_v2"
OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
    "noise_xk_handshake",
)
PHASES = ("cold", "warm")
GATES = ("sodium_init", "primitive_vectors_and_negative_cases")
REPETITIONS = 100
EXPECTED_FRAME_COUNT = 1 + len(GATES) + len(OPERATIONS) * (2 * REPETITIONS + 2) + 2
MAX_CAPTURE_BYTES = 2_097_152
MAX_FRAME_BYTES = 2_048
MAX_DURATION_US = 2**63 - 1
PRIVATE_TEXT = re.compile(
    r"[A-Za-z]:\\|/(?:home|users)/|\\users\\|\bCOM[0-9]+\b|"
    r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b|"
    r"\b(?:pin|password|private[_ -]?key|secret|token|chip[_ -]?id|"
    r"usb[_ -]?serial)\b",
    re.IGNORECASE,
)


class FrameError(ValueError):
    """A capture is malformed, incomplete, private, or outside local scope."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise FrameError("invalid command arguments")


def _object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise FrameError("duplicate JSON key")
        value[key] = item
    return value


def _exact(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        raise FrameError(f"{label} structure mismatch")
    return value


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise FrameError(f"{label} integer mismatch")
    return value


def _common(record: dict[str, Any], kind: str) -> None:
    if (
        record.get("schema") != SCHEMA
        or type(record.get("version")) is not int
        or record["version"] != VERSION
        or record.get("record_kind") != kind
        or record.get("scope") != SCOPE
        or record.get("candidate_id") != CANDIDATE_ID
        or record.get("phase2_complete") is not False
    ):
        raise FrameError(f"{kind} identity mismatch")


def _canonical_payload(record: dict[str, Any]) -> bytes:
    return json.dumps(
        record, ensure_ascii=True, separators=(",", ":"), sort_keys=False
    ).encode("ascii")


def _decode(raw: bytes) -> list[dict[str, Any]]:
    if not raw or len(raw) > MAX_CAPTURE_BYTES:
        raise FrameError("capture size mismatch")
    if b"\x00" in raw or b"\r" in raw or not raw.endswith(b"\n"):
        raise FrameError("capture framing mismatch")
    lines = raw.splitlines()
    if len(lines) != EXPECTED_FRAME_COUNT:
        raise FrameError("capture frame count mismatch")
    records: list[dict[str, Any]] = []
    for line in lines:
        if len(line) > MAX_FRAME_BYTES or not line.startswith(PREFIX):
            raise FrameError("frame prefix or size mismatch")
        payload = line[len(PREFIX):]
        try:
            text = payload.decode("ascii")
        except UnicodeDecodeError as exc:
            raise FrameError("frame encoding mismatch") from exc
        if PRIVATE_TEXT.search(text):
            raise FrameError("private text rejected")
        try:
            record = json.loads(text, object_pairs_hook=_object_pairs)
        except (json.JSONDecodeError, UnicodeError) as exc:
            raise FrameError("frame JSON mismatch") from exc
        if type(record) is not dict or _canonical_payload(record) != payload:
            raise FrameError("frame JSON is not canonical compact JSON")
        records.append(record)
    return records


def _header(record: dict[str, Any]) -> None:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "operations_required", "phase2_complete", "repetitions_cold",
        "repetitions_warm", "cold_conditioning", "radio_used", "candidate_selected",
    }, "header")
    _common(record, "header")
    if (
        type(record["operations_required"]) is not int
        or record["operations_required"] != len(OPERATIONS)
        or type(record["repetitions_cold"]) is not int
        or record["repetitions_cold"] != REPETITIONS
        or type(record["repetitions_warm"]) is not int
        or record["repetitions_warm"] != REPETITIONS
        or record["cold_conditioning"] != "32k_data_sweep"
        or record["radio_used"] is not False
        or record["candidate_selected"] is not False
    ):
        raise FrameError("header local primitive scope mismatch")


def _gate(record: dict[str, Any], gate: str) -> None:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "gate", "outcome", "phase2_complete",
    }, "gate")
    _common(record, "gate")
    if record["gate"] != gate or record["outcome"] != "pass":
        raise FrameError("gate order or outcome mismatch")


def _sample(
    record: dict[str, Any], operation: str, phase: str, iteration: int
) -> int:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "operation", "phase", "iteration", "duration_us", "outcome",
        "phase2_complete",
    }, "sample")
    _common(record, "sample")
    if (
        record["operation"] != operation
        or record["phase"] != phase
        or type(record["iteration"]) is not int
        or record["iteration"] != iteration
        or record["outcome"] != "pass"
    ):
        raise FrameError("sample order or outcome mismatch")
    return _integer(record["duration_us"], "sample duration", 0, MAX_DURATION_US)


def _statistics(samples: list[int]) -> dict[str, int]:
    ordered = sorted(samples)
    return {
        "min_us": ordered[0],
        "median_us": (ordered[REPETITIONS // 2 - 1] + ordered[REPETITIONS // 2]) // 2,
        "p95_us": ordered[(95 * REPETITIONS + 99) // 100 - 1],
        "max_us": ordered[-1],
    }


def _summary(
    record: dict[str, Any], operation: str, phase: str, samples: list[int]
) -> dict[str, int]:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "operation", "phase", "min_us", "median_us", "p95_us", "max_us",
        "outcome", "phase2_complete",
    }, "operation summary")
    _common(record, "operation_summary")
    if (
        record["operation"] != operation
        or record["phase"] != phase
        or record["outcome"] != "pass"
    ):
        raise FrameError("operation summary order or outcome mismatch")
    observed = {
        name: _integer(record[name], f"operation summary {name}", 0, MAX_DURATION_US)
        for name in ("min_us", "median_us", "p95_us", "max_us")
    }
    expected = _statistics(samples)
    if observed != expected:
        raise FrameError("operation summary statistics mismatch")
    return expected


def _runtime_resources(record: dict[str, Any]) -> dict[str, int]:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "heap_domain", "heap_start_free_bytes", "heap_min_free_bytes",
        "peak_dynamic_ram_bytes", "stack_allocation_bytes",
        "stack_high_water_free_bytes", "max_stack_used_bytes",
        "watchdog_resets", "watchdog_measurement", "phase2_complete",
    }, "runtime resources")
    _common(record, "runtime_resources")
    values = {
        name: _integer(record[name], f"runtime resources {name}", 0, 2**63 - 1)
        for name in (
            "heap_start_free_bytes", "heap_min_free_bytes",
            "peak_dynamic_ram_bytes", "stack_allocation_bytes",
            "stack_high_water_free_bytes", "max_stack_used_bytes",
            "watchdog_resets",
        )
    }
    if (
        record["heap_domain"] != "internal_8bit"
        or record["watchdog_measurement"] != "uninterrupted_terminal_frame"
        or values["heap_min_free_bytes"] > values["heap_start_free_bytes"]
        or values["peak_dynamic_ram_bytes"]
        != values["heap_start_free_bytes"] - values["heap_min_free_bytes"]
        or values["stack_allocation_bytes"] != 8192
        or values["stack_high_water_free_bytes"] > values["stack_allocation_bytes"]
        or values["max_stack_used_bytes"]
        != values["stack_allocation_bytes"] - values["stack_high_water_free_bytes"]
        or values["watchdog_resets"] != 0
    ):
        raise FrameError("runtime resource measurement mismatch")
    return values

def _complete(record: dict[str, Any]) -> None:
    _exact(record, {
        "schema", "version", "record_kind", "scope", "candidate_id",
        "operations_completed", "operations_required", "outcome",
        "phase2_complete", "radio_used", "candidate_selected",
    }, "local completion")
    _common(record, "local_complete")
    if (
        type(record["operations_required"]) is not int
        or record["operations_required"] != len(OPERATIONS)
        or type(record["operations_completed"]) is not int
        or record["operations_completed"] != len(OPERATIONS)
        or record["outcome"] != "pass"
        or record["radio_used"] is not False
        or record["candidate_selected"] is not False
    ):
        raise FrameError("terminal local completion mismatch")


def parse_capture_bytes(raw: bytes) -> dict[str, Any]:
    """Validate one complete local-primitive capture and return safe derived facts."""
    records = _decode(raw)
    cursor = 0
    _header(records[cursor])
    cursor += 1
    for gate in GATES:
        _gate(records[cursor], gate)
        cursor += 1
    summaries: dict[str, dict[str, dict[str, int]]] = {}
    for operation in OPERATIONS:
        summaries[operation] = {}
        for phase in PHASES:
            samples: list[int] = []
            for iteration in range(REPETITIONS):
                samples.append(_sample(records[cursor], operation, phase, iteration))
                cursor += 1
            summaries[operation][phase] = _summary(
                records[cursor], operation, phase, samples
            )
            cursor += 1
    resources = _runtime_resources(records[cursor])
    cursor += 1
    if cursor != len(records) - 1:
        raise FrameError("terminal frame position mismatch")
    _complete(records[cursor])
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "candidate_id": CANDIDATE_ID,
        "scope": SCOPE,
        "operations_required": len(OPERATIONS),
        "operations_completed": len(OPERATIONS),
        "cold_sample_count": len(OPERATIONS) * REPETITIONS,
        "warm_sample_count": len(OPERATIONS) * REPETITIONS,
        "summary_count": len(OPERATIONS) * len(PHASES),
        "gate_count": len(GATES),
        "runtime_resources": resources,
        "phase2_complete": False,
        "radio_used": False,
        "candidate_selected": False,
        "summaries": summaries,
    }


def parse_capture(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise FrameError("capture could not be read") from exc
    return parse_capture_bytes(raw)


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    try:
        args = parser.parse_args(argv)
        result = parse_capture(args.capture)
        print(json.dumps(result, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
        return 0
    except FrameError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    except (KeyError, AttributeError, TypeError, IndexError, OverflowError):
        print("ERROR: malformed frame structure", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
