#!/usr/bin/env python3
"""Validate fail-closed matched ESP-IDF candidate/control resource evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_REPO_PATH = (
    "tests/benchmarks/crypto/"
    "OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json"
)
SCHEMA = "OTMRAC1"
VERSION = 1
RESULT_SCHEMA = "OTMRAR1"
MAX_BYTES = 65_536
MAX_DEPTH = 14
MAX_NODES = 4_096
MAX_STRING = 500
SIGNED_MIN = -(1 << 63)
SIGNED_MAX = (1 << 63) - 1
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$")
RESULT_ID = re.compile(r"^OT-[0-9]{3}-OT005-[A-Z0-9-]{1,64}-V[0-9]+$")
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(r"\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", re.IGNORECASE),
)

PARENT_BINDINGS = {
    "benchmark_plan": (
        "tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json",
        "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
        "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8",
    ),
    "predecessor_contract": (
        "tests/benchmarks/crypto/OT-123-OT005-MATCHED-RESOURCE-ACCOUNTING-CONTRACT-V0.json",
        "4023b35e0aecc6f4c8c5077bdd8646ad81df892c8816a1348bf1c97181c0cc03",
        "77e0656f286ee8e030c9add21bcbd07e753d78179a31e352f3ec9df000c344f5",
    ),
    "phase_two_reconciliation": (
        "tests/benchmarks/crypto/OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json",
        "beddc729f8449c3f2e3a09f62ba6947312f4e1893eee38abfa7a9a616f1bae1c",
        "dcc0b408fdcc02df70717cd93ac8e5133c7159563c15d1da0f74a6c1bb70d1d0",
    ),
}

TARGET = {
    "target_id": "heltec-v4-bench-candidate",
    "mcu": "ESP32-S3",
    "idf_target": "esp32s3",
    "flash_bytes": 16_777_216,
}
TOOLCHAIN = {
    "esp_idf_version": "v6.0.2",
    "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
    "compiler": "xtensa-esp32s3-elf-gcc",
    "compiler_version": "15.2.0",
    "cmake_version": "4.0.3",
    "ninja_version": "1.12.1",
    "python_version": "3.14.6",
    "esp_idf_size_version": "2.3.1",
    "esp_idf_size_module_sha256": (
        "ba38639de2a4d1f4fa48657e94cd3f250a3744ea8595e81fc74e9da0431a15c9"
    ),
}
CANDIDATES = [
    {
        "candidate_id": "espressif_libsodium",
        "role": "primary",
        "selection_eligible": True,
        "generated_sdkconfig_sha256": (
            "b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f"
        ),
    },
    {
        "candidate_id": "esp_idf_mbedtls_psa",
        "role": "comparison",
        "selection_eligible": False,
        "api_config_baseline_sha256": (
            "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686"
        ),
        "generated_sdkconfig_sha256": (
            "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e"
        ),
    },
    {
        "candidate_id": "monocypher",
        "role": "comparison",
        "selection_eligible": False,
        "generated_sdkconfig_sha256": (
            "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"
        ),
    },
]
MATCH_FIELDS = [
    "benchmark_harness_source",
    "frame_and_runtime_instrumentation",
    "generated_sdkconfig",
    "partition_layout",
    "idf_target",
    "esp_idf_commit",
    "compiler",
    "cmake",
    "ninja",
    "python",
    "esp_idf_size",
    "compile_flags",
    "project_version",
    "cache_policy",
    "component_manager_network_policy",
]
ONLY_DIFFERENCE = (
    "candidate_and_adapter_linkage_replaced_by_reviewed_no_candidate_control_bindings"
)
ARTIFACT_FIELDS = (
    "application_bin",
    "application_elf",
    "linker_map",
    "generated_sdkconfig",
    "partition_csv",
    "size_report",
)
FAIL_CLOSED_REJECTIONS = [
    "application_bin_file_length_as_linked_flash",
    "elf_or_map_file_length_as_linked_flash",
    "archive_or_symbol_anchor_size_as_linked_flash",
    "restored_trail_or_ot093_full_product_as_control",
    "absolute_data_plus_bss_presented_as_candidate_delta",
    "unmatched_sdkconfig_toolchain_harness_or_partition",
    "forced_positive_or_absolute_valued_delta",
    "missing_noinit_tdata_or_tbss_accounting",
    "legacy_otcb0_validator_used_for_signed_delta_admission",
    "missing_or_nonidentical_a_b_builds",
    "report_path_escape_hash_or_byte_mismatch",
    "duplicate_diram_or_tls_part",
    "nonexact_or_out_of_range_integer",
]


class ContractError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _need(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in items:
        if key in value:
            raise ContractError("JSON contains a duplicate key")
        value[key] = item
    return value


def _object(value: Any, path: str) -> dict[str, Any]:
    _need(type(value) is dict, f"{path} must be an exact object")
    return value


def _list(value: Any, path: str) -> list[Any]:
    _need(type(value) is list, f"{path} must be an exact list")
    return value


def _string(value: Any, path: str) -> str:
    _need(type(value) is str and 0 < len(value) <= MAX_STRING, f"{path} must be a bounded nonempty string")
    return value


def _integer(
    value: Any,
    path: str,
    *,
    minimum: int = 0,
    maximum: int = SIGNED_MAX,
) -> int:
    _need(type(value) is int and minimum <= value <= maximum, f"{path} must be an exact integer in range")
    return value


def _boolean(value: Any, path: str) -> bool:
    _need(type(value) is bool, f"{path} must be an exact Boolean")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    missing = sorted(expected - set(value))
    extra = sorted(set(value) - expected)
    _need(not missing and not extra, f"{path} keys differ; missing={missing}, extra={extra}")


def _scan_structure(value: Any) -> None:
    seen: set[int] = set()
    nodes = 0

    def visit(child: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        _need(nodes <= MAX_NODES and depth <= MAX_DEPTH, "artifact exceeds structural bounds")
        if type(child) is dict:
            identity = id(child)
            _need(identity not in seen and len(child) <= 64, "artifact contains a cycle or oversized object")
            seen.add(identity)
            for key, item in child.items():
                _need(type(key) is str, "artifact keys must be exact strings")
                visit(key, depth + 1)
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is list:
            identity = id(child)
            _need(identity not in seen and len(child) <= 64, "artifact contains a cycle or oversized list")
            seen.add(identity)
            for item in child:
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is str:
            _need(len(child) <= MAX_STRING, "artifact contains oversized text")
        else:
            _need(child is None or type(child) in (int, bool), "artifact contains a noncanonical JSON type")

    visit(value, 0)


def _scan_public(value: Any, path: str = "artifact") -> None:
    if type(value) is dict:
        forbidden = {
            "serial_number",
            "mac_address",
            "transport_port",
            "pairing_pin",
            "password",
            "private_key",
            "secret_value",
            "local_path",
        }
        found = forbidden.intersection(value)
        _need(not found, f"{path} contains prohibited fields")
        for key, item in value.items():
            _scan_public(item, f"{path}.{key}")
    elif type(value) is list:
        for index, item in enumerate(value):
            _scan_public(item, f"{path}[{index}]")
    elif type(value) is str:
        for pattern in PRIVATE_TEXT:
            _need(pattern.search(value) is None, f"{path} contains private machine or device text")


def _decode_json(raw: bytes, path: str) -> dict[str, Any]:
    _need(len(raw) <= MAX_BYTES, f"{path} exceeds the size limit")
    _need(not raw.startswith(b"\xef\xbb\xbf"), f"{path} must not contain a UTF-8 BOM")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ContractError:
        raise
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ContractError(f"{path} is unreadable or invalid") from exc
    result = _object(value, path)
    _scan_structure(result)
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
    except OSError as exc:
        raise ContractError("JSON is unreadable or invalid") from exc
    return _decode_json(raw, "artifact")


def canonical_sha256(value: dict[str, Any]) -> str:
    try:
        encoded = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    except (TypeError, ValueError, RecursionError) as exc:
        raise ContractError("artifact is not canonically serializable") from exc
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _raw_sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as exc:
        raise ContractError("bound artifact is unreadable") from exc


def _repo_path(root: Path, value: Any, path: str) -> Path:
    text = _string(value, path)
    _need("\\" not in text, f"{path} must use repository-relative POSIX separators")
    pure = PurePosixPath(text)
    _need(not pure.is_absolute() and ".." not in pure.parts and "." not in pure.parts, f"{path} escapes the repository")
    _need(pure.parts[:3] == ("tests", "benchmarks", "crypto"), f"{path} is outside the crypto evidence directory")
    root_resolved = root.resolve()
    candidate = root_resolved.joinpath(*pure.parts).resolve()
    try:
        candidate.relative_to(root_resolved)
    except ValueError as exc:
        raise ContractError(f"{path} escapes the repository") from exc
    return candidate


def _validate_parent_binding(root: Path, name: str, value: Any) -> None:
    binding = _object(value, f"contract.bindings.{name}")
    _exact_keys(binding, {"path", "raw_sha256", "canonical_sha256"}, f"contract.bindings.{name}")
    expected_path, expected_raw, expected_canonical = PARENT_BINDINGS[name]
    _need(binding == {"path": expected_path, "raw_sha256": expected_raw, "canonical_sha256": expected_canonical}, f"{name} binding drift")
    artifact_path = _repo_path(root, binding["path"], f"contract.bindings.{name}.path")
    _need(_raw_sha256(artifact_path) == expected_raw, f"{name} raw binding mismatch")
    _need(canonical_sha256(load_json(artifact_path)) == expected_canonical, f"{name} canonical binding mismatch")


def validate_contract(value: dict[str, Any], root: Path = ROOT) -> dict[str, Any]:
    _scan_structure(value)
    _scan_public(value)
    _exact_keys(
        value,
        {
            "schema", "version", "artifact_kind", "contract_id", "accepted_date",
            "status", "bindings", "target", "toolchain", "candidates",
            "matched_build_rules", "size_report", "formulas", "result_contract",
            "fail_closed_rejections", "authority", "claims",
        },
        "contract",
    )
    _need(value["schema"] == SCHEMA and value["version"] == VERSION, "contract schema/version mismatch")
    _need(value["artifact_kind"] == "generic_matched_candidate_resource_accounting_contract", "contract artifact kind mismatch")
    _need(value["contract_id"] == "OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1", "contract identity mismatch")
    _need(value["accepted_date"] == "2026-08-26" and value["status"] == "frozen_host_only_unexecuted", "contract state mismatch")

    bindings = _object(value["bindings"], "contract.bindings")
    _exact_keys(bindings, set(PARENT_BINDINGS), "contract.bindings")
    for name in PARENT_BINDINGS:
        _validate_parent_binding(root, name, bindings[name])

    _need(value["target"] == TARGET, "contract target drift")
    _need(value["toolchain"] == TOOLCHAIN, "contract toolchain drift")
    _need(value["candidates"] == CANDIDATES, "contract candidate set/order drift")
    _need(
        value["matched_build_rules"]
        == {
            "fresh_initially_absent_builds_per_side": 2,
            "candidate_and_control_must_match": MATCH_FIELDS,
            "only_permitted_difference": ONLY_DIFFERENCE,
            "compiler_warnings_required": 0,
            "ccache_allowed": False,
            "component_manager_network_allowed": False,
            "a_b_artifact_and_json2_equality_required": True,
        },
        "matched build rules drift",
    )
    _need(
        value["size_report"]
        == {
            "producer": "python -m esp_idf_size --format json2 --output-file output.json application.map",
            "format": "esp_idf_size_json2",
            "format_version": "1.2",
            "maximum_bytes": MAX_BYTES,
            "candidate_and_control_reports_retained_and_hash_bound": True,
            "unique_diram_layout_required": True,
            "missing_named_parts_count_as_zero": True,
            "duplicate_named_tls_parts_rejected": True,
        },
        "size report rules drift",
    )
    expected_formulas = {
        "linked_flash_candidate_bytes": "candidate_report.total_size",
        "linked_flash_control_bytes": "control_report.total_size",
        "linked_flash_delta_bytes": "linked_flash_candidate_bytes - linked_flash_control_bytes",
        "static_ram_candidate_bytes": "DIRAM(.data + .bss + .noinit) + all_layout_parts(.tdata + .tbss)",
        "static_ram_control_bytes": "DIRAM(.data + .bss + .noinit) + all_layout_parts(.tdata + .tbss)",
        "static_ram_delta_bytes": "static_ram_candidate_bytes - static_ram_control_bytes",
        "phase_two_projection.static_ram_bytes": "static_ram_candidate_bytes",
        "deltas_are_signed": True,
        "excluded_from_static_ram": ["DIRAM .text", "IRAM code", "RTC FAST", "RTC SLOW", "external RAM"],
    }
    _need(value["formulas"] == expected_formulas, "resource formulas drift")
    _need(
        value["result_contract"]
        == {
            "schema": RESULT_SCHEMA,
            "version": 1,
            "one_result_per_candidate": True,
            "report_paths_repo_relative": True,
            "raw_hash_and_byte_binding_required": True,
            "signed_integer_min": SIGNED_MIN,
            "signed_integer_max": SIGNED_MAX,
            "required_absolute_fields": [
                "linked_flash_candidate_bytes", "linked_flash_control_bytes",
                "static_ram_candidate_bytes", "static_ram_control_bytes",
            ],
            "required_signed_fields": ["linked_flash_delta_bytes", "static_ram_delta_bytes"],
            "phase_two_projection": {
                "linked_flash_delta_bytes": "linked_flash_delta_bytes",
                "static_ram_bytes": "static_ram_candidate_bytes",
            },
        },
        "result contract drift",
    )
    _need(
        value["fail_closed_rejections"] == FAIL_CLOSED_REJECTIONS,
        "fail-closed rejection set drift",
    )
    _need(
        value["authority"]
        == {
            "host_contract_validation_authorized": True,
            "matched_build_execution_authorized": False,
            "benchmark_execution_authorized": False,
            "device_access_authorized": False,
            "flash_authorized": False,
            "radio_transmit_authorized": False,
            "candidate_selection_authorized": False,
            "score_credit_added": False,
        },
        "contract authority drift",
    )
    _need(
        value["claims"]
        == {
            "successor_contract_frozen": True,
            "matched_controls_built": False,
            "resource_delta_admitted": False,
            "benchmark_executed": False,
            "hardware_or_device_accessed": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "score_credit_added": False,
        },
        "contract claims drift",
    )
    return {"schema": SCHEMA, "version": VERSION, "contract_canonical_sha256": canonical_sha256(value)}


def parse_size_report(value: dict[str, Any], flash_bytes: int = TARGET["flash_bytes"]) -> dict[str, int]:
    _scan_structure(value)
    _exact_keys(value, {"version", "total_size", "layout"}, "size_report")
    _need(value["version"] == "1.2", "size report version mismatch")
    total_size = _integer(value["total_size"], "size_report.total_size", maximum=flash_bytes)
    layout = _list(value["layout"], "size_report.layout")
    _need(0 < len(layout) <= 32, "size report layout count is invalid")
    diram_parts: dict[str, Any] | None = None
    tls_sizes: dict[str, int] = {}
    diram_count = 0
    for index, raw in enumerate(layout):
        item = _object(raw, f"size_report.layout[{index}]")
        _exact_keys(item, {"name", "total", "used", "free", "parts"}, f"size_report.layout[{index}]")
        name = _string(item["name"], f"size_report.layout[{index}].name")
        _integer(item["total"], f"size_report.layout[{index}].total")
        used = _integer(item["used"], f"size_report.layout[{index}].used")
        _integer(item["free"], f"size_report.layout[{index}].free")
        parts = _object(item["parts"], f"size_report.layout[{index}].parts")
        _need(len(parts) <= 64, "size report contains too many parts")
        part_total = 0
        for part_name, raw_part in parts.items():
            _string(part_name, f"size_report.layout[{index}].part_name")
            part = _object(raw_part, f"size_report.layout[{index}].parts.{part_name}")
            _exact_keys(part, {"size"}, f"size_report.layout[{index}].parts.{part_name}")
            size = _integer(part["size"], f"size_report.layout[{index}].parts.{part_name}.size")
            part_total += size
            _need(part_total <= SIGNED_MAX, "size report part sum overflows")
            if part_name in (".tdata", ".tbss"):
                _need(part_name not in tls_sizes, "duplicate TLS part is ambiguous")
                tls_sizes[part_name] = size
        _need(part_total == used, f"size_report.layout[{index}] part sum does not equal used")
        if name == "DIRAM":
            diram_count += 1
            diram_parts = parts
    _need(diram_count == 1 and diram_parts is not None, "size report must contain one unique DIRAM layout")
    static_ram = sum(
        _integer(diram_parts.get(name, {"size": 0})["size"], f"size_report.DIRAM.{name}.size")
        for name in (".data", ".bss", ".noinit")
    ) + tls_sizes.get(".tdata", 0) + tls_sizes.get(".tbss", 0)
    _need(static_ram <= SIGNED_MAX, "static RAM sum overflows")
    return {"linked_flash_bytes": total_size, "static_ram_bytes": static_ram}


def _report(binding_value: Any, root: Path, path: str) -> tuple[dict[str, int], bytes]:
    binding = _object(binding_value, path)
    _exact_keys(binding, {"path", "bytes", "raw_sha256"}, path)
    report_path = _repo_path(root, binding["path"], f"{path}.path")
    try:
        with report_path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
    except OSError as exc:
        raise ContractError("bound size report is unreadable") from exc
    _need(len(raw) <= MAX_BYTES, "bound size report exceeds the size limit")
    _need(_integer(binding["bytes"], f"{path}.bytes", maximum=MAX_BYTES) == len(raw), "size report byte binding mismatch")
    digest = _string(binding["raw_sha256"], f"{path}.raw_sha256")
    _need(HEX64.fullmatch(digest) is not None and hashlib.sha256(raw).hexdigest() == digest, "size report digest binding mismatch")
    return parse_size_report(_decode_json(raw, path)), raw


def _artifact(value: Any, path: str) -> dict[str, Any]:
    artifact = _object(value, path)
    _exact_keys(artifact, {"bytes", "sha256"}, path)
    _integer(artifact["bytes"], f"{path}.bytes", minimum=1)
    digest = _string(artifact["sha256"], f"{path}.sha256")
    _need(HEX64.fullmatch(digest) is not None, f"{path}.sha256 must be lowercase SHA-256")
    return artifact


def _metadata(value: Any, candidate: dict[str, Any], path: str) -> dict[str, str]:
    metadata = _object(value, path)
    _exact_keys(metadata, set(MATCH_FIELDS), path)
    for name in MATCH_FIELDS:
        _string(metadata[name], f"{path}.{name}")
    for name in ("benchmark_harness_source", "frame_and_runtime_instrumentation", "partition_layout", "compile_flags"):
        _need(HEX64.fullmatch(metadata[name]) is not None, f"{path}.{name} must be lowercase SHA-256")
    expected = {
        "generated_sdkconfig": candidate["generated_sdkconfig_sha256"],
        "idf_target": TARGET["idf_target"],
        "esp_idf_commit": TOOLCHAIN["esp_idf_commit"],
        "compiler": f"{TOOLCHAIN['compiler']} {TOOLCHAIN['compiler_version']}",
        "cmake": TOOLCHAIN["cmake_version"],
        "ninja": TOOLCHAIN["ninja_version"],
        "python": TOOLCHAIN["python_version"],
        "esp_idf_size": TOOLCHAIN["esp_idf_size_version"],
        "cache_policy": "disabled",
        "component_manager_network_policy": "disabled",
    }
    for name, expected_value in expected.items():
        _need(metadata[name] == expected_value, f"{path}.{name} does not match the contract")
    return metadata


def _build_side(value: Any, candidate: dict[str, Any], path: str) -> dict[str, Any]:
    side = _object(value, path)
    _exact_keys(side, {"matched_metadata", "linkage_manifest_sha256", "runs"}, path)
    metadata = _metadata(side["matched_metadata"], candidate, f"{path}.matched_metadata")
    linkage = _string(side["linkage_manifest_sha256"], f"{path}.linkage_manifest_sha256")
    _need(HEX64.fullmatch(linkage) is not None, f"{path}.linkage_manifest_sha256 must be lowercase SHA-256")
    runs = _list(side["runs"], f"{path}.runs")
    _need(len(runs) == 2, f"{path} must contain exactly two runs")
    normalized: list[dict[str, Any]] = []
    raw_logs: list[str] = []
    for index, raw_run in enumerate(runs):
        run_path = f"{path}.runs[{index}]"
        run = _object(raw_run, run_path)
        _exact_keys(run, {"run", "initial_build_directory_absent", "compiler_warnings", "raw_build_log_sha256", "normalized_receipt_sha256", "artifacts"}, run_path)
        _need(run["run"] == ("A" if index == 0 else "B"), f"{run_path}.run order mismatch")
        _need(_boolean(run["initial_build_directory_absent"], f"{run_path}.initial_build_directory_absent"), f"{run_path} was not fresh")
        _need(_integer(run["compiler_warnings"], f"{run_path}.compiler_warnings") == 0, f"{run_path} contains compiler warnings")
        raw_log = _string(run["raw_build_log_sha256"], f"{run_path}.raw_build_log_sha256")
        receipt = _string(run["normalized_receipt_sha256"], f"{run_path}.normalized_receipt_sha256")
        _need(HEX64.fullmatch(raw_log) is not None and HEX64.fullmatch(receipt) is not None, f"{run_path} contains an invalid digest")
        artifacts = _object(run["artifacts"], f"{run_path}.artifacts")
        _exact_keys(artifacts, set(ARTIFACT_FIELDS), f"{run_path}.artifacts")
        for field in ARTIFACT_FIELDS:
            _artifact(artifacts[field], f"{run_path}.artifacts.{field}")
        _need(artifacts["generated_sdkconfig"]["sha256"] == candidate["generated_sdkconfig_sha256"], f"{run_path} sdkconfig artifact mismatch")
        normalized.append({"normalized_receipt_sha256": receipt, "artifacts": artifacts})
        raw_logs.append(raw_log)
    _need(normalized[0] == normalized[1], f"{path} A/B normalized artifacts differ")
    _need(raw_logs[0] != raw_logs[1], f"{path} A/B raw build logs must be independently retained")
    return {"matched_metadata": metadata, "linkage_manifest_sha256": linkage, "runs": runs}


def validate_result(
    contract: dict[str, Any],
    result: dict[str, Any],
    *,
    root: Path = ROOT,
    contract_path: Path | None = None,
) -> dict[str, Any]:
    contract_info = validate_contract(contract, ROOT)
    _scan_structure(result)
    _scan_public(result)
    _exact_keys(
        result,
        {
            "schema", "version", "artifact_kind", "result_id", "recorded_date",
            "status", "contract", "candidate_id", "candidate_role",
            "selection_eligible", "builds", "reports", "measurements",
            "phase_two_projection", "claims",
        },
        "result",
    )
    _need(result["schema"] == RESULT_SCHEMA and result["version"] == 1, "result schema/version mismatch")
    _need(result["artifact_kind"] == "matched_candidate_resource_accounting_result", "result artifact kind mismatch")
    _need(RESULT_ID.fullmatch(_string(result["result_id"], "result.result_id")) is not None, "result identity is invalid")
    _need(DATE.fullmatch(_string(result["recorded_date"], "result.recorded_date")) is not None, "result date is invalid")
    _need(result["status"] == "matched_resource_result_admitted", "result status mismatch")

    binding = _object(result["contract"], "result.contract")
    _exact_keys(binding, {"path", "raw_sha256", "canonical_sha256"}, "result.contract")
    _need(binding["path"] == CONTRACT_REPO_PATH, "result contract path mismatch")
    actual_contract_path = contract_path or ROOT.joinpath(*PurePosixPath(CONTRACT_REPO_PATH).parts)
    _need(_raw_sha256(actual_contract_path) == binding["raw_sha256"], "result contract raw binding mismatch")
    _need(contract_info["contract_canonical_sha256"] == binding["canonical_sha256"], "result contract canonical binding mismatch")

    candidate_id = _string(result["candidate_id"], "result.candidate_id")
    candidates = {item["candidate_id"]: item for item in CANDIDATES}
    _need(candidate_id in candidates, "result candidate is not admitted")
    candidate = candidates[candidate_id]
    _need(result["candidate_role"] == candidate["role"], "result candidate role mismatch")
    _need(_boolean(result["selection_eligible"], "result.selection_eligible") is candidate["selection_eligible"], "result selection eligibility mismatch")

    builds = _object(result["builds"], "result.builds")
    _exact_keys(builds, {"candidate", "control", "only_permitted_difference"}, "result.builds")
    _need(builds["only_permitted_difference"] == ONLY_DIFFERENCE, "result build difference boundary mismatch")
    candidate_build = _build_side(builds["candidate"], candidate, "result.builds.candidate")
    control_build = _build_side(builds["control"], candidate, "result.builds.control")
    _need(candidate_build["matched_metadata"] == control_build["matched_metadata"], "candidate/control matched metadata differ")
    _need(candidate_build["linkage_manifest_sha256"] != control_build["linkage_manifest_sha256"], "candidate/control linkage manifests must be distinct")
    all_raw_logs = [
        run["raw_build_log_sha256"]
        for build in (candidate_build, control_build)
        for run in build["runs"]
    ]
    _need(len(set(all_raw_logs)) == 4, "the four matched builds require distinct raw logs")

    reports = _object(result["reports"], "result.reports")
    _exact_keys(reports, {"candidate", "control"}, "result.reports")
    _need(
        reports["candidate"].get("path") != reports["control"].get("path"),
        "candidate and control reports require distinct retained paths",
    )
    candidate_metrics, _ = _report(reports["candidate"], root, "result.reports.candidate")
    control_metrics, _ = _report(reports["control"], root, "result.reports.control")
    for side_name, side, build in (
        ("candidate", reports["candidate"], candidate_build),
        ("control", reports["control"], control_build),
    ):
        report_artifact = build["runs"][0]["artifacts"]["size_report"]
        _need(report_artifact["bytes"] == side["bytes"] and report_artifact["sha256"] == side["raw_sha256"], f"{side_name} report does not match its A/B build tuple")

    expected_measurements = {
        "linked_flash_candidate_bytes": candidate_metrics["linked_flash_bytes"],
        "linked_flash_control_bytes": control_metrics["linked_flash_bytes"],
        "linked_flash_delta_bytes": candidate_metrics["linked_flash_bytes"] - control_metrics["linked_flash_bytes"],
        "static_ram_candidate_bytes": candidate_metrics["static_ram_bytes"],
        "static_ram_control_bytes": control_metrics["static_ram_bytes"],
        "static_ram_delta_bytes": candidate_metrics["static_ram_bytes"] - control_metrics["static_ram_bytes"],
    }
    measurements = _object(result["measurements"], "result.measurements")
    _exact_keys(measurements, set(expected_measurements), "result.measurements")
    for field in (
        "linked_flash_candidate_bytes", "linked_flash_control_bytes",
        "static_ram_candidate_bytes", "static_ram_control_bytes",
    ):
        _integer(measurements[field], f"result.measurements.{field}")
    for field in ("linked_flash_delta_bytes", "static_ram_delta_bytes"):
        _integer(measurements[field], f"result.measurements.{field}", minimum=SIGNED_MIN, maximum=SIGNED_MAX)
    _need(measurements == expected_measurements, "result measurements do not equal report recomputation")

    projection = _object(result["phase_two_projection"], "result.phase_two_projection")
    _exact_keys(projection, {"linked_flash_delta_bytes", "static_ram_bytes"}, "result.phase_two_projection")
    _integer(projection["linked_flash_delta_bytes"], "result.phase_two_projection.linked_flash_delta_bytes", minimum=SIGNED_MIN, maximum=SIGNED_MAX)
    _integer(projection["static_ram_bytes"], "result.phase_two_projection.static_ram_bytes")
    _need(
        projection
        == {
            "linked_flash_delta_bytes": expected_measurements["linked_flash_delta_bytes"],
            "static_ram_bytes": expected_measurements["static_ram_candidate_bytes"],
        },
        "Phase 2 projection does not preserve frozen semantics",
    )
    _need(
        result["claims"]
        == {
            "resource_delta_admitted": True,
            "benchmark_executed": False,
            "hardware_or_device_accessed": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "score_credit_added": False,
        },
        "result claims exceed matched resource admission",
    )
    return {
        "schema": RESULT_SCHEMA,
        "version": 1,
        "candidate_id": candidate_id,
        "verdict": "pass",
        "measurements": expected_measurements,
        "result_canonical_sha256": canonical_sha256(result),
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    contract_cmd = sub.add_parser("validate-contract")
    contract_cmd.add_argument("contract", type=Path)
    result_cmd = sub.add_parser("evaluate")
    result_cmd.add_argument("contract", type=Path)
    result_cmd.add_argument("result", type=Path)
    args = parser.parse_args(argv)
    try:
        contract = load_json(args.contract)
        if args.command == "validate-contract":
            output = validate_contract(contract)
        else:
            output = validate_result(
                contract,
                load_json(args.result),
                contract_path=args.contract.resolve(),
            )
        print(json.dumps(output, sort_keys=True))
        return 0
    except (ContractError, OSError, ValueError, TypeError, RecursionError):
        print("ERROR: matched resource evidence is invalid or unaccepted", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
