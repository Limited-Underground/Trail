"""No-device isolation, binding, one-use and per-role recovery tests."""
import contextlib
import dataclasses
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_role_recovery_coordinator as predecessor
import noise_xk_solicited_coordinator as c

spec = importlib.util.spec_from_file_location("_solicited_recovery_test_support", Path(__file__).with_name("noise_xk_role_recovery_coordinator_tests.py"))
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


class Grant:
    def __init__(self, binding):
        self.binding = binding

    def validate(self, binding, *, recovery):
        if binding != self.binding:
            raise RuntimeError("binding_mismatch")
        return c.AuthorityGrant("a" * 64, 1, False, True, c.binding_digest(binding))


class Fixture:
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        private = self.root / ".private"
        private.mkdir()
        self.benchmark = self.root / "benchmark.bin"
        self.benchmark.write_bytes(b"benchmark")
        self.payloads = (b"original application", b"second application")
        self.paths = (self.root / "restore-a.bin", self.root / "restore-b.bin")
        for path, raw in zip(self.paths, self.payloads):
            path.write_bytes(raw)
        desc = tuple(c.RestoreDescriptor(path.name, len(raw), sha(raw)) for path, raw in zip(self.paths, self.payloads))
        self.binding = c.ExecutionBinding("benchmark.bin", 9, sha(b"benchmark"), *desc,
            c.APPLICATION_OFFSET, c.BAUD, c.RUNNER_PATH.name, sha(c.RUNNER_PATH.read_bytes()), c.runner.SCHEMA)
        self.config = c.RunConfig((object(), object()), self.binding, self.benchmark, self.paths)
        self.backend = support.Backend(self.config, self.payloads)
        self.authority = Grant(self.binding)
        for key, value in {"ROOT": self.root, "PRIVATE_ROOT": private,
                           "JOURNAL_PATH": private / c.JOURNAL_NAME,
                           "EXECUTION_RECEIPT_PATH": private / c.EXECUTION_NAME,
                           "RECOVERY_RECEIPT_PATH": private / c.RECOVERY_NAME}.items():
            self.stack.enter_context(mock.patch.object(c.frozen, key, value))
        self.run_mock = self.stack.enter_context(mock.patch.object(c.runner, "run", return_value={"safe": True}))
        self.stack.enter_context(mock.patch.object(c.frozen, "_safe_radio_result", side_effect=lambda value: value))
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def execute(self):
        return c.execute(self.config, self.backend, self.authority)


class CoordinatorTests(unittest.TestCase):
    def test_private_instance_does_not_reconfigure_predecessor(self):
        self.assertIsNot(c.frozen, predecessor.frozen)
        self.assertIsNot(c.RestoreDescriptor, predecessor.RestoreDescriptor)
        self.assertIsNot(c._coordinator._operation_lock, predecessor._operation_lock)
        self.assertEqual(predecessor.RUNNER_PATH.name, "noise_xk_ready_runner.py")
        self.assertEqual(predecessor.frozen.JOURNAL_PATH.name, "noise-xk-role-recovery-journal.json")
        self.assertEqual(predecessor.frozen.JOURNAL_SCHEMA, "OTNXROLEJ0")
        self.assertEqual(c.frozen.JOURNAL_SCHEMA, "OTNXSOLJ0")
        self.assertIs(c._coordinator.runner, c.runner)

    def test_success_restores_distinct_images_in_new_namespace(self):
        with Fixture() as f:
            receipt = f.execute()
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(tuple(f.backend.current[e] for e in f.config.private_endpoints), f.payloads)
            self.assertEqual(f.run_mock.call_count, 1)
            self.assertTrue(c.frozen.JOURNAL_PATH.exists())
            self.assertFalse((f.root / ".private/noise-xk-role-recovery-journal.json").exists())
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertEqual(f.run_mock.call_count, 1)

    def test_wrong_runner_or_old_descriptor_type_fails_before_backend(self):
        with Fixture() as f:
            self.assertFalse(c._descriptor_valid(predecessor.RestoreDescriptor("a.bin", 1, "a" * 64)))
            for changes in ({"runner_name": predecessor.RUNNER_PATH.name},
                            {"runner_sha256": "f" * 64}, {"runner_schema": "wrong"}):
                f.config = dataclasses.replace(f.config, binding=dataclasses.replace(f.binding, **changes))
                with self.assertRaises(c.CoordinatorError):
                    f.execute()
                self.assertEqual(f.backend.events, [])
                self.assertFalse(c.frozen.JOURNAL_PATH.exists())

    def test_failed_a_restore_does_not_skip_b_and_recovery_needs_no_benchmark(self):
        with Fixture() as f:
            f.backend.fail.add(("A", f.binding.restore_a.sha256))
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertEqual(f.backend.current[f.config.private_endpoints[1]], f.payloads[1])
            f.backend.fail.clear()
            f.backend.events.clear()
            f.benchmark.unlink()
            receipt = c.recover(f.config, f.backend, f.authority)
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual([e for e in f.backend.events if e[0] == "write"],
                             [("write", "A", f.binding.restore_a.sha256)])

    def test_lock_rejects_concurrent_or_reentrant_operation_before_io(self):
        with Fixture() as f:
            self.assertTrue(c._coordinator._operation_lock.acquire(blocking=False))
            try:
                with self.assertRaises(c.CoordinatorError):
                    f.execute()
                self.assertEqual(f.backend.events, [])
                self.assertFalse(c.frozen.JOURNAL_PATH.exists())
            finally:
                c._coordinator._operation_lock.release()
            self.assertTrue(f.execute()["restoration_complete"])

    def test_new_readiness_failure_stage_is_preserved_and_restores(self):
        with Fixture() as f:
            stage = c.runner.StageCode.INITIAL_BOOT_CONTRACT_A
            f.run_mock.side_effect = c.runner.RunnerError(stage)
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            receipt = json.loads(c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(receipt["failure"]["stage"], stage.value)


if __name__ == "__main__":
    unittest.main()
