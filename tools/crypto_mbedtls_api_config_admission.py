#!/usr/bin/env python3
"""Validate the append-only OT-109 mbedTLS/PSA API/config admission delta."""
from __future__ import annotations
import argparse, hashlib, json, re, sys
from pathlib import Path
from typing import Any
import crypto_api_config_acceptance_contract as acceptance
import crypto_mbedtls_api_config_evidence as evidence_validator

SCHEMA, VERSION = "OTCAPIA0", 0
ADMISSION_ID = "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0"
EXPECTED_ADMISSION_SHA256 = "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd"
EXPECTED_ADMISSION_RAW_SHA256 = "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0"
EXPECTED_BUNDLE_RAW_SHA256 = "ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e"
EXPECTED_API_EVIDENCE_RAW_SHA256 = "67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155"
ADMISSION_PUBLIC_RESULT = "MBEDTLS-PSA-FIVE-OF-EIGHT-API-CONFIG-COMPARISON-PARTIAL-ADMITTED-HOST-ONLY; COUNTS-3-1-0; ONE-DIRECT-RADIO-REQUIREMENT-REMAINS; NONSELECTABLE; READINESS-BLOCKED"
MAX_BYTES = 131072
AUTHORITY_FIELDS = ("candidate_import_authorized","benchmark_build_authorized","benchmark_execution_authorized","device_access_authorized","radio_transmit_authorized","key_or_entropy_operation_authorized","candidate_selection_authorized","score_credit_added")
CLAIM_FIELDS = ("complete_api_config_eligibility_proven","candidate_imported","candidate_benchmark_executed","candidate_selected","hardware_or_device_accessed","physical_evidence_added","score_credit_added")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
class ValidationError(ValueError): pass
class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None: self.exit(2, "ERROR: invalid arguments\n")
def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw=path.read_bytes()
        if len(raw)>MAX_BYTES: raise ValidationError("JSON exceeds size limit")
        if expected_raw_sha256 is not None and hashlib.sha256(raw).hexdigest() != expected_raw_sha256: raise ValidationError("raw artifact digest mismatch")
        value=json.loads(raw.decode("utf-8"),object_pairs_hook=_pairs)
        if type(value) is not dict: raise ValidationError("document must be object")
        return value
    except ValidationError: raise
    except (OSError,UnicodeError,json.JSONDecodeError,RecursionError,ValueError) as exc: raise ValidationError("JSON unreadable or invalid") from exc
def _pairs(items):
    out={}
    for k,v in items:
        if k in out: raise ValidationError("duplicate key")
        out[k]=v
    return out
def canonical_sha256(value: Any) -> str: return hashlib.sha256(json.dumps(value,ensure_ascii=False,allow_nan=False,sort_keys=True,separators=(",",":")).encode()).hexdigest()
def _sha(value: Any, name: str) -> str:
    if type(value) is not str or not HEX64.fullmatch(value): raise ValidationError(f"{name} must be SHA-256")
    return value
def validate(admission: dict[str, Any], bundle: dict[str, Any], evidence: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    evidence_validator._scan(admission)
    contract_result=acceptance.validate_contract(contract)
    evidence_result=evidence_validator.validate_api_evidence(evidence,bundle,contract)
    if type(admission) is not dict or set(admission)!={"schema","version","artifact_kind","admission_id","accepted_date","status","public_result","parents","accepted_api_config_evidence_sha256","acceptance_counts","closed_by_this_delta","current_one_blocker","authority","claims"}: raise ValidationError("admission fields mismatch")
    if (admission["schema"],admission["version"],admission["artifact_kind"],admission["admission_id"],admission["accepted_date"],admission["status"],admission["public_result"]) != (SCHEMA,VERSION,"append_only_mbedtls_psa_api_config_acceptance_delta",ADMISSION_ID,"2026-08-21","mbedtls_psa_partial_api_config_admitted_readiness_blocked",ADMISSION_PUBLIC_RESULT): raise ValidationError("admission identity mismatch")
    parents=admission["parents"]
    if parents != {"otcac0_v1_raw_sha256":evidence_validator.PARENTS["otcac0_v1_raw_sha256"],"otcac0_v1_canonical_sha256":contract_result["contract_sha256"],"otcapioe0_v0_raw_sha256":EXPECTED_BUNDLE_RAW_SHA256,"otcapioe0_v0_canonical_sha256":evidence_result["operation_bundle_sha256"],"otcapi0_v2_raw_sha256":EXPECTED_API_EVIDENCE_RAW_SHA256,"otcapi0_v2_canonical_sha256":evidence_result["api_config_evidence_sha256"]}: raise ValidationError("admission parent mismatch")
    accepted=admission["accepted_api_config_evidence_sha256"]
    if accepted != {"espressif_libsodium":[],"esp_idf_mbedtls_psa":[evidence_result["api_config_evidence_sha256"]],"monocypher":[]}: raise ValidationError("accepted registry mismatch")
    if admission["acceptance_counts"]!={"source":3,"api_config":1,"candidate_import":0}: raise ValidationError("acceptance counts mismatch")
    if admission["closed_by_this_delta"]!={"blocker_id":"esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved","closure_evidence_sha256":evidence_result["api_config_evidence_sha256"]}: raise ValidationError("closed blocker mismatch")
    if admission["current_one_blocker"] != ["direct_radio_mtu_phy_region_unresolved"]: raise ValidationError("one-blocker state mismatch")
    for name,fields in (("authority",AUTHORITY_FIELDS),("claims",CLAIM_FIELDS)):
        if type(admission[name]) is not dict or set(admission[name])!=set(fields) or any(type(v) is not bool or v for v in admission[name].values()): raise ValidationError(f"{name} must remain exact false")
    digest=canonical_sha256(admission)
    if EXPECTED_ADMISSION_SHA256 and digest!=EXPECTED_ADMISSION_SHA256: raise ValidationError("admission digest mismatch")
    return {"schema":SCHEMA,"version":VERSION,"admission_id":ADMISSION_ID,"accepted_api_config_count":1,"source_count":3,"candidate_import_count":0,"blocker_count":1,"fully_resolved":False,"execution_authorized":False,"selection_authorized":False,"score_credit_added":False,"admission_sha256":digest}

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

if __name__ == "__main__": raise SystemExit(main())
