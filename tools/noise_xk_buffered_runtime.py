"""Injected host-only serial composition; no executable authority or CLI.

The caller supplies unopened handles. No ports are discovered and no device is
opened on import. The fresh role/authority backend remains a separate gate.
"""
from __future__ import annotations

from typing import Any

import noise_xk_ready_runner as runner
from noise_xk_buffered_receipt_endpoint import BufferedReceiptEndpoint, frozen_adapter
import ot156_noise_xk_radio_runtime as lifecycle


if not lifecycle.frozen_sources_match():
    raise RuntimeError("buffered_runtime_source_mismatch")


class RunnerReceiptEndpoint(BufferedReceiptEndpoint):
    @staticmethod
    def _receipt_from_line(raw: object) -> object | None:
        if type(raw) is not bytes or not raw or len(raw) > frozen_adapter.MAX_LINE_BYTES:
            return None
        try:
            line = raw.decode("ascii", errors="strict")
        except UnicodeError:
            return None
        marker = line.find("OT153 ")
        return None if marker < 0 else runner.parse_receipt(line[marker:])


class BufferedReconnectableEndpoint(lifecycle.ReconnectableSerialRadioEndpoint):
    """Fresh handles and buffers, with receipts from the actual runner parser."""

    def _fresh_endpoint(self) -> Any:
        handle = None
        reused = False
        try:
            handle = self._serial_handle_factory()
            issued = getattr(self, "_issued_handles", [])
            reused = any(handle is prior for prior in issued)
            if handle is None or reused:
                raise ValueError("fresh_handle_required")
            # Retain identities, including failed opens: A -> B -> A is not a
            # fresh transport even though A differs from the previous handle.
            issued.append(handle)
            self._issued_handles = issued
            handle.dtr = False
            handle.rts = False
            handle.port = self._private_port
            handle.open()
            return RunnerReceiptEndpoint(handle, monotonic=self._monotonic)
        except BaseException:
            if handle is not None and not reused:
                try:
                    handle.close()
                except BaseException:
                    pass
        raise lifecycle.AdapterError("endpoint_open_failed")
