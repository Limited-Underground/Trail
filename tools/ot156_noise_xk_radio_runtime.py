#!/usr/bin/env python3
"""Reconnectable serial transport successor for the frozen OT-153 radio run.

This module changes only the serial-handle lifecycle.  The successor runner is
responsible for sending and validating each ``RESTART`` receipt before calling
``reopen()``.  Flashing, readback, reset, receipt parsing, and command validation
remain inherited from the frozen OT-153 hardware adapter.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
import time
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
FROZEN_ADAPTER_PATH = ROOT / "tools" / "ot153_noise_xk_radio_hardware_adapter.py"
FROZEN_COORDINATOR_PATH = ROOT / "tools" / "ot153_noise_xk_radio_coordinator.py"
FROZEN_RUNNER_PATH = ROOT / "tools" / "ot153_noise_xk_radio_runner.py"
FROZEN_ADAPTER_SHA256 = "d84aa9a1c0556f6421141a25336a3e87dab054cc971722b3f8058fe0a254f94b"
FROZEN_COORDINATOR_SHA256 = "6635c73c6952b322ec1d72043a80f637b2cc04b70c1763f578ea4f38559aeaf3"
FROZEN_RUNNER_SHA256 = "8b20512bf25f06247bb59defa092b8db82fde753484a3936d9e4aee2fba808be"
INITIAL_OPEN_TIMEOUT_SECONDS = 10.0
REOPEN_TIMEOUT_SECONDS = 15.0
POST_RESTART_SETTLE_SECONDS = 0.150
OPEN_RETRY_SECONDS = 0.250


def _load_frozen_adapter() -> Any:
    spec = importlib.util.spec_from_file_location(
        "_ot156_frozen_ot153_radio_adapter", FROZEN_ADAPTER_PATH
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("frozen_adapter_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


frozen_adapter = _load_frozen_adapter()
AdapterError = frozen_adapter.AdapterError


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ReconnectableSerialRadioEndpoint:
    """OT-153-compatible endpoint with an explicit, bounded fresh reopen."""

    def __init__(
        self,
        private_port: str,
        serial_handle_factory: Callable[[], Any],
        *,
        monotonic: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if type(private_port) is not str or not private_port:
            raise AdapterError("endpoint_rejected")
        if not callable(serial_handle_factory) or not callable(monotonic) or not callable(sleep):
            raise AdapterError("endpoint_factory_rejected")
        self._private_port = private_port
        self._serial_handle_factory = serial_handle_factory
        self._monotonic = monotonic
        self._sleep = sleep
        self._endpoint: Any | None = None
        self._closed = False
        self._endpoint = self._open_with_retry(
            INITIAL_OPEN_TIMEOUT_SECONDS, "endpoint_open_failed"
        )

    def _fresh_endpoint(self) -> Any:
        serial_handle: Any | None = None
        try:
            serial_handle = self._serial_handle_factory()
            serial_handle.dtr = False
            serial_handle.rts = False
            serial_handle.port = self._private_port
            serial_handle.open()
            return frozen_adapter.SerialRadioEndpoint(serial_handle)
        except BaseException:
            if serial_handle is not None:
                try:
                    serial_handle.close()
                except BaseException:
                    pass
            raise AdapterError("endpoint_open_failed") from None

    def _open_with_retry(self, timeout_seconds: float, failure_code: str) -> Any:
        deadline = self._monotonic() + timeout_seconds
        while True:
            try:
                return self._fresh_endpoint()
            except BaseException:
                now = self._monotonic()
                if now >= deadline:
                    raise AdapterError(failure_code) from None
                delay = min(OPEN_RETRY_SECONDS, deadline - now)
                try:
                    self._sleep(delay)
                except BaseException:
                    raise AdapterError(failure_code) from None

    def write_command(self, command: str) -> None:
        if self._closed or self._endpoint is None:
            raise AdapterError("endpoint_closed")
        self._endpoint.write_command(command)

    def expect(self, kind: str, timeout_ms: int) -> object:
        if self._closed or self._endpoint is None:
            raise AdapterError("endpoint_closed")
        return self._endpoint.expect(kind, timeout_ms)

    def reopen(self) -> None:
        """Discard the old queue and open a fresh handle after restart settles."""
        if self._closed or self._endpoint is None:
            raise AdapterError("endpoint_closed")
        prior = self._endpoint
        self._endpoint = None
        try:
            prior.close()
        except BaseException:
            raise AdapterError("endpoint_reopen_failed") from None
        try:
            self._sleep(POST_RESTART_SETTLE_SECONDS)
        except BaseException:
            raise AdapterError("endpoint_reopen_failed") from None
        self._endpoint = self._open_with_retry(
            REOPEN_TIMEOUT_SECONDS, "endpoint_reopen_failed"
        )

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        endpoint, self._endpoint = self._endpoint, None
        if endpoint is None:
            return
        try:
            endpoint.close()
        except BaseException:
            raise AdapterError("endpoint_close_failed") from None


class ReconnectableEsptoolSerialBackend(frozen_adapter.EsptoolSerialBackend):
    """Frozen OT-153 backend with only its radio endpoint lifecycle replaced."""

    def open_radio_endpoint(self, private_endpoint: object) -> ReconnectableSerialRadioEndpoint:
        private_port = self._endpoint(private_endpoint)

        def serial_handle_factory() -> Any:
            return self._serial.Serial(
                port=None,
                baudrate=frozen_adapter.coordinator.BAUD,
                timeout=frozen_adapter.SERIAL_TIMEOUT_SECONDS,
            )

        return ReconnectableSerialRadioEndpoint(private_port, serial_handle_factory)


def frozen_sources_match() -> bool:
    """Return true only while every reused OT-153 source remains byte-exact."""
    try:
        return (
            _sha256(FROZEN_ADAPTER_PATH) == FROZEN_ADAPTER_SHA256
            and _sha256(FROZEN_COORDINATOR_PATH) == FROZEN_COORDINATOR_SHA256
            and _sha256(FROZEN_RUNNER_PATH) == FROZEN_RUNNER_SHA256
        )
    except OSError:
        return False
