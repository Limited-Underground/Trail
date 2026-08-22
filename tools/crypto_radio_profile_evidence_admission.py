#!/usr/bin/env python3
"""Strict validator for OT-114 OTRPE1 evidence and OTRPA1 admission."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
CONTRACT = CRYPTO / "OT-113-OT005-US915-DIRECT-RADIO-PROFILE-EXECUTION-CONTRACT-V1.json"
RECEIPT = ROOT / "tests" / "hardware" / "OT-114-2026-08-21-EXECUTION-RECEIPT-V0.json"
EVIDENCE = CRYPTO / "OT-114-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-V1.json"
ADMISSION = CRYPTO / "OT-114-OT005-US915-DIRECT-RADIO-PROFILE-ADMISSION-DELTA-V1.json"
PINS = {
    "contract": ("c59fd52f8c1608f7e7dfdb5c166504bb7ad7fb02c6e82b3d3d0677c79cd2c87c", "d73ebf7340c4351b5daa775d7cb9342f6650baf376f1c45944049d7efe49462c"),
    "receipt": ("d285d43ec30b2d81473b37bf189b14d89db389cb1636cacbe59cf9f84825d1dd", "1700446be2216f6520859928e941a72a06605dfdeedad6957b6e4f8d5259e8c4"),
    "evidence": ("b6d2a7ce4ebe3ab233ebbc748ab7831ff12cf4d8f6504d2d7e23dae108bd5876", "ac7e77a4438772a4c5b5f2b17472b302a3520e186a21b88125a9314ee6998bf0"),
    "admission": ("19325f730b96b9dbeeb4f64682c4913e7586d1995ef419b26408d82be12ef266", "eecf2b821ef2c25274cc5d3a179494b1545eb4a859280a48459ebb83c79ed257"),
}
_PRIVATE = re.compile(r"(?:[A-Za-z]:\\|/Users/|/home/|COM\d+|tty(?:USB|ACM)\d+|(?:[0-9A-F]{2}:){5}[0-9A-F]{2})", re.I)


class ValidationError(ValueError):
    pass


class SafeParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ValidationError("invalid arguments")


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate key")
        result[key] = value
    return result


def canonical_sha256(value: Any) -> str:
    raw = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    return hashlib.sha256(raw).hexdigest()


def load_pinned(path: Path, pin: tuple[str, str]) -> dict[str, Any]:
    raw = path.read_bytes()
    if len(raw) > 131072 or hashlib.sha256(raw).hexdigest() != pin[0]:
        raise ValidationError("raw mismatch")
    if _PRIVATE.search(raw.decode("utf-8")):
        raise ValidationError("unsafe text")
    try:
        value = json.loads(raw, object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError("invalid JSON") from exc
    if not isinstance(value, dict) or canonical_sha256(value) != pin[1]:
        raise ValidationError("canonical mismatch")
    return value


def _need(condition: bool) -> None:
    if not condition:
        raise ValidationError("invariant mismatch")


def _fill(role: str, direction: str, session: int, sequence: int, index: int) -> int:
    rv, dv = (1 if role == "A" else 2), (1 if direction == "A>B" else 2)
    value = (session ^ ((sequence * 0x9E3779B9) & 0xFFFFFFFF) ^ (rv << 24) ^ (dv << 16) ^ index) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    return (value ^ (value >> 8)) & 0xFF


def _data(role: str, direction: str, session: int, sequence: int, wire: int) -> bytes:
    rv, dv = (1 if role == "A" else 2), (1 if direction == "A>B" else 2)
    header = b"OTD1" + bytes((1, rv, dv, 16)) + session.to_bytes(4, "little") + sequence.to_bytes(4, "little")
    return header + bytes(_fill(role, direction, session, sequence, i) for i in range(wire - 16))


def _ack(role: str, direction: str, session: int, sequence: int) -> bytes:
    rv, dv = (1 if role == "A" else 2), (1 if direction == "A>B" else 2)
    return b"OTA1" + bytes((1, rv, dv, 16)) + session.to_bytes(4, "little") + sequence.to_bytes(4, "little")


def validate_receipt(receipt: dict[str, Any]) -> None:
    _need(receipt["schema"] == "OTRER0" and receipt["version"] == 0)
    _need(receipt["claims"] == {
        "benchmark_readiness_advanced": False, "blocker_closed": False,
        "execution_receipt_generated": True, "physical_evidence_admitted": False,
        "production_support_proven": False, "range_proven": False,
        "regulatory_compliance_proven": False,
    })
    summary = receipt["summary"]
    for key, expected in {
        "radio_frames_attempted": 242, "radio_frames_received": 242,
        "ack_frames_attempted": 240, "ack_frames_received": 240,
        "radio_transmissions_including_acks": 482, "lost": 0, "duplicates": 0,
        "corrupt": 0, "unexpected": 0, "direct_payload_ceiling_bytes": 255,
        "session_start_receipts": 4, "session_end_receipts": 2,
    }.items():
        _need(summary[key] == expected)
    _need(summary["receipt_chain_sha256"] == "3046024e082f7e6a5a78deec4bbf3d84dd9f6ed0eefaec8bb7fb8c0cc80506ab")
    _need(all(value is False for value in receipt["privacy"].values()))
    _need([step["status"] for step in receipt["steps"]] == ["passed"] * 8)
    _need([step["step"] for step in receipt["steps"]] == list(range(1, 9)))
    frames = receipt["frames"]
    _need(len(frames) == 242)
    _need(collections.Counter((f["phase"], f["direction"]) for f in frames) == collections.Counter({
        ("one_byte_probe", "A>B"): 1, ("one_byte_probe", "B>A"): 1,
        ("benchmark_mtu", "A>B"): 100, ("benchmark_mtu", "B>A"): 100,
        ("direct_ceiling", "A>B"): 10, ("direct_ceiling", "B>A"): 10,
        ("post_restart_mtu", "A>B"): 10, ("post_restart_mtu", "B>A"): 10,
    }))
    _need(len({(f["session"], f["direction"], f["sequence"]) for f in frames}) == 242)
    _need(len({f["session"] for f in frames}) == 1 and frames[0]["session"] != 0)
    for frame in frames:
        role = "A" if frame["direction"] == "A>B" else "B"
        if frame["wire_bytes"] == 1:
            data = b"\xA5"
            _need(frame["ack_wire_sha256"] is None and frame["rtt_ms"] is None)
        else:
            data = _data(role, frame["direction"], frame["session"], frame["sequence"], frame["wire_bytes"])
            ack_role = "B" if role == "A" else "A"
            ack_direction = "B>A" if frame["direction"] == "A>B" else "A>B"
            ack = _ack(ack_role, ack_direction, frame["session"], frame["sequence"])
            _need(hashlib.sha256(ack).hexdigest() == frame["ack_wire_sha256"])
            _need(frame["ack_timeout_ms"] == (2318 if frame["wire_bytes"] == 163 else 2452))
            _need(0 <= frame["rtt_ms"] <= receipt["timeout_policy"]["receipt_timeout_ms"])
        _need(hashlib.sha256(data).hexdigest() == frame["data_wire_sha256"])


def validate_evidence(evidence: dict[str, Any]) -> None:
    _need(evidence["schema"] == "OTRPE1" and evidence["version"] == 1)
    _need(evidence["parents"]["contract"]["raw_sha256"] == PINS["contract"][0])
    _need(evidence["parents"]["contract"]["canonical_sha256"] == PINS["contract"][1])
    _need(evidence["parents"]["execution_receipt"]["raw_sha256"] == PINS["receipt"][0])
    _need(evidence["parents"]["execution_receipt"]["canonical_sha256"] == PINS["receipt"][1])
    result = evidence["execution_result"]
    _need(result["radio_frames_attempted"] == result["radio_frames_received"] == 242)
    _need(result["ack_frames_attempted"] == result["ack_frames_received"] == 240)
    _need(all(result[key] == 0 for key in ("lost", "duplicates", "corrupt", "unexpected")))
    _need(evidence["session_and_receipts"]["device_monotonic_timestamp_field"] == "mono_us")
    _need(evidence["metrics"]["receipt_timeout_ms_by_data_wire_bytes"] == {"163": 2318, "255": 2452})
    _need(evidence["artifact_bindings"]["runner_source_sha256"] == "57c2685149ddb5d8e569d0067f71201c33163b7deb52f1a9d24944132ca20947")
    _need(evidence["artifact_bindings"]["per_node_exact_image_context_receipts"] == 2)
    _need(evidence["acceptance"] == {
        "contract_satisfied": True, "eligible_for_independent_admission": True,
        "evidence_self_closes_requirement": False, "readiness_advanced": False,
    })


def validate_admission(admission: dict[str, Any]) -> None:
    _need(admission["schema"] == "OTRPA1" and admission["version"] == 1)
    for name, pin_name in (("contract", "contract"), ("evidence", "evidence"), ("execution_receipt", "receipt")):
        _need(admission["bindings"][name]["raw_sha256"] == PINS[pin_name][0])
        _need(admission["bindings"][name]["canonical_sha256"] == PINS[pin_name][1])
    result = admission["admission"]
    _need(result["only_closed_requirement"] == "direct_radio_mtu_phy_region_unresolved")
    _need(result["remaining_requirement_count"] == 0 and result["readiness_advanced"] is False)
    _need(result["successor_readiness_decision_required"] is True)
    _need(result["new_executable_benchmark_plan_required"] is True)
    _need(admission["claims"]["physical_evidence_admitted"] is True)
    _need(admission["claims"]["direct_radio_requirement_closed"] is True)
    _need(admission["claims"]["readiness_advanced"] is False)


def main(argv: list[str] | None = None) -> int:
    parser = SafeParser()
    parser.add_argument("--receipt", type=Path, default=RECEIPT)
    parser.add_argument("--evidence", type=Path, default=EVIDENCE)
    parser.add_argument("--admission", type=Path, default=ADMISSION)
    try:
        args = parser.parse_args(argv)
        load_pinned(CONTRACT, PINS["contract"])
        receipt = load_pinned(args.receipt, PINS["receipt"])
        evidence = load_pinned(args.evidence, PINS["evidence"])
        admission = load_pinned(args.admission, PINS["admission"])
        validate_receipt(receipt); validate_evidence(evidence); validate_admission(admission)
    except (ValidationError, OSError, KeyError, TypeError, ValueError):
        print("ERROR: validation failed", file=sys.stderr)
        return 2
    print(json.dumps({"schema": "OTRPA1", "status": admission["status"], "closed_requirement": admission["admission"]["only_closed_requirement"], "readiness_advanced": False, "contract_raw_sha256": PINS["contract"][0], "evidence_raw_sha256": PINS["evidence"][0], "admission_raw_sha256": PINS["admission"][0]}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
