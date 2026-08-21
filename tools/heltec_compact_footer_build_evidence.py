#!/usr/bin/env python3
"""Strict host-only validator for OTFBL0/v0 compact-footer build evidence."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "OTFBL0"
VERSION = 0
RECORD_ID = "OT-106-HELTEC-V4-COMPACT-FOOTER-BUILD-V0"
STATUS = "build_evidence_captured"
PUBLIC_RESULT = (
    "COMPUTER-BUILD-ONLY; COMPACT-FOOTER-BUILD-LINKED; "
    "DEVICE-AND-RADIO-NOT-RUN"
)
PROFILES = ("ot106-a", "ot106-b")
MAX_BYTES = 256_000
SHA256 = re.compile(r"^[0-9a-f]{64}$")
PRIVATE = re.compile(r"(?i)([a-z]:\\|/users/|\\users\\|com\d+|tty|mac[_ -]?address)")
EXPECTED_SOURCE = (
    309,
    "c2b731d2e3ca8031afc3679759c8768f183e4c3665877ed342981df4ef7030d8",
    "95a888b6f8c47509af744426e94e429040b0ef29b1947371c71fc18006dec0a9",
)
EXPECTED_TOOL_HASHES = {
    "idf_py_sha256": "5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40",
    "compiler_sha256": "20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5",
    "cmake_sha256": "392ab4d6c3c91543fd297ed6c7e7354bf62edcd26fdf2706ad8613ad620cc45e",
    "ninja_sha256": "68865c3276d449d746cea5065fdec2baf755d7813e161ab04205b0907b2629b8",
    "python_sha256": "199ce15a9f0d4f9522edba59338e4879d28cf61f88e377b8164bcb716275ed22",
}
EXPECTED_INPUTS = {
    "build_helper": ("tools/Build-HeltecV4BenchCompactFooter.ps1", "81c23dab25b26a90a17be585862c74b119c63df64433d0c3b3cc2d9e50b7f289"),
    "target_contract": ("firmware/targets/heltec_v4_bench/target-contract.json", "27274362c77bd53ebc14fa32a22f39c4b6302d5cba0634a78ac1d0175fa1a9ad"),
    "sdkconfig_defaults": ("firmware/targets/heltec_v4_bench/sdkconfig.defaults", "a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb"),
}
EXPECTED_REPRODUCIBLE_DEFAULTS = "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6"
EXPECTED_RAW_RECEIPTS = (
    "154a1d21741ccb9c98a8bcfbf2462b581279cf635c4d565d3a45a49ce89af379",
    "2dd97bbedc477bb8b4eb0206b41198a4bef520ce34f1ea2b6dcb72284435c842",
)
EXPECTED_NORMALIZED_RECEIPT = "7c5a46a0edcc1efd860a737cfd185b231eb8b54bdce081823dbedba0830cb84e"
EXPECTED_ARTIFACTS = (
    ("application", "opentrail_heltec_v4_bench.bin", 473024, "d6047f45b6defbf5613bdfcc6ffbf5207a96fa26812090bab58cc593e260e017"),
    ("application_elf", "opentrail_heltec_v4_bench.elf", 6835144, "ad6a8acfe02073cd4c503820a262591176e8486e623f504d26bf7677f954d236"),
    ("linker_map", "opentrail_heltec_v4_bench.map", 6374646, "7835435b5dce27e5eafdc08ff049a3e57b447fa6f9cf8921fdcaaab4b312ab74"),
    ("bootloader", "bootloader.bin", 22480, "96e83ebe4434cd6c9049a59f396b4f8bd06c159b40259da573bdb701c571eca5"),
    ("partition_table", "partition-table.bin", 3072, "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab"),
    ("generated_sdkconfig", "sdkconfig", 106913, "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"),
    ("partition_csv", "partitions.csv", 452, "4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389"),
)
EXPECTED_OT093 = {
    "ot093_record_sha256": "240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83",
    "ot093_helper_sha256": "6f3ffcb724e2eb52f3d553596481983330fdb2484871b0aab5f793763f2d07fe",
    "ot093_validator_sha256": "84e441141708d839d6cb13117476068a7c36570fdafd880173196205c778c747",
    "ot093_tests_sha256": "83dff18b1b73f3567368e6e2cc582718eae7437e1a8a535c060ccaa1634730d4",
    "ot093_evidence_note_sha256": "d4b504b34ec731b287ef24276861bd7ce105fd2b613ce00cdc3d22cb84ce8938",
}


class ValidationError(ValueError):
    pass


def _exact_dict(value: Any, fields: tuple[str, ...], label: str) -> dict[str, Any]:
    if type(value) is not dict or tuple(value) != fields:
        raise ValidationError(f"{label} must contain exact canonical fields")
    return value


def _text(value: Any, label: str) -> str:
    if type(value) is not str or not value or len(value) > 512 or PRIVATE.search(value):
        raise ValidationError(f"{label} must be bounded public text")
    return value


def _sha(value: Any, label: str) -> str:
    value = _text(value, label)
    if SHA256.fullmatch(value) is None:
        raise ValidationError(f"{label} must be lowercase SHA-256")
    return value


def _integer(value: Any, label: str, minimum: int = 0, maximum: int = 1 << 31) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise ValidationError(f"{label} must be an exact bounded integer")
    return value


def _boolean(value: Any, expected: bool, label: str) -> bool:
    if type(value) is not bool or value is not expected:
        raise ValidationError(f"{label} must be {expected}")
    return value


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode()
    return hashlib.sha256(encoded).hexdigest()


def artifact_tuple(run: dict[str, Any]) -> tuple[tuple[str, str, int, str], ...]:
    artifacts = run["build"]["public_artifacts"]
    return tuple((item["role"], item["name"], item["bytes"], item["sha256"]) for item in artifacts)


def validate(value: Any) -> dict[str, Any]:
    root = _exact_dict(value, (
        "schema", "version", "record_id", "status", "public_result", "source",
        "toolchain", "inputs", "build_reproducibility", "footer", "app_slot_headroom",
        "historical_preservation", "claims",
    ), "root")
    if root["schema"] != SCHEMA or _integer(root["version"], "root version") != VERSION or root["record_id"] != RECORD_ID:
        raise ValidationError("OTFBL0 identity mismatch")
    if root["status"] != STATUS or root["public_result"] != PUBLIC_RESULT:
        raise ValidationError("OTFBL0 status mismatch")

    source = _exact_dict(root["source"], (
        "firmware_input_manifest_kind", "firmware_input_manifest_file_count",
        "firmware_input_manifest_sha256", "working_tree_manifest_kind",
        "working_tree_manifest_sha256", "git_core_autocrlf",
    ), "source")
    if source["firmware_input_manifest_kind"] != "git-index-stage-zero-v1" or source["working_tree_manifest_kind"] != "sha256-raw-bytes-path-v1":
        raise ValidationError("source manifest kind mismatch")
    if (
        _integer(source["firmware_input_manifest_file_count"], "source count", 1),
        _sha(source["firmware_input_manifest_sha256"], "index manifest"),
        _sha(source["working_tree_manifest_sha256"], "working manifest"),
    ) != EXPECTED_SOURCE:
        raise ValidationError("source lock mismatch")
    if source["git_core_autocrlf"] != "true":
        raise ValidationError("working-tree byte contract mismatch")

    toolchain = _exact_dict(root["toolchain"], (
        "framework", "framework_commit", "framework_clean", "idf_py_sha256",
        "compiler", "compiler_version", "compiler_crosstool", "compiler_sha256",
        "cmake_version", "cmake_sha256", "ninja_version", "ninja_sha256",
        "python_version", "python_sha256",
    ), "toolchain")
    expected_text = {
        "framework": "ESP-IDF v6.0.2",
        "framework_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "compiler": "xtensa-esp32s3-elf-gcc",
        "compiler_version": "15.2.0",
        "compiler_crosstool": "esp-15.2.0_20251204",
        "cmake_version": "cmake version 4.0.3",
        "ninja_version": "1.12.1",
        "python_version": "Python 3.14.6",
    }
    for field, expected in expected_text.items():
        if toolchain[field] != expected:
            raise ValidationError(f"toolchain {field} mismatch")
    _boolean(toolchain["framework_clean"], True, "framework clean")
    for field, expected in EXPECTED_TOOL_HASHES.items():
        if _sha(toolchain[field], field) != expected:
            raise ValidationError(f"toolchain {field} digest mismatch")

    inputs = _exact_dict(root["inputs"], (
        "project_version", "build_helper", "build_helper_sha256", "target_contract",
        "target_contract_sha256", "sdkconfig_defaults", "sdkconfig_defaults_sha256",
        "reproducible_defaults_sha256",
    ), "inputs")
    if inputs["project_version"] != "ot106-footer-v0":
        raise ValidationError("project version mismatch")
    for field, expected in EXPECTED_INPUTS.items():
        actual = (_text(inputs[field], field), _sha(inputs[f"{field}_sha256"], f"{field} digest"))
        if actual != expected:
            raise ValidationError(f"{field} lock mismatch")
    if _sha(inputs["reproducible_defaults_sha256"], "reproducible defaults") != EXPECTED_REPRODUCIBLE_DEFAULTS:
        raise ValidationError("reproducible defaults lock mismatch")

    reproducibility = _exact_dict(root["build_reproducibility"], (
        "clean_run_count", "profiles", "independent_build_directories",
        "independent_python_caches", "python_user_site_disabled",
        "shared_compiler_cache_disabled", "reproducible_build_paths_normalized", "runs",
    ), "build reproducibility")
    if _integer(reproducibility["clean_run_count"], "run count") != 2 or tuple(reproducibility["profiles"]) != PROFILES:
        raise ValidationError("exact two-profile run set required")
    for field in (
        "independent_build_directories", "independent_python_caches",
        "python_user_site_disabled", "shared_compiler_cache_disabled",
        "reproducible_build_paths_normalized",
    ):
        _boolean(reproducibility[field], True, field)
    runs = reproducibility["runs"]
    if type(runs) is not list or len(runs) != 2:
        raise ValidationError("exactly two runs required")
    artifacts_by_run = []
    for index, run_value in enumerate(runs):
        run = _exact_dict(run_value, ("profile", "build_helper_sha256", "raw_receipt_sha256", "normalized_receipt_sha256", "build"), f"run {index}")
        if run["profile"] != PROFILES[index]:
            raise ValidationError("run profile order mismatch")
        if _sha(run["build_helper_sha256"], "run build helper") != EXPECTED_INPUTS["build_helper"][1]:
            raise ValidationError("run build-helper provenance mismatch")
        if _sha(run["raw_receipt_sha256"], "raw receipt") != EXPECTED_RAW_RECEIPTS[index]:
            raise ValidationError("raw receipt lock mismatch")
        if _sha(run["normalized_receipt_sha256"], "normalized receipt") != EXPECTED_NORMALIZED_RECEIPT:
            raise ValidationError("normalized receipt lock mismatch")
        build = _exact_dict(run["build"], (
            "exit_code", "compiler_warning_count", "partition_layout",
            "public_artifacts", "application_image_bytes",
            "smallest_app_slot_bytes", "headroom_bytes",
        ), "run build")
        if _integer(build["exit_code"], "exit code") != 0 or _integer(build["compiler_warning_count"], "warning count") != 0:
            raise ValidationError("build must pass without warnings")
        if build["partition_layout"] != "OTHP0/v0-verified-generated-binary":
            raise ValidationError("partition binary verification mismatch")

        artifacts = build["public_artifacts"]
        if type(artifacts) is not list or len(artifacts) != 7:
            raise ValidationError("exact seven-artifact receipt required")
        roles = ("application", "application_elf", "linker_map", "bootloader", "partition_table", "generated_sdkconfig", "partition_csv")
        for position, item_value in enumerate(artifacts):
            item = _exact_dict(item_value, ("role", "name", "bytes", "sha256"), "artifact")
            if item["role"] != roles[position]:
                raise ValidationError("artifact role order mismatch")
            _text(item["name"], "artifact name")
            _integer(item["bytes"], "artifact bytes", 1)
            _sha(item["sha256"], "artifact digest")
        if (
            _integer(build["application_image_bytes"], "run application bytes", 1),
            _integer(build["smallest_app_slot_bytes"], "run slot bytes", 1),
            _integer(build["headroom_bytes"], "run headroom", 1),
        ) != (473024, 5177344, 4704320):
            raise ValidationError("run headroom receipt mismatch")
        normalized = {"profile": "ot106", "build": build}
        if run["normalized_receipt_sha256"] != canonical_sha256(normalized):
            raise ValidationError("normalized receipt mismatch")
        artifacts_by_run.append(artifact_tuple(run))
    if artifacts_by_run[0] != artifacts_by_run[1] or artifacts_by_run[0] != EXPECTED_ARTIFACTS:
        raise ValidationError("the exact immutable artifact tuple is required")

    footer = _exact_dict(root["footer"], (
        "component_object_linked", "target_display_owner_linked", "ble_short_codes",
        "battery_live_source_bound", "gps_live_source_bound", "radio_activity_source_bound",
    ), "footer")
    _boolean(footer["component_object_linked"], True, "footer linked")
    _boolean(footer["target_display_owner_linked"], True, "display owner linked")
    if footer["ble_short_codes"] != ["S", "A", "C", "R", "E"]:
        raise ValidationError("BLE short-code legend mismatch")
    for field in ("battery_live_source_bound", "gps_live_source_bound", "radio_activity_source_bound"):
        _boolean(footer[field], False, field)

    headroom = _exact_dict(root["app_slot_headroom"], (
        "application_image_bytes", "smallest_app_slot_bytes", "headroom_bytes",
    ), "headroom")
    app_bytes = _integer(headroom["application_image_bytes"], "application bytes", 1)
    slot_bytes = _integer(headroom["smallest_app_slot_bytes"], "slot bytes", 1)
    remaining = _integer(headroom["headroom_bytes"], "headroom", 1)
    if slot_bytes != 5177344 or slot_bytes - app_bytes != remaining or app_bytes != runs[0]["build"]["public_artifacts"][0]["bytes"]:
        raise ValidationError("headroom equation mismatch")

    preservation = _exact_dict(root["historical_preservation"], (
        "ot093_record_sha256", "ot093_helper_sha256", "ot093_validator_sha256",
        "ot093_tests_sha256", "ot093_evidence_note_sha256",
    ), "historical preservation")
    for field, expected in EXPECTED_OT093.items():
        if _sha(preservation[field], field) != expected:
            raise ValidationError("OT-093 preservation lock mismatch")

    claims = _exact_dict(root["claims"], (
        "hardware_or_device_accessed", "flash_or_erase_performed", "radio_or_ble_executed",
        "key_or_entropy_operation", "target_support_claimed", "physical_display_claimed",
        "live_telemetry_claimed", "candidate_readiness_claimed",
        "final_configuration_selected", "crypto_candidate_imported",
        "crypto_benchmark_executed", "crypto_suite_selected", "score_credit_added",
    ), "claims")
    for field in claims:
        _boolean(claims[field], False, field)
    return root


def load(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > MAX_BYTES:
            raise ValidationError("evidence exceeds the size limit")
        value = json.loads(path.read_text(encoding="utf-8"))
    except ValidationError:
        raise
    except Exception as exc:
        raise ValidationError("evidence JSON is unreadable or invalid") from exc
    return validate(value)


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[1].startswith("-"):
        print("ERROR: invalid command line", file=sys.stderr)
        return 2
    try:
        value = load(Path(argv[1]))
    except ValidationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"PASS: {value['record_id']} {value['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
