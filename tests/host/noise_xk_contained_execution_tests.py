"""Actual contained admission, byte runtime and independent recovery; no devices."""
import contextlib
import dataclasses
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_contained_execution as contained
import noise_xk_contained_bundle as bundle

spec = importlib.util.spec_from_file_location("_contained_execution_fixture",
    ROOT / "tests/host/noise_xk_diagnostic_execution_tests.py")
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)


class Fixture:
    def __init__(self, partial=False): self.partial = partial
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.stack.enter_context(mock.patch.object(base, "diagnostic", contained))
        x = self.stack.enter_context(base.Fixture(3))
        self.x = x
        old, module = x.f.backend, x.session.backend_module
        bindings = tuple(module.RoleBinding(b.role, b.private_route, b.private_identity, b.restore)
                         for b in old._bindings)
        x.f.backend = module.BoundBackend(bindings, inventory=old._inventory,
            transport=old._transport, rom_reader=old._rom_reader)
        for route in x.f.routes: x.f.backend.admit_rom(route)
        x.f.events.clear()
        original_open = x.f.backend.open_radio_endpoint
        def open_endpoint(route):
            endpoint = original_open(route)
            clock = x.f.boards[route].host_clock
            endpoint._monotonic, endpoint._sleep = clock.monotonic, clock.sleep
            endpoint._endpoint._monotonic = clock.monotonic
            return endpoint
        x.f.backend.open_radio_endpoint = open_endpoint
        x.paths["benchmark"] = bundle.BENCHMARK_NAME
        x.f.benchmark = x.f.root / bundle.BENCHMARK_NAME
        x.f.benchmark.write_bytes(b"benchmark")
        x.f.binding = dataclasses.replace(x.f.binding, benchmark_name=bundle.BENCHMARK_NAME)
        x.f.config = dataclasses.replace(x.f.config, binding=x.f.binding, benchmark_path=x.f.benchmark)
        images = bundle.freeze_images(x.f.root, x.paths)
        record = json.loads((ROOT / bundle.BUILD_RECORD_PATH).read_bytes())
        artifact = {key: images["images"]["benchmark"][key] for key in ("bytes", "sha256")}
        record["artifacts"][bundle.BENCHMARK_NAME] = {"a": artifact, "b": artifact, "byte_identical": True}
        raw = json.dumps(record).encode()
        (x.f.root / bundle.BUILD_RECORD_PATH).write_bytes(raw)
        # CI lacks the hardware image. Only its recorded identity is synthetic;
        # all source records and actual admission/runtime/recovery are exercised.
        self.stack.enter_context(mock.patch.object(bundle, "BUILD_RECORD_SHA256", hashlib.sha256(raw).hexdigest()))
        x.snapshot = {"sources": bundle.freeze_sources(x.f.root), "images": images}
        original = base.base.Board.receipt
        partial = self.partial
        def receipt(board, kind, fields):
            if kind == "TX_DONE":
                tx = {key: fields[key] for key in ("role", "scenario", "message", "result", "start_us", "done_us", "measured_us")}
                original(board, "TX_RETURN", {"schema": "OTNXTXDIAG1", **tx})
                if partial and fields["message"] == "m3": return
                rx = {**tx, "result": fields["rx_restart"], "start_us": fields["done_us"],
                      "done_us": fields["done_us"], "measured_us": "0", "attempted": "yes"}
                original(board, "RX_REARM_RETURN", {"schema": "OTNXTXDIAG1", **rx})
            original(board, kind, fields)
        self.stack.enter_context(mock.patch.object(base.base.Board, "receipt", receipt))
        return x
    def __exit__(self, *args): return self.stack.__exit__(*args)


class Tests(unittest.TestCase):
    def test_complete_byte_run_admission_and_both_restore_images(self):
        with Fixture() as x:
            result = x.execute()
            self.assertTrue(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["radio_result"]["totals"]["fragments"], 14)
            self.assertEqual(result["radio_result"]["totals"]["radio_payload_wire_bytes"], 736)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            snapshot = x.session.diagnostic_snapshot()
            self.assertEqual(set(snapshot["roles"]), {"A", "B"})
            self.assertEqual(sum(len(v["checkpoints"]) for v in snapshot["roles"].values()), 28)
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))
            text = json.dumps(snapshot)
            self.assertNotIn("session_hash", text)
            for board in x.f.boards.values():
                for command in board.commands:
                    for token in command.split()[1:3]:
                        if len(token) in (16, 32): self.assertNotIn(token, text)

    def test_partial_m3_checkpoint_retained_after_timeout_and_restoration(self):
        with Fixture(partial=True) as x:
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            result = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertFalse(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["failure"]["stage"], "cycle1_baseline")
            row = x.session.diagnostic_snapshot()["roles"]["A"]["checkpoints"][-1]
            self.assertEqual((row["kind"], row["message"]), ("TX_RETURN", "m3"))
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)

    def test_extended_source_tamper_fails_before_io(self):
        for relative in ("tools/noise_xk_contained_runtime.py", "tools/radiolib_busy_source.py", bundle.BUILD_RECORD_PATH):
            with Fixture() as x:
                (x.f.root / relative).write_bytes(b"tampered")
                with self.assertRaises(bundle.BundleError): x.execute()
                self.assertEqual(x.f.events, [])
                self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())

    def test_build_record_pin_and_source_pin_cannot_be_bypassed_by_new_snapshot(self):
        for relative, code in ((bundle.BUILD_RECORD_PATH, "build_record_digest_mismatch"),
                               ("tools/radiolib_busy_source.py", "build_source_mismatch")):
            with Fixture() as x:
                path = x.f.root / relative
                path.write_bytes(path.read_bytes() + b" ")
                x.snapshot["sources"] = bundle.freeze_sources(x.f.root)
                with self.assertRaisesRegex(bundle.BundleError, code): x.execute()
                self.assertEqual(x.f.events, [])
                self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())

    def test_recovery_skips_absent_benchmark_and_build_gate_but_requires_sources(self):
        with Fixture() as x:
            x.f.fail_ready, x.f.fail_restore = "A", "A"
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            x.f.benchmark.unlink()
            x.f.fail_restore = None
            before = len([e for e in x.f.events if e[0] == "open"])
            with mock.patch.object(bundle, "BUILD_RECORD_SHA256", "0" * 64):
                recovered = x.recover()
            self.assertTrue(recovered["restoration_complete"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertEqual(len([e for e in x.f.events if e[0] == "open"]), before)

    def test_namespace_and_adapter_are_isolated(self):
        first, second = contained.ContainedExecutionSession(3), contained.ContainedExecutionSession(4)
        prior = contained.startup.StartupExecutionSession(3)
        self.assertIn("contained-3", first.coordinator.JOURNAL_NAME)
        self.assertNotEqual(first.coordinator.JOURNAL_NAME, second.coordinator.JOURNAL_NAME)
        self.assertNotEqual(first.coordinator.JOURNAL_NAME, prior.coordinator.JOURNAL_NAME)
        self.assertIsNot(first._execution.bundle, prior._execution.bundle)
        self.assertIs(first.backend_module.coordinator, first.coordinator)
        self.assertIs(first.backend_module._prior.coordinator, first.coordinator)
        self.assertEqual(len(contained.startup.SOURCE_PATHS), 27)
        self.assertTrue(set(bundle.BUILD_SOURCE_PATHS).issubset(bundle.SOURCE_PATHS))

    def test_consumed_attempt_cannot_execute_again(self):
        with Fixture() as x:
            x.execute()
            journal, events = x.c.frozen.JOURNAL_PATH.read_bytes(), list(x.f.events)
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            self.assertEqual(x.f.events, events)
            self.assertEqual(x.c.frozen.JOURNAL_PATH.read_bytes(), journal)

    def test_restore_payload_tamper_blocks_recovery_before_io(self):
        with Fixture() as x:
            x.f.fail_ready, x.f.fail_restore = "A", "A"
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            x.f.benchmark.unlink()
            (x.f.root / x.paths["restore_b"]).write_bytes(b"wrong restore")
            events = list(x.f.events)
            with self.assertRaises(bundle.BundleError): x.recover()
            self.assertEqual(x.f.events, events)

    def test_changed_benchmark_identity_is_rejected_before_consumption(self):
        with Fixture() as x:
            x.f.benchmark.write_bytes(b"different benchmark")
            x.snapshot["images"] = bundle.freeze_images(x.f.root, x.paths)
            with self.assertRaisesRegex(bundle.BundleError, "benchmark_build_mismatch"):
                x.execute()
            self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())
            self.assertEqual(x.f.events, [])

    def test_diagnostic_capture_failure_is_safe_and_does_not_change_restoration(self):
        with Fixture() as x:
            result = x.execute()
            endpoint = x.session._observer._endpoints["A"]
            endpoint.diagnostic_snapshot = mock.Mock(side_effect=RuntimeError("private detail"))
            snapshot = x.session.diagnostic_snapshot()
            self.assertEqual(snapshot["roles"]["A"], {"error": "diagnostic_snapshot_unavailable"})
            self.assertNotIn("private detail", json.dumps(snapshot))
            self.assertTrue(result["restoration_complete"])
            self.assertTrue(snapshot["roles"]["B"]["checkpoints"])

    def test_archive_failure_during_actual_close_still_restores_both_roles(self):
        with Fixture() as x:
            original_open = x.f.backend.open_radio_endpoint
            injected = []
            def open_endpoint(route):
                endpoint = original_open(route)
                original_archive = endpoint._archive_detached
                def archive(prior):
                    if prior is not None and endpoint._endpoint is None:
                        injected.append(route)
                        raise RuntimeError("private diagnostic archival failure")
                    return original_archive(prior)
                endpoint._archive_detached = archive
                return endpoint
            x.f.backend.open_radio_endpoint = open_endpoint
            result = x.execute()
            self.assertEqual(set(injected), set(x.f.routes))
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))
            for role, descriptor in (("A", x.f.binding.restore_a), ("B", x.f.binding.restore_b)):
                self.assertIn(("write", role, descriptor.sha256), x.f.events)
                self.assertIn(("verify", role, descriptor.sha256), x.f.events)
            self.assertNotIn("private diagnostic archival failure", json.dumps(result))
            self.assertNotIn("private diagnostic archival failure", json.dumps(x.session.snapshot()))


if __name__ == "__main__": unittest.main()
