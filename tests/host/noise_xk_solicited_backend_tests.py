"""Isolated backend wiring and ROM normalization; fabricated serial peers only."""
from collections import deque
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_bound_backend as previous
import noise_xk_solicited_backend as m
import noise_xk_ready_runner as receipts
from noise_xk_solicited_runtime import SolicitedReconnectableEndpoint

spec = importlib.util.spec_from_file_location("_solicited_backend_fixture", ROOT / "tests/host/noise_xk_bound_backend_tests.py")
assert spec and spec.loader
helpers = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = helpers
spec.loader.exec_module(helpers)


class Handle(helpers.Handle):
    def __init__(self):
        super().__init__()
        self.chunks = deque()
        self.writes = []
    def write(self, raw):
        self.writes.append(raw)
        challenge = raw.decode().strip().split()[1]
        self.chunks.append(("OT153 READY schema=OTNXREADY1 challenge=" + challenge
            + " accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no\n").encode())
        return len(raw)
    def flush(self): pass
    def readline(self, size): return self.chunks.popleft() if self.chunks else b""


class Fixture(helpers.Fixture):
    def __init__(self):
        super().__init__()
        self.bindings = tuple(m.RoleBinding(b.role, b.private_route, b.private_identity,
            m.coordinator.RestoreDescriptor(b.restore.name, b.restore.bytes, b.restore.sha256))
            for b in self.bindings)
        self.backend = m.BoundBackend(self.bindings, inventory=lambda: self.records,
            transport=self.transport, rom_reader=self.rom)
    def serial(self, **kwargs):
        handle = Handle()
        self.handles.append(handle)
        return handle


class Tests(unittest.TestCase):
    def test_one_or_two_agreeing_rom_lines_normalize_exactly(self):
        expected = helpers.fake_identity(separator="")
        for output in ("boot\nMAC: " + helpers.fake_identity() + "\n",
                       "MAC: " + helpers.fake_identity() + "\r\nMAC:     " + helpers.fake_identity(separator="-") + "\n",
                       "mac:\t" + expected.upper() + "\nMAC: " + expected + "\n"):
            self.assertEqual(m.canonical_rom_output(output), "MAC: " + expected + "\n")
            self.assertEqual(m.parse_rom_identity(output), expected)

    def test_conflicting_malformed_extra_rom_lines_rejected(self):
        valid = "MAC: " + helpers.fake_identity() + "\n"
        for output in (None, "", valid * 3, valid + "MAC: " + helpers.fake_identity("ff") + "\n",
                       valid + "MAC: broken\n", valid + "MAC:\n", "MAC:\n" + valid,
                       valid + "MAC: \t\n", valid + "MAC unexpected\n",
                       "MAC: " + helpers.fake_identity() + " extra\n"):
            with self.assertRaises(m.BackendError): m.parse_rom_identity(output)

    def test_private_module_configuration_does_not_mutate_old_backend(self):
        self.assertIsNot(m._prior, previous)
        self.assertIsNot(m.coordinator.RestoreDescriptor, previous.coordinator.RestoreDescriptor)
        self.assertIs(m._prior.coordinator, m.coordinator)
        self.assertIsNot(previous._BoundEndpoint, m._BoundSolicitedEndpoint)
        with self.assertRaises(previous.BackendError):
            previous.parse_rom_identity(("MAC: " + helpers.fake_identity() + "\n") * 2)
        f = Fixture(); f.admit()
        self.assertTrue(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        old_descriptors = tuple(m.RoleBinding(b.role, b.private_route, b.private_identity,
            previous.coordinator.RestoreDescriptor(b.restore.name, b.restore.bytes, b.restore.sha256))
            for b in f.bindings)
        with self.assertRaises(m.BackendError):
            m.BoundBackend(old_descriptors, inventory=lambda: f.records, transport=f.transport)

    def test_dual_rom_admission_and_failure_cleanup_use_new_parser(self):
        f = Fixture()
        f.backend._rom_reader = lambda route: ("MAC: " + f.records[0].serial_number + "\n") * 2
        f.backend.admit_rom("FAKE-A")
        self.assertEqual(f.backend._admitted, {"A"})
        self.assertEqual(f.calls, [("reset", "FAKE-A")])
        f.backend._rom_reader = lambda route: ("MAC: " + f.records[0].serial_number + "\n") * 3
        with self.assertRaisesRegex(m.BackendError, "^rom_admission_failed$"):
            f.backend.admit_rom("FAKE-A")
        self.assertEqual(f.calls, [("reset", "FAKE-A"), ("reset", "FAKE-A")])
        self.assertEqual(f.backend._admitted, set())

    def test_query_is_guarded_and_uses_shared_receipt_identity_with_no_rom(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        self.assertIsInstance(endpoint, SolicitedReconnectableEndpoint)
        observed = []
        actual = f.backend._require
        def require(binding):
            observed.append(f.backend._lock._is_owned())
            return actual(binding)
        with mock.patch.object(f.backend, "_require", side_effect=require):
            token, receipt = endpoint.query_ready(challenge_factory=lambda: format(1, "032x"))
        self.assertEqual(observed, [True])
        self.assertIs(type(receipt), receipts.predecessor.frozen.Receipt)
        self.assertEqual(receipt.fields["challenge"], token)
        self.assertEqual(f.calls, [])
        for action in (lambda: f.backend.admit_rom("FAKE-B"), lambda: f.backend.hard_reset("FAKE-B"),
                       lambda: f.backend.write_application("FAKE-B", 0x10000, f.image("B"))):
            with self.assertRaisesRegex(m.BackendError, "^radio_lease_active$"): action()
        endpoint.close()

    def test_changed_identity_blocks_query_before_serial_write(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        f.records[0].serial_number = helpers.fake_identity("ff")
        with self.assertRaisesRegex(m.BackendError, "^passive_identity_mismatch$"):
            endpoint.query_ready()
        self.assertEqual(f.handles[0].writes, [])
        self.assertEqual(f.backend._admitted, {"B"})
        endpoint.close()

    def test_reopen_has_fresh_query_and_failed_close_keeps_global_lease(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        endpoint.query_ready(challenge_factory=lambda: format(1, "032x"))
        endpoint._sleep = lambda seconds: None
        endpoint.reopen()
        endpoint.query_ready(challenge_factory=lambda: format(2, "032x"))
        self.assertEqual(len(f.handles), 2)
        self.assertEqual([len(h.writes) for h in f.handles], [1, 1])
        f.handles[-1].fail_close = True
        with self.assertRaises(Exception): endpoint.reopen()
        with self.assertRaises(m.BackendError): endpoint.close()
        with self.assertRaisesRegex(m.BackendError, "^radio_lease_active$"): f.backend.hard_reset("FAKE-B")
        self.assertEqual(f.calls, [])

    def test_readback_and_write_guards_preserved(self):
        f = Fixture(); f.admit()
        f.backend.verify_application("FAKE-A", 0x10000, f.image())
        self.assertEqual(f.calls, [("verify", "FAKE-A"), ("reset", "FAKE-A")])
        f.calls.clear(); f.fail = "write"
        with self.assertRaises(m.BackendError): f.backend.write_application("FAKE-A", 0x10000, f.image())
        with self.assertRaisesRegex(m.BackendError, "^unverified_write_pending$"): f.backend.hard_reset("FAKE-A")
        self.assertEqual(f.calls, [("write", "FAKE-A")])
        f.fail = None
        f.backend.write_application("FAKE-A", 0x10000, f.image())
        f.backend.verify_application("FAKE-A", 0x10000, f.image())
        f.backend.hard_reset("FAKE-A")
        self.assertEqual(f.calls[-3:], [("write", "FAKE-A"), ("verify", "FAKE-A"), ("reset", "FAKE-A")])


if __name__ == "__main__":
    unittest.main()
