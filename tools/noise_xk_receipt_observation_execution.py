"""Bounded receipt byte observations through existing admission and recovery.

No device discovery, CLI, saved grant or execution authority is provided here.
Fresh callers must bind these successor sources before any physical operation.
"""
from collections import deque
from copy import deepcopy

import noise_xk_contained_execution as contained
import noise_xk_contained_runtime as runtime
from noise_xk_receipt_observation_endpoint import ReceiptObservationEndpoint

SOURCE_SCHEMA = "noise-xk-receipt-observation-source-closure-v1"
SOURCE_PATHS = contained.SOURCE_PATHS + (
    "tools/noise_xk_receipt_observation_endpoint.py",
    "tools/noise_xk_receipt_observation_execution.py",
)
previous, BundleError = contained.previous, contained.BundleError
freeze_images, verify_images = contained.freeze_images, contained.verify_images
verify_build_provenance = contained.verify_build_provenance


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


_runtime = contained.startup._load("noise_xk_solicited_runtime.py", "receipt_observation_runtime")
_runtime.SolicitedReceiptEndpoint = ReceiptObservationEndpoint


class _ObservationLifecycle:
    def __init__(self, *args, **kwargs):
        self._receipt_archive = deque(maxlen=128)
        self._receipt_archive_failures = 0
        super().__init__(*args, **kwargs)

    def _fresh_endpoint(self):
        return _runtime.SolicitedReconnectableEndpoint._fresh_endpoint(self)

    def _archive_detached(self, prior):
        try:
            return super()._archive_detached(prior)
        finally:
            if prior is not None and self._endpoint is not prior:
                try:
                    self._receipt_archive.extend(prior.diagnostic_snapshot()["receipt_observations"])
                except Exception:
                    self._receipt_archive_failures = min(65535, self._receipt_archive_failures + 1)

    def diagnostic_snapshot(self):
        snapshot = super().diagnostic_snapshot()
        rows = deque(self._receipt_archive, maxlen=128)
        if self._endpoint is not None:
            rows.extend(self._endpoint.diagnostic_snapshot()["receipt_observations"])
        return {**snapshot, "schema": "noise-xk-receipt-observation-runtime-v1",
                "receipt_observations": deepcopy(list(rows)),
                "observation_archive_failures": self._receipt_archive_failures}


def make_backend_module():
    backend = runtime.make_backend_module()

    class BoundObservedEndpoint(_ObservationLifecycle, backend._prior._BoundEndpoint):
        def reopen(self):
            with self._owner._lock:
                return super().reopen()

        def close(self):
            with self._owner._lock:
                return super().close()

        def diagnostic_snapshot(self):
            with self._owner._lock:
                return super().diagnostic_snapshot()

    backend._prior._BoundEndpoint = BoundObservedEndpoint
    return backend


class ReceiptObservationSession(contained.ContainedExecutionSession):
    def __init__(self, attempt_ordinal, capacity=128):
        super().__init__(attempt_ordinal, capacity)
        c = self.coordinator
        prefix = "noise-xk-receipt-observation-" + str(attempt_ordinal) + "-recovery-"
        c.JOURNAL_NAME, c.EXECUTION_NAME, c.RECOVERY_NAME = (
            prefix + suffix for suffix in ("journal.json", "execution.json", "receipt.json"))
        c.frozen.JOURNAL_PATH = c.frozen.PRIVATE_ROOT / c.JOURNAL_NAME
        c.frozen.EXECUTION_RECEIPT_PATH = c.frozen.PRIVATE_ROOT / c.EXECUTION_NAME
        c.frozen.RECOVERY_RECEIPT_PATH = c.frozen.PRIVATE_ROOT / c.RECOVERY_NAME
        c.frozen.JOURNAL_SCHEMA, c.frozen.RECEIPT_SCHEMA = "OTNXOBSJ0", "OTNXOBSCR0"
        self.backend_module = make_backend_module()
        self.backend_module.coordinator = c
        self.backend_module._prior.coordinator = c
        self.endpoint_type = ReceiptObservationEndpoint

    def _invoke(self, method, root, snapshot, paths, config, backend, authority):
        verify_sources(root, snapshot["sources"])
        admitted = {"sources": {"schema": contained.SOURCE_SCHEMA,
                    "sources": [entry for entry in snapshot["sources"]["sources"]
                                if entry["path"] in contained.SOURCE_PATHS]},
                    "images": snapshot["images"]}
        return super()._invoke(method, root, admitted, paths, config, backend, authority)


DiagnosticExecutionSession = ReceiptObservationSession
