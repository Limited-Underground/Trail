"""Actual successor endpoint regressions; all clocks and serial handles are fake."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import security_policy_endpoint_tests as predecessor_tests
import security_policy_endpoint as frozen
import security_policy_deadline_endpoint as corrected
from security_policy_capture import CaptureError

C = predecessor_tests.C
R = predecessor_tests.R
DEADLINE = 30.0
BEFORE = 29.999999
CROSSINGS = ("first", "second")


class WindowHandle:
    """Move time in I/O/admission, including either gap after capture's check."""
    def __init__(self, *, crossing=None, receipt=True, returned=None, fault=None):
        self.crossing, self.returned, self.fault = crossing, returned, fault
        self.is_open = True
        self.now = 0.0
        self.timeout = self.write_timeout = 0.0
        self.chunks = [R] if receipt else []
        self.read_starts, self.writes = [], []
        self.close_calls = self.final_guards = 0
        self.armed = self.fired = False
        self.clock_fault = None

    def clock(self):
        if self.clock_fault == "throw":
            raise RuntimeError("PRIVATE_CLOCK_DETAIL")
        if self.clock_fault is not None:
            return self.clock_fault
        return self.now

    def guard(self):
        if self.armed:
            self.final_guards += 1
            # The first guard is post-read. The next callback runs guards 2/3.
            target = {"first": 2, "second": 3}.get(self.crossing)
            if target == self.final_guards:
                self.now, self.fired = DEADLINE, True
                if self.fault == "guard_false": return False
                if self.fault == "guard_truthy": return 1
                if self.fault == "guard_throw": raise RuntimeError("PRIVATE_GUARD_DETAIL")
                if self.fault == "closed": self.is_open = False
                if self.fault == "regression": self.now = 0.0
                if self.fault == "nan": self.clock_fault = float("nan")
                if self.fault == "infinity": self.clock_fault = float("inf")
                if self.fault == "boolean_clock": self.clock_fault = True
                if self.fault == "clock_throw": self.clock_fault = "throw"
        return True

    def write(self, raw):
        self.writes.append(raw)
        return len(raw)

    def read(self, size):
        if size != 129 or not 0 < self.timeout <= 0.25:
            raise AssertionError("invalid bounded read")
        self.read_starts.append(self.now)
        if self.now == BEFORE:
            self.now = DEADLINE
            return self.returned if self.returned is not None else b""
        ended = self.now + min(0.125, self.timeout)
        if ended >= DEADLINE:
            self.now, self.armed, self.final_guards = BEFORE, True, 0
        else:
            self.now = ended
        return self.chunks.pop(0) if self.chunks else b""

    def close(self):
        self.close_calls += 1
        self.is_open = False


def error_chain(error):
    result = []
    seen = set()
    while error is not None and id(error) not in seen:
        seen.add(id(error))
        if type(error) in (frozen.EndpointError, CaptureError):
            result.append(error.args[0])
        error = error.__context__
    return result


class Tests(predecessor_tests.Tests):
    """Reuse the frozen endpoint's behavioral corpus with the successor factory."""
    def endpoint(self, handle, guard=lambda: True):
        return corrected.Endpoint(handle, guard=guard, monotonic=handle.clock)

    def window_endpoint(self, handle, endpoint_type=corrected.Endpoint):
        return endpoint_type(handle, guard=handle.guard, monotonic=handle.clock)

    def refused(self, handle, reason, endpoint_type=corrected.Endpoint):
        endpoint = self.window_endpoint(handle, endpoint_type)
        with self.assertRaises(frozen.EndpointError) as caught:
            endpoint.run_once(C, DEADLINE)
        self.assertIn(reason, error_chain(caught.exception))
        self.assertTrue(all(start < DEADLINE for start in handle.read_starts))
        self.assertTrue(endpoint.close())
        with self.assertRaisesRegex(frozen.EndpointError, "endpoint_consumed"):
            endpoint.run_once(C, DEADLINE)
        return caught.exception

    def test_frozen_error_type_and_lifecycle_are_reused(self):
        self.assertIs(corrected.EndpointError, frozen.EndpointError)
        for name in ("__init__", "_admit", "_now", "close"):
            self.assertIs(getattr(corrected.Endpoint, name), getattr(frozen.Endpoint, name))

    def test_both_pre_read_crossings_finish_earlier_receipt_without_read(self):
        for crossing in CROSSINGS:
            with self.subTest(crossing=crossing):
                handle = WindowHandle(crossing=crossing)
                endpoint = self.window_endpoint(handle)
                self.assertEqual(R, endpoint.run_once(C, DEADLINE))
                self.assertTrue(handle.fired)
                self.assertEqual(DEADLINE, handle.now)
                self.assertEqual(1, len(handle.writes))
                self.assertTrue(all(start < DEADLINE for start in handle.read_starts))
                self.assertLess(max(handle.read_starts), BEFORE)
                self.assertTrue(endpoint.close())
                self.assertTrue(endpoint.close())
                self.assertEqual(1, handle.close_calls)
                with self.assertRaisesRegex(frozen.EndpointError, "endpoint_consumed"):
                    endpoint.run_once(C, DEADLINE)

    def test_frozen_endpoint_still_reproduces_both_failures(self):
        for crossing in CROSSINGS:
            with self.subTest(crossing=crossing):
                handle = WindowHandle(crossing=crossing)
                error = self.refused(handle, "endpoint_read_late", frozen.Endpoint)
                self.assertIn("read_failed", error_chain(error))
                self.assertTrue(handle.fired)

    def test_both_crossings_without_receipt_still_timeout(self):
        for crossing in CROSSINGS:
            with self.subTest(crossing=crossing):
                handle = WindowHandle(crossing=crossing, receipt=False)
                error = self.refused(handle, "receipt_timeout")
                self.assertNotIn("read_failed", error_chain(error))
                self.assertTrue(handle.fired)

    def test_first_pre_read_crossing_without_any_raw_read_is_not_success(self):
        handle = WindowHandle(receipt=False)
        calls = 0
        def guard():
            nonlocal calls
            calls += 1
            if calls == 4: handle.now = DEADLINE
            return True
        endpoint = corrected.Endpoint(handle, guard=guard, monotonic=handle.clock)
        with self.assertRaises(frozen.EndpointError) as caught:
            endpoint.run_once(C, DEADLINE)
        self.assertIn("receipt_timeout", error_chain(caught.exception))
        self.assertEqual([], handle.read_starts)

    def test_success_observes_entire_thirty_second_horizon(self):
        handle = WindowHandle()
        self.assertEqual(R, self.window_endpoint(handle).run_once(C, DEADLINE))
        self.assertEqual(DEADLINE, handle.now)
        self.assertIn(BEFORE, handle.read_starts)
        self.assertGreater(len(handle.read_starts), 200)

    def test_empty_control_still_times_out(self):
        self.refused(WindowHandle(receipt=False), "receipt_timeout")

    def test_returned_bytes_at_deadline_still_refuse(self):
        for raw in (b"x", R):
            with self.subTest(length=len(raw)):
                self.refused(WindowHandle(returned=raw), "late_output")

    def test_late_first_receipt_still_refuses(self):
        self.refused(WindowHandle(receipt=False, returned=R), "late_output")

    def test_crossing_never_hides_guard_or_open_failure(self):
        for crossing in CROSSINGS:
            for fault in ("guard_false", "guard_truthy", "guard_throw", "closed"):
                with self.subTest(crossing=crossing, fault=fault):
                    self.refused(WindowHandle(crossing=crossing, fault=fault), "endpoint_admission_failed")

    def test_crossing_never_hides_invalid_or_regressing_clock(self):
        for crossing in CROSSINGS:
            for fault in ("regression", "nan", "infinity", "boolean_clock", "clock_throw"):
                with self.subTest(crossing=crossing, fault=fault):
                    self.refused(WindowHandle(crossing=crossing, fault=fault), "endpoint_clock_invalid")

    def test_post_window_admission_failure_still_refuses(self):
        handle = WindowHandle(crossing="first")
        original = handle.guard
        def guard():
            if handle.fired: return False
            return original()
        handle.guard = guard
        self.refused(handle, "endpoint_admission_failed")

    def test_pre_deadline_trailing_and_duplicate_output_still_refuse(self):
        for suffix in (b"x", R):
            with self.subTest(length=len(suffix)):
                handle = WindowHandle()
                handle.chunks.append(suffix)
                self.refused(handle, "trailing_output")

    def test_malformed_or_wrong_challenge_never_becomes_success(self):
        for raw in (b"PRIVATE_JUNK\n", R.replace(C.encode(), b"0" * 32), R[:-1], R + R):
            with self.subTest(length=len(raw)):
                handle = WindowHandle(crossing="first")
                handle.chunks = [raw]
                expected = "receipt_timeout" if raw == R[:-1] else "receipt_invalid"
                self.refused(handle, expected)

    def test_invalid_or_oversized_raw_read_still_refuses(self):
        for raw in (None, bytearray(), 1, b"x" * 129):
            with self.subTest(kind=type(raw).__name__):
                handle = WindowHandle()
                handle.chunks = [raw]
                self.refused(handle, "read_invalid")

    def test_read_exception_is_not_end_of_window(self):
        handle = WindowHandle()
        original = handle.read
        def read(size):
            if handle.read_starts: raise RuntimeError("PRIVATE_TRANSPORT_DETAIL")
            return original(size)
        handle.read = read
        error = self.refused(handle, "read_failed")
        self.assertNotIn("PRIVATE", str(error))
        self.assertTrue(error.__suppress_context__)

    def test_nonadvancing_clock_exhausts_read_budget(self):
        handle = WindowHandle(receipt=False)
        def read(size):
            handle.read_starts.append(handle.now)
            return b""
        handle.read = read
        self.refused(handle, "read_budget")
        self.assertEqual(4096, len(handle.read_starts))

    def test_timeout_assignment_failure_is_not_end_of_window(self):
        class BrokenTimeout(WindowHandle):
            def __setattr__(self, name, value):
                if name == "timeout" and getattr(self, "armed", False):
                    raise RuntimeError("PRIVATE_TIMEOUT_DETAIL")
                super().__setattr__(name, value)
        self.refused(BrokenTimeout(crossing="second"), "read_failed")

    def test_close_failure_after_window_success_remains_blocking(self):
        handle = WindowHandle(crossing="first")
        endpoint = self.window_endpoint(handle)
        self.assertEqual(R, endpoint.run_once(C, DEADLINE))
        handle.close = lambda: None
        with self.assertRaisesRegex(frozen.EndpointError, "endpoint_close_unconfirmed"):
            endpoint.close()
        with self.assertRaisesRegex(frozen.EndpointError, "endpoint_consumed"):
            endpoint.run_once(C, DEADLINE)
        handle.close = lambda: setattr(handle, "is_open", False)
        self.assertTrue(endpoint.close())


if __name__ == "__main__":
    unittest.main()
