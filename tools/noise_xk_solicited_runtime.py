"""Fresh-handle lifecycle for solicited benchmark readiness; no CLI or discovery."""
from __future__ import annotations

from typing import Any

from noise_xk_buffered_runtime import BufferedReconnectableEndpoint, lifecycle
from noise_xk_solicited_endpoint import SolicitedReceiptEndpoint


class SolicitedReconnectableEndpoint(BufferedReconnectableEndpoint):
    """Each opened handle owns fresh framing and exactly one readiness exchange."""

    def _fresh_endpoint(self) -> Any:
        handle = None
        reused = False
        try:
            handle = self._serial_handle_factory()
            issued = getattr(self, "_issued_handles", [])
            reused = any(handle is prior for prior in issued)
            if handle is None or reused:
                raise ValueError("fresh_handle_required")
            issued.append(handle)
            self._issued_handles = issued
            handle.dtr = False
            handle.rts = False
            handle.port = self._private_port
            handle.open()
            return SolicitedReceiptEndpoint(handle, monotonic=self._monotonic)
        except BaseException:
            if handle is not None and not reused:
                try:
                    handle.close()
                except BaseException:
                    pass
        raise lifecycle.AdapterError("endpoint_open_failed")

    def query_ready(self, **kwargs):
        if self._closed or self._endpoint is None:
            raise lifecycle.AdapterError("endpoint_closed")
        return self._endpoint.query_ready(**kwargs)
