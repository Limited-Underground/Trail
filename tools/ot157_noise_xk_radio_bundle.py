#!/usr/bin/env python3
"""Build and validate the host-only OT-157 reset-aware radio bundle.

The tool has no execution or persistence surface.  It binds the accepted
OT-153 firmware/build/restoration evidence to the accepted OT-156 runner and
runtime plus the fresh OT-157 coordinator and adapter.  Every authority flag
remains false until a separately accepted successor authority exists.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-28"
SCHEMA = "OT157NXBP0"
PREPARATION_ID = (
    "OT-157-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-157-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
EXPECTED_RECORD_RAW_SHA256 = "889bb6440f71864f29e815be926fbcc6c04d2de442be3cf38b6a2eceb365ba2a"
EXPECTED_RECORD_CANONICAL_SHA256 = "28a358393e3f9edaf8b9e155283737b087b5c0e43206598ba540cc5882caf25f"

PARENT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
PARENT_RAW_SHA256 = "84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b"
PARENT_CANONICAL_SHA256 = "06de67211443c1432336a0e1d16f4d62be58d870e93cc4b70c2d494c199bcfd9"
PARENT_SCHEMA = "OT153NXBP0"
PARENT_ID = (
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
)

RUNTIME_BINDINGS = (
    ("runner", "tools/ot156_noise_xk_radio_runner.py"),
    ("serial_runtime", "tools/ot156_noise_xk_radio_runtime.py"),
    ("coordinator", "tools/ot157_noise_xk_radio_coordinator.py"),
    ("hardware_adapter", "tools/ot157_noise_xk_radio_hardware_adapter.py"),
)
RUNTIME_SHA256 = {
    "runner": "81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244",
    "serial_runtime": "bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8",
    "coordinator": "a17d4615a50d2e373ba7fab98b6bed160e6b24e291aac2ec83f0fa19d1479db1",
    "hardware_adapter": "ec744f7430627600de1491d5729d8b5874ac38977faa786df90a4d39ce2d3de8",
}
BUILD_ARTIFACTS = (
    ("bootloader_bin", "bootloader.bin"),
    ("partition_table_bin", "partition-table.bin"),
    ("application_bin", "ot153_noise_xk_radio_cost.bin"),
    ("application_elf", "ot153_noise_xk_radio_cost.elf"),
)
STAGE_CODES = (
    "restart_ack_a", "restart_ack_b",
    "restart_reconnect_a", "restart_reconnect_b",
    "restart_boot_contract_a", "restart_boot_contract_b",
    "identity_generation", "cycle1_baseline", "cycle1_retry_timeout",
    "cycle1_retry_restart", "cycle2_baseline", "cycle2_retry_timeout",
    "cycle2_retry_restart", "final_status_a", "final_status_b",
    "result_validation",
)
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
    "device_identifier", "mac", "mac_address", "raw_payload", "secret",
    "private_key",
}
_SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\Z")


class ContractError(ValueError):
    """Stable privacy-safe OT-157 preparation failure."""


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


def _descriptor(path: Path, label: str, expected_name: str) -> dict[str, Any]:
    raw = _safe_file(path, label)
    if not raw or path.name != expected_name or _SAFE_NAME.fullmatch(path.name) is None:
        raise ContractError(f"{label} mismatch")
    return {"name": path.name, "bytes": len(raw), "sha256": _sha256(raw)}


def _repo_binding(relative: str, label: str, expected_sha256: str) -> dict[str, Any]:
    path = ROOT / relative
    try:
        path.resolve().relative_to(ROOT.resolve())
    except (OSError, ValueError) as exc:
        raise ContractError(f"{label} path mismatch") from exc
    raw = _safe_file(path, label)
    if _sha256(raw) != expected_sha256:
        raise ContractError(f"{label} digest mismatch")
    return {"path": relative, "bytes": len(raw), "raw_sha256": expected_sha256}


def _accepted_parent() -> tuple[dict[str, Any], dict[str, Any]]:
    raw = _safe_file(ROOT / PARENT_RELATIVE, "OT-153 parent")
    if _sha256(raw) != PARENT_RAW_SHA256:
        raise ContractError("OT-153 parent raw digest mismatch")
    value = decode_canonical(raw, "OT-153 parent")
    if canonical_sha256(value) != PARENT_CANONICAL_SHA256:
        raise ContractError("OT-153 parent canonical digest mismatch")
    if (
        value.get("schema") != PARENT_SCHEMA
        or value.get("preparation_id") != PARENT_ID
        or value.get("status")
        != "host_only_immutable_bundle_frozen_fresh_owner_authority_required"
        or type(value.get("authority")) is not dict
        or any(value["authority"].values())
        or value.get("claims", {}).get("immutable_executable_bundle_frozen") is not True
    ):
        raise ContractError("OT-153 parent boundary mismatch")
    build = value.get("firmware", {}).get("build_evidence")
    if (
        type(build) is not dict
        or build.get("status") != "reproduced"
        or build.get("run_order") != ["A", "B"]
        or type(build.get("runs")) is not list
        or len(build["runs"]) != 2
        or build["runs"][0].get("artifacts") != build["runs"][1].get("artifacts")
    ):
        raise ContractError("OT-153 build evidence mismatch")
    return value, {
        "path": PARENT_RELATIVE, "bytes": len(raw),
        "raw_sha256": PARENT_RAW_SHA256,
        "canonical_sha256": PARENT_CANONICAL_SHA256,
    }


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
        result[role] = _repo_binding(relative, role, RUNTIME_SHA256[role])
    return result


def _verified_builds(
    build_runs: Mapping[str, Mapping[str, Path]], parent: dict[str, Any]
) -> dict[str, Any]:
    if type(build_runs) is not dict or set(build_runs) != {"A", "B"}:
        raise ContractError("build evidence requires exact A/B runs")
    output: list[dict[str, Any]] = []
    resolved: dict[str, dict[str, Path]] = {}
    for run in ("A", "B"):
        artifacts = build_runs[run]
        if type(artifacts) is not dict or set(artifacts) != {
            role for role, unused in BUILD_ARTIFACTS
        }:
            raise ContractError("build artifact role mismatch")
        descriptors: dict[str, Any] = {}
        resolved[run] = {}
        for role, name in BUILD_ARTIFACTS:
            path = artifacts[role]
            if not isinstance(path, Path):
                raise ContractError("build artifact path mismatch")
            resolved[run][role] = path.resolve()
            descriptors[role] = _descriptor(path, role, name)
        output.append({"run": run, "artifacts": descriptors})
    for role, unused in BUILD_ARTIFACTS:
        if resolved["A"][role] == resolved["B"][role]:
            raise ContractError("A/B build paths are not independent")
    accepted = parent["firmware"]["build_evidence"]["runs"]
    if output != accepted:
        raise ContractError("build output tuple mismatch")
    return parent["firmware"]["build_evidence"]


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


def build_preparation(
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]],
    runtime_bindings: Mapping[str, Path] | None = None,
) -> dict[str, Any]:
    """Generate the deterministic immutable OT-157 host-only preparation."""
    parent, parent_binding = _accepted_parent()
    restore = _descriptor(
        restore_path, "Trail restoration image",
        parent["images"]["restore"]["name"],
    )
    expected_restore = {
        key: parent["images"]["restore"][key]
        for key in ("name", "bytes", "sha256")
    }
    if restore != expected_restore:
        raise ContractError("Trail restoration image mismatch")
    build = _verified_builds(build_runs, parent)
    value = {
        "schema": SCHEMA,
        "version": 0,
        "artifact_kind": "reset_aware_libsodium_noise_xk_radio_executable_bundle_preparation",
        "preparation_id": PREPARATION_ID,
        "recorded_date": RECORDED_DATE,
        "status": "host_only_immutable_bundle_frozen_fresh_owner_authority_required",
        "bindings": {
            "ot153_preparation": parent_binding,
            "runtime": _runtime_bindings(runtime_bindings),
        },
        "firmware": {
            "target": parent["firmware"]["target"],
            "project": parent["firmware"]["project"],
            "build_policy": parent["firmware"]["build_policy"],
            "build_evidence": build,
            "future_application_write": parent["firmware"]["future_application_write"],
            "firmware_bytes_changed": False,
            "firmware_rebuild_required": False,
        },
        "execution_contract": {
            **parent["execution_contract"],
            "restart_ack_order": ["A", "B"],
            "reopen_order": ["A", "B"],
            "initial_open_timeout_ms": 10_000,
            "post_restart_settle_ms": 150,
            "reopen_retry_ms": 250,
            "reopen_timeout_ms": 15_000,
            "fresh_handle_dtr_false_before_open": True,
            "fresh_handle_rts_false_before_open": True,
            "old_receipt_queue_discarded_on_reopen": True,
            "both_post_restart_contracts_before_radio": True,
            "failure_stage_allowlist": list(STAGE_CODES),
            "failure_stage_count": len(STAGE_CODES),
            "success_result_byte_equal_to_ot153": True,
            "restore_every_touched_node_on_success_or_abort": True,
            "recovery_never_requires_or_writes_benchmark": True,
        },
        "images": {
            "benchmark": build["runs"][0]["artifacts"]["application_bin"],
            "restore": parent["images"]["restore"],
        },
        "privacy": {
            "build_artifacts_record_name_only": True,
            "repository_bindings_relative_only": True,
            "absolute_paths_recorded": False,
            "serial_ports_recorded": False,
            "device_identifiers_recorded": False,
            "raw_payloads_recorded": False,
            "keys_or_secrets_recorded": False,
            "backend_or_exception_text_recorded": False,
        },
        "authority": _authority(),
        "claims": {
            "bundle_preparation_generated": True,
            "immutable_executable_bundle_frozen": True,
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
        },
    }
    _scan(value)
    return value


def validate_preparation(
    value: dict[str, Any],
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]],
    runtime_bindings: Mapping[str, Path] | None = None,
) -> dict[str, Any]:
    if value != build_preparation(restore_path, build_runs, runtime_bindings):
        raise ContractError("preparation boundary mismatch")
    if any(value["authority"].values()):
        raise ContractError("authority must remain false")
    return {
        "schema": SCHEMA,
        "canonical_sha256": canonical_sha256(value),
        "immutable_executable_bundle_frozen": True,
        "execution_authorized": False,
        "radio_transmit_authorized": False,
        "phase_two_complete": False,
    }


def validate_record_file(
    path: Path,
    restore_path: Path,
    build_runs: Mapping[str, Mapping[str, Path]],
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
