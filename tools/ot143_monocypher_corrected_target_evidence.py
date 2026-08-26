#!/usr/bin/env python3
"""Validate the accepted OT-143 host-only corrected-target build evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EVIDENCE = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json"
)
SCHEMA = "OT143CTB0"
EXPECTED_ARTIFACTS = (
    (
        "application_bin",
        "ot142_monocypher_corrected_bench.bin",
        149824,
        "8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034",
    ),
    (
        "application_elf",
        "ot142_monocypher_corrected_bench.elf",
        3314668,
        "ec74f80422a5b9722e09342d69e190282a4117b4888c96838cf915dc760466d1",
    ),
    (
        "linker_map",
        "ot142_monocypher_corrected_bench.map",
        2849996,
        "c9a2789451305417dc2cb523ba6284a9c4ca8014724d051f829b4f5a7b31af13",
    ),
    (
        "bootloader_bin",
        "bootloader.bin",
        15216,
        "604af9d70953d917734f45b4c1cb764a23c17c8e3e5b28e11e1f3f6a02ef1c38",
    ),
    (
        "partition_table_bin",
        "partition-table.bin",
        3072,
        "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab",
    ),
    (
        "generated_sdkconfig",
        "sdkconfig",
        57516,
        "5807fe7fc6d4ef3325f06099674f07080660eabd20e0b078225247605c814817",
    ),
)
EXPECTED_REQUIRED_LINES = (
    "CONFIG_ESP_CONSOLE_NONE=y",
    "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",
    "CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y",
    "CONFIG_BOOTLOADER_LOG_LEVEL=0",
    "CONFIG_LOG_DEFAULT_LEVEL_NONE=y",
    "CONFIG_LOG_DEFAULT_LEVEL=0",
    "CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y",
    "CONFIG_LOG_MAXIMUM_LEVEL=0",
    "CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y",
    "CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y",
    "CONFIG_APP_REPRODUCIBLE_BUILD=y",
    "CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y",
    "# CONFIG_ESP_CONSOLE_USB_CDC is not set",
    "# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set",
    "# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set",
    "# CONFIG_ESP_CONSOLE_UART_CUSTOM is not set",
    "# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set",
    "CONFIG_ESP_CONSOLE_UART_NONE=y",
)
EXPECTED_FORBIDDEN_LINES = (
    "CONFIG_ESP_CONSOLE_USB_CDC=y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
    "CONFIG_ESP_CONSOLE_UART_DEFAULT=y",
    "CONFIG_ESP_CONSOLE_UART_CUSTOM=y",
    "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y",
    "CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y",
    "CONFIG_LOG_DEFAULT_LEVEL_INFO=y",
    "CONFIG_BOOT_ROM_LOG_ALWAYS_OFF=y",
    "CONFIG_BOOT_ROM_LOG_ON_GPIO_HIGH=y",
    "CONFIG_BOOT_ROM_LOG_ON_GPIO_LOW=y",
    "CONFIG_BT_ENABLED=y",
    "CONFIG_ESP_WIFI_ENABLED=y",

)

EXPECTED_SOURCE_INPUTS = (
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/partitions.csv",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/sdkconfig.defaults",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected/main/app_main.c",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h",
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h",
    "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c",
    "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.h",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.h",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.h",
)
CANONICAL_LF_INPUTS = frozenset(
    {
        EXPECTED_SOURCE_INPUTS[0],
        EXPECTED_SOURCE_INPUTS[2],
        EXPECTED_SOURCE_INPUTS[3],
        EXPECTED_SOURCE_INPUTS[4],
    }
)
CANONICAL_CORRECTED_INPUTS = {
    EXPECTED_SOURCE_INPUTS[0]: (238, "a01b5298c3bbc7bbbbb72c381b991122e8aa55873e2f27f88d3ad1db45fe8c94"),
    EXPECTED_SOURCE_INPUTS[1]: (452, "4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389"),
    EXPECTED_SOURCE_INPUTS[2]: (997, "f8d20cdc61ba606e47ba76049b7be97d959441abea691deae3b85cba7fd2e404"),
    EXPECTED_SOURCE_INPUTS[3]: (877, "47bd2006a465ae18b23119314bb2385fcb47cea144f0b40191a9eaaf7b35b029"),
    EXPECTED_SOURCE_INPUTS[4]: (22619, "6504cd2de51cad0af856157fa09af0904a5df61c50fc66ad1ee04acfbbab06b3"),
}


class ValidationError(ValueError):
    """Raised when OT-143 evidence violates the frozen host-only contract."""


def _object_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError(f"duplicate key: {key}")
        result[key] = value
    return result


def load_evidence(path: Path = DEFAULT_EVIDENCE) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_object_no_duplicates)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError("evidence is unreadable") from exc
    if not isinstance(value, dict):
        raise ValidationError("evidence root must be an object")
    return value


def _keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{label} keys mismatch")


def _sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise ValidationError(f"{label} is not a SHA-256")
    try:
        int(value, 16)
    except ValueError as exc:
        raise ValidationError(f"{label} is not hexadecimal") from exc
    return value.lower()


def _artifact_tuple(items: Any, label: str) -> tuple[tuple[str, str, int, str], ...]:
    if not isinstance(items, list):
        raise ValidationError(f"{label} must be an array")
    result = []
    for index, item in enumerate(items):
        if not isinstance(item, dict):
            raise ValidationError(f"{label}[{index}] must be an object")
        _keys(item, {"role", "name", "bytes", "sha256"}, f"{label}[{index}]")
        if not isinstance(item["bytes"], int) or item["bytes"] <= 0:
            raise ValidationError(f"{label}[{index}] byte count invalid")
        result.append(
            (
                item["role"],
                item["name"],
                item["bytes"],
                _sha256(item["sha256"], f"{label}[{index}].sha256"),
            )
        )
    return tuple(result)


def validate(value: dict[str, Any], root: Path = ROOT) -> dict[str, Any]:
    _keys(
        value,
        {
            "schema",
            "version",
            "artifact_kind",
            "evidence_id",
            "recorded_date",
            "status",
            "source_project",
            "lineage",
            "configuration",
            "toolchain",
            "source_inputs",
            "build_reproducibility",
            "preserved_boundaries",
            "limitations",
            "claims",
        },
        "root",
    )
    if (
        value["schema"] != SCHEMA
        or value["version"] != 0
        or value["artifact_kind"] != "monocypher_corrected_target_build_evidence"
        or value["evidence_id"] != "OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0"
        or value["recorded_date"] != "2026-08-26"
        or value["status"] != "corrected_target_build_complete_host_only"
        or value["source_project"]
        != "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot142_corrected"
    ):
        raise ValidationError("root identity mismatch")

    lineage = value["lineage"]
    if lineage != {
        "predecessor": "OT-129",
        "accepted_direction": "OT-142",
        "frozen_protocol_runners_modified": False,
        "corrected_ot142_source_bound_directly": True,
    }:
        raise ValidationError("lineage mismatch")

    configuration = value["configuration"]
    _keys(
        configuration,
        {
            "primary_console",
            "secondary_console",
            "bootloader_log_level",
            "default_application_log_level",
            "rom_log_policy",
            "direct_usb_serial_jtag_driver_enabled",
            "bluetooth_enabled",
            "wifi_enabled",
            "required_resolved_lines",
            "forbidden_enabled_lines",
        },
        "configuration",
    )
    if (
        configuration["primary_console"] != "none"
        or configuration["secondary_console"] != "none"
        or configuration["bootloader_log_level"] != "none"
        or configuration["default_application_log_level"] != "none"
        or configuration["rom_log_policy"] != "always_on_unchanged"
        or configuration["direct_usb_serial_jtag_driver_enabled"] is not True
        or configuration["bluetooth_enabled"] is not False
        or configuration["wifi_enabled"] is not False
        or tuple(configuration["required_resolved_lines"]) != EXPECTED_REQUIRED_LINES
        or tuple(configuration["forbidden_enabled_lines"]) != EXPECTED_FORBIDDEN_LINES
    ):
        raise ValidationError("corrected configuration mismatch")

    toolchain = value["toolchain"]
    if toolchain != {
        "esp_idf_version": "v6.0.2",
        "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "esp_idf_worktree_clean": True,
        "idf_target": "esp32s3",
        "project_version": "ot142-corrected-v0",
        "ccache_allowed": False,
        "component_manager_network_allowed": False,
    }:
        raise ValidationError("toolchain mismatch")

    source_inputs = value["source_inputs"]
    if not isinstance(source_inputs, list) or len(source_inputs) != 14:
        raise ValidationError("source input count mismatch")
    seen_paths: set[str] = set()
    for index, item in enumerate(source_inputs):
        if not isinstance(item, dict):
            raise ValidationError(f"source_inputs[{index}] must be an object")
        _keys(item, {"path", "bytes", "sha256"}, f"source_inputs[{index}]")
        relative = item["path"]
        if (
            not isinstance(relative, str)
            or relative in seen_paths
            or "\\" in relative
            or relative.startswith("/")
            or ":" in relative
            or ".." in Path(relative).parts
        ):
            raise ValidationError("source path is not unique and repository-relative")
        seen_paths.add(relative)
        raw = (root / relative).read_bytes()
        if relative in CANONICAL_LF_INPUTS:
            if raw.startswith(b"\xef\xbb\xbf"):
                raise ValidationError(f"source binding has UTF-8 BOM: {relative}")
            raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        if item["bytes"] != len(raw) or _sha256(item["sha256"], "source sha256") != hashlib.sha256(raw).hexdigest():
            raise ValidationError(f"source binding mismatch: {relative}")
    if tuple(item["path"] for item in source_inputs) != EXPECTED_SOURCE_INPUTS:
        raise ValidationError("source input order or membership mismatch")
    for relative, expected in CANONICAL_CORRECTED_INPUTS.items():
        item = next(candidate for candidate in source_inputs if candidate["path"] == relative)
        if (item["bytes"], item["sha256"]) != expected:
            raise ValidationError(f"canonical corrected input mismatch: {relative}")

    build = value["build_reproducibility"]
    _keys(
        build,
        {
            "run_count",
            "initial_build_directories_absent",
            "artifact_roles",
            "required_four_artifact_roles",
            "artifact_tuples_identical",
            "canonical_artifact_tuple",
            "runs",
        },
        "build_reproducibility",
    )
    expected_roles = tuple(item[0] for item in EXPECTED_ARTIFACTS)
    if (
        build["run_count"] != 2
        or build["initial_build_directories_absent"] is not True
        or tuple(build["artifact_roles"]) != expected_roles
        or tuple(build["required_four_artifact_roles"])
        != ("application_bin", "application_elf", "linker_map", "generated_sdkconfig")
        or build["artifact_tuples_identical"] is not True
        or _artifact_tuple(build["canonical_artifact_tuple"], "canonical_artifact_tuple")
        != EXPECTED_ARTIFACTS
    ):
        raise ValidationError("build reproducibility mismatch")
    runs = build["runs"]
    if not isinstance(runs, list) or len(runs) != 2:
        raise ValidationError("run count mismatch")
    for index, run in enumerate(runs):
        if not isinstance(run, dict):
            raise ValidationError("run must be an object")
        _keys(
            run,
            {
                "run",
                "initial_build_directory_absent",
                "build_exit_code",
                "compiler_warning_count",
                "raw_build_log_sha256",
                "artifacts",
            },
            f"runs[{index}]",
        )
        if (
            run["run"] != ("A" if index == 0 else "B")
            or run["initial_build_directory_absent"] is not True
            or run["build_exit_code"] != 0
            or run["compiler_warning_count"] != 0
            or _artifact_tuple(run["artifacts"], f"runs[{index}].artifacts")
            != EXPECTED_ARTIFACTS
        ):
            raise ValidationError("run evidence mismatch")
        _sha256(run["raw_build_log_sha256"], "raw build log sha256")

    if value["preserved_boundaries"] != {
        "pre_ready_budget_bytes": 512,
        "start_retry_milliseconds": 250,
        "exact_ready_required": True,
        "frame_before_ready_rejected": True,
        "duplicate_and_post_ready_strict": True,
        "privacy_safe_diagnostics_required": True,
        "real_frame_count": 1014,
    }:
        raise ValidationError("frozen protocol boundaries changed")
    if value["limitations"] != {
        "physical_usb_silence_proven": False,
        "initial_rom_output_suppression_proven": False,
        "efuse_or_strap_change_proposed": False,
        "reason": "ESP-IDF host-only configuration cannot prove physical initial ROM output behavior.",
    }:
        raise ValidationError("physical limitation mismatch")
    claims = value["claims"]
    if set(claims) != {
        "hardware_accessed",
        "phone_accessed",
        "firmware_flashed",
        "benchmark_executed",
        "radio_used",
        "execution_authority_created",
        "candidate_selected",
        "phase_two_complete",
        "score_credit_added",
    } or any(claims.values()):
        raise ValidationError("forbidden claim present")

    return {
        "schema": SCHEMA,
        "application_sha256": EXPECTED_ARTIFACTS[0][3],
        "sdkconfig_sha256": EXPECTED_ARTIFACTS[-1][3],
        "artifact_tuples_identical": True,
        "hardware_accessed": False,
        "execution_authority_created": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    args = parser.parse_args()
    result = validate(load_evidence(args.evidence))
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())




