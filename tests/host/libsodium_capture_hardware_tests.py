"""Backend safety regressions under mock USB/ROM, plus full NVS protection.

The inherited tests exercise the unchanged coordinator/lease contract using
synthetic mbedTLS frames; libsodium protocol composition is tested separately.
"""
from pathlib import Path
import dataclasses
import importlib.util
import json
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import libsodium_capture_hardware as h
import mbedtls_comparison_hardware as original

spec = importlib.util.spec_from_file_location(
    "_libsodium_backend_contract_fixtures",
    ROOT / "tests/host/mbedtls_comparison_hardware_tests.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
# This test module is private: existing hardware tests and their module globals
# retain the original backend. Mock ROM reads now include the full NVS span.
fixtures.h = h


class Tests(fixtures.Tests):
    def test_authority_exact_files_scope_and_missing_grant(self):
        files = {}
        for name in ("caller", "package", "preflight", "registry", "hardware_source"):
            path = self.root / (name + ".json")
            path.write_bytes(json.dumps(self.session.package if name == "package" else {"bound": True}).encode())
            files[name] = (path, path.stat().st_size, h.sha(path.read_bytes()))
        expected = {"schema": "libsodium-capture-authority-1", "attempt_count": 1,
                    "radio_allowed": False, "reusable": False,
                    "binding": dataclasses.asdict(self.session.binding),
                    "bound_files": {k: {"bytes": v[1], "sha256": v[2]} for k, v in files.items()}}
        path = self.root / "grant.json"
        path.write_bytes(json.dumps(expected).encode())
        gate = h.AuthorityGate(self.session, path, h.sha(path.read_bytes()), expected, files)
        self.assertFalse(gate.validate(self.session.binding, recovery=False).radio_allowed)
        for key, value in (("schema", "mbedtls-comparison-authority-1"),
                           ("attempt_count", True), ("radio_allowed", True), ("reusable", True)):
            with self.subTest(key=key):
                with self.assertRaises(h.HardwareError):
                    h.AuthorityGate(self.session, path, h.sha(path.read_bytes()),
                                    dict(expected, **{key: value}), files)
        raw = path.read_bytes()
        path.unlink()
        with self.assertRaises(h.HardwareError):
            gate.validate(self.session.binding, recovery=False)
        path.write_bytes(raw + b" ")
        with self.assertRaisesRegex(h.HardwareError, "authority_changed"):
            gate.validate(self.session.binding, recovery=True)
        path.write_bytes(raw)
        files["registry"][0].write_bytes(b"changed")
        with self.assertRaisesRegex(h.HardwareError, "authority_file_changed"):
            gate.validate(self.session.binding, recovery=True)

    def test_read_and_write_scope_reject_before_subprocess(self):
        transport = object.__new__(h.RomTransport)
        with patch.object(h._prior.subprocess, "run") as run:
            for offset, size in ((0xD000, 4096), (0xD000, 0x3001), (0xD001, 0x3000),
                                 (0x9000, 589824), (65536, 589825)):
                with self.subTest(offset=offset, size=size):
                    with self.assertRaises(h.HardwareError):
                        transport.read("mock", offset, size)
            with self.assertRaises(h.HardwareError):
                transport.write("mock", b"x" * 589825)
            run.assert_not_called()

    def test_serial_flush_deadline_and_constructor_write_timeout(self):
        # The inherited test patches the predecessor's timing module.
        with patch.object(fixtures, "h", h._prior):
            super().test_serial_flush_deadline_and_constructor_write_timeout()

    def test_predecessor_bytes_and_module_remain_unchanged(self):
        self.assertEqual(h.sha(h.PREDECESSOR_PATH.read_bytes()), h.PREDECESSOR_SHA256)
        self.assertEqual(set(original.Backend.REGIONS), {"bootloader", "partition", "ota"})
        self.assertIsNot(h.Backend, original.Backend)
        self.assertEqual(h.Backend.REGIONS["nvs"], (0xD000, 0x3000))

    def test_source_change_and_transform_drift_fail_closed(self):
        with patch.object(Path, "read_bytes", return_value=b"raise AssertionError('must not execute')"):
            with self.assertRaisesRegex(RuntimeError, "source_mismatch"):
                h._load()
        with patch.object(h, "TRANSFORMATIONS", (("absent-token", "replacement"),)):
            with self.assertRaisesRegex(RuntimeError, "source_mismatch"):
                h._load()

    def test_exact_full_nvs_read_allowed_and_application_only_write(self):
        transport = object.__new__(h.RomTransport)
        commands = []
        def command(route, operation):
            commands.append(operation)
            if operation[0] == "read-flash":
                Path(operation[-1]).write_bytes(b"n" * int(operation[2]))
            else:
                self.assertEqual(operation[:4], ["write-flash", "--flash-size", "16MB", "0x10000"])
                self.assertEqual(Path(operation[-1]).read_bytes(), b"application")
        with patch.object(transport, "command", side_effect=command):
            self.assertEqual(transport.read("mock", 0xD000, 0x3000), b"n" * 0x3000)
            transport.write("mock", b"application")
        self.assertEqual(commands[0][:3], ["read-flash", "0xd000", "12288"])

    def test_every_nvs_page_protected_before_write(self):
        self.admit()
        image = self.c.Image(self.package["images"][0]["name"], self.raw[0], h.sha(self.raw[0]))
        original_nvs = self.protected["A"]["nvs"]
        for index in (0, 4096, 8192, 12287):
            with self.subTest(index=index):
                altered = bytearray(original_nvs)
                altered[index] ^= 1
                self.protected["A"]["nvs"] = bytes(altered)
                before = len(self.rom.events)
                with self.assertRaisesRegex(h.HardwareError, "protected_region_changed"):
                    self.hardware.write_application(self.routes[0], 65536, image)
                self.assertFalse(any(e[0] in ("write", "reset") for e in self.rom.events[before:]))
        self.protected["A"]["nvs"] = original_nvs

    def test_missing_or_partial_nvs_descriptor_rejected(self):
        for missing in (True, False):
            descriptors = json.loads(json.dumps(self.hardware.regions))
            if missing:
                del descriptors["A"]["nvs"]
            else:
                descriptors["A"]["nvs"]["bytes"] = 4096
            with self.assertRaisesRegex(h.HardwareError, "protected_regions_invalid"):
                h.Backend(self.session, self.bindings, descriptors, inventory=lambda: self.records, transport=self.rom)
        self.assertEqual(self.rom.events, [])

    def test_nvs_change_after_write_blocks_verified_reset_and_recovery(self):
        self.admit()
        image = self.c.Image(self.package["images"][0]["name"], self.raw[0], h.sha(self.raw[0]))
        self.hardware.write_application(self.routes[0], 65536, image)
        self.protected["A"]["nvs"] = b"x" * 12288
        with self.assertRaisesRegex(h.HardwareError, "protected_region_changed"):
            self.hardware.verify_application(self.routes[0], 65536, image)
        with self.assertRaisesRegex(h.HardwareError, "unverified_application"):
            self.hardware.hard_reset(self.routes[0])
        with self.assertRaisesRegex(h.HardwareError, "protected_region_changed"):
            self.hardware.admit(self.routes[0], recovery=True)

    def test_roles_offsets_and_cross_role_restore_rejected(self):
        with self.assertRaisesRegex(h.HardwareError, "roles_invalid"):
            h.Backend(self.session, self.bindings[::-1], self.hardware.regions,
                      inventory=lambda: self.records, transport=self.rom)
        self.admit()
        wrong = self.c.Image(self.paths[2].name, self.raw[2], h.sha(self.raw[2]))
        with self.assertRaisesRegex(h.HardwareError, "image_not_bound"):
            self.hardware.write_application(self.routes[0], 65536, wrong)
        image = self.c.Image(self.package["images"][0]["name"], self.raw[0], h.sha(self.raw[0]))
        for offset in (0xD000, 0, True, 65537):
            with self.assertRaisesRegex(h.HardwareError, "offset_invalid"):
                self.hardware.write_application(self.routes[0], offset, image)
        self.assertFalse(any(e[0] == "write" for e in self.rom.events))


class LibsodiumCompositionTests(unittest.TestCase):
    """Actual libsodium owner/protocol/backend over only mocked ROM and USB."""

    def setUp(self):
        # Reuse the ROM model, protected bytes and distinct original fixtures;
        # replace its session and serial data with the new capture composition.
        fixtures.Tests.setUp(self)
        import libsodium_capture_execution as execution
        self.execution = execution
        patch.object(execution, "BENCHMARK", execution.descriptor(self.paths[0])).start()
        patch.object(execution, "ORIGINALS", tuple(execution.descriptor(p) for p in self.paths[1:])).start()
        self.package = execution.freeze_package(*self.paths)
        self.session = execution.Session(self.package)
        self.c = self.session.coordinator
        self.c.ROOT = self.root
        self.c.PRIVATE_ROOT = self.root / ".private"
        for attr, suffix in (("JOURNAL_PATH", "journal"), ("EXECUTION_RECEIPT_PATH", "execution"),
                             ("RECOVERY_RECEIPT_PATH", "recovery")):
            setattr(self.c, attr, self.c.PRIVATE_ROOT / ("libsodium-capture-3-" + suffix + ".json"))
        self.bindings = tuple(h.RoleBinding(role, route, ident, descriptor)
                              for role, route, ident, descriptor in zip(
                                  ("A", "B"), self.routes, self.ids,
                                  (self.session.binding.restore_a, self.session.binding.restore_b)))
        self.static = {role: {k: v for k, v in regions.items() if k != 'nvs'}
                       for role, regions in self.hardware.regions.items()}
        self.hardware = h.IntervalBackend(self.session, self.bindings, self.static,
                                  interval_root=self.c.PRIVATE_ROOT,
                                  inventory=lambda: self.records, transport=self.rom)
        self.config = self.session.config(self.routes)
        frame_spec = importlib.util.spec_from_file_location(
            "_libsodium_concrete_frames", ROOT / "tests/host/ot121_local_primitive_frame_tests.py")
        frame_fixtures = importlib.util.module_from_spec(frame_spec)
        frame_spec.loader.exec_module(frame_fixtures)
        self.capture = frame_fixtures._encode(frame_fixtures._valid_records())
        self.handles = []
        case = self

        def open_serial(route):
            case.rom.events.append(("open", route))
            class Handle:
                def __init__(self):
                    self.raw = case.capture
                    self.is_open = True

                def read(self, size):
                    if not self.raw:
                        raise RuntimeError("mock stream ended")
                    chunk, self.raw = self.raw[:size], self.raw[size:]
                    return chunk

                def write(self, raw):
                    raise AssertionError("autorun must never write controls")

                def close(self):
                    self.is_open = False
            handle = Handle()
            case.handles.append(handle)
            return handle
        patch.object(self.rom, "open", side_effect=open_serial).start()
        for route in self.routes:
            self.hardware.admit(route)
        self.original_apps = dict(self.rom.apps)
        self.original_protected = {role: dict(regions) for role, regions in self.protected.items()}

    def assert_restored(self):
        self.assertEqual(self.rom.apps, self.original_apps)
        self.assertEqual(self.protected, self.original_protected)
        self.assertTrue(self.hardware.assert_idle())
        self.assertTrue(all(not handle.is_open for handle in self.handles))

    def test_actual_libsodium_session_backend_capture_restore_and_audit(self):
        result = self.session.execute(self.config, self.hardware, self.authority)
        self.assertEqual(result["result"], "two_node_libsodium_passed_and_restored")
        self.assertTrue(result["restoration_complete"])
        self.assert_restored()
        self.assertEqual([event[1] for event in self.rom.events if event[0] == "write"],
                         [self.routes[0], self.routes[0], self.routes[1], self.routes[1]])
        self.assertEqual({role: value["frame_count"] for role, value in result["validated_capture_custody"].items()},
                         {"A": 1621, "B": 1621})
        self.assertTrue(all(node["capture_diagnostics"]["start_write_attempts"] == 0 for node in result["nodes"]))
        audit = self.execution.audit_receipt(self.package, result, self.c.PRIVATE_ROOT,
                                             expected_authority_sha="a" * 64)
        self.assertEqual(audit["result"], "passed")

    def test_actual_libsodium_capture_failure_restores_exact_touched_original(self):
        self.capture = self.capture.splitlines(keepends=True)[0] + b"invalid frame\n"
        with self.assertRaises(self.c.CoordinatorError):
            self.session.execute(self.config, self.hardware, self.authority)
        self.assert_restored()
        self.assertEqual([event[1] for event in self.rom.events if event[0] == "write"],
                         [self.routes[0], self.routes[0]])
        result = json.loads(self.c.EXECUTION_RECEIPT_PATH.read_bytes())
        self.assertTrue(result["restoration_complete"])
        self.assertEqual(result["validated_capture_custody"], {})
        self.assertEqual(result["failure"]["capture_code"], "frame_malformed")

    def test_original_runtime_nvs_change_after_release_is_not_rechecked(self):
        reset = self.rom.reset
        released = set()
        read = self.rom.read
        def runtime_reset(route):
            reset(route)
            if self.rom.apps[route] == self.original_apps[route]:
                role = ('A', 'B')[self.routes.index(route)]
                self.protected[role]['nvs'] = b'r' * 12288
                released.add(route)
        def observed_read(route, offset, size):
            self.assertNotIn(route, released, 'released original was accessed again')
            return read(route, offset, size)
        with patch.object(self.rom, 'reset', side_effect=runtime_reset), \
             patch.object(self.rom, 'read', side_effect=observed_read):
            result = self.session.execute(self.config, self.hardware, self.authority)
        self.assertTrue(result['restoration_complete'])
        self.assertEqual(released, set(self.routes))
        self.assertEqual(self.rom.apps, self.original_apps)


class IntervalTests(unittest.TestCase):
    def setUp(self):
        fixtures.Tests.setUp(self)
        self.static = {role: {k: v for k, v in regions.items() if k != 'nvs'}
                       for role, regions in self.hardware.regions.items()}
        self.interval_root = self.root / 'intervals'
        self.interval_root.mkdir()
        self.hardware = self.make_backend()

    def make_backend(self, **kwargs):
        return h.IntervalBackend(self.session, self.bindings, self.static,
                                 interval_root=self.interval_root, inventory=lambda: self.records,
                                 transport=self.rom, **kwargs)

    def image(self, index):
        return self.c.Image(self.paths[index].name, self.raw[index], h.sha(self.raw[index]))

    def test_fresh_nvs_double_read_is_bound_without_original_runtime(self):
        self.protected['A']['nvs'] = b'f' * 12288
        self.hardware.admit(self.routes[0])
        reads = [e for e in self.rom.events if e[0] == 'read' and e[2] == 0xD000]
        self.assertEqual(len(reads), 2)
        self.assertFalse(any(e[0] == 'reset' for e in self.rom.events))
        self.assertTrue(self.hardware.interval_ready(self.routes[0]))
        with self.assertRaisesRegex(h.HardwareError, 'interval_not_closed'):
            self.hardware.hard_reset(self.routes[0])
        self.assertEqual(len(self.hardware.baseline_digest(self.routes[0])), 64)
        with self.assertRaisesRegex(h.HardwareError, 'interval_namespace_used'):
            self.hardware.admit(self.routes[0])

    def test_unstable_fresh_nvs_has_no_baseline_or_write(self):
        original = self.rom.read
        count = 0
        def read(route, offset, size):
            nonlocal count
            raw = original(route, offset, size)
            if offset == 0xD000:
                count += 1
                return bytes([count]) * size
            return raw
        with patch.object(self.rom, 'read', side_effect=read):
            with self.assertRaisesRegex(h.HardwareError, 'fresh_nvs_unstable'):
                self.hardware.admit(self.routes[0])
        self.assertEqual(list(self.interval_root.iterdir()), [])
        self.assertFalse(any(e[0] in ('write', 'reset') for e in self.rom.events))

    def test_static_or_original_failure_never_acquires_baseline(self):
        self.protected['A']['partition'] = b'x' * 4096
        with self.assertRaisesRegex(h.HardwareError, 'protected_region_changed'):
            self.hardware.admit(self.routes[0])
        self.assertEqual(list(self.interval_root.iterdir()), [])

    def test_candidate_corruption_blocks_close_and_second_write(self):
        route = self.routes[0]
        self.hardware.admit(route)
        candidate = self.image(0)
        self.hardware.write_application(route, 65536, candidate)
        self.hardware.verify_application(route, 65536, candidate)
        self.hardware.hard_reset(route)
        with self.assertRaisesRegex(h.HardwareError, 'candidate_already_started'):
            self.hardware.write_application(route, 65536, candidate)
        self.protected['A']['nvs'] = b'x' * 12288
        with self.assertRaisesRegex(h.HardwareError, 'protected_region_changed'):
            self.hardware.write_application(route, 65536, self.image(1))
        self.assertFalse(self.hardware.release_untouched(route))

    def test_restore_closed_before_release_and_postruntime_epoch_is_separate(self):
        route = self.routes[0]
        self.hardware.admit(route)
        self.hardware.write_application(route, 65536, self.image(0))
        self.hardware.verify_application(route, 65536, self.image(0))
        self.hardware.hard_reset(route)
        self.hardware.write_application(route, 65536, self.image(1))
        self.hardware.verify_application(route, 65536, self.image(1))
        with self.assertRaisesRegex(h.HardwareError, 'interval_not_closed'):
            self.hardware.hard_reset(route)
        self.hardware.close_interval(route)
        self.hardware.hard_reset(route)
        self.protected['A']['nvs'] = b'r' * 12288
        before = list(self.rom.events)
        self.assertTrue(self.hardware.resume_release(route))
        self.assertTrue(self.hardware.release_untouched(route))
        self.assertEqual(self.rom.events, before)

    def test_release_crash_recovers_exact_original_without_rebaselining(self):
        route = self.routes[0]
        self.hardware.admit(route)
        digest = self.hardware.baseline_digest(route)
        self.hardware.close_interval(route)
        original_marker = self.hardware._marker
        def marker(binding, suffix, **kwargs):
            if suffix == 'released' and kwargs.get('create'):
                raise OSError('simulated_crash_after_runtime_start')
            return original_marker(binding, suffix, **kwargs)
        with patch.object(self.hardware, '_marker', side_effect=marker):
            with self.assertRaises(h.HardwareError):
                self.hardware.hard_reset(route)
        self.assertEqual(self.hardware.interval_state(route), 'release_pending')
        self.protected['A']['nvs'] = b'r' * 12288
        recovered = self.make_backend(recovery_baselines={'A': digest})
        recovered.admit(route, recovery=True)
        self.assertTrue(recovered.resume_release(route))
        self.assertEqual(recovered.baseline_digest(route), digest)
        self.assertFalse(any(e[0] == 'write' for e in self.rom.events))

    def test_recovery_requires_external_digest_and_identity_binding(self):
        route = self.routes[0]
        self.hardware.admit(route)
        digest = self.hardware.baseline_digest(route)
        for pins in ({}, {'A': '0' * 64}):
            with self.assertRaisesRegex(h.HardwareError, 'interval_baseline_changed'):
                self.make_backend(recovery_baselines=pins).admit(route, recovery=True)
        recovered = self.make_backend(recovery_baselines={'A': digest})
        recovered.admit(route, recovery=True)
        path = recovered._path('A', 'baseline')
        raw = path.read_bytes()
        path.write_bytes(raw.replace(digest.encode(), b'0' * 64) if digest.encode() in raw else raw + b' ')
        with self.assertRaises(h.HardwareError):
            recovered.interval_ready(route)

    def test_untouched_comparison_is_released_without_application_write(self):
        for route in self.routes:
            self.hardware.admit(route)
        self.hardware.write_application(self.routes[0], 65536, self.image(0))
        self.assertFalse(self.hardware.release_untouched(self.routes[0]))
        self.assertTrue(self.hardware.release_untouched(self.routes[1]))
        self.assertFalse(any(e[0] == 'write' and e[1] == self.routes[1] for e in self.rom.events))

    def test_failed_close_proof_persistence_never_releases_original(self):
        route = self.routes[0]
        self.hardware.admit(route)
        with patch.object(self.hardware, '_write_record', side_effect=OSError('disk_failure')):
            with self.assertRaises(h.HardwareError):
                self.hardware.close_interval(route)
        self.assertFalse(any(e[0] == 'reset' for e in self.rom.events))
        self.assertEqual(self.hardware.interval_state(route), 'prepared')

    def test_pending_release_wrong_original_cannot_reset(self):
        route = self.routes[0]
        self.hardware.admit(route)
        self.hardware.close_interval(route)
        self.hardware._marker(self.bindings[0], 'release-intent', create=True)
        digest = self.hardware.baseline_digest(route)
        self.rom.apps[route] = b'x' * 589824
        before = len(self.rom.events)
        with self.assertRaisesRegex(h.HardwareError, 'installed_original_changed'):
            self.make_backend(recovery_baselines={'A': digest}).admit(route, recovery=True)
        self.assertFalse(any(e[0] in ('write', 'reset') for e in self.rom.events[before:]))

    def test_released_role_recovery_is_passive_and_original_open_forbidden(self):
        route = self.routes[0]
        self.hardware.admit(route)
        digest = self.hardware.baseline_digest(route)
        with self.assertRaisesRegex(h.HardwareError, 'candidate_not_verified'):
            self.hardware.open(route)
        self.hardware.release_untouched(route)
        self.protected['A']['nvs'] = b'r' * 12288
        recovered = self.make_backend(recovery_baselines={'A': digest})
        before = list(self.rom.events)
        recovered.admit(route, recovery=True)
        self.assertTrue(recovered.resume_release(route))
        with self.assertRaisesRegex(h.HardwareError, 'interval_released'):
            recovered.verify_application(route, 65536, self.image(1))
        self.assertEqual(self.rom.events, before)

    def test_closed_before_release_intent_recovers_without_application_rewrite(self):
        route = self.routes[0]
        self.hardware.admit(route)
        self.hardware.write_application(route, 65536, self.image(0))
        self.hardware.write_application(route, 65536, self.image(1))
        self.hardware.close_interval(route)
        digest = self.hardware.baseline_digest(route)
        before = len(self.rom.events)
        recovered = self.make_backend(recovery_baselines={'A': digest})
        recovered.admit(route, recovery=True)
        self.assertTrue(recovered.resume_release(route))
        self.assertFalse(any(e[0] == 'write' for e in self.rom.events[before:]))
        self.assertTrue(any(e[0] == 'reset' for e in self.rom.events[before:]))

    def test_baseline_fsync_failure_consumes_namespace_without_candidate_or_reset(self):
        route = self.routes[0]
        with patch('os.fsync', side_effect=OSError('disk_failure')):
            with self.assertRaises(h.HardwareError):
                self.hardware.admit(route)
        self.assertTrue(self.hardware._path('A', 'baseline').exists())
        with self.assertRaises(h.HardwareError):
            self.hardware.baseline_digest(route)
        with self.assertRaisesRegex(h.HardwareError, 'interval_namespace_used'):
            self.hardware.admit(route)
        self.assertFalse(any(e[0] in ('write', 'reset') for e in self.rom.events))


if __name__ == "__main__":
    unittest.main()
