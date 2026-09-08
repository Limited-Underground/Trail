"""No-device tests of per-role authority, consumption and restoration seams.

Runner success is injected here; real runner/stream composition is a separate gate.
"""
from __future__ import annotations

import contextlib
import dataclasses
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_role_recovery_coordinator as c


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Grant:
    def __init__(self, binding):
        self.expected = binding

    def validate(self, binding, *, recovery):
        if binding != self.expected:
            raise RuntimeError("PRIVATE binding mismatch")
        return c.AuthorityGrant("a" * 64, 1, False, True, c.binding_digest(self.expected))


class Backend:
    def __init__(self, config, payloads):
        self.endpoints = config.private_endpoints
        self.descriptors = (config.binding.restore_a, config.binding.restore_b)
        self.current = dict(zip(self.endpoints, payloads))
        self.roles = dict(zip(self.endpoints, ("A", "B")))
        self.events = []
        self.fail = set()

    def verify_role(self, endpoint, role, descriptor):
        self.events.append(("role", role))
        return self.roles.get(endpoint) == role and descriptor == self.descriptors[("A", "B").index(role)]

    def write_application(self, endpoint, offset, image):
        role = self.roles[endpoint]
        self.events.append(("write", role, image.sha256))
        if (role, image.sha256) in self.fail:
            raise KeyboardInterrupt("PRIVATE C:/secret COM77")
        self.current[endpoint] = image.payload

    def verify_application(self, endpoint, offset, image):
        self.events.append(("verify", self.roles[endpoint], image.sha256))
        if self.current[endpoint] != image.payload:
            raise RuntimeError("PRIVATE wrong bytes")

    def hard_reset(self, endpoint):
        self.events.append(("reset", self.roles[endpoint]))

    def open_radio_endpoint(self, endpoint):
        self.events.append(("open", self.roles[endpoint]))
        return mock.Mock(close=lambda: self.events.append(("close", self.roles[endpoint])))


class Fixture:
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        self.private = self.root / ".private"
        self.private.mkdir()
        self.benchmark = self.root / "benchmark.bin"
        self.benchmark.write_bytes(b"benchmark")
        self.paths = (self.root / "a" / "restore.bin", self.root / "b" / "restore.bin")
        self.payloads = (b"original application", b"different second application")
        for path, payload in zip(self.paths, self.payloads):
            path.parent.mkdir()
            path.write_bytes(payload)
        desc = tuple(c.RestoreDescriptor(path.name, len(data), sha(data)) for path, data in zip(self.paths, self.payloads))
        self.binding = c.ExecutionBinding("benchmark.bin", 9, sha(b"benchmark"), *desc,
            c.APPLICATION_OFFSET, c.BAUD, c.RUNNER_PATH.name, sha(c.RUNNER_PATH.read_bytes()), c.runner.SCHEMA)
        self.config = c.RunConfig((object(), object()), self.binding, self.benchmark, self.paths)
        self.backend = Backend(self.config, self.payloads)
        self.authority = Grant(self.binding)
        for name, value in {
            "ROOT": self.root, "PRIVATE_ROOT": self.private,
            "JOURNAL_PATH": self.private / "noise-xk-role-recovery-journal.json",
            "EXECUTION_RECEIPT_PATH": self.private / "noise-xk-role-recovery-execution.json",
            "RECOVERY_RECEIPT_PATH": self.private / "noise-xk-role-recovery-receipt.json",
        }.items():
            self.stack.enter_context(mock.patch.object(c.frozen, name, value))
        self.stack.enter_context(mock.patch.object(c.runner, "run", return_value={"safe": True}))
        self.stack.enter_context(mock.patch.object(c.frozen, "_safe_radio_result", side_effect=lambda value: value))
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def execute(self):
        return c.execute(self.config, self.backend, self.authority)

    def recover(self):
        return c.recover(self.config, self.backend, self.authority)

    def incomplete(self):
        self.backend.fail.add(("B", self.binding.restore_b.sha256))
        with unittest.TestCase().assertRaises(c.CoordinatorError):
            self.execute()


class RecoveryTests(unittest.TestCase):
    def test_success_restores_different_images_and_records_exact_role_descriptors(self):
        with Fixture() as f:
            receipt = f.execute()
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(tuple(f.backend.current[e] for e in f.config.private_endpoints), f.payloads)
            self.assertEqual(receipt["binding"]["restore_a"], dataclasses.asdict(f.binding.restore_a))
            self.assertEqual(receipt["binding"]["restore_b"], dataclasses.asdict(f.binding.restore_b))
            journal = json.loads(c.frozen.JOURNAL_PATH.read_text())
            self.assertTrue(c.frozen._journal_valid(journal, f.binding, f.authority.validate(f.binding, recovery=True)))
            self.assertNotIn(str(f.root), json.dumps(receipt))
            self.assertNotIn("PRIVATE", json.dumps(receipt))

    def test_swapped_paths_fail_before_backend_access_or_consumption(self):
        with Fixture() as f:
            f.config = dataclasses.replace(f.config, restore_paths=f.paths[::-1])
            with self.assertRaises(c.CoordinatorError) as error:
                f.execute()
            self.assertEqual(error.exception.code, c.FailureCode.ARTIFACT_INVALID)
            self.assertEqual(f.backend.events, [])
            self.assertFalse(c.frozen.JOURNAL_PATH.exists())

    def test_swapped_endpoints_fail_before_reset_write_or_consumption(self):
        with Fixture() as f:
            f.config = dataclasses.replace(f.config, private_endpoints=f.config.private_endpoints[::-1])
            with self.assertRaises(c.CoordinatorError) as error:
                f.execute()
            self.assertEqual(error.exception.code, c.FailureCode.PREFLIGHT_FAILED)
            self.assertTrue(all(event[0] == "role" for event in f.backend.events))
            self.assertFalse(c.frozen.JOURNAL_PATH.exists())

    def test_wrong_installed_second_bytes_deny_before_either_reset_or_write(self):
        with Fixture() as f:
            f.backend.current[f.config.private_endpoints[1]] = b"wrong"
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertFalse(any(e[0] in ("reset", "write") for e in f.backend.events))
            self.assertFalse(c.frozen.JOURNAL_PATH.exists())

    def test_grant_requires_exact_binding_digest(self):
        with Fixture() as f:
            f.authority.validate = lambda *a, **k: c.AuthorityGrant("a" * 64, 1, False, True, "b" * 64)
            with self.assertRaises(c.CoordinatorError) as error:
                f.execute()
            self.assertEqual(error.exception.code, c.FailureCode.AUTHORITY_REJECTED)
            self.assertEqual(f.backend.events, [])

    def test_benchmark_baseexception_restores_only_touched_role(self):
        with Fixture() as f:
            f.backend.fail.add(("A", f.binding.benchmark_sha256))
            with self.assertRaises(c.CoordinatorError) as error:
                f.execute()
            self.assertEqual(error.exception.code, c.FailureCode.BENCHMARK_WRITE_FAILED)
            self.assertEqual(tuple(f.backend.current[e] for e in f.config.private_endpoints), f.payloads)
            self.assertFalse(any(e[0] == "write" and e[1] == "B" for e in f.backend.events))
            self.assertNotIn("PRIVATE", str(error.exception))

    def test_failed_restore_recovery_uses_role_b_only_and_never_reads_benchmark(self):
        with Fixture() as f:
            f.incomplete()
            f.backend.fail.clear()
            f.backend.events.clear()
            f.benchmark.unlink()
            receipt = f.recover()
            self.assertTrue(receipt["restoration_complete"])
            writes = [e for e in f.backend.events if e[0] == "write"]
            self.assertEqual(writes, [("write", "B", f.binding.restore_b.sha256)])

    def test_recovery_rejects_swapped_descriptors_even_with_matching_new_grant(self):
        with Fixture() as f:
            f.incomplete()
            f.backend.events.clear()
            binding = dataclasses.replace(f.binding, restore_a=f.binding.restore_b, restore_b=f.binding.restore_a)
            f.config = dataclasses.replace(f.config, binding=binding, restore_paths=f.paths[::-1])
            f.authority = Grant(binding)
            with self.assertRaises(c.CoordinatorError) as error:
                f.recover()
            self.assertEqual(error.exception.code, c.FailureCode.JOURNAL_FAILED)
            self.assertEqual(f.backend.events, [])

    def test_recovery_rejects_endpoint_swap_when_benchmark_bytes_are_identical(self):
        with Fixture() as f:
            f.incomplete()
            f.backend.fail.clear()
            f.backend.events.clear()
            f.config = dataclasses.replace(f.config, private_endpoints=f.config.private_endpoints[::-1])
            with self.assertRaises(c.CoordinatorError) as error:
                f.recover()
            self.assertEqual(error.exception.code, c.FailureCode.RESTORE_FAILED)
            self.assertFalse(any(e[0] == "write" for e in f.backend.events))

    def test_role_change_after_preflight_prevents_benchmark_write(self):
        with Fixture() as f:
            reset = f.backend.hard_reset
            def change_after_second_reset(endpoint):
                reset(endpoint)
                if endpoint is f.config.private_endpoints[1]:
                    f.backend.roles[f.config.private_endpoints[0]] = "B"
            f.backend.hard_reset = change_after_second_reset
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertFalse(any(e[0] == "write" for e in f.backend.events))

    def test_recovery_wrong_a_role_does_not_prevent_independent_b_restore(self):
        with Fixture() as f:
            f.backend.fail.update({("A", f.binding.restore_a.sha256), ("B", f.binding.restore_b.sha256)})
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertEqual(set(f.backend.current.values()), {b"benchmark"})
            f.backend.fail.clear()
            f.backend.events.clear()
            f.backend.roles[f.config.private_endpoints[0]] = "WRONG"
            with self.assertRaises(c.CoordinatorError):
                f.recover()
            writes = [e for e in f.backend.events if e[0] == "write"]
            self.assertEqual(writes, [("write", "B", f.binding.restore_b.sha256)])
            journal = json.loads(c.frozen.JOURNAL_PATH.read_text())
            self.assertFalse(journal["nodes"]["A"]["restore_reset_completed"])
            self.assertTrue(journal["nodes"]["B"]["restore_reset_completed"])
            self.assertFalse(c.frozen.RECOVERY_RECEIPT_PATH.exists())

    def test_path_resolution_failure_is_sanitized_before_backend_access(self):
        with Fixture() as f:
            with mock.patch.object(Path, "resolve", side_effect=OSError("PRIVATE C:/secret")):
                with self.assertRaises(c.CoordinatorError) as error:
                    f.execute()
            self.assertEqual(str(error.exception), "invalid_configuration")
            self.assertEqual(f.backend.events, [])

    def test_private_namespace_must_be_direct_child_of_root(self):
        with Fixture() as f:
            with mock.patch.object(c.frozen, "ROOT", f.root / "other"):
                self.assertFalse(c._private_paths_valid())

    def test_radio_failure_retains_only_allowlisted_stage_and_restores_both(self):
        with Fixture() as f:
            c.runner.run.side_effect = c.runner.RunnerError(c.runner.StageCode.INITIAL_BOOT_CONTRACT_B)
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            receipt = json.loads(c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertEqual(receipt["failure"], {"code": "radio_run_failed", "stage": "initial_boot_contract_b"})
            self.assertTrue(receipt["restoration_complete"])

    def test_second_execute_never_reuses_consumed_journal(self):
        with Fixture() as f:
            f.execute()
            f.backend.events.clear()
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertEqual(f.backend.events, [])


if __name__ == "__main__":
    unittest.main()
