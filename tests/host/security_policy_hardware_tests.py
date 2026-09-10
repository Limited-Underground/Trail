"""Host-only injected transport tests for the OT-188 hardware boundary."""
from pathlib import Path
import importlib.util
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("ot188_hardware_tested", ROOT / "tools/security_policy_hardware.py")
hw = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = hw
spec.loader.exec_module(hw)


class Handle:
    def __init__(self):
        self.is_open = True
        self.fail_close = False
    def close(self):
        if self.fail_close:
            raise RuntimeError("private device details")
        self.is_open = False


class Endpoint:
    def __init__(self, handle, guard, monotonic):
        self.handle, self.guard, self.monotonic = handle, guard, monotonic
    def close(self):
        self.handle.close()


class Transport:
    def __init__(self):
        self.calls = []
        self.mac = {"route-a": "010203040506", "route-b": "111213141516"}
        self.memory = {}
        self.fail = None
        self.mutate = None
        self.handle = Handle()
        self.geometry = b"ESP32-S3\nDetected flash size: 16MB\n"
    def command(self, route, operation):
        self.calls.append(("command", route, tuple(operation)))
        if self.fail == "command":
            raise RuntimeError("private stderr route-a")
        return ("MAC: " + self.mac[route] + "\n").encode() if operation == ["read-mac"] else self.geometry
    def read(self, route, offset, size):
        self.calls.append(("read", route, offset, size))
        if self.mutate:
            self.mutate()
        return self.memory.get((route, offset), b"\xff" * size)
    def write(self, route, offset, raw):
        self.calls.append(("write", route, offset, len(raw)))
        self.memory[route, offset] = raw
        if self.fail == "write":
            raise RuntimeError("partial private write")
        if self.mutate:
            self.mutate()
        return True
    def reset(self, route):
        self.calls.append(("reset", route))
        if self.fail == "reset":
            raise RuntimeError("uncertain private reset")
        return True
    def open(self, route):
        self.calls.append(("open", route))
        if self.fail == "open":
            raise RuntimeError("uncertain private open")
        return self.handle


class HardwareTests(unittest.TestCase):
    def setUp(self):
        self.bindings = (hw.RoleBinding("A", "route-a", ":".join(f"{value:02x}" for value in range(1, 7))),
                         hw.RoleBinding("B", "route-b", "111213141516"))
        self.ports = [SimpleNamespace(device=b.private_route, serial_number=b.private_identity,
                                      vid=0x303A, pid=0x1001) for b in self.bindings]
        self.transport = Transport()
        self.backend = hw.Backend(self.bindings, inventory=lambda: self.ports,
                                  transport=self.transport, endpoint_factory=Endpoint)
    def test_constructor_and_import_have_no_device_access(self):
        self.assertEqual(self.transport.calls, [])
        self.assertTrue(self.backend.assert_idle())
    def test_full_reads_all_admitted_regions_without_reset(self):
        for offset, size in hw.READ_SPANS:
            self.assertEqual(self.backend.read("A", offset, size), b"\xff" * size)
        self.assertFalse(any(c[0] == "reset" for c in self.transport.calls))
    def test_exact_app_and_nvs_writes_are_distinct_and_leave_rom(self):
        for role in ("A", "B"):
            for offset, size in hw.WRITE_SPANS:
                raw = bytes([offset & 255]) * size
                self.assertTrue(self.backend.write(role, offset, raw))
                self.assertEqual(self.backend.read(role, offset, size), raw)
        self.assertFalse(any(c[0] == "reset" for c in self.transport.calls))
    def test_bad_scopes_rejected_before_any_device_access(self):
        for offset, raw in [(True, b"x"), (0xD000, b"x"), (0, b"x" * 32768), (0x10000, b"x" * 437488)]:
            with self.assertRaises(hw.HardwareError):
                self.backend.write("A", offset, raw)
        for offset, size in [(0xD000, 1), (True, 12288), (0x10000, True), (0x8000, 8192)]:
            with self.assertRaises(hw.HardwareError):
                self.backend.read("A", offset, size)
        self.assertEqual(self.transport.calls, [])
    def test_duplicate_roles_routes_identity_rejected(self):
        for bindings in [(self.bindings[0], self.bindings[0]),
                         (self.bindings[0], hw.RoleBinding("B", "ROUTE-A", "111213141516")),
                         (self.bindings[0], hw.RoleBinding("B", "route-b", "010203040506"))]:
            with self.assertRaises(hw.HardwareError):
                hw.Backend(bindings, transport=self.transport)
        self.assertEqual(self.transport.calls, [])
    def test_passive_swap_prevents_rom_access(self):
        self.ports[0].serial_number = self.bindings[1].private_identity
        with self.assertRaisesRegex(hw.HardwareError, "passive_identity_mismatch"):
            self.backend.read("A", 0xD000, 12288)
        self.assertEqual(self.transport.calls, [])
    def test_rom_identity_and_geometry_failure_never_reset(self):
        self.transport.mac["route-a"] = "ffffffffffff"
        with self.assertRaises(hw.HardwareError):
            self.backend.reset("A")
        self.transport.mac["route-a"] = "010203040506"
        self.transport.geometry = b"ESP32-S3\nDetected flash size: 8MB\n"
        with self.assertRaises(hw.HardwareError):
            self.backend.write("A", 0xD000, b"\xff" * 12288)
        self.assertFalse(any(c[0] in ("reset", "write") for c in self.transport.calls))
    def test_post_read_and_post_write_identity_guard(self):
        self.transport.mutate = lambda: setattr(self.ports[0], "device", "changed")
        with self.assertRaises(hw.HardwareError):
            self.backend.read("A", 0xD000, 12288)
        self.ports[0].device = "route-a"
        with self.assertRaises(hw.HardwareError):
            self.backend.write("A", 0xD000, b"\xff" * 12288)
    def test_short_read_refused(self):
        self.transport.memory["route-a", 0xD000] = b"bad"
        with self.assertRaisesRegex(hw.HardwareError, "readback_length_changed"):
            self.backend.read("A", 0xD000, 12288)
    def test_partial_write_failure_never_automatically_boots(self):
        self.transport.fail = "write"
        with self.assertRaisesRegex(hw.HardwareError, "^hardware_operation_failed$"):
            self.backend.write("A", 0xD000, b"x" * 12288)
        self.assertEqual(self.backend.generations["A"], 1)
        self.assertFalse(any(c[0] == "reset" for c in self.transport.calls))
    def test_reset_generation_advances_even_on_uncertain_result(self):
        self.transport.fail = "reset"
        with self.assertRaises(hw.HardwareError):
            self.backend.reset("B")
        self.assertEqual(self.backend.generations["B"], 1)
    def test_open_lease_blocks_every_role_rom_until_close(self):
        endpoint = self.backend.open("A")
        self.assertTrue(endpoint.guard())
        for operation in (lambda: self.backend.read("B", 0xD000, 12288),
                          lambda: self.backend.write("B", 0xD000, b"x" * 12288),
                          lambda: self.backend.reset("B"), self.backend.assert_idle):
            with self.assertRaisesRegex(hw.HardwareError, "serial_lease_active"):
                operation()
        self.assertTrue(self.backend.close("A"))
        self.assertTrue(self.backend.assert_idle())
        with self.assertRaisesRegex(hw.HardwareError, "serial_generation_changed"):
            endpoint.guard()
    def test_uncertain_open_remains_leased(self):
        self.transport.fail = "open"
        with self.assertRaises(hw.HardwareError):
            self.backend.open("A")
        with self.assertRaisesRegex(hw.HardwareError, "serial_lease_active"):
            self.backend.reset("B")
    def test_uncertain_close_remains_leased(self):
        self.backend.open("A")
        self.transport.handle.fail_close = True
        with self.assertRaises(hw.HardwareError):
            self.backend.close("A")
        with self.assertRaises(hw.HardwareError):
            self.backend.assert_idle()
        self.transport.handle.fail_close = False
        self.assertTrue(self.backend.close("A"))
    def test_guard_detects_generation_or_passive_change(self):
        endpoint = self.backend.open("A")
        self.backend.generations["A"] += 1
        with self.assertRaises(hw.HardwareError):
            endpoint.guard()
        self.backend.generations["A"] -= 1
        self.ports[0].device = "changed"
        with self.assertRaises(hw.HardwareError):
            endpoint.guard()
        self.assertTrue(self.backend.close("A"))
    def test_single_open_after_bounded_reenumeration(self):
        inventory_calls = []
        def inventory():
            inventory_calls.append(1)
            return [] if len(inventory_calls) == 1 else self.ports
        self.backend.inventory = inventory
        self.backend.open("A")
        self.assertEqual(sum(c[0] == "open" for c in self.transport.calls), 1)
        self.backend.close("A")
    def test_absence_deadline_does_not_open(self):
        self.backend.inventory = lambda: []
        times = iter((0.0, 6.0))
        self.backend.monotonic = lambda: next(times)
        with self.assertRaisesRegex(hw.HardwareError, "serial_reenumeration_timeout"):
            self.backend.open("A")
        self.assertEqual(self.transport.calls, [])
        self.assertTrue(self.backend.assert_idle())
    def test_reset_reenumeration_wait_never_retries_reset(self):
        original = self.transport.reset
        self.transport.reset = lambda route: (original(route), self.ports.clear())[0]
        times = iter((0.0, 6.0))
        self.backend.monotonic = lambda: next(times)
        with self.assertRaisesRegex(hw.HardwareError, "serial_reenumeration_timeout"):
            self.backend.reset("A")
        self.assertEqual(sum(c[0] == "reset" for c in self.transport.calls), 1)
        self.assertEqual(self.backend.generations["A"], 1)

    def test_idle_close_before_open_and_after_close(self):
        self.assertTrue(self.backend.close("A"))
        self.backend.open("A")
        self.assertTrue(self.backend.close("A"))
        self.assertTrue(self.backend.close("A"))

    def test_actual_endpoint_default_factory(self):
        from security_policy_endpoint import Endpoint as ActualEndpoint
        self.backend.endpoint_factory = None
        endpoint = self.backend.open("A")
        self.assertIsInstance(endpoint, ActualEndpoint)
        endpoint._admit()
        self.assertTrue(self.backend.close("A"))
        self.assertTrue(self.backend.assert_idle())

    def test_closed_generation_stays_invalid_after_fresh_open(self):
        old = self.backend.open("A")
        self.backend.close("A")
        self.transport.handle = Handle()
        fresh = self.backend.open("A")
        self.assertTrue(fresh.guard())
        with self.assertRaisesRegex(hw.HardwareError, "serial_generation_changed"):
            old.guard()
        self.backend.close("A")

    def test_reused_handle_after_reset_retains_uncertain_lease(self):
        self.backend.open("A")
        self.backend.close("A")
        self.backend.reset("A")
        self.transport.handle.is_open = True  # Broken transport reopens old object.
        with self.assertRaisesRegex(hw.HardwareError, "serial_handle_reused"):
            self.backend.open("A")
        before = list(self.transport.calls)
        with self.assertRaisesRegex(hw.HardwareError, "serial_lease_active"):
            self.backend.reset("B")
        with self.assertRaisesRegex(hw.HardwareError, "serial_lease_active"):
            self.backend.read("A", 0xD000, 12288)
        self.assertEqual(self.transport.calls, before)
        with self.assertRaises(hw.HardwareError):
            self.backend.close("A")

    def test_raw_exception_details_never_escape(self):
        self.transport.fail = "command"
        with self.assertRaisesRegex(hw.HardwareError, "^hardware_operation_failed$"):
            self.backend.read("A", 0xD000, 12288)


if __name__ == "__main__":
    unittest.main()
