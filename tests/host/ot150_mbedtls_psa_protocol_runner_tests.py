#!/usr/bin/env python3
"""Adversarial host-only tests for the OT-150 mbedTLS/PSA START/READY transport."""

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


runner = load("ot150_runner", ROOT / "tools" / "ot150_mbedtls_psa_protocol_runner.py")
frame_fixtures = load(
    "ot149_frame_fixtures", ROOT / "tests" / "host" / "ot149_mbedtls_psa_frame_tests.py"
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

    def test_03_bounded_opaque_boot_chatter_precedes_ready(self) -> None:
        fabricated = b"\x00\xffROM boot\r\n"
        endpoint = Endpoint([fabricated, runner.READY + frame()])
        result = self.invoke(Provider(endpoint))
        self.assertEqual(result.diagnostics.preamble_lines_ignored, 1)
        self.assertEqual(result.diagnostics.bytes_observed, len(fabricated + runner.READY + frame()))

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

    def test_06_record_count_is_bounded_by_the_512_byte_budget(self) -> None:
        exact_nine = ((b"X" * 55) + b"\n") * 8 + (b"Y" * 63) + b"\n"
        self.assertEqual(len(exact_nine), runner.MAX_PREAMBLE_BYTES)
        result = self.invoke(Provider(Endpoint([exact_nine, runner.READY, frame()])))
        self.assertEqual(result.diagnostics.preamble_lines_ignored, 9)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)
        many_short = b"\n" * 64
        result = self.invoke(Provider(Endpoint([many_short, runner.READY, frame()])))
        self.assertEqual(result.diagnostics.preamble_lines_ignored, 64)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)
        self.assert_code(
            runner.FailureCode.PREAMBLE_INVALID,
            Provider(Endpoint([b"X" * (runner.MAX_PREAMBLE_BYTES + 1)])),
        )
        error = self.assert_code(
            runner.FailureCode.FRAME_BEFORE_READY,
            Provider(Endpoint([exact_nine, frame()])),
        )
        self.assertEqual(error.diagnostics.preamble_lines_ignored, 9)
        self.assert_code(
            runner.FailureCode.READY_INVALID,
            Provider(Endpoint([runner.READY, runner.READY])),
        )
    def test_07_exact_ready_and_post_ready_frame_boundaries_remain_strict(self) -> None:
        for lookalike in [
            b"otcbxctl1 ready\n",
            b"OTCBXCTL1 READY\r\n",
            b"prefix OTCBXCTL1 READY\n",
            b"OTCBXCTL1 READY suffix\n",
        ]:
            result = self.invoke(Provider(Endpoint([lookalike, runner.READY, frame()])))
            self.assertEqual(result.diagnostics.preamble_lines_ignored, 1)
        self.assert_code(
            runner.FailureCode.FRAME_MALFORMED,
            Provider(Endpoint([runner.READY, b"\x00opaque\n"])),
        )

    def test_08_partial_timeout_and_overflow_are_distinct(self) -> None:
        self.assert_code(
            runner.FailureCode.PARTIAL_LINE_TIMEOUT,
            Provider(Endpoint([runner.READY[:-1]])),
        )
        self.assert_code(
            runner.FailureCode.PARTIAL_LINE_OVERFLOW,
            Provider(Endpoint([b"X" * (runner.MAX_PARTIAL_LINE_BYTES + 1)])),
        )

    def test_09_lifecycle_start_and_read_failures_have_no_raw_context(self) -> None:
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

    def test_10_benchmark_deadline_starts_after_ready(self) -> None:
        clock = Clock()
        chunks = [b""] * 8 + [runner.READY, frame()]
        result = self.invoke(Provider(Endpoint(chunks)), clock)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)
        self.assertGreaterEqual(clock.now, 0.39)

    def test_11_diagnostics_schema_contains_no_private_material(self) -> None:
        result = self.invoke(Provider(Endpoint([runner.READY, frame()])))
        keys = set(vars(result.diagnostics))
        self.assertEqual(keys, {
            "lifecycle", "reset_attempts", "lifecycle_polls",
            "stable_presence_polls", "open_attempts", "start_write_attempts",
            "read_calls", "empty_reads", "bytes_observed",
            "preamble_lines_ignored", "complete_lines", "frame_lines_buffered",
        })
        self.assertNotIn("PRIVATE-PORT", repr(result.diagnostics))

    def test_12_real_1015_frame_parser_accepts_reassembled_capture(self) -> None:
        payload = frame_fixtures._encode(frame_fixtures._valid_records())
        endpoint = Endpoint([runner.READY, payload])
        result = runner.capture_local_primitives(
            Provider(endpoint), "PRIVATE-PORT", control_timeout=2.0,
            capture_timeout=2.0, presence_timeout=0.5,
        )
        self.assertEqual(result.parsed["candidate_id"], "esp_idf_mbedtls_psa")
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1015)

    def test_13_ot131_shaped_fabricated_record_survives_fragmentation(self) -> None:
        fabricated = (b"\x00\x80" * 255) + b"X\n"
        chunks: list[object] = [fabricated]
        for value in runner.READY:
            chunks.extend((bytes([value]), b""))
        chunks.append(frame())
        result = self.invoke(Provider(Endpoint(chunks)))
        self.assertEqual(result.diagnostics.preamble_lines_ignored, 1)
        self.assertGreater(result.diagnostics.empty_reads, 0)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1)

    def test_14_no_hardware_or_authority_surface_is_present(self) -> None:
        source = (ROOT / "tools" / "ot150_mbedtls_psa_protocol_runner.py").read_text(encoding="utf-8")
        for forbidden in ["argparse", "esptool", "import serial", "subprocess", "write_application", "execution_authority"]:
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
