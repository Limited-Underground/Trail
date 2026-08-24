#!/usr/bin/env python3
"""Strict validator for the one-attempt OT-125 Monocypher corrective retry."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"
)
AUTHORITY_RAW_SHA256 = "b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf"
AUTHORITY_CANONICAL_SHA256 = "c2d14adc765fd46420459957c35896affda6b34e2a6416b5f9f695c83304c57b"
AUTHORITY_PIN = (AUTHORITY_RAW_SHA256, AUTHORITY_CANONICAL_SHA256)
CONSUMED_AUTHORITY_RAW_SHA256 = "765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f"
ABORT_RAW_SHA256 = "f638a6125a14d8fe28412ae0554c6958bdad3a6c2a0a1e83e3ae793bcad4e92c"
PREPARATION_RAW_SHA256 = "a80f06c4b6c0c1c56b5b36ae54b8fddacf36359b8b69abe6f0f4da2bd5d18a89"
PREPARATION_CANONICAL_SHA256 = "a6e9e18c686af1aa5d8fd5ed333519072570053360596c71ba11e71923a4006d"
RUNNER_RAW_SHA256 = "47022c46ce6d911998b5457516e250e3dec7dcc8dbe1e0c8e799a0cacfc23150"
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


def load(path: Path, pin: tuple[str, str] | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        value = json.loads(
            raw.decode("ascii"), object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError("authority unavailable") from exc
    if type(value) is not dict:
        raise ValidationError("authority shape mismatch")
    if pin is not None and (
        _sha256(raw) != pin[0] or _sha256(_canonical(value)) != pin[1]
    ):
        raise ValidationError("authority digest mismatch")
    return value


def _exact_file(relative: str, digest: str) -> Path:
    path = ROOT / relative
    if path.is_symlink() or not path.is_file():
        raise ValidationError("parent unavailable")
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise ValidationError("parent unavailable") from exc
    if _sha256(raw) != digest:
        raise ValidationError("parent digest mismatch")
    return path


def validate_parent_files() -> dict[str, str]:
    paths = {
        "consumed_authority": (
            "tests/benchmarks/crypto/OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json",
            CONSUMED_AUTHORITY_RAW_SHA256,
        ),
        "abort_receipt": (
            "tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json",
            ABORT_RAW_SHA256,
        ),
        "preparation": (
            "tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json",
            PREPARATION_RAW_SHA256,
        ),
        "runner": ("tools/ot125_monocypher_retry_runner.py", RUNNER_RAW_SHA256),
    }
    result: dict[str, str] = {}
    for name, (relative, digest) in paths.items():
        _exact_file(relative, digest)
        result[name] = digest
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
        value["schema"] != "OT125MCRA0"
        or type(value["version"]) is not int or value["version"] != 0
        or value["artifact_kind"] != "monocypher_corrective_retry_authority"
        or value["authority_id"]
        != "OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0"
        or value["recorded_date"] != "2026-08-24"
        or value["status"] != "authorized_one_attempt"
    ):
        raise ValidationError("authority identity mismatch")

    owner = _object(value["owner_authorization"], {
        "granted", "scope", "temporary_application_flash_and_exact_trail_restore",
        "permanent_firmware_decision",
    }, "owner authorization")
    if (
        owner["granted"] is not True
        or owner["scope"] != "one_corrective_two_node_monocypher_comparison_attempt"
        or owner["temporary_application_flash_and_exact_trail_restore"] is not True
        or owner["permanent_firmware_decision"] is not False
    ):
        raise ValidationError("owner authorization mismatch")

    lineage = _object(value["parents"], {
        "consumed_phase_two_authority", "abort_receipt", "preparation",
    }, "parents")
    old = _object(lineage["consumed_phase_two_authority"], {
        "path", "raw_sha256", "consumed", "reusable",
    }, "consumed authority")
    abort = _object(lineage["abort_receipt"], {
        "path", "raw_sha256", "result_admitted", "all_touched_nodes_restored",
    }, "abort receipt")
    prep = _object(lineage["preparation"], {
        "path", "raw_sha256", "canonical_sha256",
    }, "preparation")
    if (
        old != {
            "path": "tests/benchmarks/crypto/OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json",
            "raw_sha256": CONSUMED_AUTHORITY_RAW_SHA256,
            "consumed": True,
            "reusable": False,
        }
        or abort != {
            "path": "tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json",
            "raw_sha256": ABORT_RAW_SHA256,
            "result_admitted": False,
            "all_touched_nodes_restored": True,
        }
        or prep != {
            "path": "tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json",
            "raw_sha256": PREPARATION_RAW_SHA256,
            "canonical_sha256": PREPARATION_CANONICAL_SHA256,
        }
        or parents != {
            "consumed_authority": CONSUMED_AUTHORITY_RAW_SHA256,
            "abort_receipt": ABORT_RAW_SHA256,
            "preparation": PREPARATION_RAW_SHA256,
            "runner": RUNNER_RAW_SHA256,
        }
    ):
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
        "path": "tools/ot125_monocypher_retry_runner.py",
        "raw_sha256": RUNNER_RAW_SHA256,
        "capture_correction": "esptool_hard_reset_then_fresh_serial_open_with_one_bounded_pre_frame_cycle_retry",
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
        "display_reset_and_visual_preflight_required",
        "benchmark_readback_before_capture", "restore_each_touched_node",
        "restore_readback_and_hard_reset_required", "radio_allowed",
    }, "execution")
    if execution != {
        "attempt_count": 1, "node_count": 2, "application_offset": 65536,
        "application_only_writes": True,
        "both_installed_trail_readbacks_before_journal": True,
        "both_installed_trail_readbacks_before_first_write": True,
        "display_reset_and_visual_preflight_required": True,
        "benchmark_readback_before_capture": True,
        "restore_each_touched_node": True,
        "restore_readback_and_hard_reset_required": True,
        "radio_allowed": False,
    }:
        raise ValidationError("execution boundary mismatch")

    private = _object(value["private_artifacts"], {
        "journal", "execution_receipt", "recovery_receipt",
        "legacy_ot123_journal_preserved", "legacy_ot124_receipt_preserved",
    }, "private artifacts")
    if private != {
        "journal": "fixed_private_path_not_published",
        "execution_receipt": "fixed_private_path_not_published",
        "recovery_receipt": "separate_fixed_private_path_not_published",
        "legacy_ot123_journal_preserved": True,
        "legacy_ot124_receipt_preserved": True,
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
    if any(value is not False for value in claims.values()):
        raise ValidationError("authority claim mismatch")
    return {
        "canonical_sha256": _sha256(_canonical(value)),
        "phase_two_execution_authorized": True,
        "benchmark_executed": False,
        "attempt_count": 1,
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
        print("ERROR: OT-125 authority validation failed")
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
