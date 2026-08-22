#!/usr/bin/env python3
"""Validate append-only OT-118 Monocypher partial API/config admission."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

import crypto_api_config_acceptance_contract as acceptance
import crypto_monocypher_api_config_evidence as evidence_validator

SCHEMA, VERSION = "OTMAPIA0", 0
ADMISSION_ID = "OT-118-OT005-MONOCYPHER-API-CONFIG-ADMISSION-DELTA-V0"
EXPECTED_ADMISSION_RAW_SHA256 = "9fbecf19b206b31fae948b6bc7e7aa4e206ba26aa59b94fb7f07d4e1d300810a"
EXPECTED_ADMISSION_SHA256 = "df412285515fe29525b0bfd7cba45fd7ccd9a3d601be284242886e8adb19fec9"
MAX_BYTES = 131072


class ValidationError(ValueError): pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None: self.exit(2, "ERROR: invalid arguments\n")


def _pairs(items):
    out = {}
    for key, value in items:
        if key in out: raise ValidationError("duplicate key")
        out[key] = value
    return out


def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if len(raw) > MAX_BYTES: raise ValidationError("JSON exceeds size limit")
        if expected_raw_sha256 and hashlib.sha256(raw).hexdigest() != expected_raw_sha256: raise ValidationError("raw artifact digest mismatch")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
        if type(value) is not dict: raise ValidationError("document must be object")
        return value
    except ValidationError: raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as exc: raise ValidationError("JSON unreadable or invalid") from exc


def canonical_sha256(value: Any) -> str: return hashlib.sha256(json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def validate(admission: dict[str, Any], bundle: dict[str, Any], evidence: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    evidence_validator._scan(admission)
    contract_result = acceptance.validate_contract(contract)
    evidence_result = evidence_validator.validate_api_evidence(evidence, bundle, contract)
    exact = {"schema","version","artifact_kind","admission_id","accepted_date","status","public_result","parents","accepted_api_config_evidence_sha256","acceptance_counts","admitted_candidate","phase_zero","measurement_blockers","authority","claims"}
    if set(admission) != exact or (admission["schema"],admission["version"],admission["artifact_kind"],admission["admission_id"],admission["accepted_date"],admission["status"]) != (SCHEMA,VERSION,"append_only_monocypher_api_config_acceptance_delta",ADMISSION_ID,"2026-08-22","monocypher_partial_api_config_admitted_phase_zero_blocked"): raise ValidationError("admission identity mismatch")
    parents = {
        "otlapia0_v0_raw_sha256":"527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2","otlapia0_v0_canonical_sha256":"6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527",
        "otcac0_v1_raw_sha256":"575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3","otcac0_v1_canonical_sha256":contract_result["contract_sha256"],
        "otcapioe0_v0_raw_sha256":evidence_validator.EXPECTED_BUNDLE_RAW_SHA256,"otcapioe0_v0_canonical_sha256":evidence_result["operation_bundle_sha256"],
        "otcapi0_v2_raw_sha256":evidence_validator.EXPECTED_API_EVIDENCE_RAW_SHA256,"otcapi0_v2_canonical_sha256":evidence_result["api_config_evidence_sha256"],
        "otmapi0_v0_header_raw_sha256":evidence_validator.EXPECTED_ADAPTER_HEADER_RAW_SHA256,"otmapi0_v0_source_raw_sha256":evidence_validator.EXPECTED_ADAPTER_SOURCE_RAW_SHA256,
    }
    if admission["parents"] != parents: raise ValidationError("admission parent mismatch")
    registry = {"espressif_libsodium":["6e5e969c3b3f7bf29372e15e3cc75c693fb00f57f7f297c14cc77123dec4610d"],"esp_idf_mbedtls_psa":["22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8"],"monocypher":[evidence_result["api_config_evidence_sha256"]]}
    if admission["accepted_api_config_evidence_sha256"] != registry: raise ValidationError("accepted registry mismatch")
    if admission["acceptance_counts"] != {"source":3,"api_config":3,"candidate_import":0}: raise ValidationError("acceptance counts mismatch")
    expected_candidate = {"candidate_id":"monocypher","role":"comparison","coverage_state":"comparison_partial","eligible_operation_count":5,"api_config_evidence_sha256":evidence_result["api_config_evidence_sha256"],"selection_eligible":False,"selection_authorized":False}
    if admission["admitted_candidate"] != expected_candidate: raise ValidationError("admitted candidate mismatch")
    if admission["phase_zero"] != {"name":"api_configuration_admission","complete":False,"remaining":["independent_second_node_exact_profile_admission"],"completed":["espressif_libsodium_complete_api_config_admission","esp_idf_mbedtls_psa_partial_api_config_admission","monocypher_partial_api_config_admission"]}: raise ValidationError("phase-zero state mismatch")
    if admission["measurement_blockers"] != ["phase_zero_incomplete","candidate_import_and_build_admissions_absent","fresh_benchmark_execution_authority_absent"]: raise ValidationError("measurement blocker state mismatch")
    if set(admission["authority"]) != {"candidate_import_authorized","benchmark_build_authorized","benchmark_execution_authorized","device_access_authorized","flash_authorized","radio_transmit_authorized","key_or_entropy_operation_authorized","candidate_selection_authorized","suite_selection_authorized","packet_v1_authorized","score_credit_added"} or any(admission["authority"].values()): raise ValidationError("authority disposition mismatch")
    expected_claims = {"complete_api_config_eligibility_proven":False,"api_config_evidence_accepted":True,"candidate_imported":False,"candidate_benchmark_built":False,"candidate_benchmark_executed":False,"candidate_selected":False,"suite_selected":False,"hardware_or_device_accessed":False,"physical_evidence_added":False,"score_credit_added":False}
    if admission["claims"] != expected_claims: raise ValidationError("claims disposition mismatch")
    digest = canonical_sha256(admission)
    if digest != EXPECTED_ADMISSION_SHA256: raise ValidationError("admission digest mismatch")
    return {"schema":SCHEMA,"version":VERSION,"admission_id":ADMISSION_ID,"accepted_api_config_count":3,"source_count":3,"candidate_import_count":0,"phase_zero_complete":False,"measurement_ready":False,"selection_eligible":False,"execution_authorized":False,"selection_authorized":False,"score_credit_added":False,"admission_sha256":digest}


def main(argv=None) -> int:
    parser=SafeArgumentParser(); parser.add_argument("--admission",required=True,type=Path); parser.add_argument("--bundle",required=True,type=Path); parser.add_argument("--api-evidence",required=True,type=Path); parser.add_argument("--contract",required=True,type=Path); args=parser.parse_args(argv)
    try: result=validate(load(args.admission,EXPECTED_ADMISSION_RAW_SHA256),evidence_validator.load(args.bundle,evidence_validator.EXPECTED_BUNDLE_RAW_SHA256),evidence_validator.load(args.api_evidence,evidence_validator.EXPECTED_API_EVIDENCE_RAW_SHA256),acceptance.load(args.contract))
    except (ValidationError,evidence_validator.ValidationError,acceptance.ValidationError): print("ERROR: validation failed",file=sys.stderr); return 1
    print(json.dumps(result,sort_keys=True,separators=(",",":"))); return 0


if __name__ == "__main__": raise SystemExit(main())
