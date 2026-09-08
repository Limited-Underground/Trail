"""No-device closure, accepted-build and execution admission tests."""
import contextlib
import copy
import dataclasses
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_solicited_bundle as b
import noise_xk_solicited_execution as execution
c = execution.coordinator


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


class Fixture:
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        for relative in b.SOURCE_PATHS:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic source\n")
        self.paths = {"benchmark": b.BENCHMARK_NAME, "restore_a": "a.bin", "restore_b": "b.bin"}
        for role, relative in self.paths.items():
            (self.root / relative).write_bytes(role.encode())
        images = b.freeze_images(self.root, self.paths)
        descriptor = images["images"]["benchmark"]
        artifact = {key: descriptor[key] for key in ("bytes", "sha256")}
        record = {"schema": "OT163-SOLICITED-READINESS-BUILD-1",
                  "source_inputs": {b.SOURCE_PATHS[0]: {"bytes": 17, "sha256": sha(b"synthetic source\n")}},
                  "artifacts": {b.BENCHMARK_NAME: {"a": artifact, "b": artifact, "byte_identical": True}}}
        raw = json.dumps(record).encode()
        (self.root / b.BUILD_RECORD_PATH).write_bytes(raw)
        self.stack.enter_context(mock.patch.object(b, "BUILD_RECORD_SHA256", sha(raw)))
        self.snapshot = {"sources": b.freeze_sources(self.root), "images": images}
        a, second = (c.RestoreDescriptor(**images["images"][role]) for role in ("restore_a", "restore_b"))
        self.binding = c.ExecutionBinding(descriptor["name"], descriptor["bytes"], descriptor["sha256"],
            a, second, c.APPLICATION_OFFSET, c.BAUD, c.RUNNER_PATH.name, sha(c.RUNNER_PATH.read_bytes()), c.runner.SCHEMA)
        self.config = c.RunConfig((object(), object()), self.binding,
            self.root / self.paths["benchmark"], (self.root / self.paths["restore_a"], self.root / self.paths["restore_b"]))
        self.backend, self.authority = mock.Mock(), mock.Mock()
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def execute(self):
        return execution.execute(self.root, self.snapshot, self.paths, self.config, self.backend, self.authority)


class BundleTests(unittest.TestCase):
    def test_full_closure_and_missing_extra_duplicate_or_tampered_source(self):
        with Fixture() as f:
            b.verify_sources(f.root, f.snapshot["sources"])
            for change in (lambda rows: rows.pop(), lambda rows: rows.append(dict(rows[0])),
                           lambda rows: rows.append({"path": "extra.py", "bytes": 1, "sha256": "a" * 64})):
                bad = copy.deepcopy(f.snapshot["sources"])
                change(bad["sources"])
                with self.assertRaises(b.BundleError):
                    b.verify_sources(f.root, bad)
            (f.root / b.SOURCE_PATHS[0]).write_bytes(b"tampered")
            with self.assertRaises(b.BundleError):
                b.verify_sources(f.root, f.snapshot["sources"])

    def test_exact_configuration_passes_to_coordinator(self):
        with Fixture() as f, mock.patch.object(c, "execute", return_value={"passed": True}) as call:
            self.assertEqual(f.execute(), {"passed": True})
            call.assert_called_once_with(f.config, f.backend, f.authority)

    def test_image_or_configuration_mismatch_never_reaches_coordinator(self):
        for mutation in ("image", "binding", "path", "swap"):
            with self.subTest(mutation=mutation), Fixture() as f, mock.patch.object(c, "execute") as call:
                if mutation == "image":
                    (f.root / f.paths["restore_b"]).write_bytes(b"wrong")
                elif mutation == "binding":
                    f.config = dataclasses.replace(f.config, binding=dataclasses.replace(f.binding, benchmark_bytes=1))
                elif mutation == "path":
                    f.config = dataclasses.replace(f.config, benchmark_path=f.root / "other.bin")
                else:
                    f.config = dataclasses.replace(f.config, restore_paths=f.config.restore_paths[::-1])
                with self.assertRaises(b.BundleError):
                    f.execute()
                call.assert_not_called()
                self.assertFalse(f.backend.mock_calls)
                self.assertFalse(f.authority.mock_calls)

    def test_arbitrary_benchmark_with_consistent_snapshot_and_config_is_denied(self):
        with Fixture() as f, mock.patch.object(c, "execute") as call:
            (f.root / f.paths["benchmark"]).write_bytes(b"unaccepted binary")
            f.snapshot["images"] = b.freeze_images(f.root, f.paths)
            item = f.snapshot["images"]["images"]["benchmark"]
            f.config = dataclasses.replace(f.config, binding=dataclasses.replace(f.binding,
                benchmark_bytes=item["bytes"], benchmark_sha256=item["sha256"]))
            with self.assertRaisesRegex(b.BundleError, "benchmark_build_mismatch"):
                f.execute()
            call.assert_not_called()

    def test_changed_build_record_or_source_is_denied_after_refreezing_snapshot(self):
        for relative in (b.BUILD_RECORD_PATH, b.SOURCE_PATHS[0]):
            with self.subTest(relative=relative), Fixture() as f, mock.patch.object(c, "execute") as call:
                path = f.root / relative
                path.write_bytes(path.read_bytes() + b" ")
                f.snapshot["sources"] = b.freeze_sources(f.root)
                with self.assertRaises(b.BundleError):
                    f.execute()
                call.assert_not_called()

    def test_recovery_without_benchmark_file_still_checks_both_restores(self):
        with Fixture() as f, mock.patch.object(c, "recover", return_value={"restored": True}) as call:
            f.config.benchmark_path.unlink()
            result = execution.recover(f.root, f.snapshot, f.paths, f.config, f.backend, f.authority)
            self.assertEqual(result, {"restored": True})
            call.assert_called_once()
            call.reset_mock()
            f.config.restore_paths[1].write_bytes(b"wrong")
            with self.assertRaises(b.BundleError):
                execution.recover(f.root, f.snapshot, f.paths, f.config, f.backend, f.authority)
            call.assert_not_called()

    def test_repository_accepted_build_provenance(self):
        record = json.loads((ROOT / b.BUILD_RECORD_PATH).read_bytes())
        artifact = record["artifacts"][b.BENCHMARK_NAME]["a"]
        b.verify_build_provenance(ROOT, {"name": b.BENCHMARK_NAME, **artifact})


if __name__ == "__main__":
    unittest.main()
