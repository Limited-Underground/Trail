#!/usr/bin/env python3
"""Create and validate the host-only OT-154 one-attempt radio authority.

This tool has no endpoint, serial, flash, reset, or radio surface. It binds the
already accepted OT-153 bundle to one later two-node application-only attempt.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-27"
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
)
ADAPTER_RELATIVE = "tools/ot153_noise_xk_radio_hardware_adapter.py"
RUNNER_RELATIVE = "tools/ot153_noise_xk_radio_runner.py"
COORDINATOR_RELATIVE = "tools/ot153_noise_xk_radio_coordinator.py"
AUTHORITY_TOOL_RELATIVE = "tools/ot153_noise_xk_radio_execution_authority.py"

PREPARATION_SCHEMA = "OT153NXBP0"
PREPARATION_ID = (
    "OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
PREPARATION_RAW_SHA256 = (
    "84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b"
)
PREPARATION_CANONICAL_SHA256 = (
    "06de67211443c1432336a0e1d16f4d62be58d870e93cc4b70c2d494c199bcfd9"
)
AUTHORITY_SCHEMA = "OT154NXRA0"
AUTHORITY_ID = (
    "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0"
)
APPLICATION_OFFSET = 0x10000
BAUD = 115_200
RUNNER_SCHEMA = "OT153NXR0"
MAX_JSON_BYTES = 131_072
HASH64 = re.compile(r"[0-9a-f]{64}\Z")
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
            raw.decode("ascii"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict or raw != canonical_document(value):
        raise ContractError(f"{label} is not canonical")
    return value


def _safe_file(path: Path, label: str) -> bytes:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ContractError(f"{label} unavailable")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _load_canonical(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _safe_file(path, label)
    return decode_canonical(raw, label), raw


def _descriptor(path: Path, relative: str, label: str) -> dict[str, Any]:
    expected = (ROOT / relative).resolve()
    if path.resolve() != expected:
        raise ContractError(f"{label} identity mismatch")
    raw = _safe_file(path, label)
    return {"path": relative, "bytes": len(raw), "raw_sha256": _sha256(raw)}


def _image_descriptor(path: Path, expected: dict[str, Any], label: str) -> None:
    raw = _safe_file(path, label)
    if (
        path.name != expected["name"]
        or len(raw) != expected["bytes"]
        or _sha256(raw) != expected["sha256"]
    ):
        raise ContractError(f"{label} mismatch")


def _hash_descriptor(value: object, label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != {"name", "bytes", "sha256"}:
        raise ContractError(f"{label} shape mismatch")
    if (
        type(value["name"]) is not str
        or not value["name"]
        or type(value["bytes"]) is not int
        or value["bytes"] <= 0
        or type(value["sha256"]) is not str
        or HASH64.fullmatch(value["sha256"]) is None
    ):
        raise ContractError(f"{label} invalid")
    return value


def _validate_repo_binding(value: object, label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != {"path", "bytes", "raw_sha256"}:
        raise ContractError(f"{label} shape mismatch")
    relative = value["path"]
    if type(relative) is not str or not relative:
        raise ContractError(f"{label} path mismatch")
    path = (ROOT / relative).resolve()
    try:
        path.relative_to(ROOT.resolve())
    except ValueError as exc:
        raise ContractError(f"{label} path mismatch") from exc
    raw = _safe_file(path, label)
    if len(raw) != value["bytes"] or _sha256(raw) != value["raw_sha256"]:
        raise ContractError(f"{label} binding mismatch")
    return value


def _validate_preparation(value: dict[str, Any], raw: bytes) -> dict[str, Any]:
    if _sha256(raw) != PREPARATION_RAW_SHA256:
        raise ContractError("preparation raw digest mismatch")
    if _sha256(canonical_bytes(value)) != PREPARATION_CANONICAL_SHA256:
        raise ContractError("preparation canonical digest mismatch")
    if (
        value.get("schema") != PREPARATION_SCHEMA
        or value.get("version") != 0
        or value.get("preparation_id") != PREPARATION_ID
        or value.get("status")
        != "host_only_immutable_bundle_frozen_fresh_owner_authority_required"
    ):
        raise ContractError("preparation identity mismatch")
    if any(value.get("authority", {}).values()):
        raise ContractError("preparation authority drift")
    if value.get("firmware", {}).get("build_evidence", {}).get("status") != "reproduced":
        raise ContractError("build evidence unavailable")
    bindings = value.get("bindings")
    if type(bindings) is not dict:
        raise ContractError("preparation binding mismatch")
    ot152 = bindings.get("ot152_preparation")
    if type(ot152) is not dict or set(ot152) != {
        "path", "bytes", "raw_sha256", "canonical_sha256"
    }:
        raise ContractError("OT-152 binding mismatch")
    ot152_raw = _safe_file((ROOT / ot152["path"]).resolve(), "OT-152 preparation")
    if len(ot152_raw) != ot152["bytes"] or _sha256(ot152_raw) != ot152["raw_sha256"]:
        raise ContractError("OT-152 binding mismatch")
    try:
        ot152_value = json.loads(
            ot152_raw.decode("ascii"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError("OT-152 binding mismatch") from exc
    if type(ot152_value) is not dict:
        raise ContractError("OT-152 binding mismatch")
    if _sha256(canonical_bytes(ot152_value)) != ot152["canonical_sha256"]:
        raise ContractError("OT-152 canonical binding mismatch")
    if ot152_value.get("radio_profile") != RADIO_PROFILE:
        raise ContractError("radio profile mismatch")
    for label, descriptor in bindings.get("sources", {}).items():
        _validate_repo_binding(descriptor, label)
    for role, relative in (
        ("runner", RUNNER_RELATIVE),
        ("coordinator", COORDINATOR_RELATIVE),
        ("hardware_adapter", ADAPTER_RELATIVE),
    ):
        descriptor = bindings.get("runtime", {}).get(role)
        if _validate_repo_binding(descriptor, role)["path"] != relative:
            raise ContractError("runtime identity mismatch")
    runs = value["firmware"]["build_evidence"].get("runs")
    if type(runs) is not list or len(runs) != 2:
        raise ContractError("build run mismatch")
    applications = []
    for index, run in enumerate(runs):
        if run.get("run") != ("A" if index == 0 else "B"):
            raise ContractError("build run order mismatch")
        applications.append(_hash_descriptor(run["artifacts"]["application_bin"], "application"))
    if applications[0] != applications[1]:
        raise ContractError("application build mismatch")
    execution = value.get("execution_contract")
    required = {
        "node_count": 2,
        "both_nodes_present_simultaneously_required": True,
        "message_wire_bytes": [48, 48, 64],
        "raw_message_schema": "OTNXK0/v0",
        "packet_v1_wrapper_used": False,
        "ota1_ack_wrapper_used": False,
        "radio_payload_wire_bytes": 736,
        "transmissions": 14,
        "theoretical_airtime_us": 1_447_424,
        "one_bounded_whole_handshake_restart_per_direction": True,
        "success_or_abort_consumes_future_authority": True,
    }
    if type(execution) is not dict or any(execution.get(k) != v for k, v in required.items()):
        raise ContractError("execution boundary mismatch")
    if execution.get("role_cycles") != [
        {"cycle": 1, "initiator": "A", "responder": "B"},
        {"cycle": 2, "initiator": "B", "responder": "A"},
    ]:
        raise ContractError("role cycle mismatch")
    restore = value.get("images", {}).get("restore")
    if type(restore) is not dict or restore.get("application_offset") != APPLICATION_OFFSET:
        raise ContractError("restore boundary mismatch")
    return {
        "benchmark": applications[0],
        "restore": restore,
        "execution": execution,
        "radio_profile": RADIO_PROFILE,
    }


def _load_validated_preparation() -> tuple[dict[str, Any], bytes, dict[str, Any]]:
    value, raw = _load_canonical((ROOT / PREPARATION_RELATIVE).resolve(), "preparation")
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
    if _descriptor(adapter_path, ADAPTER_RELATIVE, "adapter") != preparation["bindings"]["runtime"]["hardware_adapter"]:
        raise ContractError("adapter binding mismatch")
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
        "artifact_kind": "libsodium_noise_xk_radio_one_attempt_authority",
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
            "execution_authority_tool": _descriptor(
                (ROOT / AUTHORITY_TOOL_RELATIVE).resolve(),
                AUTHORITY_TOOL_RELATIVE,
                "authority tool",
            ),
        },
        "images": {
            "benchmark": validated["benchmark"],
            "restore": {
                "name": validated["restore"]["name"],
                "bytes": validated["restore"]["bytes"],
                "sha256": validated["restore"]["sha256"],
            },
        },
        "execution": {
            "attempt_count": 1,
            "node_count": 2,
            "application_offset": APPLICATION_OFFSET,
            "application_only_writes": True,
            "both_installed_trail_readbacks_before_first_write": True,
            "display_reset_and_visual_preflight_required": True,
            "benchmark_readback_before_radio": True,
            "role_cycles": validated["execution"]["role_cycles"],
            "message_wire_bytes": [48, 48, 64],
            "radio_payload_wire_bytes": 736,
            "transmissions": 14,
            "theoretical_airtime_us": 1_447_424,
            "one_bounded_whole_handshake_restart_per_direction": True,
            "radio_allowed": True,
            "packet_v1_allowed": False,
            "ota1_wrapper_allowed": False,
            "selection_allowed": False,
            "restore_each_touched_node": True,
            "restore_readback_and_hard_reset_required": True,
            "recovery_only_mode_required": True,
            "recovery_requires_benchmark_artifact": False,
        },
        "radio_profile": validated["radio_profile"],
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_noise_xk_radio_cost_attempt",
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
    if authority_path.resolve() != (ROOT / AUTHORITY_RELATIVE).resolve():
        raise ContractError("authority identity mismatch")
    preparation, preparation_raw, unused = _load_validated_preparation()
    authority, authority_raw = _load_canonical(authority_path, "authority")
    validate_authority(
        authority,
        preparation,
        preparation_raw,
        benchmark_path,
        restore_path,
        adapter_path,
        recovery=recovery,
    )
    return _sha256(authority_raw)


def write_new(path: Path, value: dict[str, Any], relative: str) -> None:
    if path.resolve() != (ROOT / relative).resolve():
        raise ContractError("output identity mismatch")
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
                preparation,
                preparation_raw,
                args.benchmark_app.resolve(),
                args.restore_app.resolve(),
                args.adapter.resolve(),
                owner_authorization_granted=args.owner_authorization_granted,
            )
            write_new((ROOT / AUTHORITY_RELATIVE).resolve(), value, AUTHORITY_RELATIVE)
        else:
            value, unused_raw = _load_canonical(
                (ROOT / AUTHORITY_RELATIVE).resolve(), "authority"
            )
        result = validate_authority(
            value,
            preparation,
            preparation_raw,
            args.benchmark_app.resolve(),
            args.restore_app.resolve(),
            args.adapter.resolve(),
        )
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
        return 0
    except BaseException:
        print("ERROR: OT-154 execution authority validation failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
