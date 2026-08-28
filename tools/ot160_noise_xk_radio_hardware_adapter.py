#!/usr/bin/env python3
"""Concrete fail-closed adapter for the OT-160 host-only correction.

The immutable OT-153 application-write/readback/reset implementation and the
accepted OT-156 reconnectable radio transport remain reused through private
module instances.  Both instances are bound to the fresh OT-160 coordinator,
which restores the inherited byte-hash contract without changing any device
operation. No corrected bundle or authority is accepted here: OT-161 must
freeze the exact successor, and OT-162 must separately accept fresh authority
before any hardware execution is permitted.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
FROZEN_ADAPTER_PATH = ROOT / "tools" / "ot153_noise_xk_radio_hardware_adapter.py"
COORDINATOR_PATH = ROOT / "tools" / "ot160_noise_xk_radio_coordinator.py"
RUNTIME_PATH = ROOT / "tools" / "ot156_noise_xk_radio_runtime.py"
FROZEN_ADAPTER_SHA256 = "d84aa9a1c0556f6421141a25336a3e87dab054cc971722b3f8058fe0a254f94b"
COORDINATOR_SHA256 = "444528fd341b3d55f3a5b3224b217620e1b37e3c7960d224aefbe01d9953a02d"
RUNTIME_SHA256 = "bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8"

FUTURE_AUTHORITY_TOOL_PATH = ROOT / "tools" / "ot162_noise_xk_radio_execution_authority.py"
FUTURE_AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
)
FUTURE_AUTHORITY_PATH = ROOT / FUTURE_AUTHORITY_RELATIVE


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
            _source_sha256(FROZEN_ADAPTER_PATH) == FROZEN_ADAPTER_SHA256
            and _source_sha256(COORDINATOR_PATH) == COORDINATOR_SHA256
            and _source_sha256(RUNTIME_PATH) == RUNTIME_SHA256
        )
    except OSError:
        return False


if not sources_match():
    raise RuntimeError("successor_source_mismatch")

coordinator = _load("_ot160_coordinator", COORDINATOR_PATH)
frozen = _load("_ot160_frozen_ot153_adapter", FROZEN_ADAPTER_PATH)
runtime = _load("_ot160_reconnectable_runtime", RUNTIME_PATH)

ADAPTER_PATH = Path(__file__).resolve()
frozen.coordinator = coordinator
frozen.ADAPTER_PATH = ADAPTER_PATH
frozen.COORDINATOR_PATH = COORDINATOR_PATH
frozen.FUTURE_AUTHORITY_TOOL_PATH = FUTURE_AUTHORITY_TOOL_PATH
frozen.FUTURE_AUTHORITY_PATH = FUTURE_AUTHORITY_PATH
frozen.FUTURE_AUTHORITY_RELATIVE = FUTURE_AUTHORITY_RELATIVE
runtime.frozen_adapter.coordinator = coordinator

AdapterError = frozen.AdapterError
ArgumentError = frozen.ArgumentError
SafeArgumentParser = frozen.SafeArgumentParser
ExecutionAuthorityGate = frozen.ExecutionAuthorityGate
SerialRadioEndpoint = frozen.SerialRadioEndpoint
ReconnectableSerialRadioEndpoint = runtime.ReconnectableSerialRadioEndpoint
ReconnectableEsptoolSerialBackend = runtime.ReconnectableEsptoolSerialBackend
preflight_only = frozen.preflight_only


def _load_authority_contract() -> Any:
    return frozen._load_authority_contract()


def main(
    argv: list[str] | None = None,
    *,
    backend_factory: Callable[[tuple[str, str]], object] = ReconnectableEsptoolSerialBackend,
    authority_loader: Callable[[], Any] = _load_authority_contract,
) -> int:
    return frozen.main(
        argv,
        backend_factory=backend_factory,
        authority_loader=authority_loader,
    )


def __getattr__(name: str) -> Any:
    return getattr(frozen, name)


if __name__ == "__main__":
    raise SystemExit(main())
