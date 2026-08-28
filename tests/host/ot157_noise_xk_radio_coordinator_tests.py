#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-157 successor coordinator."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import json
import sys
import tempfile
import traceback
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools/ot157_noise_xk_radio_coordinator.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


coordinator = load("ot157_noise_xk_radio_coordinator_tests_module", MODULE_PATH)
base = coordinator.frozen

BENCHMARK_NAME = "ot153_noise_xk_radio_cost.bin"
BENCHMARK_PAYLOAD = b"exact unchanged OT-153 benchmark fixture"
RESTORE_PAYLOAD = b"exact Trail restoration fixture"
AUTHORITY_SHA256 = "a" * 64
SAFE_RESULT = {"schema": "OT153NXR0", "version": 0, "result": "safe-fixture"}


class PrivateEndpoint:
    def __init__(self, label: str) -> None:
        self.label = label

    def __repr__(self) -> str:
        return f"PRIVATE-ENDPOINT-{self.label}-COM77"


class Authority:
    def __init__(self) -> None:
        self.calls: list[bool] = []

    def validate(self, binding, *, recovery: bool):
        del binding
        self.calls.append(recovery)
        return coordinator.AuthorityGrant(
            raw_sha256=AUTHORITY_SHA256,
            attempt_count=1,
            reusable=False,
            radio_allowed=True,
        )


class RadioEndpoint:
    def __init__(self, label: str, events: list[tuple]) -> None:
        self.label = label
        self.events = events

    def write_command(self, command: str) -> None:
        raise AssertionError(command)

    def expect(self, kind: str, timeout_ms: int) -> object:
        raise AssertionError((kind, timeout_ms))

    def reopen(self) -> None:
        raise AssertionError("runner is replaced by a deterministic fixture")

    def close(self) -> None:
        self.events.append(("close", self.label))


class Backend:
    def __init__(self, endpoints: tuple[PrivateEndpoint, PrivateEndpoint]) -> None:
        self.endpoints = endpoints
        self.current = {endpoint: RESTORE_PAYLOAD for endpoint in endpoints}
        self.events: list[tuple] = []
        self.failures: set[tuple[str, str, str]] = set()

    @staticmethod
    def kind(image) -> str:
        return "benchmark" if image.name == BENCHMARK_NAME else "restore"

    def write_application(self, endpoint, offset: int, image) -> None:
        kind = self.kind(image)
        self.events.append(("write", endpoint.label, kind, offset))
        if ("write", endpoint.label, kind) in self.failures:
            raise RuntimeError("PRIVATE WRITE COM77 C:/secret")
        self.current[endpoint] = image.payload

    def verify_application(self, endpoint, offset: int, image) -> None:
        kind = self.kind(image)
        self.events.append(("verify", endpoint.label, kind, offset))
        if ("verify", endpoint.label, kind) in self.failures:
            raise RuntimeError("PRIVATE VERIFY COM77 C:/secret")
        if self.current[endpoint] != image.payload:
            raise RuntimeError("PRIVATE IMAGE MISMATCH")

    def hard_reset(self, endpoint) -> None:
        self.events.append(("reset", endpoint.label))

    def open_radio_endpoint(self, endpoint) -> RadioEndpoint:
        if not all(self.current[item] == BENCHMARK_PAYLOAD for item in self.endpoints):
            raise RuntimeError("PRIVATE BOTH-NODE ORDER FAILURE")
        self.events.append(("open", endpoint.label))
        return RadioEndpoint(endpoint.label, self.events)


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ot157-coordinator-")
        self.root = Path(self.temporary.name).resolve()
        self.private = self.root / ".private"
        self.benchmark = self.root / BENCHMARK_NAME
        self.restore = self.root / coordinator.RESTORE_NAME
        self.benchmark.write_bytes(BENCHMARK_PAYLOAD)
        self.restore.write_bytes(RESTORE_PAYLOAD)
        self.endpoints = (PrivateEndpoint("A"), PrivateEndpoint("B"))
        runner_sha256 = hashlib.sha256(coordinator.SUCCESSOR_RUNNER_PATH.read_bytes()).hexdigest()
        self.binding = coordinator.ExecutionBinding(
            benchmark_name=BENCHMARK_NAME,
            benchmark_bytes=len(BENCHMARK_PAYLOAD),
            benchmark_sha256=hashlib.sha256(BENCHMARK_PAYLOAD).hexdigest(),
            restore_name=coordinator.RESTORE_NAME,
            restore_bytes=len(RESTORE_PAYLOAD),
            restore_sha256=hashlib.sha256(RESTORE_PAYLOAD).hexdigest(),
            application_offset=coordinator.APPLICATION_OFFSET,
            baud=coordinator.BAUD,
            runner_name=coordinator.RUNNER_NAME,
            runner_sha256=runner_sha256,
            runner_schema=coordinator.runner.SCHEMA,
        )
        self.config = coordinator.RunConfig(
            private_endpoints=self.endpoints,
            binding=self.binding,
            benchmark_path=self.benchmark,
            restore_path=self.restore,
        )
        self.backend = Backend(self.endpoints)
        self.authority = Authority()
        self.stack = contextlib.ExitStack()

    def __enter__(self):
        self.stack.enter_context(mock.patch.object(coordinator, "ROOT", self.root))
        self.stack.enter_context(mock.patch.object(coordinator, "PRIVATE_ROOT", self.private))
        self.stack.enter_context(mock.patch.object(
            coordinator,
            "JOURNAL_PATH",
            self.private / "ot157-noise-xk-radio-execution-journal.json",
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator,
            "EXECUTION_RECEIPT_PATH",
            self.private / "ot157-noise-xk-radio-execution-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator,
            "RECOVERY_RECEIPT_PATH",
            self.private / "ot157-noise-xk-radio-recovery-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(base, "ROOT", self.root))
        self.stack.enter_context(mock.patch.object(base, "PRIVATE_ROOT", self.private))
        self.stack.enter_context(mock.patch.object(
            base, "JOURNAL_PATH", self.private / "ot157-noise-xk-radio-execution-journal.json"
        ))
        self.stack.enter_context(mock.patch.object(
            base, "EXECUTION_RECEIPT_PATH",
            self.private / "ot157-noise-xk-radio-execution-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(
            base, "RECOVERY_RECEIPT_PATH",
            self.private / "ot157-noise-xk-radio-recovery-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(base, "RESTORE_BYTES", len(RESTORE_PAYLOAD)))
        self.stack.enter_context(mock.patch.object(
            base, "RESTORE_SHA256", hashlib.sha256(RESTORE_PAYLOAD).hexdigest()
        ))
        coordinator._active_journal = None
        return self

    def __exit__(self, *args):
        self.stack.close()
        coordinator._active_journal = None
        self.temporary.cleanup()


class Tests(unittest.TestCase):
    def assert_invalid_configuration_has_no_side_effects(
        self, fixture: Fixture
    ) -> None:
        with self.assertRaises(coordinator.CoordinatorError) as caught:
            coordinator.execute(fixture.config, fixture.backend, fixture.authority)
        self.assertEqual(
            caught.exception.code, coordinator.FailureCode.INVALID_CONFIGURATION
        )
        self.assertEqual(fixture.authority.calls, [])
        self.assertEqual(fixture.backend.events, [])
        rendered = "".join(traceback.format_exception(caught.exception))
        self.assertNotIn("PRIVATE", rendered)
        self.assertNotIn("COM77", rendered)
        self.assertNotIn("secret-path", rendered)
        self.assertIsNone(caught.exception.__cause__)
        self.assertIsNone(caught.exception.__context__)

    def success(self, fixture: Fixture):
        with (
            mock.patch.object(
                coordinator, "_original_runner_run", return_value=dict(SAFE_RESULT)
            ) as run,
            mock.patch.object(
                coordinator.runner, "validate_public_result", side_effect=lambda value: value
            ),
        ):
            receipt = coordinator.execute(fixture.config, fixture.backend, fixture.authority)
        return receipt, run

    def test_01_hash_locks_namespace_and_inherited_state_machine_are_exact(self) -> None:
        self.assertTrue(coordinator.sources_match())
        self.assertEqual(
            hashlib.sha256(coordinator.FROZEN_COORDINATOR_PATH.read_bytes()).hexdigest(),
            coordinator.FROZEN_COORDINATOR_SHA256,
        )
        self.assertEqual(
            hashlib.sha256(coordinator.SUCCESSOR_RUNNER_PATH.read_bytes()).hexdigest(),
            coordinator.SUCCESSOR_RUNNER_SHA256,
        )
        self.assertEqual(coordinator.RUNNER_NAME, "ot156_noise_xk_radio_runner.py")
        self.assertEqual(coordinator.JOURNAL_SCHEMA, "OT157NXJ0")
        self.assertEqual(coordinator.RECEIPT_SCHEMA, "OT157NXCR0")
        for path in (
            coordinator.JOURNAL_PATH,
            coordinator.EXECUTION_RECEIPT_PATH,
            coordinator.RECOVERY_RECEIPT_PATH,
        ):
            self.assertIn("ot157-noise-xk", path.name)
            self.assertNotIn("ot153-noise-xk", path.name)
        self.assertIs(coordinator.execute, base.execute)
        self.assertIs(coordinator.recover, base.recover)
        self.assertIs(coordinator._restore_touched, base._restore_touched)
        with Fixture() as fixture:
            self.assertFalse(fixture.private.exists())
            self.assertTrue(
                base._private_paths_valid(),
                "fresh OT-157 private filenames must pass the inherited configuration gate",
            )
            self.assertTrue(fixture.private.is_dir())
        self.assertEqual(coordinator.RESTORE_BYTES, 500_944)
        self.assertEqual(
            coordinator.RESTORE_SHA256,
            "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
        )

    def test_02_success_restores_both_and_keeps_stage_empty(self) -> None:
        with Fixture() as fixture:
            receipt, run = self.success(fixture)
            run.assert_called_once()
            self.assertEqual(receipt["schema"], "OT157NXCR0")
            self.assertIsNone(receipt["failure"])
            self.assertTrue(receipt["restoration_complete"])
            journal = json.loads(base.JOURNAL_PATH.read_text("ascii"))
            self.assertIsNone(journal["radio_failure_stage"])
            self.assertTrue(journal["radio_result_validated"])
            for endpoint in fixture.endpoints:
                self.assertEqual(fixture.backend.current[endpoint], RESTORE_PAYLOAD)
            first_open = next(i for i, event in enumerate(fixture.backend.events) if event[0] == "open")
            self.assertTrue(all(
                ("verify", label, "benchmark", coordinator.APPLICATION_OFFSET)
                in fixture.backend.events[:first_open]
                for label in ("A", "B")
            ))

    def test_03_every_allowlisted_runner_stage_is_persisted_and_restored(self) -> None:
        for stage in coordinator.runner.StageCode:
            with self.subTest(stage=stage.value), Fixture() as fixture:
                error = coordinator.runner.RunnerError(stage)
                with mock.patch.object(
                    coordinator, "_original_runner_run", side_effect=error
                ):
                    with self.assertRaises(coordinator.CoordinatorError) as caught:
                        coordinator.execute(fixture.config, fixture.backend, fixture.authority)
                self.assertEqual(caught.exception.code, coordinator.FailureCode.RADIO_RUN_FAILED)
                self.assertIsNone(caught.exception.__cause__)
                self.assertIsNone(caught.exception.__context__)
                journal = json.loads(base.JOURNAL_PATH.read_text("ascii"))
                receipt = json.loads(base.EXECUTION_RECEIPT_PATH.read_text("ascii"))
                self.assertEqual(journal["radio_failure_stage"], stage.value)
                self.assertEqual(
                    receipt["failure"],
                    {"code": "radio_run_failed", "stage": stage.value},
                )
                self.assertTrue(receipt["restoration_complete"])
                for endpoint in fixture.endpoints:
                    self.assertEqual(fixture.backend.current[endpoint], RESTORE_PAYLOAD)

    def test_04_unallowlisted_stage_and_private_exception_text_never_serialize(self) -> None:
        with Fixture() as fixture:
            error = coordinator.runner.RunnerError(
                coordinator.runner.StageCode.RESTART_RECONNECT_A
            )
            error.stage = "COM77-C:/PRIVATE/device-path"
            with mock.patch.object(
                coordinator, "_original_runner_run", side_effect=error
            ):
                with self.assertRaises(coordinator.CoordinatorError):
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            journal_text = base.JOURNAL_PATH.read_text("ascii")
            receipt_text = base.EXECUTION_RECEIPT_PATH.read_text("ascii")
            self.assertIsNone(json.loads(journal_text)["radio_failure_stage"])
            self.assertEqual(
                json.loads(receipt_text)["failure"], {"code": "radio_run_failed"}
            )
            rendered = journal_text + receipt_text
            self.assertNotIn("COM77", rendered)
            self.assertNotIn("PRIVATE", rendered)
            self.assertNotIn("device-path", rendered)

    def test_05_restore_failure_continues_peer_and_recovery_is_restore_only(self) -> None:
        with Fixture() as fixture:
            fixture.backend.failures.add(("write", "A", "restore"))
            with (
                mock.patch.object(
                    coordinator, "_original_runner_run", return_value=dict(SAFE_RESULT)
                ),
                mock.patch.object(
                    coordinator.runner, "validate_public_result", side_effect=lambda value: value
                ),
            ):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.RESTORE_FAILED)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)
            self.assertIn(("reset", "B"), fixture.backend.events)

            fixture.backend.failures.clear()
            fixture.benchmark.unlink()
            before = len(fixture.backend.events)
            with mock.patch.object(coordinator, "_original_runner_run") as run:
                receipt = coordinator.recover(
                    fixture.config, fixture.backend, fixture.authority
                )
            run.assert_not_called()
            recovery_events = fixture.backend.events[before:]
            self.assertEqual(receipt["result"], "recovery_only_restored")
            self.assertFalse(any(event[0] == "open" for event in recovery_events))
            self.assertFalse(any(
                event[0] == "write" and event[2] == "benchmark"
                for event in recovery_events
            ))
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertTrue(fixture.authority.calls[-1])

    def test_06_coordinator_errors_have_no_private_exception_chain(self) -> None:
        with Fixture() as fixture:
            with mock.patch.object(
                coordinator, "_original_runner_run",
                side_effect=KeyboardInterrupt("PRIVATE COM77 C:/secret"),
            ):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            rendered = "".join(traceback.format_exception(caught.exception))
            self.assertNotIn("PRIVATE", rendered)
            self.assertNotIn("COM77", rendered)
            self.assertIsNone(caught.exception.__cause__)
            self.assertIsNone(caught.exception.__context__)
            for endpoint in fixture.endpoints:
                self.assertEqual(fixture.backend.current[endpoint], RESTORE_PAYLOAD)

    def test_07_private_path_failures_are_sanitized_before_authority_or_device(self) -> None:
        cases = (
            (
                "resolve_runtime_error",
                lambda: mock.patch.object(
                    Path,
                    "resolve",
                    side_effect=RuntimeError("PRIVATE COM77 secret-path"),
                ),
            ),
            (
                "mkdir_os_error",
                lambda: mock.patch.object(
                    Path,
                    "mkdir",
                    side_effect=OSError("PRIVATE COM77 secret-path"),
                ),
            ),
            (
                "reparse_or_link",
                lambda: mock.patch.object(
                    base, "_has_reparse_or_symlink_ancestry", return_value=True
                ),
            ),
        )
        for label, patcher in cases:
            with self.subTest(case=label), Fixture() as fixture, patcher():
                self.assert_invalid_configuration_has_no_side_effects(fixture)

        with self.subTest(case="private_root_is_file"), Fixture() as fixture:
            fixture.private.write_text("PRIVATE COM77 secret-path", encoding="ascii")
            self.assert_invalid_configuration_has_no_side_effects(fixture)


if __name__ == "__main__":
    unittest.main(verbosity=2)
