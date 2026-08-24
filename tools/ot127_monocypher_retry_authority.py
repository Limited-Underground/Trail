#!/usr/bin/env python3
"""Strict validator for the one-attempt OT-127 Monocypher corrective retry."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"
)
AUTHORITY_RAW_SHA256 = "d043fc7dc700ce2c43914fe079b4a594b04e730252d8cb490f02097d9472448b"
AUTHORITY_CANONICAL_SHA256 = "126135ca4c07670e17a0c4fee36399d04295461d193ac4f6e02ecca86360724e"
AUTHORITY_PIN = (AUTHORITY_RAW_SHA256, AUTHORITY_CANONICAL_SHA256)
CONSUMED_AUTHORITY_RAW_SHA256 = "b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf"
ABORT_RAW_SHA256 = "247b0b80e64a3f6bf6654be279e90dcbd80a067c52ef861313a6f370c0355941"
PREPARATION_RAW_SHA256 = "a80f06c4b6c0c1c56b5b36ae54b8fddacf36359b8b69abe6f0f4da2bd5d18a89"
PREPARATION_CANONICAL_SHA256 = "a6e9e18c686af1aa5d8fd5ed333519072570053360596c71ba11e71923a4006d"
RUNNER_RAW_SHA256 = "ff81188b1f211aaf504192d3827147b2f572b29e895e45eeeee1bc505ffb5438"
BENCHMARK_SHA256 = "5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64"
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"


class ValidationError(ValueError):
    """Fail-closed public validation error."""


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_: str) -> None:
    raise ValidationError("invalid JSON constant")


def _canonical(value: Any) -> bytes:
    try:
        return json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
            allow_nan=False,
        ).encode("ascii")
    except (TypeError, ValueError, UnicodeError) as exc:
        raise ValidationError("invalid canonical JSON") from exc


def _decode_json(raw: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(
            raw.decode("ascii"), object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"{label} unavailable") from exc
    if type(value) is not dict:
        raise ValidationError(f"{label} shape mismatch")
    return value


def load(path: Path, pin: tuple[str, str] | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise ValidationError("authority unavailable") from exc
    value = _decode_json(raw, "authority")
    if pin is not None and (
        _sha256(raw) != pin[0] or _sha256(_canonical(value)) != pin[1]
    ):
        raise ValidationError("authority digest mismatch")
    return value


def _exact_file(relative: str, digest: str) -> tuple[Path, bytes]:
    path = ROOT / relative
    if path.is_symlink() or not path.is_file():
        raise ValidationError("parent unavailable")
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise ValidationError("parent unavailable") from exc
    if _sha256(raw) != digest:
        raise ValidationError("parent digest mismatch")
    return path, raw


def validate_parent_files() -> dict[str, str]:
    paths = {
        "consumed_authority": (
            "tests/benchmarks/crypto/OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json",
            CONSUMED_AUTHORITY_RAW_SHA256,
        ),
        "abort_receipt": (
            "tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json",
            ABORT_RAW_SHA256,
        ),
        "preparation": (
            "tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json",
            PREPARATION_RAW_SHA256,
        ),
        "runner": ("tools/ot127_monocypher_retry_runner.py", RUNNER_RAW_SHA256),
    }
    result: dict[str, str] = {}
    abort_raw: bytes | None = None
    for name, (relative, digest) in paths.items():
        _, raw = _exact_file(relative, digest)
        result[name] = digest
        if name == "abort_receipt":
            abort_raw = raw
    if abort_raw is None:
        raise ValidationError("abort receipt unavailable")
    abort = _decode_json(abort_raw, "abort receipt")
    if (
        abort.get("schema") != "OTMCRAR0"
        or abort.get("version") != 0
        or abort.get("result")
        != "monocypher_corrective_retry_execution_aborted_all_devices_restored_and_running_trail"
        or abort.get("node_count") != 2
        or abort.get("all_touched_nodes_restored") is not True
        or abort.get("all_devices_trail_application_verified") is not True
        or abort.get("all_devices_runtime_reset_complete") is not True
        or abort.get("two_usb_endpoints_returned") is not True
        or type(abort.get("abort")) is not dict
        or abort["abort"].get("root_cause_confirmed") is not True
        or abort["abort"].get("benchmark_result_admitted") is not False
        or type(abort.get("authority")) is not dict
        or abort["authority"].get("consumed_by_abort") is not True
        or abort["authority"].get("reusable") is not False
    ):
        raise ValidationError("abort receipt boundary mismatch")
    return result


def _object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        raise ValidationError(f"{label} shape mismatch")
    return value


def validate_authority(
    value: dict[str, Any], parents: dict[str, str]
) -> dict[str, Any]:
    _object(value, {
        "schema", "version", "artifact_kind", "authority_id", "recorded_date",
        "status", "owner_authorization", "parents", "candidate", "runner",
        "images", "execution", "private_artifacts", "consumption", "claims",
    }, "authority")
    if (
        value["schema"] != "OT127MCRA0"
        or type(value["version"]) is not int or value["version"] != 0
        or value["artifact_kind"] != "monocypher_corrective_retry_authority"
        or value["authority_id"]
        != "OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0"
        or value["recorded_date"] != "2026-08-24"
        or value["status"] != "authorized_one_attempt"
    ):
        raise ValidationError("authority identity mismatch")

    owner = _object(value["owner_authorization"], {
        "granted", "scope", "temporary_application_flash_and_exact_trail_restore",
        "permanent_firmware_decision",
    }, "owner authorization")
    if owner != {
        "granted": True,
        "scope": "one_corrective_two_node_monocypher_comparison_attempt",
        "temporary_application_flash_and_exact_trail_restore": True,
        "permanent_firmware_decision": False,
    }:
        raise ValidationError("owner authorization mismatch")

    lineage = _object(value["parents"], {
        "consumed_corrective_retry_authority", "abort_receipt", "preparation",
    }, "parents")
    if lineage["consumed_corrective_retry_authority"] != {
        "path": "tests/benchmarks/crypto/OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json",
        "raw_sha256": CONSUMED_AUTHORITY_RAW_SHA256,
        "consumed": True,
        "reusable": False,
    } or lineage["abort_receipt"] != {
        "path": "tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json",
        "raw_sha256": ABORT_RAW_SHA256,
        "result_admitted": False,
        "all_touched_nodes_restored": True,
        "all_devices_trail_application_verified": True,
        "all_devices_runtime_reset_complete": True,
    } or lineage["preparation"] != {
        "path": "tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json",
        "raw_sha256": PREPARATION_RAW_SHA256,
        "canonical_sha256": PREPARATION_CANONICAL_SHA256,
    } or parents != {
        "consumed_authority": CONSUMED_AUTHORITY_RAW_SHA256,
        "abort_receipt": ABORT_RAW_SHA256,
        "preparation": PREPARATION_RAW_SHA256,
        "runner": RUNNER_RAW_SHA256,
    }:
        raise ValidationError("authority lineage mismatch")

    candidate = _object(value["candidate"], {
        "id", "role", "version", "operations", "operations_required",
        "operations_total", "selection_eligible",
    }, "candidate")
    if candidate != {
        "id": "monocypher", "role": "comparison", "version": "4.0.3",
        "operations": [
            "ed25519_sign", "ed25519_verify", "x25519",
            "chacha20poly1305_encrypt", "chacha20poly1305_decrypt",
        ],
        "operations_required": 5, "operations_total": 8,
        "selection_eligible": False,
    }:
        raise ValidationError("candidate boundary mismatch")

    runner = _object(value["runner"], {
        "path", "raw_sha256", "capture_correction",
    }, "runner")
    if runner != {
        "path": "tools/ot127_monocypher_retry_runner.py",
        "raw_sha256": RUNNER_RAW_SHA256,
        "capture_correction": "fresh_reset_then_fresh_serial_open_with_fixed_initial_frame_grace_and_fixed_capture_deadline",
    }:
        raise ValidationError("runner boundary mismatch")

    images = _object(value["images"], {"benchmark", "restore"}, "images")
    if images != {
        "benchmark": {
            "name": "ot123_monocypher_candidate_bench.bin", "bytes": 186640,
            "sha256": BENCHMARK_SHA256,
        },
        "restore": {
            "name": "opentrail_heltec_v4_bench.bin", "bytes": 473152,
            "sha256": RESTORE_SHA256,
        },
    }:
        raise ValidationError("image boundary mismatch")

    execution = _object(value["execution"], {
        "attempt_count", "node_count", "application_offset",
        "application_only_writes", "both_installed_trail_readbacks_before_journal",
        "both_installed_trail_readbacks_before_first_write",
        "all_preflight_devices_reset_before_journal",
        "all_preflight_devices_reset_on_preflight_failure",
        "display_reset_and_visual_preflight_required",
        "benchmark_readback_before_capture", "fresh_reset_before_each_serial_open",
        "fresh_serial_open_per_capture_cycle", "initial_frame_grace_seconds",
        "capture_deadline_seconds", "capture_deadline_cli_override_allowed",
        "restore_each_touched_node", "restore_readback_and_hard_reset_required",
        "radio_allowed", "selection_allowed",
    }, "execution")
    if execution != {
        "attempt_count": 1, "node_count": 2, "application_offset": 65536,
        "application_only_writes": True,
        "both_installed_trail_readbacks_before_journal": True,
        "both_installed_trail_readbacks_before_first_write": True,
        "all_preflight_devices_reset_before_journal": True,
        "all_preflight_devices_reset_on_preflight_failure": True,
        "display_reset_and_visual_preflight_required": True,
        "benchmark_readback_before_capture": True,
        "fresh_reset_before_each_serial_open": True,
        "fresh_serial_open_per_capture_cycle": True,
        "initial_frame_grace_seconds": 10,
        "capture_deadline_seconds": 180,
        "capture_deadline_cli_override_allowed": False,
        "restore_each_touched_node": True,
        "restore_readback_and_hard_reset_required": True,
        "radio_allowed": False,
        "selection_allowed": False,
    }:
        raise ValidationError("execution boundary mismatch")
    if (
        type(execution["initial_frame_grace_seconds"]) is not int
        or execution["initial_frame_grace_seconds"] < 10
        or type(execution["capture_deadline_seconds"]) is not int
        or execution["capture_deadline_seconds"] != 180
    ):
        raise ValidationError("capture timing boundary mismatch")

    private = _object(value["private_artifacts"], {
        "journal", "execution_receipt", "recovery_receipt",
        "all_prior_private_artifacts_preserved",
    }, "private artifacts")
    if private != {
        "journal": "fixed_private_path_not_published",
        "execution_receipt": "fixed_private_path_not_published",
        "recovery_receipt": "separate_fixed_private_path_not_published",
        "all_prior_private_artifacts_preserved": True,
    }:
        raise ValidationError("private artifact boundary mismatch")

    consumption = _object(value["consumption"], {
        "consumed_on_success_or_abort", "continuing_authority", "reusable",
    }, "consumption")
    if consumption != {
        "consumed_on_success_or_abort": True,
        "continuing_authority": False,
        "reusable": False,
    }:
        raise ValidationError("consumption boundary mismatch")

    claims = _object(value["claims"], {
        "benchmark_executed", "candidate_selected", "phase_two_complete",
        "radio_used", "regulatory_acceptance_proven", "score_credit_added",
        "suite_selected", "supported_target_proven",
    }, "claims")
    if any(item is not False for item in claims.values()):
        raise ValidationError("authority claim mismatch")
    return {
        "canonical_sha256": _sha256(_canonical(value)),
        "phase_two_execution_authorized": True,
        "benchmark_executed": False,
        "attempt_count": 1,
        "capture_deadline_seconds": 180,
        "initial_frame_grace_seconds": 10,
        "reusable": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authority", type=Path, default=AUTHORITY_PATH)
    args = parser.parse_args(argv)
    try:
        if args.authority.resolve() != AUTHORITY_PATH.resolve():
            raise ValidationError("authority identity mismatch")
        parents = validate_parent_files()
        result = validate_authority(load(args.authority, AUTHORITY_PIN), parents)
    except (OSError, ValidationError):
        print("ERROR: OT-127 authority validation failed")
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
