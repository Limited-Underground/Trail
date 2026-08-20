#!/usr/bin/env python3
"""Strict validator for the two-run OT-093 OT-005 build-lock baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTCBL0"
VERSION = 0
MAX_BYTES = 65_536
MAX_DEPTH = 10
MAX_NODES = 768
MAX_STRING = 256
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
PRIVATE_TEXT = re.compile(
    r"(?i)(?:[a-z]:\\|/users/|/home/|\\users\\|\bcom\d+\b|"
    r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b|"
    r"\b(?:password|passwd|private[_ -]?key|secret|token)\s*[:=]|"
    r"project_description)"
)
BLOCKERS = (
    "exact_client_board_and_revision_not_frozen",
    "esp_idf_toolchain_and_sdkconfig_not_pinned",
    "candidate_source_commits_and_dependency_locks_not_pinned",
    "direct_radio_mtu_and_phy_profile_not_frozen",
)
DENIED_CLAIMS = (
    "ot005_candidate_imported",
    "secure_lora_adapter_imported",
    "secure_lora_adapter_executed",
    "suite_selected",
    "handshake_kdf_selected",
    "packet_v1_wire_selected",
    "radio_profile_selected",
    "benchmark_executed",
    "hardware_or_device_accessed",
    "key_or_entropy_operation",
    "score_credit_added",
)
ARTIFACT_ROLES = (
    ("application", "opentrail_heltec_v4_bench.bin"),
    ("application_elf", "opentrail_heltec_v4_bench.elf"),
    ("linker_map", "opentrail_heltec_v4_bench.map"),
    ("bootloader", "bootloader.bin"),
    ("partition_table", "partition-table.bin"),
    ("generated_sdkconfig", "sdkconfig"),
    ("partition_csv", "partitions.csv"),
)
EXPECTED_ARTIFACTS = (
    ("application", "opentrail_heltec_v4_bench.bin", 471456, "7e0e9b358be30a88b55a44fa2e69448a3842f91267fd3065b881705ba5ae6d70"),
    ("application_elf", "opentrail_heltec_v4_bench.elf", 6792588, "b36262437c9a93925a5cf0d5a518731ddd29793bb3c4f5d2bab57da796d61696"),
    ("linker_map", "opentrail_heltec_v4_bench.map", 6366378, "9f360bba687717f7919858837a115588530a0ec6d480bc9872750a416334cf5d"),
    ("bootloader", "bootloader.bin", 22480, "96e83ebe4434cd6c9049a59f396b4f8bd06c159b40259da573bdb701c571eca5"),
    ("partition_table", "partition-table.bin", 3072, "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab"),
    ("generated_sdkconfig", "sdkconfig", 106913, "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"),
    ("partition_csv", "partitions.csv", 452, "4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389"),
)
TEXT_DIGEST_KIND = "utf8-crlf-normalized-lf-v1"
FROZEN_STATUS = "build_baseline_frozen"
FROZEN_PUBLIC_RESULT = "BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED"
PENDING_STATUS = "rebuild_pending"
PENDING_PUBLIC_RESULT = (
    "RAW-MANIFEST-REVISED; FRESH-A-B-RECONCILIATION-REQUIRED; "
    "OTCB0-EXECUTION-BLOCKED"
)
EXPECTED_RAW_BUILD_EVIDENCE_SHA256 = (
    "c5b4ce41ecc52bc5998c4c2eb508357687c0c90cd486c046d2391b23e22c2431",
    "15c152df46002d264dca148bc1bc632164d46d0f3f98014c3485a86c7bd3a1ca",
)


class ValidationError(ValueError):
    """The baseline is malformed, unsafe, or exceeds its authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate JSON key")
        result[key] = value
    return result


def load(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValidationError("baseline exceeds the size limit")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ValidationError("baseline JSON is unreadable or invalid") from exc
    return _object(value, "baseline")


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _object(value: Any, path: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{path} must be an exact object")
    return value


def _list(value: Any, path: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{path} must be an exact list")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{path} must contain the exact canonical fields")


def _string(value: Any, path: str) -> str:
    if type(value) is not str or not value or len(value) > MAX_STRING:
        raise ValidationError(f"{path} must be a bounded nonempty exact string")
    return value


def _integer(value: Any, path: str, *, minimum: int, maximum: int) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise ValidationError(f"{path} must be an exact integer in range")
    return value


def _boolean(value: Any, path: str, expected: bool) -> None:
    if type(value) is not bool or value is not expected:
        raise ValidationError(f"{path} must be {str(expected).lower()}")


def _bounded(value: Any) -> None:
    seen: set[int] = set()
    nodes = 0

    def visit(child: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise ValidationError("baseline exceeds structural bounds")
        if type(child) is dict:
            identity = id(child)
            if identity in seen or len(child) > 32:
                raise ValidationError("baseline contains a cycle or oversized object")
            seen.add(identity)
            for key, item in child.items():
                if type(key) is not str:
                    raise ValidationError("baseline object keys must be exact strings")
                visit(key, depth + 1)
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is list:
            identity = id(child)
            if identity in seen or len(child) > 16:
                raise ValidationError("baseline contains a cycle or oversized list")
            seen.add(identity)
            for item in child:
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is str:
            if len(child) > MAX_STRING:
                raise ValidationError("baseline contains oversized text")
            if PRIVATE_TEXT.search(child):
                raise ValidationError("baseline contains private or nonpublic build text")
        elif child is not None and type(child) not in (int, bool):
            raise ValidationError("baseline contains a noncanonical JSON type")

    visit(value, 0)


def _sha(value: Any, path: str, *, length: int = 64) -> str:
    text = _string(value, path)
    pattern = HEX64 if length == 64 else HEX40
    if not pattern.fullmatch(text):
        raise ValidationError(f"{path} must be lowercase SHA evidence")
    return text


def normalized_receipt(baseline: dict[str, Any], run: dict[str, Any]) -> dict[str, Any]:
    return {
        "source": baseline["source"],
        "toolchain": baseline["toolchain"],
        "target": baseline["target"],
        "inputs": baseline["inputs"],
        "public_artifacts": run["public_artifacts"],
        "app_slot_headroom": baseline["app_slot_headroom"],
    }


def validate(baseline: dict[str, Any]) -> dict[str, Any]:
    _bounded(baseline)
    _exact_keys(
        baseline,
        {
            "schema",
            "version",
            "artifact_kind",
            "baseline_id",
            "accepted_date",
            "status",
            "public_result",
            "source",
            "toolchain",
            "target",
            "inputs",
            "build_reproducibility",
            "app_slot_headroom",
            "otcb0_gate",
            "claim_scope",
            "claims",
            "historical_otcb0_plan_blockers",
            "otcb0_plan_blockers_applicability",
        },
        "baseline",
    )
    if baseline["schema"] != SCHEMA or _integer(
        baseline["version"], "version", minimum=VERSION, maximum=VERSION
    ) != VERSION:
        raise ValidationError("baseline schema/version mismatch")
    if baseline["artifact_kind"] != "build_lock":
        raise ValidationError("artifact_kind must be build_lock")
    if baseline["baseline_id"] != "OT-093-OT005-BUILD-BASELINE-V0":
        raise ValidationError("baseline_id mismatch")
    if not DATE.fullmatch(_string(baseline["accepted_date"], "accepted_date")):
        raise ValidationError("accepted_date must be YYYY-MM-DD")
    if baseline["accepted_date"] != "2026-08-20":
        raise ValidationError("accepted_date mismatch")
    status = _string(baseline["status"], "status")
    public_result = _string(baseline["public_result"], "public_result")
    if status == FROZEN_STATUS:
        if public_result != FROZEN_PUBLIC_RESULT:
            raise ValidationError("frozen public_result mismatch")
        rebuild_pending = False
    elif status == PENDING_STATUS:
        if public_result != PENDING_PUBLIC_RESULT:
            raise ValidationError("pending public_result mismatch")
        rebuild_pending = True
    else:
        raise ValidationError("status must be rebuild_pending or build_baseline_frozen")

    source = _object(baseline["source"], "source")
    _exact_keys(
        source,
        {
            "firmware_base_commit",
            "firmware_scope_clean",
            "input_manifest_kind",
            "input_manifest_file_count",
            "input_manifest_sha256",
            "working_tree_manifest_kind",
            "working_tree_manifest_sha256",
            "git_core_autocrlf",
        },
        "source",
    )
    if _sha(source["firmware_base_commit"], "source.firmware_base_commit", length=40) != (
        "0afac6b1cf3d142aca2f2cae98264f80ee801989"
    ):
        raise ValidationError("firmware base commit mismatch")
    _boolean(source["firmware_scope_clean"], "source.firmware_scope_clean", True)
    if source["input_manifest_kind"] != "git-index-stage-zero-v1":
        raise ValidationError("firmware input-manifest kind mismatch")
    if _integer(source["input_manifest_file_count"], "source.input_manifest_file_count", minimum=1, maximum=4096) != 307:
        raise ValidationError("firmware input-manifest file count mismatch")
    if _sha(source["input_manifest_sha256"], "source.input_manifest_sha256") != (
        "6738195a7da53eb3d03c4a47552f6c0b6489559a2d81c0ba068489fe9faf7bc3"
    ):
        raise ValidationError("firmware input-manifest digest mismatch")
    if source["working_tree_manifest_kind"] != "sha256-raw-bytes-path-v1":
        raise ValidationError("firmware working-tree manifest kind mismatch")
    if _sha(source["working_tree_manifest_sha256"], "source.working_tree_manifest_sha256") != (
        "3837dbce866a3fc7cef76fd374bf242bb0125c042e8de15273a9e44bafff3324"
    ):
        raise ValidationError("firmware working-tree manifest digest mismatch")
    if source["git_core_autocrlf"] != "true":
        raise ValidationError("firmware working-tree EOL contract mismatch")

    toolchain = _object(baseline["toolchain"], "toolchain")
    _exact_keys(
        toolchain,
        {
            "esp_idf_version",
            "esp_idf_commit",
            "esp_idf_tracked_source_clean",
            "idf_py_sha256",
            "compiler",
            "compiler_version",
            "crosstool",
            "compiler_executable_sha256",
            "cmake_version",
            "cmake_executable_sha256",
            "ninja_version",
            "ninja_executable_sha256",
            "python_version",
            "python_executable_sha256",
            "independent_python_cache",
            "python_user_site_disabled",
        },
        "toolchain",
    )
    if toolchain["esp_idf_version"] != "v6.0.2":
        raise ValidationError("ESP-IDF version mismatch")
    if _sha(toolchain["esp_idf_commit"], "toolchain.esp_idf_commit", length=40) != (
        "7101770dc6db2667b3c477cc31365dd1acd6db4e"
    ):
        raise ValidationError("ESP-IDF commit mismatch")
    _boolean(toolchain["esp_idf_tracked_source_clean"], "toolchain.esp_idf_tracked_source_clean", True)
    if _sha(toolchain["idf_py_sha256"], "toolchain.idf_py_sha256") != (
        "5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40"
    ):
        raise ValidationError("idf.py digest mismatch")
    if toolchain["compiler"] != "xtensa-esp32s3-elf-gcc":
        raise ValidationError("compiler mismatch")
    if toolchain["compiler_version"] != "15.2.0" or toolchain["crosstool"] != "esp-15.2.0_20251204":
        raise ValidationError("compiler release mismatch")
    if _sha(toolchain["compiler_executable_sha256"], "toolchain.compiler_executable_sha256") != (
        "20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5"
    ):
        raise ValidationError("compiler executable digest mismatch")
    expected_tools = {
        "cmake_version": "cmake version 4.0.3",
        "cmake_executable_sha256": "392ab4d6c3c91543fd297ed6c7e7354bf62edcd26fdf2706ad8613ad620cc45e",
        "ninja_version": "1.12.1",
        "ninja_executable_sha256": "68865c3276d449d746cea5065fdec2baf755d7813e161ab04205b0907b2629b8",
        "python_version": "Python 3.14.6",
        "python_executable_sha256": "199ce15a9f0d4f9522edba59338e4879d28cf61f88e377b8164bcb716275ed22",
    }
    for field, expected in expected_tools.items():
        actual = _sha(toolchain[field], f"toolchain.{field}") if field.endswith("sha256") else _string(toolchain[field], f"toolchain.{field}")
        if actual != expected:
            raise ValidationError(f"toolchain.{field} mismatch")
    _boolean(toolchain["independent_python_cache"], "toolchain.independent_python_cache", True)
    _boolean(toolchain["python_user_site_disabled"], "toolchain.python_user_site_disabled", True)

    target = _object(baseline["target"], "target")
    _exact_keys(
        target,
        {
            "target_id",
            "mcu",
            "processor_revision",
            "flash_bytes",
            "psram_bytes",
            "exact_received_revision",
            "rf_variant",
            "supported",
        },
        "target",
    )
    if target["target_id"] != "heltec-v4-bench-candidate":
        raise ValidationError("target_id mismatch")
    if target["mcu"] != "ESP32-S3" or target["processor_revision"] != "v0.2":
        raise ValidationError("target processor baseline mismatch")
    _integer(target["flash_bytes"], "target.flash_bytes", minimum=16_777_216, maximum=16_777_216)
    _integer(target["psram_bytes"], "target.psram_bytes", minimum=2_097_152, maximum=2_097_152)
    if target["exact_received_revision"] is not None or target["rf_variant"] is not None:
        raise ValidationError("received revision and RF variant must remain unresolved")
    _boolean(target["supported"], "target.supported", False)

    inputs = _object(baseline["inputs"], "inputs")
    _exact_keys(
        inputs,
        {
            "target_contract",
            "target_contract_sha256",
            "sdkconfig_defaults",
            "sdkconfig_defaults_sha256",
            "build_helper",
            "build_helper_sha256",
            "reproducible_defaults_sha256",
            "generated_sdkconfig_sha256",
            "generated_sdkconfig_role",
            "future_candidate_builds_require_same_baseline_config",
            "project_version",
            "text_digest_kind",
        },
        "inputs",
    )
    expected_paths = {
        "target_contract": "firmware/targets/heltec_v4_bench/target-contract.json",
        "sdkconfig_defaults": "firmware/targets/heltec_v4_bench/sdkconfig.defaults",
        "build_helper": "tools/Build-HeltecV4BenchTarget.ps1",
    }
    if inputs["project_version"] != "ot093-precrypto-v0":
        raise ValidationError("inputs.project_version mismatch")
    if inputs["text_digest_kind"] != TEXT_DIGEST_KIND:
        raise ValidationError("inputs.text_digest_kind mismatch")
    for field, expected in expected_paths.items():
        if inputs[field] != expected:
            raise ValidationError(f"inputs.{field} mismatch")
        _sha(inputs[f"{field}_sha256"], f"inputs.{field}_sha256")
    expected_text_hashes = {
        "target_contract_sha256": "8f263e9d5fd756e5b80dbed33dbbc4e264a66763ce6088bca3d894cc42025615",
        "sdkconfig_defaults_sha256": "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0",
        "build_helper_sha256": "c36a58e0529a7628b66cc4674f4207804ae2d0a709b9d3a20b4973de6c585214",
    }
    for field, expected in expected_text_hashes.items():
        if inputs[field] != expected:
            raise ValidationError(f"inputs.{field} mismatch")
    if _sha(inputs["reproducible_defaults_sha256"], "inputs.reproducible_defaults_sha256") != (
        "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6"
    ):
        raise ValidationError("reproducible defaults digest mismatch")
    _sha(inputs["generated_sdkconfig_sha256"], "inputs.generated_sdkconfig_sha256")
    if inputs["generated_sdkconfig_role"] != "PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0":
        raise ValidationError("generated sdkconfig cannot be final OTCB0 configuration")
    _boolean(
        inputs["future_candidate_builds_require_same_baseline_config"],
        "inputs.future_candidate_builds_require_same_baseline_config",
        True,
    )

    reproducibility = _object(baseline["build_reproducibility"], "build_reproducibility")
    _exact_keys(
        reproducibility,
        {
            "clean_run_count",
            "independent_build_directories",
            "shared_compiler_cache_disabled",
            "reproducible_build_paths_normalized",
            "runs",
        },
        "build_reproducibility",
    )
    _boolean(reproducibility["independent_build_directories"], "build_reproducibility.independent_build_directories", True)
    _boolean(reproducibility["shared_compiler_cache_disabled"], "build_reproducibility.shared_compiler_cache_disabled", True)
    _boolean(reproducibility["reproducible_build_paths_normalized"], "build_reproducibility.reproducible_build_paths_normalized", True)

    headroom = _object(baseline["app_slot_headroom"], "app_slot_headroom")
    _exact_keys(headroom, {"application_image_bytes", "smallest_app_slot_bytes", "headroom_bytes"}, "app_slot_headroom")
    runs = _list(reproducibility["runs"], "build_reproducibility.runs")
    artifact_sets: list[list[tuple[str, str, int, str]]] = []
    normalized_digests: list[str] = []
    if rebuild_pending:
        _integer(
            reproducibility["clean_run_count"],
            "build_reproducibility.clean_run_count",
            minimum=0,
            maximum=0,
        )
        if runs:
            raise ValidationError("rebuild_pending must not contain run receipts")
        if any(headroom[field] is not None for field in headroom):
            raise ValidationError("rebuild_pending headroom must be null")
    else:
        _integer(
            reproducibility["clean_run_count"],
            "build_reproducibility.clean_run_count",
            minimum=2,
            maximum=2,
        )
        image_bytes = _integer(headroom["application_image_bytes"], "app_slot_headroom.application_image_bytes", minimum=1, maximum=5_177_343)
        slot_bytes = _integer(headroom["smallest_app_slot_bytes"], "app_slot_headroom.smallest_app_slot_bytes", minimum=5_177_344, maximum=5_177_344)
        remaining = _integer(headroom["headroom_bytes"], "app_slot_headroom.headroom_bytes", minimum=1, maximum=5_177_343)
        if slot_bytes - image_bytes != remaining:
            raise ValidationError("application headroom equation mismatch")
        if len(runs) != 2:
            raise ValidationError("exactly two run receipts are required")
        for run_index, expected_profile in enumerate(("ot093-a", "ot093-b")):
            run_path = f"build_reproducibility.runs[{run_index}]"
            run = _object(runs[run_index], run_path)
            _exact_keys(
                run,
                {
                    "profile",
                    "build_exit_code",
                    "compiler_warning_count",
                    "raw_build_evidence_sha256",
                    "public_artifacts",
                    "normalized_receipt_sha256",
                },
                run_path,
            )
            if run["profile"] != expected_profile:
                raise ValidationError("run profile order mismatch")
            _integer(run["build_exit_code"], f"{run_path}.build_exit_code", minimum=0, maximum=0)
            _integer(run["compiler_warning_count"], f"{run_path}.compiler_warning_count", minimum=0, maximum=0)
            raw_digest = _sha(run["raw_build_evidence_sha256"], f"{run_path}.raw_build_evidence_sha256")
            if raw_digest != EXPECTED_RAW_BUILD_EVIDENCE_SHA256[run_index]:
                raise ValidationError("raw build-evidence digest mismatch")
            artifacts = _list(run["public_artifacts"], f"{run_path}.public_artifacts")
            if len(artifacts) != len(EXPECTED_ARTIFACTS):
                raise ValidationError("public_artifacts length mismatch")
            artifact_set: list[tuple[str, str, int, str]] = []
            for artifact_index, expected in enumerate(EXPECTED_ARTIFACTS):
                artifact_path = f"{run_path}.public_artifacts[{artifact_index}]"
                artifact = _object(artifacts[artifact_index], artifact_path)
                _exact_keys(artifact, {"role", "name", "bytes", "sha256"}, artifact_path)
                role, name, expected_bytes, expected_sha256 = expected
                actual = (
                    _string(artifact["role"], f"{artifact_path}.role"),
                    _string(artifact["name"], f"{artifact_path}.name"),
                    _integer(artifact["bytes"], f"{artifact_path}.bytes", minimum=1, maximum=33_554_432),
                    _sha(artifact["sha256"], f"{artifact_path}.sha256"),
                )
                if actual != (role, name, expected_bytes, expected_sha256):
                    raise ValidationError("public artifact immutable tuple mismatch")
                artifact_set.append(actual)
            if artifacts[0]["bytes"] != image_bytes or artifacts[5]["sha256"] != inputs["generated_sdkconfig_sha256"]:
                raise ValidationError("run artifacts do not match baseline inputs or headroom")
            artifact_sets.append(artifact_set)
            digest = _sha(run["normalized_receipt_sha256"], f"{run_path}.normalized_receipt_sha256")
            if digest != canonical_sha256(normalized_receipt(baseline, run)):
                raise ValidationError("normalized run receipt digest mismatch")
            normalized_digests.append(digest)
        if artifact_sets[0] != artifact_sets[1] or normalized_digests[0] != normalized_digests[1]:
            raise ValidationError("two-run artifact or normalized-receipt equality mismatch")

    gate = _object(baseline["otcb0_gate"], "otcb0_gate")
    _exact_keys(gate, {"schema", "version", "plan_id", "plan_sha256", "plan_status", "execution_authorized"}, "otcb0_gate")
    if gate["schema"] != "OTCB0" or _integer(
        gate["version"], "otcb0_gate.version", minimum=0, maximum=0
    ) != 0:
        raise ValidationError("OTCB0 gate schema/version mismatch")
    if gate["plan_id"] != "OT-005-CRYPTO-ESP32S3-V0":
        raise ValidationError("OTCB0 plan_id mismatch")
    if _sha(gate["plan_sha256"], "otcb0_gate.plan_sha256") != (
        "49792b585286823ffa9b7589704d57e8393b3dbf3d514917ffd7b5970301edb7"
    ):
        raise ValidationError("OTCB0 plan digest mismatch")
    if gate["plan_status"] != "draft_blocked":
        raise ValidationError("OTCB0 plan must remain draft_blocked")
    _boolean(gate["execution_authorized"], "otcb0_gate.execution_authorized", False)

    if baseline["claim_scope"] != (
        "NO-OT005-CANDIDATE-OR-SECURE-LORA-ADAPTER-IMPORTED-OR-EXECUTED; "
        "EXISTING-ESP-IDF-NIMBLE-CRYPTOGRAPHIC-OBJECTS-ARE-NOT-AN-OT005-SELECTION"
    ):
        raise ValidationError("claim_scope mismatch")
    claims = _object(baseline["claims"], "claims")
    _exact_keys(claims, set(DENIED_CLAIMS), "claims")
    for field in DENIED_CLAIMS:
        _boolean(claims[field], f"claims.{field}", False)
    if _list(
        baseline["historical_otcb0_plan_blockers"],
        "historical_otcb0_plan_blockers",
    ) != list(BLOCKERS):
        raise ValidationError("historical OTCB0 blockers must preserve the canonical ordered set")
    if baseline["otcb0_plan_blockers_applicability"] != (
        "FINAL-CANDIDATE-READY-TARGET-TOOLCHAIN-SDKCONFIG-APPLICABILITY-REMAINS-UNRESOLVED"
    ):
        raise ValidationError("historical OTCB0 blocker applicability mismatch")

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "baseline_id": baseline["baseline_id"],
        "status": baseline["status"],
        "public_result": baseline["public_result"],
        "otcb0_status": gate["plan_status"],
        "execution_authorized": False,
        "score_credit_added": False,
        "ordered_artifacts_equal": (
            False if rebuild_pending else artifact_sets[0] == artifact_sets[1]
        ),
        "normalized_receipts_equal": (
            False if rebuild_pending else normalized_digests[0] == normalized_digests[1]
        ),
        "normalized_receipt_sha256": (
            None if rebuild_pending else normalized_digests[0]
        ),
        "baseline_sha256": canonical_sha256(baseline),
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(validate(load(args.baseline)), sort_keys=True))
        return 0
    except ValidationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
