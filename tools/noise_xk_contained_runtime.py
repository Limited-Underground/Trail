"""Injected contained parser lifecycle and isolated existing role backend.

No new coordinator, manifest, discovery or execution authority. Callers must bind
the complete successor sources and firmware before any hardware execution.
"""
from collections import deque
from copy import deepcopy
import itertools

import noise_xk_startup_execution as startup
from noise_xk_contained_endpoint import ContainedReceiptEndpoint

_runtime = startup._load("noise_xk_solicited_runtime.py", "contained_runtime")
_runtime.SolicitedReceiptEndpoint = ContainedReceiptEndpoint
_instances = itertools.count()


class _DiagnosticLifecycle:
    def __init__(self, *args, **kwargs):
        self._archived_checkpoints = deque(maxlen=128)
        self._archived_counts = dict.fromkeys(("non_ascii", "non_protocol", "malformed_protocol", "framing_invalid"), 0)
        super().__init__(*args, **kwargs)

    def _fresh_endpoint(self):
        return _runtime.SolicitedReconnectableEndpoint._fresh_endpoint(self)

    def _archive_detached(self, prior):
        if prior is not None and self._endpoint is not prior:
            snapshot = prior.diagnostic_snapshot()
            self._archived_checkpoints.extend(snapshot["checkpoints"])
            for key, value in snapshot["parser_misses"].items():
                self._archived_counts[key] = min(65535, self._archived_counts[key] + value)

    def reopen(self):
        prior = self._endpoint
        try:
            return super().reopen()
        finally:
            self._archive_detached(prior)

    def close(self):
        prior = self._endpoint
        try:
            return super().close()
        finally:
            self._archive_detached(prior)

    def diagnostic_snapshot(self):
        rows = deque(self._archived_checkpoints, maxlen=128)
        counts = dict(self._archived_counts)
        if self._endpoint is not None:
            current = self._endpoint.diagnostic_snapshot()
            rows.extend(current["checkpoints"])
            for key, value in current["parser_misses"].items():
                counts[key] = min(65535, counts[key] + value)
        return {"schema": "noise-xk-contained-runtime-diagnostics-v1",
                "checkpoints": deepcopy(list(rows)), "parser_misses": counts}


class ContainedReconnectableEndpoint(_DiagnosticLifecycle, _runtime.SolicitedReconnectableEndpoint):
    """Caller-injected serial factory; fresh handles and original bounded retry."""


def make_backend_module():
    """Return an isolated pinned backend with its role and lease guards intact."""
    backend = startup._load("noise_xk_solicited_backend.py", "contained_backend_" + str(next(_instances)))
    class BoundContainedEndpoint(_DiagnosticLifecycle, backend._BoundSolicitedEndpoint):
        def reopen(self):
            # Keep detachment and archival in the same reentrant critical
            # section as the predecessor role/lifecycle guards.
            with self._owner._lock:
                return super().reopen()

        def close(self):
            with self._owner._lock:
                return super().close()

        def diagnostic_snapshot(self):
            with self._owner._lock:
                return super().diagnostic_snapshot()
    backend._prior._BoundEndpoint = BoundContainedEndpoint
    return backend
