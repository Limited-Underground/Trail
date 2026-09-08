"""Bounded, injected endpoint diagnostics without identities or raw exceptions.

This module neither opens hardware nor alters runner validation. A successful
expect event means transport returned a receipt, not that the runner accepted it.
The final operation/receipt bounds a failure location; it is not a root cause.
"""
from __future__ import annotations

from collections import deque
from copy import deepcopy
import re
import threading

_KINDS = frozenset(("READY", "STALE_SELFTEST", "BOOT", "PROFILE", "STATUS", "COMMANDS",
    "PREPARED", "RX_START", "TX_ARM", "TX_START", "TX_DONE", "RX", "STAGE_ACCEPT",
    "WITHHELD", "TIMEOUT", "ABORT", "END", "RESTART", "REJECT"))
_COMMANDS = frozenset(("ready", "prepare", "arm-tx", "send", "abort", "end", "profile", "status", "restart"))
_FLAGS = frozenset(("accepted", "stale_selftest", "radio_ready", "idle", "tx", "wiped",
    "received", "transmitted", "forced", "permit_consumed", "active", "permit", "complete"))
_NUMBERS = frozenset(("wire", "stage", "uses", "expires_ms", "start_us", "done_us",
    "measured_us", "mono_us", "deadline_ms", "deadline_us", "timeout_us", "tx_attempted",
    "tx_sent", "tx_failed", "rx_accepted", "rx_rejected", "lost", "duplicates", "corrupt",
    "unexpected", "forced_timeouts", "radio_frames"))
_SIGNED = frozenset(("result", "rx_restart", "last_radio"))
_ENUMS = {
    "role": frozenset(("I", "R", "none")),
    "scenario": frozenset(("baseline", "retry-m2-withheld", "retry-restart", "none")),
    "message": frozenset(("m1", "m2", "m3", "none")),
    "next": frozenset(("m1", "m2", "m3", "end")),
}
_ERRORS = frozenset(("endpoint_closed", "endpoint_failed", "endpoint_open_failed",
    "endpoint_reopen_failed", "endpoint_close_failed", "endpoint_read_failed", "endpoint_write_failed",
    "command_rejected", "receipt_timeout", "receipt_size_invalid", "receipt_framing_invalid",
    "receipt_sequence_invalid", "receipt_read_budget", "receipt_record_budget",
    "readiness_timeout", "readiness_receipt_invalid", "readiness_sequence_invalid",
    "readiness_endpoint_invalid", "readiness_challenge_invalid", "readiness_read_budget",
    "readiness_record_budget", "passive_identity_mismatch", "radio_close_unconfirmed"))


def _kind(value):
    return value if type(value) is str and value in _KINDS else "OTHER"


def _fields(receipt):
    fields = getattr(receipt, "fields", None)
    if type(fields) is not dict:
        return {}
    safe = {}
    for key in _FLAGS | _NUMBERS | _SIGNED | _ENUMS.keys():
        value = fields.get(key)
        if type(value) is not str:
            continue
        if key in _FLAGS and value in ("yes", "no"):
            safe[key] = value
        elif key in _ENUMS and value in _ENUMS[key]:
            safe[key] = value
        elif key in _NUMBERS and re.fullmatch(r"[0-9]{1,20}", value) and int(value) <= 2**64 - 1:
            safe[key] = int(value)
        elif key in _SIGNED and re.fullmatch(r"-?[0-9]{1,6}", value) and -32768 <= int(value) <= 32767:
            safe[key] = int(value)
    return safe


class RetryDiagnostics:
    def __init__(self, capacity=128):
        if type(capacity) is not int or not 8 <= capacity <= 512:
            raise ValueError("diagnostic_capacity_invalid")
        self._records = deque(maxlen=capacity)
        self._lock = threading.Lock()

    def wrap(self, endpoint, role):
        if type(role) is not str or role not in ("A", "B"):
            raise ValueError("diagnostic_role_invalid")
        return DiagnosticEndpoint(endpoint, role, self)

    def snapshot(self):
        with self._lock:
            return deepcopy(list(self._records))

    def _emit(self, role, operation, phase, kind, *, receipt=None, command=None, error=None):
        # Instrumentation must not mask a result or change an exception. Only
        # allowlisted scalars enter the ring; endpoint/exception objects do not.
        try:
            row = {"role": role, "operation": operation, "phase": phase, "kind": kind}
            if command is not None:
                tokens = command.split() if type(command) is str else []
                row["command"] = tokens[0] if tokens and tokens[0] in _COMMANDS else "other"
                if len(tokens) == 5 and tokens[0] == "prepare" and tokens[4] in _ENUMS["scenario"]:
                    row["scenario"] = tokens[4]
                if len(tokens) == 4 and tokens[0] in ("arm-tx", "send") and tokens[3] in _ENUMS["message"]:
                    row["message"] = tokens[3]
            if receipt is not None:
                row["receipt_kind"] = _kind(getattr(receipt, "kind", None))
                row["fields"] = _fields(receipt)
            if error is not None:
                args = getattr(error, "args", ())
                row["error"] = args[0] if type(args) is tuple and len(args) == 1 and type(args[0]) is str and args[0] in _ERRORS else "operation_failed"
            with self._lock:
                self._records.append(row)
        except BaseException:
            pass


class DiagnosticEndpoint:
    def __init__(self, endpoint, role, observer):
        if type(role) is not str or role not in ("A", "B"):
            raise ValueError("diagnostic_role_invalid")
        self._endpoint, self._role, self._observer = endpoint, role, observer

    def _invoke(self, operation, kind, method, *args, command=None, **kwargs):
        self._observer._emit(self._role, operation, "started", kind, command=command)
        try:
            result = method(*args, **kwargs)
        except BaseException as error:
            self._observer._emit(self._role, operation, "failed", kind, command=command, error=error)
            raise
        receipt = result if operation == "expect" else (
            result[1] if operation == "query_ready" and type(result) is tuple and len(result) == 2 else None)
        self._observer._emit(self._role, operation, "succeeded", kind, receipt=receipt, command=command)
        return result

    def write_command(self, command):
        return self._invoke("write_command", "COMMAND", self._endpoint.write_command, command, command=command)

    def expect(self, kind, timeout_ms):
        return self._invoke("expect", _kind(kind), self._endpoint.expect, kind, timeout_ms)

    def query_ready(self, **kwargs):
        return self._invoke("query_ready", "READY", self._endpoint.query_ready, **kwargs)

    def reopen(self):
        return self._invoke("reopen", "LIFECYCLE", self._endpoint.reopen)

    def close(self):
        return self._invoke("close", "LIFECYCLE", self._endpoint.close)
