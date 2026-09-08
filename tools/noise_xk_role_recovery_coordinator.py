#!/usr/bin/env python3
"""Injected, host-only successor with exact per-role application restoration.

No CLI, concrete backend, authority issuance or hardware discovery is provided.
The frozen coordinator owns consumption and failure cleanup. A caller must supply
an exact-binding grant and a backend that verifies each role independently of its
currently installed application (benchmark bytes are identical on both roles).
The runner digest is not a complete executable-bundle source lock: a later
bundle must also bind runtime, buffered endpoint, adapter and coordinator sources.
"""
from __future__ import annotations

import dataclasses
import hashlib
import importlib
import importlib.util
import sys
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
FROZEN_PATH = ROOT / "tools/ot153_noise_xk_radio_coordinator.py"
FROZEN_SHA256 = "6635c73c6952b322ec1d72043a80f637b2cc04b70c1763f578ea4f38559aeaf3"
RUNNER_PATH = ROOT / "tools/noise_xk_ready_runner.py"
FROZEN_RUNNER_PATH = ROOT / "tools/ot153_noise_xk_radio_runner.py"
FROZEN_RUNNER_SHA256 = "8b20512bf25f06247bb59defa092b8db82fde753484a3936d9e4aee2fba808be"


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("coordinator_dependency_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


if (hashlib.sha256(FROZEN_PATH.read_bytes()).hexdigest() != FROZEN_SHA256
        or hashlib.sha256(FROZEN_RUNNER_PATH.read_bytes()).hexdigest() != FROZEN_RUNNER_SHA256):
    raise RuntimeError("frozen_coordinator_mismatch")
frozen = _load(__name__ + "_frozen", FROZEN_PATH)
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
runner = importlib.import_module("noise_xk_ready_runner")
Image = frozen.Image
FailureCode = frozen.FailureCode
CoordinatorError = frozen.CoordinatorError
APPLICATION_OFFSET = frozen.APPLICATION_OFFSET
FACTORY_SLOT_BYTES = frozen.FACTORY_SLOT_BYTES
BAUD = frozen.BAUD


@dataclass(frozen=True)
class RestoreDescriptor:
    name: str
    bytes: int
    sha256: str


@dataclass(frozen=True)
class ExecutionBinding:
    benchmark_name: str
    benchmark_bytes: int
    benchmark_sha256: str
    restore_a: RestoreDescriptor
    restore_b: RestoreDescriptor
    application_offset: int
    baud: int
    runner_name: str
    runner_sha256: str
    runner_schema: str


@dataclass(frozen=True)
class AuthorityGrant:
    raw_sha256: str
    attempt_count: int
    reusable: bool
    radio_allowed: bool
    binding_sha256: str


@dataclass(frozen=True, repr=False)
class RunConfig:
    private_endpoints: tuple[object, object]
    binding: ExecutionBinding
    benchmark_path: Path
    restore_paths: tuple[Path, Path]


def binding_digest(binding: ExecutionBinding) -> str:
    return frozen._sha256(frozen._canonical_bytes(dataclasses.asdict(binding)))


def _descriptor_valid(value: object) -> bool:
    return (type(value) is RestoreDescriptor and type(value.name) is str
            and frozen.SAFE_IMAGE_NAME.fullmatch(value.name) is not None
            and type(value.bytes) is int and 0 < value.bytes <= FACTORY_SLOT_BYTES
            and type(value.sha256) is str and frozen.HASH64.fullmatch(value.sha256) is not None)


def _binding_valid(value: object) -> bool:
    if type(value) is not ExecutionBinding:
        return False
    benchmark = RestoreDescriptor(value.benchmark_name, value.benchmark_bytes, value.benchmark_sha256)
    return (_descriptor_valid(benchmark) and _descriptor_valid(value.restore_a)
            and _descriptor_valid(value.restore_b)
            and value.restore_a.sha256 != value.restore_b.sha256
            and benchmark.sha256 not in (value.restore_a.sha256, value.restore_b.sha256)
            and type(value.application_offset) is int and value.application_offset == APPLICATION_OFFSET
            and type(value.baud) is int and value.baud == BAUD
            and value.runner_name == RUNNER_PATH.name
            and value.runner_schema == runner.SCHEMA
            and value.runner_sha256 == frozen._runner_digest())


def _private_paths_valid_inner() -> bool:
    expected = {
        frozen.JOURNAL_PATH: "noise-xk-role-recovery-journal.json",
        frozen.EXECUTION_RECEIPT_PATH: "noise-xk-role-recovery-execution.json",
        frozen.RECOVERY_RECEIPT_PATH: "noise-xk-role-recovery-receipt.json",
    }
    return (frozen.PRIVATE_ROOT.is_absolute() and frozen.PRIVATE_ROOT.is_dir()
            and frozen.PRIVATE_ROOT.name == ".private"
            and frozen.PRIVATE_ROOT.parent.resolve() == frozen.ROOT.resolve()
            and not frozen._has_reparse_or_symlink_ancestry(frozen.PRIVATE_ROOT, frozen.ROOT)
            and len({p.resolve() for p in expected}) == 3
            and all(p.is_absolute() and p.parent.resolve() == frozen.PRIVATE_ROOT.resolve()
                    and p.name == name for p, name in expected.items()))


def _private_paths_valid() -> bool:
    try:
        return _private_paths_valid_inner()
    except (OSError, RuntimeError, ValueError):
        return False


def _config_valid_inner(config: object, *, recovery: bool) -> bool:
    if type(config) is not RunConfig or not _binding_valid(config.binding) or not _private_paths_valid():
        return False
    endpoints = config.private_endpoints
    paths = config.restore_paths
    receipt = frozen.RECOVERY_RECEIPT_PATH if recovery else frozen.EXECUTION_RECEIPT_PATH
    return (type(endpoints) is tuple and len(endpoints) == 2
            and all(e is not None for e in endpoints) and frozen._endpoints_distinct(endpoints)
            and isinstance(config.benchmark_path, Path) and config.benchmark_path.is_absolute()
            and type(paths) is tuple and len(paths) == 2
            and all(isinstance(p, Path) and p.is_absolute() for p in paths)
            and len({p.resolve() for p in paths}) == 2 and not receipt.exists())


def _config_valid(config: object, *, recovery: bool) -> bool:
    try:
        return _config_valid_inner(config, recovery=recovery)
    except (OSError, RuntimeError, ValueError):
        return False


def _grant(config: RunConfig, authority, recovery: bool):
    ok, grant = frozen._attempt(lambda: authority.validate(config.binding, recovery=recovery))
    if not (ok and type(grant) is AuthorityGrant and type(grant.raw_sha256) is str
            and frozen.HASH64.fullmatch(grant.raw_sha256)
            and type(grant.attempt_count) is int and grant.attempt_count == 1
            and grant.reusable is False and grant.radio_allowed is True
            and grant.binding_sha256 == binding_digest(config.binding)):
        frozen._raise(frozen._Failure(FailureCode.AUTHORITY_REJECTED))
    return grant


def _restores(config: RunConfig):
    images = tuple(frozen._read_exact_image(path, desc.name, desc.bytes, desc.sha256)
                   for path, desc in zip(config.restore_paths, (config.binding.restore_a, config.binding.restore_b)))
    if any(image is None for image in images):
        frozen._raise(frozen._Failure(FailureCode.ARTIFACT_INVALID))
    return images


def _prepare(config, authority, *, recovery):
    if not _config_valid(config, recovery=recovery):
        frozen._raise(frozen._Failure(FailureCode.INVALID_CONFIGURATION))
    grant = _grant(config, authority, recovery)
    restores = _restores(config)
    binding = config.binding
    image = frozen._read_exact_image(config.benchmark_path, binding.benchmark_name,
                                     binding.benchmark_bytes, binding.benchmark_sha256)
    if image is None:
        frozen._raise(frozen._Failure(FailureCode.ARTIFACT_INVALID))
    return binding, grant, image, restores


def _prepare_recovery(config, authority):
    if not _config_valid(config, recovery=True):
        frozen._raise(frozen._Failure(FailureCode.INVALID_CONFIGURATION))
    return config.binding, _grant(config, authority, True), _restores(config)


class _RoleCheckedBackend:
    def __init__(self, config, backend):
        self.config, self.backend = config, backend

    def _check(self, endpoint):
        matches = [i for i, candidate in enumerate(self.config.private_endpoints) if endpoint is candidate]
        if len(matches) != 1:
            raise RuntimeError("role_endpoint_mismatch")
        i = matches[0]
        desc = (self.config.binding.restore_a, self.config.binding.restore_b)[i]
        # A boolean success is mandatory: None/implicit no-op does not admit a role.
        if self.backend.verify_role(endpoint, ("A", "B")[i], desc) is not True:
            raise RuntimeError("role_verification_failed")

    def write_application(self, endpoint, offset, image):
        self._check(endpoint)
        return self.backend.write_application(endpoint, offset, image)

    def verify_application(self, endpoint, offset, image):
        self._check(endpoint)
        return self.backend.verify_application(endpoint, offset, image)

    def hard_reset(self, endpoint):
        self._check(endpoint)
        return self.backend.hard_reset(endpoint)

    def open_radio_endpoint(self, endpoint):
        self._check(endpoint)
        return self.backend.open_radio_endpoint(endpoint)


def _preflight(config, backend, restores):
    # Both exact role/installed-byte checks precede any preflight reset or write.
    checks = [frozen._attempt(lambda e=e, image=image: backend.verify_application(e, APPLICATION_OFFSET, image))[0]
              for e, image in zip(config.private_endpoints, restores)]
    if not all(checks):
        return False
    return all([frozen._attempt(lambda e=e: backend.hard_reset(e))[0] for e in config.private_endpoints])


def _restore_touched(config, backend, restores, journal):
    complete = True
    for role, endpoint, image in zip(("A", "B"), config.private_endpoints, restores):
        state = journal["nodes"][role]
        if not state["benchmark_write_started"] or state["restore_reset_completed"]:
            continue
        state["restore_write_started"] = True
        if not frozen._persist(journal):
            complete = False
        if not frozen._attempt(lambda: backend.write_application(endpoint, APPLICATION_OFFSET, image))[0]:
            complete = False
            continue
        if not frozen._attempt(lambda: backend.verify_application(endpoint, APPLICATION_OFFSET, image))[0]:
            complete = False
            continue
        state["restore_readback_verified"] = True
        if not frozen._persist(journal):
            complete = False
        if not frozen._attempt(lambda: backend.hard_reset(endpoint))[0]:
            complete = False
            continue
        state["restore_reset_completed"] = True
        if not frozen._persist(journal):
            complete = False
    return complete and frozen._restoration_complete(journal)


# Private module mutation only; frozen files and existing imported coordinators remain intact.
frozen.ExecutionBinding = ExecutionBinding
frozen.RunConfig = RunConfig
frozen._RUNNER_PATH = RUNNER_PATH
frozen.RUNNER_NAME = RUNNER_PATH.name
frozen.JOURNAL_SCHEMA = "OTNXROLEJ0"
frozen.RECEIPT_SCHEMA = "OTNXROLECR0"
frozen.JOURNAL_PATH = frozen.PRIVATE_ROOT / "noise-xk-role-recovery-journal.json"
frozen.EXECUTION_RECEIPT_PATH = frozen.PRIVATE_ROOT / "noise-xk-role-recovery-execution.json"
frozen.RECOVERY_RECEIPT_PATH = frozen.PRIVATE_ROOT / "noise-xk-role-recovery-receipt.json"
frozen._binding_valid = _binding_valid
frozen._config_valid = _config_valid
frozen._private_paths_valid = _private_paths_valid
frozen._prepare = _prepare
frozen._prepare_recovery = _prepare_recovery
frozen._preflight = _preflight
frozen._restore_touched = _restore_touched
_safe_stages = frozenset(stage.value for stage in runner.StageCode)
_active_journal = None
_original_new_journal = frozen._new_journal
_original_journal_valid = frozen._journal_valid
_original_receipt = frozen._receipt


def _new_journal(binding, grant):
    global _active_journal
    journal = _original_new_journal(binding, grant)
    journal["radio_failure_stage"] = None
    _active_journal = journal
    return journal


def _journal_valid(value, binding, grant):
    if type(value) is not dict or "radio_failure_stage" not in value:
        return False
    stage = value["radio_failure_stage"]
    if stage is not None and (type(stage) is not str or stage not in _safe_stages
                              or value.get("radio_run_invoked") is not True
                              or value.get("radio_result_validated") is not False):
        return False
    predecessor = dict(value)
    predecessor.pop("radio_failure_stage")
    return _original_journal_valid(predecessor, binding, grant)


def _run(*args, **kwargs):
    try:
        return runner.run(*args, **kwargs)
    except runner.RunnerError as exc:
        if exc.stage in _safe_stages and _active_journal is not None:
            _active_journal["radio_failure_stage"] = exc.stage
            frozen._persist(_active_journal)
        raise


def _receipt(journal, result, restoration_complete, radio_result, failure):
    receipt = _original_receipt(journal, result, restoration_complete, radio_result, failure)
    stage = journal.get("radio_failure_stage")
    if (stage in _safe_stages and type(receipt.get("failure")) is dict
            and receipt["failure"].get("code") == "radio_run_failed"):
        receipt["failure"]["stage"] = stage
    return receipt


frozen._new_journal = _new_journal
frozen._journal_valid = _journal_valid
frozen._receipt = _receipt
frozen.runner = SimpleNamespace(run=_run, validate_public_result=runner.validate_public_result)


def execute(config: RunConfig, backend, authority) -> dict[str, Any]:
    return frozen.execute(config, _RoleCheckedBackend(config, backend), authority)


def recover(config: RunConfig, backend, authority) -> dict[str, Any]:
    return frozen.recover(config, _RoleCheckedBackend(config, backend), authority)
