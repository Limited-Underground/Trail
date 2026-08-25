#!/usr/bin/env python3
"""Validate the accepted OT-139 host-only quiet-target build evidence."""

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
    / "OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0.json"
)
SCHEMA = "OT139QTB0"
EXPECTED_ARTIFACTS = (
    (
        "application_bin",
        "ot139_monocypher_quiet_bench.bin",
        149920,
        "29eee8c7294064d772770e2b4591c352eb0a9068b63f5a1fc62d89481ec5f204",
    ),
    (
        "application_elf",
        "ot139_monocypher_quiet_bench.elf",
        3314692,
        "ad67778525e29b62bad777bc69e95e1128f6e10011ef0964bccb15c2508a7a62",
    ),
    (
        "linker_map",
        "ot139_monocypher_quiet_bench.map",
        2849968,
        "15626da3ced8a15dee28dffc450a65ffbd036aa5d584aa58945aa9e7e6bca8c1",
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


class ValidationError(ValueError):
    """Raised when OT-139 evidence violates the frozen host-only contract."""


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
        or value["artifact_kind"] != "monocypher_quiet_target_build_evidence"
        or value["evidence_id"] != "OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0"
        or value["recorded_date"] != "2026-08-25"
        or value["status"] != "quiet_target_build_complete_host_only"
        or value["source_project"]
        != "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet"
    ):
        raise ValidationError("root identity mismatch")

    lineage = value["lineage"]
    if lineage != {
        "predecessor": "OT-129",
        "accepted_direction": "OT-138",
        "frozen_protocol_runners_modified": False,
        "frozen_ot129_source_reused_by_reference": True,
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
        raise ValidationError("quiet configuration mismatch")

    toolchain = value["toolchain"]
    if toolchain != {
        "esp_idf_version": "v6.0.2",
        "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "esp_idf_worktree_clean": True,
        "idf_target": "esp32s3",
        "project_version": "ot139-quiet-v0",
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
        if item["bytes"] != len(raw) or _sha256(item["sha256"], "source sha256") != hashlib.sha256(raw).hexdigest():
            raise ValidationError(f"source binding mismatch: {relative}")

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
