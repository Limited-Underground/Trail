"""No hardware: parser, passive identity and fresh serial lifecycle bounds."""
from pathlib import Path
import sys
import copy
import unittest
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import ble_startup_diagnostics as d


class CaptureTests(unittest.TestCase):
    def capture(self, raw=b"", *, ports=None, fail=None, deadline_open=False, chunk_size=256):
        clock = [0.0]
        events = []
        class Serial:
            def __init__(self, **kwargs):
                events.append(("construct", kwargs))
                self.raw = raw
            def __setattr__(self, name, value):
                if name in ("dtr", "rts", "port"):
                    events.append((name, value))
                object.__setattr__(self, name, value)
            def open(self):
                events.append(("open",))
                if deadline_open:
                    clock[0] = 15.1
                if fail == "open":
                    raise OSError("private identifier secret")
            def read(self, size):
                events.append(("read", size))
                clock[0] += min(self.timeout, 0.1)
                if fail == "read":
                    raise OSError("private identifier secret")
                size = min(size, chunk_size)
                value, self.raw = self.raw[:size], self.raw[size:]
                return value
            def close(self):
                events.append(("close",))
                if fail == "close":
                    raise OSError("private identifier secret")
        selected = SimpleNamespace(device="COM7", vid=0x303a, pid=0x1001,
                                   serial_number="010203040506")
        def enumerate_ports():
            if fail == "enumerate":
                raise OSError("private identifier secret")
            return [selected] if ports is None else ports(selected)
        result = d.capture_startup(SimpleNamespace(Serial=Serial), enumerate_ports,
            "010203040506", monotonic=lambda: clock[0],
            sleep=lambda value: clock.__setitem__(0, clock[0] + value), wall_time=lambda: 1789350000.25)
        self.assertNotIn("secret", str(result))
        self.assertTrue(result["early_output_may_be_missing"])
        self.assertLessEqual(clock[0], 15.25)
        return result, events

    def test_allowlist_and_numeric_bounds(self):
        cases = {b"companion boot self-check PASS": "self_check_pass",
                 b"companion boot self-check FAIL": "self_check_fail",
                 b"companion runtime FAIL": "runtime_fail",
                 b"companion runtime started": "runtime_started",
                 b"companion runtime start error=2": "runtime_start_error",
                 b"companion security stage=3": "security_stage",
                 b"companion security detail=4": "security_detail",
                 b"heartbeat elapsed_ms=5000": "heartbeat",
                 b"app_stack minimum_free_bytes=8000": "app_stack"}
        for body, category in cases.items():
            self.assertEqual(d.parse_startup_line(b"I (25) ot_bench: " + body)["category"], category)
        for body in (b"companion security stage=256", b"companion security detail=-1",
                     b"heartbeat elapsed_ms=18446744073709551616",
                     b"app_stack minimum_free_bytes=4294967296", b"private secret"):
            self.assertIsNone(d.parse_startup_line(b"I (25) ot_bench: " + body))
        self.assertIsNone(d.parse_startup_line(b"I (25) other_tag: companion runtime FAIL"))
        self.assertIsNone(d.parse_startup_line(b"\xff"))

    def test_panic_reset_only_fixed_categories(self):
        self.assertEqual(d.parse_startup_line(b"Guru Meditation Error: Core 0 panic'ed (LoadProhibited). private secret"), {"category": "panic", "reason": "load_prohibited"})
        self.assertEqual(d.parse_startup_line(b"rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)"), {"category": "reset_reason", "value": 1})

    def test_fresh_handle_levels_before_open_and_no_writes(self):
        result, events = self.capture(b"I (2) ot_bench: companion runtime started\nI (5) ot_bench: heartbeat elapsed_ms=5\n")
        self.assertEqual([x[0] for x in events[:5]], ["construct", "dtr", "rts", "port", "open"])
        self.assertEqual(events[1:3], [("dtr", False), ("rts", False)])
        self.assertIsNone(events[0][1]["port"])
        self.assertEqual(events[-1], ("close",))
        self.assertEqual(result["status"], "window_complete")
        self.assertEqual(len(result["markers"]), 2)

    def test_no_output_does_not_claim_boot_failure(self):
        result, _ = self.capture()
        self.assertEqual(result["status"], "window_complete")
        self.assertEqual(result["markers"], [])

    def test_missing_identity_never_opens(self):
        result, events = self.capture(ports=lambda selected: [])
        self.assertEqual(result["status"], "device_not_observed")
        self.assertEqual(events, [])

    def test_duplicate_identity_never_opens(self):
        result, events = self.capture(ports=lambda selected: [selected, selected])
        self.assertEqual(result["status"], "identity_ambiguous")
        self.assertEqual(events, [])

    def test_serial_errors_are_observation_categories(self):
        for failure, status in (("enumerate", "enumeration_error"), ("open", "open_error"),
                                ("read", "read_error"), ("close", "close_error")):
            result, events = self.capture(fail=failure)
            self.assertEqual(result["status"], status)
            self.assertEqual(result["markers"], [])
            if failure != "enumerate":
                self.assertEqual(events[-1], ("close",))

    def test_open_time_consumes_observation_window(self):
        result, events = self.capture(deadline_open=True)
        self.assertEqual(result["status"], "window_complete")
        self.assertFalse(any(event[0] == "read" for event in events))

    def test_line_byte_and_marker_limits(self):
        result, _ = self.capture(b"X" * 513)
        self.assertEqual(result["status"], "line_limit")
        result, _ = self.capture(b"x\n" * 8192)
        self.assertEqual(result["status"], "byte_limit")
        self.assertEqual(result["bytes_read"], 16384)
        result, _ = self.capture(b"I (1) ot_bench: companion runtime FAIL\n" * 129)
        self.assertEqual(result["status"], "marker_limit")
        self.assertEqual(len(result["markers"]), 128)

    def test_strict_subprocess_result_validator(self):
        valid, _ = self.capture(b"I (1) ot_bench: heartbeat elapsed_ms=1\n")
        self.assertEqual(d.validate_startup_result(valid), valid)
        mutations = (("raw_log", "secret"), ("status", "secret"),
                     ("elapsed_ms", True), ("elapsed_ms", 240001),
                     ("bytes_read", True), ("bytes_read", 16385),
                     ("early_output_may_be_missing", 1),
                     ("markers", [{"category": "heartbeat", "value": True}]),
                     ("markers", [{"category": "heartbeat", "value": 1 << 64}]),
                     ("markers", [{"category": "secret"}]),
                     ("markers", [{"category": "panic", "raw": "secret"}]),
                     ("markers", [{"category": "security_stage", "value": 256}]))
        for key, value in mutations:
            invalid = copy.deepcopy(valid)
            invalid[key] = value
            with self.assertRaisesRegex(ValueError, "^startup_result_invalid$"):
                d.validate_startup_result(invalid)
        for invalid in (None, [], {}, {**valid, "markers": tuple(valid["markers"])}):
            with self.assertRaisesRegex(ValueError, "^startup_result_invalid$"):
                d.validate_startup_result(invalid)

    def test_reset_and_watchdog_actual_formats(self):
        self.assertEqual(d.parse_startup_line(b"rst:0xff,boot:0x8"), {"category": "reset_reason", "value": 255})
        for raw in (b"rst:0x100,boot:0x8", b"rst:0x-1,boot:0x8", b"rst:0x1,boot:0x8 secret",
                    b"prefix rst:0x1,boot:0x8", b"rst:0x1,garbageboot:0x8"):
            self.assertIsNone(d.parse_startup_line(raw))
        self.assertEqual(d.parse_startup_line(b"Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU1). private discarded"), {"category": "interrupt_watchdog"})
        self.assertEqual(d.parse_startup_line(b"E (123) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:"), {"category": "task_watchdog"})
        self.assertIsNone(d.parse_startup_line(b"E (123) task_wdt: - secret_task (CPU 0)"))

    def test_sdk_panic_print_dec_composition(self):
        # panic.c prints "Core " then panic_print_dec(core), which pads to two columns.
        for core in (0, 1):
            for reason, category in (("LoadProhibited", "panic"),
                                     ("Interrupt wdt timeout on CPU0", "interrupt_watchdog"),
                                     ("Interrupt wdt timeout on CPU1", "interrupt_watchdog")):
                line = ("Guru Meditation Error: Core " + f"{core:2d}" +
                        " panic'ed (" + reason + "). private details discarded").encode()
                self.assertEqual(d.parse_startup_line(line)["category"], category)
                captured, _ = self.capture(line + b"\n")
                self.assertEqual(captured["markers"][0]["category"], category)
                self.assertNotIn("private details", str(captured))
        for core_field in ("   0", "2", "\t0"):
            line = ("Guru Meditation Error: Core " + core_field + " panic'ed (LoadProhibited). ").encode()
            self.assertIsNone(d.parse_startup_line(line))

    def test_marker_receipt_timing_and_wall_anchor(self):
        valid, _ = self.capture(b"rst:0x1,boot:0x8\nI (2) ot_bench: companion runtime started\n")
        self.assertEqual(valid["schema"], "OT225-STARTUP-1")
        self.assertEqual(valid["capture_started_unix_ms"], 1789350000250)
        d.validate_startup_result(valid)
        for changes in ({"received_ms": True}, {"received_ms": -1}, {"received_ms": 240001}, {"value": True}):
            invalid = copy.deepcopy(valid)
            invalid["markers"][0].update(changes)
            with self.assertRaises(ValueError):
                d.validate_startup_result(invalid)
        invalid = copy.deepcopy(valid)
        invalid["markers"][0]["received_ms"] = 2
        invalid["markers"][1]["received_ms"] = 1
        with self.assertRaises(ValueError):
            d.validate_startup_result(invalid)
        for bad in (True, -1, 253402300800000, "secret"):
            invalid = copy.deepcopy(valid)
            invalid["capture_started_unix_ms"] = bad
            with self.assertRaises(ValueError):
                d.validate_startup_result(invalid)

    def test_sdk_crash_full_collector_fragmented_redacted(self):
        # panic.c + panic_arch.c + debug_helpers.c exact emitted composition.
        register_row = lambda names, values: "".join(f"{name:<8}: 0x{value:08x}  " for name, value in zip(names, values))
        lines = ["Guru Meditation Error: Core " + f"{0:2d}" + " panic'ed (Unhandled debug exception). ",
                 "Debug exception reason: Stack canary watchpoint triggered (ot_ble_host) ",
                 register_row(("PC", "PS", "A0", "A1"), (0x42000100, 0xdeadbeef, 0xdeadbeef, 0x3fc90000)),
                 register_row(("A14", "A15", "SAR", "EXCCAUSE"), (0xdeadbeef, 0xdeadbeef, 3, 28)),
                 "Backtrace:" + "".join(f" 0x{pc:08x}:0x3fc90000" for pc in (0x42000100, 0x40374010, 0x40000100)) + " |<-CORRUPTED"]
        raw = ("\x1b[0;31m" + "\r\n".join(lines) + "\x1b[0m\r\n").encode()
        result, _ = self.capture(raw, chunk_size=7)
        self.assertEqual([m["category"] for m in result["markers"]],
                         ["panic", "stack_canary", "program_counter", "exception_cause", "backtrace"])
        self.assertEqual(result["markers"][0]["reason"], "debug_exception")
        self.assertEqual(result["markers"][1]["task"], "ot_ble_host")
        self.assertEqual(result["markers"][-1]["pcs"], [0x42000100, 0x40374010, 0x40000100])
        self.assertEqual(result["markers"][-1]["termination"], "corrupted")
        self.assertNotIn(str(0xdeadbeef), str(result))
        self.assertNotIn(str(0x3fc90000), str(result))
        d.validate_startup_result(result)

    def test_sdk_stack_overflow_and_watchdog_task(self):
        raw = (b"***ERROR*** A stack overflow in task ot_ble_host has been detected.\r\n"
               b"E (100) task_wdt:  - ot_ble_host (CPU 0)\r\n"
               b"E (100) task_wdt:  - private_name (CPU 1)\r\n")
        result, _ = self.capture(raw, chunk_size=9)
        self.assertEqual([m["category"] for m in result["markers"]], ["stack_overflow", "watchdog_task"])
        self.assertTrue(all(m["task"] == "ot_ble_host" for m in result["markers"]))
        self.assertNotIn("private_name", str(result))
        d.validate_startup_result(result)

    def test_long_backtrace_retains_bounded_complete_pc_prefix(self):
        raw = ("Backtrace:" + "".join(f" 0x{0x42000100+i:08x}:0x3fc90000" for i in range(30)) + "\r\n").encode()
        result, _ = self.capture(raw, chunk_size=31)
        self.assertEqual(result["status"], "line_limit")
        trace = result["markers"][0]
        self.assertEqual(trace["termination"], "truncated")
        self.assertEqual(trace["pcs"], [0x42000100+i for i in range(20)])
        d.validate_startup_result(result)

    def test_crash_identity_and_address_validation(self):
        self.assertEqual(d.parse_startup_line(b"Debug exception reason: Stack canary watchpoint triggered (private_name) "), {"category": "stack_canary"})
        trace = d.parse_startup_line(b"Backtrace: 0x3fc90000:0xdeadbeef 0x42000100:0xdeadbeef")
        self.assertEqual(trace, {"category": "backtrace", "pcs": [0x42000100], "termination": "corrupted"})
        self.assertIsNone(d.parse_startup_line(b"Backtrace: 0x42000100:0x3fc90000 secret"))
        valid, _ = self.capture(b"I (1) ot_bench: companion runtime started\n")
        for marker in ({"category": "program_counter", "value": 0x3fc90000},
                       {"category": "program_counter", "value": True},
                       {"category": "stack_canary", "task": "private_name"},
                       {"category": "panic", "reason": "private_reason"},
                       {"category": "exception_cause", "value": 64},
                       {"category": "backtrace", "pcs": [True], "termination": "complete"},
                       {"category": "backtrace", "pcs": [0x42000100]*21, "termination": "complete"}):
            invalid = copy.deepcopy(valid)
            invalid["markers"] = [{**marker, "received_ms": 1}]
            with self.assertRaises(ValueError):
                d.validate_startup_result(invalid)

    def test_ble_host_stack_exact_producer_and_bounds(self):
        raw = b"\x1b[0;32mI (5123) ot_bench: ble_host_stack minimum_free_bytes=8192\x1b[0m\r\n"
        result, _ = self.capture(raw, chunk_size=7)
        self.assertEqual(len(result["markers"]), 1)
        self.assertEqual(result["markers"][0]["category"], "ble_host_stack")
        self.assertEqual(result["markers"][0]["value"], 8192)
        d.validate_startup_result(result)
        for bad in ("-1", "4294967296", "8192 secret"):
            self.assertIsNone(d.parse_startup_line(("I (1) ot_bench: ble_host_stack minimum_free_bytes=" + bad).encode()))
        for bad in (True, -1, 1 << 32):
            invalid = copy.deepcopy(result)
            invalid["markers"][0]["value"] = bad
            with self.assertRaises(ValueError):
                d.validate_startup_result(invalid)

    def test_worker_support_matches_imported_parser(self):
        ns = {}
        exec(d.WORKER_SUPPORT, ns)
        self.assertEqual(ns["parse_startup_line"](b"I (1) ot_bench: companion runtime FAIL"), {"category": "runtime_fail"})


if __name__ == "__main__":
    unittest.main()
