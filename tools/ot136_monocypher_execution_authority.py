#!/usr/bin/env python3
"""Build and validate the host-only OT-136 executable Monocypher authority.

This tool performs canonical file validation only.  It has no serial, esptool,
subprocess, device-discovery, flash, capture, or reset surface.  OT-136 binds
the OT-135 protocol successor after OT-133 consumed its one attempt on abort.
No attempt or continuing authority is inherited.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-25"

PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-136-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-136-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json"
)
ADAPTER_RELATIVE = "tools/ot136_monocypher_hardware_adapter.py"
CONTRACT_TOOL_RELATIVE = "tools/ot136_monocypher_execution_authority.py"

PREPARATION_SCHEMA = "OT136MEBP0"
PREPARATION_ID = (
    "OT-136-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
AUTHORITY_SCHEMA = "OT136MOAA0"
AUTHORITY_ID = "OT-136-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0"

BENCHMARK_NAME = "ot129_monocypher_protocol_bench.bin"
BENCHMARK_BYTES = 187_680
BENCHMARK_SHA256 = "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"

OT133_AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-133-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json"
)
OT133_AUTHORITY_RAW_SHA256 = (
    "9d8f1a08b7d3ac2257ef6a11eacf6eb99ce70bcf888b293415bdb1a4c6d7a296"
)
OT133_ABORT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json"
)
OT133_ABORT_RAW_SHA256 = (
    "5a57675d1d367968fa1af20c97ab0a7ca4eb005a5b3f1fcf77c52fc552af6d04"
)
OT135_DECISION_RELATIVE = (
    "docs/decisions/0074-host-only-monocypher-byte-bounded-preamble-correction.md"
)
OT135_DECISION_RAW_SHA256 = (
    "cbefae9fb4e8c3b2179b8bbd486f4e2bb03ddd76eb2d3d2921a23f8792942cde"
)
OT135_EVIDENCE_RELATIVE = "tests/hardware/OT-135-2026-08-25.md"
OT135_EVIDENCE_RAW_SHA256 = (
    "1f776274041d8ab9fabbe09ddd15d257ee55c13bb36d06a9ed205a5838ebe9d3"
)

FIXED_RUNTIME_BINDINGS: tuple[tuple[str, str, str], ...] = (
    (
        "coordinator",
        "tools/ot136_monocypher_coordinator.py",
        "3d340194f98b9d99d7833510d19b142e660ee2999d2d4e20000ca13d7f380867",
    ),
    (
        "protocol_transport",
        "tools/ot135_monocypher_protocol_runner.py",
        "e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993",
    ),
    (
        "frame_parser",
        "tools/ot123_monocypher_frames.py",
        "2276aab6246898186804d39b08be342989ad2cf2c804b546b43cbab31350a721",
    ),
    (
        "frame_schema",
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher-result-frame.schema.json",
        "516cab7753d1ca22f59181480df4eed4aec232150035cee6a7a116fbb539c0e0",
    ),
)


class ContractError(ValueError):
    """A closed contract failure without private runtime detail."""


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ContractError("duplicate key")
        value[key] = item
    return value


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


def _safe_file(path: Path, label: str) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise ContractError(f"{label} unavailable")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _require_exact_path(path: Path, relative: str, label: str) -> None:
    if not path.is_absolute() or path.resolve() != (ROOT / relative).resolve():
        raise ContractError(f"{label} identity mismatch")


def _fixed_binding(relative: str, digest: str) -> dict[str, Any]:
    raw = _safe_file(ROOT / relative, "bound input")
    if _sha256(raw) != digest:
        raise ContractError("bound input digest mismatch")
    return {"path": relative, "raw_sha256": digest}


def _derived_binding(path: Path, relative: str, label: str) -> dict[str, Any]:
    _require_exact_path(path, relative, label)
    raw = _safe_file(path, label)
    return {
        "path": relative,
        "raw_sha256": _sha256(raw),
        "digest_source": "derived_from_final_file_at_preparation",
    }


def _image_descriptor(
    path: Path | None,
    name: str,
    size: int,
    digest: str,
    *,
    required: bool,
) -> dict[str, Any]:
    if required:
        if path is None or not path.is_absolute() or path.name != name:
            raise ContractError("image identity mismatch")
        raw = _safe_file(path, "image")
        if len(raw) != size or _sha256(raw) != digest:
            raise ContractError("image digest mismatch")
    elif path is not None:
        raise ContractError("recovery benchmark must be absent")
    return {"name": name, "bytes": size, "sha256": digest}


def _execution_contract() -> dict[str, Any]:
    return {
        "attempt_count": 1,
        "node_count": 2,
        "application_offset": 0x10000,
        "application_only_writes": True,
        "distinct_endpoint_values_required": True,
        "single_bound_flash_capture_backend_required": True,
        "adapter_invokes_frozen_ot136_coordinator_only": True,
        "both_installed_trail_readbacks_before_journal": True,
        "both_installed_trail_readbacks_before_first_write": True,
        "all_preflight_devices_reset_before_journal": True,
        "all_preflight_devices_reset_on_preflight_failure": True,
        "display_reset_and_visual_preflight_required": True,
        "benchmark_readback_before_capture": True,
        "capture_transport_owns_exactly_one_pre_start_reset": True,
        "verified_endpoint_lifecycle_required": True,
        "retry_exact_start_until_exact_ready": True,
        "reset_or_reopen_after_start_allowed": False,
        "control_timeout_seconds": 5,
        "presence_timeout_seconds": 5,
        "start_retry_milliseconds": 250,
        "capture_deadline_seconds": 180,
        "capture_deadline_starts_at_ready": True,
        "capture_deadline_cli_override_allowed": False,
        "restore_each_touched_node": True,
        "restore_readback_and_hard_reset_required": True,
        "recovery_only_mode_required": True,
        "recovery_requires_benchmark_artifact": False,
        "recovery_retry_until_restored_required": True,
        "radio_allowed": False,
        "selection_allowed": False,
    }


def _build_preparation(
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> dict[str, Any]:
    runtime = {
        name: _fixed_binding(relative, digest)
        for name, relative, digest in FIXED_RUNTIME_BINDINGS
    }
    runtime["hardware_adapter"] = _derived_binding(
        adapter_path, ADAPTER_RELATIVE, "hardware adapter"
    )
    runtime["authority_tool"] = _derived_binding(
        ROOT / CONTRACT_TOOL_RELATIVE,
        CONTRACT_TOOL_RELATIVE,
        "authority tool",
    )
    return {
        "schema": PREPARATION_SCHEMA,
        "version": 0,
        "artifact_kind": "monocypher_executable_bundle_preparation",
        "preparation_id": PREPARATION_ID,
        "recorded_date": RECORDED_DATE,
        "status": "executable_bundle_prepared_authority_pending",
        "supersession": {
            "reason": "ot133_consumed_on_abort_ot135_successor_accepted",
            "ot133_authority_consumed": True,
            "ot133_authority_reusable": False,
            "replacement_attempt_inherited": False,
            "ot133_authority": _fixed_binding(
                OT133_AUTHORITY_RELATIVE, OT133_AUTHORITY_RAW_SHA256
            ),
            "ot133_abort": _fixed_binding(
                OT133_ABORT_RELATIVE, OT133_ABORT_RAW_SHA256
            ),
            "ot135_decision": _fixed_binding(
                OT135_DECISION_RELATIVE, OT135_DECISION_RAW_SHA256
            ),
            "ot135_evidence": _fixed_binding(
                OT135_EVIDENCE_RELATIVE, OT135_EVIDENCE_RAW_SHA256
            ),
        },
        "candidate": {
            "id": "monocypher",
            "version": "4.0.3",
            "role": "comparison",
            "operations": [
                "ed25519_sign",
                "ed25519_verify",
                "x25519",
                "chacha20poly1305_encrypt",
                "chacha20poly1305_decrypt",
            ],
            "operations_required": 5,
            "operations_total": 8,
            "selection_eligible": False,
        },
        "runtime": runtime,
        "images": {
            "benchmark": _image_descriptor(
                benchmark_path,
                BENCHMARK_NAME,
                BENCHMARK_BYTES,
                BENCHMARK_SHA256,
                required=not recovery,
            ),
            "restore": _image_descriptor(
                restore_path,
                RESTORE_NAME,
                RESTORE_BYTES,
                RESTORE_SHA256,
                required=True,
            ),
        },
        "execution": _execution_contract(),
        "private_artifacts": {
            "journal": "new_fixed_ot136_private_path_not_published",
            "execution_receipt": "new_fixed_ot136_private_path_not_published",
            "recovery_receipt": "new_fixed_ot136_private_path_not_published",
            "all_prior_private_artifacts_preserved": True,
        },
        "privacy": {
            "anonymous_role_labels_only": True,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "raw_capture_recorded": False,
            "serial_ports_recorded": False,
            "exception_text_recorded": False,
            "failure_code_and_bounded_counters_only": True,
        },
        "claims": {
            "executable_bundle_prepared": True,
            "execution_authorized": False,
            "hardware_accessed": False,
            "firmware_flashed": False,
            "benchmark_executed": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "radio_used": False,
            "score_credit_added": False,
        },
    }


def build_preparation(
    benchmark_path: Path, restore_path: Path, adapter_path: Path
) -> dict[str, Any]:
    return _build_preparation(
        benchmark_path, restore_path, adapter_path, recovery=False
    )


def validate_preparation(
    value: dict[str, Any],
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool = False,
) -> dict[str, Any]:
    if recovery is not True and benchmark_path is None:
        raise ContractError("benchmark unavailable")
    expected = _build_preparation(
        benchmark_path, restore_path, adapter_path, recovery=recovery
    )
    if value != expected:
        raise ContractError("preparation boundary mismatch")
    return {
        "canonical_sha256": _sha256(canonical_bytes(value)),
        "adapter_sha256": value["runtime"]["hardware_adapter"]["raw_sha256"],
        "executable_bundle_prepared": True,
        "execution_authorized": False,
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
    prep = validate_preparation(
        preparation, benchmark_path, restore_path, adapter_path
    )
    if preparation_raw != canonical_document(preparation):
        raise ContractError("preparation is not canonical")
    return {
        "schema": AUTHORITY_SCHEMA,
        "version": 0,
        "artifact_kind": "monocypher_one_attempt_execution_authority",
        "authority_id": AUTHORITY_ID,
        "recorded_date": RECORDED_DATE,
        "status": "authorized_one_attempt_not_executed",
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_monocypher_application_only_attempt",
            "temporary_application_flash_and_exact_trail_restore": True,
            "permanent_firmware_decision": False,
            "visual_display_reset_confirmation_required": True,
        },
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "raw_sha256": _sha256(preparation_raw),
            "canonical_sha256": prep["canonical_sha256"],
            "adapter_path": ADAPTER_RELATIVE,
            "adapter_sha256": prep["adapter_sha256"],
            "immutable": True,
        },
        "supersession": copy.deepcopy(preparation["supersession"]),
        "candidate": copy.deepcopy(preparation["candidate"]),
        "images": copy.deepcopy(preparation["images"]),
        "execution": copy.deepcopy(preparation["execution"]),
        "private_artifacts": copy.deepcopy(preparation["private_artifacts"]),
        "consumption": {
            "consumed_on_success_or_abort": True,
            "continuing_authority": False,
            "reusable": False,
        },
        "claims": {
            "benchmark_executed": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "radio_used": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
            "suite_selected": False,
            "supported_target_proven": False,
        },
    }


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
    if recovery:
        if benchmark_path is not None:
            raise ContractError("recovery benchmark must be absent")
        prep = validate_preparation(
            preparation,
            None,
            restore_path,
            adapter_path,
            recovery=True,
        )
        expected = build_authority_from_validated_preparation(
            preparation, preparation_raw, prep
        )
    else:
        if benchmark_path is None:
            raise ContractError("benchmark unavailable")
        expected = build_authority(
            preparation,
            preparation_raw,
            benchmark_path,
            restore_path,
            adapter_path,
            owner_authorization_granted=True,
        )
    if value != expected:
        raise ContractError("authority boundary mismatch")
    return {
        "canonical_sha256": _sha256(canonical_bytes(value)),
        "phase_two_execution_authorized": True,
        "attempt_count": 1,
        "node_count": 2,
        "reusable": False,
        "benchmark_executed": False,
    }


def build_authority_from_validated_preparation(
    preparation: dict[str, Any], preparation_raw: bytes, prep: dict[str, Any]
) -> dict[str, Any]:
    """Reconstruct authority without touching the benchmark during recovery."""
    if preparation_raw != canonical_document(preparation):
        raise ContractError("preparation is not canonical")
    value = {
        "schema": AUTHORITY_SCHEMA,
        "version": 0,
        "artifact_kind": "monocypher_one_attempt_execution_authority",
        "authority_id": AUTHORITY_ID,
        "recorded_date": RECORDED_DATE,
        "status": "authorized_one_attempt_not_executed",
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_monocypher_application_only_attempt",
            "temporary_application_flash_and_exact_trail_restore": True,
            "permanent_firmware_decision": False,
            "visual_display_reset_confirmation_required": True,
        },
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "raw_sha256": _sha256(preparation_raw),
            "canonical_sha256": prep["canonical_sha256"],
            "adapter_path": ADAPTER_RELATIVE,
            "adapter_sha256": prep["adapter_sha256"],
            "immutable": True,
        },
        "supersession": copy.deepcopy(preparation["supersession"]),
        "candidate": copy.deepcopy(preparation["candidate"]),
        "images": copy.deepcopy(preparation["images"]),
        "execution": copy.deepcopy(preparation["execution"]),
        "private_artifacts": copy.deepcopy(preparation["private_artifacts"]),
        "consumption": {
            "consumed_on_success_or_abort": True,
            "continuing_authority": False,
            "reusable": False,
        },
        "claims": {
            "benchmark_executed": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "radio_used": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
            "suite_selected": False,
            "supported_target_proven": False,
        },
    }
    return value


def load_canonical(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _safe_file(path, label)
    return decode_canonical(raw, label), raw


def validate_execution_authority(
    authority_path: Path,
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> str:
    """Validate the complete live binding and return the authority raw digest."""
    _require_exact_path(authority_path, AUTHORITY_RELATIVE, "authority")
    _require_exact_path(adapter_path, ADAPTER_RELATIVE, "hardware adapter")
    if type(recovery) is not bool:
        raise ContractError("recovery mode mismatch")
    preparation, preparation_raw = load_canonical(
        ROOT / PREPARATION_RELATIVE, "preparation"
    )
    authority, authority_raw = load_canonical(authority_path, "authority")
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
    _require_exact_path(path, relative, "output")
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as handle:
            handle.write(canonical_document(value))
    except FileExistsError as exc:
        raise ContractError("output already exists") from exc
    except OSError as exc:
        raise ContractError("output unavailable") from exc


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("prepare", "validate-preparation", "authorize", "validate-authority"):
        command = commands.add_parser(name)
        command.add_argument("--benchmark-app", type=Path, required=True)
        command.add_argument("--restore-app", type=Path, required=True)
        command.add_argument("--adapter", type=Path, required=True)
    commands.choices["authorize"].add_argument(
        "--owner-authorization-granted", action="store_true"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "prepare":
            value = build_preparation(
                args.benchmark_app, args.restore_app, args.adapter
            )
            write_new(ROOT / PREPARATION_RELATIVE, value, PREPARATION_RELATIVE)
            result = validate_preparation(
                value, args.benchmark_app, args.restore_app, args.adapter
            )
        elif args.command == "validate-preparation":
            value, _ = load_canonical(ROOT / PREPARATION_RELATIVE, "preparation")
            result = validate_preparation(
                value, args.benchmark_app, args.restore_app, args.adapter
            )
        elif args.command == "authorize":
            preparation, preparation_raw = load_canonical(
                ROOT / PREPARATION_RELATIVE, "preparation"
            )
            value = build_authority(
                preparation,
                preparation_raw,
                args.benchmark_app,
                args.restore_app,
                args.adapter,
                owner_authorization_granted=args.owner_authorization_granted,
            )
            write_new(ROOT / AUTHORITY_RELATIVE, value, AUTHORITY_RELATIVE)
            result = validate_authority(
                value,
                preparation,
                preparation_raw,
                args.benchmark_app,
                args.restore_app,
                args.adapter,
            )
        else:
            preparation, preparation_raw = load_canonical(
                ROOT / PREPARATION_RELATIVE, "preparation"
            )
            authority, _ = load_canonical(ROOT / AUTHORITY_RELATIVE, "authority")
            result = validate_authority(
                authority,
                preparation,
                preparation_raw,
                args.benchmark_app,
                args.restore_app,
                args.adapter,
            )
    except (OSError, ContractError):
        print("ERROR: OT-136 executable bundle/authority validation failed")
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
