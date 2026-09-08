"""Explicitly constructed, role-bound app-only transport; no CLI or discovery on import.

ROM admission is a controlled preflight operation. Later role checks use fresh
passive USB descriptors, including on serial reopen, so they never reset a live
radio session. This is accidental route-swap protection, not authentication of
malicious/cloned USB descriptors. An indistinguishable unplug/replug between
inventory samples cannot be detected. The caller must bind the complete source
and dependency closure and perform partition/recovery admission separately.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import importlib.metadata
import re
import subprocess
import threading
from typing import Callable

import noise_xk_role_recovery_coordinator as coordinator
from noise_xk_buffered_runtime import BufferedReconnectableEndpoint, lifecycle


class BackendError(RuntimeError):
    """Only fixed, non-identifying failure codes escape this boundary."""


def normalize_identity(value: object) -> str:
    if type(value) is not str or not re.fullmatch(
        r"(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|"
        r"[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})", value
    ):
        raise BackendError("identity_invalid")
    return value.replace(":", "").replace("-", "").lower()


def parse_rom_identity(output: object) -> str:
    if type(output) is not str:
        raise BackendError("rom_identity_invalid")
    lines = re.findall(r"(?im)^\s*MAC:\s*([^\r\n]+?)\s*$", output)
    if len(lines) != 1:
        raise BackendError("rom_identity_invalid")
    return normalize_identity(lines[0])


@dataclass(frozen=True, repr=False)
class RoleBinding:
    role: str
    private_route: str
    private_identity: str
    restore: coordinator.RestoreDescriptor


class _BoundEndpoint(BufferedReconnectableEndpoint):
    def __init__(self, owner, binding, factory):
        self._owner, self._binding = owner, binding
        self._lease_released = False
        super().__init__(binding.private_route, factory)

    def write_command(self, command):
        with self._owner._lock:
            self._owner._require(self._binding)
            return super().write_command(command)

    def expect(self, kind, timeout_ms):
        with self._owner._lock:
            self._owner._require(self._binding)
            return super().expect(kind, timeout_ms)

    def reopen(self):
        with self._owner._lock:
            self._owner._require(self._binding)
            return super().reopen()

    def close(self):
        with self._owner._lock:
            if self._lease_released:
                return
            # Failed close retains the lease: no later ROM action may assume
            # the serial handle was released merely because close was tried.
            try:
                super().close()
                if any(getattr(handle, "is_open", None) is not False
                       for handle in getattr(self, "_issued_handles", ())):
                    raise BackendError("radio_close_unconfirmed")
            except BaseException:
                raise BackendError("radio_close_unconfirmed") from None
            self._owner._leases.discard(self._binding.role)
            self._lease_released = True


class BoundBackend:
    def __init__(self, bindings: tuple[RoleBinding, RoleBinding], *,
                 inventory: Callable | None = None, transport=None,
                 rom_reader: Callable | None = None):
        try:
            valid = (type(bindings) is tuple and len(bindings) == 2
                     and all(type(b) is RoleBinding for b in bindings)
                     and tuple(b.role for b in bindings) == ("A", "B")
                     and all(type(b.private_route) is str and b.private_route
                             and coordinator._descriptor_valid(b.restore) for b in bindings)
                     and bindings[0].private_route.casefold() != bindings[1].private_route.casefold()
                     and normalize_identity(bindings[0].private_identity) !=
                     normalize_identity(bindings[1].private_identity)
                     and bindings[0].restore.sha256 != bindings[1].restore.sha256)
            if not valid:
                raise ValueError()
            self._bindings = bindings
            self._lock = threading.RLock()
            self._admitted: set[str] = set()
            self._leases: set[str] = set()
            self._written: set[str] = set()
            self._write_identity: dict[str, tuple[int, str]] = {}
            self._verified: set[str] = set()
            if transport is None and importlib.metadata.version("pyserial") != "3.5":
                raise ValueError()
            self._transport = transport or lifecycle.frozen_adapter.EsptoolSerialBackend(
                tuple(b.private_route for b in bindings))
            if inventory is None:
                from serial.tools.list_ports import comports
                inventory = comports
            self._inventory = inventory
            self._rom_reader = rom_reader or self._read_rom
        except BaseException:
            raise BackendError("backend_configuration_invalid") from None

    def _binding(self, endpoint):
        matches = [b for b in self._bindings if type(endpoint) is str and endpoint == b.private_route]
        if len(matches) != 1:
            raise BackendError("role_rejected")
        return matches[0]

    def _passive(self, binding):
        try:
            records = list(self._inventory())
            native = [p for p in records if p.vid == 0x303A and p.pid == 0x1001]
            expected = normalize_identity(binding.private_identity)
            routes = [p for p in records if p.device.casefold() == binding.private_route.casefold()]
            identities = [p for p in native if normalize_identity(p.serial_number) == expected]
            if (len(routes) != 1 or len(identities) != 1 or routes[0] is not identities[0]
                    or identities[0].device != binding.private_route):
                raise ValueError()
        except BaseException:
            self._admitted.discard(binding.role)
            raise BackendError("passive_identity_mismatch") from None

    def _require(self, binding):
        self._passive(binding)
        if binding.role not in self._admitted:
            raise BackendError("rom_admission_required")

    def verify_role(self, endpoint, role, descriptor):
        with self._lock:
            binding = None
            try:
                binding = self._binding(endpoint)
                if role != binding.role or descriptor != binding.restore:
                    raise BackendError("role_rejected")
                self._require(binding)
                return True
            except BaseException:
                if binding is not None:
                    self._admitted.discard(binding.role)
                return False

    def _no_radio(self):
        if self._leases:
            raise BackendError("radio_lease_active")

    def _read_rom(self, route):
        result = subprocess.run(
            [self._transport._python, "-m", "esptool", "--chip", "esp32s3", "--port", route,
             "--baud", str(coordinator.BAUD), "--before", "default-reset", "--after", "no-reset",
             "--no-stub", "read-mac"], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
        if result.returncode != 0:
            raise BackendError("rom_read_failed")
        return result.stdout.decode("utf-8", errors="strict")

    def _restore_runtime(self, binding):
        # Cleanup never resets a now-ambiguous route. This gate is deliberately
        # independent of successful ROM admission, including read failures.
        self._no_radio()
        self._passive(binding)
        self._transport.hard_reset(binding.private_route)
        self._passive(binding)

    def admit_rom(self, endpoint):
        with self._lock:
            self._no_radio()
            binding = self._binding(endpoint)
            if binding.role in self._written:
                raise BackendError("unrestored_write_pending")
            self._admitted.discard(binding.role)
            self._passive(binding)
            try:
                try:
                    observed = parse_rom_identity(self._rom_reader(binding.private_route))
                    if observed != normalize_identity(binding.private_identity):
                        raise BackendError("rom_identity_mismatch")
                    self._passive(binding)
                finally:
                    self._restore_runtime(binding)
            except BaseException:
                raise BackendError("rom_admission_failed") from None
            self._admitted.add(binding.role)

    def _application(self, operation, endpoint, offset, image):
        with self._lock:
            self._no_radio()
            binding = self._binding(endpoint)
            self._require(binding)
            is_write = operation == "write_application"
            size, digest = getattr(image, "size", None), getattr(image, "sha256", None)
            if (type(offset) is not int or offset != coordinator.APPLICATION_OFFSET
                    or type(size) is not int or not 0 < size <= coordinator.FACTORY_SLOT_BYTES
                    or type(digest) is not str or re.fullmatch(r"[0-9a-f]{64}", digest) is None):
                raise BackendError("application_image_invalid")
            key = (size, digest)
            if is_write:
                payload = getattr(image, "payload", None)
                if (type(payload) is not bytes or len(payload) != size
                        or hashlib.sha256(payload).hexdigest() != digest):
                    raise BackendError("application_image_invalid")
            elif key != self._write_identity.get(binding.role, (binding.restore.bytes, binding.restore.sha256)):
                raise BackendError("application_image_mismatch")
            if is_write:
                self._written.add(binding.role)
                self._write_identity[binding.role] = key
            if binding.role in self._written:
                self._verified.discard(binding.role)
            try:
                try:
                    getattr(self._transport, operation)(endpoint, offset, image)
                finally:
                    # Only the untouched, read-only preflight may automatically
                    # resume the known installed application. Never boot a newly
                    # written image, including partial/failed writes, until exact
                    # readback succeeds and the coordinator explicitly resets.
                    if not is_write and binding.role not in self._written:
                        self._restore_runtime(binding)
            except BaseException:
                raise BackendError("application_operation_failed") from None
            if not is_write and binding.role in self._written:
                self._verified.add(binding.role)

    def write_application(self, endpoint, offset, image):
        self._application("write_application", endpoint, offset, image)

    def verify_application(self, endpoint, offset, image):
        self._application("verify_application", endpoint, offset, image)

    def hard_reset(self, endpoint):
        with self._lock:
            self._no_radio()
            binding = self._binding(endpoint)
            self._require(binding)
            if binding.role in self._written and binding.role not in self._verified:
                raise BackendError("unverified_write_pending")
            try:
                self._restore_runtime(binding)
            except BaseException:
                raise BackendError("runtime_reset_failed") from None
            self._written.discard(binding.role)
            self._write_identity.pop(binding.role, None)
            self._verified.discard(binding.role)

    def open_radio_endpoint(self, endpoint):
        with self._lock:
            binding = self._binding(endpoint)
            self._require(binding)
            if binding.role in self._written:
                raise BackendError("unrestored_write_pending")
            if binding.role in self._leases:
                raise BackendError("radio_lease_active")
            self._leases.add(binding.role)

            def factory():
                self._require(binding)
                return self._transport._serial.Serial(
                    port=None, baudrate=coordinator.BAUD,
                    timeout=lifecycle.frozen_adapter.SERIAL_TIMEOUT_SECONDS)
            try:
                return _BoundEndpoint(self, binding, factory)
            except BaseException:
                # Keep an uncertain failed-open lease; require a new backend
                # after external handle cleanup, not an automatic ROM reset.
                raise BackendError("radio_open_failed") from None
