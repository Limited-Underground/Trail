"""Host-only receipt observations; unchanged parser, deadlines and exceptions.

Counts transport activity during expect, including bytes returned at/after its
deadline. Pending state is captured before the predecessor clears the buffer.
These counts cannot establish what firmware emitted or what occurred on USB.
No raw bytes, receipt fields, caller strings or exception text are retained.
"""
from collections import deque
from copy import deepcopy

from noise_xk_contained_endpoint import ContainedReceiptEndpoint

_LIMIT = 65535
_KINDS = frozenset(("TX_START", "TX_DONE", "RX_START", "RX_DONE", "READY",
                    "PROFILE", "STATUS", "BOOT", "STALE_SELFTEST", "COMMANDS"))
_FAILURES = frozenset(("endpoint_failed", "receipt_request_rejected",
                       "receipt_read_budget", "endpoint_read_failed",
                       "receipt_timeout", "receipt_size_invalid",
                       "receipt_record_budget", "receipt_framing_invalid",
                       "receipt_sequence_invalid"))


def _add(row, key, value=1):
    row[key] = min(_LIMIT, row[key] + value)


class _ObservedReads:
    def __init__(self, serial, row):
        self._serial, self._row = serial, row

    def __getattr__(self, name):
        return getattr(self._serial, name)

    def readline(self, size):
        row = self._row
        _add(row, "read_calls")
        row["last_read_bytes"] = 0
        try:
            raw = self._serial.readline(size)
        except BaseException:
            _add(row, "read_errors")
            raise
        if type(raw) is bytes:
            row["last_read_bytes"] = min(_LIMIT, len(raw))
            _add(row, "read_bytes", len(raw))
            if not raw:
                _add(row, "empty_reads")
        else:
            _add(row, "invalid_reads")
        return raw


class ReceiptObservationEndpoint(ContainedReceiptEndpoint):
    """Add bounded metadata without changing the inherited receipt algorithm."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._receipt_observations = deque(maxlen=128)
        self._observation = None

    def _capture(self, outcome):
        row = self._observation
        if row is None or row["outcome"] != "in_progress":
            return
        row["outcome"] = outcome
        row["pending_bytes"] = min(_LIMIT, len(self._pending))
        row["pending_state"] = ("empty" if not self._pending else
                                "complete_line_buffered" if b"\n" in self._pending else
                                "unterminated_bytes")

    def _fail(self, code):
        self._capture(code if type(code) is str and code in _FAILURES else "other_failure")
        return super()._fail(code)

    def _receipt_from_line(self, raw):
        if self._observation is not None:
            _add(self._observation, "parsed_lines")
        return super()._receipt_from_line(raw)

    def expect(self, kind, timeout_ms):
        row = dict.fromkeys(("read_calls", "empty_reads", "read_bytes", "read_errors",
                             "invalid_reads", "parsed_lines", "pending_bytes", "last_read_bytes"), 0)
        row.update(kind=kind if type(kind) is str and kind in _KINDS else "other",
                   outcome="in_progress", pending_state="empty")
        serial = self._serial
        self._observation = row
        self._serial = _ObservedReads(serial, row)
        try:
            receipt = super().expect(kind, timeout_ms)
            self._capture("accepted")
            return receipt
        finally:
            # Also preserve original BaseException behavior; never retain text.
            self._capture("other_failure")
            self._serial = serial
            self._observation = None
            self._receipt_observations.append(row)

    def diagnostic_snapshot(self):
        snapshot = super().diagnostic_snapshot()
        snapshot["schema"] = "noise-xk-receipt-observation-endpoint-v1"
        snapshot["receipt_observations"] = deepcopy(list(self._receipt_observations))
        return snapshot
