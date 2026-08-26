#!/usr/bin/env python3
"""Build and validate the host-only OT-143 Monocypher execution contract.

This tool performs canonical file validation only.  It has no serial, esptool,
subprocess, device-discovery, flash, capture, or reset surface.  OT-143 binds
the accepted OT-142 corrected target after OT-140 consumed its one attempt on
an exactly restored abort.  No attempt or continuing authority is inherited,
and preparation never grants execution authority.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-26"

PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-143-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-143-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json"
)
ADAPTER_RELATIVE = "tools/ot143_monocypher_hardware_adapter.py"
CONTRACT_TOOL_RELATIVE = "tools/ot143_monocypher_execution_authority.py"

PREPARATION_SCHEMA = "OT143MEBP0"
PREPARATION_ID = (
    "OT-143-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0"
)
AUTHORITY_SCHEMA = "OT143MOAA0"
AUTHORITY_ID = "OT-143-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0"

BENCHMARK_NAME = "ot142_monocypher_corrected_bench.bin"
BENCHMARK_BYTES = 149_824
BENCHMARK_SHA256 = "8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"

OT140_AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-140-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json"
)
OT140_AUTHORITY_RAW_SHA256 = (
    "fa867f3551b069767d2b00187841dc60cf861ee38492a1958ef86ed98398de9a"
)
OT142_TARGET_SOURCE_RELATIVE = (
    "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
    "monocypher_ot142_corrected/main/app_main.c"
)
OT142_TARGET_SOURCE_RAW_SHA256 = (
    "6504cd2de51cad0af856157fa09af0904a5df61c50fc66ad1ee04acfbbab06b3"
)
OT142_REAL_LIBRARY_TEST_RELATIVE = "tests/host/monocypher_real_ed25519_vector_tests.cpp"
OT142_REAL_LIBRARY_TEST_RAW_SHA256 = (
    "c9cb0c844e13c653dec1857d5c20d19fe26c45c209934570634964e9edb7c9ed"
)
OT142_TARGET_TEST_RELATIVE = "tests/host/ot142_monocypher_corrected_target_tests.py"
OT142_TARGET_TEST_RAW_SHA256 = (
    "2d5caf6f1669f81b8df7ad9d38ba32d6968c4f31ec3d3594adec2599cbcaf4aa"
)
OT143_BUILD_EVIDENCE_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json"
)
OT143_BUILD_EVIDENCE_RAW_SHA256 = (
    "1045d5d59c26775b8a8c2a8520226fcb224b566f5554b5ff495b9876a8af8c37"
)
OT142_ACCEPTED_COMMIT = "ecf6f49bb8bb57469ad6a58c5e71e533dca3c893"
OT142_ESP_IDF_COMMIT = "7101770dc6db2667b3c477cc31365dd1acd6db4e"

OT142_ARTIFACT_TUPLE: tuple[tuple[str, str, int, str], ...] = (
    ("application_bin", BENCHMARK_NAME, BENCHMARK_BYTES, BENCHMARK_SHA256),
    (
        "application_elf",
        "ot142_monocypher_corrected_bench.elf",
        3_314_668,
        "ec74f80422a5b9722e09342d69e190282a4117b4888c96838cf915dc760466d1",
    ),
    (
        "linker_map",
        "ot142_monocypher_corrected_bench.map",
        2_849_996,
        "c9a2789451305417dc2cb523ba6284a9c4ca8014724d051f829b4f5a7b31af13",
    ),
    (
        "bootloader_bin",
        "bootloader.bin",
        15_216,
        "604af9d70953d917734f45b4c1cb764a23c17c8e3e5b28e11e1f3f6a02ef1c38",
    ),
    (
        "partition_table_bin",
        "partition-table.bin",
        3_072,
        "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab",
    ),
    (
        "generated_sdkconfig",
        "sdkconfig",
        57_516,
        "5807fe7fc6d4ef3325f06099674f07080660eabd20e0b078225247605c814817",
    ),
)

FIXED_RUNTIME_BINDINGS: tuple[tuple[str, str, str], ...] = (
    (
        "coordinator",
        "tools/ot143_monocypher_coordinator.py",
        "937f37a01beef9df8eb327c741ae7453c3f3d565c02977f7ae7ff5711f48579b",
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


def _fixed_lf_text_binding(relative: str, digest: str) -> dict[str, Any]:
    raw = _safe_file(ROOT / relative, "bound text input")
    canonical = raw.replace(b"\r\n", b"\n")
    if b"\r" in canonical or _sha256(canonical) != digest:
        raise ContractError("bound text input digest mismatch")
    return {
        "path": relative,
        "canonical_lf_sha256": digest,
        "digest_policy": "canonical_lf",
    }


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


def _artifact_tuple_descriptor(
    benchmark_path: Path | None, *, required: bool
) -> list[dict[str, Any]]:
    if required:
        if (
            benchmark_path is None
            or not benchmark_path.is_absolute()
            or benchmark_path.name != BENCHMARK_NAME
        ):
            raise ContractError("artifact tuple identity mismatch")
        artifact_root = benchmark_path.parent
        for _, name, size, digest in OT142_ARTIFACT_TUPLE:
            raw = _safe_file(artifact_root / name, "artifact tuple")
            if len(raw) != size or _sha256(raw) != digest:
                raise ContractError("artifact tuple digest mismatch")
    elif benchmark_path is not None:
        raise ContractError("recovery benchmark must be absent")
    return [
        {"role": role, "name": name, "bytes": size, "sha256": digest}
        for role, name, size, digest in OT142_ARTIFACT_TUPLE
    ]


def _execution_contract() -> dict[str, Any]:
    return {
        "attempt_count": 1,
        "node_count": 2,
        "application_offset": 0x10000,
        "application_only_writes": True,
        "distinct_endpoint_values_required": True,
        "single_bound_flash_capture_backend_required": True,
        "adapter_invokes_frozen_ot143_coordinator_only": True,
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
            "reason": "ot140_authority_consumed_ot142_corrected_successor_accepted",
            "ot140_authority_consumed": True,
            "ot140_authority_reusable": False,
            "replacement_attempt_inherited": False,
            "ot140_authority": _fixed_binding(
                OT140_AUTHORITY_RELATIVE, OT140_AUTHORITY_RAW_SHA256
            ),
            "ot140_terminal_boundary": "fail_closed_exact_restoration_no_result",
        },
        "accepted_corrected_target": {
            "accepted_commit": OT142_ACCEPTED_COMMIT,
            "target_source": _fixed_lf_text_binding(
                OT142_TARGET_SOURCE_RELATIVE, OT142_TARGET_SOURCE_RAW_SHA256
            ),
            "real_library_test": _fixed_binding(
                OT142_REAL_LIBRARY_TEST_RELATIVE,
                OT142_REAL_LIBRARY_TEST_RAW_SHA256,
            ),
            "target_test": _fixed_binding(
                OT142_TARGET_TEST_RELATIVE, OT142_TARGET_TEST_RAW_SHA256
            ),
            "build_evidence": _fixed_binding(
                OT143_BUILD_EVIDENCE_RELATIVE,
                OT143_BUILD_EVIDENCE_RAW_SHA256,
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
        "corrected_target": {
            "esp_idf_commit": OT142_ESP_IDF_COMMIT,
            "source_input_count": 14,
            "artifact_tuple": _artifact_tuple_descriptor(
                benchmark_path, required=not recovery
            ),
            "only_application_bin_is_writable": True,
        },
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
            "journal": "new_fixed_ot143_private_path_not_published",
            "execution_receipt": "new_fixed_ot143_private_path_not_published",
            "recovery_receipt": "new_fixed_ot143_private_path_not_published",
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
        print("ERROR: OT-143 executable bundle/authority validation failed")
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())








