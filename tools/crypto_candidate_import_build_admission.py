#!/usr/bin/env python3
"""Strict OT-120 retained candidate import/build contract and admission validator."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "tests/benchmarks/crypto"
CONTRACT = CRYPTO / "OT-120-OT005-CANDIDATE-IMPORT-BUILD-CONTRACT-V1.json"
ADMISSION = CRYPTO / "OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1.json"

CANDIDATE_SLUGS = (
    ("espressif_libsodium", "LIBSODIUM"),
    ("esp_idf_mbedtls_psa", "MBEDTLS-PSA"),
    ("monocypher", "MONOCYPHER"),
)
GRAPH_PATHS = {
    candidate_id: CRYPTO / f"OT-120-OT005-{slug}-IMPORT-BUILD-GRAPH-V1.jsonl"
    for candidate_id, slug in CANDIDATE_SLUGS
}
EVIDENCE_PATHS = {
    candidate_id: CRYPTO / f"OT-120-OT005-{slug}-IMPORT-BUILD-EVIDENCE-V1.json"
    for candidate_id, slug in CANDIDATE_SLUGS
}

# Immutable hashes for the accepted OT-120 Phase 1 contract and retained evidence.
EXPECTED_CONTRACT_RAW_SHA256: str | None = "ac0b3dd0e7f6fbd1fdb7edbf482ed301cfd2ce15a32c9b6f2e48bf1b8408df51"
EXPECTED_CONTRACT_SHA256: str | None = "bbbc9c93028affce509bc145ce2f3de44c0cc2a5934cc3804bd3526dee94a8ea"
EXPECTED_GRAPH_RAW_SHA256: dict[str, str | None] = {
    "espressif_libsodium": "c939dbc7afbc103a44c16d92474528acc782ca442873f06fa9a8a8b04aaec20c",
    "esp_idf_mbedtls_psa": "7338e2383f152d554d6a64e3d46e7260b0c9dba79099311ec7e140b1ccde7a55",
    "monocypher": "9e59ab26cca582301027ee3544bfd69643dfde5b8a84a2ae494cb98969ba9645",
}
EXPECTED_EVIDENCE_RAW_SHA256: dict[str, str | None] = {
    "espressif_libsodium": "735b4755f25da280cde7ba79387f5eeeee5a38bf477b60042e64d31f20f2186f",
    "esp_idf_mbedtls_psa": "e4cf79a47c6cd3a64f44412bf6b010815e7e66f3e7633242d2c2c89c61bf1307",
    "monocypher": "4ba0b05ab4ee043e506b8bc15d5140451b0e6b1b95fd5eb46bae26a595789633",
}
EXPECTED_EVIDENCE_SHA256: dict[str, str | None] = {
    "espressif_libsodium": "28c98e83cf2149177353f47346e8c37d263e8a436a10bff3b3f4cefe7608bd49",
    "esp_idf_mbedtls_psa": "5ed9d04e6d773be599e22bbccb3a8117850d99636dfc3a30adeefcc1f384866d",
    "monocypher": "390a94a0d256f4a8863c0d44363b788c7b8c9a91c4e94bebdf9356d8ca1a0c61",
}
EXPECTED_ADMISSION_RAW_SHA256: str | None = "90af31966553bee58fcf71e4decfee8d2bcadfee58ef026e3f96cffcd6f45ccf"
EXPECTED_ADMISSION_SHA256: str | None = "0c55f49803d833c075670b17fa8d033bd5a7cd4997e8714ff247161f7fa2057b"

MAX_JSON_BYTES = 524_288
MAX_GRAPH_BYTES = 524_288
MAX_DEPTH = 20
MAX_STRING = 2048
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
LOGICAL_PATH = re.compile(r"^[A-Za-z0-9_.+/-]{1,240}$")
PRIVATE_TEXT = re.compile(
    r"[A-Za-z]:\\|/(?:home|users)/|\\users\\|\bCOM[0-9]+\b|"
    r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b|"
    r"\b(?:pin|password|private[_ -]?key|secret|token)\s*[:=]",
    re.IGNORECASE,
)

OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
    "noise_xk_handshake",
)
ARTIFACT_ROLES = (
    "application",
    "application_elf",
    "linker_map",
    "bootloader",
    "partition_table",
    "generated_sdkconfig",
    "partition_csv",
)

PARENTS = (
    ("license_aware_source_lock_policy", "tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json", "d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427", "51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a"),
    ("libsodium_source_lock_admission", "tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0", "9f253738d1766a2d6d273ff5d566bb42c828e6db8cdc3a4b156068f55c07075d"),
    ("monocypher_source_lock_admission", "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52", "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52"),
    ("mbedtls_psa_source_lock_admission", "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json", "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85", "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85"),
    ("final_candidate_configuration_admission", "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json", "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2", "d94aa0dd96c13a628c5858f544c950f5c53903456ebe1687dee287f2d15abcf2"),
    ("api_configuration_acceptance_contract", "tests/benchmarks/crypto/OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json", "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3", "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22"),
    ("mbedtls_psa_api_configuration_admission", "tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json", "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0", "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd"),
    ("successor_readiness_review", "tests/benchmarks/crypto/OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json", "333f8d525160f45627a13913e5d1adabe8e5c8374290af32b9af1df96ef1bd7e", "ad10935a52bbbcb1ed06f523ef5084a8d70b91aca355d7b497e9ad54c18f453e"),
    ("phased_execution_plan", "tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json", "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a", "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8"),
    ("libsodium_api_configuration_admission", "tests/benchmarks/crypto/OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0.json", "527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2", "6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527"),
    ("monocypher_api_configuration_admission", "tests/benchmarks/crypto/OT-118-OT005-MONOCYPHER-API-CONFIG-ADMISSION-DELTA-V0.json", "9fbecf19b206b31fae948b6bc7e7aa4e206ba26aa59b94fb7f07d4e1d300810a", "df412285515fe29525b0bfd7cba45fd7ccd9a3d601be284242886e8adb19fec9"),
    ("phase_zero_current_state", "tests/benchmarks/crypto/OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1.json", "afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36", "0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443"),
)


class ValidationError(ValueError):
    """Artifact is malformed, private, mutated, or exceeds its authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        self.exit(2, "ERROR: invalid arguments\n")


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in items:
        if key in result:
            raise ValidationError("duplicate JSON key")
        result[key] = value
    return result


def canonical_sha256(value: Any) -> str:
    try:
        raw = json.dumps(
            value, ensure_ascii=True, allow_nan=False, sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValidationError("canonical serialization failed") from exc
    return hashlib.sha256(raw).hexdigest()


def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if not raw or len(raw) > MAX_JSON_BYTES:
            raise ValidationError("JSON size invalid")
        if expected_raw_sha256 is not None and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
            raise ValidationError("raw artifact digest mismatch")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as exc:
        raise ValidationError("JSON unreadable or invalid") from exc
    if type(value) is not dict:
        raise ValidationError("document must be an exact object")
    return value


def _scan(value: Any, depth: int = 0) -> None:
    if depth > MAX_DEPTH:
        raise ValidationError("document exceeds structural bounds")
    if type(value) is dict:
        if len(value) > 160:
            raise ValidationError("document exceeds structural bounds")
        for key, item in value.items():
            if type(key) is not str or len(key) > 128:
                raise ValidationError("document contains invalid key")
            _scan(item, depth + 1)
    elif type(value) is list:
        if len(value) > 256:
            raise ValidationError("document exceeds structural bounds")
        for item in value:
            _scan(item, depth + 1)
    elif type(value) is str:
        if len(value) > MAX_STRING or PRIVATE_TEXT.search(value):
            raise ValidationError("private or unbounded text")
    elif value is not None and type(value) not in (bool, int):
        raise ValidationError("noncanonical JSON type")


def _exact(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != expected:
        raise ValidationError(f"{label} keys mismatch")
    return value


def _sha(value: Any, label: str, length: int = 64) -> str:
    if type(value) is not str or not (HEX64 if length == 64 else HEX40).fullmatch(value):
        raise ValidationError(f"{label} digest invalid")
    return value

def _file_sha256(path: Path, label: str) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as exc:
        raise ValidationError(f"{label} unavailable") from exc


def _false_fields(value: Any, expected: set[str], label: str) -> None:
    record = _exact(value, expected, label)
    if any(type(record[field]) is not bool or record[field] for field in expected):
        raise ValidationError(f"{label} exceeds authority")


def _verify_parent_files(parents: list[dict[str, Any]]) -> None:
    if len(parents) != len(PARENTS):
        raise ValidationError("parent count mismatch")
    for actual, expected in zip(parents, PARENTS):
        role, logical_path, raw_digest, canonical_digest = expected
        expected_record = {
            "role": role,
            "path": logical_path,
            "raw_sha256": raw_digest,
            "canonical_sha256": canonical_digest,
        }
        if actual != expected_record:
            raise ValidationError("parent order or binding mismatch")
        path = ROOT / logical_path
        parent = load(path, raw_digest)
        if canonical_sha256(parent) != canonical_digest:
            raise ValidationError("parent canonical digest mismatch")


def validate_contract(contract: dict[str, Any], *, verify_parent_files: bool = True) -> dict[str, Any]:
    _scan(contract)
    _exact(contract, {
        "schema", "version", "artifact_kind", "contract_id", "accepted_date",
        "status", "public_result", "append_only_parents",
        "historical_non_admitting_reference", "target", "toolchain",
        "candidate_order", "candidates", "build_policy", "retention_policy",
        "admission_policy", "authority", "claims",
    }, "contract")
    if (
        contract["schema"] != "OTCIBC1"
        or contract["version"] != 1
        or contract["artifact_kind"] != "append_only_candidate_import_build_acceptance_contract"
        or contract["contract_id"] != "OT-120-OT005-CANDIDATE-IMPORT-BUILD-CONTRACT-V1"
        or contract["accepted_date"] != "2026-08-22"
        or contract["status"] != "phase_one_contract_frozen_host_only"
    ):
        raise ValidationError("contract identity mismatch")
    parents = contract["append_only_parents"]
    if type(parents) is not list:
        raise ValidationError("parent registry invalid")
    if verify_parent_files:
        _verify_parent_files(parents)
    elif parents != [
        {"role": role, "path": path, "raw_sha256": raw, "canonical_sha256": canonical}
        for role, path, raw, canonical in PARENTS
    ]:
        raise ValidationError("parent order or binding mismatch")

    if contract["historical_non_admitting_reference"] != {
        "path": "tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json",
        "raw_sha256": "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9",
        "canonical_sha256": "0d560714c234a49a913457fc7a577a175eb38206f1f79861323b269aa58b2f34",
        "admissible_for_phase_one": False,
        "reason": "generic_build_predates_final_configuration_and_did_not_retain_probe_symbols",
    }:
        raise ValidationError("historical OT-099 boundary mismatch")
    if contract["target"] != {
        "target_id": "heltec-v4-bench-candidate", "board_model": "HTIT-WB32LAF",
        "exact_received_revision": "V4.2", "mcu": "ESP32-S3",
        "processor_revision": "v0.2", "flash_bytes": 16777216,
        "psram_bytes": 2097152, "supported": False,
    }:
        raise ValidationError("target binding mismatch")
    if contract["toolchain"] != {
        "esp_idf_version": "v6.0.2", "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "compiler": "xtensa-esp32s3-elf-gcc", "compiler_version": "15.2.0",
        "reproducible_build": True, "compile_time_date_disabled": True,
        "cpu_frequency_mhz": 160, "freertos_tick_hz": 100,
    }:
        raise ValidationError("toolchain binding mismatch")
    order = [candidate_id for candidate_id, _ in CANDIDATE_SLUGS]
    if contract["candidate_order"] != order:
        raise ValidationError("candidate order mismatch")
    candidates = contract["candidates"]
    if type(candidates) is not list or len(candidates) != 3 or any(type(item) is not dict for item in candidates):
        raise ValidationError("candidate set must contain exactly three records")
    if [item.get("candidate_id") for item in candidates] != order:
        raise ValidationError("candidate order mismatch")
    if [item.get("role") for item in candidates] != ["primary", "comparison", "comparison"]:
        raise ValidationError("candidate role mismatch")
    expected_configs = (
        ("1.0.22", "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9", "d5f32dcb2ec24c853ac2040fb8f9410a8f61831b7b7d5e9522766e8456e2ec5e", "86e76598aa0a668239ae86e515b6e9a53fd03682e3a9b3325fceb19043780df2", "6e5e969c3b3f7bf29372e15e3cc75c693fb00f57f7f297c14cc77123dec4610d", 107001, "b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f", "complete_selectable", True),
        ("4.1.0", "ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49", "12f8699d8d286a484e054df186fb0e8c97b75263d23caf4bd77ed48082e9c7ab", "4fc3c63306582732b2628b962352286f256af83036c2c6d65d62f27e0702e9dd", "22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8", 106921, "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686", "comparison_partial", False),
        ("4.0.3", "fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f", "b78dd459464180518c97f8bd192edab6fb7e886634e30bc498c2f2f5ad0307ca", "cd82634f6512a3bff927111686aa389e8fb9885e8d53e21b90690b4509bf1154", "c1ce6c0de2a72852359fa15efd9e27d9ffd15171362ab6adc4d04f66825949e9", 106913, "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f", "comparison_partial", False),
    )
    eligible = (
        list(OPERATIONS),
        ["x25519", "sha256", "hkdf_sha256", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"],
        ["ed25519_sign", "ed25519_verify", "x25519", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"],
    )
    unavailable = (
        [],
        ["ed25519_sign", "ed25519_verify", "noise_xk_handshake"],
        ["sha256", "hkdf_sha256", "noise_xk_handshake"],
    )
    required_fields = {
        "candidate_id", "role", "version", "source_evidence_sha256",
        "source_admission_canonical_sha256", "project_dependency_lock_sha256",
        "source_manifest_sha256", "sbom_sha256",
        "api_config_evidence_canonical_sha256", "generated_sdkconfig_bytes",
        "generated_sdkconfig_sha256", "coverage_state", "eligible_operations",
        "unavailable_operations", "selection_eligible",
    }
    for index, candidate in enumerate(candidates):
        _exact(candidate, required_fields, f"candidates[{index}]")
        version, source, lock, sbom, api, config_bytes, config_sha, coverage, selection = expected_configs[index]
        if (
            candidate["version"], candidate["source_evidence_sha256"],
            candidate["project_dependency_lock_sha256"], candidate["sbom_sha256"],
            candidate["api_config_evidence_canonical_sha256"],
            candidate["generated_sdkconfig_bytes"], candidate["generated_sdkconfig_sha256"],
            candidate["coverage_state"], candidate["selection_eligible"],
        ) != (version, source, lock, sbom, api, config_bytes, config_sha, coverage, selection):
            raise ValidationError("candidate immutable binding mismatch")
        for field in ("source_admission_canonical_sha256", "source_manifest_sha256"):
            _sha(candidate[field], f"candidate.{field}")
        if candidate["eligible_operations"] != eligible[index] or candidate["unavailable_operations"] != unavailable[index]:
            raise ValidationError("candidate operation coverage mismatch")

    if contract["build_policy"] != {
        "candidate_count": 3, "clean_run_count_per_candidate": 2,
        "independent_build_directories": True, "initial_build_directories_absent": True,
        "component_manager_network_disabled": True, "shared_compiler_cache_disabled": True,
        "reproducible_paths_normalized": True, "build_exit_code": 0,
        "compiler_warning_count": 0, "candidate_graph_entry_required": True,
        "admitted_operation_anchors_retained": True,
        "unavailable_operation_anchors_prohibited": True,
        "two_run_artifact_tuples_equal": True,
        "two_run_normalized_receipts_equal": True,
    }:
        raise ValidationError("build policy mismatch")
    if contract["retention_policy"] != {
        "normalized_build_graph_manifest_required": True,
        "normalized_run_receipts_required": True,
        "raw_build_logs_public": False, "raw_build_log_sha256_required": True,
        "required_artifact_roles": list(ARTIFACT_ROLES),
        "exact_binary_sdkconfig_sbom_source_bindings_required": True,
        "ignored_build_directories_are_not_evidence": True,
    }:
        raise ValidationError("retention policy mismatch")
    if contract["admission_policy"] != {
        "prior_counts": {"source": 3, "api_config": 3, "candidate_import": 0},
        "accepted_counts": {"source": 3, "api_config": 3, "candidate_import": 3},
        "exact_profile_units": 2, "per_candidate_evidence_count": 1,
        "atomic_all_candidates_required": True, "partial_admission_prohibited": True,
        "phase_zero_complete": True, "phase_one_complete_after_admission": True,
        "measurement_ready": False,
        "measurement_blockers_after_admission": ["fresh_benchmark_execution_authority_absent"],
    }:
        raise ValidationError("admission policy mismatch")
    authority = contract["authority"]
    _exact(authority, {
        "contract_validation_authorized", "one_time_host_only_phase_one_authorized",
        "one_time_authority_consumed_by_admission_required",
        "continuing_candidate_import_authorized", "continuing_benchmark_build_authorized",
        "benchmark_execution_authorized", "device_access_authorized", "flash_authorized",
        "radio_transmit_authorized", "key_or_entropy_operation_authorized",
        "candidate_selection_authorized", "suite_selection_authorized",
        "packet_v1_authorized", "score_credit_added",
    }, "authority")
    true_authority = {
        "contract_validation_authorized",
        "one_time_host_only_phase_one_authorized",
        "one_time_authority_consumed_by_admission_required",
    }
    if any(authority[field] is not True for field in true_authority) or any(
        authority[field] is not False for field in authority if field not in true_authority
    ):
        raise ValidationError("contract exceeds authority")
    claims = contract["claims"]
    _exact(claims, {
        "contract_frozen", "candidate_imported", "candidate_benchmark_built",
        "benchmark_executed", "hardware_or_device_accessed", "candidate_selected",
        "suite_selected", "support_proven", "compatibility_proven",
        "regulatory_compliance_proven", "score_credit_added",
    }, "claims")
    if claims["contract_frozen"] is not True or any(
        claims[field] is not False for field in claims if field != "contract_frozen"
    ):
        raise ValidationError("contract claims exceed scope")
    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 is not None and digest != EXPECTED_CONTRACT_SHA256:
        raise ValidationError("contract canonical digest mismatch")
    return {
        "schema": "OTCIBC1", "version": 1, "candidate_count": 3,
        "prior_candidate_import_count": 0, "accepted_candidate_import_count": 3,
        "phase_zero_complete": True, "phase_one_contract_frozen": True,
        "one_time_phase_one_authorized": True, "measurement_ready": False,
        "contract_sha256": digest,
    }


def load_graph(path: Path, expected_raw_sha256: str | None = None) -> list[dict[str, Any]]:
    try:
        raw = path.read_bytes()
        if not raw or len(raw) > MAX_GRAPH_BYTES:
            raise ValidationError("graph size invalid")
        if expected_raw_sha256 is not None and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
            raise ValidationError("graph raw digest mismatch")
        text = raw.decode("utf-8")
        if "\r" in text or not text.endswith("\n"):
            raise ValidationError("graph must use canonical LF JSONL")
        records = [json.loads(line, object_pairs_hook=_pairs) for line in text.splitlines()]
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as exc:
        raise ValidationError("graph unreadable or invalid") from exc
    if not records or any(type(record) is not dict for record in records):
        raise ValidationError("graph must contain exact JSON objects")
    return records


def validate_graph(records: list[dict[str, Any]], candidate: dict[str, Any]) -> dict[str, Any]:
    _scan(records)
    if type(records) is not list or not records:
        raise ValidationError("graph must contain at least one record")
    header = _exact(records[0], {
        "schema", "version", "record_kind", "candidate_id", "entry_count",
        "source_evidence_sha256", "api_config_evidence_sha256",
        "generated_sdkconfig_sha256",
    }, "graph header")
    if header != {
        "schema": "OTCIBG1", "version": 1, "record_kind": "header",
        "candidate_id": candidate["candidate_id"], "entry_count": len(records) - 1,
        "source_evidence_sha256": candidate["source_evidence_sha256"],
        "api_config_evidence_sha256": candidate["api_config_evidence_canonical_sha256"],
        "generated_sdkconfig_sha256": candidate["generated_sdkconfig_sha256"],
    }:
        raise ValidationError("graph header binding mismatch")
    if len(records) < 2:
        raise ValidationError("graph contains no retained candidate entries")
    paths: list[str] = []
    candidate_entries = 0
    retained_anchors: set[str] = set()
    unavailable_anchors: set[str] = set()
    for index, raw in enumerate(records[1:], 1):
        record = _exact(raw, {
            "record_kind", "logical_path", "artifact_role", "bytes", "sha256",
            "retained_in_final_link", "operation_id",
        }, f"graph[{index}]")
        if record["record_kind"] != "build_graph_entry":
            raise ValidationError("graph record kind mismatch")
        if record["artifact_role"] not in {
            "candidate_source", "candidate_object", "candidate_archive",
            "benchmark_adapter", "application_elf",
        }:
            raise ValidationError("graph artifact role invalid")
        path = record["logical_path"]
        if type(path) is not str or not LOGICAL_PATH.fullmatch(path) or PRIVATE_TEXT.search(path):
            raise ValidationError("graph logical path invalid")
        if type(record["bytes"]) is not int or record["bytes"] < 0 or record["bytes"] > 134_217_728:
            raise ValidationError("graph entry byte count invalid")
        _sha(record["sha256"], "graph entry")
        if type(record["retained_in_final_link"]) is not bool:
            raise ValidationError("graph retained flag invalid")
        operation = record["operation_id"]
        if operation is not None and operation not in OPERATIONS:
            raise ValidationError("graph operation is not canonical")
        if record["artifact_role"] in {"candidate_source", "candidate_object", "candidate_archive"}:
            candidate_entries += 1
        if operation is not None and record["retained_in_final_link"]:
            retained_anchors.add(operation)
            if operation in candidate["unavailable_operations"]:
                unavailable_anchors.add(operation)
        paths.append(path)
    if paths != sorted(paths) or len(paths) != len(set(paths)):
        raise ValidationError("graph entries must be unique and sorted")
    if candidate_entries < 1:
        raise ValidationError("candidate source is absent from graph")
    if not set(candidate["eligible_operations"]).issubset(retained_anchors):
        raise ValidationError("admitted operation anchors are not retained")
    if unavailable_anchors:
        raise ValidationError("unavailable operation anchor is retained")
    return {
        "schema": "OTCIBG1", "version": 1,
        "candidate_id": candidate["candidate_id"], "entry_count": len(records) - 1,
        "eligible_anchors_retained": True, "unavailable_anchors_retained": False,
    }


def validate_evidence(
    evidence: dict[str, Any], graph_records: list[dict[str, Any]],
    contract: dict[str, Any], *, evidence_raw_sha256: str, graph_raw_sha256: str,
) -> dict[str, Any]:
    _scan(evidence)
    validate_contract(contract)
    _exact(evidence, {
        "schema", "version", "artifact_kind", "evidence_id", "recorded_date",
        "status", "public_result", "contract_raw_sha256", "contract_canonical_sha256",
        "candidate_id", "source_bindings", "target", "toolchain", "graph_binding",
        "build_reproducibility", "one_time_authority", "boundaries", "claims",
    }, "evidence")
    candidate_id = evidence["candidate_id"]
    if type(candidate_id) is not str:
        raise ValidationError("evidence candidate is not canonical")
    candidate_map = {item["candidate_id"]: item for item in contract["candidates"]}
    if candidate_id not in candidate_map:
        raise ValidationError("evidence candidate is not canonical")
    candidate = candidate_map[candidate_id]
    if (
        evidence["schema"] != "OTCIBE1" or evidence["version"] != 1
        or evidence["artifact_kind"] != "retained_candidate_import_build_evidence"
        or evidence["recorded_date"] != "2026-08-22"
        or evidence["status"] != "candidate_import_build_evidence_complete_pending_atomic_admission"
    ):
        raise ValidationError("evidence identity mismatch")
    slug = dict(CANDIDATE_SLUGS)[candidate_id]
    if evidence["evidence_id"] != f"OT-120-OT005-{slug}-IMPORT-BUILD-EVIDENCE-V1":
        raise ValidationError("evidence identifier mismatch")
    contract_raw = EXPECTED_CONTRACT_RAW_SHA256
    if contract_raw is None:
        raise ValidationError("contract raw digest is not frozen")
    if evidence["contract_raw_sha256"] != contract_raw or evidence["contract_canonical_sha256"] != canonical_sha256(contract):
        raise ValidationError("evidence contract binding mismatch")
    expected_source = {
        field: candidate[field] for field in (
            "version", "source_evidence_sha256", "source_admission_canonical_sha256",
            "project_dependency_lock_sha256", "source_manifest_sha256", "sbom_sha256",
            "api_config_evidence_canonical_sha256", "generated_sdkconfig_bytes",
            "generated_sdkconfig_sha256", "coverage_state", "eligible_operations",
            "unavailable_operations",
        )
    }
    if evidence["source_bindings"] != expected_source:
        raise ValidationError("evidence source/config binding mismatch")
    if evidence["target"] != contract["target"] or evidence["toolchain"] != contract["toolchain"]:
        raise ValidationError("evidence target/toolchain mismatch")
    graph_result = validate_graph(graph_records, candidate)
    if evidence["graph_binding"] != {
        "path": f"tests/benchmarks/crypto/OT-120-OT005-{slug}-IMPORT-BUILD-GRAPH-V1.jsonl",
        "raw_sha256": graph_raw_sha256,
        "entry_count": graph_result["entry_count"],
        "manifest_kind": "canonical-lf-jsonl-build-graph-v1",
    }:
        raise ValidationError("evidence graph binding mismatch")
    reproduction = _exact(evidence["build_reproducibility"], {
        "clean_run_count", "independent_build_directories", "shared_compiler_cache_disabled",
        "component_manager_network_disabled", "reproducible_paths_normalized", "runs",
    }, "build reproducibility")
    if reproduction["clean_run_count"] != 2 or any(
        reproduction[field] is not True for field in (
            "independent_build_directories", "shared_compiler_cache_disabled",
            "component_manager_network_disabled", "reproducible_paths_normalized",
        )
    ):
        raise ValidationError("build reproducibility policy mismatch")
    runs = reproduction["runs"]
    if type(runs) is not list or len(runs) != 2:
        raise ValidationError("exactly two build runs are required")
    artifact_sets = []
    normalized = []
    for index, run in enumerate(runs):
        _exact(run, {
            "profile", "initial_build_directory_absent", "build_exit_code",
            "compiler_warning_count", "raw_build_evidence_sha256",
            "normalized_receipt_sha256", "artifacts",
        }, f"runs[{index}]")
        expected_profile = f"ot120-{slug.lower()}-{'a' if index == 0 else 'b'}"
        if (
            run["profile"] != expected_profile
            or run["initial_build_directory_absent"] is not True
            or run["build_exit_code"] != 0 or run["compiler_warning_count"] != 0
        ):
            raise ValidationError("build run result mismatch")
        _sha(run["raw_build_evidence_sha256"], "raw build evidence")
        _sha(run["normalized_receipt_sha256"], "normalized receipt")
        artifacts = run["artifacts"]
        if (
            type(artifacts) is not list or len(artifacts) != len(ARTIFACT_ROLES)
            or any(type(item) is not dict for item in artifacts)
            or [item.get("role") for item in artifacts] != list(ARTIFACT_ROLES)
        ):
            raise ValidationError("ordered artifact roles mismatch")
        tuples = []
        for item in artifacts:
            _exact(item, {"role", "name", "bytes", "sha256"}, "artifact")
            if type(item["name"]) is not str or not LOGICAL_PATH.fullmatch(item["name"]):
                raise ValidationError("artifact name invalid")
            if type(item["bytes"]) is not int or item["bytes"] < 1 or item["bytes"] > 134_217_728:
                raise ValidationError("artifact byte count invalid")
            _sha(item["sha256"], "artifact")
            tuples.append((item["role"], item["name"], item["bytes"], item["sha256"]))
        sdkconfig = artifacts[5]
        if sdkconfig["bytes"] != candidate["generated_sdkconfig_bytes"] or sdkconfig["sha256"] != candidate["generated_sdkconfig_sha256"]:
            raise ValidationError("generated sdkconfig does not match accepted configuration")
        artifact_sets.append(tuples)
        normalized.append(run["normalized_receipt_sha256"])
    if artifact_sets[0] != artifact_sets[1]:
        raise ValidationError("two-run artifact or receipt equality mismatch")
    normalized_receipt_sha256 = canonical_sha256({
        "candidate_id": candidate_id,
        "source_bindings": evidence["source_bindings"],
        "target": evidence["target"],
        "toolchain": evidence["toolchain"],
        "graph_binding": evidence["graph_binding"],
        "artifacts": runs[0]["artifacts"],
    })
    if normalized[0] != normalized[1] or any(
        digest != normalized_receipt_sha256 for digest in normalized
    ):
        raise ValidationError("normalized receipt digest mismatch")
    if evidence["one_time_authority"] != {
        "host_only_phase_one_instruction_used": True, "consumed": True,
        "hardware_scope_included": False, "benchmark_execution_scope_included": False,
    }:
        raise ValidationError("evidence one-time authority mismatch")
    _false_fields(evidence["boundaries"], {
        "benchmark_executed", "device_accessed", "flashed", "radio_used",
        "key_or_entropy_operation", "candidate_selected", "suite_selected",
        "packet_v1_authorized", "score_credit_added",
    }, "boundaries")
    claims = _exact(evidence["claims"], {
        "candidate_imported_for_benchmark", "candidate_benchmark_built",
        "benchmark_executed", "hardware_or_device_accessed", "candidate_selected",
        "support_proven", "compatibility_proven", "regulatory_compliance_proven",
        "score_credit_added",
    }, "claims")
    if claims["candidate_imported_for_benchmark"] is not True or claims["candidate_benchmark_built"] is not True:
        raise ValidationError("evidence does not claim its bounded Phase 1 result")
    if any(claims[field] is not False for field in claims if field not in {"candidate_imported_for_benchmark", "candidate_benchmark_built"}):
        raise ValidationError("evidence claims exceed Phase 1")
    digest = canonical_sha256(evidence)
    expected = EXPECTED_EVIDENCE_SHA256[candidate_id]
    if expected is not None and digest != expected:
        raise ValidationError("evidence canonical digest mismatch")
    return {
        "schema": "OTCIBE1", "version": 1, "candidate_id": candidate_id,
        "candidate_imported": True, "candidate_benchmark_built": True,
        "benchmark_executed": False, "evidence_sha256": digest,
        "evidence_raw_sha256": evidence_raw_sha256,
        "graph_raw_sha256": graph_raw_sha256,
    }


def validate_admission(
    admission: dict[str, Any], contract: dict[str, Any],
    evidence_results: list[dict[str, Any]],
) -> dict[str, Any]:
    _scan(admission)
    validate_contract(contract)
    _exact(admission, {
        "schema", "version", "artifact_kind", "admission_id", "accepted_date",
        "status", "public_result", "parents", "accepted_candidate_imports",
        "acceptance_counts", "phases", "measurement_blockers", "one_time_authority",
        "continuing_authority", "claims",
    }, "admission")
    if (
        admission["schema"] != "OTCIBA1" or admission["version"] != 1
        or admission["artifact_kind"] != "append_only_atomic_candidate_import_build_admission"
        or admission["admission_id"] != "OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1"
        or admission["accepted_date"] != "2026-08-22"
        or admission["status"] != "phase_one_complete_measurement_awaits_fresh_authority"
    ):
        raise ValidationError("admission identity mismatch")
    if admission["parents"] != {
        "contract_raw_sha256": "ac0b3dd0e7f6fbd1fdb7edbf482ed301cfd2ce15a32c9b6f2e48bf1b8408df51",
        "contract_canonical_sha256": "bbbc9c93028affce509bc145ce2f3de44c0cc2a5934cc3804bd3526dee94a8ea",
        "otrtpa1_v1_raw_sha256": "afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36",
        "otrtpa1_v1_canonical_sha256": "0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443",
    }:
        raise ValidationError("admission parent binding mismatch")
    ordered = [result["candidate_id"] for result in evidence_results]
    if ordered != [candidate_id for candidate_id, _ in CANDIDATE_SLUGS] or len(set(ordered)) != 3:
        raise ValidationError("atomic evidence set mismatch")
    accepted = admission["accepted_candidate_imports"]
    if type(accepted) is not list or len(accepted) != 3:
        raise ValidationError("atomic admission requires three candidates")
    for item, result in zip(accepted, evidence_results):
        _exact(item, {"candidate_id", "evidence_raw_sha256", "evidence_canonical_sha256", "graph_raw_sha256"}, "accepted candidate")
        if (
            item["candidate_id"] != result["candidate_id"]
            or item["evidence_raw_sha256"] != result["evidence_raw_sha256"]
            or item["evidence_canonical_sha256"] != result["evidence_sha256"]
            or item["graph_raw_sha256"] != result["graph_raw_sha256"]
        ):
            raise ValidationError("accepted candidate evidence binding mismatch")
        for field in ("evidence_raw_sha256", "evidence_canonical_sha256", "graph_raw_sha256"):
            _sha(item[field], field)
    if admission["acceptance_counts"] != {
        "exact_profile_units": 2, "source": 3, "api_config": 3, "candidate_import": 3,
    }:
        raise ValidationError("acceptance count mismatch")
    if admission["phases"] != {
        "phase_zero_complete": True, "phase_one_complete": True,
        "phase_two_execution_admitted": False, "phase_three_admission_complete": False,
    }:
        raise ValidationError("phase state mismatch")
    if admission["measurement_blockers"] != ["fresh_benchmark_execution_authority_absent"]:
        raise ValidationError("measurement blocker mismatch")
    if admission["one_time_authority"] != {
        "host_only_phase_one_instruction_used": True, "consumed": True,
        "hardware_scope_included": False, "benchmark_execution_scope_included": False,
    }:
        raise ValidationError("one-time authority mismatch")
    _false_fields(admission["continuing_authority"], {
        "candidate_import_authorized", "benchmark_build_authorized",
        "benchmark_execution_authorized", "device_access_authorized", "flash_authorized",
        "radio_transmit_authorized", "key_or_entropy_operation_authorized",
        "candidate_selection_authorized", "suite_selection_authorized",
        "packet_v1_authorized", "score_credit_added",
    }, "continuing authority")
    claims = _exact(admission["claims"], {
        "candidate_imports_accepted", "candidate_benchmark_builds_accepted",
        "phase_one_complete", "measurement_ready", "benchmark_executed",
        "hardware_or_device_accessed", "candidate_selected", "suite_selected",
        "support_proven", "compatibility_proven", "regulatory_compliance_proven",
        "score_credit_added",
    }, "claims")
    for field in ("candidate_imports_accepted", "candidate_benchmark_builds_accepted", "phase_one_complete"):
        if claims[field] is not True:
            raise ValidationError("admission bounded claim missing")
    if any(claims[field] is not False for field in claims if field not in {"candidate_imports_accepted", "candidate_benchmark_builds_accepted", "phase_one_complete"}):
        raise ValidationError("admission claims exceed Phase 1")
    digest = canonical_sha256(admission)
    if EXPECTED_ADMISSION_SHA256 is not None and digest != EXPECTED_ADMISSION_SHA256:
        raise ValidationError("admission canonical digest mismatch")
    return {
        "schema": "OTCIBA1", "version": 1, "candidate_import_count": 3,
        "phase_zero_complete": True, "phase_one_complete": True,
        "measurement_ready": False, "execution_authorized": False,
        "selection_authorized": False, "score_credit_added": False,
        "admission_sha256": digest,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=CONTRACT)
    parser.add_argument("--admission", type=Path)
    parser.add_argument("--evidence", action="append", type=Path)
    parser.add_argument("--graph", action="append", type=Path)
    args = parser.parse_args(argv)
    try:
        contract = load(args.contract, EXPECTED_CONTRACT_RAW_SHA256)
        result: dict[str, Any] = validate_contract(contract)
        supplied = (args.admission is not None, args.evidence is not None, args.graph is not None)
        if any(supplied):
            if not all(supplied) or len(args.evidence) != 3 or len(args.graph) != 3:
                raise ValidationError("admission requires exactly three evidence and graph paths")
            evidence_results = []
            for (candidate_id, _), evidence_path, graph_path in zip(CANDIDATE_SLUGS, args.evidence, args.graph):
                expected_graph_raw = EXPECTED_GRAPH_RAW_SHA256[candidate_id]
                expected_evidence_raw = EXPECTED_EVIDENCE_RAW_SHA256[candidate_id]
                graph_records = load_graph(graph_path, expected_graph_raw)
                graph_raw = _file_sha256(graph_path, "graph")
                evidence = load(evidence_path, expected_evidence_raw)
                evidence_raw = _file_sha256(evidence_path, "evidence")
                evidence_results.append(validate_evidence(
                    evidence, graph_records, contract,
                    evidence_raw_sha256=evidence_raw, graph_raw_sha256=graph_raw,
                ))
            admission = load(args.admission, EXPECTED_ADMISSION_RAW_SHA256)
            result = validate_admission(admission, contract, evidence_results)
        print(json.dumps(result, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
        return 0
    except ValidationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    except (KeyError, AttributeError, TypeError, IndexError) as exc:
        del exc
        print("ERROR: malformed artifact structure", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
