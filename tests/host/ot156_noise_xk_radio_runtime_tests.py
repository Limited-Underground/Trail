#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-156 reconnectable serial runtime."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot156_noise_xk_radio_runtime.py"
SPEC = importlib.util.spec_from_file_location("ot156_noise_xk_radio_runtime_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-156 runtime unavailable")
runtime = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runtime
SPEC.loader.exec_module(runtime)


PRIVATE_PORT = "PRIVATE-ENDPOINT"
RESTART = b"OT153 RESTART accepted=yes wiped=yes tx=no\n"
STALE = b"OT153 STALE_SELFTEST passed=yes stale_rejected=yes radio_frames=0\n"


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0
        self.sleeps: list[float] = []

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds


class FakeSerialHandle:
    def __init__(
        self,
        events: list[tuple[Any, ...]],
        name: str,
        *,
        lines: list[bytes] | None = None,
        open_error: BaseException | None = None,
    ) -> None:
        object.__setattr__(self, "events", events)
        object.__setattr__(self, "name", name)
        object.__setattr__(self, "lines", list(lines or []))
        object.__setattr__(self, "open_error", open_error)
        object.__setattr__(self, "writes", [])
        object.__setattr__(self, "close_count", 0)

    def __setattr__(self, key: str, value: Any) -> None:
        if key in {"dtr", "rts", "port"}:
            self.events.append((self.name, "set", key, value))
        object.__setattr__(self, key, value)

    def open(self) -> None:
        self.events.append((self.name, "open"))
        if self.open_error is not None:
            raise self.open_error

    def close(self) -> None:
        self.close_count += 1
        self.events.append((self.name, "close"))

    def write(self, value: bytes) -> None:
        self.writes.append(value)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return self.lines.pop(0) if self.lines else b""


class HandleFactory:
    def __init__(self, handles: list[FakeSerialHandle]) -> None:
        self.handles = list(handles)
        self.calls = 0

    def __call__(self) -> FakeSerialHandle:
        self.calls += 1
        if not self.handles:
            raise RuntimeError("PRIVATE backend text")
        return self.handles.pop(0)


class Tests(unittest.TestCase):
    def _endpoint(
        self, factory: HandleFactory, clock: FakeClock
    ) -> runtime.ReconnectableSerialRadioEndpoint:
        return runtime.ReconnectableSerialRadioEndpoint(
            PRIVATE_PORT,
            factory,
            monotonic=clock.monotonic,
            sleep=clock.sleep,
        )

    def test_01_transient_initial_open_failure_retries_then_succeeds(self) -> None:
        events: list[tuple[Any, ...]] = []
        failed = FakeSerialHandle(events, "failed", open_error=OSError("PRIVATE failure"))
        opened = FakeSerialHandle(events, "opened")
        clock = FakeClock()
        endpoint = self._endpoint(HandleFactory([failed, opened]), clock)
        self.assertEqual(clock.sleeps, [runtime.OPEN_RETRY_SECONDS])
        self.assertEqual(failed.close_count, 1)
        endpoint.write_command("profile")
        self.assertEqual(opened.writes, [b"profile\n"])
        endpoint.close()

    def test_02_initial_open_exhaustion_is_bounded_and_private_text_is_hidden(self) -> None:
        events: list[tuple[Any, ...]] = []
        clock = FakeClock()

        def always_fail() -> FakeSerialHandle:
            return FakeSerialHandle(
                events,
                f"failure-{len(events)}",
                open_error=OSError(f"{PRIVATE_PORT} backend failure"),
            )

        with self.assertRaises(runtime.AdapterError) as captured:
            runtime.ReconnectableSerialRadioEndpoint(
                PRIVATE_PORT,
                always_fail,
                monotonic=clock.monotonic,
                sleep=clock.sleep,
            )
        self.assertEqual(str(captured.exception), "endpoint_open_failed")
        self.assertEqual(clock.now, runtime.INITIAL_OPEN_TIMEOUT_SECONDS)
        self.assertTrue(clock.sleeps)
        self.assertTrue(all(delay <= runtime.OPEN_RETRY_SECONDS for delay in clock.sleeps))

    def test_03_reopen_discards_old_handle_and_its_stale_receipts(self) -> None:
        events: list[tuple[Any, ...]] = []
        old = FakeSerialHandle(events, "old", lines=[RESTART, b"OT153 PROFILE configured=no\n"])
        fresh = FakeSerialHandle(events, "fresh", lines=[STALE])
        factory = HandleFactory([old, fresh])
        clock = FakeClock()
        endpoint = self._endpoint(factory, clock)
        endpoint.write_command("restart")
        self.assertEqual(endpoint.expect("RESTART", 5_000).kind, "RESTART")
        endpoint.reopen()
        receipt = endpoint.expect("STALE_SELFTEST", 5_000)
        self.assertEqual(receipt.fields["passed"], "yes")
        self.assertEqual(old.close_count, 1)
        self.assertEqual(old.lines, [b"OT153 PROFILE configured=no\n"])
        self.assertIn(runtime.POST_RESTART_SETTLE_SECONDS, clock.sleeps)
        endpoint.close()

    def test_04_dtr_rts_and_port_are_set_before_every_open(self) -> None:
        events: list[tuple[Any, ...]] = []
        first = FakeSerialHandle(events, "first")
        second = FakeSerialHandle(events, "second")
        endpoint = self._endpoint(HandleFactory([first, second]), FakeClock())
        endpoint.reopen()
        for name in ("first", "second"):
            relevant = [event for event in events if event[0] == name]
            relevant = relevant[: relevant.index((name, "open")) + 1]
            self.assertEqual(
                relevant,
                [
                    (name, "set", "dtr", False),
                    (name, "set", "rts", False),
                    (name, "set", "port", PRIVATE_PORT),
                    (name, "open"),
                ],
            )
        endpoint.close()

    def test_05_reopen_exhaustion_is_15_seconds_and_leaves_no_live_handle(self) -> None:
        events: list[tuple[Any, ...]] = []
        first = FakeSerialHandle(events, "first")
        clock = FakeClock()
        calls = 0

        def factory() -> FakeSerialHandle:
            nonlocal calls
            calls += 1
            if calls == 1:
                return first
            return FakeSerialHandle(
                events, f"retry-{calls}", open_error=OSError("PRIVATE reconnect failure")
            )

        endpoint = runtime.ReconnectableSerialRadioEndpoint(
            PRIVATE_PORT, factory, monotonic=clock.monotonic, sleep=clock.sleep
        )
        with self.assertRaises(runtime.AdapterError) as captured:
            endpoint.reopen()
        self.assertEqual(str(captured.exception), "endpoint_reopen_failed")
        self.assertEqual(
            clock.now,
            runtime.POST_RESTART_SETTLE_SECONDS + runtime.REOPEN_TIMEOUT_SECONDS,
        )
        self.assertEqual(first.close_count, 1)
        endpoint.close()

    def test_06_close_is_idempotent_and_closed_endpoint_cannot_reopen(self) -> None:
        events: list[tuple[Any, ...]] = []
        handle = FakeSerialHandle(events, "only")
        endpoint = self._endpoint(HandleFactory([handle]), FakeClock())
        endpoint.close()
        endpoint.close()
        self.assertEqual(handle.close_count, 1)
        for action in (
            lambda: endpoint.write_command("profile"),
            lambda: endpoint.expect("PROFILE", 5_000),
            endpoint.reopen,
        ):
            with self.assertRaises(runtime.AdapterError) as captured:
                action()
            self.assertEqual(str(captured.exception), "endpoint_closed")

    def test_07_backend_inherits_every_frozen_device_operation(self) -> None:
        successor = runtime.ReconnectableEsptoolSerialBackend
        frozen = runtime.frozen_adapter.EsptoolSerialBackend
        self.assertTrue(issubclass(successor, frozen))
        self.assertIs(successor.write_application, frozen.write_application)
        self.assertIs(successor.verify_application, frozen.verify_application)
        self.assertIs(successor.hard_reset, frozen.hard_reset)
        self.assertIsNot(successor.open_radio_endpoint, frozen.open_radio_endpoint)

    def test_08_frozen_ot153_sources_are_byte_exact(self) -> None:
        self.assertTrue(runtime.frozen_sources_match())


if __name__ == "__main__":
    unittest.main(verbosity=2)
