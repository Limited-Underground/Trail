#!/usr/bin/env python3
"""Bounded receive-only capture for the existing OT-121 libsodium autorun app.

No hardware CLI. Provider owns the bounded reset and a fresh DTR-false open;
read must have a finite timeout. No prior serial handle is accepted or reused.
"""
import enum
import importlib.util
import json
import math
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Protocol

ROOT = Path(__file__).resolve().parents[1]
_FRAME_PATH = ROOT / "tools" / "ot121_local_primitive_frames.py"
_SPEC = importlib.util.spec_from_file_location("ot121_local_primitive_frames", _FRAME_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("frame_contract_unavailable")
frame_contract = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(frame_contract)

READ_SIZE = 512
# Empty coordinator metadata: the admitted autorun app has no control exchange.
START = b""
READY = b""
CAPTURE_TIMEOUT_SECONDS = 180.0
PRESENCE_TIMEOUT_SECONDS = 5.0
POLL_SECONDS = 0.05
# The admitted autorun app waits 3000 ms before emitting its header. Continuous
# presence therefore needs only 100 ms of observation, not a boot-length sleep.
STABLE_PRESENCE_POLLS = 3
MAX_PREAMBLE_BYTES = 4096
MAX_PACKED_FRAMES_PER_LINE = 8
MAX_PARTIAL_LINE_BYTES = MAX_PACKED_FRAMES_PER_LINE * frame_contract.MAX_FRAME_BYTES + 2
MAX_CAPTURE_BYTES = frame_contract.MAX_CAPTURE_BYTES
FRAMED_KINDS = ("header", "gate", "sample", "operation_summary", "runtime_resources", "local_complete")
REJECTION_CLASSES = ("none", "blank", "nonframe_prefix", "nonascii", "json", "duplicate_key", "noncanonical", "frame_size", "private_text", "packed_count", "internal_framing", "sequence_semantics", "line_size")

class FailureCode(str, enum.Enum):
    RESET_FAILED = "reset_failed"
    ENDPOINT_STABILITY_TIMEOUT = "endpoint_stability_timeout"
    ENDPOINT_RETURN_TIMEOUT = "endpoint_return_timeout"
    ENDPOINT_ENUMERATION_FAILED = "endpoint_enumeration_failed"
    ENDPOINT_OPEN_FAILED = "endpoint_open_failed"
    ENDPOINT_CLOSE_FAILED = "endpoint_close_failed"
    PREAMBLE_INVALID = "preamble_invalid"
    STREAM_READ_FAILED = "stream_read_failed"
    PARTIAL_LINE_OVERFLOW = "partial_line_overflow"
    PARTIAL_LINE_TIMEOUT = "partial_line_timeout"
    FRAME_MALFORMED = "frame_malformed"
    FRAME_COUNT_INCOMPLETE = "frame_count_incomplete"
    FRAME_COUNT_EXCEEDED = "frame_count_exceeded"
    CAPTURE_SIZE_EXCEEDED = "capture_size_exceeded"
    INVALID_LIMIT = "invalid_limit"

@dataclass(frozen=True)
class CaptureDiagnostics:
    lifecycle: str
    reset_attempts: int
    lifecycle_polls: int
    stable_presence_polls: int
    open_attempts: int
    start_write_attempts: int
    read_calls: int
    empty_reads: int
    bytes_observed: int
    preamble_lines_ignored: int
    complete_lines: int
    frame_lines_buffered: int
    rejection_class: str
    rejected_line_bytes: int
    last_framed_kind: str
    last_framed_operation: str
    last_framed_phase: str
    last_framed_iteration: int

@dataclass(frozen=True)
class CaptureResult:
    parsed: dict[str, object]
    diagnostics: CaptureDiagnostics
    canonical_frames: bytes

def validate_diagnostics(value: object) -> dict[str, object] | None:
    """Export only bounded counters and allowlisted framing-only context."""
    if type(value) is not CaptureDiagnostics:
        return None
    raw = vars(value).copy()
    if set(raw) != set(CaptureDiagnostics.__dataclass_fields__):
        return None
    strings = {
        "lifecycle": ("unverified", "reenumerated", "stable_continuous"),
        "rejection_class": REJECTION_CLASSES,
        "last_framed_kind": ("none", "unknown", *FRAMED_KINDS),
        "last_framed_operation": ("none", "unknown", *frame_contract.OPERATIONS),
        "last_framed_phase": ("none", "unknown", *frame_contract.PHASES),
    }
    for key, allowed in strings.items():
        if type(raw[key]) is not str or raw[key] not in allowed:
            return None
    for key in set(raw) - set(strings):
        minimum, maximum = (0, 10_000_000)
        if key == "last_framed_iteration":
            minimum, maximum = -1, frame_contract.REPETITIONS - 1
        elif key == "rejected_line_bytes":
            maximum = MAX_PARTIAL_LINE_BYTES
        if type(raw[key]) is not int or not minimum <= raw[key] <= maximum:
            return None
    return raw

class CaptureError(RuntimeError):
    def __init__(self, code: FailureCode, diagnostics: CaptureDiagnostics):
        super().__init__(code.value)
        self.code = code
        self.diagnostics = diagnostics

class Endpoint(Protocol):
    def read(self, size: int) -> bytes: ...
    def close(self) -> None: ...

class Provider(Protocol):
    def reset(self, private_endpoint: object) -> None: ...
    def is_present(self, private_endpoint: object) -> bool: ...
    def open(self, private_endpoint: object) -> Endpoint: ...

class _Counters:
    def __init__(self):
        for field in CaptureDiagnostics.__dataclass_fields__:
            setattr(self, field, 0)
        self.lifecycle = "unverified"
        self.rejection_class = "none"
        self.last_framed_kind = "none"
        self.last_framed_operation = "none"
        self.last_framed_phase = "none"
        self.last_framed_iteration = -1

    def freeze(self):
        return CaptureDiagnostics(**vars(self))

def _attempt(operation):
    try:
        return True, operation()
    except Exception:
        return False, None

class _FramingError(ValueError):
    """Contains only a fixed diagnostic class, never wire content."""

def _duplicate_checked_pairs(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise _FramingError("duplicate_key")
        value[key] = item
    return value

def _framed_context(frame, counters):
    # Diagnostic context only: full sequence semantics are checked at the end.
    value = json.loads(frame[len(frame_contract.PREFIX):])
    for key, target, allowed in (
        ("record_kind", "last_framed_kind", FRAMED_KINDS),
        ("operation", "last_framed_operation", frame_contract.OPERATIONS),
        ("phase", "last_framed_phase", frame_contract.PHASES),
    ):
        item = value.get(key)
        setattr(counters, target, "none" if key not in value else item if type(item) is str and item in allowed else "unknown")
    iteration = value.get("iteration")
    counters.last_framed_iteration = iteration if type(iteration) is int and 0 <= iteration < frame_contract.REPETITIONS else -1

def _frames(line):
    """Validate packed framing without repairing payloads or discarding gaps."""
    clean = line[:-1]
    if clean.endswith(b"\r"):
        clean = clean[:-1]
    decoder = json.JSONDecoder(object_pairs_hook=_duplicate_checked_pairs)
    frames = []
    while clean:
        if len(frames) >= MAX_PACKED_FRAMES_PER_LINE:
            raise _FramingError("packed_count")
        if not clean.startswith(frame_contract.PREFIX):
            raise _FramingError("nonframe_prefix")
        try:
            suffix = clean[len(frame_contract.PREFIX):].decode("ascii")
        except UnicodeDecodeError:
            raise _FramingError("nonascii") from None
        try:
            value, consumed = decoder.raw_decode(suffix)
        except _FramingError:
            raise
        except (ValueError, RecursionError):
            raise _FramingError("json") from None
        payload = suffix[:consumed].encode("ascii")
        try:
            canonical = json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode("ascii")
        except (ValueError, RecursionError):
            raise _FramingError("noncanonical") from None
        if type(value) is not dict or canonical != payload:
            raise _FramingError("noncanonical")
        frame = frame_contract.PREFIX + payload
        if len(frame) > frame_contract.MAX_FRAME_BYTES:
            raise _FramingError("frame_size")
        if frame_contract.PRIVATE_TEXT.search(suffix[:consumed]):
            raise _FramingError("private_text")
        frames.append(frame + b"\n")
        clean = clean[len(frame):]
    if not frames:
        raise _FramingError("blank")
    return frames

def capture_local_primitives(
    provider: Provider, private_endpoint: object, *,
    monotonic: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
    capture_timeout: float = CAPTURE_TIMEOUT_SECONDS,
    presence_timeout: float = PRESENCE_TIMEOUT_SECONDS,
) -> CaptureResult:
    """Reset once, reopen freshly and retain one strictly validated autorun stream.

    Deadlines are checked after blocking provider calls as well as before reads.
    Bounds cannot preempt an uncooperative backend; its calls must be bounded.
    """
    counters = _Counters()
    def fail(code):
        raise CaptureError(code, counters.freeze()) from None
    for limit, maximum in ((capture_timeout, CAPTURE_TIMEOUT_SECONDS), (presence_timeout, PRESENCE_TIMEOUT_SECONDS)):
        if type(limit) not in (int, float) or not math.isfinite(limit) or not 0 < limit <= maximum:
            fail(FailureCode.INVALID_LIMIT)
    counters.reset_attempts = 1
    ok, _ = _attempt(lambda: provider.reset(private_endpoint))
    if not ok:
        fail(FailureCode.RESET_FAILED)
    deadline = monotonic() + presence_timeout
    saw_absent = False
    stable = 0
    while True:
        counters.lifecycle_polls += 1
        ok, present = _attempt(lambda: provider.is_present(private_endpoint))
        if not ok or type(present) is not bool:
            fail(FailureCode.ENDPOINT_ENUMERATION_FAILED)
        if monotonic() >= deadline:
            fail(FailureCode.ENDPOINT_RETURN_TIMEOUT if saw_absent else FailureCode.ENDPOINT_STABILITY_TIMEOUT)
        if present:
            stable += 1
            counters.stable_presence_polls = stable
            if saw_absent or stable >= STABLE_PRESENCE_POLLS:
                counters.lifecycle = "reenumerated" if saw_absent else "stable_continuous"
                break
        else:
            saw_absent = True
            stable = 0
            counters.stable_presence_polls = 0
        sleep(min(POLL_SECONDS, max(0, deadline - monotonic())))
    counters.open_attempts = 1
    capture_deadline = monotonic() + capture_timeout
    ok, endpoint = _attempt(lambda: provider.open(private_endpoint))
    if not ok or endpoint is None:
        fail(FailureCode.ENDPOINT_OPEN_FAILED)
    partial = bytearray()
    capture = bytearray()
    preamble_bytes = 0
    try:
        while True:
            def check_deadline():
                if monotonic() >= capture_deadline:
                    fail(FailureCode.PARTIAL_LINE_TIMEOUT if partial else FailureCode.FRAME_COUNT_INCOMPLETE)
            check_deadline()
            counters.read_calls += 1
            ok, chunk = _attempt(lambda: endpoint.read(READ_SIZE))
            check_deadline()
            if not ok or not isinstance(chunk, (bytes, bytearray)):
                fail(FailureCode.STREAM_READ_FAILED)
            if not chunk:
                counters.empty_reads += 1
                sleep(min(POLL_SECONDS, max(0, capture_deadline - monotonic())))
                continue
            counters.bytes_observed += len(chunk)
            if counters.bytes_observed > MAX_CAPTURE_BYTES:
                fail(FailureCode.CAPTURE_SIZE_EXCEEDED)
            partial.extend(chunk)
            while b"\n" in partial:
                newline = partial.index(b"\n")
                if newline + 1 > MAX_PARTIAL_LINE_BYTES:
                    counters.rejection_class = "line_size"
                    counters.rejected_line_bytes = MAX_PARTIAL_LINE_BYTES
                    fail(FailureCode.PARTIAL_LINE_OVERFLOW)
                line = bytes(partial[:newline + 1])
                del partial[:newline + 1]
                counters.complete_lines += 1
                if not counters.frame_lines_buffered:
                    first = line.find(frame_contract.PREFIX)
                    discarded = line if first < 0 else line[:first]
                    preamble_bytes += len(discarded)
                    if preamble_bytes > MAX_PREAMBLE_BYTES or any(byte not in (9, 10, 13, 27) and not 32 <= byte <= 126 for byte in discarded):
                        fail(FailureCode.PREAMBLE_INVALID)
                    if first < 0:
                        counters.preamble_lines_ignored += 1
                        continue
                    line = line[first:]
                try:
                    frames = _frames(line)
                except _FramingError as error:
                    counters.rejection_class = str(error)
                    counters.rejected_line_bytes = min(len(line), MAX_PARTIAL_LINE_BYTES)
                    fail(FailureCode.FRAME_MALFORMED)
                except Exception:
                    counters.rejection_class = "internal_framing"
                    counters.rejected_line_bytes = min(len(line), MAX_PARTIAL_LINE_BYTES)
                    fail(FailureCode.FRAME_MALFORMED)
                counters.frame_lines_buffered += len(frames)
                _framed_context(frames[-1], counters)
                if counters.frame_lines_buffered > frame_contract.EXPECTED_FRAME_COUNT:
                    fail(FailureCode.FRAME_COUNT_EXCEEDED)
                capture.extend(b"".join(frames))
                if len(capture) > MAX_CAPTURE_BYTES:
                    fail(FailureCode.CAPTURE_SIZE_EXCEEDED)
                if counters.frame_lines_buffered == frame_contract.EXPECTED_FRAME_COUNT:
                    if partial:
                        fail(FailureCode.FRAME_COUNT_EXCEEDED)
                    ok, parsed = _attempt(lambda: frame_contract.parse_capture_bytes(bytes(capture)))
                    if not ok or not isinstance(parsed, dict):
                        counters.rejection_class = "sequence_semantics"
                        fail(FailureCode.FRAME_MALFORMED)
                    check_deadline()
                    return CaptureResult(parsed, counters.freeze(), bytes(capture))
            if len(partial) > MAX_PARTIAL_LINE_BYTES:
                counters.rejection_class = "line_size"
                counters.rejected_line_bytes = MAX_PARTIAL_LINE_BYTES
                fail(FailureCode.PARTIAL_LINE_OVERFLOW)
            if not counters.frame_lines_buffered and frame_contract.PREFIX not in partial and preamble_bytes + len(partial) > MAX_PREAMBLE_BYTES + len(frame_contract.PREFIX) - 1:
                fail(FailureCode.PREAMBLE_INVALID)
    finally:
        ok, _ = _attempt(endpoint.close)
        if not ok:
            fail(FailureCode.ENDPOINT_CLOSE_FAILED)
