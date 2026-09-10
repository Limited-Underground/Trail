"""Package admission plus real composed mocked execution/recovery, no hardware."""
import contextlib
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_console_execution_package as package

spec = importlib.util.spec_from_file_location("_receipt_console_fixture", ROOT / "tests/host/noise_xk_receipt_observation_execution_tests.py")
prior = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = prior
spec.loader.exec_module(prior)
sha = lambda raw: hashlib.sha256(raw).hexdigest()


@contextlib.contextmanager
def fixture():
    with contextlib.ExitStack() as stack:
        x = stack.enter_context(prior.fixture())
        root = x.f.root
        # Old source fixture retains its synthetic benchmark build proof; pin
        # that exact historical fixture instead of changing production constants.
        historical = {"sources": package.observed.freeze_sources(root),
                      "images": deepcopy(x.snapshot["images"]), "image_paths": dict(x.paths)}
        raw = json.dumps(historical).encode()
        p = root / package.HISTORICAL_BINDING_PATH
        p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(raw)
        stack.enter_context(patch.object(package, "HISTORICAL_BINDING_SHA256", sha(raw)))
        for name in package.BUILD_SOURCE_PATHS + ("tools/noise_xk_console_execution_package.py",):
            p = root / name; p.parent.mkdir(parents=True, exist_ok=True)
            source = ROOT / name
            p.write_bytes(source.read_bytes() if source.exists() else b"synthetic future build input\n")
        x.paths["benchmark"] = package.BENCHMARK_NAME
        x.f.benchmark = root / package.BENCHMARK_NAME
        x.f.benchmark.write_bytes(b"benchmark")
        record = {"schema": "OT163-RECEIPT-CONSOLE-BUILD-1", "project_version": "nxk-receipts-v1",
                  "target": package.TARGET, "source_inputs": {},
                  "builds": [{"directory": "a", "initially_absent": True, "exit_code": 0},
                             {"directory": "b", "initially_absent": True, "exit_code": 0}],
                  "tests": {"full_host_matrix": "passed"},
                  "artifacts": {package.BENCHMARK_NAME: {"bytes": 9, "sha256": sha(b"benchmark"), "comparison": "exact"}}}
        for name in package.BUILD_SOURCE_PATHS:
            d = package.previous._descriptor(root, name)
            record["source_inputs"][name] = {k: d[k] for k in ("bytes", "sha256")}
        raw = json.dumps(record).encode()
        (root / package.BUILD_RECORD_PATH).write_bytes(raw)
        x.package = package.freeze_package(root, package.BENCHMARK_NAME, sha(raw))
        x.session = package.ReceiptConsoleSession(x.package)
        c = x.c = x.session.coordinator
        private = root / ".private"
        for name, value in {"ROOT": root, "PRIVATE_ROOT": private,
            "JOURNAL_PATH": private / c.JOURNAL_NAME, "EXECUTION_RECEIPT_PATH": private / c.EXECUTION_NAME,
            "RECOVERY_RECEIPT_PATH": private / c.RECOVERY_NAME}.items():
            stack.enter_context(patch.object(c.frozen, name, value))
        x.snapshot = x.package["snapshot"]
        descriptors = tuple(c.RestoreDescriptor(**x.snapshot["images"]["images"][role]) for role in ("restore_a", "restore_b"))
        x.f.binding = c.ExecutionBinding(package.BENCHMARK_NAME, 9, sha(b"benchmark"), *descriptors,
            c.APPLICATION_OFFSET, c.BAUD, c.RUNNER_PATH.name, sha(c.RUNNER_PATH.read_bytes()), c.runner.SCHEMA)
        x.f.config = c.RunConfig(x.f.routes, x.f.binding, x.f.benchmark, x.f.paths)
        x.f.authority = SimpleNamespace(validate=lambda supplied, recovery:
            c.AuthorityGrant("a" * 64, 1, False, True, c.binding_digest(x.f.binding)))
        old, backend = x.f.backend, x.session.backend_module
        bindings = tuple(backend.RoleBinding(b.role, b.private_route, b.private_identity, descriptor)
                         for b, descriptor in zip(old._bindings, descriptors))
        x.f.backend = backend.BoundBackend(bindings, inventory=old._inventory,
            transport=old._transport, rom_reader=old._rom_reader)
        for route in x.f.routes: x.f.backend.admit_rom(route)
        original = x.f.backend.open_radio_endpoint
        def open_endpoint(route):
            endpoint = original(route); clock = x.f.boards[route].host_clock
            endpoint._monotonic, endpoint._sleep = clock.monotonic, clock.sleep
            endpoint._endpoint._monotonic = clock.monotonic
            return endpoint
        x.f.backend.open_radio_endpoint = open_endpoint
        x.f.events.clear()
        yield x


class Tests(unittest.TestCase):
    def test_package_and_complete_composed_runtime(self):
        with fixture() as x:
            package.verify_package(x.f.root, x.package)
            self.assertFalse(x.package["scope"]["grant_issued"])
            self.assertEqual(x.package["scope"]["application_offset"], 65536)
            self.assertEqual(len(package.observed.SOURCE_PATHS), 42)
            result = x.execute()
            self.assertTrue(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["radio_result"]["totals"]["fragments"], 14)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            self.assertIn("receipt_observations", x.session.diagnostic_snapshot()["roles"]["A"])

    def test_tamper_source_build_and_original_before_io(self):
        for relative in (package.BUILD_SOURCE_PATHS[0], package.BUILD_RECORD_PATH, package.HISTORICAL_BINDING_PATH,
                         "A.bin", "B.bin", package.BENCHMARK_NAME):
            with self.subTest(path=relative), fixture() as x:
                (x.f.root / relative).write_bytes(b"changed")
                with self.assertRaises(package.BundleError): x.execute()
                self.assertEqual(x.f.events, [])
                self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())

    def test_scope_and_closure_changes_rejected(self):
        for mutate in (lambda p: p["scope"].update(grant_issued=True),
                       lambda p: p["scope"].update(grant_issued=0),
                       lambda p: p["scope"].update(application_offset=0),
                       lambda p: p["snapshot"]["sources"]["sources"].pop(),
                       lambda p: p["image_paths"].update(restore_a="B.bin")):
            with fixture() as x:
                changed = deepcopy(x.package); mutate(changed)
                with self.assertRaises(package.BundleError): package.verify_package(x.f.root, changed)
                self.assertEqual(x.f.events, [])

    def test_external_record_pin_and_failed_validation_rejected(self):
        with fixture() as x:
            with self.assertRaises(package.BundleError): package.freeze_package(x.f.root, package.BENCHMARK_NAME, "0" * 64)
            record_path = x.f.root / package.BUILD_RECORD_PATH
            record = json.loads(record_path.read_bytes()); record["tests"]["full_host_matrix"] = "running"
            raw = json.dumps(record).encode(); record_path.write_bytes(raw)
            with self.assertRaises(package.BundleError): package.freeze_package(x.f.root, package.BENCHMARK_NAME, sha(raw))

    def test_invalid_build_pair_or_unknown_source_cannot_be_reblessed(self):
        for mutate in (lambda r: r["builds"][1].update(directory="a"),
                       lambda r: r["source_inputs"].update({"unknown.c": {"bytes": 1, "sha256": "0" * 64}}),
                       lambda r: r["artifacts"][package.BENCHMARK_NAME].update(comparison="unverified")):
            with fixture() as x:
                path = x.f.root / package.BUILD_RECORD_PATH
                record = json.loads(path.read_bytes()); mutate(record)
                raw = json.dumps(record).encode(); path.write_bytes(raw)
                with self.assertRaises(package.BundleError):
                    package.freeze_package(x.f.root, package.BENCHMARK_NAME, sha(raw))

    def test_consumed_or_missing_authority_cannot_mutate(self):
        with fixture() as x:
            x.execute(); before = list(x.f.events)
            with self.assertRaises(prior.prior.base.prior.CoordinatorError): x.execute()
            self.assertEqual(x.f.events, before)
        with fixture() as x:
            x.f.authority = None
            with self.assertRaises(Exception): x.execute()
            self.assertFalse(any(e[0] == "write" for e in x.f.events))
            self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())

    def test_restore_only_recovery_without_benchmark(self):
        with fixture() as x:
            x.f.fail_ready, x.f.fail_restore = "A", "A"
            with self.assertRaises(prior.prior.base.prior.CoordinatorError): x.execute()
            x.f.benchmark.unlink(); x.f.fail_restore = None
            package.verify_package(x.f.root, x.package, recovery=True)
            self.assertTrue(x.recover()["restoration_complete"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)

    def test_recovery_rejects_source_change_and_namespace_isolated(self):
        with fixture() as x:
            old = package.observed.ReceiptObservationSession(5)
            self.assertNotEqual(old.coordinator.JOURNAL_NAME, x.c.JOURNAL_NAME)
            x.f.fail_ready, x.f.fail_restore = "A", "A"
            with self.assertRaises(prior.prior.base.prior.CoordinatorError): x.execute()
            (x.f.root / package.BUILD_SOURCE_PATHS[-1]).write_bytes(b"changed")
            before = list(x.f.events)
            with self.assertRaises(package.BundleError): x.recover()
            self.assertEqual(before, x.f.events)


if __name__ == "__main__": unittest.main()
