#!/usr/bin/env python3
"""OT-151 successor transport with strict early-failure recognition."""

from __future__ import annotations

import enum
import importlib.util
import sys
import time
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("runtime_contract_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


base = _load("ot150_mbedtls_psa_protocol_runner", ROOT / "tools" / "ot150_mbedtls_psa_protocol_runner.py")
failure_contract = _load("ot151_mbedtls_psa_failure_frames", ROOT / "tools" / "ot151_mbedtls_psa_failure_frames.py")

frame_contract = base.frame_contract
START = base.START
READY = base.READY
READ_SIZE = base.READ_SIZE
CONTROL_TIMEOUT_SECONDS = base.CONTROL_TIMEOUT_SECONDS
CAPTURE_TIMEOUT_SECONDS = base.CAPTURE_TIMEOUT_SECONDS
PRESENCE_TIMEOUT_SECONDS = base.PRESENCE_TIMEOUT_SECONDS
POLL_SECONDS = base.POLL_SECONDS
START_RETRY_SECONDS = base.START_RETRY_SECONDS
STABLE_PRESENCE_POLLS = base.STABLE_PRESENCE_POLLS
MAX_PREAMBLE_BYTES = base.MAX_PREAMBLE_BYTES
MAX_PARTIAL_LINE_BYTES = base.MAX_PARTIAL_LINE_BYTES
CaptureDiagnostics = base.CaptureDiagnostics
CaptureResult = base.CaptureResult
Endpoint = base.Endpoint
Provider = base.Provider
_Counters = base._Counters
_attempt = base._attempt


class FailureCode(str, enum.Enum):
    RESET_FAILED = "reset_failed"
    ENDPOINT_STABILITY_TIMEOUT = "endpoint_stability_timeout"
    ENDPOINT_RETURN_TIMEOUT = "endpoint_return_timeout"
    ENDPOINT_ENUMERATION_FAILED = "endpoint_enumeration_failed"
    ENDPOINT_OPEN_FAILED = "endpoint_open_failed"
    PREAMBLE_INVALID = "preamble_invalid"
    START_WRITE_FAILED = "start_write_failed"
    READY_TIMEOUT = "ready_timeout"
    READY_INVALID = "ready_invalid"
    FRAME_BEFORE_READY = "frame_before_ready"
    STREAM_READ_FAILED = "stream_read_failed"
    PARTIAL_LINE_OVERFLOW = "partial_line_overflow"
    PARTIAL_LINE_TIMEOUT = "partial_line_timeout"
    FRAME_MALFORMED = "frame_malformed"
    FRAME_COUNT_INCOMPLETE = "frame_count_incomplete"
    FRAME_COUNT_EXCEEDED = "frame_count_exceeded"
    BENCHMARK_REPORTED_FAILURE = "benchmark_reported_failure"


class CaptureError(RuntimeError):
    def __init__(self, code: FailureCode, diagnostics: CaptureDiagnostics):
        super().__init__(code.value)
        self.code = code
        self.diagnostics = diagnostics


def capture_local_primitives(
    provider: Provider,
    private_endpoint: object,
    *,
    monotonic: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
    control_timeout: float = CONTROL_TIMEOUT_SECONDS,
    capture_timeout: float = CAPTURE_TIMEOUT_SECONDS,
    presence_timeout: float = PRESENCE_TIMEOUT_SECONDS,
) -> CaptureResult:
    """Capture one strict success stream or reject one exact early failure."""
    counters = _Counters()

    def fail(code: FailureCode) -> None:
        raise CaptureError(code, counters.freeze())

    present_ok, present_value = _attempt(lambda: provider.is_present(private_endpoint))
    counters.lifecycle_polls += 1
    if not present_ok:
        fail(FailureCode.ENDPOINT_ENUMERATION_FAILED)
    initially_present = bool(present_value)
    counters.reset_attempts = 1
    reset_ok, _ = _attempt(lambda: provider.reset(private_endpoint))
    if not reset_ok:
        fail(FailureCode.RESET_FAILED)

    deadline = monotonic() + presence_timeout
    saw_absent = not initially_present
    stable = 0
    while True:
        counters.lifecycle_polls += 1
        present_ok, present_value = _attempt(lambda: provider.is_present(private_endpoint))
        if not present_ok:
            fail(FailureCode.ENDPOINT_ENUMERATION_FAILED)
        present = bool(present_value)
        if not present:
            saw_absent = True
            stable = 0
        else:
            stable += 1
            counters.stable_presence_polls = stable
            if saw_absent or stable >= STABLE_PRESENCE_POLLS:
                counters.lifecycle = "reenumerated" if saw_absent else "stable_continuous"
                break
        if monotonic() >= deadline:
            fail(
                FailureCode.ENDPOINT_RETURN_TIMEOUT
                if saw_absent
                else FailureCode.ENDPOINT_STABILITY_TIMEOUT
            )
        sleep(POLL_SECONDS)

    counters.open_attempts = 1
    open_ok, endpoint_value = _attempt(lambda: provider.open(private_endpoint))
    if not open_ok or endpoint_value is None:
        fail(FailureCode.ENDPOINT_OPEN_FAILED)
    endpoint = endpoint_value

    try:
        phase = "ready"
        phase_deadline = monotonic() + control_timeout
        next_start = monotonic()
        capture_deadline: float | None = None
        partial = bytearray()
        capture = bytearray()
        preamble_bytes = 0

        while True:
            active_deadline = capture_deadline if phase == "capture" else phase_deadline
            if active_deadline is None or monotonic() >= active_deadline:
                if partial:
                    fail(FailureCode.PARTIAL_LINE_TIMEOUT)
                fail(
                    FailureCode.READY_TIMEOUT
                    if phase == "ready"
                    else FailureCode.FRAME_COUNT_INCOMPLETE
                )
            if phase == "ready" and monotonic() >= next_start:
                counters.start_write_attempts += 1

                def write_start() -> int:
                    written_count = endpoint.write(START)
                    endpoint.flush()
                    return written_count

                write_ok, written_value = _attempt(write_start)
                if (
                    not write_ok
                    or not isinstance(written_value, int)
                    or isinstance(written_value, bool)
                    or written_value != len(START)
                ):
                    fail(FailureCode.START_WRITE_FAILED)
                next_start = monotonic() + START_RETRY_SECONDS

            counters.read_calls += 1
            read_ok, chunk_value = _attempt(lambda: endpoint.read(READ_SIZE))
            if not read_ok or not isinstance(chunk_value, (bytes, bytearray)):
                fail(FailureCode.STREAM_READ_FAILED)
            chunk = bytes(chunk_value)
            if not chunk:
                counters.empty_reads += 1
                sleep(POLL_SECONDS)
                continue
            counters.bytes_observed += len(chunk)
            partial.extend(chunk)
            if len(partial) > MAX_PARTIAL_LINE_BYTES and b"\n" not in partial:
                fail(FailureCode.PARTIAL_LINE_OVERFLOW)

            while True:
                newline = partial.find(b"\n")
                if newline < 0:
                    if (
                        phase == "ready"
                        and preamble_bytes + len(partial) > MAX_PREAMBLE_BYTES
                        and not READY.startswith(partial)
                    ):
                        fail(FailureCode.PREAMBLE_INVALID)
                    if len(partial) > MAX_PARTIAL_LINE_BYTES:
                        fail(FailureCode.PARTIAL_LINE_OVERFLOW)
                    break
                line = bytes(partial[: newline + 1])
                del partial[: newline + 1]
                counters.complete_lines += 1
                if phase == "ready":
                    if line.startswith(frame_contract.PREFIX):
                        fail(FailureCode.FRAME_BEFORE_READY)
                    if line == READY:
                        phase = "capture"
                        capture_deadline = monotonic() + capture_timeout
                        continue
                    preamble_bytes += len(line)
                    counters.preamble_lines_ignored += 1
                    if preamble_bytes > MAX_PREAMBLE_BYTES:
                        fail(FailureCode.PREAMBLE_INVALID)
                    continue
                if line == READY:
                    fail(FailureCode.READY_INVALID)
                if not line.startswith(frame_contract.PREFIX):
                    fail(FailureCode.FRAME_MALFORMED)
                capture.extend(line)
                counters.frame_lines_buffered += 1
                if counters.frame_lines_buffered > frame_contract.EXPECTED_FRAME_COUNT:
                    fail(FailureCode.FRAME_COUNT_EXCEEDED)
                if (
                    counters.frame_lines_buffered < frame_contract.EXPECTED_FRAME_COUNT
                    and failure_contract.looks_like_local_complete(line)
                ):
                    if partial:
                        fail(FailureCode.FRAME_MALFORMED)
                    valid, parsed_failure = _attempt(
                        lambda: failure_contract.parse_early_failure_bytes(bytes(capture))
                    )
                    if not valid or not isinstance(parsed_failure, dict):
                        fail(FailureCode.FRAME_MALFORMED)
                    fail(FailureCode.BENCHMARK_REPORTED_FAILURE)
                if counters.frame_lines_buffered == frame_contract.EXPECTED_FRAME_COUNT:
                    if partial:
                        fail(FailureCode.FRAME_COUNT_EXCEEDED)
                    parse_ok, parsed_value = _attempt(
                        lambda: frame_contract.parse_capture_bytes(bytes(capture))
                    )
                    if not parse_ok or not isinstance(parsed_value, dict):
                        fail(FailureCode.FRAME_MALFORMED)
                    return CaptureResult(parsed=parsed_value, diagnostics=counters.freeze())
    finally:
        try:
            endpoint.close()
        except Exception:
            pass
