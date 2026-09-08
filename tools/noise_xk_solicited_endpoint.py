"""Bounded solicited readiness on the existing buffered receipt transport.

The firmware CLI must process commands and emit each READY/PROFILE/STATUS
transaction serially in FIFO order. The final accepted challenge was the last
write, so older replies are drained before it, never by a guessed quiet delay.
No device is opened by this module.
"""
from __future__ import annotations

import re
import secrets

from noise_xk_buffered_runtime import RunnerReceiptEndpoint
from noise_xk_buffered_receipt_endpoint import _MAX_BYTES, _MAX_READS, _OUTER_COLOR


def _challenge():
    return secrets.token_hex(16)


class SolicitedReceiptEndpoint(RunnerReceiptEndpoint):
    _STARTUP_KINDS = frozenset(("BOOT", "PROFILE", "STATUS", "STALE_SELFTEST", "COMMANDS", "RX_START"))

    @staticmethod
    def _command_valid(command):
        return ((type(command) is str and re.fullmatch(r"ready [0-9a-f]{32}", command) is not None)
                or RunnerReceiptEndpoint._command_valid(command))

    def query_ready(self, *, challenge_factory=_challenge, timeout_ms=10000):
        if (self._failed or self._closed or getattr(self, "_readiness_queried", False)):
            self._fail("readiness_endpoint_invalid")
        if (not callable(challenge_factory) or type(timeout_ms) is not int
                or not 0 < timeout_ms <= 10000):
            self._fail("readiness_request_invalid")
        self._readiness_queried = True
        deadline = self._monotonic() + timeout_ms / 1000.0
        next_query = self._monotonic()
        issued = set()
        latest = None
        previous_transaction = None
        reads = records = 0
        while self._monotonic() < deadline:
            # Process bytes already received before considering another write.
            # A read timeout/partial line is not a failed endpoint; only the
            # absolute deadline or bounded budgets terminate readiness.
            if b"\n" not in self._pending:
                if self._monotonic() >= next_query and len(issued) < 20:
                    failed = False
                    try:
                        challenge = challenge_factory()
                    except BaseException:
                        failed = True
                    if failed:
                        self._fail("readiness_challenge_invalid")
                    if (type(challenge) is not str or re.fullmatch(r"[0-9a-f]{32}", challenge) is None
                            or challenge in issued):
                        self._fail("readiness_challenge_invalid")
                    self.write_command("ready " + challenge)
                    issued.add(challenge)
                    latest = challenge
                    next_query = self._monotonic() + 0.5
                if reads >= _MAX_READS:
                    self._fail("readiness_read_budget")
                reads += 1
                failed = False
                try:
                    raw = self._serial.readline(_MAX_BYTES + 1)
                except BaseException:
                    failed = True
                if failed:
                    self._fail("endpoint_read_failed")
                if self._monotonic() >= deadline:
                    self._fail("readiness_timeout")
                if type(raw) is not bytes or len(raw) > _MAX_BYTES:
                    self._fail("receipt_size_invalid")
                self._pending.extend(raw)
                if len(self._pending) > _MAX_BYTES:
                    self._fail("receipt_size_invalid")
                if b"\n" not in self._pending:
                    continue
            records += 1
            if records > _MAX_READS:
                self._fail("readiness_record_budget")
            end = self._pending.index(10) + 1
            line = bytes(self._pending[:end])
            del self._pending[:end]
            framed = _OUTER_COLOR.fullmatch(line)
            if framed is None:
                self._fail("receipt_framing_invalid")
            receipt = self._receipt_from_line(framed[1] + b"\n")
            if receipt is None:
                if b"OT153 " in framed[1]:
                    self._fail("readiness_receipt_invalid")
                continue
            if previous_transaction is not None:
                if receipt.kind != previous_transaction:
                    self._fail("readiness_sequence_invalid")
                previous_transaction = "STATUS" if previous_transaction == "PROFILE" else None
                continue
            if receipt.kind == "READY":
                challenge = receipt.fields.get("challenge")
                if (type(challenge) is not str or challenge not in issued or receipt.fields != {
                        "schema": "OTNXREADY1", "challenge": challenge, "accepted": "yes",
                        "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no"}):
                    self._fail("readiness_receipt_invalid")
                if challenge == latest:
                    return challenge, receipt
                previous_transaction = "PROFILE"
                continue
            if receipt.kind not in self._STARTUP_KINDS:
                self._fail("readiness_sequence_invalid")
        self._fail("readiness_timeout")
