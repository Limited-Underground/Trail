#!/usr/bin/env python3
"""Validate bounded host-only OT-118 Monocypher API/configuration evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

import crypto_api_config_acceptance_contract as acceptance

ROOT = Path(__file__).resolve().parents[1]
SCHEMA, VERSION = "OTCAPIOE0", 0
EVIDENCE_ID = "OT-118-OT005-MONOCYPHER-API-CONFIG-OPERATION-EVIDENCE-V0"
API_EVIDENCE_ID = "OT-118-OT005-MONOCYPHER-API-CONFIG-EVIDENCE-V2"
EXPECTED_BUNDLE_RAW_SHA256 = "3a5f21eecf3be83b4259282c42f68d3c24780a9269bb0993ccfaeec648a2eb8d"
EXPECTED_BUNDLE_SHA256 = "e8be5eade8699b51f6d5dee42a8ca85b05d15ac8119e9c2ddccd700a542aa852"
EXPECTED_API_EVIDENCE_RAW_SHA256 = "f158c2b33dd9a6bbcdf6b13f396e831703184eeeb623914e400bf10d9929c009"
EXPECTED_API_EVIDENCE_SHA256 = "c1ce6c0de2a72852359fa15efd9e27d9ffd15171362ab6adc4d04f66825949e9"
EXPECTED_ADAPTER_HEADER_RAW_SHA256 = "110f5b54cf37538e30450d73a3402807eb59143f2ceb528b12280b7725e52072"
EXPECTED_ADAPTER_SOURCE_RAW_SHA256 = "e12b800841c6c8347cdf08d05768f2cfbc83ee271fdae7616f8a3b16e4263e59"
ADAPTER_HEADER = ROOT / "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.h"
ADAPTER_SOURCE = ROOT / "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c"
CORE_SOURCE = ROOT / "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c"
ED_SOURCE = ROOT / "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c"
CORE_SHA = "f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123"
ED_SHA = "ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453"
OPERATIONS = acceptance.OPERATIONS
ELIGIBLE = {"ed25519_sign", "ed25519_verify", "x25519", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"}
DIRECT = ["crypto_ed25519_sign", "crypto_ed25519_check", "crypto_x25519", "crypto_aead_init_ietf", "crypto_aead_write", "crypto_aead_read", "crypto_wipe"]
MAX_BYTES = 262144


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


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _scan(value: Any, depth: int = 0) -> None:
    if depth > 18: raise ValidationError("document exceeds structural bounds")
    if type(value) is dict:
        for item in value.values(): _scan(item, depth + 1)
    elif type(value) is list:
        for item in value: _scan(item, depth + 1)
    elif type(value) is str:
        if len(value) > 1024 or re.search(r"[A-Za-z]:\\|/(?:home|users)/|\bCOM[0-9]+\b|\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", value, re.I): raise ValidationError("private or unbounded text")
    elif value is not None and type(value) not in (bool, int): raise ValidationError("noncanonical JSON type")


def _file_digest(path: Path, digest: str) -> None:
    try:
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest: raise ValidationError("bound source digest mismatch")
    except OSError as exc: raise ValidationError("bound source unreadable") from exc


def validate_operation_bundle(bundle: dict[str, Any]) -> dict[str, Any]:
    _scan(bundle)
    exact = {"schema", "version", "artifact_kind", "evidence_id", "recorded_date", "status", "public_result", "parents", "candidate", "configuration_reproduction", "adapter", "operation_records", "authority", "claims"}
    if set(bundle) != exact or (bundle["schema"], bundle["version"], bundle["artifact_kind"], bundle["evidence_id"], bundle["recorded_date"], bundle["status"]) != (SCHEMA, VERSION, "monocypher_api_config_operation_evidence", EVIDENCE_ID, "2026-08-22", "comparison_partial_host_only"): raise ValidationError("bundle identity mismatch")
    candidate = bundle["candidate"]
    if candidate != {"candidate_id":"monocypher","role":"comparison","source_evidence_sha256":"fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f","source_admission_raw_sha256":"6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52","generated_sdkconfig_sha256":"4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"}: raise ValidationError("candidate binding mismatch")
    reproduction = bundle["configuration_reproduction"]
    if reproduction["run_count"] != 2 or reproduction["runs_identical"] is not True or reproduction["generated_sdkconfig_bytes"] != 106913 or reproduction["generated_sdkconfig_sha256"] != candidate["generated_sdkconfig_sha256"]: raise ValidationError("configuration reproduction mismatch")
    if reproduction["required_effective_symbols"] != ['CONFIG_APP_COMPILE_TIME_DATE=n','CONFIG_APP_REPRODUCIBLE_BUILD=y','CONFIG_COMPILER_OPTIMIZATION_SIZE=y','CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y','CONFIG_FREERTOS_HZ=100','CONFIG_IDF_TARGET="esp32s3"'] or reproduction["forbidden_effective_symbols"] != ['CONFIG_APP_COMPILE_TIME_DATE=y']: raise ValidationError("effective symbol mismatch")
    if reproduction["source_requirements"] != [{"path":"tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c","raw_sha256":CORE_SHA},{"path":"tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c","raw_sha256":ED_SHA}]: raise ValidationError("source requirements mismatch")
    for index, run in enumerate(reproduction["runs"]):
        if run["run"] != ("A" if index == 0 else "B") or run["exit_code"] != 0 or run["generated_sdkconfig_bytes"] != 106913 or run["generated_sdkconfig_sha256"] != candidate["generated_sdkconfig_sha256"]: raise ValidationError("configuration run mismatch")
        for field in ("isolated_root_initially_absent","configuration_only","component_manager_disabled","required_symbols_present","forbidden_symbols_absent"):
            if run[field] is not True: raise ValidationError("configuration run true field mismatch")
        for field in ("candidate_source_copied","candidate_compiled","benchmark_executed","device_accessed","radio_used"):
            if run[field] is not False: raise ValidationError("configuration run must be false")
    adapter = bundle["adapter"]
    if adapter != {"schema":"OTMAPI0","version":0,"header_path":"tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.h","header_raw_sha256":EXPECTED_ADAPTER_HEADER_RAW_SHA256,"source_path":"tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c","source_raw_sha256":EXPECTED_ADAPTER_SOURCE_RAW_SHA256,"direct_api_symbols":DIRECT,"benchmark_only":True}: raise ValidationError("adapter binding mismatch")
    _file_digest(ADAPTER_HEADER, EXPECTED_ADAPTER_HEADER_RAW_SHA256); _file_digest(ADAPTER_SOURCE, EXPECTED_ADAPTER_SOURCE_RAW_SHA256); _file_digest(CORE_SOURCE, CORE_SHA); _file_digest(ED_SOURCE, ED_SHA)
    records = bundle["operation_records"]
    if len(records) != 8 or [r["operation_id"] for r in records] != list(OPERATIONS): raise ValidationError("operation order mismatch")
    digests = {}
    for record in records:
        operation = record["operation_id"]
        expected_state = "eligible" if operation in ELIGIBLE else "unavailable"
        if record["state"] != expected_state or record["final_sdkconfig_sha256"] != candidate["generated_sdkconfig_sha256"]: raise ValidationError("operation state mismatch")
        if expected_state == "unavailable" and (record["source_anchor_ids"] or record["api_symbols"] or record["required_effective_symbols"]): raise ValidationError("unavailable operation must have no evidence")
        digests[operation] = canonical_sha256(record)
    for field, value in bundle["authority"].items():
        if type(value) is not bool or value: raise ValidationError("authority disposition mismatch")
    for field, value in bundle["claims"].items():
        if type(value) is not bool or value is (field != "api_config_evidence_generated"): raise ValidationError("claims disposition mismatch")
    digest = canonical_sha256(bundle)
    if digest != EXPECTED_BUNDLE_SHA256: raise ValidationError("bundle digest mismatch")
    return {"schema":SCHEMA,"version":VERSION,"candidate_id":"monocypher","eligible_operation_count":5,"coverage_state":"comparison_partial","selection_eligible":False,"operation_evidence_sha256":digests,"bundle_sha256":digest,"execution_authorized":False,"selection_authorized":False,"score_credit_added":False}


def validate_api_evidence(evidence: dict[str, Any], bundle: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    bundle_result = validate_operation_bundle(bundle)
    try: result = acceptance.validate_evidence_structure(evidence, contract)
    except acceptance.ValidationError as exc: raise ValidationError(str(exc)) from exc
    if evidence["evidence_id"] != API_EVIDENCE_ID or result["candidate_id"] != "monocypher" or result["coverage_state"] != "comparison_partial" or result["eligible_operation_count"] != 5 or result["selection_eligible"] is not False: raise ValidationError("partial comparison disposition mismatch")
    for operation in evidence["operation_results"]:
        expected = bundle_result["operation_evidence_sha256"][operation["operation_id"]] if operation["state"] == "eligible" else None
        if operation["evidence_sha256"] != expected: raise ValidationError("operation evidence digest mismatch")
    digest = canonical_sha256(evidence)
    if digest != EXPECTED_API_EVIDENCE_SHA256: raise ValidationError("API evidence digest mismatch")
    return {**result,"operation_bundle_sha256":bundle_result["bundle_sha256"],"api_config_evidence_sha256":digest}


def main(argv=None) -> int:
    parser = SafeArgumentParser(); parser.add_argument("--bundle", required=True, type=Path); parser.add_argument("--api-evidence", required=True, type=Path); parser.add_argument("--contract", required=True, type=Path); args = parser.parse_args(argv)
    try: result = validate_api_evidence(load(args.api_evidence, EXPECTED_API_EVIDENCE_RAW_SHA256), load(args.bundle, EXPECTED_BUNDLE_RAW_SHA256), acceptance.load(args.contract))
    except (ValidationError, acceptance.ValidationError): print("ERROR: validation failed", file=sys.stderr); return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":"))); return 0


if __name__ == "__main__": raise SystemExit(main())
