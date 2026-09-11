"""OT-195 restoration observation seam, not an executable trial/operator.

A future source-bound successor must admit fresh originals, this diagnostic image
and its additional NVS writes/read before invoking this seam. OT-188/192 grants
and operators cannot authorize it. Importing this module performs no device I/O.
"""
import hashlib
import os

import security_policy_execution as execution
import security_policy_stage_readback as readback


def _unavailable(role, reason):
    return {"role": role, "available": False, "reason": reason,
            "stage": None, "error": None}


def _save_capture(path, raw):
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "wb", buffering=0) as stream:
        execution.need(stream.write(raw) == len(raw), "diagnostic_save_failed")
        os.fsync(stream.fileno())
    execution.need(path.read_bytes() == raw, "diagnostic_save_failed")


class Observation:
    """Single capture of one role; raw NVS stays in an exclusive private file.

Original absence is a byte-level proof only. Fresh live custody and candidate
identity are obligations of the future executable package, not this object.
"""
    def __init__(self, original_nvs, role):
        execution.need(role in ("A", "B"), "diagnostic_role_invalid")
        readback.assert_fresh(original_nvs)
        self.role = role
        self.original_sha = hashlib.sha256(original_nvs).digest()
        self.consumed = False
        self.result = _unavailable(role, "not_captured")

    def capture(self, root, backend, journal, row):
        """Called only after the durable restore-intent and confirmed close.

Exceptions in observation never replace a restoration result. Closure, journal
and binding failures remain hard gates before any diagnostic ROM operation.
"""
        self.result = _unavailable(self.role, "capture_unavailable")
        execution.need(not self.consumed, "diagnostic_capture_consumed")
        self.consumed = True
        execution.need(row["role"] == self.role and
                       hashlib.sha256(row["nvs"]).digest() == self.original_sha,
                       "diagnostic_original_changed")
        execution.need(journal.healthy and backend.assert_idle() is True,
                       "diagnostic_boundary_unavailable")
        execution.Journal.validate(journal.events, journal.owner)
        events = [e["event"] for e in journal.events if e.get("role") == self.role]
        execution.need(events and events[-1] == "restore_intent" and
                       events.count("restore_intent") == 1 and
                       "candidate_boot_intent" in events and
                       ("serial_open_intent" not in events or "serial_closed" in events),
                       "diagnostic_boundary_unavailable")
        name = f"ot195-stage-{self.role}-{journal.owner['attempt']}.bin"
        path = execution.private_path(root, name)
        # Refuse preexisting capture before ROM access; never overwrite evidence.
        execution.need(not path.exists(), "diagnostic_capture_exists")
        raw = backend.read(self.role, execution.NVS_OFFSET, execution.NVS_SPAN)
        execution.need(type(raw) is bytes and len(raw) == execution.NVS_SPAN,
                       "diagnostic_read_invalid")
        _save_capture(path, raw)
        self.result = _unavailable(self.role, "record_unavailable")
        decoded = readback.decode(raw)
        self.result = {"role": self.role, "available": True, "reason": "observed",
                       "stage": decoded["stage"], "error": decoded["error"]}


def restore_with_observation(root, backend, journal, row, observation):
    """Preserve the actual frozen restoration path, with one prior NVS capture.

No receipt/result is accepted or changed here. After an observation failure the
unchanged restoration still must independently verify all originals and protected
regions before reset. A failed close or journal barrier blocks all ROM access.
"""
    execution.need(type(observation) is Observation, "diagnostic_observer_invalid")
    role = row["role"]
    observation.result = _unavailable(observation.role, "capture_unavailable")
    execution.need(backend.close(role) is True and backend.assert_idle() is True,
                   "serial_close_unconfirmed")
    execution.need(role == observation.role and type(row["nvs"]) is bytes and
                   hashlib.sha256(row["nvs"]).digest() == observation.original_sha,
                   "diagnostic_original_changed")
    execution.need(journal.healthy, "journal_invalid")
    events = [e["event"] for e in journal.events if e.get("role") == role]
    if "serial_open_intent" in events and "serial_closed" not in events:
        journal.add("serial_closed", role)
    journal.add("restore_intent", role)
    try:
        observation.capture(root, backend, journal, row)
    except Exception:
        # Preserve the fixed failure stage already set by the observer. Never
        # print the exception, raw flash, private path, identity or journal.
        pass
    execution.restore(backend, journal, row)
    return dict(observation.result)
