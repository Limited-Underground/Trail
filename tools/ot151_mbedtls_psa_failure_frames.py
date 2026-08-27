#!/usr/bin/env python3
"""Strict validator for an early terminal mbedTLS/PSA benchmark failure.

The OT-149 success parser remains the sole admission boundary for the complete
1,015-frame measurement.  This module recognizes only the two canonical
pre-measurement failure transcripts emitted by the frozen target so the host
can stop waiting without admitting a benchmark result.
"""

from __future__ import annotations

import importlib.util
import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
_FRAME_PATH = ROOT / "tools" / "ot149_mbedtls_psa_frames.py"
_SPEC = importlib.util.spec_from_file_location("ot149_mbedtls_psa_frames", _FRAME_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("frame_contract_unavailable")
frame_contract = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(frame_contract)

PREFIX = frame_contract.PREFIX
MAX_FRAME_BYTES = frame_contract.MAX_FRAME_BYTES
MAX_FAILURE_CAPTURE_BYTES = 8 * 1024
PRIVATE_TEXT = re.compile(
    r"(?i)(?:\\bCOM\\d+\\b|VID[:=_ -]|PID[:=_ -]|serial|device[_ -]?id|"
    r"private|secret|coordinate|latitude|longitude|[A-Za-z]:[\\\\/])"
)


class FailureTranscriptError(ValueError):
    """Closed failure for a malformed early-terminal transcript."""


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise FailureTranscriptError("duplicate field")
        value[key] = item
    return value


def _canonical(record: dict[str, Any]) -> bytes:
    return json.dumps(
        record, ensure_ascii=True, separators=(",", ":"), sort_keys=False
    ).encode("ascii")


def _decode_line(line: bytes) -> dict[str, Any]:
    if (
        not line.endswith(b"\n")
        or len(line) > MAX_FRAME_BYTES + 1
        or not line.startswith(PREFIX)
        or b"\r" in line
        or b"\x00" in line
    ):
        raise FailureTranscriptError("frame boundary mismatch")
    payload = line[len(PREFIX) : -1]
    try:
        text = payload.decode("ascii")
        record = json.loads(text, object_pairs_hook=_pairs)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise FailureTranscriptError("frame JSON mismatch") from exc
    if type(record) is not dict or _canonical(record) != payload:
        raise FailureTranscriptError("frame JSON is not canonical compact JSON")
    if PRIVATE_TEXT.search(text):
        raise FailureTranscriptError("private text rejected")
    return record


def _decode(raw: bytes) -> list[dict[str, Any]]:
    if (
        not raw
        or len(raw) > MAX_FAILURE_CAPTURE_BYTES
        or raw.startswith(b"\xef\xbb\xbf")
        or b"\r" in raw
        or b"\x00" in raw
        or not raw.endswith(b"\n")
    ):
        raise FailureTranscriptError("capture boundary mismatch")
    return [_decode_line(line + b"\n") for line in raw.splitlines()]


def _common(record: dict[str, Any], kind: str) -> None:
    if (
        record.get("schema") != "OTCBXRF2"
        or type(record.get("version")) is not int
        or record.get("version") != 2
        or record.get("record_kind") != kind
        or record.get("scope") != "candidate_local_v2"
        or record.get("candidate_id") != "esp_idf_mbedtls_psa"
        or record.get("phase2_complete") is not False
    ):
        raise FailureTranscriptError(f"{kind} identity mismatch")


def _exact(record: dict[str, Any], fields: set[str], kind: str) -> None:
    if set(record) != fields:
        raise FailureTranscriptError(f"{kind} field mismatch")
    _common(record, kind)


def _header(record: dict[str, Any]) -> None:
    _exact(
        record,
        {
            "schema", "version", "record_kind", "scope", "candidate_id",
            "operations_required", "repetitions_cold", "repetitions_warm",
            "cold_conditioning", "phase2_complete", "radio_used",
            "candidate_selected",
        },
        "header",
    )
    if (
        type(record["operations_required"]) is not int
        or record["operations_required"] != 5
        or type(record["repetitions_cold"]) is not int
        or record["repetitions_cold"] != 100
        or type(record["repetitions_warm"]) is not int
        or record["repetitions_warm"] != 100
        or record["cold_conditioning"] != "32k_data_sweep"
        or record["radio_used"] is not False
        or record["candidate_selected"] is not False
    ):
        raise FailureTranscriptError("header scope mismatch")


def _gate(record: dict[str, Any], name: str, outcome: str) -> None:
    _exact(
        record,
        {
            "schema", "version", "record_kind", "scope", "candidate_id",
            "gate", "outcome", "phase2_complete",
        },
        "gate",
    )
    if record["gate"] != name or record["outcome"] != outcome:
        raise FailureTranscriptError("gate order or outcome mismatch")


def _integer(value: object, maximum: int, field: str) -> int:
    if type(value) is not int or value < 0 or value > maximum:
        raise FailureTranscriptError(f"{field} mismatch")
    return value


def _resources(record: dict[str, Any]) -> None:
    _exact(
        record,
        {
            "schema", "version", "record_kind", "scope", "candidate_id",
            "heap_domain", "heap_start_free_bytes", "heap_min_free_bytes",
            "peak_dynamic_ram_bytes", "stack_allocation_bytes",
            "stack_high_water_free_bytes", "max_stack_used_bytes",
            "watchdog_resets", "watchdog_measurement", "phase2_complete",
        },
        "runtime_resources",
    )
    values = {
        name: _integer(record[name], 2**63 - 1, name)
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
        or values["stack_high_water_free_bytes"] > 8192
        or values["max_stack_used_bytes"]
        != 8192 - values["stack_high_water_free_bytes"]
        or values["watchdog_resets"] != 0
    ):
        raise FailureTranscriptError("runtime resource mismatch")


def _complete(record: dict[str, Any]) -> None:
    _exact(
        record,
        {
            "schema", "version", "record_kind", "scope", "candidate_id",
            "operations_completed", "operations_required", "outcome",
            "phase2_complete", "radio_used", "candidate_selected",
        },
        "local_complete",
    )
    if (
        type(record["operations_completed"]) is not int
        or record["operations_completed"] != 0
        or type(record["operations_required"]) is not int
        or record["operations_required"] != 5
        or record["outcome"] != "fail"
        or record["radio_used"] is not False
        or record["candidate_selected"] is not False
    ):
        raise FailureTranscriptError("terminal failure mismatch")


def looks_like_local_complete(line: bytes) -> bool:
    """Return true for a decodable local-complete record, including malformed ones."""
    if not line.startswith(PREFIX) or not line.endswith(b"\n"):
        return False
    try:
        payload = line[len(PREFIX) : -1].decode("ascii")
        value = json.loads(payload)
    except (UnicodeError, json.JSONDecodeError):
        return False
    return type(value) is dict and value.get("record_kind") == "local_complete"


def parse_early_failure_bytes(raw: bytes) -> dict[str, object]:
    """Validate one exact pre-measurement failure transcript and return safe facts."""
    records = _decode(raw)
    if len(records) not in {3, 5}:
        raise FailureTranscriptError("failure frame count mismatch")
    _header(records[0])
    if len(records) == 3:
        _gate(records[1], "psa_crypto_init", "fail")
        failed_gate = "psa_crypto_init"
    else:
        _gate(records[1], "psa_crypto_init", "pass")
        _gate(records[2], "primitive_vectors_and_negative_cases", "fail")
        _resources(records[3])
        failed_gate = "primitive_vectors_and_negative_cases"
    _complete(records[-1])
    return {
        "schema": "OTCBXRF2",
        "version": 2,
        "candidate_id": "esp_idf_mbedtls_psa",
        "failed_gate": failed_gate,
        "operations_completed": 0,
        "frame_count": len(records),
        "benchmark_passed": False,
        "phase2_complete": False,
        "radio_used": False,
        "candidate_selected": False,
    }
