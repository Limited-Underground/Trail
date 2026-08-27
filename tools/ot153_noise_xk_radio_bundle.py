#!/usr/bin/env python3
"""Build and validate the host-only OT-153 Noise XK radio bundle.

The module is deliberately pure host preparation.  It reads caller-supplied
build and restoration artifacts, emits only privacy-safe descriptors, and
cannot discover endpoints, flash hardware, transmit radio frames, or grant
execution authority.  A separately accepted, non-reusable owner authority is
required before any physical attempt.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-27"
SCHEMA = "OT153NXBP0"
PREPARATION_ID = (
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
EXPECTED_RECORD_RAW_SHA256 = "84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b"
EXPECTED_RECORD_CANONICAL_SHA256 = "06de67211443c1432336a0e1d16f4d62be58d870e93cc4b70c2d494c199bcfd9"

OT152_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0.json"
)
OT152_RAW_SHA256 = "f556e9c70a4e46afc12d4ff7cbfd3ea8ad95f9b3055313a1ffbbcefe21611fb4"
OT152_CANONICAL_SHA256 = "aba89cdc3e295ee33c73bb477a2d97ae758386142eb1c15f2a429aec1297c715"

ESP_IDF_VERSION = "v6.0.2"
ESP_IDF_COMMIT = "7101770dc6db2667b3c477cc31365dd1acd6db4e"
PROJECT_VERSION = "ot153-noise-xk-radio-v0"
RADIOLIB_VERSION = "7.7.1"
RADIOLIB_COMPONENT_SHA256 = (
    "024269f750d7eb181d07bd57ccec5e5e4e276dbb262d681a6501c8e2244e80f0"
)
LIBSODIUM_VERSION = "1.0.22"
LIBSODIUM_COMPONENT_SHA256 = (
    "39c9dc77d81804d54a539c8f076faed165152be7720ddd0e721acb9daf4aa5af"
)

RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 500_944
RESTORE_SHA256 = "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e"
APPLICATION_OFFSET = 0x10000

TARGET_BINDINGS = (
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/.gitignore",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/dependencies.lock",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/sdkconfig.defaults",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/idf_component.yml",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp",
)
SOURCE_BINDINGS = (
    (
        "noise_adapter_header",
        "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.h",
        "b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45",
    ),
    (
        "noise_adapter_source",
        "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c",
        "8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8",
    ),
    (
        "radio_hal_header",
        "firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.hpp",
        None,
    ),
    (
        "radio_hal_source",
        "firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp",
        None,
    ),
)
RUNTIME_BINDINGS = (
    ("runner", "tools/ot153_noise_xk_radio_runner.py"),
    ("coordinator", "tools/ot153_noise_xk_radio_coordinator.py"),
    ("hardware_adapter", "tools/ot153_noise_xk_radio_hardware_adapter.py"),
)
BUILD_ARTIFACTS = (
    ("bootloader_bin", "bootloader.bin"),
    ("partition_table_bin", "partition-table.bin"),
    ("application_bin", "ot153_noise_xk_radio_cost.bin"),
    ("application_elf", "ot153_noise_xk_radio_cost.elf"),
)

MESSAGE_WIRE_BYTES = (48, 48, 64)
TOTAL_RADIO_BYTES = 736
TOTAL_TRANSMISSIONS = 14
TOTAL_THEORETICAL_AIRTIME_US = 1_447_424
MAX_RECORD_BYTES = 131_072
MAX_DEPTH = 20
MAX_ITEMS = 4_096
_PRIVATE_TEXT = re.compile(
    r"(?:[A-Za-z]:[\\/]|/Users/|/home/|COM\d+|tty(?:USB|ACM)\d+|"
    r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2})",
    re.IGNORECASE,
)
_PRIVATE_KEYS = {
    "absolute_path", "private_path", "serial_port", "endpoint", "device_id",
    "device_identifier", "mac", "mac_address", "raw_payload", "secret", "private_key",
}
_SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\Z")


class ContractError(ValueError):
    """A stable, privacy-safe OT-153 preparation failure."""


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
    raise ContractError("non-finite number")


def _scan(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise ContractError("structure too deep")
    if isinstance(value, str):
        if len(value) > 4_096 or _PRIVATE_TEXT.search(value):
            raise ContractError("private or unsafe text")
        return 1
    if value is None or type(value) in (bool, int):
        return 1
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ContractError("non-finite number")
        return 1
    if isinstance(value, list):
        count = 1 + sum(_scan(item, depth + 1) for item in value)
    elif isinstance(value, dict):
        lowered = {str(key).lower() for key in value}
        if lowered & _PRIVATE_KEYS:
            raise ContractError("private field prohibited")
        count = 1 + sum(
            _scan(key, depth + 1) + _scan(item, depth + 1)
            for key, item in value.items()
        )
    else:
        raise ContractError("unsupported value")
    if count > MAX_ITEMS:
        raise ContractError("structure too large")
    return count


def canonical_bytes(value: Any) -> bytes:
    _scan(value)
    try:
        return json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
            allow_nan=False,
        ).encode("ascii")
    except (TypeError, ValueError, UnicodeError) as exc:
        raise ContractError("invalid canonical JSON") from exc


def canonical_document(value: Any) -> bytes:
    return canonical_bytes(value) + b"\n"


def canonical_sha256(value: Any) -> str:
    return _sha256(canonical_bytes(value))


def decode_canonical(raw: bytes, label: str = "record") -> dict[str, Any]:
    if not raw or len(raw) > MAX_RECORD_BYTES or raw.startswith(b"\xef\xbb\xbf"):
        raise ContractError(f"{label} unavailable")
    try:
        value = json.loads(
            raw.decode("ascii"), object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict:
        raise ContractError(f"{label} shape mismatch")
    _scan(value)
    if raw != canonical_document(value):
        raise ContractError(f"{label} is not canonical")
    return value


def _safe_file(path: Path, label: str) -> bytes:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ContractError(f"{label} unavailable")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _descriptor(path: Path, label: str, expected_name: str | None = None) -> dict[str, Any]:
    raw = _safe_file(path, label)
    if not raw:
        raise ContractError(f"{label} is empty")
    if expected_name is not None and path.name != expected_name:
        raise ContractError(f"{label} name mismatch")
    if not _SAFE_NAME.fullmatch(path.name):
        raise ContractError(f"{label} name mismatch")
    return {"name": path.name, "bytes": len(raw), "sha256": _sha256(raw)}


def _repo_binding(relative: str, label: str, expected_sha256: str | None = None) -> dict[str, Any]:
    path = ROOT / relative
    try:
        path.resolve().relative_to(ROOT.resolve())
    except (OSError, ValueError) as exc:
        raise ContractError(f"{label} path mismatch") from exc
    raw = _safe_file(path, label)
    digest = _sha256(raw)
    if expected_sha256 is not None and digest != expected_sha256:
        raise ContractError(f"{label} digest mismatch")
    return {"path": relative, "bytes": len(raw), "raw_sha256": digest}


def _ot152_binding() -> dict[str, Any]:
    raw = _safe_file(ROOT / OT152_RELATIVE, "OT-152 parent")
    if _sha256(raw) != OT152_RAW_SHA256:
        raise ContractError("OT-152 raw digest mismatch")
    try:
        value = json.loads(
            raw.decode("utf-8"), object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError("OT-152 parent unavailable") from exc
    _scan(value)
    if canonical_sha256(value) != OT152_CANONICAL_SHA256:
        raise ContractError("OT-152 canonical digest mismatch")
    return {
        "path": OT152_RELATIVE, "bytes": len(raw),
        "raw_sha256": OT152_RAW_SHA256,
        "canonical_sha256": OT152_CANONICAL_SHA256,
    }


def _source_bindings() -> dict[str, Any]:
    result = {
        f"target_{index:02d}": _repo_binding(relative, "target input")
        for index, relative in enumerate(TARGET_BINDINGS, 1)
    }
    for role, relative, expected in SOURCE_BINDINGS:
        result[role] = _repo_binding(relative, role, expected)

    target_source = _safe_file(
        ROOT / TARGET_BINDINGS[-1], "target source"
    ).decode("utf-8", errors="strict").lower()
    if any(token in target_source for token in ("ota1", "packet_v1", "packet v1")):
        raise ContractError("Packet V1 or OTA1 wrapper prohibited")

    target_lock = _safe_file(
        ROOT / TARGET_BINDINGS[2], "target dependency lock"
    ).decode("utf-8", errors="strict")
    required_lock_text = (
        "version: 6.0.2",
        f"version: {RADIOLIB_VERSION}",
        f"component_hash: {RADIOLIB_COMPONENT_SHA256}",
    )
    if any(item not in target_lock for item in required_lock_text):
        raise ContractError("target dependency lock mismatch")

    libsodium_lock_relative = (
        "tests/benchmarks/crypto/esp_idf/"
        "espressif_libsodium_1_0_22/dependencies.lock"
    )
    libsodium_lock = _safe_file(
        ROOT / libsodium_lock_relative, "libsodium dependency lock"
    ).decode("utf-8", errors="strict")
    if (
        f"version: {LIBSODIUM_VERSION}" not in libsodium_lock
        or f"component_hash: {LIBSODIUM_COMPONENT_SHA256}" not in libsodium_lock
    ):
        raise ContractError("libsodium dependency lock mismatch")
    result["libsodium_dependency_lock"] = _repo_binding(
        libsodium_lock_relative, "libsodium dependency lock"
    )
    return result


def _runtime_bindings(runtime: Mapping[str, Path] | None) -> dict[str, Any]:
    expected = {role: (ROOT / relative).resolve() for role, relative in RUNTIME_BINDINGS}
    supplied = expected if runtime is None else runtime
    if type(supplied) is not dict or set(supplied) != set(expected):
        raise ContractError("runtime binding set mismatch")
    result: dict[str, Any] = {}
    identities: set[Path] = set()
    for role, relative in RUNTIME_BINDINGS:
        path = supplied[role]
        if not isinstance(path, Path):
            raise ContractError("runtime binding path mismatch")
        resolved = path.resolve()
        if resolved != expected[role] or resolved in identities:
            raise ContractError("runtime binding path mismatch")
        identities.add(resolved)
        result[role] = _repo_binding(relative, role)
    return result


def _build_evidence(
    build_runs: Mapping[str, Mapping[str, Path]] | None,
) -> dict[str, Any]:
    if build_runs is None:
        return {
            "status": "pending",
            "run_order": ["A", "B"],
            "independent_build_roots_required": True,
            "byte_identical_output_tuple_required": True,
            "output_roles": [role for role, unused in BUILD_ARTIFACTS],
            "runs": [],
        }
    if type(build_runs) is not dict or set(build_runs) != {"A", "B"}:
        raise ContractError("build evidence requires exact A/B runs")
    output: list[dict[str, Any]] = []
    paths: dict[str, dict[str, Path]] = {}
    for run in ("A", "B"):
        artifacts = build_runs[run]
        if type(artifacts) is not dict or set(artifacts) != {
            role for role, unused in BUILD_ARTIFACTS
        }:
            raise ContractError("build artifact role mismatch")
        descriptors: dict[str, Any] = {}
        paths[run] = {}
        for role, expected_name in BUILD_ARTIFACTS:
            path = artifacts[role]
            if not isinstance(path, Path):
                raise ContractError("build artifact path mismatch")
            paths[run][role] = path.resolve()
            descriptors[role] = _descriptor(path, role, expected_name)
        output.append({"run": run, "artifacts": descriptors})
    for role, unused in BUILD_ARTIFACTS:
        if paths["A"][role] == paths["B"][role]:
            raise ContractError("A/B build paths are not independent")
        if output[0]["artifacts"][role] != output[1]["artifacts"][role]:
            raise ContractError("A/B build outputs differ")
    return {
        "status": "reproduced",
        "run_order": ["A", "B"],
        "independent_build_roots_required": True,
        "byte_identical_output_tuple_required": True,
        "output_roles": [role for role, unused in BUILD_ARTIFACTS],
        "runs": output,
    }


def _authority() -> dict[str, bool]:
    return {
        "owner_one_attempt_authority_accepted": False,
        "device_access_authorized": False,
        "serial_access_authorized": False,
        "reset_authorized": False,
        "flash_authorized": False,
        "radio_transmit_authorized": False,
        "key_or_entropy_operation_authorized": False,
        "benchmark_execution_authorized": False,
        "candidate_selection_authorized": False,
        "suite_selection_authorized": False,
        "packet_v1_authorized": False,
        "score_credit_authorized": False,
    }


def _claims(frozen: bool) -> dict[str, bool]:
    return {
        "bundle_preparation_generated": True,
        "immutable_executable_bundle_frozen": frozen,
        "hardware_or_device_accessed": False,
        "firmware_flashed": False,
        "radio_used": False,
        "radio_measurement_admitted": False,
        "phase_two_complete": False,
        "candidate_selected": False,
        "suite_selected": False,
        "packet_v1_wire_selected": False,
        "production_support_proven": False,
        "regulatory_compliance_proven": False,
        "field_ready_proven": False,
        "score_credit_added": False,
    }


def build_preparation(
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]] | None = None,
    runtime_bindings: Mapping[str, Path] | None = None,
) -> dict[str, Any]:
    """Generate a deterministic in-memory OT-153 host-only preparation."""
    restore = _descriptor(restore_path, "Trail restoration image", RESTORE_NAME)
    if restore != {
        "name": RESTORE_NAME, "bytes": RESTORE_BYTES, "sha256": RESTORE_SHA256
    }:
        raise ContractError("Trail restoration image mismatch")
    build = _build_evidence(build_runs)
    frozen = build["status"] == "reproduced"
    value = {
        "schema": SCHEMA,
        "version": 0,
        "artifact_kind": "libsodium_noise_xk_radio_executable_bundle_preparation",
        "preparation_id": PREPARATION_ID,
        "recorded_date": RECORDED_DATE,
        "status": (
            "host_only_immutable_bundle_frozen_fresh_owner_authority_required"
            if frozen else
            "host_only_bundle_preparation_build_evidence_pending"
        ),
        "bindings": {
            "ot152_preparation": _ot152_binding(),
            "sources": _source_bindings(),
            "runtime": _runtime_bindings(runtime_bindings),
        },
        "toolchain_and_dependencies": {
            "esp_idf": {"version": ESP_IDF_VERSION, "commit": ESP_IDF_COMMIT},
            "radiolib": {
                "version": RADIOLIB_VERSION,
                "component_sha256": RADIOLIB_COMPONENT_SHA256,
            },
            "libsodium": {
                "version": LIBSODIUM_VERSION,
                "component_sha256": LIBSODIUM_COMPONENT_SHA256,
            },
        },
        "firmware": {
            "target": "heltec_wifi_lora_32_v4_2",
            "project": "ot153_noise_xk_radio_cost",
            "build_policy": {
                "fixed_project_version": PROJECT_VERSION,
                "ccache_enabled": False,
            },
            "build_evidence": build,
            "future_application_write": {
                "artifact_role": "application_bin",
                "application_offset": APPLICATION_OFFSET,
                "writable_only_after_separate_authority": True,
                "bootloader_writable": False,
                "partition_table_writable": False,
                "nvs_writable": False,
            },
        },
        "execution_contract": {
            "node_count": 2,
            "both_nodes_present_simultaneously_required": True,
            "role_cycles": [
                {"cycle": 1, "initiator": "A", "responder": "B"},
                {"cycle": 2, "initiator": "B", "responder": "A"},
            ],
            "message_wire_bytes": list(MESSAGE_WIRE_BYTES),
            "raw_message_schema": "OTNXK0/v0",
            "packet_v1_wrapper_used": False,
            "ota1_ack_wrapper_used": False,
            "radio_payload_wire_bytes": TOTAL_RADIO_BYTES,
            "transmissions": TOTAL_TRANSMISSIONS,
            "theoretical_airtime_us": TOTAL_THEORETICAL_AIRTIME_US,
            "one_bounded_whole_handshake_restart_per_direction": True,
            "success_or_abort_consumes_future_authority": True,
        },
        "images": {
            "restore": {
                **restore,
                "application_offset": APPLICATION_OFFSET,
                "exact_readback_and_restore_required": True,
            }
        },
        "privacy": {
            "build_artifacts_record_name_only": True,
            "repository_bindings_relative_only": True,
            "absolute_paths_recorded": False,
            "serial_ports_recorded": False,
            "device_identifiers_recorded": False,
            "raw_payloads_recorded": False,
            "keys_or_secrets_recorded": False,
        },
        "authority": _authority(),
        "claims": _claims(frozen),
    }
    _scan(value)
    return value


def validate_preparation(
    value: dict[str, Any],
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]] | None = None,
    runtime_bindings: Mapping[str, Path] | None = None,
) -> dict[str, Any]:
    expected = build_preparation(restore_path, build_runs, runtime_bindings)
    if value != expected:
        raise ContractError("preparation boundary mismatch")
    if any(value["authority"].values()):
        raise ContractError("authority must remain false")
    frozen = value["firmware"]["build_evidence"]["status"] == "reproduced"
    return {
        "schema": SCHEMA,
        "canonical_sha256": canonical_sha256(value),
        "bundle_preparation_generated": True,
        "immutable_executable_bundle_frozen": frozen,
        "execution_authorized": False,
        "radio_transmit_authorized": False,
        "phase_two_complete": False,
    }


def validate_record_file(
    path: Path,
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]] | None = None,
    runtime_bindings: Mapping[str, Path] | None = None,
) -> dict[str, Any]:
    raw = _safe_file(path, "preparation record")
    if (
        EXPECTED_RECORD_RAW_SHA256 != "TO_BE_PINNED"
        and _sha256(raw) != EXPECTED_RECORD_RAW_SHA256
    ):
        raise ContractError("preparation raw digest mismatch")
    value = decode_canonical(raw, "preparation record")
    if (
        EXPECTED_RECORD_CANONICAL_SHA256 != "TO_BE_PINNED"
        and canonical_sha256(value) != EXPECTED_RECORD_CANONICAL_SHA256
    ):
        raise ContractError("preparation canonical digest mismatch")
    return validate_preparation(value, restore_path, build_runs, runtime_bindings)
