"""Host-only identity/lease/cleanup checks with fabricated transport identities."""
import dataclasses
import hashlib
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import noise_xk_bound_backend as m


def fake_identity(last="01", separator=":"):
    return separator.join(("02", "00", "00", "00", "00", last))


class Handle:
    def __init__(self):
        self.is_open = False
        self.fail_close = False
    def open(self):
        self.is_open = True
    def close(self):
        if self.fail_close:
            raise RuntimeError("private_transport_detail")
        self.is_open = False


class Fixture:
    def __init__(self):
        self.calls = []
        self.handles = []
        self.records = [SimpleNamespace(device="FAKE-A", serial_number=fake_identity(), vid=0x303a, pid=0x1001),
                        SimpleNamespace(device="FAKE-B", serial_number=fake_identity("02"), vid=0x303a, pid=0x1001)]
        self.bindings = tuple(m.RoleBinding(role, p.device, p.serial_number,
            m.coordinator.RestoreDescriptor(role + ".bin", 16, hashlib.sha256(digit.encode() * 16).hexdigest()))
            for role, p, digit in zip(("A", "B"), self.records, ("a", "b")))
        self.fail = None
        def operation(kind, route, *args):
            self.calls.append((kind, route))
            if self.fail == kind:
                raise RuntimeError("private_transport_detail")
        self.transport = SimpleNamespace(
            hard_reset=lambda route: operation("reset", route),
            write_application=lambda *args: operation("write", *args),
            verify_application=lambda *args: operation("verify", *args),
            _serial=SimpleNamespace(Serial=self.serial))
        self.backend = m.BoundBackend(self.bindings, inventory=lambda: self.records,
            transport=self.transport, rom_reader=self.rom)
    def serial(self, **kwargs):
        handle = Handle()
        self.handles.append(handle)
        return handle
    def image(self, role="A"):
        payload = (b"a" if role == "A" else b"b") * 16
        return SimpleNamespace(size=16, sha256=hashlib.sha256(payload).hexdigest(), payload=payload)
    def rom(self, route):
        self.calls.append(("rom", route))
        if self.fail == "rom":
            raise RuntimeError("private_transport_detail")
        return "esptool v5.0.1\nMAC:                " + next(p.serial_number for p in self.records if p.device == route) + "\n"
    def admit(self):
        for b in self.bindings:
            self.backend.admit_rom(b.private_route)
        self.calls.clear()


class Tests(unittest.TestCase):
    def test_strict_normalization_and_rom_parser(self):
        for value in (fake_identity(separator=""), fake_identity(), fake_identity(separator="-")):
            self.assertEqual(m.normalize_identity(value), fake_identity(separator=""))
        for value in (" " + fake_identity(separator=""), fake_identity().replace(":", "-", 1), "xx" + fake_identity(separator=""), None):
            with self.assertRaises(m.BackendError):
                m.normalize_identity(value)
        for value in ("", "MAC: bad", "MAC: " + fake_identity() + "\nMAC: " + fake_identity()):
            with self.assertRaises(m.BackendError):
                m.parse_rom_identity(value)

    def test_immutable_private_binding_and_role_checks(self):
        f = Fixture()
        self.assertNotIn("FAKE-A", repr(f.bindings[0]))
        with self.assertRaises(dataclasses.FrozenInstanceError):
            f.bindings[0].role = "B"
        self.assertFalse(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        f.admit()
        self.assertTrue(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        self.assertEqual(f.calls, [])
        self.assertFalse(f.backend.verify_role("FAKE-A", "B", f.bindings[1].restore))

    def test_passive_swap_revokes_even_if_routes_return(self):
        f = Fixture(); f.admit()
        f.records[0].serial_number, f.records[1].serial_number = f.records[1].serial_number, f.records[0].serial_number
        self.assertFalse(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        f.records[0].serial_number, f.records[1].serial_number = f.records[1].serial_number, f.records[0].serial_number
        self.assertFalse(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        self.assertTrue(f.backend.verify_role("FAKE-B", "B", f.bindings[1].restore))
        self.assertEqual(f.calls, [])

    def test_duplicate_descriptor_or_route_denied(self):
        for duplicate in ("route", "identity"):
            f = Fixture(); f.admit()
            p = f.records[0]
            f.records.append(SimpleNamespace(device=p.device if duplicate == "route" else "FAKE-C",
                serial_number=p.serial_number, vid=p.vid, pid=p.pid))
            self.assertFalse(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))

    def test_rom_failure_always_attempts_runtime_cleanup(self):
        f = Fixture(); f.fail = "rom"
        with self.assertRaisesRegex(m.BackendError, "^rom_admission_failed$"):
            f.backend.admit_rom("FAKE-A")
        self.assertEqual(f.calls, [("rom", "FAKE-A"), ("reset", "FAKE-A")])
        self.assertEqual(f.backend._admitted, set())

    def test_rom_mismatch_and_cleanup_failure_never_admit(self):
        f = Fixture()
        f.backend._rom_reader = lambda route: "MAC: " + fake_identity("ff")
        with self.assertRaises(m.BackendError): f.backend.admit_rom("FAKE-A")
        self.assertEqual(f.calls, [("reset", "FAKE-A")])
        self.assertEqual(f.backend._admitted, set())
        f = Fixture(); f.fail = "reset"
        with self.assertRaises(m.BackendError): f.backend.admit_rom("FAKE-A")
        self.assertEqual(f.backend._admitted, set())

    def test_readback_failure_restores_runtime_and_allows_recovery(self):
        f = Fixture(); f.admit(); f.fail = "verify"
        with self.assertRaisesRegex(m.BackendError, "^application_operation_failed$"):
            f.backend.verify_application("FAKE-A", 0x10000, f.image())
        self.assertEqual(f.calls, [("verify", "FAKE-A"), ("reset", "FAKE-A")])
        self.assertTrue(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        f.fail = None
        f.backend.write_application("FAKE-A", 0x10000, f.image())
        self.assertEqual(f.calls[-1:], [("write", "FAKE-A")])

    def test_no_written_image_boots_until_verified_explicit_reset(self):
        for fail in (None, "write"):
            f = Fixture(); f.admit(); f.fail = fail
            if fail:
                with self.assertRaises(m.BackendError):
                    f.backend.write_application("FAKE-A", 0x10000, f.image())
            else:
                f.backend.write_application("FAKE-A", 0x10000, f.image())
            self.assertEqual(f.calls, [("write", "FAKE-A")])
            with self.assertRaises(m.BackendError): f.backend.hard_reset("FAKE-A")
            with self.assertRaises(m.BackendError): f.backend.admit_rom("FAKE-A")
            f.fail = "verify"
            with self.assertRaises(m.BackendError):
                f.backend.verify_application("FAKE-A", 0x10000, f.image())
            self.assertNotIn(("reset", "FAKE-A"), f.calls)
            f.fail = None
            f.backend.write_application("FAKE-A", 0x10000, f.image())
            f.backend.verify_application("FAKE-A", 0x10000, f.image())
            self.assertNotIn(("reset", "FAKE-A"), f.calls)
            f.backend.hard_reset("FAKE-A")
            self.assertEqual(f.calls[-1], ("reset", "FAKE-A"))

    def test_failed_reopen_close_keeps_old_handle_lease(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        f.handles[0].fail_close = True
        with self.assertRaises(Exception): endpoint.reopen()
        self.assertIsNone(endpoint._endpoint)
        with self.assertRaises(m.BackendError): endpoint.close()
        with self.assertRaises(m.BackendError): f.backend.hard_reset("FAKE-B")
        self.assertTrue(f.handles[0].is_open)
        self.assertEqual(f.calls, [])

    def test_changed_route_during_readback_does_not_reset_replacement(self):
        f = Fixture(); f.admit()
        f.transport.verify_application = lambda *args: setattr(f.records[0], "serial_number", fake_identity("ff"))
        with self.assertRaises(m.BackendError): f.backend.verify_application("FAKE-A", 0x10000, f.image())
        self.assertEqual(f.calls, [])
        self.assertEqual(f.backend._admitted, {"B"})

    def test_missing_a_does_not_prevent_independent_b_recovery(self):
        f = Fixture(); f.admit()
        f.records = [f.records[1]]
        self.assertFalse(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        self.assertFalse(f.backend.verify_role("FAKE-UNKNOWN", "B", f.bindings[1].restore))
        self.assertTrue(f.backend.verify_role("FAKE-B", "B", f.bindings[1].restore))
        f.backend.write_application("FAKE-B", 0x10000, f.image("B"))
        f.backend.verify_application("FAKE-B", 0x10000, f.image("B"))
        f.backend.hard_reset("FAKE-B")
        self.assertEqual(f.calls, [("write", "FAKE-B"), ("verify", "FAKE-B"), ("reset", "FAKE-B")])

    def test_any_live_radio_lease_blocks_all_rom_operations(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        self.assertIsInstance(endpoint, m.BufferedReconnectableEndpoint)
        self.assertFalse(f.handles[0].dtr); self.assertFalse(f.handles[0].rts)
        for action in (lambda: f.backend.admit_rom("FAKE-B"),
                       lambda: f.backend.hard_reset("FAKE-B"),
                       lambda: f.backend.verify_application("FAKE-B", 0x10000, f.image()),
                       lambda: f.backend.write_application("FAKE-B", 0x10000, f.image())):
            with self.assertRaisesRegex(m.BackendError, "^radio_lease_active$"): action()
        self.assertEqual(f.calls, [])
        self.assertTrue(f.backend.verify_role("FAKE-A", "A", f.bindings[0].restore))
        endpoint.close()
        f.backend.hard_reset("FAKE-B")
        self.assertEqual(f.calls, [("reset", "FAKE-B")])

    def test_reopen_fresh_handle_passive_check_and_failed_close_lease(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        endpoint._sleep = lambda unused: None
        endpoint.reopen()
        self.assertEqual(len(f.handles), 2)
        self.assertFalse(f.handles[0].is_open)
        self.assertEqual(f.calls, [])
        f.handles[-1].fail_close = True
        with self.assertRaises(m.BackendError): endpoint.close()
        with self.assertRaises(m.BackendError): f.backend.hard_reset("FAKE-B")
        with self.assertRaises(m.BackendError): endpoint.close()
        self.assertEqual(f.calls, [])

    def test_endpoint_route_change_rejected_before_command_or_reopen(self):
        f = Fixture(); f.admit()
        endpoint = f.backend.open_radio_endpoint("FAKE-A")
        f.records[0].serial_number = fake_identity("ff")
        with self.assertRaises(m.BackendError): endpoint.write_command("reset")
        with self.assertRaises(m.BackendError): endpoint.reopen()
        self.assertEqual(len(f.handles), 1)
        endpoint.close()

    def test_old_closed_endpoint_cannot_release_new_endpoint_lease(self):
        f = Fixture(); f.admit()
        prior = f.backend.open_radio_endpoint("FAKE-A")
        prior.close()
        current = f.backend.open_radio_endpoint("FAKE-A")
        prior.close()
        with self.assertRaises(m.BackendError): f.backend.hard_reset("FAKE-B")
        self.assertTrue(f.handles[-1].is_open)
        current.close()

    def test_default_rom_command_is_bounded_and_output_not_exposed(self):
        f = Fixture(); f.transport._python = "fake-python"
        with patch.object(m.subprocess, "run", return_value=SimpleNamespace(returncode=0,
                stdout=("MAC: " + fake_identity() + "\n").encode("ascii"))) as run:
            self.assertEqual(m.parse_rom_identity(f.backend._read_rom("FAKE-A")), fake_identity(separator=""))
        args, kwargs = run.call_args
        self.assertIn("--no-stub", args[0]); self.assertEqual(args[0][-1], "read-mac")
        self.assertEqual(kwargs["timeout"], 30)

    def test_preflight_readback_accepts_only_bound_restore_without_side_effects(self):
        f = Fixture(); f.admit()
        with self.assertRaisesRegex(m.BackendError, "^application_image_mismatch$"):
            f.backend.verify_application("FAKE-A", 0x10000, f.image("B"))
        for offset, image in ((0, f.image()), (0x10000, SimpleNamespace(size=16, sha256="a" * 64, payload=b"wrong"))):
            with self.assertRaises(m.BackendError):
                f.backend.write_application("FAKE-A", offset, image)
        self.assertEqual(f.calls, [])
        self.assertEqual(f.backend._written, set())

    def test_verify_must_match_last_attempted_write_before_explicit_boot(self):
        f = Fixture(); f.admit()
        f.backend.write_application("FAKE-A", 0x10000, f.image("B"))
        with self.assertRaises(m.BackendError):
            f.backend.verify_application("FAKE-A", 0x10000, f.image("A"))
        with self.assertRaises(m.BackendError): f.backend.hard_reset("FAKE-A")
        self.assertEqual(f.calls, [("write", "FAKE-A")])
        f.backend.verify_application("FAKE-A", 0x10000, f.image("B"))
        f.backend.hard_reset("FAKE-A")
        self.assertEqual(f.calls, [("write", "FAKE-A"), ("verify", "FAKE-A"), ("reset", "FAKE-A")])

    def test_default_transport_requires_pinned_pyserial_without_host_import(self):
        f = Fixture()
        with patch.object(m.importlib.metadata, "version", return_value="3.4"), patch.object(
                m.lifecycle.frozen_adapter, "EsptoolSerialBackend") as transport:
            with self.assertRaises(m.BackendError):
                m.BoundBackend(f.bindings, inventory=lambda: f.records)
            transport.assert_not_called()
        with patch.object(m.importlib.metadata, "version", return_value="3.5"), patch.object(
                m.lifecycle.frozen_adapter, "EsptoolSerialBackend", return_value=f.transport) as transport:
            backend = m.BoundBackend(f.bindings, inventory=lambda: f.records)
            transport.assert_called_once_with(("FAKE-A", "FAKE-B"))
            self.assertEqual(backend._admitted, set())


if __name__ == "__main__":
    unittest.main()

