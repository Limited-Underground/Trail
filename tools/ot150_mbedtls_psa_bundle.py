#!/usr/bin/env python3
"""Build and validate the host-only OT-150 mbedTLS/PSA bundle freeze.

This module only reads caller-supplied files and returns deterministic JSON
data.  It cannot write an output, execute a command, discover an endpoint, or
grant benchmark authority.  A separately accepted owner authority remains
mandatory before any later physical attempt.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-27"
PREPARATION_SCHEMA = "OT150MERBP0"
PREPARATION_ID = (
    "OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0"
)
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0.json"
)
RESOURCE_RESULT_RELATIVE = (
    "tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json"
)
CANDIDATE_REPORT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json"
)
CONTROL_REPORT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json"
)
PROJECT_VER = "ot150-mbedtls-psa-v0"
APPLICATION_OFFSET = 0x10000

RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 500_944
RESTORE_SHA256 = "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e"

GENERATED_SDKCONFIG_SHA256 = (
    "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e"
)

FIXED_BINDINGS: tuple[tuple[str, str, int, str, str | None], ...] = (
    (
        "benchmark_plan",
        "tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json",
        7_891,
        "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
        "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8",
    ),
    (
        "ot149_preparation",
        "tests/benchmarks/crypto/OT-149-OT005-MBEDTLS-PSA-TARGET-PREPARATION-V0.json",
        10_546,
        "49d1cfba5fca01afeb1928be922eec287873b85b1d517c81c249e148331b8867",
        "25007391dea93761c2a4afa727548cb097cda9ee332221867e865ae0d9d3de6c",
    ),
    (
        "resource_successor",
        "tests/benchmarks/crypto/OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json",
        6_631,
        "575efa5700bf2d2e40cf3fb49c0b9f860815dddc3ea81bb5a54a3d91733339bd",
        "1c44d3d6f0c0d7c38ce83c51f1b79c75130455f2bbf99fed7edb4ffa1b9efaf2",
    ),
    (
        "frame_parser",
        "tools/ot149_mbedtls_psa_frames.py",
        12_472,
        "a431e45c8ab2098d6973171f4c325c7f5d7e2b6f183e01b3422086dbac6d15c9",
        None,
    ),
    (
        "frame_schema",
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/mbedtls-psa-result-frame.schema.json",
        7_512,
        "f24496e3151a725682838e791810b41401bd06e36db6ad9dbca89e9ee7aac2dd",
        "1962b1c65471d3766ae2e152d6f888412afd6050320082f3e088682a242e1933",
    ),
)

REQUIRED_ARTIFACT_ROLES = (
    "application_bin",
    "application_elf",
    "linker_map",
    "bootloader_bin",
    "partition_table_bin",
    "generated_sdkconfig",
    "size_report_json2",
)
COMMON_ARTIFACT_ROLES = (
    "bootloader_bin",
    "partition_table_bin",
    "generated_sdkconfig",
)
DISTINCT_LINKAGE_ROLES = ("application_bin", "application_elf", "linker_map")
OPERATIONS = (
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
)
UNAVAILABLE_OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "noise_xk_handshake",
)
MAX_JSON2_BYTES = 65_536
_SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\Z")
_SAFE_ROLE = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")

RUNTIME_BINDINGS = (
    ("bundle_validator", "tools/ot150_mbedtls_psa_bundle.py"),
    ("coordinator", "tools/ot150_mbedtls_psa_coordinator.py"),
    ("execution_authority_tool", "tools/ot150_mbedtls_psa_execution_authority.py"),
    ("hardware_adapter", "tools/ot150_mbedtls_psa_hardware_adapter.py"),
    (
        "matched_resource_validator",
        "tools/crypto_matched_resource_accounting.py",
    ),
    ("protocol_transport", "tools/ot150_mbedtls_psa_protocol_runner.py"),
)


class ContractError(ValueError):
    """A fail-closed public contract error without private path detail."""


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ContractError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_: str) -> None:
    raise ContractError("invalid JSON constant")


def canonical_bytes(value: Any) -> bytes:
    try:
        return json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
            allow_nan=False,
        ).encode("ascii")
    except (TypeError, ValueError, UnicodeError) as exc:
        raise ContractError("invalid canonical JSON") from exc


def canonical_document(value: Any) -> bytes:
    return canonical_bytes(value) + b"\n"


def decode_canonical(raw: bytes, label: str) -> dict[str, Any]:
    if raw.startswith(b"\xef\xbb\xbf"):
        raise ContractError(f"{label} contains BOM")
    try:
        value = json.loads(
            raw.decode("ascii"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict:
        raise ContractError(f"{label} shape mismatch")
    if raw != canonical_document(value):
        raise ContractError(f"{label} is not canonical")
    return value


def _decode_json(raw: bytes, label: str) -> dict[str, Any]:
    if raw.startswith(b"\xef\xbb\xbf"):
        raise ContractError(f"{label} contains BOM")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict:
        raise ContractError(f"{label} shape mismatch")
    return value


def _safe_file(path: Path, label: str) -> bytes:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ContractError(f"{label} unavailable")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _repo_relative(path: Path, label: str) -> str:
    try:
        relative = path.resolve().relative_to(ROOT.resolve()).as_posix()
    except (OSError, ValueError) as exc:
        raise ContractError(f"{label} must be repository relative") from exc
    if not relative or relative.startswith("../") or ":" in relative:
        raise ContractError(f"{label} must be repository relative")
    return relative


def _canonical_sha256(value: Any) -> str:
    return _sha256(canonical_bytes(value))


def _fixed_binding(
    relative: str,
    expected_bytes: int,
    expected_raw: str,
    expected_canonical: str | None,
) -> dict[str, Any]:
    raw = _safe_file((ROOT / relative).resolve(), "fixed binding")
    if len(raw) != expected_bytes or _sha256(raw) != expected_raw:
        raise ContractError("fixed binding digest mismatch")
    value: dict[str, Any] = {
        "path": relative,
        "bytes": expected_bytes,
        "raw_sha256": expected_raw,
    }
    if expected_canonical is not None:
        parsed = _decode_json(raw, "fixed JSON binding")
        if _canonical_sha256(parsed) != expected_canonical:
            raise ContractError("fixed binding canonical digest mismatch")
        value["canonical_sha256"] = expected_canonical
    return value


def fixed_bindings() -> dict[str, Any]:
    return {
        role: _fixed_binding(relative, size, raw, canonical)
        for role, relative, size, raw, canonical in FIXED_BINDINGS
    }


def _validate_json2(raw: bytes) -> None:
    if not raw or len(raw) > MAX_JSON2_BYTES:
        raise ContractError("JSON2 report size mismatch")
    value = _decode_json(raw, "JSON2 report")
    if value.get("version") != "1.2":
        raise ContractError("JSON2 report version mismatch")
    if set(value) != {"version", "total_size", "layout"}:
        raise ContractError("JSON2 report structure mismatch")
    total = value["total_size"]
    layout = value["layout"]
    if (
        type(total) is not int
        or not 0 <= total <= 16_777_216
        or type(layout) is not list
        or not 0 < len(layout) <= 32
    ):
        raise ContractError("JSON2 report structure mismatch")
    diram_count = 0
    tls_parts: set[str] = set()
    for region in layout:
        if type(region) is not dict or set(region) != {
            "name",
            "total",
            "used",
            "free",
            "parts",
        }:
            raise ContractError("JSON2 report structure mismatch")
        if (
            type(region["name"]) is not str
            or not region["name"]
            or any(type(region[key]) is not int or region[key] < 0 for key in ("total", "used", "free"))
            or type(region["parts"]) is not dict
            or len(region["parts"]) > 64
        ):
            raise ContractError("JSON2 report structure mismatch")
        part_total = 0
        for name, part in region["parts"].items():
            if (
                type(name) is not str
                or not name
                or type(part) is not dict
                or set(part) != {"size"}
                or type(part["size"]) is not int
                or part["size"] < 0
            ):
                raise ContractError("JSON2 report structure mismatch")
            part_total += part["size"]
            if name in (".tdata", ".tbss"):
                if name in tls_parts:
                    raise ContractError("JSON2 report duplicate TLS part")
                tls_parts.add(name)
        if part_total != region["used"]:
            raise ContractError("JSON2 report part sum mismatch")
        if region["name"] == "DIRAM":
            diram_count += 1
    if diram_count != 1:
        raise ContractError("JSON2 report requires one unique DIRAM")


def _private_descriptor(path: Path, role: str) -> dict[str, Any]:
    raw = _safe_file(path, f"{role} artifact")
    name = path.name
    if not _SAFE_NAME.fullmatch(name):
        raise ContractError("artifact name is not privacy safe")
    if role == "size_report_json2":
        _validate_json2(raw)
    return {"name": name, "bytes": len(raw), "sha256": _sha256(raw)}


def _run_descriptors(
    side: str, runs: Mapping[str, Mapping[str, Path]]
) -> list[dict[str, Any]]:
    if type(runs) is not dict or set(runs) != {"A", "B"}:
        raise ContractError(f"{side} requires exact A/B runs")
    output: list[dict[str, Any]] = []
    resolved_by_run: dict[str, dict[str, Path]] = {}
    for run in ("A", "B"):
        artifacts = runs[run]
        if type(artifacts) is not dict or set(artifacts) != set(REQUIRED_ARTIFACT_ROLES):
            raise ContractError(f"{side} artifact role mismatch")
        resolved_by_run[run] = {}
        descriptors: dict[str, Any] = {}
        for role in REQUIRED_ARTIFACT_ROLES:
            path = artifacts[role]
            if not isinstance(path, Path):
                raise ContractError(f"{side} artifact path mismatch")
            resolved_by_run[run][role] = path.resolve()
            descriptors[role] = _private_descriptor(path, role)
        output.append({"run": run, "artifacts": descriptors})
    for role in REQUIRED_ARTIFACT_ROLES:
        if resolved_by_run["A"][role] == resolved_by_run["B"][role]:
            raise ContractError(f"{side} A/B paths are not independent")
        if output[0]["artifacts"][role] != output[1]["artifacts"][role]:
            raise ContractError(f"{side} A/B artifacts differ")
    return output


def _runtime_descriptors(runtime_bindings: Mapping[str, Path]) -> dict[str, Any]:
    if (
        type(runtime_bindings) is not dict
        or set(runtime_bindings) != {role for role, unused_path in RUNTIME_BINDINGS}
    ):
        raise ContractError("runtime binding set mismatch")
    output: dict[str, Any] = {}
    identities: set[Path] = set()
    for role, expected_relative in RUNTIME_BINDINGS:
        if not _SAFE_ROLE.fullmatch(role):
            raise ContractError("runtime role mismatch")
        path = runtime_bindings[role]
        if not isinstance(path, Path):
            raise ContractError("runtime path mismatch")
        resolved = path.resolve()
        if resolved in identities:
            raise ContractError("runtime binding identity reused")
        identities.add(resolved)
        raw = _safe_file(path, "runtime binding")
        relative = _repo_relative(path, "runtime binding")
        if relative != expected_relative:
            raise ContractError("runtime binding path mismatch")
        output[role] = {
            "path": relative,
            "bytes": len(raw),
            "raw_sha256": _sha256(raw),
        }
    return output


def _claims() -> dict[str, bool]:
    return {
        "executable_resource_bundle_prepared": True,
        "execution_authorized": False,
        "benchmark_execution_authorized": False,
        "hardware_or_device_accessed": False,
        "firmware_flashed": False,
        "radio_used": False,
        "matched_resource_result_admitted": False,
        "candidate_selected": False,
        "suite_selected": False,
        "phase_two_complete": False,
        "phase_three_complete": False,
        "field_ready_proven": False,
        "score_credit_added": False,
    }


def _authority() -> dict[str, bool]:
    return {
        "one_attempt_authority_created": False,
        "benchmark_execution_authorized": False,
        "device_access_authorized": False,
        "serial_access_authorized": False,
        "reset_authorized": False,
        "flash_authorized": False,
        "radio_transmit_authorized": False,
        "key_or_entropy_operation_authorized": False,
        "matched_resource_admission_authorized": False,
        "candidate_selection_authorized": False,
        "phase_two_completion_authorized": False,
        "score_credit_authorized": False,
    }


def _build_preparation(
    candidate_runs: Mapping[str, Mapping[str, Path]],
    control_runs: Mapping[str, Mapping[str, Path]],
    restore_path: Path,
    runtime_bindings: Mapping[str, Path],
) -> dict[str, Any]:
    candidate = _run_descriptors("candidate", candidate_runs)
    control = _run_descriptors("control", control_runs)
    candidate_artifacts = candidate[0]["artifacts"]
    control_artifacts = control[0]["artifacts"]

    for role in COMMON_ARTIFACT_ROLES:
        if candidate_artifacts[role] != control_artifacts[role]:
            raise ContractError("candidate/control common artifacts differ")
    if candidate_artifacts["generated_sdkconfig"]["sha256"] != GENERATED_SDKCONFIG_SHA256:
        raise ContractError("generated sdkconfig digest mismatch")
    for role in DISTINCT_LINKAGE_ROLES:
        if candidate_artifacts[role]["sha256"] == control_artifacts[role]["sha256"]:
            raise ContractError("candidate/control linkage distinction absent")

    restore_raw = _safe_file(restore_path, "Trail restoration image")
    if (
        restore_path.name != RESTORE_NAME
        or len(restore_raw) != RESTORE_BYTES
        or _sha256(restore_raw) != RESTORE_SHA256
    ):
        raise ContractError("Trail restoration image mismatch")

    future_write = {
        "role": "candidate_application_bin",
        "name": candidate_artifacts["application_bin"]["name"],
        "bytes": candidate_artifacts["application_bin"]["bytes"],
        "sha256": candidate_artifacts["application_bin"]["sha256"],
        "application_offset": APPLICATION_OFFSET,
        "future_writable_after_separate_authority_only": True,
        "control_application_writable": False,
        "other_artifacts_writable": False,
    }

    return {
        "schema": PREPARATION_SCHEMA,
        "version": 0,
        "artifact_kind": "mbedtls_psa_executable_resource_bundle_preparation",
        "preparation_id": PREPARATION_ID,
        "recorded_date": RECORDED_DATE,
        "status": "host_bundle_frozen_fresh_owner_authority_required",
        "bindings": fixed_bindings(),
        "candidate": {
            "id": "esp_idf_mbedtls_psa",
            "version": "4.1.0",
            "role": "comparison",
            "selection_eligible": False,
            "operations": list(OPERATIONS),
            "unavailable_operations": list(UNAVAILABLE_OPERATIONS),
        },
        "build_policy": {
            "project_version": PROJECT_VER,
            "run_order": ["A", "B"],
            "a_b_byte_and_hash_equality_required_per_side": True,
            "candidate_control_common_artifacts_equal": list(COMMON_ARTIFACT_ROLES),
            "candidate_control_linkage_artifacts_distinct": list(DISTINCT_LINKAGE_ROLES),
            "json2_format": "esp_idf_size_json2_v1.2",
            "resource_result_admitted_by_this_preparation": False,
        },
        "builds": {"candidate": candidate, "control": control},
        "future_public_outputs": {
            "canonical_preparation": {
                "path": PREPARATION_RELATIVE,
                "schema": PREPARATION_SCHEMA,
            },
            "matched_resource_result": {
                "path": RESOURCE_RESULT_RELATIVE,
                "schema": "OTMRAR1",
                "admitted": False,
            },
            "json2_reports": {
                "candidate": {
                    "path": CANDIDATE_REPORT_RELATIVE,
                    "format": "esp_idf_size_json2_v1.2",
                },
                "control": {
                    "path": CONTROL_REPORT_RELATIVE,
                    "format": "esp_idf_size_json2_v1.2",
                },
            },
        },
        "runtime": _runtime_descriptors(runtime_bindings),
        "images": {
            "future_benchmark_write": future_write,
            "restore": {
                "name": RESTORE_NAME,
                "bytes": RESTORE_BYTES,
                "sha256": RESTORE_SHA256,
                "exact_readback_and_restore_required_by_future_authority": True,
            },
        },
        "privacy": {
            "build_artifacts_record_name_only": True,
            "runtime_and_fixed_bindings_repo_relative_only": True,
            "absolute_paths_recorded": False,
            "serial_ports_recorded": False,
            "device_identifiers_recorded": False,
            "raw_capture_recorded": False,
        },
        "authority": _authority(),
        "claims": _claims(),
    }


def build_preparation(
    candidate_runs: Mapping[str, Mapping[str, Path]],
    control_runs: Mapping[str, Mapping[str, Path]],
    restore_path: Path,
    runtime_bindings: Mapping[str, Path],
) -> dict[str, Any]:
    """Build an in-memory canonical preparation without persisting authority."""
    return _build_preparation(candidate_runs, control_runs, restore_path, runtime_bindings)


def validate_preparation(
    value: dict[str, Any],
    candidate_runs: Mapping[str, Mapping[str, Path]],
    control_runs: Mapping[str, Mapping[str, Path]],
    restore_path: Path,
    runtime_bindings: Mapping[str, Path],
) -> dict[str, Any]:
    expected = _build_preparation(
        candidate_runs, control_runs, restore_path, runtime_bindings
    )
    if value != expected:
        raise ContractError("preparation boundary mismatch")
    return {
        "schema": PREPARATION_SCHEMA,
        "canonical_sha256": _canonical_sha256(value),
        "executable_resource_bundle_prepared": True,
        "execution_authorized": False,
        "resource_result_admitted": False,
        "phase_two_complete": False,
    }
