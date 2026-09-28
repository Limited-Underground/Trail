"""Bounded, sanitized OT-0101e connection observation; import opens no devices.

The caller must already own exclusive, authorized OT-DEV-002 trial custody,
with the candidate running and its ROM/restoration operator off the serial port.
CLI flags acknowledge that precondition; they do not issue hardware authority.
This collector never writes, resets, retries, or reopens a device. A caller-owned
watchdog must bound driver/enumerator calls that ignore their timeout.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

from ble_startup_diagnostics import parse_startup_line, validate_startup_result
from gnss_counter_capture import bound_identity, normalized_identity


SCHEMA = "OT0101E-CONNECTION-CAPTURE-1"
ARMED_SIGNAL = "CONNECTION_CAPTURE_ARMED"
MAX_SECONDS = 600
MAX_BYTES = 1_048_576
MAX_RECORDS = 4096
MAX_LINE_BYTES = 512
U32_MAX = (1 << 32) - 1
PREFIX = r"I \(([0-9]{1,10})\) ot_bench: conn_diag "
BOOT = re.compile(PREFIX + r"boot reset=([0-9]{1,10}) sensor_display=([01])")
EVENT = re.compile(PREFIX + r"event k=([0-9]{1,2}) t=([0-9]{1,10}) "
                   r"a=([0-9]{1,10}) b=([0-9]{1,10})")
SAMPLE = re.compile(PREFIX + r"sample t=([0-9]{1,10}) host_valid=([01]) "
                    r"host_free=([0-9]{1,10}) calls=([0-9]{1,10}) "
                    r"renders=([0-9]{1,10}) failures=([0-9]{1,10}) "
                    r"max_us=([0-9]{1,10}) dropped=([0-9]{1,10})")
STARTUP_CATEGORIES = frozenset((
    "self_check_pass", "self_check_fail", "runtime_fail", "runtime_started",
    "runtime_start_error", "security_stage", "security_detail", "heartbeat",
    "app_stack", "ble_host_stack", "panic", "interrupt_watchdog",
    "task_watchdog", "stack_canary", "stack_overflow", "watchdog_task",
    "reset_reason",
))
STOP_REASONS = frozenset((
    "end_of_input", "window_complete", "byte_limit", "record_limit",
    "deadline_overrun", "identity_invalid", "enumeration_error",
    "device_not_observed", "identity_ambiguous", "route_invalid",
    "route_ambiguous", "open_error", "read_error", "close_error",
    "clock_invalid", "cancelled", "readiness_error", "operator_stop", "cancellation_error",
))


def parse_line(raw):
    """Return only known scalar fields from one complete ASCII line.

    The shared startup parser supplies its fixed panic/reset vocabulary. Its
    old ELF address bounds are irrelevant here: no PC, registers or backtrace
    record is admitted, and arbitrary task names or panic text never escape.
    """
    if type(raw) is not bytes or len(raw) > MAX_LINE_BYTES:
        return None
    if raw.endswith(b"\r"):
        raw = raw[:-1]
    if any(value < 32 or value > 126 for value in raw):
        return None
    line = raw.decode("ascii")
    for pattern, category, fields in (
        (BOOT, "boot", ("log_ms", "reset", "sensor_display")),
        (EVENT, "event", ("log_ms", "kind", "uptime_ms", "a", "b")),
        (SAMPLE, "sample", ("log_ms", "uptime_ms", "host_valid", "host_free",
                            "calls", "renders", "failures", "max_us", "dropped")),
    ):
        match = pattern.fullmatch(line)
        if match is None:
            continue
        values = tuple(int(value) for value in match.groups())
        if any(value > U32_MAX for value in values):
            return None
        record = dict(zip(fields, values))
        if category == "event" and not 1 <= record["kind"] <= 13:
            return None
        return {"category": category, **record}
    marker = parse_startup_line(raw)
    if marker is None or marker["category"] not in STARTUP_CATEGORIES:
        return None
    return marker


class DiagnosticCapture:
    """Incremental feed parser; empty reads preserve fragments across timeouts.

    Receipt times are host elapsed milliseconds, not device event timestamps.
    An overlong line is discarded through its newline, including any suffix
    resembling a valid record. Partial final lines are never accepted.
    """

    def __init__(self, duration_seconds=MAX_SECONDS, *, max_bytes=MAX_BYTES,
                 max_records=MAX_RECORDS):
        if (type(duration_seconds) is not int or not 1 <= duration_seconds <= MAX_SECONDS
                or type(max_bytes) is not int or not 1 <= max_bytes <= MAX_BYTES
                or type(max_records) is not int or not 1 <= max_records <= MAX_RECORDS):
            raise ValueError("connection_capture_limits_invalid")
        self.duration_ms = duration_seconds * 1000
        self.max_bytes = max_bytes
        self.max_records = max_records
        self.records = []
        self.bytes_read = 0
        self.ignored_lines = 0
        self.overlong_lines = 0
        self.late_bytes_discarded = 0
        self.stop_reason = None
        self._pending = bytearray()
        self._discarding = False
        self._partial_tail = False
        self._received_ms = 0
        self._closed = False

    def feed(self, raw, received_ms):
        if (type(raw) is not bytes or type(received_ms) is not int
                or not 0 <= received_ms <= U32_MAX):
            raise ValueError("connection_capture_input_invalid")
        if self._closed or self.stop_reason is not None:
            return False
        if received_ms < self._received_ms:
            self.stop_reason = "clock_invalid"
            return False
        self._received_ms = received_ms
        if received_ms >= self.duration_ms:
            self.late_bytes_discarded += len(raw)
            self.stop_reason = ("deadline_overrun" if raw or received_ms > self.duration_ms
                                else "window_complete")
            return False
        remaining = self.max_bytes - self.bytes_read
        admitted = raw[:remaining]
        self.bytes_read += len(admitted)
        for value in admitted:
            if value == 10:
                if self._discarding:
                    self._discarding = False
                else:
                    record = parse_line(bytes(self._pending))
                    if record is None:
                        self.ignored_lines += 1
                    else:
                        self.records.append({**record, "received_ms": received_ms})
                        if len(self.records) >= self.max_records:
                            self._pending.clear()
                            self.stop_reason = "record_limit"
                            return False
                self._pending.clear()
            elif not self._discarding:
                if len(self._pending) < MAX_LINE_BYTES:
                    self._pending.append(value)
                else:
                    self._pending.clear()
                    self._discarding = True
                    self.overlong_lines += 1
        if self.bytes_read >= self.max_bytes:
            self.stop_reason = "byte_limit"
            return False
        return True

    def finish(self, received_ms=None, reason="end_of_input"):
        if reason not in STOP_REASONS:
            raise ValueError("connection_capture_stop_invalid")
        if received_ms is not None:
            if type(received_ms) is not int or not 0 <= received_ms <= U32_MAX:
                raise ValueError("connection_capture_time_invalid")
            if received_ms < self._received_ms:
                self.stop_reason = "clock_invalid"
            else:
                self._received_ms = received_ms
        self.stop_reason = self.stop_reason or reason
        categories = {record["category"] for record in self.records}
        partial = self._partial_tail or bool(self._pending) or self._discarding
        self._partial_tail = partial
        self._pending.clear()
        self._discarding = False
        self._closed = True
        return {
            "schema": SCHEMA,
            "status": "observed" if self.records else "unobserved",
            "stop_reason": self.stop_reason,
            "elapsed_ms": self._received_ms,
            "requested_duration_ms": self.duration_ms,
            "bytes_read": self.bytes_read,
            "ignored_lines": self.ignored_lines,
            "overlong_lines": self.overlong_lines,
            "partial_line_discarded": partial,
            "late_bytes_discarded": self.late_bytes_discarded,
            "capture_truncated": (partial or self.overlong_lines > 0 or
                                  self.stop_reason not in ("end_of_input", "window_complete")),
            "firmware_drops_observed": any(record["category"] == "sample" and
                                            record["dropped"] != 0 for record in self.records),
            "missing": {"boot": "boot" not in categories,
                        "sample": "sample" not in categories,
                        "callback": "event" not in categories},
            "early_output_may_be_missing": True,
            "main_task_drain_may_delay_or_lose_events": True,
            "no_callback_does_not_prove_request_loss": True,
            "records": [dict(record) for record in self.records],
        }


def validate_capture(value):
    """Revalidate the complete stored record vocabulary without admitting text."""
    def require(ok):
        if not ok:
            raise ValueError("connection_capture_result_invalid")
    require(type(value) is dict and set(value) == set(DiagnosticCapture(1).finish()))
    require(value["schema"] == SCHEMA and value["stop_reason"] in STOP_REASONS)
    for name in ("elapsed_ms", "bytes_read", "ignored_lines", "overlong_lines", "late_bytes_discarded"):
        require(type(value[name]) is int and 0 <= value[name] <= U32_MAX)
    require(value["bytes_read"] <= MAX_BYTES)
    require(type(value["requested_duration_ms"]) is int and
            1000 <= value["requested_duration_ms"] <= MAX_SECONDS * 1000 and
            value["requested_duration_ms"] % 1000 == 0)
    for name in ("partial_line_discarded", "capture_truncated", "firmware_drops_observed"):
        require(type(value[name]) is bool)
    for name in ("early_output_may_be_missing", "main_task_drain_may_delay_or_lose_events",
                 "no_callback_does_not_prove_request_loss"):
        require(value[name] is True)
    require(type(value["records"]) is list and len(value["records"]) <= MAX_RECORDS)
    previous = 0
    categories = set()
    fields = {
        "boot": {"log_ms", "reset", "sensor_display"},
        "event": {"log_ms", "kind", "uptime_ms", "a", "b"},
        "sample": {"log_ms", "uptime_ms", "host_valid", "host_free", "calls",
                   "renders", "failures", "max_us", "dropped"},
    }
    for record in value["records"]:
        require(type(record) is dict and type(record.get("category")) is str)
        require(type(record.get("received_ms")) is int and
                previous <= record["received_ms"] <= value["elapsed_ms"])
        previous = record["received_ms"]
        category = record["category"]
        categories.add(category)
        if category in fields:
            require(set(record) == fields[category] | {"category", "received_ms"})
            require(all(type(record[key]) is int and 0 <= record[key] <= U32_MAX
                        for key in fields[category]))
            if category == "event":
                require(1 <= record["kind"] <= 13)
            if category == "boot":
                require(record["sensor_display"] in (0, 1))
            if category == "sample":
                require(record["host_valid"] in (0, 1))
        else:
            require(category in STARTUP_CATEGORIES)
            # Reuse the fixed panic/task vocabulary; no ELF address fields are
            # allowed. Receipt timing is checked above rather than rebased.
            validate_startup_result({"schema": "OT225-STARTUP-1", "status": "window_complete",
                "markers": [{**record, "received_ms": 0}], "early_output_may_be_missing": True,
                "bytes_read": 0, "elapsed_ms": 0, "capture_started_unix_ms": 0})
    require(value["status"] == ("observed" if value["records"] else "unobserved"))
    require(type(value["missing"]) is dict and set(value["missing"]) == {"boot", "sample", "callback"})
    require(all(type(flag) is bool for flag in value["missing"].values()))
    require(value["missing"] == {"boot": "boot" not in categories, "sample": "sample" not in categories,
                                 "callback": "event" not in categories})
    return value


def capture(registry, inventory_id, duration_seconds, *, custody_confirmed=False,
            serial_module=None, comports=None, monotonic=time.monotonic,
            on_armed=None, stop_requested=None):
    """Single passive handle under existing custody; no retry or port writes.

    on_armed receives no arguments and runs once after an identity-bound open,
    before reading. Its execution consumes the same total duration budget.
    stop_requested is a nonblocking Boolean callback checked between reads.
    """
    if custody_confirmed is not True or inventory_id != "OT-DEV-002":
        raise ValueError("connection_capture_custody_required")
    parser = DiagnosticCapture(duration_seconds)
    started = monotonic()
    port = None
    stop_reason = "window_complete"

    def elapsed():
        return max(0, int((monotonic() - started) * 1000))

    def stopping():
        nonlocal stop_reason
        if stop_requested is None:
            return False
        try:
            value = stop_requested()
            if type(value) is not bool:
                raise ValueError()
        except Exception:
            stop_reason = "cancellation_error"
            return True
        if value:
            stop_reason = "operator_stop"
        return value

    try:
        try:
            expected = bound_identity(registry, inventory_id)
        except Exception:
            stop_reason = "identity_invalid"
            return parser.finish(elapsed(), stop_reason)
        if serial_module is None or comports is None:
            import serial
            from serial.tools.list_ports import comports as list_ports
            serial_module = serial if serial_module is None else serial_module
            comports = list_ports if comports is None else comports
        try:
            ports = list(comports())
        except Exception:
            stop_reason = "enumeration_error"
            return parser.finish(elapsed(), stop_reason)
        matches = [item for item in ports if getattr(item, "vid", None) == 0x303A
                   and getattr(item, "pid", None) == 0x1001
                   and normalized_identity(getattr(item, "serial_number", None)) == expected]
        if len(matches) != 1:
            stop_reason = "identity_ambiguous" if matches else "device_not_observed"
            return parser.finish(elapsed(), stop_reason)
        route = getattr(matches[0], "device", None)
        if type(route) is not str or re.fullmatch(r"COM[1-9][0-9]{0,3}", route) is None:
            stop_reason = "route_invalid"
            return parser.finish(elapsed(), stop_reason)
        if sum(getattr(item, "device", None) == route for item in ports) != 1:
            stop_reason = "route_ambiguous"
            return parser.finish(elapsed(), stop_reason)
        if elapsed() >= parser.duration_ms:
            return parser.finish(elapsed(), "deadline_overrun")
        if stopping():
            return parser.finish(elapsed(), stop_reason)
        try:
            port = serial_module.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=0)
            port.dtr = False
            port.rts = False
            port.port = route
            if elapsed() >= parser.duration_ms:
                stop_reason = "deadline_overrun"
            else:
                port.open()
                if elapsed() >= parser.duration_ms:
                    stop_reason = "deadline_overrun"
                elif stopping():
                    pass
                elif on_armed is not None:
                    try:
                        on_armed()
                    except Exception:
                        stop_reason = "readiness_error"
                    if stop_reason == "window_complete" and elapsed() >= parser.duration_ms:
                        stop_reason = "deadline_overrun"
                while stop_reason == "window_complete" and elapsed() < parser.duration_ms and not stopping():
                    port.timeout = min(0.25, (parser.duration_ms - elapsed()) / 1000)
                    if port.timeout <= 0:
                        break
                    try:
                        raw = port.read(min(256, parser.max_bytes - parser.bytes_read))
                    except Exception:
                        stop_reason = "read_error"
                        break
                    if type(raw) is not bytes:
                        stop_reason = "read_error"
                        break
                    if not parser.feed(raw, elapsed()):
                        break
        except Exception:
            stop_reason = "open_error"
    except KeyboardInterrupt:
        stop_reason = "cancelled"
    finally:
        if port is not None:
            try:
                port.close()
            except Exception:
                # Closure failure remains visible even if a capture limit fired.
                parser.stop_reason = "close_error"
    return parser.finish(elapsed(), stop_reason)


def main(argv=None, *, readiness_output=None):
    args_parser = argparse.ArgumentParser(description=__doc__)
    args_parser.add_argument("--capture", action="store_true", required=True)
    args_parser.add_argument("--custody-confirmed", action="store_true", required=True)
    args_parser.add_argument("--registry", type=Path, required=True)
    args_parser.add_argument("--inventory-id", choices=("OT-DEV-002",), required=True)
    args_parser.add_argument("--duration", type=int, required=True)
    args = args_parser.parse_args(argv)

    def signal_armed():
        print(ARMED_SIGNAL, file=sys.stderr if readiness_output is None else readiness_output,
              flush=True)

    try:
        result = capture(args.registry, args.inventory_id, args.duration,
                         custody_confirmed=args.custody_confirmed, on_armed=signal_armed)
    except Exception:
        print(json.dumps({"schema": SCHEMA, "status": "unobserved", "stop_reason": "capture_refused"}))
        return 1
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["stop_reason"] == "window_complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
