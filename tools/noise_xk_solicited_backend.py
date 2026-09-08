"""Isolated role backend successor selecting solicited readiness, without a CLI.

All write/readback, role, recovery and lease guards remain in the byte-pinned
predecessor. Only this private module instance receives the new coordinator,
receipt lifecycle and the already exercised esptool identity-output adapter.
"""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import re
import sys

import noise_xk_solicited_coordinator as coordinator
from noise_xk_solicited_runtime import SolicitedReconnectableEndpoint

_PATH = Path(__file__).with_name("noise_xk_bound_backend.py")
_SHA256 = "a7593e6f76763f1b7f83750f8f3f84ca4c29ed0620df5a95b84124d967f3f78a"


def _load():
    try:
        raw = _PATH.read_bytes()
        if hashlib.sha256(raw).hexdigest() != _SHA256:
            raise ValueError()
        spec = importlib.util.spec_from_file_location(__name__ + "_prior", _PATH)
        if spec is None or spec.loader is None:
            raise ValueError()
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        # Execute the exact verified bytes, never a potentially stale .pyc or
        # a second filesystem read after validating the predecessor digest.
        exec(compile(raw, str(_PATH), "exec"), module.__dict__)
        return module
    except BaseException:
        raise RuntimeError("solicited_backend_source_mismatch") from None


_prior = _load()
BackendError = _prior.BackendError
normalize_identity = _prior.normalize_identity
RoleBinding = _prior.RoleBinding
_single_rom_parser = _prior.parse_rom_identity


def canonical_rom_output(output):
    """Fold exactly one/two strict agreeing identity lines; retain no raw output."""
    if type(output) is not str:
        raise BackendError("rom_identity_invalid")
    lines = [line for line in output.splitlines() if re.match(r"^[ \t]*MAC\b", line, re.I)]
    if len(lines) not in (1, 2):
        raise BackendError("rom_identity_line_count")
    identities = []
    for line in lines:
        match = re.fullmatch(r"[ \t]*MAC:[ \t]*(.*?)[ \t]*", line, re.I)
        if match is None:
            raise BackendError("rom_identity_line_invalid")
        identities.append(normalize_identity(match[1]))
    if len(set(identities)) != 1:
        raise BackendError("rom_identity_conflict")
    return "MAC: " + identities[0] + "\n"


def parse_rom_identity(output):
    return _single_rom_parser(canonical_rom_output(output))


class _BoundSolicitedEndpoint(_prior._BoundEndpoint, SolicitedReconnectableEndpoint):
    def query_ready(self, **kwargs):
        with self._owner._lock:
            self._owner._require(self._binding)
            return super().query_ready(**kwargs)


# Configuration affects this isolated predecessor instance only. In particular
# the ordinary imported backend retains its old descriptor and endpoint types.
_prior.coordinator = coordinator
_prior.parse_rom_identity = parse_rom_identity
_prior._BoundEndpoint = _BoundSolicitedEndpoint


class BoundBackend(_prior.BoundBackend):
    """Existing guarded backend with exact new descriptor and runtime identities."""
