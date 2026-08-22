#!/usr/bin/env python3
"""Validate append-only OT-117 libsodium API/configuration admission."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

import crypto_api_config_acceptance_contract as acceptance
import crypto_libsodium_api_config_evidence as evidence_validator


SCHEMA, VERSION = "OTLAPIA0", 0
ADMISSION_ID = "OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0"
EXPECTED_ADMISSION_SHA256 = "6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527"
EXPECTED_ADMISSION_RAW_SHA256 = "527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2"
EXPECTED_BUNDLE_RAW_SHA256 = "b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58"
EXPECTED_API_EVIDENCE_RAW_SHA256 = "34888d71da2c9042856ea48c7b1225f21c1345582c144239cab0096ff03e69b5"
ADMISSION_PUBLIC_RESULT = (
    "LIBSODIUM-EIGHT-OF-EIGHT-API-CONFIG-PRIMARY-COMPLETE-ADMITTED-HOST-ONLY; "
    "COUNTS-3-2-0; PHASE-0-INCOMPLETE; MEASUREMENT-READINESS-BLOCKED; "
    "NO-EXECUTION-SELECTION-OR-SCORE"
)
MAX_BYTES = 131072
MBEDTLS_EVIDENCE_SHA256 = "22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8"
PARENT_CONSTANTS = {
    "otcapia0_v0_raw_sha256": "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0",
    "otcapia0_v0_canonical_sha256": "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd",
    "otcbr1_v0_raw_sha256": "333f8d525160f45627a13913e5d1adabe8e5c8374290af32b9af1df96ef1bd7e",
    "otcbr1_v0_canonical_sha256": "ad10935a52bbbcb1ed06f523ef5084a8d70b91aca355d7b497e9ad54c18f453e",
    "otcbx1_v1_raw_sha256": "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
    "otcbx1_v1_canonical_sha256": "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8",
}
AUTHORITY_FIELDS = (
    "candidate_import_authorized", "benchmark_build_authorized", "benchmark_execution_authorized",
    "device_access_authorized", "flash_authorized", "radio_transmit_authorized",
    "key_or_entropy_operation_authorized", "candidate_selection_authorized",
    "suite_selection_authorized", "packet_v1_authorized", "score_credit_added",
)
CLAIM_FIELDS = (
    "complete_api_config_eligibility_proven", "api_config_evidence_accepted",
    "candidate_imported", "candidate_benchmark_built", "candidate_benchmark_executed",
    "candidate_selected", "suite_selected", "hardware_or_device_accessed",
    "physical_evidence_added", "score_credit_added",
)


class ValidationError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, "ERROR: invalid arguments\n")


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in items:
        if key in out:
            raise ValidationError("duplicate key")
        out[key] = value
    return out


def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if len(raw) > MAX_BYTES:
            raise ValidationError("JSON exceeds size limit")
        if expected_raw_sha256 and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
            raise ValidationError("raw artifact digest mismatch")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
        if type(value) is not dict:
            raise ValidationError("document must be object")
        return value
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ValidationError("JSON unreadable or invalid") from exc


def canonical_sha256(value: Any) -> str:
    try:
        raw = json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode()
    except (TypeError, ValueError, RecursionError, OverflowError) as exc:
        raise ValidationError("canonical serialization failed") from exc
    return hashlib.sha256(raw).hexdigest()


def validate(admission: dict[str, Any], bundle: dict[str, Any], evidence: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    evidence_validator._scan(admission)
    contract_result = acceptance.validate_contract(contract)
    evidence_result = evidence_validator.validate_api_evidence(evidence, bundle, contract)
    exact_fields = {"schema", "version", "artifact_kind", "admission_id", "accepted_date", "status", "public_result", "parents", "accepted_api_config_evidence_sha256", "acceptance_counts", "admitted_candidate", "phase_zero", "measurement_blockers", "authority", "claims"}
    if type(admission) is not dict or set(admission) != exact_fields:
        raise ValidationError("admission fields mismatch")
    identity = (admission["schema"], admission["version"], admission["artifact_kind"], admission["admission_id"], admission["accepted_date"], admission["status"], admission["public_result"])
    if identity != (SCHEMA, VERSION, "append_only_libsodium_api_config_acceptance_delta", ADMISSION_ID, "2026-08-22", "libsodium_complete_api_config_admitted_measurement_blocked", ADMISSION_PUBLIC_RESULT):
        raise ValidationError("admission identity mismatch")
    expected_parents = {
        **PARENT_CONSTANTS,
        "otcac0_v1_raw_sha256": evidence_validator.PARENTS["otcac0_v1_raw_sha256"],
        "otcac0_v1_canonical_sha256": contract_result["contract_sha256"],
        "otcapioe0_v0_raw_sha256": EXPECTED_BUNDLE_RAW_SHA256,
        "otcapioe0_v0_canonical_sha256": evidence_result["operation_bundle_sha256"],
        "otcapi0_v2_raw_sha256": EXPECTED_API_EVIDENCE_RAW_SHA256,
        "otcapi0_v2_canonical_sha256": evidence_result["api_config_evidence_sha256"],
        "otnxk0_v0_header_raw_sha256": evidence_validator.EXPECTED_ADAPTER_HEADER_RAW_SHA256,
        "otnxk0_v0_source_raw_sha256": evidence_validator.EXPECTED_ADAPTER_SOURCE_RAW_SHA256,
    }
    if admission["parents"] != expected_parents:
        raise ValidationError("admission parent mismatch")
    expected_registry = {
        "espressif_libsodium": [evidence_result["api_config_evidence_sha256"]],
        "esp_idf_mbedtls_psa": [MBEDTLS_EVIDENCE_SHA256],
        "monocypher": [],
    }
    if admission["accepted_api_config_evidence_sha256"] != expected_registry:
        raise ValidationError("accepted registry mismatch")
    if admission["acceptance_counts"] != {"source": 3, "api_config": 2, "candidate_import": 0}:
        raise ValidationError("acceptance counts mismatch")
    if admission["admitted_candidate"] != {"candidate_id": "espressif_libsodium", "role": "primary", "coverage_state": "complete_selectable", "eligible_operation_count": 8, "api_config_evidence_sha256": evidence_result["api_config_evidence_sha256"], "selection_eligible": True, "selection_authorized": False}:
        raise ValidationError("admitted candidate mismatch")
    if admission["phase_zero"] != {"name": "api_configuration_admission", "complete": False, "remaining": ["independent_second_node_exact_profile_admission", "independent_monocypher_api_configuration_admission", "preserve_mbedtls_psa_five_of_eight_partial_nonselectable_state"]}:
        raise ValidationError("phase-zero state mismatch")
    if admission["measurement_blockers"] != ["phase_zero_incomplete", "candidate_import_and_build_admissions_absent", "fresh_benchmark_execution_authority_absent"]:
        raise ValidationError("measurement blocker state mismatch")
    for name, fields in (("authority", AUTHORITY_FIELDS), ("claims", CLAIM_FIELDS)):
        values = admission[name]
        if type(values) is not dict or set(values) != set(fields):
            raise ValidationError(f"{name} fields mismatch")
        for field, value in values.items():
            expected = name == "claims" and field in {"complete_api_config_eligibility_proven", "api_config_evidence_accepted"}
            if type(value) is not bool or value is not expected:
                raise ValidationError(f"{name} disposition mismatch")
    digest = canonical_sha256(admission)
    if EXPECTED_ADMISSION_SHA256 and digest != EXPECTED_ADMISSION_SHA256:
        raise ValidationError("admission digest mismatch")
    return {"schema": SCHEMA, "version": VERSION, "admission_id": ADMISSION_ID, "accepted_api_config_count": 2, "source_count": 3, "candidate_import_count": 0, "phase_zero_complete": False, "measurement_ready": False, "selection_eligible": True, "execution_authorized": False, "selection_authorized": False, "score_credit_added": False, "admission_sha256": digest}


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser()
    parser.add_argument("--admission", required=True, type=Path)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--api-evidence", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate(
            load(args.admission, EXPECTED_ADMISSION_RAW_SHA256),
            evidence_validator.load(args.bundle, EXPECTED_BUNDLE_RAW_SHA256),
            evidence_validator.load(args.api_evidence, EXPECTED_API_EVIDENCE_RAW_SHA256),
            acceptance.load(args.contract),
        )
    except (ValidationError, evidence_validator.ValidationError, acceptance.ValidationError):
        print("ERROR: validation failed", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
