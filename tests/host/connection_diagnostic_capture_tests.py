"""Host-only framing, privacy and bounded fake-serial observation tests."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import connection_diagnostic_capture as collector


BOOT = b"I (100) ot_bench: conn_diag boot reset=1 sensor_display=1"
EVENT = b"I (200) ot_bench: conn_diag event k=1 t=195 a=0 b=0"
SAMPLE = (b"I (300) ot_bench: conn_diag sample t=299 host_valid=1 host_free=2048 "
          b"calls=4 renders=3 failures=0 max_us=2100 dropped=0")


class ParserTests(unittest.TestCase):
    def test_current_firmware_format_strings_feed_the_collector(self):
        source = (ROOT / "firmware/targets/heltec_v4_bench/main/app_main.cpp").read_text(encoding="utf-8")
        # Use the actual producer's format strings, not a second fixture format.
        parser = collector.DiagnosticCapture(10)
        for category, values in (("boot", (1, 0)), ("event", (13, 123, 7, 0)),
                                 ("sample", (124, 1, 2000, 7, 4, 0, 10000, 2))):
            match = re.search(r'"(conn_diag ' + category + r' [^"\n]+)"', source)
            self.assertIsNotNone(match)
            line = ("I (150) ot_bench: " + match[1] % values + "\n").encode("ascii")
            for offset in range(0, len(line), 7):
                parser.feed(line[offset:offset + 7], 200)
        result = parser.finish(201)
        self.assertEqual([row["category"] for row in result["records"]], ["boot", "event", "sample"])
        self.assertEqual(result["records"][1]["kind"], 13)
        self.assertEqual(result["records"][1]["a"], 7)
        self.assertEqual(result["records"][2]["dropped"], 2)
        self.assertEqual(collector.validate_capture(result), result)

    def test_stored_capture_revalidates_fixed_vocabulary_and_privacy(self):
        parser = collector.DiagnosticCapture(600)
        parser.feed(BOOT + b"\n" + SAMPLE + b"\n" +
                    b"Debug exception reason: Stack canary watchpoint triggered (nimble_host)\n" +
                    b"rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\n", 450000)
        value = parser.finish(600000, "window_complete")
        self.assertEqual(collector.validate_capture(json.loads(json.dumps(value))), value)
        for mutation in (lambda obj: obj.update(raw="private-secret"),
                         lambda obj: obj["records"][0].update(reset=True),
                         lambda obj: obj["records"][2].update(task="private-secret"),
                         lambda obj: obj["records"][0].update(received_ms=600001),
                         lambda obj: obj.update(missing={"boot": "private-secret"})):
            changed = json.loads(json.dumps(value))
            mutation(changed)
            with self.assertRaises(ValueError):
                collector.validate_capture(changed)

    def test_fragmented_and_coalesced_lines_preserve_order_and_receipt_time(self):
        parser = collector.DiagnosticCapture(10)
        parser.feed(BOOT[:11], 10)
        parser.feed(b"", 200)
        parser.feed(BOOT[11:] + b"\r\n" + EVENT + b"\n" + SAMPLE[:40], 201)
        parser.feed(SAMPLE[40:] + b"\n", 202)
        result = parser.finish(203)
        self.assertEqual([row["category"] for row in result["records"]], ["boot", "event", "sample"])
        self.assertEqual([row["received_ms"] for row in result["records"]], [201, 201, 202])
        self.assertEqual(result["records"][1]["uptime_ms"], 195)
        self.assertEqual(result["missing"], {"boot": False, "sample": False, "callback": False})

    def test_every_event_kind_and_u32_boundary(self):
        for kind in range(1, 14):
            line = (f"I (4294967295) ot_bench: conn_diag event k={kind} "
                    "t=4294967295 a=4294967295 b=4294967295").encode()
            self.assertEqual(collector.parse_line(line)["kind"], kind)
        for bad in (b"0", b"14", b"-1", b"01secret"):
            self.assertIsNone(collector.parse_line(EVENT.replace(b"k=1", b"k=" + bad)))
        self.assertIsNone(collector.parse_line(EVENT.replace(b"t=195", b"t=4294967296")))
        self.assertIsNone(collector.parse_line(BOOT.replace(b"reset=1", b"reset=4294967296")))
        self.assertIsNone(collector.parse_line(SAMPLE.replace(b"host_valid=1", b"host_valid=2")))

    def test_full_line_allowlist_rejects_secrets_non_ascii_and_controls(self):
        for bad in (EVENT + b" secret", b"secret " + EVENT, EVENT + b"\x00",
                    EVENT.replace(b"t=195", b"t=\xff195"), EVENT.replace(b" a=", b"\ta="),
                    BOOT.replace(b"sensor_display=1", b"sensor_display=2"),
                    EVENT.replace(b"ot_bench", b"other_tag"), b"PIN=123456", b"$GPGGA,secret"):
            self.assertIsNone(collector.parse_line(bad))
        self.assertIsNone(collector.parse_line(EVENT + b"\r\r"))

    def test_whole_overlong_line_is_discarded_until_newline(self):
        parser = collector.DiagnosticCapture(10)
        parser.feed(b"x" * (collector.MAX_LINE_BYTES + 1), 1)
        parser.feed(b"", 2)
        parser.feed(EVENT + b"\n" + BOOT + b"\n", 3)
        result = parser.finish(4)
        self.assertEqual([row["category"] for row in result["records"]], ["boot"])
        self.assertEqual(result["overlong_lines"], 1)
        self.assertTrue(result["capture_truncated"])
        self.assertFalse(result["partial_line_discarded"])

    def test_partial_tail_is_never_treated_as_a_complete_record(self):
        parser = collector.DiagnosticCapture(10)
        parser.feed(BOOT + b"\n" + EVENT, 1)
        result = parser.finish(2)
        self.assertEqual(len(result["records"]), 1)
        self.assertTrue(result["partial_line_discarded"])
        self.assertTrue(result["missing"]["callback"])
        self.assertEqual(parser._pending, bytearray())
        self.assertTrue(parser.finish()["partial_line_discarded"])

    def test_only_fixed_panic_reset_vocabulary_is_retained(self):
        raw = (b"Guru Meditation Error: Core  0 panic'ed (LoadProhibited). private-secret\n"
               b"***ERROR*** A stack overflow in task ot_ble_host has been detected.\n"
               b"Debug exception reason: Stack canary watchpoint triggered (private-task)\n"
               b"rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\n"
               b"Backtrace: 0x42000100:0x3fc91234\n"
               b"PC      : 0x42000100  PS      : 0x00000000  A0      : 0x42000200  A1      : 0x3fc91234  \n")
        parser = collector.DiagnosticCapture(10)
        parser.feed(raw, 1)
        result = parser.finish(2)
        self.assertEqual([row["category"] for row in result["records"]],
                         ["panic", "stack_overflow", "stack_canary", "reset_reason"])
        encoded = json.dumps(result)
        for value in ("private-secret", "private-task", "42000100", "3fc91234", "pcs", "program_counter"):
            self.assertNotIn(value, encoded)
        self.assertEqual(result["records"][1]["task"], "ot_ble_host")
        self.assertNotIn("task", result["records"][2])

    def test_reset_and_uptime_history_are_not_collapsed(self):
        parser = collector.DiagnosticCapture(10)
        parser.feed(EVENT + b"\n" + BOOT + b"\n" + EVENT.replace(b"t=195", b"t=1") + b"\n", 4)
        result = parser.finish(5)
        self.assertEqual(len(result["records"]), 3)
        self.assertEqual([row.get("uptime_ms") for row in result["records"]], [195, None, 1])

    def test_host_reset_reason_is_scalar_and_does_not_claim_device_reboot(self):
        parser = collector.DiagnosticCapture(10)
        host_reset = b"I (201) ot_bench: conn_diag event k=13 t=200 a=4294967295 b=0"
        parser.feed(EVENT + b"\n" + host_reset[:35], 1)
        parser.feed(b"", 2)
        parser.feed(host_reset[35:] + b"\n" + host_reset + b" secret\n", 3)
        result = parser.finish(4)
        self.assertEqual([row["kind"] for row in result["records"]], [1, 13])
        self.assertEqual(result["records"][1]["a"], collector.U32_MAX)
        self.assertEqual(result["records"][1]["received_ms"], 3)
        self.assertTrue(result["missing"]["boot"])
        self.assertTrue(result["missing"]["sample"])
        self.assertNotIn("secret", json.dumps(result))

    def test_missing_callbacks_and_firmware_overrun_are_explicit_not_diagnoses(self):
        parser = collector.DiagnosticCapture(10)
        parser.feed(SAMPLE.replace(b"dropped=0", b"dropped=5") + b"\n", 1)
        result = parser.finish(2)
        self.assertTrue(result["firmware_drops_observed"])
        self.assertTrue(result["missing"]["callback"])
        self.assertTrue(result["no_callback_does_not_prove_request_loss"])
        self.assertTrue(result["main_task_drain_may_delay_or_lose_events"])

    def test_record_and_byte_limits_preserve_earliest_history(self):
        parser = collector.DiagnosticCapture(10, max_records=2)
        self.assertFalse(parser.feed(BOOT + b"\n" + EVENT + b"\n" + SAMPLE + b"\n", 1))
        result = parser.finish(2)
        self.assertEqual(result["stop_reason"], "record_limit")
        self.assertEqual(len(result["records"]), 2)
        self.assertTrue(result["capture_truncated"])
        parser = collector.DiagnosticCapture(10, max_bytes=len(BOOT) + 2)
        self.assertFalse(parser.feed(BOOT + b"\n" + EVENT + b"\n", 1))
        result = parser.finish(2)
        self.assertEqual(result["stop_reason"], "byte_limit")
        self.assertEqual(result["bytes_read"], len(BOOT) + 2)
        self.assertTrue(result["partial_line_discarded"])

    def test_deadline_and_clock_regression_never_admit_late_records(self):
        parser = collector.DiagnosticCapture(1)
        parser.feed(BOOT + b"\n", 1)
        self.assertFalse(parser.feed(EVENT + b"\n", 1001))
        result = parser.finish(1001)
        self.assertEqual(result["stop_reason"], "deadline_overrun")
        self.assertEqual(len(result["records"]), 1)
        self.assertGreater(result["late_bytes_discarded"], 0)
        parser = collector.DiagnosticCapture(1)
        parser.feed(b"", 10)
        self.assertFalse(parser.feed(EVENT + b"\n", 9))
        self.assertEqual(parser.finish()["stop_reason"], "clock_invalid")

    def test_limits_types_and_empty_capture(self):
        for duration in (0, 601, True, 1.5):
            with self.assertRaises(ValueError):
                collector.DiagnosticCapture(duration)
        for kwargs in ({"max_bytes": collector.MAX_BYTES + 1}, {"max_records": 0}):
            with self.assertRaises(ValueError):
                collector.DiagnosticCapture(1, **kwargs)
        parser = collector.DiagnosticCapture(1)
        with self.assertRaises(ValueError):
            parser.feed(bytearray(b"secret"), 1)
        parser.feed(b"", 1000)
        result = parser.finish(1000)
        self.assertEqual(result["status"], "unobserved")
        self.assertTrue(all(result["missing"].values()))
        self.assertEqual(result["stop_reason"], "window_complete")
        maximum = collector.DiagnosticCapture(600)
        self.assertFalse(maximum.feed(EVENT + b"\n", 600_000))
        self.assertEqual(maximum.finish()["records"], [])


class SerialTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.registry = Path(self.temp.name) / "registry.json"
        self.registry.write_text(json.dumps({"devices": [
            {"inventory_id": "OT-DEV-002", "esp32s3_base_mac": "001122334455"}]}))
        self.clock = 0.0
        self.calls = []
        self.chunks = [BOOT[:10], b"", BOOT[10:] + b"\n" + EVENT + b"\n", SAMPLE + b"\n"]
        self.read_delay = 0.1
        self.read_error = False
        self.open_error = False
        self.close_error = False
        self.open_delay = 0
        self.ports = [SimpleNamespace(vid=0x303A, pid=0x1001, serial_number="00:11:22:33:44:55", device="COM42")]
        owner = self

        class FakeSerial:
            def __init__(self, **kwargs):
                owner.calls.append(("construct", kwargs))
                self.dtr = True
                self.rts = True
                self.port = None
                self.timeout = kwargs["timeout"]

            def open(self):
                owner.calls.append(("open", self.dtr, self.rts, self.port))
                owner.clock += owner.open_delay
                if owner.open_error:
                    raise OSError("private-open-details")

            def read(self, count):
                owner.calls.append(("read", count, self.timeout))
                owner.clock += owner.read_delay
                if owner.read_error:
                    raise OSError("private-read-details")
                return owner.chunks.pop(0) if owner.chunks else b""

            def write(self, _data):
                raise AssertionError("collector must never write")

            def close(self):
                owner.calls.append(("close",))
                if owner.close_error:
                    raise OSError("private-close-details")

        self.serial = SimpleNamespace(Serial=FakeSerial)

    def capture(self, **kwargs):
        return collector.capture(self.registry, "OT-DEV-002", 1, custody_confirmed=True,
                                 serial_module=self.serial, comports=lambda: self.ports,
                                 monotonic=lambda: self.clock, **kwargs)

    def test_single_passive_handle_and_fragment_history(self):
        result = self.capture()
        self.assertEqual([row["category"] for row in result["records"]], ["boot", "event", "sample"])
        self.assertEqual([call for call in self.calls if call[0] == "open"], [("open", False, False, "COM42")])
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)
        self.assertNotIn("COM42", json.dumps(result))
        self.assertNotIn("001122334455", json.dumps(result))

    def test_armed_callback_runs_once_after_open_before_read(self):
        def armed():
            self.assertEqual(self.calls[-1], ("open", False, False, "COM42"))
            self.assertFalse(any(call[0] in ("read", "close") for call in self.calls))
            self.calls.append(("armed",))
        result = self.capture(on_armed=armed)
        self.assertEqual(sum(call[0] == "armed" for call in self.calls), 1)
        kinds = [call[0] for call in self.calls]
        self.assertLess(kinds.index("armed"), kinds.index("read"))
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)
        self.assertEqual(len(result["records"]), 3)

    def test_failed_or_late_open_and_missing_identity_never_arm(self):
        armed = mock.Mock()
        self.open_error = True
        self.assertEqual(self.capture(on_armed=armed)["stop_reason"], "open_error")
        self.open_error = False
        self.open_delay = 2
        self.assertEqual(self.capture(on_armed=armed)["stop_reason"], "deadline_overrun")
        self.clock = 0
        self.ports = []
        self.assertEqual(self.capture(on_armed=armed)["stop_reason"], "device_not_observed")
        armed.assert_not_called()
        self.assertFalse(any(call[0] == "read" for call in self.calls))

    def test_armed_callback_failure_is_typed_and_closes_without_reading(self):
        def armed():
            raise OSError("private-readiness-details")
        result = self.capture(on_armed=armed)
        self.assertEqual(result["stop_reason"], "readiness_error")
        self.assertTrue(result["capture_truncated"])
        self.assertEqual(result["records"], [])
        self.assertFalse(any(call[0] == "read" for call in self.calls))
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)
        self.assertNotIn("private-", json.dumps(result))

    def test_operator_stop_before_open_does_not_arm_or_construct(self):
        armed = mock.Mock()
        result = self.capture(on_armed=armed, stop_requested=lambda: True)
        self.assertEqual(result["stop_reason"], "operator_stop")
        self.assertEqual(self.calls, [])
        armed.assert_not_called()

    def test_operator_stop_keeps_partial_line_explicit_and_closes_once(self):
        def stop():
            return any(call[0] == "read" for call in self.calls)
        result = self.capture(stop_requested=stop)
        self.assertEqual(result["stop_reason"], "operator_stop")
        self.assertTrue(result["partial_line_discarded"])
        self.assertEqual(result["records"], [])
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_cancellation_callback_error_is_typed_without_exception_details(self):
        def stop():
            if any(call[0] == "open" for call in self.calls):
                raise OSError("private cancellation details")
            return False
        result = self.capture(stop_requested=stop)
        self.assertEqual(result["stop_reason"], "cancellation_error")
        self.assertNotIn("private", json.dumps(result))
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_armed_callback_time_is_inside_the_capture_deadline(self):
        for callback_duration in (1.0, 2.0):
            with self.subTest(callback_duration=callback_duration):
                self.clock = 0
                self.calls.clear()
                def armed():
                    self.clock += callback_duration
                result = self.capture(on_armed=armed)
                self.assertEqual(result["stop_reason"], "deadline_overrun")
                self.assertGreaterEqual(result["elapsed_ms"], result["requested_duration_ms"])
                self.assertFalse(any(call[0] == "read" for call in self.calls))
                self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_missing_or_ambiguous_identity_never_opens(self):
        self.ports = []
        self.assertEqual(self.capture()["stop_reason"], "device_not_observed")
        self.assertEqual(self.calls, [])
        peer = SimpleNamespace(vid=0x303A, pid=0x1001, serial_number="001122334455", device="COM42")
        self.ports = [peer, peer]
        self.assertEqual(self.capture()["stop_reason"], "identity_ambiguous")
        self.assertEqual(self.calls, [])

    def test_alias_and_invalid_route_never_open(self):
        self.ports.append(SimpleNamespace(vid=1, pid=2, serial_number="other", device="COM42"))
        self.assertEqual(self.capture()["stop_reason"], "route_ambiguous")
        self.ports = self.ports[:1]
        self.ports[0].device = "COM0"
        self.assertEqual(self.capture()["stop_reason"], "route_invalid")
        self.assertEqual(self.calls, [])

    def test_late_read_is_discarded_and_handle_closed_once(self):
        self.read_delay = 2.0
        result = self.capture()
        self.assertEqual(result["stop_reason"], "deadline_overrun")
        self.assertEqual(result["records"], [])
        self.assertEqual(sum(call[0] == "open" for call in self.calls), 1)
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_slow_enumeration_and_open_do_not_start_reads_after_deadline(self):
        def slow_enumerate():
            self.clock = 2
            return self.ports
        result = collector.capture(self.registry, "OT-DEV-002", 1, custody_confirmed=True,
                                   serial_module=self.serial, comports=slow_enumerate,
                                   monotonic=lambda: self.clock)
        self.assertEqual(result["stop_reason"], "deadline_overrun")
        self.assertEqual(self.calls, [])
        self.clock = 0
        self.open_delay = 2
        result = self.capture()
        self.assertEqual(result["stop_reason"], "deadline_overrun")
        self.assertFalse(any(call[0] == "read" for call in self.calls))
        self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_unknown_serial_return_type_is_not_coerced_or_retained(self):
        self.chunks = ["private-secret"]
        result = self.capture()
        self.assertEqual(result["stop_reason"], "read_error")
        self.assertNotIn("private-secret", json.dumps(result))
        self.assertEqual(result["records"], [])

    def test_errors_are_fixed_and_do_not_retry_or_expose_exception_text(self):
        for field, reason in (("open_error", "open_error"), ("read_error", "read_error"), ("close_error", "close_error")):
            with self.subTest(field=field):
                self.clock = 0
                self.calls.clear()
                setattr(self, field, True)
                result = self.capture()
                setattr(self, field, False)
                self.assertEqual(result["stop_reason"], reason)
                self.assertNotIn("private-", json.dumps(result))
                self.assertEqual(sum(call[0] == "open" for call in self.calls), 1)
                self.assertEqual(sum(call[0] == "close" for call in self.calls), 1)

    def test_custody_wrong_device_and_duration_refuse_before_port_access(self):
        for device, custody, duration in (("OT-DEV-001", True, 1), ("OT-DEV-002", False, 1), ("OT-DEV-002", True, 601)):
            with self.assertRaises(ValueError):
                collector.capture(self.registry, device, duration, custody_confirmed=custody,
                                  serial_module=self.serial, comports=lambda: self.ports)
        self.assertEqual(self.calls, [])

    def test_cli_requires_explicit_capture_and_custody_flags(self):
        with mock.patch.object(collector, "capture") as capture, redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                collector.main(["--registry", str(self.registry), "--inventory-id", "OT-DEV-002", "--duration", "1"])
            capture.assert_not_called()
        with mock.patch.object(collector, "capture", side_effect=OSError("private-secret")), redirect_stdout(io.StringIO()) as output:
            result = collector.main(["--capture", "--custody-confirmed", "--registry", str(self.registry),
                                     "--inventory-id", "OT-DEV-002", "--duration", "1"])
            self.assertEqual(result, 1)
            self.assertNotIn("private-secret", output.getvalue())

    def test_cli_flushes_fixed_armed_signal_separately_from_final_json(self):
        class ReadinessOutput(io.StringIO):
            flush_count = 0
            def flush(self):
                self.flush_count += 1
                return super().flush()
        readiness = ReadinessOutput()
        def captured(*args, **kwargs):
            kwargs["on_armed"]()
            self.assertEqual(readiness.getvalue(), "CONNECTION_CAPTURE_ARMED\n")
            self.assertEqual(readiness.flush_count, 1)
            return {"schema": collector.SCHEMA, "stop_reason": "window_complete"}
        with mock.patch.object(collector, "capture", side_effect=captured), redirect_stdout(io.StringIO()) as output:
            result = collector.main(["--capture", "--custody-confirmed", "--registry", str(self.registry),
                                     "--inventory-id", "OT-DEV-002", "--duration", "1"],
                                    readiness_output=readiness)
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(output.getvalue())["stop_reason"], "window_complete")
        self.assertNotIn(collector.ARMED_SIGNAL, output.getvalue())
        self.assertEqual(readiness.getvalue(), "CONNECTION_CAPTURE_ARMED\n")


if __name__ == "__main__":
    unittest.main()
