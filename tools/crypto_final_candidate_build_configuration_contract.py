#!/usr/bin/env python3
"""Exact field contract for the immutable OT-107 configuration evidence."""

from __future__ import annotations

from typing import Any


EVIDENCE_ID = "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0"
PROPOSAL_SHA256 = "f9072a602a9c139b1e7728735db04cc270720bc37e0429c22bcdb0cd56202a15"
PUBLIC_RESULT = (
    "FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-GENERATED-HOST-ONLY; "
    "TWO-REPRODUCIBLE-CONFIGURATION-ONLY-RUNS-PER-CANDIDATE; "
    "THREE-OTCBR0-REQUIREMENTS-REMAIN-PENDING-ADMISSION; "
    "NO-SOURCE-COPY-IMPORT-COMPILE-BENCHMARK-DEVICE-RADIO-OR-SELECTION; "
    "OTCBR0-READINESS-BLOCKED"
)
ROOT_KEYS = {
    "schema", "version", "artifact_kind", "evidence_id", "recorded_date",
    "status", "public_result", "parents", "target", "toolchain",
    "common_configuration", "candidates", "reproducibility",
    "acceptance_counts", "authority", "claims",
}
CANDIDATE_KEYS = {
    "candidate_id", "role", "kconfig_settings", "overlay_lf_sha256",
    "source_requirements", "source_requirement_sha256", "compile_definitions",
    "extra_component_path", "generation_receipts",
}
RECEIPT_KEYS = {
    "run", "isolated_root_initially_absent", "configuration_only",
    "component_manager_disabled", "candidate_source_copied",
    "candidate_compiled", "benchmark_executed", "device_accessed",
    "radio_used", "exit_code", "generated_sdkconfig_bytes",
    "generated_sdkconfig_sha256", "required_effective_symbols",
    "forbidden_effective_symbols",
}
COMMON_SYMBOLS = [
    'CONFIG_IDF_TARGET="esp32s3"',
    "CONFIG_APP_REPRODUCIBLE_BUILD=y",
    "CONFIG_APP_COMPILE_TIME_DATE=n",
    "CONFIG_COMPILER_OPTIMIZATION_SIZE=y",
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y",
    "CONFIG_FREERTOS_HZ=100",
]
CANDIDATES = (
    {
        "candidate_id": "espressif_libsodium",
        "role": "primary",
        "bytes": 107001,
        "sha256": "b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f",
        "source_requirement_sha256": [],
        "extra_component_path": "tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium",
        "required": COMMON_SYMBOLS + ["CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n"],
        "forbidden": ["CONFIG_APP_COMPILE_TIME_DATE=y", "CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=y"],
    },
    {
        "candidate_id": "esp_idf_mbedtls_psa",
        "role": "comparison",
        "bytes": 106921,
        "sha256": "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686",
        "source_requirement_sha256": [],
        "extra_component_path": None,
        "required": COMMON_SYMBOLS + ["CONFIG_MBEDTLS_CHACHA20_C=y", "CONFIG_MBEDTLS_CHACHAPOLY_C=y"],
        "forbidden": ["CONFIG_APP_COMPILE_TIME_DATE=y", "CONFIG_MBEDTLS_CHACHA20_C=n", "CONFIG_MBEDTLS_CHACHAPOLY_C=n"],
    },
    {
        "candidate_id": "monocypher",
        "role": "comparison",
        "bytes": 106913,
        "sha256": "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f",
        "source_requirement_sha256": [
            "f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123",
            "ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453",
        ],
        "extra_component_path": None,
        "required": COMMON_SYMBOLS,
        "forbidden": ["CONFIG_APP_COMPILE_TIME_DATE=y"],
    },
)


def _fail(error_type: type[ValueError], label: str) -> None:
    raise error_type(label)


def _exact(value: Any, expected: Any, error_type: type[ValueError], label: str) -> None:
    if type(value) is not type(expected):
        _fail(error_type, label)
    if type(expected) is dict:
        if set(value) != set(expected):
            _fail(error_type, label)
        for key in expected:
            _exact(value[key], expected[key], error_type, label)
    elif type(expected) is list:
        if len(value) != len(expected):
            _fail(error_type, label)
        for actual, wanted in zip(value, expected, strict=True):
            _exact(actual, wanted, error_type, label)
    elif value != expected:
        _fail(error_type, label)


def validate_evidence_contract(
    value: dict[str, Any], proposal: dict[str, Any], error_type: type[ValueError]
) -> None:
    if set(value) != ROOT_KEYS:
        _fail(error_type, "configuration evidence fields")
    _exact(
        [
            value["schema"], value["version"], value["artifact_kind"],
            value["evidence_id"], value["recorded_date"], value["status"],
            value["public_result"],
        ],
        [
            "OTCBCGE0", 0, "final_candidate_build_configuration_generation_evidence",
            EVIDENCE_ID, "2026-08-21",
            "candidate_build_configurations_generated_host_only_pending_admission",
            PUBLIC_RESULT,
        ],
        error_type,
        "configuration evidence identity",
    )
    _exact(value["parents"], {"proposal_raw_sha256": PROPOSAL_SHA256}, error_type, "configuration evidence parent")
    _exact(value["target"], proposal["target"], error_type, "configuration evidence target")
    _exact(value["toolchain"], proposal["toolchain"], error_type, "configuration evidence toolchain")
    expected_common = {
        key: proposal["proposed_common_configuration"][key]
        for key in (
            "tracked_target_defaults_path", "tracked_target_defaults_sha256",
            "reproducible_defaults_sha256", "decision_settings",
            "decision_settings_lf_sha256",
        )
    }
    _exact(value["common_configuration"], expected_common, error_type, "configuration evidence common settings")
    if type(value["candidates"]) is not list or len(value["candidates"]) != 3:
        _fail(error_type, "configuration evidence candidates")
    for candidate, expected, proposed in zip(
        value["candidates"], CANDIDATES, proposal["candidate_overlays"], strict=True
    ):
        if type(candidate) is not dict or set(candidate) != CANDIDATE_KEYS:
            _fail(error_type, "configuration evidence candidate fields")
        for key in ("candidate_id", "role"):
            _exact(candidate[key], expected[key], error_type, "configuration evidence candidate identity")
        for key in ("kconfig_settings", "overlay_lf_sha256", "source_requirements", "compile_definitions"):
            _exact(candidate[key], proposed[key], error_type, "configuration evidence proposal binding")
        _exact(candidate["source_requirement_sha256"], expected["source_requirement_sha256"], error_type, "configuration evidence source hashes")
        _exact(candidate["extra_component_path"], expected["extra_component_path"], error_type, "configuration evidence component path")
        receipts = candidate["generation_receipts"]
        if type(receipts) is not list or len(receipts) != 2:
            _fail(error_type, "configuration evidence receipts")
        for receipt, run in zip(receipts, ("A", "B"), strict=True):
            if type(receipt) is not dict or set(receipt) != RECEIPT_KEYS:
                _fail(error_type, "configuration receipt fields")
            expected_receipt = {
                "run": run,
                "isolated_root_initially_absent": True,
                "configuration_only": True,
                "component_manager_disabled": True,
                "candidate_source_copied": False,
                "candidate_compiled": False,
                "benchmark_executed": False,
                "device_accessed": False,
                "radio_used": False,
                "exit_code": 0,
                "generated_sdkconfig_bytes": expected["bytes"],
                "generated_sdkconfig_sha256": expected["sha256"],
                "required_effective_symbols": expected["required"],
                "forbidden_effective_symbols": expected["forbidden"],
            }
            _exact(receipt, expected_receipt, error_type, "configuration receipt")
    _exact(
        value["reproducibility"],
        {"runs_per_candidate": 2, "all_candidate_pairs_equal": True, "isolated_roots_removed": True},
        error_type,
        "configuration reproducibility",
    )
    _exact(value["acceptance_counts"], {"source": 3, "api_config": 0, "candidate_import": 0}, error_type, "configuration counts")
    _exact(
        value["authority"],
        {
            "final_candidate_configuration_authorized": False,
            "candidate_import_authorized": False,
            "benchmark_build_authorized": False,
            "benchmark_execution_authorized": False,
            "device_access_authorized": False,
            "radio_transmit_authorized": False,
            "key_or_entropy_operation_authorized": False,
            "suite_selection_authorized": False,
            "packet_v1_authorized": False,
            "score_credit_added": False,
        },
        error_type,
        "configuration evidence authority",
    )
    _exact(
        value["claims"],
        {
            "final_candidate_configuration_evidence_generated": True,
            "final_candidate_configuration_proven": False,
            "source_acquired_or_copied": False,
            "candidate_imported": False,
            "candidate_compiled": False,
            "benchmark_executed": False,
            "hardware_or_device_accessed": False,
            "radio_used": False,
            "key_or_entropy_operation": False,
            "candidate_selected": False,
            "readiness_accepted": False,
            "score_credit_added": False,
        },
        error_type,
        "configuration evidence claims",
    )
