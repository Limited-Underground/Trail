#!/usr/bin/env python3
"""Fail-closed OT-116 successor-readiness and phased-plan validator."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
READINESS = CRYPTO / "OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json"
CONTRACT = CRYPTO / "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json"
PINS = {
    "readiness": ("333f8d525160f45627a13913e5d1adabe8e5c8374290af32b9af1df96ef1bd7e", "ad10935a52bbbcb1ed06f523ef5084a8d70b91aca355d7b497e9ad54c18f453e"),
    "contract": ("0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a", "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8"),
}
MAX_BYTES = 131072
MAX_DEPTH = 18
MAX_ITEMS = 4096
PRIVATE = re.compile(
    r"(?:[A-Za-z]:\\|/(?:Users|home)/|COM\d+|tty(?:USB|ACM)\d+|"
    r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}|"
    r"(?:pin|password|private[_ -]?key|secret)\s*[:=])",
    re.I,
)
OPERATIONS = [
    "ed25519_sign", "ed25519_verify", "x25519", "sha256", "hkdf_sha256",
    "chacha20poly1305_encrypt", "chacha20poly1305_decrypt", "noise_xk_handshake",
]
BLOCKERS = [
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "espressif_libsodium_source_lock_absent",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "monocypher_source_lock_absent",
    "direct_radio_mtu_phy_region_unresolved",
]
PARENTS = [
    ("preselection_build_baseline", "tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json", "240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83", "16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733"),
    ("historical_six_requirement_ledger", "tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json", "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae", "705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3"),
    ("libsodium_source_lock_admission", "tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0", "9f253738d1766a2d6d273ff5d566bb42c828e6db8cdc3a4b156068f55c07075d"),
    ("monocypher_source_lock_admission", "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52", "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52"),
    ("exact_received_target_profile_admission", "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json", "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105", "dc7247ae9b277418c104690193fa7bfce2d9297038d2c24c2f5daedf4dc3331e"),
    ("mbedtls_psa_source_lock_admission", "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85", "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85"),
    ("final_per_candidate_configuration_admission", "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json", "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2", "d94aa0dd96c13a628c5858f544c950f5c53903456ebe1687dee287f2d15abcf2"),
    ("per_candidate_api_config_acceptance_contract", "tests/benchmarks/crypto/OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json", "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3", "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22"),
    ("mbedtls_psa_partial_api_config_admission", "tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json", "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0", "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd"),
    ("direct_radio_execution_contract", "tests/benchmarks/crypto/OT-113-OT005-US915-DIRECT-RADIO-PROFILE-EXECUTION-CONTRACT-V1.json", "c59fd52f8c1608f7e7dfdb5c166504bb7ad7fb02c6e82b3d3d0677c79cd2c87c", "d73ebf7340c4351b5daa775d7cb9342f6650baf376f1c45944049d7efe49462c"),
    ("direct_radio_execution_receipt", "tests/hardware/OT-114-2026-08-21-EXECUTION-RECEIPT-V0.json", "d285d43ec30b2d81473b37bf189b14d89db389cb1636cacbe59cf9f84825d1dd", "1700446be2216f6520859928e941a72a06605dfdeedad6957b6e4f8d5259e8c4"),
    ("direct_radio_profile_evidence", "tests/benchmarks/crypto/OT-114-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-V1.json", "b6d2a7ce4ebe3ab233ebbc748ab7831ff12cf4d8f6504d2d7e23dae108bd5876", "ac7e77a4438772a4c5b5f2b17472b302a3520e186a21b88125a9314ee6998bf0"),
    ("direct_radio_profile_admission", "tests/benchmarks/crypto/OT-114-OT005-US915-DIRECT-RADIO-PROFILE-ADMISSION-DELTA-V1.json", "19325f730b96b9dbeeb4f64682c4913e7586d1995ef419b26408d82be12ef266", "eecf2b821ef2c25274cc5d3a179494b1545eb4a859280a48459ebb83c79ed257"),
]


class ValidationError(ValueError):
    pass


class SafeParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ValidationError("invalid arguments")


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate key")
        result[key] = value
    return result


def _scan(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise ValidationError("structure too deep")
    if isinstance(value, str):
        if len(value) > 4096 or PRIVATE.search(value):
            raise ValidationError("unsafe text")
        return 1
    if value is None or type(value) in (bool, int):
        return 1
    if isinstance(value, list):
        count = 1 + sum(_scan(item, depth + 1) for item in value)
    elif isinstance(value, dict):
        count = 1 + sum(_scan(key, depth + 1) + _scan(item, depth + 1) for key, item in value.items())
    else:
        raise ValidationError("unsupported value")
    if count > MAX_ITEMS:
        raise ValidationError("structure too large")
    return count


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()



def canonical_parent_sha256(path: Path) -> str:
    """Hash an immutable pinned parent without imposing OT-116 value types."""
    raw = path.read_bytes()
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError("invalid parent JSON") from exc
    if not isinstance(value, dict):
        raise ValidationError("parent root must be object")
    return canonical_sha256(value)


def load(path: Path | str, expected: tuple[str, str] | None = None) -> dict[str, Any]:
    raw = Path(path).read_bytes()
    if len(raw) > MAX_BYTES:
        raise ValidationError("file too large")
    if expected is not None and expected[0] != "TO_BE_PINNED" and hashlib.sha256(raw).hexdigest() != expected[0]:
        raise ValidationError("raw digest mismatch")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError("invalid JSON") from exc
    if not isinstance(value, dict):
        raise ValidationError("root must be object")
    _scan(value)
    if expected is not None and expected[1] != "TO_BE_PINNED" and canonical_sha256(value) != expected[1]:
        raise ValidationError("canonical digest mismatch")
    return value


def _need(condition: bool) -> None:
    if not condition:
        raise ValidationError("invariant mismatch")


def validate_readiness(value: dict[str, Any], verify_parent_files: bool = True) -> dict[str, Any]:
    _scan(value)
    _need(set(value) == {"schema", "version", "artifact_kind", "readiness_id", "accepted_date", "status", "public_result", "append_only_parents", "historical_six_requirements", "closure_map", "current_requirements", "acceptance_counts", "candidate_dispositions", "successor_boundary", "authority", "claims"})
    _need(value["schema"] == "OTCBR1" and type(value["version"]) is int and value["version"] == 0)
    _need(value["artifact_kind"] == "append_only_successor_candidate_readiness_review")
    _need(value["readiness_id"] == "OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1")
    _need(value["accepted_date"] == "2026-08-22")
    _need(value["status"] == "requirements_closed_execution_blocked")
    expected_parents = [dict(role=role, path=path, raw_sha256=raw, canonical_sha256=canonical) for role, path, raw, canonical in PARENTS]
    _need(value["append_only_parents"] == expected_parents)
    if verify_parent_files:
        for _, path, raw, canonical in PARENTS:
            parent = ROOT / path
            _need(hashlib.sha256(parent.read_bytes()).hexdigest() == raw)
            _need(canonical_parent_sha256(parent) == canonical)
    _need(value["historical_six_requirements"] == BLOCKERS)
    _need(value["closure_map"] == [
        {"blocker_id": BLOCKERS[0], "parent_role": "exact_received_target_profile_admission"},
        {"blocker_id": BLOCKERS[1], "parent_role": "final_per_candidate_configuration_admission"},
        {"blocker_id": BLOCKERS[2], "parent_role": "libsodium_source_lock_admission"},
        {"blocker_id": BLOCKERS[3], "parent_role": "mbedtls_psa_partial_api_config_admission", "supporting_parent_role": "mbedtls_psa_source_lock_admission"},
        {"blocker_id": BLOCKERS[4], "parent_role": "monocypher_source_lock_admission"},
        {"blocker_id": BLOCKERS[5], "parent_role": "direct_radio_profile_admission"},
    ])
    _need(value["current_requirements"] == [])
    _need(value["acceptance_counts"] == {"source": 3, "api_config": 1, "candidate_import": 0})
    dispositions = value["candidate_dispositions"]
    _need([item["candidate_id"] for item in dispositions] == ["espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"])
    _need([item["role"] for item in dispositions] == ["primary", "comparison", "comparison"])
    _need(all(item["source_lock_accepted"] is True and item["final_configuration_accepted"] is True for item in dispositions))
    _need(dispositions[1]["api_config_admission"] == "accepted_comparison_partial_5_of_8")
    _need([item["api_config_admission"] for item in dispositions] == ["required_before_measurement", "accepted_comparison_partial_5_of_8", "required_before_measurement"])
    _need(all(item["candidate_import_admission"] == "required_before_measurement" for item in dispositions))
    _need(all(item["measurement_ready"] is False and item["selection_eligible"] is False for item in dispositions))
    boundary = value["successor_boundary"]
    _need(boundary == {"historical_otcbr0_v0_mutated": False, "historical_plan_promoted": False, "readiness_requirements_closed": True, "measurement_readiness_admitted": False, "readiness_advanced": False, "new_phased_contract_required": True, "independent_api_config_and_import_admissions_required": True, "independent_second_node_exact_profile_admission_required": True})
    _need(all(item is False for item in value["authority"].values()))
    claims = value["claims"]
    _need(claims["historical_requirements_closed"] is True)
    _need(all(claims[key] is False for key in claims if key != "historical_requirements_closed"))
    return {"schema": "OTCBR1", "status": value["status"], "requirements_closed": True, "measurement_ready": False, "readiness_advanced": False, "canonical_sha256": canonical_sha256(value)}


def validate_contract(value: dict[str, Any], readiness: dict[str, Any]) -> dict[str, Any]:
    _scan(value)
    _need(set(value) == {"schema", "version", "artifact_kind", "contract_id", "accepted_date", "status", "public_result", "readiness_review", "target", "toolchain", "radio_profile", "operations", "candidates", "ordered_phases", "required_gates", "result_contract", "authority", "claims"})
    _need(value["schema"] == "OTCBX1" and type(value["version"]) is int and value["version"] == 1)
    _need(value["artifact_kind"] == "phased_executable_benchmark_procedure_contract")
    _need(value["contract_id"] == "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1")
    _need(value["accepted_date"] == "2026-08-22" and value["status"] == "frozen_executable_contract")
    binding = value["readiness_review"]
    _need(binding == {"path": "tests/benchmarks/crypto/OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json", "raw_sha256": hashlib.sha256(READINESS.read_bytes()).hexdigest(), "canonical_sha256": canonical_sha256(readiness), "status": "requirements_closed_execution_blocked", "readiness_advanced": False})
    _need(value["target"] == {"target_id": "heltec-v4-bench-candidate", "accepted_profile_unit": "OT-DEV-001", "manufacturer": "Heltec Automation", "board_model": "HTIT-WB32LAF", "exact_received_revision": "V4.2", "mcu": "ESP32-S3", "processor_revision": "v0.2", "flash_bytes": 16777216, "psram_bytes": 2097152, "supported": False, "physical_nodes_required_for_measurement": 2, "second_node_exact_profile_admitted": False})
    _need(value["toolchain"] == {"esp_idf_version": "v6.0.2", "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e", "compiler": "xtensa-esp32s3-elf-gcc", "compiler_version": "15.2.0", "reproducible_build": True, "compile_time_date_disabled": True, "cpu_frequency_mhz": 160, "freertos_tick_hz": 100})
    _need(value["radio_profile"] == {"region_code": "US915", "frequency_hz": 915000000, "bandwidth_hz": 125000, "spreading_factor": 7, "coding_rate": "4/5", "explicit_header": True, "crc_enabled": True, "low_data_rate_optimization": False, "sync_word": "0x12", "preamble_symbols": 8, "tx_power_command_setpoint_dbm": 2, "benchmark_total_wire_bytes": 163, "admitted_direct_total_wire_ceiling_bytes": 255, "first_locally_rejected_total_wire_bytes": 256})
    _need(value["operations"] == OPERATIONS)
    candidates = value["candidates"]
    _need([item["candidate_id"] for item in candidates] == ["espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"])
    _need([item["role"] for item in candidates] == ["primary", "comparison", "comparison"])
    _need([item["generated_sdkconfig_sha256"] for item in candidates] == ["b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f", "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686", "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"])
    _need([item["version"] for item in candidates] == ["1.0.22", "4.1.0", "4.0.3"])
    _need([item["source_commit"] for item in candidates] == ["77e1ce5d6dee871c49ef211222ba18ef0c486bda", "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5", "ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f"])
    _need([item["dependency_lock_sha256"] for item in candidates] == ["d5f32dcb2ec24c853ac2040fb8f9410a8f61831b7b7d5e9522766e8456e2ec5e", "12f8699d8d286a484e054df186fb0e8c97b75263d23caf4bd77ed48082e9c7ab", "b78dd459464180518c97f8bd192edab6fb7e886634e30bc498c2f2f5ad0307ca"])
    _need(all(item["import_state"] == "not_admitted" and item["selection_eligible"] is False for item in candidates))
    _need([item["api_config_state"] for item in candidates] == ["not_admitted", "accepted_comparison_partial_5_of_8", "not_admitted"])
    partial = candidates[1]
    _need(partial["api_config_state"] == "accepted_comparison_partial_5_of_8")
    _need(partial["eligible_operations"] == OPERATIONS[2:7])
    _need(partial["unavailable_operations"] == OPERATIONS[0:2] + OPERATIONS[7:8])
    phases = value["ordered_phases"]
    _need([phase["phase"] for phase in phases] == [0, 1, 2, 3])
    _need([phase["name"] for phase in phases] == ["api_configuration_admission", "candidate_import_and_build_admission", "two_node_target_measurement", "independent_result_admission"])
    _need(phases[0]["hardware_access"] is False and phases[1]["hardware_access"] is False)
    _need(phases[0]["required_before_next_phase"] == ["independent_libsodium_api_configuration_admission", "independent_second_node_exact_profile_admission", "independent_monocypher_api_configuration_admission", "preserve_mbedtls_psa_five_of_eight_partial_nonselectable_state"])
    _need(phases[1]["required_before_next_phase"] == ["independent_libsodium_import_and_build_admission", "independent_mbedtls_psa_import_and_build_admission", "independent_monocypher_import_and_build_admission", "exact_binary_sdkconfig_sbom_and_source_bindings", "zero_compiler_warnings"])
    _need(phases[2]["hardware_access"] is True and phases[2]["requires_separate_authority"] is True)
    _need(phases[2]["minimum_repetitions_per_admitted_operation"] == {"cold": 100, "warm": 100})
    _need(phases[2]["unavailable_operations_must_have_no_measurements"] is True)
    _need(value["required_gates"] == ["primitive_vectors_and_negative_cases", "noise_xk_independent_interoperability", "invitation_replay_reorder_timeout_refusal", "entropy_and_cold_start_uniqueness", "temporary_secret_wipe_and_log_redaction", "rollback_safe_counter_interruption", "two_device_join_revoke_reset_recovery", "license_sbom_and_reproducible_lock"])
    _need(value["result_contract"] == {"future_schema": "OTCBXR1", "independent_admission_required": True, "raw_traces_retained_privately": True, "public_device_identifiers_prohibited": True, "candidate_pass_does_not_select": True, "partial_candidate_cannot_pass_selection_gate": True})
    _need(value["authority"]["contract_validation_authorized"] is True)
    _need(all(value["authority"][key] is False for key in value["authority"] if key != "contract_validation_authorized"))
    claims = value["claims"]
    _need(claims["contract_frozen"] is True and claims["historical_requirements_closed"] is True)
    _need(all(claims[key] is False for key in claims if key not in {"contract_frozen", "historical_requirements_closed"}))
    return {"schema": "OTCBX1", "status": value["status"], "execution_authorized": False, "measurement_ready": False, "phase_count": 4, "canonical_sha256": canonical_sha256(value)}


def main(argv: list[str] | None = None) -> int:
    parser = SafeParser()
    parser.add_argument("--readiness", type=Path, default=READINESS)
    parser.add_argument("--contract", type=Path, default=CONTRACT)
    try:
        args = parser.parse_args(argv)
        readiness_pin = PINS["readiness"] if args.readiness.resolve() == READINESS.resolve() else None
        contract_pin = PINS["contract"] if args.contract.resolve() == CONTRACT.resolve() else None
        readiness = load(args.readiness, readiness_pin)
        readiness_result = validate_readiness(readiness)
        contract = load(args.contract, contract_pin)
        contract_result = validate_contract(contract, readiness)
    except (ValidationError, OSError, KeyError, TypeError, ValueError):
        print("ERROR: validation failed", file=sys.stderr)
        return 2
    print(json.dumps({"readiness_schema": readiness_result["schema"], "readiness_status": readiness_result["status"], "contract_schema": contract_result["schema"], "contract_status": contract_result["status"], "measurement_ready": False, "execution_authorized": False, "readiness_raw_sha256": hashlib.sha256(args.readiness.read_bytes()).hexdigest(), "contract_raw_sha256": hashlib.sha256(args.contract.read_bytes()).hexdigest()}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
