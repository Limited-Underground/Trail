#!/usr/bin/env python3
"""Create and validate the host-only OT-162 one-attempt radio authority.

This tool has no endpoint, serial, flash, reset, radio, execution, or recovery
surface.  It binds the accepted OT-161 corrected reset-aware executable/restoration
bundle to exactly one later two-node application-only attempt.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-28"
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-161-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
)
ADAPTER_RELATIVE = "tools/ot160_noise_xk_radio_hardware_adapter.py"
AUTHORITY_TOOL_RELATIVE = "tools/ot162_noise_xk_radio_execution_authority.py"

PREPARATION_SCHEMA = "OT161NXBP0"
PREPARATION_ID = (
    "OT-161-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
PREPARATION_RAW_SHA256 = (
    "942f7bda82273e8d06901827934eac6dc2c30ac3135ba614c2067eecb8cb171c"
)
PREPARATION_CANONICAL_SHA256 = (
    "0364615ab9d1129f4b3d83e0ea34d66da0b6e4a8a3070646d277984881388e9f"
)
PARENT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
PARENT_RAW_SHA256 = (
    "84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b"
)
PARENT_CANONICAL_SHA256 = (
    "06de67211443c1432336a0e1d16f4d62be58d870e93cc4b70c2d494c199bcfd9"
)
AUTHORITY_SCHEMA = "OT162NXRA0"
AUTHORITY_ID = (
    "OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0"
)
APPLICATION_OFFSET = 0x10000
BAUD = 115_200
RUNNER_SCHEMA = "OT153NXR0"
MAX_JSON_BYTES = 131_072
MAX_DEPTH = 20
MAX_ITEMS = 4_096
HASH64 = re.compile(r"[0-9a-f]{64}\Z")
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
RUNTIME_BINDINGS = {
    "runner": {
        "path": "tools/ot156_noise_xk_radio_runner.py",
        "bytes": 11099,
        "raw_sha256": "81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244",
    },
    "serial_runtime": {
        "path": "tools/ot156_noise_xk_radio_runtime.py",
        "bytes": 6588,
        "raw_sha256": "bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8",
    },
    "coordinator": {
        "path": "tools/ot160_noise_xk_radio_coordinator.py",
        "bytes": 7264,
        "raw_sha256": "444528fd341b3d55f3a5b3224b217620e1b37e3c7960d224aefbe01d9953a02d",
    },
    "hardware_adapter": {
        "path": ADAPTER_RELATIVE,
        "bytes": 3952,
        "raw_sha256": "24d75806cdf7ae28c47fe427cac12a7ef3564d76d68a926ad58bd610c9e8f4b9",
    },
}
BENCHMARK = {
    "name": "ot153_noise_xk_radio_cost.bin",
    "bytes": 296640,
    "sha256": "ed2eef319d5bca22d1d89a0be61e63463ada1a8fb3277238cdf95cf93093cd3c",
}
RESTORE = {
    "name": "opentrail_heltec_v4_bench.bin",
    "bytes": 500944,
    "sha256": "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
}
STAGE_CODES = (
    "restart_ack_a", "restart_ack_b", "restart_reconnect_a",
    "restart_reconnect_b", "restart_boot_contract_a",
    "restart_boot_contract_b", "identity_generation", "cycle1_baseline",
    "cycle1_retry_timeout", "cycle1_retry_restart", "cycle2_baseline",
    "cycle2_retry_timeout", "cycle2_retry_restart", "final_status_a",
    "final_status_b", "result_validation",
)
RADIO_PROFILE = {
    "region_code": "US915",
    "frequency_hz": 915_000_000,
    "bandwidth_hz": 125_000,
    "spreading_factor": 7,
    "coding_rate_denominator": 5,
    "explicit_header": True,
    "crc_enabled": True,
    "low_data_rate_optimization": False,
    "sync_word": "0x12",
    "preamble_symbols": 8,
    "tx_power_command_setpoint_dbm": 2,
    "admitted_direct_total_wire_ceiling_bytes": 255,
    "calibrated_rf_readback": False,
}


class ContractError(ValueError):
    """A stable privacy-safe contract failure."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ContractError("invalid arguments")


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _scan(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise ContractError("structure too deep")
    if isinstance(value, str):
        if len(value) > 4096 or _PRIVATE_TEXT.search(value):
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
        raise ContractError("canonical encoding failed") from exc


def canonical_document(value: Any) -> bytes:
    return canonical_bytes(value) + b"\n"


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ContractError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_: str) -> None:
    raise ContractError("invalid JSON constant")


def decode_canonical(raw: bytes, label: str = "record") -> dict[str, Any]:
    if not raw or len(raw) > MAX_JSON_BYTES or raw.startswith(b"\xef\xbb\xbf"):
        raise ContractError(f"{label} unavailable")
    try:
        value = json.loads(
            raw.decode("ascii"), object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict or raw != canonical_document(value):
        raise ContractError(f"{label} is not canonical")
    return value


def _safe_file(path: Path, label: str) -> bytes:
    try:
        if not path.is_absolute() or path.is_symlink() or not path.is_file():
            raise ContractError(f"{label} unavailable")
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _load_canonical(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _safe_file(path, label)
    return decode_canonical(raw, label), raw


def _repo_descriptor(relative: str, label: str) -> dict[str, Any]:
    path = ROOT / relative
    try:
        resolved = path.resolve()
        if resolved.relative_to(ROOT.resolve()) != Path(relative):
            raise ContractError(f"{label} identity mismatch")
    except (OSError, ValueError) as exc:
        raise ContractError(f"{label} identity mismatch") from exc
    raw = _safe_file(resolved, label)
    return {"path": relative, "bytes": len(raw), "raw_sha256": _sha256(raw)}


def _validate_repo_binding(value: object, expected: dict[str, Any], label: str) -> None:
    if type(value) is not dict or value != expected:
        raise ContractError(f"{label} binding mismatch")
    if _repo_descriptor(expected["path"], label) != expected:
        raise ContractError(f"{label} binding mismatch")


def _image_descriptor(path: Path, expected: dict[str, Any], label: str) -> None:
    raw = _safe_file(path, label)
    if (
        path.name != expected["name"]
        or len(raw) != expected["bytes"]
        or _sha256(raw) != expected["sha256"]
    ):
        raise ContractError(f"{label} mismatch")


def _load_ot153_parent(binding: object) -> dict[str, Any]:
    expected = {
        "path": PARENT_RELATIVE,
        "bytes": 7496,
        "raw_sha256": PARENT_RAW_SHA256,
        "canonical_sha256": PARENT_CANONICAL_SHA256,
    }
    if binding != expected:
        raise ContractError("OT-153 parent binding mismatch")
    value, raw = _load_canonical(ROOT / PARENT_RELATIVE, "OT-153 parent")
    if (
        len(raw) != expected["bytes"]
        or _sha256(raw) != PARENT_RAW_SHA256
        or _sha256(canonical_bytes(value)) != PARENT_CANONICAL_SHA256
        or value.get("schema") != "OT153NXBP0"
        or value.get("preparation_id")
        != "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
        or value.get("status")
        != "host_only_immutable_bundle_frozen_fresh_owner_authority_required"
        or any(value.get("authority", {}).values())
    ):
        raise ContractError("OT-153 parent binding mismatch")
    ot152 = value.get("bindings", {}).get("ot152_preparation")
    if type(ot152) is not dict or set(ot152) != {
        "path", "bytes", "raw_sha256", "canonical_sha256"
    }:
        raise ContractError("OT-152 binding mismatch")
    ot152_raw = _safe_file(ROOT / ot152["path"], "OT-152 parent")
    try:
        ot152_value = json.loads(
            ot152_raw.decode("ascii"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError("OT-152 binding mismatch") from exc
    if (
        len(ot152_raw) != ot152["bytes"]
        or _sha256(ot152_raw) != ot152["raw_sha256"]
        or _sha256(canonical_bytes(ot152_value)) != ot152["canonical_sha256"]
        or ot152_value.get("radio_profile") != RADIO_PROFILE
    ):
        raise ContractError("OT-152 binding mismatch")
    return value


def _validate_preparation(value: dict[str, Any], raw: bytes) -> dict[str, Any]:
    if (
        len(raw) != 6113
        or _sha256(raw) != PREPARATION_RAW_SHA256
        or _sha256(canonical_bytes(value)) != PREPARATION_CANONICAL_SHA256
    ):
        raise ContractError("preparation digest mismatch")
    if (
        value.get("schema") != PREPARATION_SCHEMA
        or value.get("version") != 0
        or value.get("preparation_id") != PREPARATION_ID
        or value.get("status")
        != "host_only_immutable_bundle_frozen_fresh_owner_authority_required"
        or type(value.get("authority")) is not dict
        or any(value["authority"].values())
        or value.get("claims", {}).get("immutable_executable_bundle_frozen") is not True
        or value.get("claims", {}).get("radio_used") is not False
    ):
        raise ContractError("preparation identity mismatch")
    bindings = value.get("bindings")
    if type(bindings) is not dict or set(bindings) != {"ot153_preparation", "runtime"}:
        raise ContractError("preparation binding mismatch")
    parent = _load_ot153_parent(bindings["ot153_preparation"])
    runtime = bindings["runtime"]
    if type(runtime) is not dict or set(runtime) != set(RUNTIME_BINDINGS):
        raise ContractError("runtime binding mismatch")
    for role, expected in RUNTIME_BINDINGS.items():
        _validate_repo_binding(runtime[role], expected, role)
    build = value.get("firmware", {}).get("build_evidence")
    if (
        type(build) is not dict
        or build.get("status") != "reproduced"
        or build.get("run_order") != ["A", "B"]
        or type(build.get("runs")) is not list
        or len(build["runs"]) != 2
        or build["runs"][0].get("artifacts") != build["runs"][1].get("artifacts")
    ):
        raise ContractError("build evidence mismatch")
    if value.get("images", {}).get("benchmark") != BENCHMARK:
        raise ContractError("benchmark binding mismatch")
    expected_restore = {**RESTORE, "application_offset": APPLICATION_OFFSET,
                        "exact_readback_and_restore_required": True}
    if value.get("images", {}).get("restore") != expected_restore:
        raise ContractError("restore binding mismatch")
    execution = value.get("execution_contract")
    required = {
        "node_count": 2,
        "both_nodes_present_simultaneously_required": True,
        "role_cycles": [
            {"cycle": 1, "initiator": "A", "responder": "B"},
            {"cycle": 2, "initiator": "B", "responder": "A"},
        ],
        "message_wire_bytes": [48, 48, 64],
        "raw_message_schema": "OTNXK0/v0",
        "packet_v1_wrapper_used": False,
        "ota1_ack_wrapper_used": False,
        "radio_payload_wire_bytes": 736,
        "transmissions": 14,
        "theoretical_airtime_us": 1_447_424,
        "one_bounded_whole_handshake_restart_per_direction": True,
        "success_or_abort_consumes_future_authority": True,
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
    }
    if type(execution) is not dict or execution != required:
        raise ContractError("execution boundary mismatch")
    if parent["firmware"]["build_evidence"] != build:
        raise ContractError("parent build mismatch")
    return {
        "benchmark": BENCHMARK,
        "restore": RESTORE,
        "execution": execution,
        "radio_profile": RADIO_PROFILE,
    }


def _load_validated_preparation() -> tuple[dict[str, Any], bytes, dict[str, Any]]:
    value, raw = _load_canonical(ROOT / PREPARATION_RELATIVE, "preparation")
    return value, raw, _validate_preparation(value, raw)


def _binding(preparation: dict[str, Any], validated: dict[str, Any]) -> dict[str, Any]:
    runner = preparation["bindings"]["runtime"]["runner"]
    return {
        "benchmark_name": validated["benchmark"]["name"],
        "benchmark_bytes": validated["benchmark"]["bytes"],
        "benchmark_sha256": validated["benchmark"]["sha256"],
        "restore_name": validated["restore"]["name"],
        "restore_bytes": validated["restore"]["bytes"],
        "restore_sha256": validated["restore"]["sha256"],
        "application_offset": APPLICATION_OFFSET,
        "baud": BAUD,
        "runner_name": Path(runner["path"]).name,
        "runner_sha256": runner["raw_sha256"],
        "runner_schema": RUNNER_SCHEMA,
    }


def load_preparation_binding(
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> dict[str, Any]:
    if type(recovery) is not bool:
        raise ContractError("recovery mode mismatch")
    preparation, unused_raw, validated = _load_validated_preparation()
    expected_adapter = preparation["bindings"]["runtime"]["hardware_adapter"]
    if _repo_descriptor(ADAPTER_RELATIVE, "adapter") != expected_adapter:
        raise ContractError("adapter binding mismatch")
    try:
        if adapter_path.resolve() != (ROOT / ADAPTER_RELATIVE).resolve():
            raise ContractError("adapter identity mismatch")
    except OSError as exc:
        raise ContractError("adapter identity mismatch") from exc
    _image_descriptor(restore_path, validated["restore"], "restore image")
    if recovery:
        if benchmark_path is not None:
            raise ContractError("recovery benchmark must be absent")
    else:
        if benchmark_path is None:
            raise ContractError("benchmark unavailable")
        _image_descriptor(benchmark_path, validated["benchmark"], "benchmark image")
    return _binding(preparation, validated)


def _authority_value(
    preparation: dict[str, Any], preparation_raw: bytes, validated: dict[str, Any]
) -> dict[str, Any]:
    return {
        "schema": AUTHORITY_SCHEMA,
        "version": 0,
        "artifact_kind": "reset_aware_libsodium_noise_xk_radio_one_attempt_authority",
        "authority_id": AUTHORITY_ID,
        "recorded_date": RECORDED_DATE,
        "status": "authorized_one_attempt_not_executed",
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "bytes": len(preparation_raw),
            "raw_sha256": _sha256(preparation_raw),
            "canonical_sha256": _sha256(canonical_bytes(preparation)),
        },
        "runtime": {
            **preparation["bindings"]["runtime"],
            "execution_authority_tool": _repo_descriptor(
                AUTHORITY_TOOL_RELATIVE, "authority tool"
            ),
        },
        "images": {
            "benchmark": validated["benchmark"],
            "restore": validated["restore"],
        },
        "execution": {
            "attempt_count": 1,
            "application_offset": APPLICATION_OFFSET,
            "application_only_writes": True,
            "both_installed_trail_readbacks_before_first_write": True,
            "display_reset_and_visual_preflight_required": True,
            "benchmark_readback_before_radio": True,
            **validated["execution"],
            "radio_allowed": True,
            "packet_v1_allowed": False,
            "ota1_wrapper_allowed": False,
            "selection_allowed": False,
            "restore_readback_and_hard_reset_required": True,
            "recovery_only_mode_required": True,
            "recovery_requires_benchmark_artifact": False,
        },
        "radio_profile": validated["radio_profile"],
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_reset_aware_noise_xk_radio_cost_attempt",
            "standing_test_device_authorization_recorded": True,
            "temporary_application_flash_and_exact_trail_restore": True,
            "permanent_firmware_decision": False,
        },
        "consumption": {
            "consumed_on_success_or_abort": True,
            "continuing_authority": False,
            "reusable": False,
        },
        "claims": {
            "benchmark_executed": False,
            "radio_measurement_admitted": False,
            "phase_two_complete": False,
            "candidate_selected": False,
            "suite_selected": False,
            "packet_v1_wire_selected": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "field_ready_proven": False,
            "score_credit_added": False,
        },
    }


def build_authority(
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark_path: Path,
    restore_path: Path,
    adapter_path: Path,
    *,
    owner_authorization_granted: bool,
) -> dict[str, Any]:
    if owner_authorization_granted is not True:
        raise ContractError("owner authorization absent")
    actual, actual_raw, validated = _load_validated_preparation()
    if preparation != actual or preparation_raw != actual_raw:
        raise ContractError("preparation boundary mismatch")
    load_preparation_binding(
        benchmark_path, restore_path, adapter_path, recovery=False
    )
    return _authority_value(actual, actual_raw, validated)


def validate_authority(
    value: dict[str, Any],
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool = False,
) -> dict[str, Any]:
    actual, actual_raw, validated = _load_validated_preparation()
    if preparation != actual or preparation_raw != actual_raw:
        raise ContractError("preparation boundary mismatch")
    load_preparation_binding(
        benchmark_path, restore_path, adapter_path, recovery=recovery
    )
    expected = _authority_value(actual, actual_raw, validated)
    if value != expected:
        raise ContractError("authority boundary mismatch")
    return {
        "schema": AUTHORITY_SCHEMA,
        "authority_id": AUTHORITY_ID,
        "authority_canonical_sha256": _sha256(canonical_bytes(value)),
        "attempt_count": 1,
        "reusable": False,
        "radio_allowed": True,
    }


def validate_execution_authority(
    authority_path: Path,
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> str:
    try:
        if authority_path.resolve() != (ROOT / AUTHORITY_RELATIVE).resolve():
            raise ContractError("authority identity mismatch")
    except OSError as exc:
        raise ContractError("authority identity mismatch") from exc
    preparation, preparation_raw, unused = _load_validated_preparation()
    authority, authority_raw = _load_canonical(authority_path, "authority")
    validate_authority(
        authority, preparation, preparation_raw, benchmark_path, restore_path,
        adapter_path, recovery=recovery,
    )
    return _sha256(authority_raw)


def write_new(path: Path, value: dict[str, Any], relative: str) -> None:
    try:
        if path.resolve() != (ROOT / relative).resolve():
            raise ContractError("output identity mismatch")
    except OSError as exc:
        raise ContractError("output identity mismatch") from exc
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(canonical_document(value))
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError as exc:
        raise ContractError("output already exists") from exc
    except OSError as exc:
        raise ContractError("output unavailable") from exc


def _parser() -> SafeArgumentParser:
    parser = SafeArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("authorize", "validate-authority"):
        command = commands.add_parser(name)
        command.add_argument("--benchmark-app", type=Path, required=True)
        command.add_argument("--restore-app", type=Path, required=True)
        command.add_argument("--adapter", type=Path, required=True)
    commands.choices["authorize"].add_argument(
        "--owner-authorization-granted", action="store_true"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = _parser().parse_args(argv)
        preparation, preparation_raw, unused = _load_validated_preparation()
        if args.command == "authorize":
            value = build_authority(
                preparation, preparation_raw, args.benchmark_app.resolve(),
                args.restore_app.resolve(), args.adapter.resolve(),
                owner_authorization_granted=args.owner_authorization_granted,
            )
            write_new(ROOT / AUTHORITY_RELATIVE, value, AUTHORITY_RELATIVE)
        else:
            value, unused_raw = _load_canonical(ROOT / AUTHORITY_RELATIVE, "authority")
        result = validate_authority(
            value, preparation, preparation_raw, args.benchmark_app.resolve(),
            args.restore_app.resolve(), args.adapter.resolve(),
        )
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
        return 0
    except BaseException:
        print("ERROR: OT-162 execution authority validation failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
