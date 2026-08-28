#!/usr/bin/env python3
"""OT-160 coordinator binding for the corrected reset-aware Noise XK successor.

The complete restoration-safe state machine remains the immutable OT-153
coordinator. This module creates a fresh private-state namespace, binds that
state machine to the hash-locked OT-156 runner, and keeps source-file hashing
separate from the inherited byte-hash helper used by application readback. It
grants no authority and performs no operation merely by being imported.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
FROZEN_COORDINATOR_PATH = ROOT / "tools" / "ot153_noise_xk_radio_coordinator.py"
SUCCESSOR_RUNNER_PATH = ROOT / "tools" / "ot156_noise_xk_radio_runner.py"
FROZEN_COORDINATOR_SHA256 = "6635c73c6952b322ec1d72043a80f637b2cc04b70c1763f578ea4f38559aeaf3"
SUCCESSOR_RUNNER_SHA256 = "81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244"


def _source_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _load(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("successor_source_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def sources_match() -> bool:
    try:
        return (
            _source_sha256(FROZEN_COORDINATOR_PATH) == FROZEN_COORDINATOR_SHA256
            and _source_sha256(SUCCESSOR_RUNNER_PATH) == SUCCESSOR_RUNNER_SHA256
        )
    except OSError:
        return False


if not sources_match():
    raise RuntimeError("successor_source_mismatch")

runner = _load("_ot160_reset_aware_runner", SUCCESSOR_RUNNER_PATH)
frozen = _load("_ot160_frozen_ot153_coordinator", FROZEN_COORDINATOR_PATH)

# The loaded module is private to OT-160. Patch only the accepted successor
# runner and private record namespace; every restoration and fail-closed rule
# remains the frozen OT-153 implementation. There is intentionally no local
# `_sha256`: module fallback exposes the frozen byte-hash helper to adapters.
PRIVATE_ROOT = ROOT / ".private"
JOURNAL_PATH = PRIVATE_ROOT / "ot160-noise-xk-radio-execution-journal.json"
EXECUTION_RECEIPT_PATH = PRIVATE_ROOT / "ot160-noise-xk-radio-execution-receipt.json"
RECOVERY_RECEIPT_PATH = PRIVATE_ROOT / "ot160-noise-xk-radio-recovery-receipt.json"
JOURNAL_SCHEMA = "OT160NXJ0"
RECEIPT_SCHEMA = "OT160NXCR0"
RUNNER_NAME = SUCCESSOR_RUNNER_PATH.name

frozen.runner = runner
frozen._RUNNER_PATH = SUCCESSOR_RUNNER_PATH
frozen.RUNNER_NAME = RUNNER_NAME
frozen.PRIVATE_ROOT = PRIVATE_ROOT
frozen.JOURNAL_PATH = JOURNAL_PATH
frozen.EXECUTION_RECEIPT_PATH = EXECUTION_RECEIPT_PATH
frozen.RECOVERY_RECEIPT_PATH = RECOVERY_RECEIPT_PATH
frozen.JOURNAL_SCHEMA = JOURNAL_SCHEMA
frozen.RECEIPT_SCHEMA = RECEIPT_SCHEMA


def _private_paths_valid() -> bool:
    root = frozen.ROOT
    private_root = frozen.PRIVATE_ROOT
    expected = {
        frozen.JOURNAL_PATH: "ot160-noise-xk-radio-execution-journal.json",
        frozen.EXECUTION_RECEIPT_PATH: "ot160-noise-xk-radio-execution-receipt.json",
        frozen.RECOVERY_RECEIPT_PATH: "ot160-noise-xk-radio-recovery-receipt.json",
    }
    paths = tuple(expected)
    try:
        if (
            not root.is_absolute()
            or not private_root.is_absolute()
            or private_root.name != ".private"
            or private_root.parent.resolve() != root.resolve()
        ):
            return False
        private_root.mkdir(mode=0o700, parents=False, exist_ok=True)
        return (
            private_root.is_dir()
            and not frozen._has_reparse_or_symlink_ancestry(private_root, root)
            and len({path.resolve() for path in paths}) == len(paths)
            and all(
                path.is_absolute()
                and path.parent.resolve() == private_root.resolve()
                and path.name == name
                for path, name in expected.items()
            )
        )
    except (OSError, RuntimeError):
        return False


frozen._private_paths_valid = _private_paths_valid

_SAFE_STAGES = frozenset(stage.value for stage in runner.StageCode)
_active_journal: dict[str, Any] | None = None
_original_new_journal = frozen._new_journal
_original_journal_valid = frozen._journal_valid
_original_receipt = frozen._receipt
_original_runner_run = runner.run


def _new_journal(binding: Any, grant: Any) -> dict[str, Any]:
    global _active_journal
    journal = _original_new_journal(binding, grant)
    journal["radio_failure_stage"] = None
    _active_journal = journal
    return journal


def _journal_valid(value: object, binding: Any, grant: Any) -> bool:
    if type(value) is not dict or "radio_failure_stage" not in value:
        return False
    stage = value["radio_failure_stage"]
    if stage is not None and (
        type(stage) is not str
        or stage not in _SAFE_STAGES
        or value.get("radio_run_invoked") is not True
        or value.get("radio_result_validated") is not False
    ):
        return False
    predecessor = dict(value)
    predecessor.pop("radio_failure_stage")
    return _original_journal_valid(predecessor, binding, grant)


def _recording_runner_run(*args: Any, **kwargs: Any) -> dict[str, Any]:
    try:
        return _original_runner_run(*args, **kwargs)
    except runner.RunnerError as exc:
        stage = exc.stage if exc.stage in _SAFE_STAGES else None
        if stage is not None and _active_journal is not None:
            _active_journal["radio_failure_stage"] = stage
            frozen._persist(_active_journal)
        raise


def _receipt(
    journal: dict[str, Any],
    result: str,
    restoration_complete: bool,
    radio_result: dict[str, Any] | None,
    failure: Any,
) -> dict[str, Any]:
    receipt = _original_receipt(
        journal, result, restoration_complete, radio_result, failure
    )
    stage = journal.get("radio_failure_stage")
    if (
        stage in _SAFE_STAGES
        and type(receipt.get("failure")) is dict
        and receipt["failure"].get("code") == "radio_run_failed"
    ):
        receipt["failure"]["stage"] = stage
    return receipt


frozen._new_journal = _new_journal
frozen._journal_valid = _journal_valid
frozen._receipt = _receipt
runner.run = _recording_runner_run

# Explicit public aliases keep type identity stable for adapters and future
# authority tools while __getattr__ forwards the remaining tested surface,
# including the frozen byte-hash helper.
ExecutionBinding = frozen.ExecutionBinding
AuthorityGrant = frozen.AuthorityGrant
RunConfig = frozen.RunConfig
Image = frozen.Image
FailureCode = frozen.FailureCode
CoordinatorError = frozen.CoordinatorError
RESTORE_NAME = frozen.RESTORE_NAME
RESTORE_BYTES = frozen.RESTORE_BYTES
RESTORE_SHA256 = frozen.RESTORE_SHA256
APPLICATION_OFFSET = frozen.APPLICATION_OFFSET
FACTORY_SLOT_BYTES = frozen.FACTORY_SLOT_BYTES
BAUD = frozen.BAUD
HASH64 = frozen.HASH64
SAFE_IMAGE_NAME = frozen.SAFE_IMAGE_NAME

execute = frozen.execute
recover = frozen.recover


def __getattr__(name: str) -> Any:
    return getattr(frozen, name)
