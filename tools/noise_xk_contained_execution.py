"""Contained runtime through inherited admission and independent role recovery.

Disk source checks require a fresh, source-verified caller process. They do not
authenticate imported code objects or grant physical execution permission.
"""
from types import SimpleNamespace
import threading

import noise_xk_startup_execution as startup
import noise_xk_contained_bundle as bundle
import noise_xk_contained_runtime as runtime
from noise_xk_retry_diagnostics import RetryDiagnostics

SOURCE_SCHEMA, SOURCE_PATHS = bundle.SOURCE_SCHEMA, bundle.SOURCE_PATHS
freeze_sources, verify_sources = bundle.freeze_sources, bundle.verify_sources
freeze_images, verify_images = bundle.freeze_images, bundle.verify_images
verify_build_provenance = bundle.verify_build_provenance
previous, BundleError = bundle.previous, bundle.BundleError


class _RetainedDiagnostics(RetryDiagnostics):
    def __init__(self, capacity):
        super().__init__(capacity)
        self._endpoints = {}
        self._endpoint_lock = threading.Lock()

    def wrap(self, endpoint, role):
        wrapped = super().wrap(endpoint, role)
        with self._endpoint_lock:
            self._endpoints[role] = endpoint
        return wrapped

    def diagnostic_snapshot(self):
        with self._endpoint_lock:
            endpoints = dict(self._endpoints)
        roles = {}
        for role, endpoint in endpoints.items():
            try:
                roles[role] = endpoint.diagnostic_snapshot()
            except Exception:
                # Evidence failure never changes execution or recovery outcome,
                # and no raw private exception enters retained diagnostics.
                roles[role] = {"error": "diagnostic_snapshot_unavailable"}
        return {"schema": "noise-xk-contained-session-diagnostics-v1",
                "transport": self.snapshot(), "roles": roles}


class ContainedExecutionSession(startup.StartupExecutionSession):
    def __init__(self, attempt_ordinal, capacity=128):
        super().__init__(attempt_ordinal, capacity)
        self._observer = _RetainedDiagnostics(capacity)
        coordinator = self.coordinator
        prefix = "noise-xk-contained-" + str(attempt_ordinal) + "-recovery-"
        coordinator.JOURNAL_NAME = prefix + "journal.json"
        coordinator.EXECUTION_NAME = prefix + "execution.json"
        coordinator.RECOVERY_NAME = prefix + "receipt.json"
        coordinator.frozen.JOURNAL_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.JOURNAL_NAME
        coordinator.frozen.EXECUTION_RECEIPT_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.EXECUTION_NAME
        coordinator.frozen.RECOVERY_RECEIPT_PATH = coordinator.frozen.PRIVATE_ROOT / coordinator.RECOVERY_NAME
        coordinator.frozen.JOURNAL_SCHEMA = "OTNXCONTJ0"
        coordinator.frozen.RECEIPT_SCHEMA = "OTNXCONTCR0"
        # Preserve the inherited 27 -> 26 -> 24 source filters and generic image
        # and path validation. Only this isolated execution module's build gate
        # changes; the predecessor bundle and other sessions are untouched.
        adapter = SimpleNamespace(**{name: value for name, value in
            vars(self._execution.bundle).items() if not name.startswith("__")})
        adapter.verify_build_provenance = verify_build_provenance
        self._execution.bundle = adapter
        self.backend_module = runtime.make_backend_module()
        self.backend_module.coordinator = coordinator
        self.backend_module._prior.coordinator = coordinator
        self.endpoint_type = runtime.ContainedReceiptEndpoint

    def diagnostic_snapshot(self):
        return self._observer.diagnostic_snapshot()

    def _invoke(self, method, root, snapshot, paths, config, backend, authority):
        verify_sources(root, snapshot["sources"])
        old_paths = frozenset(startup.SOURCE_PATHS)
        admitted = {"sources": {"schema": startup.SOURCE_SCHEMA,
                    "sources": [entry for entry in snapshot["sources"]["sources"]
                                if entry["path"] in old_paths]}, "images": snapshot["images"]}
        return super()._invoke(method, root, admitted, paths, config, backend, authority)


DiagnosticExecutionSession = ContainedExecutionSession
