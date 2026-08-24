#!/usr/bin/env python3
"""Adversarial host-only tests for the OT-129 retrying START/READY protocol."""

from __future__ import annotations

import importlib.util
import sys
import traceback
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


runner = load("ot129_runner", ROOT / "tools" / "ot129_monocypher_protocol_runner.py")
frame_fixtures = load(
    "ot123_frame_fixtures", ROOT / "tests" / "host" / "ot123_monocypher_frame_tests.py"
)


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class Endpoint:
    def __init__(self, chunks: list[object], *, short_write: bool = False) -> None:
        self.chunks = list(chunks)
        self.short_write = short_write
        self.writes: list[bytes] = []
        self.flushes = 0
        self.closed = 0

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data) - 1 if self.short_write else len(data)

    def flush(self) -> None:
        self.flushes += 1

    def read(self, size: int) -> bytes:
        del size
        if not self.chunks:
            return b""
        item = self.chunks.pop(0)
        if isinstance(item, Exception):
            raise item
        return item  # type: ignore[return-value]

    def close(self) -> None:
        self.closed += 1


class Provider:
    def __init__(self, endpoint: Endpoint, presence: list[object] | None = None) -> None:
        self.endpoint = endpoint
        self.presence = list(presence or [True])
        self.resets = 0
        self.opens = 0

    def reset(self, private_endpoint: object) -> None:
        del private_endpoint
        self.resets += 1

    def is_present(self, private_endpoint: object) -> bool:
        del private_endpoint
        item = self.presence.pop(0) if len(self.presence) > 1 else self.presence[0]
        if isinstance(item, Exception):
            raise item
        return bool(item)

    def open(self, private_endpoint: object) -> Endpoint:
        del private_endpoint
        self.opens += 1
        return self.endpoint


def frame() -> bytes:
    return runner.frame_contract.PREFIX + b'{}\n'


class ProtocolTests(unittest.TestCase):
    def invoke(self, provider: Provider, clock: Clock | None = None):
        clock = clock or Clock()
        with mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 1), mock.patch.object(
            runner.frame_contract, "parse_capture_bytes", return_value={"accepted": True}
        ):
            return runner.capture_local_primitives(
                provider, "PRIVATE-PORT", monotonic=clock.monotonic,
                sleep=clock.sleep, control_timeout=2.0, capture_timeout=2.0,
                presence_timeout=0.5,
            )

    def assert_code(self, expected, provider: Provider) -> runner.CaptureError:
        with self.assertRaises(runner.CaptureError) as caught:
            self.invoke(provider)
        error = caught.exception
        self.assertEqual(error.code, expected)
        self.assertEqual(str(error), expected.value)
        self.assertIsNone(error.__context__)
        self.assertIsNone(error.__cause__)
        rendered = "".join(traceback.format_exception(error))
        self.assertNotIn("PRIVATE-PORT", rendered)
        self.assertNotIn("SECRET_SENTINEL", rendered)
        return error

    def test_01_every_byte_and_timeout_boundary_is_preserved(self) -> None:
        stream = runner.READY + frame()
        chunks: list[object] = []
        for value in stream:
            chunks.extend((bytes([value]), b""))
        endpoint = Endpoint(chunks)
        result = self.invoke(Provider(endpoint))
        self.assertEqual(result.parsed, {"accepted": True})
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)
        self.assertGreater(result.diagnostics.empty_reads, 0)
        self.assertTrue(endpoint.writes)
        self.assertTrue(all(item == runner.START for item in endpoint.writes))
        self.assertEqual(endpoint.closed, 1)

    def test_02_start_retries_until_delayed_device_ready(self) -> None:
        clock = Clock()
        endpoint = Endpoint([b""] * 11 + [runner.READY, frame()])
        result = self.invoke(Provider(endpoint), clock)
        self.assertGreaterEqual(result.diagnostics.start_write_attempts, 3)
        self.assertEqual(len(endpoint.writes), result.diagnostics.start_write_attempts)
        self.assertTrue(all(item == runner.START for item in endpoint.writes))

    def test_03_bounded_printable_boot_chatter_precedes_ready(self) -> None:
        endpoint = Endpoint([b"ROM boot\n", runner.READY + frame()])
        result = self.invoke(Provider(endpoint))
        self.assertEqual(result.diagnostics.preamble_lines_ignored, 1)

    def test_04_continuous_and_reenumerated_lifecycles_are_proved(self) -> None:
        continuous_provider = Provider(Endpoint([runner.READY, frame()]))
        continuous = self.invoke(continuous_provider)
        reenumerated_provider = Provider(
            Endpoint([runner.READY, frame()]), [True, False, True]
        )
        reenumerated = self.invoke(reenumerated_provider)
        self.assertEqual(continuous.diagnostics.lifecycle, "stable_continuous")
        self.assertGreaterEqual(continuous.diagnostics.stable_presence_polls, 3)
        self.assertEqual(reenumerated.diagnostics.lifecycle, "reenumerated")
        self.assertEqual(reenumerated_provider.resets, 1)
        self.assertEqual(reenumerated_provider.opens, 1)

    def test_05_frame_before_ready_fails_closed(self) -> None:
        self.assert_code(
            runner.FailureCode.FRAME_BEFORE_READY, Provider(Endpoint([frame()]))
        )

    def test_06_malformed_preamble_and_duplicate_ready_are_classified(self) -> None:
        self.assert_code(
            runner.FailureCode.PREAMBLE_INVALID,
            Provider(Endpoint([b"BAD\x00\n"])),
        )
        self.assert_code(
            runner.FailureCode.READY_INVALID,
            Provider(Endpoint([runner.READY, runner.READY])),
        )

    def test_07_partial_timeout_and_overflow_are_distinct(self) -> None:
        self.assert_code(
            runner.FailureCode.PARTIAL_LINE_TIMEOUT,
            Provider(Endpoint([runner.READY[:-1]])),
        )
        self.assert_code(
            runner.FailureCode.PARTIAL_LINE_OVERFLOW,
            Provider(Endpoint([b"X" * (runner.MAX_PARTIAL_LINE_BYTES + 1)])),
        )

    def test_08_lifecycle_start_and_read_failures_have_no_raw_context(self) -> None:
        self.assert_code(
            runner.FailureCode.ENDPOINT_RETURN_TIMEOUT,
            Provider(Endpoint([]), [False]),
        )
        self.assert_code(
            runner.FailureCode.ENDPOINT_ENUMERATION_FAILED,
            Provider(Endpoint([]), [RuntimeError("SECRET_SENTINEL")]),
        )
        self.assert_code(
            runner.FailureCode.START_WRITE_FAILED,
            Provider(Endpoint([], short_write=True)),
        )
        error = self.assert_code(
            runner.FailureCode.STREAM_READ_FAILED,
            Provider(Endpoint([RuntimeError("SECRET_SENTINEL")])),
        )
        self.assertNotIn("SECRET", repr(error.diagnostics))

    def test_09_benchmark_deadline_starts_after_ready(self) -> None:
        clock = Clock()
        chunks = [b""] * 8 + [runner.READY, frame()]
        result = self.invoke(Provider(Endpoint(chunks)), clock)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)
        self.assertGreaterEqual(clock.now, 0.39)

    def test_10_diagnostics_schema_contains_no_private_material(self) -> None:
        result = self.invoke(Provider(Endpoint([runner.READY, frame()])))
        keys = set(vars(result.diagnostics))
        self.assertEqual(keys, {
            "lifecycle", "reset_attempts", "lifecycle_polls",
            "stable_presence_polls", "open_attempts", "start_write_attempts",
            "read_calls", "empty_reads", "bytes_observed",
            "preamble_lines_ignored", "complete_lines", "frame_lines_buffered",
        })
        self.assertNotIn("PRIVATE-PORT", repr(result.diagnostics))

    def test_11_real_1014_frame_parser_accepts_reassembled_capture(self) -> None:
        payload = frame_fixtures._encode(frame_fixtures._valid_records())
        endpoint = Endpoint([runner.READY, payload])
        result = runner.capture_local_primitives(
            Provider(endpoint), "PRIVATE-PORT", control_timeout=2.0,
            capture_timeout=2.0, presence_timeout=0.5,
        )
        self.assertEqual(result.parsed["candidate_id"], "monocypher")
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1014)


if __name__ == "__main__":
    unittest.main(verbosity=2)
