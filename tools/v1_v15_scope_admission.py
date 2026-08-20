#!/usr/bin/env python3
"""Validate the public, non-executing OpenTrail V1/V1.5 scope plan."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTVS0"
VERSION = 0
ARTIFACT_KIND = "v1_v15_acceptance_scope_plan"
PLAN_ID = "OT-089-V1-V15-ACCEPTANCE-SCOPE-V0"
DECISION_ID = "0033"
PLAN_STATUS = "owner_approved_scope_adopted"
PLAN_REPORT = "OWNER-APPROVED-SCOPE-ADOPTED"
IMPLEMENTATION_STATUS = "IMPLEMENTATION-AND-PHYSICAL-ACCEPTANCE-OPEN"
RELEASE_STATUS = "NOT-EVALUATED"
PUBLIC_SUMMARY = (
    "Owner-approved V1 scope and security boundary adopted; "
    "implementation and physical acceptance remain open."
)
CANONICAL_PLAN_SHA256 = (
    "14b85459dcab366e40191613c9656927d7e2a87d82898ea10777210b569557f8"
)
MAX_PLAN_BYTES = 64 * 1024
MAX_JSON_DEPTH = 64

TOP_LEVEL_KEYS = {
    "schema",
    "version",
    "artifact_kind",
    "plan_id",
    "decision_id",
    "plan_status",
    "public_summary",
    "implementation_status",
    "v1",
    "v1_5",
    "supersession",
    "progress",
    "privacy",
    "execution_authority",
    "claims",
    "open_followup_gates",
}
FORBIDDEN_KEYS = {
    "access_token",
    "account_email",
    "coordinates",
    "device_serial",
    "key_password",
    "keystore_password",
    "local_path",
    "mac_address",
    "pairing_pin",
    "password",
    "private_key",
    "refresh_token",
    "store_password",
    "store_token",
    "transport_port",
}
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
    re.compile(
        r"\b(?:password|private[_ -]?key|access[_ -]?token)\s*[:=]",
        re.IGNORECASE,
    ),
)


class AdmissionError(ValueError):
    pass


def _reject_duplicate_object_pairs(
    pairs: list[tuple[str, Any]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AdmissionError("plan JSON contains duplicate fields")
        result[key] = value
    return result


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AdmissionError(f"{path} must be an object")
    return value


def _reject_excessive_nesting(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        item, depth = pending.pop()
        if depth > MAX_JSON_DEPTH:
            raise AdmissionError("plan JSON exceeds the nesting limit")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    if set(value) != expected:
        raise AdmissionError(f"{path} keys differ from the canonical shape")


def _scan_public(value: Any, path: str = "plan") -> None:
    if isinstance(value, dict):
        for key in value:
            if not isinstance(key, str):
                raise AdmissionError(f"{path} contains a noncanonical field name")
            if key in FORBIDDEN_KEYS or any(pattern.search(key) for pattern in PRIVATE_TEXT):
                raise AdmissionError(f"{path} contains a prohibited field name")
        for item in value.values():
            _scan_public(item, f"{path}.field")
    elif isinstance(value, list):
        for item in value:
            _scan_public(item, f"{path}.item")
    elif isinstance(value, str):
        for pattern in PRIVATE_TEXT:
            if pattern.search(value):
                raise AdmissionError(
                    f"{path} contains private machine, device, or credential text"
                )


def canonical_sha256(value: dict[str, Any]) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def validate_plan(plan: dict[str, Any]) -> dict[str, Any]:
    _scan_public(plan)
    _exact_keys(plan, TOP_LEVEL_KEYS, "plan")
    if plan["schema"] != SCHEMA or plan["version"] != VERSION:
        raise AdmissionError("plan schema/version mismatch")
    if plan["artifact_kind"] != ARTIFACT_KIND or plan["plan_id"] != PLAN_ID:
        raise AdmissionError("plan artifact_kind/plan_id mismatch")
    if plan["decision_id"] != DECISION_ID:
        raise AdmissionError("plan decision_id mismatch")
    if plan["plan_status"] != PLAN_STATUS:
        raise AdmissionError("plan status is not canonical")
    if plan["public_summary"] != PUBLIC_SUMMARY:
        raise AdmissionError("plan public summary is not canonical")
    try:
        lora = _object(plan["v1"], "plan.v1")["lora_security"]
        lora = _object(lora, "plan.v1.lora_security")
        if (
            lora["key_provisioning_and_replacement_workflow"]
            != "contract_frozen_not_implemented"
        ):
            raise AdmissionError("secure-LoRa workflow freeze status is not canonical")
        if plan["open_followup_gates"] != [
            "ble_pairing_implementation_and_physical_acceptance_open",
            "secure_lora_implementation_and_physical_acceptance_open",
            "implementation_and_physical_acceptance_open",
        ]:
            raise AdmissionError("plan open follow-up gates are not canonical")
    except (KeyError, TypeError, AttributeError) as exc:
        raise AdmissionError(
            "plan structure or field types differ from the canonical scope"
        ) from exc
    if canonical_sha256(plan) != CANONICAL_PLAN_SHA256:
        raise AdmissionError("plan differs from the canonical accepted scope")

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "v1_v15_acceptance_scope_admission",
        "plan_id": PLAN_ID,
        "plan_sha256": CANONICAL_PLAN_SHA256,
        "plan_status": PLAN_REPORT,
        "implementation_status": IMPLEMENTATION_STATUS,
        "v1_status": RELEASE_STATUS,
        "v1_5_status": RELEASE_STATUS,
        "execution_authority_granted": False,
        "score_credit_added": False,
        "open_followup_gates": plan["open_followup_gates"],
    }


def load_plan(path: Path) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
    except OSError as exc:
        raise AdmissionError("plan JSON could not be read") from exc
    if len(encoded) > MAX_PLAN_BYTES:
        raise AdmissionError("plan JSON exceeds the 65536-byte limit")
    try:
        text = encoded.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise AdmissionError("plan JSON is not valid UTF-8") from exc
    try:
        value = json.loads(text, object_pairs_hook=_reject_duplicate_object_pairs)
        _reject_excessive_nesting(value)
    except AdmissionError as exc:
        raise AdmissionError(
            "plan JSON is malformed or contains duplicate fields"
        ) from exc
    except (ValueError, RecursionError) as exc:
        raise AdmissionError(
            "plan JSON is malformed or contains duplicate fields"
        ) from exc
    return _object(value, "plan")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate-plan")
    validate.add_argument("--input", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(validate_plan(load_plan(args.input)), sort_keys=True))
        return 0
    except (AdmissionError, ValueError, RecursionError) as exc:
        error = (
            str(exc)
            if isinstance(exc, AdmissionError)
            else "plan JSON is malformed or exceeds the nesting limit"
        )
        report = {
            "schema": SCHEMA,
            "version": VERSION,
            "artifact_kind": "v1_v15_acceptance_scope_admission",
            "plan_status": "PLAN-INVALID",
            "implementation_status": IMPLEMENTATION_STATUS,
            "v1_status": RELEASE_STATUS,
            "v1_5_status": RELEASE_STATUS,
            "execution_authority_granted": False,
            "score_credit_added": False,
            "error": error,
        }
        print(json.dumps(report, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
