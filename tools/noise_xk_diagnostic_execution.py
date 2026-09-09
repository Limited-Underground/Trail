"""Injected diagnostics around the unchanged solicited execution/recovery path.

No discovery, hardware action or grant issuance occurs on import/construction.
The caller must bind the attempt ordinal and complete source snapshot into its
fresh authority; choosing another ordinal never grants another physical attempt.
"""
from __future__ import annotations

import hashlib
import importlib.util
import itertools
from pathlib import Path
import sys
import threading
from types import SimpleNamespace

import noise_xk_solicited_bundle as previous_bundle
import noise_xk_solicited_backend as previous_backend
import noise_xk_solicited_coordinator as previous_coordinator
from noise_xk_retry_diagnostics import RetryDiagnostics

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SCHEMA = "noise-xk-diagnostic-source-closure-v1"
SOURCE_PATHS = previous_bundle.SOURCE_PATHS + (
    "tools/noise_xk_retry_diagnostics.py",
    "tools/noise_xk_diagnostic_execution.py",
)
_PINS = {
    "noise_xk_solicited_coordinator.py": "f0270e0c33967ff8a56f6a6dd50c4ffee1f1c7ddeac871fc512539cdff4cf4a7",
    "noise_xk_solicited_execution.py": "7d8a3440229009b59b07a5e88dffab6ffbf4ebe566f1b52f9225c6de49c38eab",
}
_instances = itertools.count()
_operation_lock = threading.Lock()
verify_images = previous_bundle.verify_images
verify_build_provenance = previous_bundle.verify_build_provenance
freeze_images = previous_bundle.freeze_images
BundleError = previous_bundle.BundleError
previous = previous_bundle.previous


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
    verify_build_provenance=verify_build_provenance, previous=previous, BundleError=BundleError)


def _load(filename, suffix):
    path = ROOT / "tools" / filename
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != _PINS[filename]:
        raise RuntimeError("diagnostic_dependency_mismatch")
    name = __name__ + "_" + suffix
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("diagnostic_dependency_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    exec(compile(raw, str(path), "exec", dont_inherit=True), module.__dict__)
    return module


class _ObservedBackend:
    def __init__(self, backend, endpoints, observer):
        self._backend, self._endpoints, self._observer = backend, endpoints, observer

    def verify_role(self, endpoint, role, descriptor):
        return self._backend.verify_role(endpoint, role, descriptor)

    def write_application(self, endpoint, offset, image):
        return self._backend.write_application(endpoint, offset, image)

    def verify_application(self, endpoint, offset, image):
        return self._backend.verify_application(endpoint, offset, image)

    def hard_reset(self, endpoint):
        return self._backend.hard_reset(endpoint)

    def open_radio_endpoint(self, endpoint):
        matches = [index for index, value in enumerate(self._endpoints) if endpoint is value]
        if len(matches) != 1:
            raise RuntimeError("diagnostic_endpoint_mismatch")
        opened = self._backend.open_radio_endpoint(endpoint)
        return None if opened is None else self._observer.wrap(opened, ("A", "B")[matches[0]])


class DiagnosticExecutionSession:
    def __init__(self, attempt_ordinal, capacity=128):
        if type(attempt_ordinal) is not int or not 1 <= attempt_ordinal <= 9:
            raise ValueError("diagnostic_attempt_invalid")
        self._observer = RetryDiagnostics(capacity)
        suffix = str(next(_instances))
        coordinator = _load("noise_xk_solicited_coordinator.py", suffix + "_coordinator")
        self.coordinator = coordinator
        # Immutable API identities are shared for compatibility with the
        # existing concrete backend. Only private module references change.
        for name in ("RestoreDescriptor", "ExecutionBinding", "AuthorityGrant", "RunConfig",
                     "Image", "FailureCode", "CoordinatorError"):
            value = getattr(previous_coordinator, name)
            setattr(coordinator, name, value)
            setattr(coordinator._coordinator, name, value)
            if hasattr(coordinator.frozen, name):
                setattr(coordinator.frozen, name, value)
        prefix = "noise-xk-diagnostic-" + str(attempt_ordinal) + "-recovery-"
        coordinator.JOURNAL_NAME = prefix + "journal.json"
        coordinator.EXECUTION_NAME = prefix + "execution.json"
        coordinator.RECOVERY_NAME = prefix + "receipt.json"
        coordinator.frozen.JOURNAL_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.JOURNAL_NAME
        coordinator.frozen.EXECUTION_RECEIPT_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.EXECUTION_NAME
        coordinator.frozen.RECOVERY_RECEIPT_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.RECOVERY_NAME
        coordinator.frozen.JOURNAL_SCHEMA = "OTNXDIAGJ0"
        coordinator.frozen.RECEIPT_SCHEMA = "OTNXDIAGCR0"
        self._execution = _load("noise_xk_solicited_execution.py", suffix + "_execution")
        self._execution.coordinator = coordinator
        self.backend_module = SimpleNamespace(**{
            name: value for name, value in vars(previous_backend).items() if not name.startswith("__")})
        self.backend_module.coordinator = coordinator

    def snapshot(self):
        return self._observer.snapshot()

    def _invoke(self, method, root, snapshot, paths, config, backend, authority):
        if not _operation_lock.acquire(blocking=False):
            raise self.coordinator.CoordinatorError(self.coordinator.FailureCode.INVALID_CONFIGURATION)
        try:
            verify_sources(root, snapshot["sources"])
            old_paths = frozenset(previous_bundle.SOURCE_PATHS)
            admitted = {"sources": {"schema": previous_bundle.SOURCE_SCHEMA,
                        "sources": [entry for entry in snapshot["sources"]["sources"] if entry["path"] in old_paths]},
                        "images": snapshot["images"]}
            wrapped = _ObservedBackend(backend, config.private_endpoints, self._observer)
            return method(root, admitted, paths, config, wrapped, authority)
        finally:
            _operation_lock.release()

    def execute(self, root, snapshot, paths, config, backend, authority):
        return self._invoke(self._execution.execute, root, snapshot, paths, config, backend, authority)

    def recover(self, root, snapshot, paths, config, backend, authority):
        return self._invoke(self._execution.recover, root, snapshot, paths, config, backend, authority)
