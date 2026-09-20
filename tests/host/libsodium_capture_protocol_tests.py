#!/usr/bin/env python3
"""Host-only adversarial capture tests using the admitted OT-121 parser fixture."""
import importlib.util
import dataclasses
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
p = load("libsodium_capture_protocol", ROOT / "tools/libsodium_capture_protocol.py")
f = load("ot121_fixture", ROOT / "tests/host/ot121_local_primitive_frame_tests.py")
RAW = f._encode(f._valid_records())
LINES = RAW.splitlines(keepends=True)

class Clock:
    def __init__(self): self.value = 0.0
    def now(self): return self.value
    def sleep(self, value): self.value += value

class Fake:
    def __init__(self, chunks, presence=None, failure=None, clock=None):
        self.chunks = iter(chunks)
        self.presence = iter(presence or [True] * 20)
        self.failure = failure
        self.clock = clock
        self.events = []
    def reset(self, endpoint):
        self.events.append("reset")
        if self.failure == "reset": raise RuntimeError("sensitive-endpoint")
    def is_present(self, endpoint):
        self.events.append("present")
        if self.failure == "present": raise RuntimeError("sensitive-endpoint")
        return next(self.presence, False)
    def open(self, endpoint):
        self.events.append("open")
        if self.failure == "open": raise RuntimeError("sensitive-endpoint")
        return self
    def read(self, size):
        self.events.append("read")
        if self.failure == "read": raise RuntimeError("sensitive-endpoint")
        if self.failure == "late": self.clock.sleep(181)
        return next(self.chunks, b"")
    def close(self):
        self.events.append("close")
        if self.failure == "close": raise RuntimeError("sensitive-endpoint")

def chunks(raw, size=487):
    return [raw[i:i + size] for i in range(0, len(raw), size)]

class CaptureTests(unittest.TestCase):
    def run_capture(self, fake, **limits):
        clock = fake.clock or Clock()
        return p.capture_local_primitives(fake, object(), monotonic=clock.now, sleep=clock.sleep, **limits)
    def rejects(self, payload, code, **options):
        fake = Fake(payload, **options)
        with self.assertRaises(p.CaptureError) as raised:
            self.run_capture(fake, capture_timeout=0.2)
        self.assertEqual(raised.exception.code.value, code)
        self.assertNotIn("sensitive", str(raised.exception))
        self.assertEqual(fake.events[-1], "close")
        return raised.exception
    def test_late_blank_and_backtrace_diagnostics_keep_last_framed_context(self):
        for line, classification in ((b"\r\n", "blank"), (b"Backtrace: 0x1234\n", "nonframe_prefix")):
            error = self.rejects(chunks(b"".join(LINES[:1417]) + line), "frame_malformed")
            diagnostic = error.diagnostics
            self.assertEqual(diagnostic.frame_lines_buffered, 1417)
            self.assertEqual(diagnostic.rejection_class, classification)
            self.assertEqual(diagnostic.rejected_line_bytes, len(line))
            self.assertEqual(diagnostic.last_framed_kind, "operation_summary")
            self.assertEqual(diagnostic.last_framed_operation, "chacha20poly1305_decrypt")
            self.assertEqual(diagnostic.last_framed_phase, "warm")
            self.assertEqual(diagnostic.last_framed_iteration, -1)
            self.assertIsNotNone(p.validate_diagnostics(diagnostic))
    def test_failure_classes_do_not_leak_wire_text(self):
        for line, classification in (
            (p.frame_contract.PREFIX + b'{bad json}\n', "json"),
            (LINES[0].replace(b'header', b'COM77'), "private_text"),
            (LINES[0].replace(b'{', b'{"version":2,', 1), "duplicate_key"),
            (LINES[0].replace(b'{', b'{ ', 1), "noncanonical"),
            (p.frame_contract.PREFIX + b'{"a":"\xff"}\n', "nonascii"),
            (p.frame_contract.PREFIX + b'{"a":"' + b'x' * 2050 + b'"}\n', "frame_size"),
        ):
            error = self.rejects([LINES[0], line], "frame_malformed")
            self.assertEqual(error.diagnostics.rejection_class, classification)
            exported = p.validate_diagnostics(error.diagnostics)
            self.assertIsNotNone(exported)
            self.assertNotIn("COM77", json.dumps(exported))
            self.assertNotIn("bad json", json.dumps(exported))
            self.assertNotIn("COM77", str(error))
    def test_packed_bad_prefix_retains_previous_whole_line_context(self):
        error = self.rejects([LINES[0], LINES[1][:-1] + b"bad-prefix" + LINES[2]], "frame_malformed")
        self.assertEqual(error.diagnostics.rejection_class, "nonframe_prefix")
        self.assertEqual(error.diagnostics.frame_lines_buffered, 1)
        self.assertEqual(error.diagnostics.last_framed_kind, "header")
    def test_fragmented_malformed_line_same_diagnostics(self):
        line = p.frame_contract.PREFIX + b'{"a":broken}\n'
        first = self.rejects([LINES[0], line], "frame_malformed")
        second = self.rejects([LINES[0], line[:8], b"", line[8:]], "frame_malformed")
        self.assertEqual(first.diagnostics.rejection_class, second.diagnostics.rejection_class)
        self.assertEqual(first.diagnostics.rejected_line_bytes, second.diagnostics.rejected_line_bytes)
    def test_context_is_allowlisted_not_semantic_acceptance(self):
        line = p.frame_contract.PREFIX + b'{"record_kind":"custom-sensitive","operation":[],"phase":"custom-sensitive","iteration":100}\n'
        error = self.rejects([line, b"\n"], "frame_malformed")
        self.assertEqual(error.diagnostics.last_framed_kind, "unknown")
        self.assertEqual(error.diagnostics.last_framed_operation, "unknown")
        self.assertEqual(error.diagnostics.last_framed_phase, "unknown")
        self.assertEqual(error.diagnostics.last_framed_iteration, -1)
        self.assertNotIn("custom-sensitive", repr(error.diagnostics))
    def test_diagnostic_validator_rejects_unbounded_injected_fields(self):
        good = self.run_capture(Fake(chunks(RAW))).diagnostics
        self.assertEqual(good.last_framed_kind, "local_complete")
        self.assertEqual(good.rejection_class, "none")
        self.assertIsNotNone(p.validate_diagnostics(good))
        for key, value in (
            ("lifecycle", "COM77"), ("rejection_class", "COM77"),
            ("last_framed_kind", []), ("last_framed_operation", "COM77"),
            ("last_framed_phase", "COM77"), ("last_framed_iteration", True),
            ("last_framed_iteration", 100), ("last_framed_iteration", -2),
            ("rejected_line_bytes", p.MAX_PARTIAL_LINE_BYTES + 1),
            ("bytes_observed", 10_000_001), ("bytes_observed", -1),
        ):
            self.assertIsNone(p.validate_diagnostics(dataclasses.replace(good, **{key: value})))
        self.assertIsNone(p.validate_diagnostics(dataclasses.asdict(good)))
        self.assertIsNone(p.validate_diagnostics(p.CaptureDiagnostics))
        injected = dataclasses.replace(good)
        object.__setattr__(injected, "unapproved", "COM77")
        self.assertIsNone(p.validate_diagnostics(injected))
    def test_fragmented_timeouts_retain_every_canonical_frame(self):
        wire = b"boot\r\n" + RAW.replace(b"\n", b"\r\n")
        stream = []
        for chunk in chunks(wire): stream.extend([chunk, b""])
        fake = Fake(stream)
        result = self.run_capture(fake)
        self.assertEqual(result.canonical_frames, RAW)
        self.assertEqual(result.parsed, p.frame_contract.parse_capture_bytes(RAW))
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1621)
        self.assertEqual(result.diagnostics.start_write_attempts, 0)
        self.assertEqual(fake.events[:5], ["reset", "present", "present", "present", "open"])
        self.assertEqual(fake.events[-1], "close")
    def test_packed_max_eight_frames(self):
        wire = b"".join(b"".join(line.rstrip(b"\n") for line in LINES[i:i + 8]) + b"\n" for i in range(0, len(LINES), 8))
        result = self.run_capture(Fake(chunks(b"boot " + wire)))
        self.assertEqual(result.canonical_frames, RAW)
    def test_reenumeration_opens_at_first_return(self):
        fake = Fake(chunks(RAW), presence=[False, True])
        self.assertEqual(self.run_capture(fake).diagnostics.lifecycle, "reenumerated")
        self.assertEqual(fake.events[:4], ["reset", "present", "present", "open"])
    def test_packed_ninth_rejected(self):
        error = self.rejects([b"".join(line.rstrip(b"\n") for line in LINES[:9]) + b"\n"], "frame_malformed")
        self.assertEqual(error.diagnostics.rejection_class, "packed_count")
    def test_post_header_noise_and_packed_gaps_rejected(self):
        for noise in (b"noise\n", b"\n", b" \n", b"\x00\n"):
            self.rejects([LINES[0] + noise], "frame_malformed")
        self.rejects([LINES[0][:-1] + b" " + LINES[1]], "frame_malformed")
    def test_noncanonical_duplicate_and_private_rejected(self):
        for line in (LINES[0].replace(b'{', b'{ ', 1), LINES[0].replace(b'{', b'{"version":2,', 1), LINES[0].replace(b'header', b'COM77')):
            self.rejects([line], "frame_malformed")
    def test_final_parser_rejects_semantically_wrong_sequence(self):
        error = self.rejects([LINES[1] + LINES[0] + b"".join(LINES[2:])], "frame_malformed")
        self.assertEqual(error.diagnostics.rejection_class, "sequence_semantics")
    def test_extra_same_chunk_frame_or_partial_rejected(self):
        for suffix in (LINES[0], b"x", b"\n"):
            self.rejects([RAW + suffix], "frame_count_exceeded")
    def test_partial_and_incomplete_timeouts(self):
        self.rejects([LINES[0], LINES[1][:10], b""], "partial_line_timeout")
        self.rejects([LINES[0]], "frame_count_incomplete")
    def test_bounded_preamble_lines_partial_and_binary(self):
        for payload in (b"a\n" * (p.MAX_PREAMBLE_BYTES // 2 + 1), b"a" * (p.MAX_PREAMBLE_BYTES + 20), b"bad\x00\n"):
            self.rejects([payload], "preamble_invalid")
    def test_ansi_boot_preamble_discarded_only_before_first_frame(self):
        boot = b"\x1b[0;32mboot message\x1b[0m\r\n" * 100
        result = self.run_capture(Fake(chunks(boot + RAW)))
        self.assertEqual(result.canonical_frames, RAW)
        self.assertNotIn(b"\x1b", result.canonical_frames)
        self.rejects([LINES[0] + boot], "frame_malformed")
    def test_partial_line_and_complete_line_overflow(self):
        for suffix in (b"", b"\n"):
            error = self.rejects([LINES[0], b"a" * (p.MAX_PARTIAL_LINE_BYTES + 1) + suffix], "partial_line_overflow")
            self.assertEqual(error.diagnostics.rejection_class, "line_size")
            self.assertEqual(error.diagnostics.rejected_line_bytes, p.MAX_PARTIAL_LINE_BYTES)
    def test_total_byte_limit(self):
        self.rejects([b"x" * (p.MAX_CAPTURE_BYTES + 1)], "capture_size_exceeded")
    def test_read_close_failures_and_late_success(self):
        self.rejects([], "stream_read_failed", failure="read")
        self.rejects([RAW], "endpoint_close_failed", failure="close")
        self.rejects([RAW], "frame_count_incomplete", failure="late", clock=Clock())
    def test_lifecycle_failures_never_open_or_read(self):
        for failure, code in (("reset", "reset_failed"), ("present", "endpoint_enumeration_failed"), ("open", "endpoint_open_failed")):
            fake = Fake([], failure=failure)
            with self.assertRaises(p.CaptureError) as raised: self.run_capture(fake)
            self.assertEqual(raised.exception.code.value, code)
            self.assertNotIn("read", fake.events)
            self.assertNotIn("close", fake.events)
    def test_absent_endpoint_times_out(self):
        fake = Fake([], presence=[False])
        with self.assertRaises(p.CaptureError) as raised: self.run_capture(fake, presence_timeout=0.1)
        self.assertEqual(raised.exception.code.value, "endpoint_return_timeout")
        self.assertNotIn("open", fake.events)
    def test_continuous_presence_must_be_stable_before_deadline(self):
        fake = Fake([], presence=[True] * 20)
        with self.assertRaises(p.CaptureError) as raised:
            self.run_capture(fake, presence_timeout=0.075)
        self.assertEqual(raised.exception.code.value, "endpoint_stability_timeout")
        self.assertNotIn("open", fake.events)
    def test_slow_open_cannot_extend_capture_deadline(self):
        clock = Clock()
        fake = Fake([RAW], clock=clock)
        original_open = fake.open
        def late_open(endpoint):
            clock.sleep(181)
            return original_open(endpoint)
        fake.open = late_open
        with self.assertRaises(p.CaptureError) as raised: self.run_capture(fake)
        self.assertEqual(raised.exception.code.value, "frame_count_incomplete")
        self.assertNotIn("read", fake.events)
        self.assertEqual(fake.events[-1], "close")
    def test_diagnostics_are_compatible_and_stream_has_no_control_exchange(self):
        expected = {"lifecycle", "reset_attempts", "lifecycle_polls", "stable_presence_polls", "open_attempts", "start_write_attempts", "read_calls", "empty_reads", "bytes_observed", "preamble_lines_ignored", "complete_lines", "frame_lines_buffered"}
        self.assertEqual(set(p.CaptureDiagnostics.__dataclass_fields__), expected | {"rejection_class", "rejected_line_bytes", "last_framed_kind", "last_framed_operation", "last_framed_phase", "last_framed_iteration"})
        self.assertEqual(p.START, b"")
        self.assertEqual(p.READY, b"")
    def test_invalid_or_expanded_limit_never_resets(self):
        for value in (0, -1, True, float("nan"), float("inf"), 181):
            fake = Fake([])
            with self.assertRaises(p.CaptureError): self.run_capture(fake, capture_timeout=value)
            self.assertEqual(fake.events, [])

if __name__ == "__main__": unittest.main()
