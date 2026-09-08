"""Host-only candidate receipt transport; not wired to any execution authority.

Retains the frozen OT-153 command and receipt contracts. A restart requires a
new endpoint instance after reopening; this class never opens, resets or flashes.
"""
from __future__ import annotations

import re
import time
from typing import Any, Callable

from ot156_noise_xk_radio_runtime import frozen_adapter

_MAX_BYTES = frozen_adapter.MAX_LINE_BYTES
_MAX_READS = frozen_adapter.MAX_RECEIPT_LINES
_OUTER_COLOR = re.compile(rb"(?:\x1b\[[0-9;]{0,16}m)?([^\x1b]*?)(?:\x1b\[[0-9;]{0,16}m)?\r?\n")


class BufferedReceiptEndpoint(frozen_adapter.SerialRadioEndpoint):
    def __init__(self, serial_handle: Any, *, monotonic: Callable[[], float] = time.monotonic):
        super().__init__(serial_handle)
        self._monotonic = monotonic
        self._pending = bytearray()
        self._failed = False

    def _fail(self, code: str) -> None:
        self._pending.clear()
        self._failed = True
        raise frozen_adapter.AdapterError(code) from None

    def write_command(self, command: str) -> None:
        if self._failed:
            self._fail("endpoint_failed")
        if self._closed or not self._command_valid(command):
            self._fail("command_rejected")
        failed = False
        try:
            super().write_command(command)
        except Exception:
            failed = True
        if failed:
            self._fail("endpoint_write_failed")

    def expect(self, kind: str, timeout_ms: int) -> object:
        if self._failed or self._closed:
            self._fail("endpoint_failed")
        if type(kind) is not str or not kind or type(timeout_ms) is not int or not 0 < timeout_ms <= 60_000:
            self._fail("receipt_request_rejected")
        deadline = self._monotonic() + timeout_ms / 1000.0
        reads = 0
        records = 0
        while self._monotonic() < deadline:
            if b"\n" not in self._pending:
                if reads >= _MAX_READS:
                    self._fail("receipt_read_budget")
                reads += 1
                failed = False
                try:
                    raw = self._serial.readline(_MAX_BYTES + 1)
                except Exception:
                    failed = True
                if failed:
                    self._fail("endpoint_read_failed")
                if self._monotonic() >= deadline:
                    self._fail("receipt_timeout")
                if type(raw) is not bytes or len(raw) > _MAX_BYTES:
                    self._fail("receipt_size_invalid")
                self._pending.extend(raw)
                if len(self._pending) > _MAX_BYTES:
                    self._fail("receipt_size_invalid")
                if b"\n" not in self._pending:
                    continue
            records += 1
            if records > _MAX_READS:
                self._fail("receipt_record_budget")
            newline = self._pending.index(10) + 1
            line = bytes(self._pending[:newline])
            del self._pending[:newline]
            # Only outer console SGR wrappers are formatting. Embedded escape bytes
            # are never removed to turn malformed protocol fields into valid ones.
            framed = _OUTER_COLOR.fullmatch(line)
            if framed is None:
                self._fail("receipt_framing_invalid")
            receipt = self._receipt_from_line(framed[1] + b"\n")
            if receipt is None:
                continue
            if receipt.kind == kind:
                return receipt
            if receipt.kind not in self._PASSIVE_KINDS:
                self._fail("receipt_sequence_invalid")
        self._fail("receipt_timeout")

    def close(self) -> None:
        self._pending.clear()
        self._failed = True
        super().close()
