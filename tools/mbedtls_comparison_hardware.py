"""Explicit nonradio comparison hardware boundary; no CLI or authority issuance.

Construction performs no device access. The caller must source-verify this module,
the session and its package, then bind fresh physical preflight and one-use authority.
USB identity checks prevent accidental route swaps, not cloned-descriptor attacks.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import dataclasses
import hashlib
import importlib.metadata
import json
import re
import subprocess
import sys
import tempfile
import threading
import time


class HardwareError(RuntimeError):
    """Fixed failure categories only; never includes route or subprocess output."""


def require(condition: bool, code: str) -> None:
    if not condition:
        raise HardwareError(code)


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def identity(value: str) -> str:
    require(type(value) is str and re.fullmatch(
        r"(?:[a-fA-F0-9]{12}|[a-fA-F0-9]{2}(?::[a-fA-F0-9]{2}){5}|[a-fA-F0-9]{2}(?:-[a-fA-F0-9]{2}){5})", value), "identity_invalid")
    return value.replace(":", "").replace("-", "").lower()


def safe(function):
    def invoke(*args, **kwargs):
        try:
            return function(*args, **kwargs)
        except HardwareError:
            raise
        except BaseException:
            raise HardwareError("hardware_operation_failed") from None
    return invoke


@dataclass(frozen=True, repr=False)
class RoleBinding:
    role: str
    private_route: str
    private_identity: str
    restore: object


class RomTransport:
    """Pinned esptool ROM operations and serial opening with explicit modem state."""
    def __init__(self):
        require(sys.version_info[:3] == (3, 14, 6), "python_version_changed")
        require(importlib.metadata.version("esptool") == "5.3.1", "esptool_version_changed")
        require(importlib.metadata.version("pyserial") == "3.5", "serial_version_changed")

    def command(self, route: str, operation: list[str], *, reset=False) -> bytes:
        args = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", route,
                "--baud", "115200", "--before", "default-reset", "--after",
                "hard-reset" if reset else "no-reset"]
        if not reset:
            args.append("--no-stub")
        result = subprocess.run(args + operation, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=180, check=False)
        require(result.returncode == 0, "rom_command_failed")
        return result.stdout

    def read(self, route: str, offset: int, size: int) -> bytes:
        require(type(offset) is int and type(size) is int and
                ((offset, size) in ((0, 32768), (0x8000, 4096), (0x9000, 8192)) or
                 (offset == 65536 and 0 < size <= 589824)), "read_scope_invalid")
        with tempfile.TemporaryDirectory(prefix="trail-comparison-read-") as directory:
            path = Path(directory) / "region.bin"
            self.command(route, ["read-flash", hex(offset), str(size), str(path)])
            raw = path.read_bytes()
        require(len(raw) == size, "readback_length_changed")
        return raw

    def write(self, route: str, raw: bytes) -> None:
        require(type(raw) is bytes and 0 < len(raw) <= 589824, "write_scope_invalid")
        with tempfile.TemporaryDirectory(prefix="trail-comparison-write-") as directory:
            path = Path(directory) / "application.bin"
            path.write_bytes(raw)
            self.command(route, ["write-flash", "--flash-size", "16MB", "0x10000", str(path)])

    def reset(self, route: str) -> None:
        self.command(route, ["run"], reset=True)

    def open(self, route: str):
        import serial
        handle = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=0.5)
        handle.dtr = False
        handle.rts = False
        handle.port = route
        try:
            handle.open()
            return handle
        except BaseException:
            try:
                handle.close()
            except BaseException:
                pass
            raise HardwareError("serial_open_failed") from None


class Backend:
    REGIONS = {"bootloader": (0, 32768), "partition": (0x8000, 4096), "ota": (0x9000, 8192)}
    ORIGINAL_SPAN = 589824

    @safe
    def __init__(self, session, bindings: tuple, protected_regions: dict, *, inventory=None, transport=None):
        require(type(bindings) is tuple and len(bindings) == 2, "roles_invalid")
        require(tuple(b.role for b in bindings) == ("A", "B"), "roles_invalid")
        require(all(type(b) is RoleBinding for b in bindings), "roles_invalid")
        require(all(type(b.private_route) is str and b.private_route for b in bindings), "route_invalid")
        require(bindings[0].private_route.casefold() != bindings[1].private_route.casefold(), "duplicate_route")
        require(identity(bindings[0].private_identity) != identity(bindings[1].private_identity), "duplicate_identity")
        require(tuple(b.restore for b in bindings) == (session.binding.restore_a, session.binding.restore_b), "restore_binding_changed")
        require(set(protected_regions) == {"A", "B"}, "protected_regions_invalid")
        for regions in protected_regions.values():
            require(set(regions) == set(self.REGIONS), "protected_regions_invalid")
            for name, descriptor in regions.items():
                require(set(descriptor) == {"bytes", "sha256"} and descriptor["bytes"] == self.REGIONS[name][1]
                        and re.fullmatch("[0-9a-f]{64}", descriptor["sha256"]), "protected_regions_invalid")
        self.session = session
        self.bindings = bindings
        self.regions = json.loads(json.dumps(protected_regions))
        self.transport = transport or RomTransport()
        if inventory is None:
            from serial.tools.list_ports import comports
            inventory = comports
        self.inventory = inventory
        self.lock = threading.RLock()
        self.leases = set()
        self.admitted = set()
        self.verified = {}
        self.preflight = {}

    @safe
    def assert_idle(self):
        with self.lock:
            require(not self.leases, "serial_lease_active")
            return True

    @safe
    def current_preflight(self):
        with self.lock:
            return json.loads(json.dumps([self.preflight[r] for r in ("A", "B") if r in self.preflight]))

    def binding(self, route):
        matches = [b for b in self.bindings if type(route) is str and b.private_route == route]
        require(len(matches) == 1, "route_rejected")
        return matches[0]

    def passive(self, binding, *, absent_ok=False):
        records = list(self.inventory())
        routes = [p for p in records if p.device.casefold() == binding.private_route.casefold()]
        if absent_ok and not routes:
            return False
        matches = [p for p in records if p.vid == 0x303A and p.pid == 0x1001
                   and identity(p.serial_number) == identity(binding.private_identity)]
        require(len(routes) == 1 and len(matches) == 1 and routes[0] is matches[0]
                and routes[0].device == binding.private_route, "passive_identity_mismatch")
        return True

    def rom_identity(self, binding):
        require(not self.leases, "serial_lease_active")
        self.passive(binding)
        raw = self.transport.command(binding.private_route, ["read-mac"]).decode("utf-8")
        lines = re.findall(r"(?im)^\s*MAC:\s*([^\r\n]+?)\s*$", raw)
        require(len(lines) in (1, 2) and all(identity(v) == identity(binding.private_identity) for v in lines), "rom_identity_mismatch")
        self.passive(binding)

    def read_protected(self, binding):
        observed = {}
        for name, (offset, size) in self.REGIONS.items():
            self.rom_identity(binding)
            raw = self.transport.read(binding.private_route, offset, size)
            observed[name] = {"bytes": len(raw), "sha256": sha(raw)}
            require(observed[name] == self.regions[binding.role][name], "protected_region_changed")
        return observed

    @safe
    def admit(self, route, *, recovery=False):
        require(type(recovery) is bool, "recovery_mode_invalid")
        with self.lock:
            binding = self.binding(route)
            self.admitted.discard(binding.role)
            self.verified.pop(binding.role, None)
            self.rom_identity(binding)
            info = self.transport.command(route, ["flash-id"]).decode("utf-8")
            require(re.search(r"(?m)^Detected flash size:\s*16MB\s*$", info) is not None
                    and "ESP32-S3" in info, "flash_geometry_mismatch")
            regions = self.read_protected(binding)
            node = {"role": binding.role, "checks_complete": False, "runtime_reset_succeeded": False, "regions": regions}
            self.preflight[binding.role] = node
            if not recovery:
                descriptor = binding.restore
                self.rom_identity(binding)
                raw = self.transport.read(route, 0x10000, self.ORIGINAL_SPAN)
                require(len(raw) == self.ORIGINAL_SPAN and sha(raw[:descriptor.bytes]) == descriptor.sha256
                        and raw[descriptor.bytes:] == b"\xff" * (self.ORIGINAL_SPAN - descriptor.bytes), "installed_original_changed")
                node["regions"]["application"] = {"bytes": len(raw), "sha256": sha(raw)}
                self.verified[binding.role] = descriptor.sha256
                self.rom_identity(binding)
                self.transport.reset(route)
                self.passive(binding)
                node["runtime_reset_succeeded"] = True
            node["checks_complete"] = True
            self.admitted.add(binding.role)
            return json.loads(json.dumps(node))

    @safe
    def verify_role(self, route, role, descriptor):
        with self.lock:
            binding = self.binding(route)
            require(binding.role == role and binding.restore == descriptor and role in self.admitted, "role_not_admitted")
            self.passive(binding)
            return True

    def image(self, binding, image):
        raw = image.payload
        require(type(raw) is bytes and len(raw) == image.size and sha(raw) == image.sha256, "image_invalid")
        expected = [self.session.package["images"][0], dataclasses.asdict(binding.restore)]
        require(any(image.name == d["name"] and image.size == d["bytes"] and image.sha256 == d["sha256"] for d in expected), "image_not_bound")
        return raw

    @safe
    def write_application(self, route, offset, image):
        with self.lock:
            binding = self.binding(route)
            require(type(offset) is int and offset == 65536, "offset_invalid")
            self.verify_role(route, binding.role, binding.restore)
            raw = self.image(binding, image)
            self.verified.pop(binding.role, None)
            self.read_protected(binding)
            self.rom_identity(binding)
            self.transport.write(route, raw)
            self.passive(binding)

    @safe
    def verify_application(self, route, offset, image):
        with self.lock:
            binding = self.binding(route)
            require(type(offset) is int and offset == 65536, "offset_invalid")
            self.verify_role(route, binding.role, binding.restore)
            raw = self.image(binding, image)
            self.verified.pop(binding.role, None)
            self.rom_identity(binding)
            span = self.ORIGINAL_SPAN if image.sha256 == binding.restore.sha256 else ((len(raw) + 4095) // 4096) * 4096
            observed = self.transport.read(route, offset, span)
            require(observed == raw + b"\xff" * (span - len(raw)), "application_readback_changed")
            self.read_protected(binding)
            self.verified[binding.role] = image.sha256

    @safe
    def hard_reset(self, route):
        with self.lock:
            binding = self.binding(route)
            self.verify_role(route, binding.role, binding.restore)
            require(binding.role in self.verified, "unverified_application")
            self.rom_identity(binding)
            self.transport.reset(route)
            self.passive(binding)

    reset = hard_reset

    @safe
    def is_present(self, route):
        with self.lock:
            return self.passive(self.binding(route), absent_ok=True)

    @safe
    def open(self, route):
        with self.lock:
            binding = self.binding(route)
            self.verify_role(route, binding.role, binding.restore)
            require(binding.role in self.verified and not self.leases, "serial_lease_active")
            self.leases.add(binding.role)
            # Failed/uncertain opens retain the lease until external reconciliation.
            handle = self.transport.open(route)
            return LeasedEndpoint(self, binding, handle)


class LeasedEndpoint:
    def __init__(self, owner, binding, handle):
        self.owner, self.binding, self.handle = owner, binding, handle
        self.closed = False

    def call(self, name, *args):
        with self.owner.lock:
            require(not self.closed, "serial_closed")
            self.owner.passive(self.binding)
            return getattr(self.handle, name)(*args)

    @safe
    def read(self, size):
        return self.call("read", size)

    @safe
    def write(self, raw):
        return self.call("write", raw)

    @safe
    def flush(self):
        deadline = time.monotonic() + 1.0
        while True:
            with self.owner.lock:
                require(not self.closed, "serial_closed")
                self.owner.passive(self.binding)
                pending = self.handle.out_waiting
                require(type(pending) is int and pending >= 0, "serial_queue_invalid")
            if pending == 0:
                return
            require(time.monotonic() < deadline, "serial_flush_timeout")
            time.sleep(0.005)

    @safe
    def close(self):
        with self.owner.lock:
            if self.closed:
                return
            self.handle.close()
            require(getattr(self.handle, "is_open", None) is False, "serial_close_unconfirmed")
            self.closed = True
            self.owner.leases.discard(self.binding.role)


def decode(raw: bytes) -> dict:
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate_key")
            result[key] = value
        return result
    value = json.loads(raw, object_pairs_hook=pairs)
    require(type(value) is dict, "object_required")
    return value


class AuthorityGate:
    """Exact caller-provided nonradio scope; journal consumption belongs to Session."""
    def __init__(self, session, grant_path: Path, grant_sha: str, expected_grant: dict, bound_files: dict):
        self.session = session
        self.path = Path(grant_path)
        self.sha = grant_sha
        self.expected = json.loads(json.dumps(expected_grant))
        self.files = dict(bound_files)
        require(self.expected.get("schema") == "mbedtls-comparison-authority-1"
                and type(self.expected.get("attempt_count")) is int
                and self.expected.get("attempt_count") == 1
                and self.expected.get("radio_allowed") is False
                and self.expected.get("reusable") is False, "authority_scope_invalid")
        require({"caller", "package", "preflight", "registry", "hardware_source"} <= set(self.files) <= {"caller", "package", "preflight", "registry", "hardware_source", "manifest", "core_source"}, "authority_files_invalid")
        require(self.expected.get("bound_files") == {
            k: {"bytes": v[1], "sha256": v[2]} for k, v in self.files.items()}, "authority_files_mismatch")
        require(self.expected.get("binding") == dataclasses.asdict(session.binding), "authority_binding_mismatch")

    @safe
    def validate(self, binding, *, recovery):
        require(binding == self.session.binding and type(recovery) is bool, "authority_binding_mismatch")
        for path, size, digest in self.files.values():
            raw = Path(path).read_bytes()
            require(len(raw) == size and sha(raw) == digest, "authority_file_changed")
        package = decode(Path(self.files["package"][0]).read_bytes())
        require(package == self.session.package, "authority_package_changed")
        raw = self.path.read_bytes()
        require(sha(raw) == self.sha and json.dumps(decode(raw), sort_keys=True) == json.dumps(self.expected, sort_keys=True), "authority_changed")
        # Session's preparation revalidates source/image bytes in normal/recovery mode.
        return self.session.coordinator.AuthorityGrant(self.sha, 1, False, False)
