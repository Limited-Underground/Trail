#!/usr/bin/env python3
"""Validate OT-109 host-only mbedTLS/PSA API/configuration evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

import crypto_api_config_acceptance_contract as acceptance

SCHEMA, VERSION = "OTCAPIOE0", 0
EVIDENCE_ID = "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-V0"
API_EVIDENCE_ID = "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-EVIDENCE-V2"
EXPECTED_BUNDLE_SHA256 = "6a17a6f5a753a19d2d78d7cb6f0c757ef9791e0bf2e953e27afc3eccb04f27ed"
EXPECTED_BUNDLE_RAW_SHA256 = "ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e"
EXPECTED_API_EVIDENCE_SHA256 = "22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8"
EXPECTED_API_EVIDENCE_RAW_SHA256 = "67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155"
BUNDLE_PUBLIC_RESULT = "MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-GENERATED-HOST-ONLY; FIVE-OF-EIGHT-COMPARISON-PARTIAL; NONSELECTABLE; NO-IMPORT-BUILD-BENCHMARK-DEVICE-RADIO-OR-SCORE"
AUTHORITY_FIELDS = ("candidate_import_authorized","benchmark_build_authorized","benchmark_execution_authorized","device_access_authorized","radio_transmit_authorized","key_or_entropy_operation_authorized","candidate_selection_authorized","score_credit_added")
CLAIM_FIELDS = ("api_config_evidence_generated","api_config_evidence_accepted","candidate_imported","candidate_benchmark_executed","candidate_selected","hardware_or_device_accessed","physical_evidence_added","score_credit_added")
MAX_BYTES, MAX_DEPTH, MAX_NODES, MAX_STRING = 262144, 18, 8192, 1024
HEX64 = re.compile(r"^[0-9a-f]{64}$")
OPERATIONS = acceptance.OPERATIONS
ELIGIBLE_OPERATIONS = frozenset(("x25519", "sha256", "hkdf_sha256", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"))
UNAVAILABLE_OPERATIONS = frozenset(OPERATIONS) - ELIGIBLE_OPERATIONS
REQUIRED_EFFECTIVE_SYMBOLS = tuple(sorted((
    'CONFIG_IDF_TARGET="esp32s3"', "CONFIG_APP_REPRODUCIBLE_BUILD=y",
    "CONFIG_APP_COMPILE_TIME_DATE=n", "CONFIG_COMPILER_OPTIMIZATION_SIZE=y",
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y", "CONFIG_FREERTOS_HZ=100",
    "CONFIG_MBEDTLS_CHACHA20_C=y", "CONFIG_MBEDTLS_CHACHAPOLY_C=y",
)))
FORBIDDEN_EFFECTIVE_SYMBOLS = tuple(sorted((
    "CONFIG_APP_COMPILE_TIME_DATE=y", "CONFIG_MBEDTLS_CHACHA20_C=n",
    "CONFIG_MBEDTLS_CHACHAPOLY_C=n",
)))
OPERATION_FACTS = {
    "ed25519_sign": ("unavailable", (), (), (), "identifiers_and_generic_api_only_no_ed25519_implementation"),
    "ed25519_verify": ("unavailable", (), (), (), "identifiers_and_generic_api_only_no_ed25519_implementation"),
    "x25519": ("eligible", ("tf_psa_crypto_values","tf_psa_api","tf_psa_core","tf_psa_x25519","tf_psa_ecp"), ("psa_raw_key_agreement","PSA_ALG_ECDH","PSA_ECC_FAMILY_MONTGOMERY"), (), "concrete_psa_api_and_everest_implementation_present"),
    "sha256": ("eligible", ("tf_psa_crypto_values","tf_psa_api","tf_psa_core","tf_psa_hash_dispatch","tf_psa_sha256"), ("psa_hash_compute","PSA_ALG_SHA_256"), (), "concrete_psa_api_and_builtin_implementation_present"),
    "hkdf_sha256": ("eligible", ("tf_psa_crypto_config","tf_psa_crypto_values","tf_psa_api","tf_psa_core"), ("psa_key_derivation_setup","psa_key_derivation_input_bytes","psa_key_derivation_input_key","psa_key_derivation_output_bytes","PSA_ALG_HKDF","PSA_ALG_SHA_256"), (), "concrete_psa_api_and_core_implementation_present"),
    "chacha20poly1305_encrypt": ("eligible", ("tf_psa_crypto_values","tf_psa_api","tf_psa_core","tf_psa_aead_dispatch","tf_psa_chacha20","tf_psa_chachapoly","tf_psa_poly1305"), ("psa_aead_encrypt","PSA_ALG_CHACHA20_POLY1305"), ("CONFIG_MBEDTLS_CHACHA20_C=y","CONFIG_MBEDTLS_CHACHAPOLY_C=y"), "concrete_psa_api_and_builtin_implementation_present_default_disabled"),
    "chacha20poly1305_decrypt": ("eligible", ("tf_psa_crypto_values","tf_psa_api","tf_psa_core","tf_psa_aead_dispatch","tf_psa_chacha20","tf_psa_chachapoly","tf_psa_poly1305"), ("psa_aead_decrypt","PSA_ALG_CHACHA20_POLY1305"), ("CONFIG_MBEDTLS_CHACHA20_C=y","CONFIG_MBEDTLS_CHACHAPOLY_C=y"), "concrete_psa_api_and_builtin_implementation_present_default_disabled"),
    "noise_xk_handshake": ("unavailable", (), (), (), "no_noise_xk_composition_or_state_machine_present"),
}
PARENTS = {
    "otcmse0_v0_raw_sha256": "1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030",
    "otcmse0_v0_canonical_sha256": "3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e",
    "otcsle0_v1_raw_sha256": "ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49",
    "otmpsla0_v0_raw_sha256": "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85",
    "otcbcge0_v0_raw_sha256": "0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc",
    "otcbcga0_v0_raw_sha256": "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
    "otcac0_v1_raw_sha256": "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
    "otcac0_v1_canonical_sha256": "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22",
}
CANDIDATE = acceptance.CANDIDATE_BY_ID["esp_idf_mbedtls_psa"]

class ValidationError(ValueError): pass

class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, "ERROR: invalid arguments\n")

def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    out = {}
    for key, value in items:
        if key in out: raise ValidationError("JSON contains a duplicate key")
        out[key] = value
    return out

def _obj(value: Any, name: str) -> dict[str, Any]:
    if type(value) is not dict: raise ValidationError(f"{name} must be an object")
    return value

def _arr(value: Any, name: str) -> list[Any]:
    if type(value) is not list: raise ValidationError(f"{name} must be an array")
    return value

def _keys(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected: raise ValidationError(f"{name} fields are not exact")

def _sha(value: Any, name: str) -> str:
    if type(value) is not str or not HEX64.fullmatch(value): raise ValidationError(f"{name} must be a lowercase SHA-256")
    return value

def _bool(value: Any, name: str, expected: bool) -> None:
    if type(value) is not bool or value is not expected: raise ValidationError(f"{name} must be {str(expected).lower()}")

def _scan(value: Any) -> None:
    seen, nodes = set(), 0
    def visit(item: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if depth > MAX_DEPTH or nodes > MAX_NODES: raise ValidationError("document exceeds structural bounds")
        if type(item) in (dict, list):
            if id(item) in seen: raise ValidationError("document contains a cycle")
            seen.add(id(item))
            for child in (item.values() if type(item) is dict else item): visit(child, depth + 1)
            seen.remove(id(item))
        elif type(item) is str:
            if len(item) > MAX_STRING: raise ValidationError("string exceeds bound")
            if re.search(r"[A-Za-z]:\\|/(?:home|users)/|\bCOM[0-9]+\b|\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", item, re.I): raise ValidationError("private machine or device text")
        elif item is not None and type(item) not in (bool, int): raise ValidationError("noncanonical JSON type")
    visit(value, 0)

def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if len(raw) > MAX_BYTES: raise ValidationError("JSON exceeds size limit")
        if expected_raw_sha256 is not None and hashlib.sha256(raw).hexdigest() != expected_raw_sha256: raise ValidationError("raw artifact digest mismatch")
        return _obj(json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs), "document")
    except ValidationError: raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc: raise ValidationError("JSON unreadable or invalid") from exc

def canonical_sha256(value: Any) -> str:
    try: raw = json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode()
    except (TypeError, ValueError, RecursionError, OverflowError) as exc: raise ValidationError("canonical serialization failed") from exc
    return hashlib.sha256(raw).hexdigest()

def operation_sha256(record: dict[str, Any]) -> str: return canonical_sha256(record)

def validate_operation_bundle(bundle: dict[str, Any]) -> dict[str, Any]:
    _scan(bundle); bundle = _obj(bundle, "bundle")
    _keys(bundle, {"schema","version","artifact_kind","evidence_id","recorded_date","status","public_result","parents","candidate","configuration_reproduction","operation_records","authority","claims"}, "bundle")
    if (bundle["schema"],bundle["version"],bundle["artifact_kind"],bundle["evidence_id"],bundle["recorded_date"],bundle["status"],bundle["public_result"]) != (SCHEMA,VERSION,"mbedtls_psa_api_config_operation_evidence",EVIDENCE_ID,"2026-08-21","comparison_partial_host_only",BUNDLE_PUBLIC_RESULT):
        raise ValidationError("bundle identity mismatch")
    if bundle["parents"] != PARENTS or bundle["candidate"] != CANDIDATE: raise ValidationError("bundle parent or candidate binding mismatch")
    reproduction = _obj(bundle["configuration_reproduction"], "configuration_reproduction")
    _keys(reproduction, {"run_count","runs_identical","generated_sdkconfig_bytes","generated_sdkconfig_sha256","required_effective_symbols","forbidden_effective_symbols","runs"}, "configuration_reproduction")
    if reproduction["run_count"] != 2 or reproduction["runs_identical"] is not True or reproduction["generated_sdkconfig_bytes"] != 106921 or reproduction["generated_sdkconfig_sha256"] != CANDIDATE["generated_sdkconfig_sha256"]: raise ValidationError("configuration reproduction mismatch")
    required = _arr(reproduction["required_effective_symbols"], "required_effective_symbols")
    forbidden = _arr(reproduction["forbidden_effective_symbols"], "forbidden_effective_symbols")
    if required != list(REQUIRED_EFFECTIVE_SYMBOLS) or forbidden != list(FORBIDDEN_EFFECTIVE_SYMBOLS): raise ValidationError("effective symbol lists mismatch")
    runs = _arr(reproduction["runs"], "runs")
    if len(runs) != 2: raise ValidationError("two fresh runs required")
    for index, run in enumerate(runs):
        run = _obj(run, f"run[{index}]"); _keys(run, {"run","isolated_root_initially_absent","configuration_only","component_manager_disabled","exit_code","generated_sdkconfig_bytes","generated_sdkconfig_sha256","required_symbols_present","forbidden_symbols_absent","candidate_source_copied","candidate_compiled","benchmark_executed","device_accessed","radio_used"}, f"run[{index}]")
        if run["run"] != ("A" if index == 0 else "B") or run["exit_code"] != 0 or run["generated_sdkconfig_bytes"] != 106921 or run["generated_sdkconfig_sha256"] != CANDIDATE["generated_sdkconfig_sha256"]: raise ValidationError("configuration run mismatch")
        for field in ("isolated_root_initially_absent","configuration_only","component_manager_disabled","required_symbols_present","forbidden_symbols_absent"): _bool(run[field], field, True)
        for field in ("candidate_source_copied","candidate_compiled","benchmark_executed","device_accessed","radio_used"): _bool(run[field], field, False)
    records = _arr(bundle["operation_records"], "operation_records")
    if len(records) != len(OPERATIONS): raise ValidationError("operation set incomplete")
    operation_digests = {}
    for index, operation_id in enumerate(OPERATIONS):
        record = _obj(records[index], f"operation[{index}]")
        _keys(record, {"operation_id","state","source_assessment_sha256","source_anchor_ids","api_symbols","required_effective_symbols","source_state","final_sdkconfig_sha256"}, f"operation[{index}]")
        if record["operation_id"] != operation_id or record["source_assessment_sha256"] != PARENTS["otcmse0_v0_canonical_sha256"] or record["final_sdkconfig_sha256"] != CANDIDATE["generated_sdkconfig_sha256"]: raise ValidationError("operation binding mismatch")
        anchors, symbols, config = (_arr(record[k], f"operation[{index}].{k}") for k in ("source_anchor_ids","api_symbols","required_effective_symbols"))
        expected_state, expected_anchors, expected_symbols, expected_config, expected_source_state = OPERATION_FACTS[operation_id]
        if (record["state"] != expected_state or anchors != list(expected_anchors)
                or symbols != list(expected_symbols) or config != list(expected_config)
                or record["source_state"] != expected_source_state):
            raise ValidationError("operation facts do not match accepted OT-096/configuration evidence")
        if operation_id in ELIGIBLE_OPERATIONS:
            operation_digests[operation_id] = operation_sha256(record)
    if len(operation_digests) != 5 or len(set(operation_digests.values())) != 5: raise ValidationError("eligible operation digest mismatch")
    for name, fields in (("authority",AUTHORITY_FIELDS),("claims",CLAIM_FIELDS)):
        values = _obj(bundle[name], name); _keys(values,set(fields),name)
        for field, value in values.items():
            expected = name == "claims" and field == "api_config_evidence_generated"
            if type(value) is not bool or value is not expected: raise ValidationError(f"{name} claim disposition mismatch")
    digest = canonical_sha256(bundle)
    if EXPECTED_BUNDLE_SHA256 and digest != EXPECTED_BUNDLE_SHA256: raise ValidationError("bundle digest mismatch")
    return {"schema":SCHEMA,"version":VERSION,"candidate_id":CANDIDATE["candidate_id"],"eligible_operation_count":5,"coverage_state":"comparison_partial","selection_eligible":False,"operation_evidence_sha256":operation_digests,"bundle_sha256":digest,"execution_authorized":False,"score_credit_added":False}

def validate_api_evidence(evidence: dict[str, Any], bundle: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    bundle_result = validate_operation_bundle(bundle)
    result = acceptance.validate_evidence_structure(evidence, contract)
    if evidence["evidence_id"] != API_EVIDENCE_ID or result["candidate_id"] != CANDIDATE["candidate_id"] or result["coverage_state"] != "comparison_partial" or result["selection_eligible"] is not False: raise ValidationError("OTCAPI0/v2 disposition mismatch")
    expected = bundle_result["operation_evidence_sha256"]
    for operation in evidence["operation_results"]:
        operation_id = operation["operation_id"]
        if operation_id in expected and operation["evidence_sha256"] != expected[operation_id]: raise ValidationError("operation evidence digest mismatch")
        if operation_id not in expected and operation["evidence_sha256"] is not None: raise ValidationError("unavailable operation digest mismatch")
    digest = canonical_sha256(evidence)
    if EXPECTED_API_EVIDENCE_SHA256 and digest != EXPECTED_API_EVIDENCE_SHA256: raise ValidationError("API/config evidence digest mismatch")
    return {**result,"operation_bundle_sha256":bundle_result["bundle_sha256"],"api_config_evidence_sha256":digest}

def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser()
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--api-evidence", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate_api_evidence(load(args.api_evidence, EXPECTED_API_EVIDENCE_RAW_SHA256), load(args.bundle, EXPECTED_BUNDLE_RAW_SHA256), acceptance.load(args.contract))
    except (ValidationError, acceptance.ValidationError):
        print("ERROR: validation failed", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0

if __name__ == "__main__": raise SystemExit(main())
