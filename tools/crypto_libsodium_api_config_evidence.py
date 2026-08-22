#!/usr/bin/env python3
"""Validate complete host-only OT-117 libsodium API/configuration evidence."""
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
EVIDENCE_ID = "OT-117-OT005-LIBSODIUM-API-CONFIG-OPERATION-EVIDENCE-V0"
API_EVIDENCE_ID = "OT-117-OT005-LIBSODIUM-API-CONFIG-EVIDENCE-V2"
EXPECTED_BUNDLE_SHA256 = "6419ac77392aa7b7f295cbda7719a16581a617e7564c2afec6c03aac7b2fea90"
EXPECTED_BUNDLE_RAW_SHA256 = "b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58"
EXPECTED_API_EVIDENCE_SHA256 = "6e5e969c3b3f7bf29372e15e3cc75c693fb00f57f7f297c14cc77123dec4610d"
EXPECTED_API_EVIDENCE_RAW_SHA256 = "34888d71da2c9042856ea48c7b1225f21c1345582c144239cab0096ff03e69b5"
EXPECTED_ADAPTER_HEADER_RAW_SHA256 = "b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45"
EXPECTED_ADAPTER_SOURCE_RAW_SHA256 = "8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8"
ADAPTER_HEADER = ROOT / "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.h"
ADAPTER_SOURCE = ROOT / "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
BUNDLE_PUBLIC_RESULT = (
    "LIBSODIUM-EIGHT-OF-EIGHT-API-CONFIG-EVIDENCE-GENERATED-HOST-ONLY; "
    "PRIMARY-COMPLETE-STRUCTURALLY-SELECTION-ELIGIBLE; "
    "NO-IMPORT-BUILD-BENCHMARK-DEVICE-RADIO-SELECTION-OR-SCORE"
)
MAX_BYTES, MAX_DEPTH, MAX_NODES, MAX_STRING = 262144, 18, 8192, 1024
HEX64 = re.compile(r"^[0-9a-f]{64}$")
OPERATIONS = acceptance.OPERATIONS
CANDIDATE = acceptance.CANDIDATE_BY_ID["espressif_libsodium"]
REQUIRED_EFFECTIVE_SYMBOLS = tuple(sorted((
    'CONFIG_IDF_TARGET="esp32s3"',
    "CONFIG_APP_REPRODUCIBLE_BUILD=y",
    "CONFIG_APP_COMPILE_TIME_DATE=n",
    "CONFIG_COMPILER_OPTIMIZATION_SIZE=y",
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y",
    "CONFIG_FREERTOS_HZ=100",
    "CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n",
)))
FORBIDDEN_EFFECTIVE_SYMBOLS = tuple(sorted((
    "CONFIG_APP_COMPILE_TIME_DATE=y",
    "CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=y",
)))
ADAPTER_DIRECT_SYMBOLS = (
    "crypto_hash_sha256", "crypto_hash_sha256_init", "crypto_hash_sha256_update", "crypto_hash_sha256_final",
    "crypto_kdf_hkdf_sha256_extract", "crypto_kdf_hkdf_sha256_expand",
    "crypto_scalarmult_curve25519",
    "crypto_aead_chacha20poly1305_ietf_encrypt", "crypto_aead_chacha20poly1305_ietf_decrypt",
    "sodium_memzero",
)
OPERATION_FACTS = {
    "ed25519_sign": (("libsodium_crypto_sign_header", "libsodium_crypto_sign_implementation"), ("crypto_sign_detached",), (), "concrete_libsodium_api_and_ed25519_implementation_present"),
    "ed25519_verify": (("libsodium_crypto_sign_header", "libsodium_crypto_sign_implementation"), ("crypto_sign_verify_detached",), (), "concrete_libsodium_api_and_ed25519_implementation_present"),
    "x25519": (("libsodium_curve25519_header", "libsodium_curve25519_implementation"), ("crypto_scalarmult_curve25519",), (), "concrete_libsodium_api_and_curve25519_implementation_present"),
    "sha256": (("libsodium_sha256_header", "libsodium_sha256_implementation"), ("crypto_hash_sha256",), ("CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n",), "concrete_libsodium_api_and_native_sha256_implementation_present"),
    "hkdf_sha256": (("libsodium_hkdf_sha256_header", "libsodium_hkdf_sha256_implementation"), ("crypto_kdf_hkdf_sha256_extract", "crypto_kdf_hkdf_sha256_expand"), ("CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n",), "concrete_libsodium_api_and_native_hkdf_sha256_implementation_present"),
    "chacha20poly1305_encrypt": (("libsodium_chacha20poly1305_header", "libsodium_chacha20poly1305_implementation"), ("crypto_aead_chacha20poly1305_ietf_encrypt",), (), "concrete_libsodium_ietf_chacha20poly1305_encrypt_present"),
    "chacha20poly1305_decrypt": (("libsodium_chacha20poly1305_header", "libsodium_chacha20poly1305_implementation"), ("crypto_aead_chacha20poly1305_ietf_decrypt",), (), "concrete_libsodium_ietf_chacha20poly1305_decrypt_present"),
    "noise_xk_handshake": (("ot117_noise_xk_adapter_header", "ot117_noise_xk_adapter_source"), ADAPTER_DIRECT_SYMBOLS, ("CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n",), "concrete_benchmark_only_noise_xk_25519_chachapoly_sha256_adapter_present"),
}
PARENTS = {
    "otlmi0_v0_raw_sha256": "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9",
    "otcsla0_v0_raw_sha256": "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0",
    "otcbcge0_v0_raw_sha256": "0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc",
    "otcbcga0_v0_raw_sha256": "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
    "otcac0_v1_raw_sha256": "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
    "otcac0_v1_canonical_sha256": "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22",
    "otcapia0_v0_raw_sha256": "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0",
    "otcapia0_v0_canonical_sha256": "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd",
}
AUTHORITY_FIELDS = (
    "candidate_import_authorized", "benchmark_build_authorized", "benchmark_execution_authorized",
    "device_access_authorized", "radio_transmit_authorized", "key_or_entropy_operation_authorized",
    "candidate_selection_authorized", "score_credit_added",
)
CLAIM_FIELDS = (
    "api_config_evidence_generated", "api_config_evidence_accepted", "candidate_imported",
    "candidate_benchmark_executed", "candidate_selected", "hardware_or_device_accessed",
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
            raise ValidationError("JSON contains a duplicate key")
        out[key] = value
    return out


def _obj(value: Any, name: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{name} must be an object")
    return value


def _arr(value: Any, name: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{name} must be an array")
    return value


def _keys(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{name} fields are not exact")


def _bool(value: Any, name: str, expected: bool) -> None:
    if type(value) is not bool or value is not expected:
        raise ValidationError(f"{name} must be {str(expected).lower()}")


def _scan(value: Any) -> None:
    seen: set[int] = set()
    nodes = 0

    def visit(item: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if depth > MAX_DEPTH or nodes > MAX_NODES:
            raise ValidationError("document exceeds structural bounds")
        if type(item) in (dict, list):
            if id(item) in seen:
                raise ValidationError("document contains a cycle")
            seen.add(id(item))
            for child in (item.values() if type(item) is dict else item):
                visit(child, depth + 1)
            seen.remove(id(item))
        elif type(item) is str:
            if len(item) > MAX_STRING:
                raise ValidationError("string exceeds bound")
            if re.search(r"[A-Za-z]:\\|/(?:home|users)/|\bCOM[0-9]+\b|\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", item, re.I):
                raise ValidationError("private machine or device text")
        elif item is not None and type(item) not in (bool, int):
            raise ValidationError("noncanonical JSON type")

    visit(value, 0)


def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if len(raw) > MAX_BYTES:
            raise ValidationError("JSON exceeds size limit")
        if expected_raw_sha256 and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
            raise ValidationError("raw artifact digest mismatch")
        return _obj(json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs), "document")
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


def _validate_adapter_files(adapter: dict[str, Any]) -> None:
    _keys(adapter, {"schema", "version", "suite", "header_path", "header_raw_sha256", "source_path", "source_raw_sha256", "direct_api_symbols", "benchmark_only"}, "adapter")
    expected = {
        "schema": "OTNXK0", "version": 0,
        "suite": "Noise_XK_25519_ChaChaPoly_SHA256",
        "header_path": "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.h",
        "header_raw_sha256": EXPECTED_ADAPTER_HEADER_RAW_SHA256,
        "source_path": "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c",
        "source_raw_sha256": EXPECTED_ADAPTER_SOURCE_RAW_SHA256,
        "direct_api_symbols": list(ADAPTER_DIRECT_SYMBOLS),
        "benchmark_only": True,
    }
    if adapter != expected:
        raise ValidationError("adapter binding mismatch")
    for path, digest in ((ADAPTER_HEADER, EXPECTED_ADAPTER_HEADER_RAW_SHA256), (ADAPTER_SOURCE, EXPECTED_ADAPTER_SOURCE_RAW_SHA256)):
        try:
            if not digest or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValidationError("adapter raw artifact digest mismatch")
        except OSError as exc:
            raise ValidationError("adapter raw artifact unreadable") from exc


def validate_operation_bundle(bundle: dict[str, Any]) -> dict[str, Any]:
    _scan(bundle)
    bundle = _obj(bundle, "bundle")
    _keys(bundle, {"schema", "version", "artifact_kind", "evidence_id", "recorded_date", "status", "public_result", "parents", "candidate", "configuration_reproduction", "adapter", "operation_records", "authority", "claims"}, "bundle")
    identity = (bundle["schema"], bundle["version"], bundle["artifact_kind"], bundle["evidence_id"], bundle["recorded_date"], bundle["status"], bundle["public_result"])
    if identity != (SCHEMA, VERSION, "libsodium_api_config_operation_evidence", EVIDENCE_ID, "2026-08-22", "complete_primary_host_only", BUNDLE_PUBLIC_RESULT):
        raise ValidationError("bundle identity mismatch")
    if bundle["parents"] != PARENTS or bundle["candidate"] != CANDIDATE:
        raise ValidationError("bundle parent or candidate binding mismatch")
    reproduction = _obj(bundle["configuration_reproduction"], "configuration_reproduction")
    _keys(reproduction, {"run_count", "runs_identical", "generated_sdkconfig_bytes", "generated_sdkconfig_sha256", "required_effective_symbols", "forbidden_effective_symbols", "runs"}, "configuration_reproduction")
    if reproduction["run_count"] != 2 or reproduction["runs_identical"] is not True or reproduction["generated_sdkconfig_bytes"] != 107001 or reproduction["generated_sdkconfig_sha256"] != CANDIDATE["generated_sdkconfig_sha256"]:
        raise ValidationError("configuration reproduction mismatch")
    if reproduction["required_effective_symbols"] != list(REQUIRED_EFFECTIVE_SYMBOLS) or reproduction["forbidden_effective_symbols"] != list(FORBIDDEN_EFFECTIVE_SYMBOLS):
        raise ValidationError("effective symbol lists mismatch")
    runs = _arr(reproduction["runs"], "runs")
    if len(runs) != 2:
        raise ValidationError("two fresh runs required")
    for index, run in enumerate(runs):
        run = _obj(run, f"run[{index}]")
        _keys(run, {"run", "isolated_root_initially_absent", "configuration_only", "component_manager_disabled", "exit_code", "generated_sdkconfig_bytes", "generated_sdkconfig_sha256", "required_symbols_present", "forbidden_symbols_absent", "candidate_source_copied", "candidate_compiled", "benchmark_executed", "device_accessed", "radio_used"}, f"run[{index}]")
        if run["run"] != ("A" if index == 0 else "B") or run["exit_code"] != 0 or run["generated_sdkconfig_bytes"] != 107001 or run["generated_sdkconfig_sha256"] != CANDIDATE["generated_sdkconfig_sha256"]:
            raise ValidationError("configuration run mismatch")
        for field in ("isolated_root_initially_absent", "configuration_only", "component_manager_disabled", "required_symbols_present", "forbidden_symbols_absent"):
            _bool(run[field], field, True)
        for field in ("candidate_source_copied", "candidate_compiled", "benchmark_executed", "device_accessed", "radio_used"):
            _bool(run[field], field, False)
    _validate_adapter_files(_obj(bundle["adapter"], "adapter"))
    records = _arr(bundle["operation_records"], "operation_records")
    if len(records) != len(OPERATIONS):
        raise ValidationError("operation set incomplete")
    operation_digests: dict[str, str] = {}
    for index, operation_id in enumerate(OPERATIONS):
        record = _obj(records[index], f"operation[{index}]")
        _keys(record, {"operation_id", "state", "source_anchor_ids", "api_symbols", "required_effective_symbols", "source_state", "final_sdkconfig_sha256"}, f"operation[{index}]")
        anchors, symbols, config, source_state = OPERATION_FACTS[operation_id]
        if record != {"operation_id": operation_id, "state": "eligible", "source_anchor_ids": list(anchors), "api_symbols": list(symbols), "required_effective_symbols": list(config), "source_state": source_state, "final_sdkconfig_sha256": CANDIDATE["generated_sdkconfig_sha256"]}:
            raise ValidationError("operation facts mismatch")
        operation_digests[operation_id] = canonical_sha256(record)
    if len(operation_digests) != 8 or len(set(operation_digests.values())) != 8:
        raise ValidationError("complete purpose-distinct operation evidence required")
    for name, fields in (("authority", AUTHORITY_FIELDS), ("claims", CLAIM_FIELDS)):
        values = _obj(bundle[name], name)
        _keys(values, set(fields), name)
        for field, value in values.items():
            expected = name == "claims" and field == "api_config_evidence_generated"
            if type(value) is not bool or value is not expected:
                raise ValidationError(f"{name} disposition mismatch")
    digest = canonical_sha256(bundle)
    if EXPECTED_BUNDLE_SHA256 and digest != EXPECTED_BUNDLE_SHA256:
        raise ValidationError("bundle digest mismatch")
    return {"schema": SCHEMA, "version": VERSION, "candidate_id": CANDIDATE["candidate_id"], "eligible_operation_count": 8, "coverage_state": "complete_selectable", "selection_eligible": True, "operation_evidence_sha256": operation_digests, "bundle_sha256": digest, "execution_authorized": False, "selection_authorized": False, "score_credit_added": False}


def validate_api_evidence(evidence: dict[str, Any], bundle: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    bundle_result = validate_operation_bundle(bundle)
    result = acceptance.validate_evidence_structure(evidence, contract)
    if evidence["evidence_id"] != API_EVIDENCE_ID or result["candidate_id"] != CANDIDATE["candidate_id"] or result["coverage_state"] != "complete_selectable" or result["eligible_operation_count"] != 8 or result["selection_eligible"] is not True:
        raise ValidationError("OTCAPI0/v2 complete-primary disposition mismatch")
    expected = bundle_result["operation_evidence_sha256"]
    for operation in evidence["operation_results"]:
        if operation["state"] != "eligible" or operation["evidence_sha256"] != expected[operation["operation_id"]]:
            raise ValidationError("operation evidence digest mismatch")
    digest = canonical_sha256(evidence)
    if EXPECTED_API_EVIDENCE_SHA256 and digest != EXPECTED_API_EVIDENCE_SHA256:
        raise ValidationError("API/config evidence digest mismatch")
    return {**result, "operation_bundle_sha256": bundle_result["bundle_sha256"], "api_config_evidence_sha256": digest}


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


if __name__ == "__main__":
    raise SystemExit(main())
