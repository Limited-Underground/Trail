"""Bounded passive startup diagnostics; import is inert and opens no ports.

WORKER_SUPPORT is executed inside the already admitted isolated worker.
"""

WORKER_SUPPORT = r'''
import re
import time

STARTUP_SECONDS = 15.0
STARTUP_BYTES = 16384
STARTUP_LINE_BYTES = 512
STARTUP_MARKERS = 128
# Exact OT225 ELF executable sections plus ESP32-S3 ROM; never data/stack RAM.
CODE_RANGES = ((0x40000000, 0x40060000), (0x40374000, 0x40387dbf),
               (0x42000020, 0x42079bac))
TASKS = frozenset(("ot_ble_host", "main", "IDLE0", "IDLE1"))
PANIC_REASONS = {"IllegalInstruction": "illegal_instruction", "LoadProhibited": "load_prohibited",
    "StoreProhibited": "store_prohibited", "InstrFetchProhibited": "instruction_fetch_prohibited",
    "LoadStoreAlignment": "load_store_alignment", "IntegerDivideByZero": "divide_by_zero",
    "Unhandled debug exception": "debug_exception", "Cache error": "cache_error",
    "Memory protection fault": "memory_protection_fault", "Unknown": "unknown"}


def code_address(value):
    return type(value) is int and any(low <= value < high for low, high in CODE_RANGES)


def parse_startup_line(raw, truncated=False):
    if type(raw) is not bytes or len(raw) > STARTUP_LINE_BYTES:
        return None
    try:
        line = raw.decode("ascii").rstrip("\r")
    except UnicodeDecodeError:
        return None
    line = re.sub(r"\x1b\[[0-9;]*m", "", line)
    match = re.fullmatch(r"[IWE] \([0-9]{1,10}\) ot_bench: (.+)", line)
    if match:
        body = match[1]
        literals = {"companion boot self-check PASS": "self_check_pass",
                    "companion boot self-check FAIL": "self_check_fail",
                    "companion runtime FAIL": "runtime_fail",
                    "companion runtime started": "runtime_started"}
        if body in literals:
            return {"category": literals[body]}
        fields = (("companion runtime start error=", "runtime_start_error", 255),
                  ("companion security stage=", "security_stage", 255),
                  ("companion security detail=", "security_detail", 255),
                  ("heartbeat elapsed_ms=", "heartbeat", (1 << 64) - 1),
                  ("app_stack minimum_free_bytes=", "app_stack", (1 << 32) - 1),
                  ("ble_host_stack minimum_free_bytes=", "ble_host_stack", (1 << 32) - 1))
        for prefix, category, maximum in fields:
            if body.startswith(prefix):
                value = body[len(prefix):]
                if re.fullmatch(r"[0-9]{1,20}", value) and int(value) <= maximum:
                    return {"category": category, "value": int(value)}
        return None
    # panic.c panic_print_dec pads single-digit cores, after an existing space.
    # Keep only fixed categories; accept the SDK two-space and compact forms.
    if re.fullmatch(r"Guru Meditation Error: Core {1,2}[01] panic'ed \(Interrupt wdt timeout on CPU[01]\)\. [^\r\n]*", line):
        return {"category": "interrupt_watchdog"}
    if re.fullmatch(r"Guru Meditation Error: Core {1,2}[01] panic'ed \([^\r\n]+\)\. [^\r\n]*", line):
        reason = re.search(r"panic'ed \(([^)]+)\)", line)[1]
        marker = {"category": "panic"}
        if reason in PANIC_REASONS:
            marker["reason"] = PANIC_REASONS[reason]
        return marker
    canary = re.fullmatch(r"Debug exception reason: Stack canary watchpoint triggered \(([^\r\n()]*)\) *", line)
    if canary:
        marker = {"category": "stack_canary"}
        if canary[1] in TASKS:
            marker["task"] = canary[1]
        return marker
    overflow = re.fullmatch(r"\*\*\*ERROR\*\*\* A stack overflow in task ([^\r\n]+) has been detected\.", line)
    if overflow:
        marker = {"category": "stack_overflow"}
        if overflow[1] in TASKS:
            marker["task"] = overflow[1]
        return marker
    watched = re.fullmatch(r"E \([0-9]{1,10}\) task_wdt:  - ([^\r\n]+) \(CPU (?:0|1|0/1)\)", line)
    if watched and watched[1] in TASKS:
        return {"category": "watchdog_task", "task": watched[1]}
    # Register rows use eight-column names and eight hex digits (panic_arch.c).
    register_names = (("PC", "PS", "A0", "A1"), ("A14", "A15", "SAR", "EXCCAUSE"))
    for names in register_names:
        pattern = "".join(re.escape(f"{name:<8}: 0x") + r"([0-9a-fA-F]{8})  " for name in names)
        registers = re.fullmatch(pattern, line)
        if registers:
            if names[0] == "PC":
                pc = int(registers[1], 16)
                return {"category": "program_counter", "value": pc} if code_address(pc) else None
            cause = int(registers[4], 16)
            return {"category": "exception_cause", "value": cause} if cause <= 63 else None
    if line.startswith("Backtrace:"):
        tail = line[len("Backtrace:"):]
        pairs = list(re.finditer(r" 0x([0-9a-fA-F]{8}):0x[0-9a-fA-F]{8}", tail))
        if not pairs or pairs[0].start() != 0:
            return None
        if any(a.end() != b.start() for a, b in zip(pairs, pairs[1:])):
            return None
        ending = tail[pairs[-1].end():]
        endings = {"": "complete", " |<-CORRUPTED": "corrupted", " |<-CONTINUES": "continues"}
        if truncated:
            # Accept only an unfinished next PC:SP token or SDK termination suffix.
            if ending and not re.fullmatch(r" (?:0x[0-9a-fA-F]{0,8}(?::0x[0-9a-fA-F]{0,8})?|\|<-[A-Z]*)?", ending):
                return None
            termination = "truncated"
        else:
            if ending not in endings:
                return None
            termination = endings[ending]
        if len(pairs) > 20:
            termination = "truncated"
        addresses = [int(pair[1], 16) for pair in pairs[:20]]
        pcs = [address for address in addresses if code_address(address)]
        if len(pcs) != len(addresses) and termination != "truncated":
            termination = "corrupted"
        return {"category": "backtrace", "pcs": pcs, "termination": termination}
    # task_wdt.c prints this exact caption; subsequent task names are ignored.
    if re.fullmatch(r"E \([0-9]{1,10}\) task_wdt: Task watchdog got triggered\. The following tasks/users did not reset the watchdog in time:", line):
        return {"category": "task_watchdog"}
    reset = re.fullmatch(r"rst:0x([0-9a-fA-F]{1,2})(?: ?\([A-Z0-9_]+\))?,boot:0x[0-9a-fA-F]{1,2}(?: ?\([A-Z0-9_]+\))?", line)
    if reset:
        return {"category": "reset_reason", "value": int(reset[1], 16)}
    return None


def capture_startup(serial_module, comports, expected_identity, *,
                    monotonic=time.monotonic, sleep=time.sleep, wall_time=time.time):
    """Read a fresh serial handle after reset; caller owns exclusive custody.

    The caller's isolated-worker watchdog bounds blocking driver/enumerator calls.
    Missing early output and serial observation errors never establish boot failure.
    """
    started = monotonic()
    deadline = started + STARTUP_SECONDS
    result = {"schema": "OT225-STARTUP-1", "status": "unobserved", "markers": [],
              "early_output_may_be_missing": True, "bytes_read": 0,
              "capture_started_unix_ms": int(wall_time() * 1000)}
    port = None
    def finish(status):
        result["status"] = status
        result["elapsed_ms"] = max(0, int((monotonic() - started) * 1000))
        return result
    def identity(value):
        if type(value) is not str:
            return None
        value = value.replace(":", "").replace("-", "").lower()
        return value if re.fullmatch(r"[0-9a-f]{12}", value) else None
    expected = identity(expected_identity)
    if expected is None:
        return finish("identity_invalid")
    try:
        while monotonic() < deadline:
            try:
                ports = list(comports())
            except Exception:
                return finish("enumeration_error")
            matches = [p for p in ports if getattr(p, "vid", None) == 0x303a
                       and getattr(p, "pid", None) == 0x1001
                       and identity(getattr(p, "serial_number", None)) == expected]
            if len(matches) > 1:
                return finish("identity_ambiguous")
            if matches:
                route = matches[0].device
                if (type(route) is not str or not re.fullmatch(r"COM[1-9][0-9]{0,3}", route)
                        or sum(getattr(p, "device", None) == route for p in ports) != 1):
                    return finish("route_ambiguous")
                break
            sleep(min(0.1, max(0, deadline - monotonic())))
        else:
            return finish("device_not_observed")
        if monotonic() >= deadline:
            return finish("deadline")
        try:
            port = serial_module.Serial(port=None, baudrate=115200,
                                        timeout=min(0.25, deadline - monotonic()), write_timeout=0)
            port.dtr = False
            port.rts = False
            port.port = route
            if monotonic() >= deadline:
                return finish("deadline")
            port.open()
        except Exception:
            return finish("open_error")
        pending = bytearray()
        while monotonic() < deadline:
            port.timeout = min(0.25, max(0, deadline - monotonic()))
            try:
                raw = port.read(min(256, STARTUP_BYTES - result["bytes_read"]))
            except Exception:
                return finish("read_error")
            if type(raw) is not bytes:
                return finish("read_error")
            result["bytes_read"] += len(raw)
            if result["bytes_read"] > STARTUP_BYTES:
                return finish("byte_limit")
            for value in raw:
                if value == 10:
                    marker = parse_startup_line(bytes(pending))
                    pending.clear()
                    if marker is not None:
                        # Receipt time may lag the device event because USB buffers.
                        marker["received_ms"] = max(0, int((monotonic() - started) * 1000))
                        result["markers"].append(marker)
                        if len(result["markers"]) >= STARTUP_MARKERS:
                            return finish("marker_limit")
                else:
                    pending.append(value)
                    if len(pending) > STARTUP_LINE_BYTES:
                        marker = parse_startup_line(bytes(pending[:STARTUP_LINE_BYTES]), truncated=True)
                        if marker is not None and marker["category"] == "backtrace":
                            marker["received_ms"] = max(0, int((monotonic() - started) * 1000))
                            result["markers"].append(marker)
                        return finish("line_limit")
            if result["bytes_read"] >= STARTUP_BYTES:
                return finish("byte_limit")
        return finish("window_complete")
    finally:
        if port is not None:
            try:
                port.close()
            except Exception:
                result["status"] = "close_error"
'''

exec(WORKER_SUPPORT)


def validate_startup_result(value):
    """Reject any subprocess diagnostic outside the sanitized wire vocabulary."""
    statuses = {"unobserved", "identity_invalid", "enumeration_error",
                "identity_ambiguous", "route_ambiguous", "device_not_observed",
                "deadline", "open_error", "read_error", "byte_limit", "line_limit",
                "marker_limit", "window_complete", "close_error"}
    literals = {"self_check_pass", "self_check_fail", "runtime_fail",
                "runtime_started", "panic", "interrupt_watchdog", "task_watchdog"}
    numbers = {"runtime_start_error": 255, "security_stage": 255,
               "security_detail": 255, "heartbeat": (1 << 64) - 1,
               "app_stack": (1 << 32) - 1, "ble_host_stack": (1 << 32) - 1, "reset_reason": 255, "exception_cause": 63}
    def require(ok):
        if not ok:
            raise ValueError("startup_result_invalid")
    require(type(value) is dict and set(value) == {
        "schema", "status", "markers", "early_output_may_be_missing", "bytes_read", "elapsed_ms", "capture_started_unix_ms"})
    require(value["schema"] == "OT225-STARTUP-1" and type(value["schema"]) is str)
    require(type(value["status"]) is str and value["status"] in statuses)
    require(value["early_output_may_be_missing"] is True)
    require(type(value["bytes_read"]) is int and 0 <= value["bytes_read"] <= STARTUP_BYTES)
    require(type(value["elapsed_ms"]) is int and 0 <= value["elapsed_ms"] <= 240000)
    require(type(value["markers"]) is list and len(value["markers"]) <= STARTUP_MARKERS)
    require(type(value["capture_started_unix_ms"]) is int and
            0 <= value["capture_started_unix_ms"] <= 253402300799999)
    previous_ms = 0
    for marker in value["markers"]:
        require(type(marker) is dict and type(marker.get("category")) is str)
        require(type(marker.get("received_ms")) is int and
                previous_ms <= marker["received_ms"] <= value["elapsed_ms"])
        previous_ms = marker["received_ms"]
        category = marker["category"]
        if category == "panic":
            require(set(marker) in ({"category", "received_ms"}, {"category", "received_ms", "reason"}))
            if "reason" in marker:
                require(type(marker["reason"]) is str and marker["reason"] in PANIC_REASONS.values())
        elif category in ("stack_canary", "stack_overflow", "watchdog_task"):
            require(set(marker) in ({"category", "received_ms"}, {"category", "received_ms", "task"}))
            if category == "watchdog_task":
                require("task" in marker)
            if "task" in marker:
                require(type(marker["task"]) is str and marker["task"] in TASKS)
        elif category == "program_counter":
            require(set(marker) == {"category", "received_ms", "value"} and code_address(marker["value"]))
        elif category == "backtrace":
            require(set(marker) == {"category", "received_ms", "pcs", "termination"})
            require(type(marker["pcs"]) is list and len(marker["pcs"]) <= 20 and all(code_address(pc) for pc in marker["pcs"]))
            require(type(marker["termination"]) is str and marker["termination"] in ("complete", "corrupted", "continues", "truncated"))
        elif category in literals:
            require(set(marker) == {"category", "received_ms"})
        else:
            require(category in numbers and set(marker) == {"category", "value", "received_ms"})
            require(type(marker["value"]) is int and 0 <= marker["value"] <= numbers[category])
    return value
