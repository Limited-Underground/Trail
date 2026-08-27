#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-153 restoration-safe coordinator."""

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
MODULE_PATH = ROOT / "tools" / "ot153_noise_xk_radio_coordinator.py"
SPEC = importlib.util.spec_from_file_location("ot153_radio_coordinator_tests_module", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to load OT-153 radio coordinator")
coordinator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = coordinator
SPEC.loader.exec_module(coordinator)


BENCHMARK_NAME = "ot153_noise_xk_radio_bench.bin"
BENCHMARK_PAYLOAD = b"ot153 exact benchmark fixture"
RESTORE_PAYLOAD = b"exact ot147 trail restore fixture"
AUTHORITY_SHA256 = "a" * 64
SAFE_RESULT = {"schema": "OT153NXR0", "version": 0, "result": "safe-fixture"}


class PrivateEndpoint:
    def __init__(self, label: str) -> None:
        self.label = label

    def __repr__(self) -> str:
        return f"PRIVATE-ENDPOINT-{self.label}-SECRET"


class Authority:
    def __init__(self, *, radio_allowed: bool = True, reject: bool = False) -> None:
        self.radio_allowed = radio_allowed
        self.reject = reject
        self.calls: list[tuple[object, bool]] = []

    def validate(self, binding, *, recovery: bool):
        self.calls.append((binding, recovery))
        if self.reject:
            raise RuntimeError("PRIVATE AUTHORITY FAILURE")
        return coordinator.AuthorityGrant(
            raw_sha256=AUTHORITY_SHA256,
            attempt_count=1,
            reusable=False,
            radio_allowed=self.radio_allowed,
        )


class FakeRadioEndpoint:
    def __init__(self, label: str, events: list[tuple]) -> None:
        self.label = label
        self.events = events

    def write_command(self, command: str) -> None:
        raise AssertionError(command)

    def expect(self, kind: str, timeout_ms: int) -> object:
        raise AssertionError((kind, timeout_ms))

    def close(self) -> None:
        self.events.append(("close", self.label))


class Backend:
    def __init__(self, endpoints: tuple[PrivateEndpoint, PrivateEndpoint]) -> None:
        self.endpoints = endpoints
        self.current = {endpoint: RESTORE_PAYLOAD for endpoint in endpoints}
        self.events: list[tuple] = []
        self.failures: set[tuple[str, str, str]] = set()

    @staticmethod
    def _kind(image) -> str:
        return "benchmark" if image.name == BENCHMARK_NAME else "restore"

    def write_application(self, endpoint, offset: int, image) -> None:
        kind = self._kind(image)
        self.events.append(("write", endpoint.label, kind, offset))
        if ("write", endpoint.label, kind) in self.failures:
            raise RuntimeError("PRIVATE WRITE FAILURE")
        self.current[endpoint] = image.payload

    def verify_application(self, endpoint, offset: int, image) -> None:
        kind = self._kind(image)
        self.events.append(("verify", endpoint.label, kind, offset))
        if ("verify", endpoint.label, kind) in self.failures:
            raise RuntimeError("PRIVATE VERIFY FAILURE")
        if self.current[endpoint] != image.payload:
            raise RuntimeError("PRIVATE IMAGE MISMATCH")

    def hard_reset(self, endpoint) -> None:
        self.events.append(("reset", endpoint.label))
        if ("reset", endpoint.label, "any") in self.failures:
            raise RuntimeError("PRIVATE RESET FAILURE")

    def open_radio_endpoint(self, endpoint) -> FakeRadioEndpoint:
        if not all(self.current[value] == BENCHMARK_PAYLOAD for value in self.endpoints):
            raise RuntimeError("PRIVATE BOTH-NODE PREFLIGHT FAILURE")
        self.events.append(("open", endpoint.label))
        if ("open", endpoint.label, "radio") in self.failures:
            raise RuntimeError("PRIVATE OPEN FAILURE")
        return FakeRadioEndpoint(endpoint.label, self.events)


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.private = self.root / ".private"
        self.private.mkdir()
        self.benchmark = self.root / BENCHMARK_NAME
        self.restore = self.root / coordinator.RESTORE_NAME
        self.benchmark.write_bytes(BENCHMARK_PAYLOAD)
        self.restore.write_bytes(RESTORE_PAYLOAD)
        self.endpoints = (PrivateEndpoint("A"), PrivateEndpoint("B"))
        runner_sha256 = coordinator._runner_digest()
        assert runner_sha256 is not None
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
            coordinator, "JOURNAL_PATH", self.private / "ot153-noise-xk-radio-execution-journal.json"
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "EXECUTION_RECEIPT_PATH", self.private / "ot153-noise-xk-radio-execution-receipt.json"
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "RECOVERY_RECEIPT_PATH", self.private / "ot153-noise-xk-radio-recovery-receipt.json"
        ))
        self.stack.enter_context(mock.patch.object(coordinator, "RESTORE_BYTES", len(RESTORE_PAYLOAD)))
        self.stack.enter_context(mock.patch.object(
            coordinator, "RESTORE_SHA256", hashlib.sha256(RESTORE_PAYLOAD).hexdigest()
        ))
        return self

    def __exit__(self, *args):
        self.stack.close()
        self.temporary.cleanup()


class Tests(unittest.TestCase):
    def assert_safe_error(self, error: coordinator.CoordinatorError) -> None:
        rendered = "".join(traceback.format_exception(error))
        self.assertNotIn("PRIVATE", rendered)
        self.assertIsNone(error.__context__)
        self.assertIsNone(error.__cause__)

    def _success(self, fixture: Fixture):
        with (
            mock.patch.object(coordinator.runner, "run", return_value=dict(SAFE_RESULT)) as run,
            mock.patch.object(
                coordinator.runner, "validate_public_result", side_effect=lambda value: value
            ),
        ):
            receipt = coordinator.execute(fixture.config, fixture.backend, fixture.authority)
        return receipt, run

    def test_01_success_flashes_both_before_runner_and_restores_both(self) -> None:
        with Fixture() as fixture:
            receipt, run = self._success(fixture)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(receipt["result"], "noise_xk_radio_run_passed_and_restored")
            self.assertTrue(receipt["restoration_complete"])
            self.assertTrue(receipt["radio_result_validated"])
            events = fixture.backend.events
            first_open = next(index for index, event in enumerate(events) if event[0] == "open")
            required = {
                ("write", "A", "benchmark", coordinator.APPLICATION_OFFSET),
                ("verify", "A", "benchmark", coordinator.APPLICATION_OFFSET),
                ("write", "B", "benchmark", coordinator.APPLICATION_OFFSET),
                ("verify", "B", "benchmark", coordinator.APPLICATION_OFFSET),
            }
            self.assertTrue(required.issubset(set(events[:first_open])))
            self.assertIn(("reset", "A"), events[:first_open])
            self.assertIn(("reset", "B"), events[:first_open])
            self.assertTrue(all(
                node["benchmark_reset_completed"] for node in receipt["nodes"]
            ))
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)
            self.assertIn(("close", "A"), events)
            self.assertIn(("close", "B"), events)

    def test_02_authority_must_grant_one_nonreusable_radio_attempt(self) -> None:
        with Fixture() as fixture:
            fixture.authority = Authority(radio_allowed=False)
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.AUTHORITY_REJECTED)
            self.assertEqual(fixture.backend.events, [])
            self.assertFalse(coordinator.JOURNAL_PATH.exists())
            self.assert_safe_error(caught.exception)

    def test_03_preflight_attempts_both_readbacks_and_both_resets_before_journal(self) -> None:
        with Fixture() as fixture:
            fixture.backend.failures.add(("verify", "A", "restore"))
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.PREFLIGHT_FAILED)
            self.assertEqual([event[0] for event in fixture.backend.events], [
                "verify", "verify", "reset", "reset"
            ])
            self.assertFalse(coordinator.JOURNAL_PATH.exists())

    def test_04_existing_journal_consumes_attempt_before_device_io(self) -> None:
        with Fixture() as fixture:
            coordinator.JOURNAL_PATH.write_text("occupied\n", encoding="ascii")
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(
                caught.exception.code, coordinator.FailureCode.AUTHORITY_ALREADY_CONSUMED
            )
            self.assertEqual(fixture.backend.events, [])

    def test_05_second_benchmark_failure_restores_both_touched_nodes(self) -> None:
        with Fixture() as fixture:
            fixture.backend.failures.add(("verify", "B", "benchmark"))
            with mock.patch.object(coordinator.runner, "run") as run:
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.BENCHMARK_VERIFY_FAILED)
            run.assert_not_called()
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)
            receipt = json.loads(coordinator.EXECUTION_RECEIPT_PATH.read_text("ascii"))
            self.assertTrue(receipt["restoration_complete"])

    def test_06_base_exception_from_exact_runner_is_closed_and_restored(self) -> None:
        with Fixture() as fixture:
            with mock.patch.object(
                coordinator.runner, "run", side_effect=KeyboardInterrupt("PRIVATE INTERRUPT")
            ):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.RADIO_RUN_FAILED)
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)
            self.assert_safe_error(caught.exception)

    def test_07_restore_failure_continues_other_node_and_recovery_is_retryable(self) -> None:
        with Fixture() as fixture:
            fixture.backend.failures.add(("write", "A", "restore"))
            with (
                mock.patch.object(coordinator.runner, "run", return_value=dict(SAFE_RESULT)),
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
            receipt = coordinator.recover(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(receipt["result"], "recovery_only_restored")
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.authority.calls[-1][1], True)

    def test_08_recovery_never_opens_radio_or_writes_benchmark(self) -> None:
        with Fixture() as fixture:
            grant = fixture.authority.validate(fixture.binding, recovery=False)
            journal = coordinator._new_journal(fixture.binding, grant)
            for label in ("A", "B"):
                journal["nodes"][label]["installed_app_readback_verified"] = True
                journal["nodes"][label]["preflight_reset_completed"] = True
            journal["nodes"]["A"]["benchmark_write_started"] = True
            fixture.backend.current[fixture.endpoints[0]] = BENCHMARK_PAYLOAD
            self.assertTrue(coordinator._write_new(coordinator.JOURNAL_PATH, journal))
            fixture.benchmark.unlink()
            with mock.patch.object(coordinator.runner, "run") as run:
                coordinator.recover(fixture.config, fixture.backend, fixture.authority)
            run.assert_not_called()
            self.assertFalse(any(event[0] == "open" for event in fixture.backend.events))
            self.assertFalse(any(
                event[0] == "write" and event[2] == "benchmark"
                for event in fixture.backend.events
            ))

    def test_09_invalid_runner_result_fails_closed_then_restores(self) -> None:
        with Fixture() as fixture:
            with mock.patch.object(coordinator.runner, "run", return_value={"path": "PRIVATE"}):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.RADIO_RUN_FAILED)
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)

    def test_10_receipts_never_serialize_private_values_or_backend_text(self) -> None:
        with Fixture() as fixture:
            receipt, unused_run = self._success(fixture)
            text = coordinator.EXECUTION_RECEIPT_PATH.read_text("ascii")
            self.assertNotIn("PRIVATE-ENDPOINT", text)
            self.assertNotIn(str(fixture.root), text)
            self.assertNotIn("BACKEND", text)
            self.assertEqual(receipt["privacy"]["filesystem_paths_recorded"], False)

    def test_11_structural_boundary_is_exact_and_application_only(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("def main(", source)
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("write_flash", source)
        self.assertIn("ot153_noise_xk_radio_runner.py", source)
        self.assertIn("runner.run(opened[0], opened[1])", source)
        self.assertNotIn("ot150_mbedtls_psa_protocol_runner.py", source)
        self.assertEqual(coordinator.RESTORE_BYTES, 500_944)
        self.assertEqual(
            coordinator.RESTORE_SHA256,
            "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
        )
        self.assertEqual(coordinator.APPLICATION_OFFSET, 0x10000)
        self.assertIn("os.O_EXCL", source)


    def test_12_benchmark_reset_failure_attempts_both_resets_and_never_runs(self) -> None:
        with Fixture() as fixture:
            original_reset = fixture.backend.hard_reset
            calls = {"A": 0, "B": 0}

            def reset(endpoint):
                calls[endpoint.label] += 1
                if endpoint.label == "A" and calls["A"] == 2:
                    fixture.backend.events.append(("reset", "A"))
                    raise RuntimeError("PRIVATE BENCHMARK RESET FAILURE")
                original_reset(endpoint)

            fixture.backend.hard_reset = reset
            with mock.patch.object(coordinator.runner, "run") as run:
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.backend, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.BENCHMARK_RESET_FAILED)
            run.assert_not_called()
            self.assertGreaterEqual(calls["B"], 2)
            self.assertEqual(fixture.backend.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.backend.current[fixture.endpoints[1]], RESTORE_PAYLOAD)


if __name__ == "__main__":
    unittest.main(verbosity=2)
