"""Bounded pre-readiness startup tolerance around the frozen diagnostic path.

Only malformed records bearing a recognized startup kind, and exact healthy
READY records for syntactically valid unissued challenges, may be skipped.
Issued challenges, active records, post-readiness parsing and all budgets retain
the predecessor policy. No hardware or authority is created by this module.
"""
from __future__ import annotations

import hashlib
import importlib.util
import itertools
from pathlib import Path
import sys
from types import SimpleNamespace

import noise_xk_diagnostic_execution as predecessor

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SCHEMA = "noise-xk-startup-source-closure-v1"
SOURCE_PATHS = predecessor.SOURCE_PATHS + ("tools/noise_xk_startup_execution.py",)
previous = predecessor.previous
BundleError = predecessor.BundleError
freeze_images = predecessor.freeze_images
verify_images = predecessor.verify_images
verify_build_provenance = predecessor.verify_build_provenance
_instances = itertools.count()
_PINS = {
    "noise_xk_solicited_endpoint.py": "703c73e66fc2ab4ba422d117c031cc6dc13cd2ecb0e8fb82ebe4561159d56436",
    "noise_xk_solicited_runtime.py": "ce7b86c759124e25f3b5e0b1962880eb96850963d6f25b14b7c1e0feb4e16064",
    "noise_xk_solicited_backend.py": "6775be513856618f9549530fe6c8b715104fe0591efab916ab087f02eee2cf2f",
}
_MALFORMED_BEFORE = '''                if b"OT153 " in framed[1]:
                    self._fail("readiness_receipt_invalid")
                continue'''
_MALFORMED_AFTER = '''                if b"OT153 " in framed[1]:
                    tail = framed[1].split(b"OT153 ", 1)[1]
                    token = re.match(rb"([A-Z_]+)(?: |$)", tail)
                    if token is None or token[1].decode("ascii") not in self._STARTUP_KINDS:
                        self._fail("readiness_receipt_invalid")
                continue'''
_READY_BEFORE = '''                if (type(challenge) is not str or challenge not in issued or receipt.fields != {
                        "schema": "OTNXREADY1", "challenge": challenge, "accepted": "yes",
                        "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no"}):
                    self._fail("readiness_receipt_invalid")
                if challenge == latest:'''
_READY_AFTER = '''                if (type(challenge) is not str or re.fullmatch(r"[0-9a-f]{32}", challenge) is None or receipt.fields != {
                        "schema": "OTNXREADY1", "challenge": challenge, "accepted": "yes",
                        "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no"}):
                    self._fail("readiness_receipt_invalid")
                if challenge not in issued:
                    continue
                if challenge == latest:'''


def transform_endpoint(raw):
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != _PINS["noise_xk_solicited_endpoint.py"]:
        raise RuntimeError("startup_endpoint_source_mismatch")
    source = raw.decode("utf-8").replace("\r\n", "\n")
    for before, after in ((_MALFORMED_BEFORE, _MALFORMED_AFTER), (_READY_BEFORE, _READY_AFTER)):
        if source.count(before) != 1:
            raise RuntimeError("startup_endpoint_anchor_mismatch")
        source = source.replace(before, after, 1)
    return source.encode("utf-8")


def _load(filename, suffix, transform=False):
    path = ROOT / "tools" / filename
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != _PINS[filename]:
        raise RuntimeError("startup_dependency_mismatch")
    name = __name__ + "_" + suffix
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("startup_dependency_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    compiled = transform_endpoint(raw) if transform else raw
    exec(compile(compiled, str(path), "exec", dont_inherit=True), module.__dict__)
    return module


def freeze_sources(root):
    return {"schema": SOURCE_SCHEMA,
            "sources": [previous._descriptor(root, path) for path in SOURCE_PATHS]}


def verify_sources(root, manifest):
    previous._require(type(manifest) is dict and set(manifest) == {"schema", "sources"}
                      and manifest["schema"] == SOURCE_SCHEMA
                      and type(manifest["sources"]) is list, "source_manifest_invalid")
    for entry in manifest["sources"]:
        previous._valid_descriptor(entry)
    paths = [entry["path"] for entry in manifest["sources"]]
    previous._require(len(paths) == len(set(paths)) and set(paths) == set(SOURCE_PATHS),
                      "source_closure_mismatch")
    for entry in manifest["sources"]:
        previous._require(previous._descriptor(root, entry["path"]) == entry,
                          "source_digest_mismatch")


bundle = SimpleNamespace(SOURCE_SCHEMA=SOURCE_SCHEMA, SOURCE_PATHS=SOURCE_PATHS,
    freeze_sources=freeze_sources, verify_sources=verify_sources,
    freeze_images=freeze_images, verify_images=verify_images,
    verify_build_provenance=verify_build_provenance, previous=previous, BundleError=BundleError,
    transform_endpoint=transform_endpoint)


class StartupExecutionSession(predecessor.DiagnosticExecutionSession):
    def __init__(self, attempt_ordinal, capacity=128):
        super().__init__(attempt_ordinal, capacity)
        suffix = str(next(_instances))
        endpoint = _load("noise_xk_solicited_endpoint.py", suffix + "_endpoint", transform=True)
        runtime = _load("noise_xk_solicited_runtime.py", suffix + "_runtime")
        runtime.SolicitedReceiptEndpoint = endpoint.SolicitedReceiptEndpoint
        backend = _load("noise_xk_solicited_backend.py", suffix + "_backend")
        # Reuse the exact fresh-handle implementation with only its local parser
        # class changed. It constructs that parser after open and before any read.
        bound_type = type("BoundStartupEndpoint", (backend._BoundSolicitedEndpoint,), {
            "_fresh_endpoint": runtime.SolicitedReconnectableEndpoint._fresh_endpoint})
        backend._prior._BoundEndpoint = bound_type
        backend.coordinator = self.coordinator
        backend._prior.coordinator = self.coordinator
        self.backend_module = backend
        self.endpoint_type = endpoint.SolicitedReceiptEndpoint

    def _invoke(self, method, root, snapshot, paths, config, backend, authority):
        verify_sources(root, snapshot["sources"])
        old_paths = frozenset(predecessor.SOURCE_PATHS)
        admitted = {"sources": {"schema": predecessor.SOURCE_SCHEMA,
                    "sources": [entry for entry in snapshot["sources"]["sources"] if entry["path"] in old_paths]},
                    "images": snapshot["images"]}
        return super()._invoke(method, root, admitted, paths, config, backend, authority)


# The private bridge can reuse its existing session construction seam.
DiagnosticExecutionSession = StartupExecutionSession
