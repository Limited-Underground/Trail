#!/usr/bin/env python3
"""Focused host-only tests for the OT-140 restoration-safe coordinator."""

from __future__ import annotations

import contextlib
import dataclasses
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
MODULE_PATH = ROOT / "tools" / "ot140_monocypher_coordinator.py"
SPEC = importlib.util.spec_from_file_location("ot140_coordinator_tests_module", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to load OT-140 coordinator")
coordinator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = coordinator
SPEC.loader.exec_module(coordinator)


BENCHMARK_PAYLOAD = b"ot140 benchmark fixture"
RESTORE_PAYLOAD = b"exact trail restore fixture"
AUTHORITY_SHA256 = "a" * 64


class PrivateEndpoint:
    def __init__(self, label: str) -> None:
        self.label = label

    def __repr__(self) -> str:
        return f"PRIVATE-ENDPOINT-{self.label}-SECRET"


class Authority:
    def __init__(self, *, reject: bool = False) -> None:
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
            radio_allowed=False,
        )


class FlashTransport:
    def __init__(self, endpoints: tuple[PrivateEndpoint, PrivateEndpoint]) -> None:
        self.endpoints = endpoints
        self.current = {endpoint: RESTORE_PAYLOAD for endpoint in endpoints}
        self.events: list[tuple[str, str, str | None, int | None]] = []
        self.failures: set[tuple[str, str, str]] = set()

    def _label(self, endpoint: PrivateEndpoint) -> str:
        return endpoint.label

    @staticmethod
    def _kind(image) -> str:
        return "benchmark" if image.name == coordinator.BENCHMARK_NAME else "restore"

    def write_application(self, endpoint, offset: int, image) -> None:
        label, kind = self._label(endpoint), self._kind(image)
        self.events.append(("write", label, kind, offset))
        if ("write", label, kind) in self.failures:
            raise RuntimeError("PRIVATE WRITE FAILURE")
        self.current[endpoint] = image.payload

    def verify_application(self, endpoint, offset: int, image) -> None:
        label, kind = self._label(endpoint), self._kind(image)
        self.events.append(("verify", label, kind, offset))
        if ("verify", label, kind) in self.failures:
            raise RuntimeError("PRIVATE VERIFY FAILURE")
        if self.current[endpoint] != image.payload:
            raise RuntimeError("PRIVATE IMAGE MISMATCH")

    def hard_reset(self, endpoint) -> None:
        label = self._label(endpoint)
        self.events.append(("reset", label, None, None))
        if ("reset", label, "any") in self.failures:
            raise RuntimeError("PRIVATE RESET FAILURE")


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.private = self.root / ".private"
        self.private.mkdir()
        self.benchmark = self.root / coordinator.BENCHMARK_NAME
        self.restore = self.root / coordinator.RESTORE_NAME
        self.benchmark.write_bytes(BENCHMARK_PAYLOAD)
        self.restore.write_bytes(RESTORE_PAYLOAD)
        self.endpoints = (PrivateEndpoint("A"), PrivateEndpoint("B"))
        self.config = coordinator.RunConfig(
            private_endpoints=self.endpoints,
            benchmark_path=self.benchmark,
            restore_path=self.restore,
        )
        self.transport = FlashTransport(self.endpoints)
        self.authority = Authority()
        self.stack = contextlib.ExitStack()

    def __enter__(self):
        self.stack.enter_context(mock.patch.object(coordinator, "ROOT", self.root))
        self.stack.enter_context(mock.patch.object(coordinator, "PRIVATE_ROOT", self.private))
        self.stack.enter_context(mock.patch.object(
            coordinator, "JOURNAL_PATH",
            self.private / "ot140-monocypher-execution-journal.json",
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "EXECUTION_RECEIPT_PATH",
            self.private / "ot140-monocypher-execution-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "RECOVERY_RECEIPT_PATH",
            self.private / "ot140-monocypher-recovery-receipt.json",
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "BENCHMARK_BYTES", len(BENCHMARK_PAYLOAD)
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "BENCHMARK_SHA256", hashlib.sha256(BENCHMARK_PAYLOAD).hexdigest()
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "RESTORE_BYTES", len(RESTORE_PAYLOAD)
        ))
        self.stack.enter_context(mock.patch.object(
            coordinator, "RESTORE_SHA256", hashlib.sha256(RESTORE_PAYLOAD).hexdigest()
        ))
        return self

    def __exit__(self, *args):
        self.stack.close()
        self.temporary.cleanup()


def diagnostics(frame_count: int = 1014):
    return coordinator.protocol.CaptureDiagnostics(
        lifecycle="reenumerated",
        reset_attempts=1,
        lifecycle_polls=3,
        stable_presence_polls=1,
        open_attempts=1,
        start_write_attempts=2,
        read_calls=20,
        empty_reads=2,
        bytes_observed=4096,
        preamble_lines_ignored=0,
        complete_lines=frame_count + 1,
        frame_lines_buffered=frame_count,
    )


def capture_result(label: str):
    return coordinator.protocol.CaptureResult(
        parsed={"candidate_id": "monocypher", "node_fixture": label},
        diagnostics=diagnostics(),
    )


class Ot140CoordinatorTests(unittest.TestCase):
    def assert_safe_error(self, error: coordinator.CoordinatorError) -> None:
        rendered = "".join(traceback.format_exception(error))
        self.assertNotIn("PRIVATE", rendered)
        self.assertIsNone(error.__context__)
        self.assertIsNone(error.__cause__)

    def test_01_success_requires_two_captures_and_restores_each_node(self) -> None:
        with Fixture() as fixture:
            captures = iter((capture_result("A"), capture_result("B")))
            with mock.patch.object(
                coordinator.protocol, "capture_local_primitives",
                side_effect=lambda provider, endpoint: next(captures),
            ) as capture:
                receipt = coordinator.execute(
                    fixture.config, fixture.transport, fixture.authority
                )
            self.assertEqual(capture.call_count, 2)
            self.assertTrue(all(call.args[0] is fixture.transport for call in capture.call_args_list))
            self.assertEqual(receipt["result"], "two_node_monocypher_passed_and_restored")
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(
                [node["capture_diagnostics"]["frame_lines_buffered"] for node in receipt["nodes"]],
                [1014, 1014],
            )
            self.assertTrue(all("result_sha256" in node for node in receipt["nodes"]))
            self.assertEqual(fixture.transport.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertEqual(fixture.transport.current[fixture.endpoints[1]], RESTORE_PAYLOAD)
            expected = [
                ("verify", "A", "restore", coordinator.APPLICATION_OFFSET),
                ("verify", "B", "restore", coordinator.APPLICATION_OFFSET),
                ("reset", "A", None, None),
                ("reset", "B", None, None),
                ("write", "A", "benchmark", coordinator.APPLICATION_OFFSET),
                ("verify", "A", "benchmark", coordinator.APPLICATION_OFFSET),
                ("write", "A", "restore", coordinator.APPLICATION_OFFSET),
                ("verify", "A", "restore", coordinator.APPLICATION_OFFSET),
                ("reset", "A", None, None),
                ("write", "B", "benchmark", coordinator.APPLICATION_OFFSET),
                ("verify", "B", "benchmark", coordinator.APPLICATION_OFFSET),
                ("write", "B", "restore", coordinator.APPLICATION_OFFSET),
                ("verify", "B", "restore", coordinator.APPLICATION_OFFSET),
                ("reset", "B", None, None),
            ]
            self.assertEqual(fixture.transport.events, expected)

    def test_02_authority_rejection_precedes_artifact_or_device_io(self) -> None:
        with Fixture() as fixture:
            fixture.authority.reject = True
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.AUTHORITY_REJECTED)
            self.assertEqual(fixture.transport.events, [])
            self.assertFalse(coordinator.JOURNAL_PATH.exists())
            self.assert_safe_error(caught.exception)

    def test_03_artifact_tamper_fails_before_preflight_or_journal(self) -> None:
        with Fixture() as fixture:
            fixture.benchmark.write_bytes(b"tampered")
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.ARTIFACT_INVALID)
            self.assertEqual(fixture.transport.events, [])
            self.assertFalse(coordinator.JOURNAL_PATH.exists())

    def test_04_preflight_failure_attempts_both_readbacks_and_both_resets(self) -> None:
        with Fixture() as fixture:
            fixture.transport.failures.add(("verify", "A", "restore"))
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.PREFLIGHT_FAILED)
            self.assertEqual(len(fixture.transport.events), 4)
            self.assertEqual([event[0] for event in fixture.transport.events], ["verify", "verify", "reset", "reset"])
            self.assertFalse(coordinator.JOURNAL_PATH.exists())

    def test_05_benchmark_failure_restores_touched_node_and_aborts(self) -> None:
        with Fixture() as fixture:
            fixture.transport.failures.add(("verify", "A", "benchmark"))
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.BENCHMARK_VERIFY_FAILED)
            self.assertEqual(fixture.transport.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assertNotIn(("write", "B", "benchmark", coordinator.APPLICATION_OFFSET), fixture.transport.events)
            receipt = json.loads(coordinator.EXECUTION_RECEIPT_PATH.read_text("ascii"))
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(receipt["failure"]["code"], "benchmark_verify_failed")
            self.assert_safe_error(caught.exception)

    def test_06_capture_error_keeps_only_safe_code_and_diagnostics(self) -> None:
        with Fixture() as fixture:
            error = coordinator.protocol.CaptureError(
                coordinator.protocol.FailureCode.READY_TIMEOUT, diagnostics(0)
            )
            with mock.patch.object(
                coordinator.protocol, "capture_local_primitives", side_effect=error
            ):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.CAPTURE_FAILED)
            self.assertEqual(caught.exception.capture_code, "ready_timeout")
            receipt_text = coordinator.EXECUTION_RECEIPT_PATH.read_text("ascii")
            self.assertIn('"capture_code":"ready_timeout"', receipt_text)
            self.assertNotIn("PRIVATE", receipt_text)
            self.assertNotIn(str(fixture.root), receipt_text)
            self.assertEqual(fixture.transport.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assert_safe_error(caught.exception)

    def test_07_keyboard_interrupt_is_contained_and_restored(self) -> None:
        with Fixture() as fixture:
            with mock.patch.object(
                coordinator.protocol, "capture_local_primitives",
                side_effect=KeyboardInterrupt("PRIVATE INTERRUPT"),
            ):
                with self.assertRaises(coordinator.CoordinatorError) as caught:
                    coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.CAPTURE_FAILED)
            self.assertEqual(fixture.transport.current[fixture.endpoints[0]], RESTORE_PAYLOAD)
            self.assert_safe_error(caught.exception)

    def test_08_existing_journal_consumes_attempt_before_device_io(self) -> None:
        with Fixture() as fixture:
            coordinator.JOURNAL_PATH.write_text("occupied\n", encoding="ascii")
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.execute(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.AUTHORITY_ALREADY_CONSUMED)
            self.assertEqual(fixture.transport.events, [])

    def test_09_recovery_only_restores_and_never_benchmark_writes_or_captures(self) -> None:
        with Fixture() as fixture:
            binding = coordinator._binding()
            grant = fixture.authority.validate(binding, recovery=False)
            journal = coordinator._new_journal(binding, grant)
            for label in ("A", "B"):
                journal["nodes"][label]["installed_app_readback_verified"] = True
                journal["nodes"][label]["preflight_reset_completed"] = True
            journal["nodes"]["A"]["benchmark_write_started"] = True
            fixture.transport.current[fixture.endpoints[0]] = BENCHMARK_PAYLOAD
            self.assertTrue(coordinator._write_new(coordinator.JOURNAL_PATH, journal))
            fixture.benchmark.unlink()
            receipt = coordinator.recover(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(receipt["result"], "recovery_only_restored")
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(fixture.authority.calls[-1][1], True)
            self.assertNotIn(("write", "A", "benchmark", coordinator.APPLICATION_OFFSET), fixture.transport.events)
            self.assertEqual(
                fixture.transport.events,
                [
                    ("write", "A", "restore", coordinator.APPLICATION_OFFSET),
                    ("verify", "A", "restore", coordinator.APPLICATION_OFFSET),
                    ("reset", "A", None, None),
                ],
            )

    def test_10_failed_recovery_can_be_retried_to_success(self) -> None:
        with Fixture() as fixture:
            binding = coordinator._binding()
            grant = fixture.authority.validate(binding, recovery=False)
            journal = coordinator._new_journal(binding, grant)
            for label in ("A", "B"):
                journal["nodes"][label]["installed_app_readback_verified"] = True
                journal["nodes"][label]["preflight_reset_completed"] = True
            journal["nodes"]["A"]["benchmark_write_started"] = True
            fixture.transport.current[fixture.endpoints[0]] = BENCHMARK_PAYLOAD
            self.assertTrue(coordinator._write_new(coordinator.JOURNAL_PATH, journal))
            fixture.transport.failures.add(("write", "A", "restore"))
            with self.assertRaises(coordinator.CoordinatorError) as caught:
                coordinator.recover(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(caught.exception.code, coordinator.FailureCode.RESTORE_FAILED)
            self.assertFalse(coordinator.RECOVERY_RECEIPT_PATH.exists())
            fixture.transport.failures.clear()
            receipt = coordinator.recover(fixture.config, fixture.transport, fixture.authority)
            self.assertEqual(receipt["result"], "recovery_only_restored")
            self.assertEqual(fixture.transport.current[fixture.endpoints[0]], RESTORE_PAYLOAD)

    def test_11_private_values_and_broad_write_surfaces_are_structurally_absent(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("def main(", source)
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("write_flash", source)
        self.assertNotIn("serial_port", source)
        self.assertIn("protocol.capture_local_primitives(", source)
        self.assertIn("ot135_monocypher_protocol_runner.py", source)
        self.assertEqual(coordinator.protocol.MAX_PREAMBLE_BYTES, 512)
        self.assertFalse(hasattr(coordinator.protocol, "MAX_PREAMBLE_LINES"))
        self.assertNotIn("ot132_monocypher_protocol_runner.py", source)
        self.assertNotIn("ot129_monocypher_protocol_runner.py", source)
        self.assertEqual(coordinator.APPLICATION_OFFSET, 0x10000)
        self.assertEqual(len({
            coordinator.JOURNAL_PATH.name,
            coordinator.EXECUTION_RECEIPT_PATH.name,
            coordinator.RECOVERY_RECEIPT_PATH.name,
        }), 3)
        unsafe = dataclasses.replace(diagnostics(), lifecycle="PRIVATE-SECRET")
        self.assertIsNone(coordinator._capture_diagnostics(unsafe))
        endpoint_a = "".join(["same", "-endpoint"])
        endpoint_b = "".join(["same", "-endpoint"])
        self.assertIsNot(endpoint_a, endpoint_b)
        duplicate_config = coordinator.RunConfig(
            private_endpoints=(endpoint_a, endpoint_b),
            benchmark_path=Path("C:/placeholder/ot139_monocypher_quiet_bench.bin"),
            restore_path=Path("C:/placeholder/opentrail_heltec_v4_bench.bin"),
        )
        self.assertFalse(coordinator._config_valid(duplicate_config, recovery=False))


if __name__ == "__main__":
    unittest.main(verbosity=2)



