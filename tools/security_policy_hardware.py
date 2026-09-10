"""Explicit OT-188 ROM/serial boundary; importing or constructing opens no device.

The orchestrator owns authority, exact image/NVS binding and verified-before-reset
ordering. This module never discovers a role, retries a mutation or erases NVS.
USB/ROM identity checks prevent accidental swaps, not cloned identity attacks.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import functools
import importlib.metadata
import re
import subprocess
import sys
import tempfile
import threading
import time


class HardwareError(RuntimeError):
    """Fixed categories only, without identifiers or subprocess diagnostics."""


def require(condition, code):
    if not condition:
        raise HardwareError(code)


def safe(function):
    @functools.wraps(function)
    def call(*args, **kwargs):
        try:
            return function(*args, **kwargs)
        except HardwareError:
            raise
        except BaseException:
            raise HardwareError("hardware_operation_failed") from None
    return call


def identity(value):
    require(type(value) is str and re.fullmatch(
        r"(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})",
        value) is not None, "identity_invalid")
    return value.replace(":", "").replace("-", "").lower()


@dataclass(frozen=True, repr=False)
class RoleBinding:
    role: str
    private_route: str
    private_identity: str


READ_SPANS = frozenset(((0, 32768), (0x8000, 4096), (0x9000, 8192),
                        (0xD000, 0x3000), (0x10000, 589824)))
WRITE_SPANS = frozenset(((0xD000, 0x3000), (0x10000, 589824)))


def read_scope(offset, size):
    require(type(offset) is int and type(size) is int and
            (offset, size) in READ_SPANS, "read_scope_invalid")


def write_scope(offset, raw):
    require(type(offset) is int and type(raw) is bytes and
            (offset, len(raw)) in WRITE_SPANS, "write_scope_invalid")


class RomTransport:
    """No-stub ROM I/O, private transient region files and bounded serial writes."""
    @safe
    def __init__(self, private_root):
        require(isinstance(private_root, Path) and private_root.is_absolute(), "private_root_required")
        require(private_root.is_dir() and private_root.name == ".private", "private_root_invalid")
        require(not any(p.is_symlink() or p.is_junction() for p in (private_root, *private_root.parents)),
                "private_root_indirect")
        self.private_root = private_root.resolve()
        require(sys.version_info[:3] == (3, 14, 6), "python_version_changed")
        require(importlib.metadata.version("esptool") == "5.3.1", "esptool_version_changed")
        require(importlib.metadata.version("pyserial") == "3.5", "serial_version_changed")

    @safe
    def command(self, route, operation, *, reset=False):
        args = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", route,
                "--baud", "115200", "--before", "default-reset", "--after",
                "hard-reset" if reset else "no-reset", "--no-stub"]
        result = subprocess.run(args + operation, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=180, check=False)
        require(result.returncode == 0 and len(result.stdout) <= 1048576, "rom_command_failed")
        return result.stdout

    @safe
    def read(self, route, offset, size):
        read_scope(offset, size)
        with tempfile.TemporaryDirectory(prefix="ot188-read-", dir=self.private_root) as directory:
            path = Path(directory) / "region.bin"
            self.command(route, ["read-flash", hex(offset), str(size), str(path)])
            raw = path.read_bytes()
        require(len(raw) == size, "readback_length_changed")
        return raw

    @safe
    def write(self, route, offset, raw):
        write_scope(offset, raw)
        with tempfile.TemporaryDirectory(prefix="ot188-write-", dir=self.private_root) as directory:
            path = Path(directory) / "region.bin"
            path.write_bytes(raw)
            self.command(route, ["write-flash", "--flash-size", "16MB", hex(offset), str(path)])
        return True

    @safe
    def reset(self, route):
        self.command(route, ["run"], reset=True)
        return True

    @safe
    def open(self, route):
        import serial
        handle = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=0.5)
        handle.dtr = False
        handle.rts = False
        handle.port = route
        try:
            handle.open()
        except BaseException:
            try:
                handle.close()
            except BaseException:
                pass
            raise HardwareError("serial_open_failed") from None
        return handle


class Backend:
    @safe
    def __init__(self, bindings, *, inventory=None, transport=None, monotonic=time.monotonic,
                 private_root=None, endpoint_factory=None):
        require(type(bindings) is tuple and len(bindings) == 2 and
                all(type(b) is RoleBinding for b in bindings), "bindings_invalid")
        require(tuple(b.role for b in bindings) == ("A", "B"), "roles_invalid")
        require(all(type(b.private_route) is str and b.private_route for b in bindings), "route_invalid")
        require(len({b.private_route.casefold() for b in bindings}) == 2 and
                len({identity(b.private_identity) for b in bindings}) == 2, "duplicate_role")
        require(callable(monotonic), "clock_invalid")
        self.bindings = bindings
        self.inventory = inventory
        self.transport = transport if transport is not None else RomTransport(private_root)
        self.monotonic = monotonic
        self.endpoint_factory = endpoint_factory
        self.lock = threading.RLock()
        self.generations = {"A": 0, "B": 0}
        self.leases = set()
        self.endpoints = {}
        self.handles = {}
        self.issued_handles = []  # Retain objects: Python ids alone can be recycled.

    def binding(self, role):
        require(type(role) is str and role in ("A", "B"), "role_invalid")
        return self.bindings[0 if role == "A" else 1]

    def passive(self, binding, *, absent_ok=False):
        inventory = self.inventory
        if inventory is None:
            from serial.tools.list_ports import comports
            inventory = comports
        records = list(inventory())
        routes = [p for p in records if p.device.casefold() == binding.private_route.casefold()]
        matches = [p for p in records if p.vid == 0x303A and p.pid == 0x1001 and
                   identity(p.serial_number) == identity(binding.private_identity)]
        if absent_ok and not routes and not matches:
            return False
        require(len(routes) == 1 and len(matches) == 1 and routes[0] is matches[0] and
                routes[0].device == binding.private_route, "passive_identity_mismatch")
        return True

    def wait_present(self, binding):
        started = self.monotonic()
        last = started
        for _ in range(501):
            current = self.monotonic()
            require(type(current) in (int, float) and current >= last and current - started <= 5.0,
                    "serial_reenumeration_timeout")
            last = current
            if self.passive(binding, absent_ok=True):
                return
            time.sleep(0.01)
        raise HardwareError("serial_reenumeration_timeout")

    def rom(self, binding):
        require(not self.leases, "serial_lease_active")
        self.passive(binding)
        raw = self.transport.command(binding.private_route, ["read-mac"])
        require(type(raw) is bytes, "rom_identity_invalid")
        values = re.findall(r"(?im)^\s*MAC:\s*([^\r\n]+?)\s*$", raw.decode("utf-8"))
        require(len(values) in (1, 2) and all(identity(v) == identity(binding.private_identity)
                                            for v in values), "rom_identity_mismatch")
        self.passive(binding)
        info = self.transport.command(binding.private_route, ["flash-id"])
        require(type(info) is bytes and b"ESP32-S3" in info and
                re.search(rb"(?m)^Detected flash size:\s*16MB\s*$", info) is not None,
                "flash_geometry_mismatch")
        self.passive(binding)

    @safe
    def assert_idle(self):
        with self.lock:
            require(not self.leases, "serial_lease_active")
            return True

    @safe
    def read(self, role, offset, size):
        read_scope(offset, size)
        with self.lock:
            binding = self.binding(role)
            self.rom(binding)
            raw = self.transport.read(binding.private_route, offset, size)
            self.passive(binding)
            require(type(raw) is bytes and len(raw) == size, "readback_length_changed")
            return raw

    @safe
    def write(self, role, offset, raw):
        write_scope(offset, raw)
        with self.lock:
            binding = self.binding(role)
            self.rom(binding)
            self.generations[role] += 1
            require(self.transport.write(binding.private_route, offset, raw) is True, "write_unconfirmed")
            self.passive(binding)
            return True

    @safe
    def reset(self, role):
        with self.lock:
            binding = self.binding(role)
            self.rom(binding)
            self.generations[role] += 1
            require(self.transport.reset(binding.private_route) is True, "reset_unconfirmed")
            self.wait_present(binding)
            return True

    def guard(self, role, generation):
        with self.lock:
            require(role in self.leases and self.generations[role] == generation, "serial_generation_changed")
            self.passive(self.binding(role))
            return True

    @safe
    def open(self, role):
        with self.lock:
            binding = self.binding(role)
            require(not self.leases, "serial_lease_active")
            self.wait_present(binding)
            self.leases.add(role)  # Even an uncertain failed open blocks every ROM action.
            generation = self.generations[role]
            handle = self.transport.open(binding.private_route)
            require(not any(handle is prior for prior in self.issued_handles), "serial_handle_reused")
            self.issued_handles.append(handle)
            self.handles[role] = handle
            self.guard(role, generation)
            factory = self.endpoint_factory
            if factory is None:
                from security_policy_endpoint import Endpoint
                factory = Endpoint
            endpoint = factory(handle, guard=lambda: self.guard(role, generation), monotonic=self.monotonic)
            self.endpoints[role] = endpoint
            return endpoint

    @safe
    def close(self, role):
        with self.lock:
            self.binding(role)
            if role not in self.leases:
                require(role not in self.endpoints and role not in self.handles, "serial_close_unconfirmed")
                return True
            require(role in self.endpoints, "serial_close_unavailable")
            self.endpoints[role].close()
            require(getattr(self.handles[role], "is_open", None) is False, "serial_close_unconfirmed")
            self.leases.remove(role)
            self.generations[role] += 1
            self.endpoints.pop(role)
            self.handles.pop(role)
            return True
