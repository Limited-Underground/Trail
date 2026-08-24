#!/usr/bin/env python3
"""Build and validate the host-only OT-130 immutable execution contract.

This module never opens a device, imports a serial backend, invokes esptool, or
executes a benchmark.  It prepares two canonical documents only after the
future OT-130 restoration-safe coordinator exists:

* an immutable firmware/transport/coordinator/restoration preparation; and
* one fresh, non-reusable, two-node application-only execution authority.

The coordinator digest is deliberately derived from the final file at runtime.
There is no placeholder digest that could accidentally become authority.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]

RECORDED_DATE = "2026-08-24"
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-130-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json"
)
COORDINATOR_RELATIVE = "tools/ot130_monocypher_coordinator.py"
CONTRACT_TOOL_RELATIVE = "tools/ot130_monocypher_bundle_authority.py"

PREPARATION_SCHEMA = "OT130MEBP0"
PREPARATION_ID = (
    "OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0"
)
AUTHORITY_SCHEMA = "OT130MOAA0"
AUTHORITY_ID = "OT-130-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0"

APPLICATION_OFFSET = 0x10000
NODE_COUNT = 2
ATTEMPT_COUNT = 1
BENCHMARK_NAME = "ot129_monocypher_protocol_bench.bin"
BENCHMARK_BYTES = 187_680
BENCHMARK_SHA256 = "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"
SDKCONFIG_BYTES = 106_913
SDKCONFIG_SHA256 = "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"

SOURCE_BINDINGS: tuple[tuple[str, str], ...] = (
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/CMakeLists.txt",
        "7fe5e93aff6f130b88cc0eed244cb1a21163693483cf2bf33dec184a7f9d08e3",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/partitions.csv",
        "4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/main/CMakeLists.txt",
        "ad8f3ad9d5b836a5a18e1f7059e0bbaf3adb238e058bd0b1e9021cd2f201621c",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/main/app_main.c",
        "fac7a9375a5dba5366215dc0eab0a03a83cfd22fd50a2ac563f1c378cb7aae2b",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/main/ot129_control_protocol.c",
        "d10b9e6769676530c4eacd7e31bbf2192f293f1fa1099138673525bc395bdcdf",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "monocypher_ot129/main/ot129_control_protocol.h",
        "14e2896e43e9a873ffb0fbfc4ec01c371095f39b42a6f82b2544ecf0f7c57e76",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
        "include/ot121_benchmark_frame.h",
        "cc5a4596400a8a2766e66fcab6b7d51dceecbc4f3c1be4055a2581d400415d4c",
    ),
    (
        "firmware/targets/heltec_v4_bench/sdkconfig.defaults",
        "a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb",
    ),
    (
        "tests/benchmarks/crypto/esp_idf/ot120_candidate_builds/"
        "reproducible.defaults",
        "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6",
    ),
    (
        "tests/benchmarks/crypto/adapters/monocypher_api_v0/"
        "monocypher_benchmark_api.c",
        "e12b800841c6c8347cdf08d05768f2cfbc83ee271fdae7616f8a3b16e4263e59",
    ),
    (
        "tests/benchmarks/crypto/adapters/monocypher_api_v0/"
        "monocypher_benchmark_api.h",
        "110f5b54cf37538e30450d73a3402807eb59143f2ceb528b12280b7725e52072",
    ),
    (
        "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c",
        "f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123",
    ),
    (
        "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.h",
        "fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3",
    ),
    (
        "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/"
        "monocypher-ed25519.c",
        "ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453",
    ),
    (
        "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/"
        "monocypher-ed25519.h",
        "3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce",
    ),
)

RUNTIME_BINDINGS: tuple[tuple[str, str, str], ...] = (
    (
        "protocol_transport",
        "tools/ot129_monocypher_protocol_runner.py",
        "f95c5e673a698dc1392de08611abd3cb38f63dec45423ae2100f2f59265e9c9e",
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

LINEAGE_BINDINGS: tuple[tuple[str, str, str], ...] = (
    (
        "ot123_preparation",
        "tests/benchmarks/crypto/"
        "OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json",
        "a80f06c4b6c0c1c56b5b36ae54b8fddacf36359b8b69abe6f0f4da2bd5d18a89",
    ),
    (
        "consumed_ot127_authority",
        "tests/benchmarks/crypto/"
        "OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json",
        "d043fc7dc700ce2c43914fe079b4a594b04e730252d8cb490f02097d9472448b",
    ),
    (
        "ot128_abort_receipt",
        "tests/benchmarks/crypto/"
        "OT-128-OT005-MONOCYPHER-SECOND-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json",
        "b34b4f761d77a5d952e50eda4c48d7629b5185043cc84c146dc9b974ba36f09e",
    ),
    (
        "ot129_decision",
        "docs/decisions/0068-host-only-monocypher-start-ready-protocol-correction.md",
        "abc6212fde0cc4dd3df612c746ca8b9a1ce82869897beb911a883bb8e42c2448",
    ),
    (
        "ot129_evidence",
        "tests/hardware/OT-129-2026-08-24.md",
        "f101211094047c65d7326ca3a796933105987f37cff7003d647553adfb79e866",
    ),
)


class ContractError(ValueError):
    """A public, fail-closed contract error without private detail."""


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


def _fixed_binding(relative: str, expected_sha256: str) -> dict[str, Any]:
    raw = _safe_file(ROOT / relative, "bound input")
    if _sha256(raw) != expected_sha256:
        raise ContractError("bound input digest mismatch")
    return {"path": relative, "raw_sha256": expected_sha256}


def _derived_binding(relative: str) -> dict[str, Any]:
    raw = _safe_file(ROOT / relative, "coordinator")
    return {
        "path": relative,
        "raw_sha256": _sha256(raw),
        "digest_source": "derived_from_final_file_at_preparation",
    }


def _image(path: Path, name: str, size: int, digest: str) -> dict[str, Any]:
    if path.name != name:
        raise ContractError("image identity mismatch")
    raw = _safe_file(path, "image")
    if len(raw) != size or _sha256(raw) != digest:
        raise ContractError("image digest mismatch")
    return {"name": name, "bytes": size, "sha256": digest}


def _ordered_bindings(
    values: Iterable[tuple[str, str]],
) -> list[dict[str, Any]]:
    return [_fixed_binding(path, digest) for path, digest in values]


def _named_bindings(
    values: Iterable[tuple[str, str, str]],
) -> dict[str, dict[str, Any]]:
    return {
        name: _fixed_binding(path, digest)
        for name, path, digest in values
    }


def _execution_contract() -> dict[str, Any]:
    return {
        "attempt_count": ATTEMPT_COUNT,
        "node_count": NODE_COUNT,
        "application_offset": APPLICATION_OFFSET,
        "application_only_writes": True,
        "both_installed_trail_readbacks_before_journal": True,
        "both_installed_trail_readbacks_before_first_write": True,
        "all_preflight_devices_reset_before_journal": True,
        "all_preflight_devices_reset_on_preflight_failure": True,
        "benchmark_readback_before_capture": True,
        "display_reset_and_visual_preflight_required": True,
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
        "distinct_endpoint_values_required": True,
        "single_bound_flash_capture_backend_required": True,
        "radio_allowed": False,
        "selection_allowed": False,
    }


def build_preparation(benchmark_path: Path, restore_path: Path) -> dict[str, Any]:
    runtime = _named_bindings(RUNTIME_BINDINGS)
    runtime["coordinator"] = _derived_binding(COORDINATOR_RELATIVE)
    runtime["contract_tool"] = _derived_binding(CONTRACT_TOOL_RELATIVE)
    return {
        "schema": PREPARATION_SCHEMA,
        "version": 0,
        "artifact_kind": "monocypher_immutable_execution_bundle_preparation",
        "preparation_id": PREPARATION_ID,
        "recorded_date": RECORDED_DATE,
        "status": "immutable_bundle_prepared_authority_pending",
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
        "lineage": _named_bindings(LINEAGE_BINDINGS),
        "source_inputs": _ordered_bindings(SOURCE_BINDINGS),
        "runtime": runtime,
        "images": {
            "benchmark": _image(
                benchmark_path, BENCHMARK_NAME, BENCHMARK_BYTES, BENCHMARK_SHA256
            ),
            "restore": _image(
                restore_path, RESTORE_NAME, RESTORE_BYTES, RESTORE_SHA256
            ),
        },
        "build": {
            "project_path": (
                "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
                "monocypher_ot129"
            ),
            "application_name": "ot129_monocypher_protocol_bench",
            "project_version": "ot129-protocol-v0",
            "idf_target": "esp32s3",
            "esp_idf_version": "v6.0.2",
            "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
            "ccache_allowed": False,
            "component_manager_network_allowed": False,
            "accepted_compile_only_build_reference": "ot129_evidence",
            "fresh_independent_builds_verified": 2,
            "initial_build_directories_absent": True,
            "compiler_warning_count_each": 0,
            "artifact_tuple_equality_verified": True,
            "artifact_tuple": {
                "application_bin": {
                    "bytes": 187680,
                    "sha256": "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268",
                },
                "application_elf": {
                    "bytes": 3847764,
                    "sha256": "f62612da6bae5fc202250a930bee66bbc171c9643e48a9014c75488883175d3c",
                },
                "linker_map": {
                    "bytes": 3737462,
                    "sha256": "8ac125a0f280cd26feb4073d1b4323e5ade0eb5d86cd1b37d0aeff549df1e4e1",
                },
                "sdkconfig": {
                    "bytes": 106913,
                    "sha256": "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f",
                },
                "bootloader_bin": {
                    "bytes": 22480,
                    "sha256": "96e83ebe4434cd6c9049a59f396b4f8bd06c159b40259da573bdb701c571eca5",
                },
                "partition_table_bin": {
                    "bytes": 3072,
                    "sha256": "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab",
                },
            },
            "application_digest_bound": True,
            "generated_sdkconfig": {
                "bytes": SDKCONFIG_BYTES,
                "sha256": SDKCONFIG_SHA256,
            },
        },
        "execution": _execution_contract(),
        "private_artifacts": {
            "journal": "new_fixed_private_path_not_published",
            "execution_receipt": "new_fixed_private_path_not_published",
            "recovery_receipt": "separate_new_fixed_private_path_not_published",
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
            "immutable_bundle_prepared": True,
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


def validate_preparation(
    value: dict[str, Any], benchmark_path: Path, restore_path: Path
) -> dict[str, Any]:
    expected = build_preparation(benchmark_path, restore_path)
    if value != expected:
        raise ContractError("preparation boundary mismatch")
    coordinator = value["runtime"]["coordinator"]
    return {
        "canonical_sha256": _sha256(canonical_bytes(value)),
        "coordinator_sha256": coordinator["raw_sha256"],
        "immutable_bundle_prepared": True,
        "execution_authorized": False,
    }


def build_authority(
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark_path: Path,
    restore_path: Path,
    *,
    owner_authorization_granted: bool,
) -> dict[str, Any]:
    if owner_authorization_granted is not True:
        raise ContractError("owner authorization absent")
    prep_result = validate_preparation(preparation, benchmark_path, restore_path)
    if preparation_raw != canonical_document(preparation):
        raise ContractError("preparation is not canonical")
    coordinator = preparation["runtime"]["coordinator"]
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
        },
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "raw_sha256": _sha256(preparation_raw),
            "canonical_sha256": prep_result["canonical_sha256"],
            "coordinator_path": coordinator["path"],
            "coordinator_sha256": coordinator["raw_sha256"],
            "immutable": True,
        },
        "lineage": {
            "consumed_ot127_authority_raw_sha256": dict(
                (name, digest) for name, _, digest in LINEAGE_BINDINGS
            )["consumed_ot127_authority"],
            "ot128_abort_receipt_raw_sha256": dict(
                (name, digest) for name, _, digest in LINEAGE_BINDINGS
            )["ot128_abort_receipt"],
            "ot129_protocol_correction_accepted": True,
            "all_prior_authorities_nonreusable": True,
            "latest_admitted_result_remains_ot122": True,
        },
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
    benchmark_path: Path,
    restore_path: Path,
) -> dict[str, Any]:
    expected = build_authority(
        preparation,
        preparation_raw,
        benchmark_path,
        restore_path,
        owner_authorization_granted=True,
    )
    if value != expected:
        raise ContractError("authority boundary mismatch")
    return {
        "canonical_sha256": _sha256(canonical_bytes(value)),
        "phase_two_execution_authorized": True,
        "attempt_count": ATTEMPT_COUNT,
        "node_count": NODE_COUNT,
        "reusable": False,
        "benchmark_executed": False,
    }


def load_canonical(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _safe_file(path, label)
    return decode_canonical(raw, label), raw


def write_new(path: Path, value: dict[str, Any], expected_relative: str) -> None:
    expected = (ROOT / expected_relative).resolve()
    if path.resolve() != expected:
        raise ContractError("output identity mismatch")
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
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("prepare", "validate-preparation", "authorize", "validate-authority"):
        command = subparsers.add_parser(name)
        command.add_argument("--benchmark-app", type=Path, required=True)
        command.add_argument("--restore-app", type=Path, required=True)
    subparsers.choices["authorize"].add_argument(
        "--owner-authorization-granted", action="store_true"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "prepare":
            value = build_preparation(args.benchmark_app, args.restore_app)
            write_new(ROOT / PREPARATION_RELATIVE, value, PREPARATION_RELATIVE)
            result = validate_preparation(value, args.benchmark_app, args.restore_app)
        elif args.command == "validate-preparation":
            value, _ = load_canonical(ROOT / PREPARATION_RELATIVE, "preparation")
            result = validate_preparation(value, args.benchmark_app, args.restore_app)
        elif args.command == "authorize":
            preparation, preparation_raw = load_canonical(
                ROOT / PREPARATION_RELATIVE, "preparation"
            )
            value = build_authority(
                preparation,
                preparation_raw,
                args.benchmark_app,
                args.restore_app,
                owner_authorization_granted=args.owner_authorization_granted,
            )
            write_new(ROOT / AUTHORITY_RELATIVE, value, AUTHORITY_RELATIVE)
            result = validate_authority(
                value,
                preparation,
                preparation_raw,
                args.benchmark_app,
                args.restore_app,
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
            )
    except (OSError, ContractError):
        print("ERROR: OT-130 immutable bundle/authority validation failed")
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
