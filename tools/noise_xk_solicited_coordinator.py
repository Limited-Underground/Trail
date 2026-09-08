"""Isolated solicited-runner binding of the pinned per-role recovery coordinator.

No authority is issued and no hardware is opened here. Configuration changes
apply only to this module's privately loaded coordinator and its private frozen
state machine, never to an existing imported predecessor coordinator.
"""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace

import noise_xk_solicited_runner as runner

ROOT = Path(__file__).resolve().parents[1]
PREDECESSOR_PATH = ROOT / "tools/noise_xk_role_recovery_coordinator.py"
PREDECESSOR_SHA256 = "50067f2a81c6c4924164294f81b57496b435390c47172b12aa9e55297bdd42fa"
RUNNER_PATH = ROOT / "tools/noise_xk_solicited_runner.py"
JOURNAL_NAME = "noise-xk-solicited-recovery-journal.json"
EXECUTION_NAME = "noise-xk-solicited-recovery-execution.json"
RECOVERY_NAME = "noise-xk-solicited-recovery-receipt.json"


def _load_private():
    raw = PREDECESSOR_PATH.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PREDECESSOR_SHA256:
        raise RuntimeError("solicited_coordinator_source_mismatch")
    name = __name__ + "_private"
    spec = importlib.util.spec_from_file_location(name, PREDECESSOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("solicited_coordinator_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    # Execute exactly the verified bytes; do not trust a stale bytecode cache.
    exec(compile(raw, str(PREDECESSOR_PATH), "exec", dont_inherit=True), module.__dict__)
    return module


_coordinator = _load_private()
frozen = _coordinator.frozen
_coordinator.runner = runner
_coordinator.RUNNER_PATH = RUNNER_PATH
_coordinator._safe_stages = frozenset(stage.value for stage in runner.StageCode)
frozen._RUNNER_PATH = RUNNER_PATH
frozen.RUNNER_NAME = RUNNER_PATH.name
frozen.JOURNAL_SCHEMA = "OTNXSOLJ0"
frozen.RECEIPT_SCHEMA = "OTNXSOLCR0"
frozen.JOURNAL_PATH = frozen.PRIVATE_ROOT / JOURNAL_NAME
frozen.EXECUTION_RECEIPT_PATH = frozen.PRIVATE_ROOT / EXECUTION_NAME
frozen.RECOVERY_RECEIPT_PATH = frozen.PRIVATE_ROOT / RECOVERY_NAME
frozen.runner = SimpleNamespace(run=_coordinator._run, validate_public_result=runner.validate_public_result)


def _private_paths_valid_inner():
    expected = {
        frozen.JOURNAL_PATH: JOURNAL_NAME,
        frozen.EXECUTION_RECEIPT_PATH: EXECUTION_NAME,
        frozen.RECOVERY_RECEIPT_PATH: RECOVERY_NAME,
    }
    return (frozen.PRIVATE_ROOT.is_absolute() and frozen.PRIVATE_ROOT.is_dir()
            and frozen.PRIVATE_ROOT.name == ".private"
            and frozen.PRIVATE_ROOT.parent.resolve() == frozen.ROOT.resolve()
            and not frozen._has_reparse_or_symlink_ancestry(frozen.PRIVATE_ROOT, frozen.ROOT)
            and len({path.resolve() for path in expected}) == 3
            and all(path.is_absolute() and path.parent.resolve() == frozen.PRIVATE_ROOT.resolve()
                    and path.name == name for path, name in expected.items()))


_coordinator._private_paths_valid_inner = _private_paths_valid_inner

# Export the exact classes used by the isolated validators. A second dataclass
# declaration would break their intentional type-identity checks.
RestoreDescriptor = _coordinator.RestoreDescriptor
ExecutionBinding = _coordinator.ExecutionBinding
AuthorityGrant = _coordinator.AuthorityGrant
RunConfig = _coordinator.RunConfig
Image = _coordinator.Image
FailureCode = _coordinator.FailureCode
CoordinatorError = _coordinator.CoordinatorError
APPLICATION_OFFSET = _coordinator.APPLICATION_OFFSET
FACTORY_SLOT_BYTES = _coordinator.FACTORY_SLOT_BYTES
BAUD = _coordinator.BAUD
binding_digest = _coordinator.binding_digest
_descriptor_valid = _coordinator._descriptor_valid
_binding_valid = _coordinator._binding_valid
execute = _coordinator.execute
recover = _coordinator.recover
