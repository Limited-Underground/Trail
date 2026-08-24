#!/usr/bin/env python3
"""Adversarial, hardware-free tests for the OT-127 Monocypher comparison coordinator."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
AUTHORITY_STUBBED = not (TOOLS / "ot127_monocypher_retry_authority.py").is_file()
if AUTHORITY_STUBBED:
    authority_stub = types.ModuleType("ot127_monocypher_retry_authority")
    authority_stub.AUTHORITY_RAW_SHA256 = "1" * 64
    authority_stub.AUTHORITY_CANONICAL_SHA256 = "2" * 64
    authority_stub.AUTHORITY_PIN = (
        authority_stub.AUTHORITY_RAW_SHA256,
        authority_stub.AUTHORITY_CANONICAL_SHA256,
    )
    authority_stub.CONSUMED_AUTHORITY_RAW_SHA256 = (
        "b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf"
    )
    authority_stub.validate_parent_files = lambda: {}
    authority_stub.load = lambda path, pin: {}
    authority_stub.validate_authority = lambda value, parents: {
        "canonical_sha256": authority_stub.AUTHORITY_CANONICAL_SHA256,
        "phase_two_execution_authorized": True,
        "benchmark_executed": False,
    }
    sys.modules[authority_stub.__name__] = authority_stub

SPEC = importlib.util.spec_from_file_location(
    "ot127_monocypher_retry_runner", TOOLS / "ot127_monocypher_retry_runner.py"
)
assert SPEC is not None and SPEC.loader is not None
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


PORT_A = "PRIVATE_ENDPOINT_ALPHA"
PORT_B = "PRIVATE_ENDPOINT_BRAVO"
BENCHMARK_PAYLOAD = b"bounded benchmark image"
RESTORE_PAYLOAD = b"exact restore image fixture"


def fake_images(path: Path, name: str, digest: str, size: int | None = None) -> runner.Image:
    del path, size
    if name == runner.BENCHMARK_NAME:
        if digest != runner.BENCHMARK_SHA256:
            raise runner.RunnerError("image digest mismatch")
        return runner.Image(name, BENCHMARK_PAYLOAD, runner.BENCHMARK_SHA256)
    if name == runner.RESTORE_NAME and digest == runner.RESTORE_SHA256:
        # The production loader pins the real 473,152-byte OT-115 digest.  This
        # unit seam uses a small stand-in while preserving readback verification.
        return runner.Image(
            name, RESTORE_PAYLOAD, hashlib.sha256(RESTORE_PAYLOAD).hexdigest()
        )
    raise runner.RunnerError("image identity mismatch")


class FakeTransport:
    def __init__(self, fail: str | None = None) -> None:
        self.fail = fail
        self.calls: list[tuple[object, ...]] = []
        self.current: dict[str, bytes] = {
            "A": RESTORE_PAYLOAD,
            "B": RESTORE_PAYLOAD,
        }

    @staticmethod
    def _node(port: str) -> str:
        return "A" if port == PORT_A else "B"

    @staticmethod
    def _kind(image: runner.Image) -> str:
        return "benchmark" if image.name == runner.BENCHMARK_NAME else "restore"

    def _raise(self, point: str) -> None:
        if self.fail == point:
            raise RuntimeError(f"private failure at {point} {PORT_A} {PORT_B}")

    def write_application(self, private_port: str, offset: int, image: runner.Image) -> None:
        node, kind = self._node(private_port), self._kind(image)
        self.calls.append(("write", node, kind, offset))
        self._raise(f"write:{kind}:{node}")
        self.current[node] = image.payload

    def verify_application(
        self, private_port: str, offset: int, image: runner.Image
    ) -> None:
        node, kind = self._node(private_port), self._kind(image)
        self.calls.append(("verify", node, kind, offset, image.size))
        self._raise(f"verify:{kind}:{node}")
        if self.fail == f"mismatch:{kind}:{node}":
            raise RuntimeError("exact image mismatch")
        if self.current.get(node) != image.payload:
            raise RuntimeError("exact image mismatch")

    def hard_reset(self, private_port: str) -> None:
        node = self._node(private_port)
        kind = "benchmark" if self.current.get(node) == BENCHMARK_PAYLOAD else "restore"
        self.calls.append(("reset", node, kind))
        self._raise(f"reset:{kind}:{node}")

    def capture_local_primitives(
        self, private_port: str, baud: int
    ) -> bytes:
        node = self._node(private_port)
        self.calls.append(("capture", node, baud))
        self._raise(f"capture:{node}")
        return f"safe-capture-{node}".encode("ascii")


def fake_parser(raw: bytes) -> dict[str, object]:
    node = raw.decode("ascii")[-1]
    if node not in {"A", "B"}:
        raise runner.RunnerError("frame parse failed")
    return {
        "schema": "OTCBXRF2",
        "version": 2,
        "candidate_id": "parser-controlled-value",
        "candidate_role": "parser-controlled-value",
        "selection_eligible": True,
        "unavailable_operations": [],
        "scope": "candidate_local_v2",
        "operations_completed": 5,
        "phase2_complete": False,
        "radio_used": False,
        "candidate_selected": False,
        "test_node": node,
    }


class Fixture:
    def __init__(self, directory: str) -> None:
        base = Path(directory).resolve()
        self.root = base
        self.private = base / ".private"
        self.private.mkdir()
        self.benchmark = base / runner.BENCHMARK_NAME
        self.restore = base / runner.RESTORE_NAME
        self.authority = runner.AUTHORITY_PATH.resolve()
        self.journal = self.private / "ot127-monocypher-corrective-retry-journal.json"
        self.receipt = self.private / "ot127-monocypher-corrective-retry-execution-receipt.json"
        self.recovery_receipt = self.private / "ot127-monocypher-corrective-retry-recovery-receipt.json"
        self.config = runner.RunConfig(
            private_ports=(PORT_A, PORT_B),
            authority_path=self.authority,
            authority_sha256=runner.AUTHORITY_RAW_SHA256,
            benchmark_path=self.benchmark,
            benchmark_sha256=runner.BENCHMARK_SHA256,
            restore_path=self.restore,
            receipt_path=self.receipt,

        )


class Ot127MonocypherRunnerTests(unittest.TestCase):
    def run_fixture(
        self, fixture: Fixture, transport: FakeTransport,
        *, recover: bool = False,
    ) -> dict[str, object]:
        config = fixture.config
        if recover:
            config = runner.RunConfig(**{
                **config.__dict__,
                "recover": True,
                "receipt_path": fixture.recovery_receipt,
            })
        with (
            mock.patch.object(runner, "ROOT", fixture.root),
            mock.patch.object(runner, "JOURNAL_PATH", fixture.journal),
            mock.patch.object(runner, "EXECUTION_RECEIPT_PATH", fixture.receipt),
            mock.patch.object(runner, "RECOVERY_RECEIPT_PATH", fixture.recovery_receipt),
            mock.patch.object(runner, "_validate_authority"),
            mock.patch.object(runner, "_validate_continuation_parent"),
            mock.patch.object(runner, "_read_exact_image", side_effect=fake_images),
        ):
            return runner.execute(config, transport, fake_parser)

    def test_01_exact_authority_validates_against_all_pinned_parents(self) -> None:
        if not AUTHORITY_STUBBED:
            runner._validate_authority(
                runner.AUTHORITY_PATH.resolve(), runner.AUTHORITY_RAW_SHA256
            )
            with self.assertRaises(runner.RunnerError):
                runner._validate_authority(
                    runner.AUTHORITY_PATH.resolve(), "0" * 64
                )
        runner._validate_continuation_parent()
        with (
            mock.patch.object(runner, "CONTINUATION_PARENT_RAW_SHA256", "0" * 64),
            self.assertRaises(runner.RunnerError),
        ):
            runner._validate_continuation_parent()
        with tempfile.TemporaryDirectory(
            prefix="ot127-monocypher-parent-test-"
        ) as directory:
            copied = Path(directory).resolve() / runner.CONTINUATION_PARENT_PATH.name
            copied.write_bytes(runner.CONTINUATION_PARENT_PATH.read_bytes())
            with (
                mock.patch.object(runner, "CONTINUATION_PARENT_PATH", copied),
                self.assertRaises(runner.RunnerError),
            ):
                runner._validate_continuation_parent()

    def test_02_happy_path_is_app_only_two_node_and_restores(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture, transport = Fixture(directory), FakeTransport()
            receipt = self.run_fixture(fixture, transport)
            self.assertEqual(receipt["result"], "two_node_monocypher_corrective_retry_passed_and_restored")
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(receipt["application_offset"], 0x10000)
            self.assertFalse(receipt["claims"]["phase_two_complete"])
            self.assertFalse(receipt["claims"]["radio_used"])
            self.assertFalse(receipt["claims"]["candidate_selected"])
            self.assertEqual(receipt["comparison_boundary"], runner.COMPARISON_BOUNDARY)
            self.assertEqual(
                receipt["continuation_parent"], runner.CONTINUATION_PARENT
            )
            self.assertTrue(all(
                node["installed_app_readback_verified"]
                for node in receipt["nodes"]
            ))
            self.assertTrue(all(
                node["local_primitive_result"]["candidate_id"]
                == "parser-controlled-value"
                for node in receipt["nodes"]
            ))
            writes = [call for call in transport.calls if call[0] == "write"]
            self.assertEqual(
                writes,
                [
                    ("write", "A", "benchmark", 0x10000),
                    ("write", "A", "restore", 0x10000),
                    ("write", "B", "benchmark", 0x10000),
                    ("write", "B", "restore", 0x10000),
                ],
            )
            self.assertTrue(all(call[3] == 0x10000 for call in writes))
            self.assertEqual(
                transport.calls,
                [
                    ("verify", "A", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("verify", "B", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("reset", "A", "restore"),
                    ("reset", "B", "restore"),
                    ("write", "A", "benchmark", 0x10000),
                    ("verify", "A", "benchmark", 0x10000, len(BENCHMARK_PAYLOAD)),
                    ("capture", "A", 115_200),
                    ("write", "A", "restore", 0x10000),
                    ("verify", "A", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("reset", "A", "restore"),
                    ("write", "B", "benchmark", 0x10000),
                    ("verify", "B", "benchmark", 0x10000, len(BENCHMARK_PAYLOAD)),
                    ("capture", "B", 115_200),
                    ("write", "B", "restore", 0x10000),
                    ("verify", "B", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("reset", "B", "restore"),
                ],
            )
            journal = json.loads(fixture.journal.read_text(encoding="ascii"))
            self.assertEqual(journal["state"], "restored")
            self.assertEqual(journal["comparison_boundary"], runner.COMPARISON_BOUNDARY)
            self.assertEqual(
                journal["continuation_parent"], runner.CONTINUATION_PARENT
            )
            self.assertTrue(all(
                journal["nodes"][node]["installed_app_readback_verified"]
                for node in ("A", "B")
            ))
            self.assertRegex(journal["run_nonce"], r"^[0-9a-f]{32}$")
            stored = fixture.journal.read_text(encoding="ascii") + fixture.receipt.read_text(encoding="ascii")
            for private in (PORT_A, PORT_B, str(fixture.benchmark), str(fixture.restore)):
                self.assertNotIn(private, stored)

    def test_02a_installed_readback_failure_still_resets_both_before_journal(self) -> None:
        for failure in (
            "verify:restore:A", "mismatch:restore:A",
            "verify:restore:B", "mismatch:restore:B",
        ):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory(
                    prefix="ot127-monocypher-runner-test-"
                ) as directory:
                    fixture, transport = Fixture(directory), FakeTransport(failure)
                    with self.assertRaisesRegex(
                        runner.RunnerError,
                        "^installed application preflight failed$",
                    ):
                        self.run_fixture(fixture, transport)
                    self.assertEqual(
                        transport.calls,
                        [
                            ("verify", "A", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                            ("verify", "B", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                            ("reset", "A", "restore"),
                            ("reset", "B", "restore"),
                        ],
                    )
                    self.assertFalse(fixture.journal.exists())
                    self.assertFalse(fixture.receipt.exists())

    def test_02b_preflight_reset_failure_still_attempts_both_and_no_journal(self) -> None:
        for failure in ("reset:restore:A", "reset:restore:B"):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory(
                    prefix="ot127-monocypher-runner-test-"
                ) as directory:
                    fixture, transport = Fixture(directory), FakeTransport(failure)
                    with self.assertRaisesRegex(
                        runner.RunnerError,
                        "^installed application preflight failed$",
                    ):
                        self.run_fixture(fixture, transport)
                    self.assertEqual(
                        [call[:2] for call in transport.calls],
                        [
                            ("verify", "A"), ("verify", "B"),
                            ("reset", "A"), ("reset", "B"),
                        ],
                    )
                    self.assertFalse(fixture.journal.exists())
                    self.assertFalse(fixture.receipt.exists())

    def test_02c_journal_creation_occurs_only_after_both_preflight_resets(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ot127-monocypher-runner-test-"
        ) as directory:
            fixture, transport = Fixture(directory), FakeTransport()
            with (
                mock.patch.object(runner, "ROOT", fixture.root),
                mock.patch.object(runner, "JOURNAL_PATH", fixture.journal),
                mock.patch.object(runner, "EXECUTION_RECEIPT_PATH", fixture.receipt),
                mock.patch.object(runner, "_validate_authority"),
                mock.patch.object(runner, "_validate_continuation_parent"),
                mock.patch.object(runner, "_read_exact_image", side_effect=fake_images),
                mock.patch.object(
                    runner, "_create_journal",
                    side_effect=runner.RunnerError("journal creation failed"),
                ),
                self.assertRaisesRegex(
                    runner.RunnerError, "^journal creation failed$"
                ),
            ):
                runner.execute(fixture.config, transport, fake_parser)
            self.assertEqual(
                [call[:2] for call in transport.calls],
                [
                    ("verify", "A"), ("verify", "B"),
                    ("reset", "A"), ("reset", "B"),
                ],
            )
            self.assertFalse(fixture.journal.exists())
            self.assertFalse(fixture.receipt.exists())
    def test_03_each_primary_failure_restores_every_touched_node(self) -> None:
        cases = (
            "write:benchmark:A", "verify:benchmark:A", "mismatch:benchmark:A",
            "capture:A", "write:benchmark:B", "verify:benchmark:B",
            "mismatch:benchmark:B", "capture:B",
        )
        for failure in cases:
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
                    fixture, transport = Fixture(directory), FakeTransport(failure)
                    with self.assertRaises(runner.RunnerError):
                        self.run_fixture(fixture, transport)
                    journal = json.loads(fixture.journal.read_text(encoding="ascii"))
                    self.assertEqual(journal["state"], "aborted")
                    self.assertTrue(fixture.receipt.exists())
                    for node in ("A", "B"):
                        if journal["nodes"][node]["benchmark_write_started"]:
                            self.assertTrue(journal["nodes"][node]["restore_reset_completed"])

    def test_04_restore_failure_is_publicly_safe_and_recoverable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            broken = FakeTransport("write:restore:A")
            with self.assertRaises(runner.RunnerError):
                self.run_fixture(fixture, broken)
            receipt = json.loads(fixture.receipt.read_text(encoding="ascii"))
            self.assertFalse(receipt["restoration_complete"])
            self.assertEqual(receipt["result"], "monocypher_corrective_retry_execution_aborted")
            self.assertEqual(receipt["comparison_boundary"], runner.COMPARISON_BOUNDARY)
            fixture.receipt.unlink()
            recovered = self.run_fixture(fixture, FakeTransport(), recover=True)
            self.assertEqual(recovered["result"], "corrective_retry_recovery_only_restored")
            self.assertTrue(recovered["restoration_complete"])
            self.assertEqual(recovered["comparison_boundary"], runner.COMPARISON_BOUNDARY)

    def test_04a_baseexception_restore_attempts_both_nodes(self) -> None:
        for failure_type in (KeyboardInterrupt, SystemExit):
            with self.subTest(failure_type=failure_type.__name__):
                with tempfile.TemporaryDirectory(
                    prefix="ot127-monocypher-runner-test-"
                ) as directory:
                    fixture = Fixture(directory)
                    benchmark = runner.Image(
                        runner.BENCHMARK_NAME,
                        BENCHMARK_PAYLOAD,
                        runner.BENCHMARK_SHA256,
                    )
                    journal = runner._new_journal(benchmark, "a" * 32)
                    for node in ("A", "B"):
                        journal["nodes"][node]["installed_app_readback_verified"] = True
                        journal["nodes"][node]["benchmark_write_started"] = True

                    class BaseFailureTransport(FakeTransport):
                        def __init__(self) -> None:
                            super().__init__()
                            self.current = {
                                "A": BENCHMARK_PAYLOAD,
                                "B": BENCHMARK_PAYLOAD,
                            }

                        def write_application(
                            self, private_port: str, offset: int,
                            image: runner.Image,
                        ) -> None:
                            node, kind = self._node(private_port), self._kind(image)
                            self.calls.append(("write", node, kind, offset))
                            if node == "A" and kind == "restore":
                                raise failure_type("restore interrupted")
                            self.current[node] = image.payload

                    transport = BaseFailureTransport()
                    with mock.patch.object(runner, "JOURNAL_PATH", fixture.journal):
                        runner._create_journal(fixture.journal, journal)
                    with self.assertRaisesRegex(
                        runner.RunnerError, "^recovery failed$"
                    ) as caught:
                        self.run_fixture(fixture, transport, recover=True)
                    self.assertNotIn("restore interrupted", str(caught.exception))
                    self.assertIn(
                        ("write", "B", "restore", 0x10000), transport.calls
                    )
                    self.assertIn(
                        ("verify", "B", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                        transport.calls,
                    )
                    self.assertIn(("reset", "B", "restore"), transport.calls)
                    receipt = json.loads(
                        fixture.recovery_receipt.read_text(encoding="ascii")
                    )
                    self.assertEqual(
                        receipt["result"], "corrective_retry_recovery_failed"
                    )
                    self.assertFalse(receipt["restoration_complete"])
                    self.assertEqual(
                        receipt["comparison_boundary"], runner.COMPARISON_BOUNDARY
                    )

    def test_05_consumed_or_complete_authority_cannot_run_or_recover(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            self.run_fixture(fixture, FakeTransport())
            fixture.receipt.unlink()
            untouched = FakeTransport()
            with self.assertRaises(runner.RunnerError):
                self.run_fixture(fixture, untouched)
            with self.assertRaises(runner.RunnerError):
                self.run_fixture(fixture, untouched, recover=True)
            self.assertEqual(untouched.calls, [])

    def test_06_exactly_two_distinct_private_ports_are_required(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            for ports in ((PORT_A,), (PORT_A, PORT_A), (PORT_A, PORT_B, "third")):
                with self.subTest(count=len(ports)):
                    config = runner.RunConfig(
                        **{**fixture.config.__dict__, "private_ports": ports}
                    )
                    with self.assertRaises(runner.ArgumentError):
                        runner._preflight_paths(config)

    def test_06a_private_journal_path_is_fixed_and_not_a_cli_argument(self) -> None:
        self.assertEqual(
            runner.JOURNAL_PATH,
            ROOT / ".private" / "ot127-monocypher-corrective-retry-journal.json",
        )
        self.assertEqual(
            runner.EXECUTION_RECEIPT_PATH,
            ROOT / ".private"
            / "ot127-monocypher-corrective-retry-execution-receipt.json",
        )
        self.assertEqual(
            runner.RECOVERY_RECEIPT_PATH,
            ROOT / ".private"
            / "ot127-monocypher-corrective-retry-recovery-receipt.json",
        )
        self.assertEqual(runner.JOURNAL_SCHEMA, "OT127MCRJ0")
        self.assertEqual(runner.RECEIPT_SCHEMA, "OT127MCER0")
        self.assertNotIn("journal_path", runner.RunConfig.__dataclass_fields__)
        self.assertNotIn("--private-journal", runner._parser()._option_string_actions)
        with tempfile.TemporaryDirectory(
            prefix="ot127-monocypher-runner-test-"
        ) as directory:
            fixture = Fixture(directory)
            for invalid in (
                Path(directory).resolve() / "public.json",
                fixture.private / "alternate-name.json",
            ):
                with (
                    mock.patch.object(runner, "JOURNAL_PATH", invalid),
                    self.assertRaises(runner.ArgumentError),
                ):
                    runner._preflight_paths(fixture.config)

    def test_06b_private_journal_reparse_ancestry_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ot127-monocypher-runner-test-"
        ) as directory:
            fixture = Fixture(directory)
            with (
                mock.patch.object(
                    runner, "_has_reparse_or_symlink_ancestry", return_value=True
                ),
                self.assertRaises(runner.ArgumentError),
            ):
                runner._preflight_paths(fixture.config)

    def test_07_artifact_preflight_failure_does_not_consume_authority(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            with (
                mock.patch.object(runner, "ROOT", fixture.root),
                mock.patch.object(runner, "JOURNAL_PATH", fixture.journal),
                mock.patch.object(runner, "_validate_authority"),
                mock.patch.object(runner, "_validate_continuation_parent"),
                mock.patch.object(
                    runner, "_read_exact_image",
                    side_effect=runner.RunnerError("image digest mismatch"),
                ),
            ):
                with self.assertRaises(runner.RunnerError):
                    runner.execute(fixture.config, FakeTransport(), fake_parser)
            self.assertFalse(fixture.journal.exists())
            self.assertFalse(fixture.receipt.exists())

    def test_08_malformed_journal_and_invalid_offset_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            fixture.journal.write_text('{"schema":"bad"}\n', encoding="ascii")
            with self.assertRaises(runner.RunnerError):
                self.run_fixture(fixture, FakeTransport(), recover=True)
        with self.assertRaises(runner.RunnerError):
            runner.EsptoolSerialTransport._require_offset(0)
        with self.assertRaises(runner.RunnerError):
            runner.EsptoolSerialTransport._require_offset(0x8000)

    def test_09_cli_and_process_errors_never_echo_private_values(self) -> None:
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = runner.main(["--port-a", PORT_A, "--port-b", PORT_B])
        self.assertEqual(result, 2)
        combined = stdout.getvalue() + stderr.getvalue()
        self.assertNotIn(PORT_A, combined)
        self.assertNotIn(PORT_B, combined)
        self.assertNotIn("Traceback", combined)

        backend = object.__new__(runner.EsptoolSerialTransport)
        backend._python = sys.executable
        process_error = RuntimeError(f"failure {PORT_A} {PORT_B}")
        with mock.patch.object(runner.subprocess, "run", side_effect=process_error):
            with self.assertRaises(runner.RunnerError) as caught:
                backend.hard_reset(PORT_A)
        self.assertNotIn(PORT_A, str(caught.exception))
        self.assertNotIn(PORT_B, str(caught.exception))

    def test_10_source_has_no_radio_or_broad_flash_operation(self) -> None:
        source = (TOOLS / "ot127_monocypher_retry_runner.py").read_text(encoding="utf-8")
        for forbidden in (
            "erase-flash", "erase_flash", "write_flash", "read_flash", "0x00000000",
            "0x00008000", "0x00009000", "partition-table.bin",
            "bootloader.bin", "ota_data_initial.bin", "915000000",
            "frequency_hz", "power_dbm",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn('"write-flash"', source)
        self.assertIn('"read-flash"', source)
        self.assertNotIn('"verify-flash"', source)
        self.assertIn('"--no-stub"', source)
        self.assertIn('"0x10000"', source)
        self.assertNotIn("--offset", source)
        execute_source = source[source.index("def execute("):source.index("def _allowed_discarded_serial_bytes")]
        self.assertNotIn("transport.hard_reset(private_port)", execute_source)
        self.assertIn("raw_capture = transport.capture_local_primitives(", execute_source)
        self.assertIn("endpoint = serial.Serial(", source)
        self.assertIn("port=None", source)
        self.assertIn("endpoint.dtr = False", source)
        self.assertNotIn("endpoint.rts = True", source)
        self.assertIn("self.hard_reset(private_port)", source)
        self.assertLess(
            source.index("self.hard_reset(private_port)"),
            source.index("return self._open_capture_endpoint("),
        )
        self.assertEqual(source.count("endpoint.reset_input_buffer()"), 0)
        self.assertIn("CAPTURE_OPEN_ATTEMPTS = 5", source)
        self.assertIn("CAPTURE_OPEN_RETRY_DELAY_SECONDS = 0.25", source)
        self.assertIn("CAPTURE_EMPTY_READ_LIMIT = 8", source)
        self.assertGreaterEqual(runner.FIRST_FRAME_GRACE_SECONDS, 10.0)
        self.assertEqual(runner.CAPTURE_DEADLINE_SECONDS, 180.0)
        self.assertNotIn("capture_timeout_seconds", runner.RunConfig.__dataclass_fields__)
        self.assertNotIn(
            "--capture-timeout-seconds", runner._parser()._option_string_actions
        )
        self.assertEqual(runner.CAPTURE_CYCLE_ATTEMPTS, 2)
        self.assertLess(
            runner.CAPTURE_REENUMERATION_SECONDS
            + (runner.CAPTURE_OPEN_ATTEMPTS - 1)
            * runner.CAPTURE_OPEN_RETRY_DELAY_SECONDS,
            runner.FIRMWARE_STARTUP_DELAY_SECONDS,
        )
        self.assertNotIn('parser.add_argument("--benchmark-sha256"', source)
        self.assertNotIn('parser.add_argument("--private-journal"', source)
        self.assertIn('ROOT / ".private" / "ot127-monocypher-corrective-retry-journal.json"', source)
        self.assertNotIn("ot123-monocypher-recovery-journal.json", source)
        self.assertNotIn("ot124-monocypher-execution-receipt.json", source)
        self.assertNotIn("ot125", source.lower())
        self.assertIn("import ot127_monocypher_retry_authority", source)
        self.assertEqual(runner.JOURNAL_SCHEMA, "OT127MCRJ0")
        self.assertEqual(runner.RECEIPT_SCHEMA, "OT127MCER0")
        self.assertEqual(
            runner.CONTINUATION_PARENT_RAW_SHA256,
            "247b0b80e64a3f6bf6654be279e90dcbd80a067c52ef861313a6f370c0355941",
        )
        accepted = (TOOLS / "ot123_monocypher_runner.py").read_bytes()
        self.assertEqual(
            hashlib.sha256(accepted).hexdigest(),
            "f6c4070512d1c8d0d58bd386646f1e4098084b275610a7834dbf0ee0145eb1d1",
        )
        accepted_retry = (TOOLS / "ot125_monocypher_retry_runner.py").read_bytes()
        self.assertEqual(
            hashlib.sha256(accepted_retry).hexdigest(),
            "47022c46ce6d911998b5457516e250e3dec7dcc8dbe1e0c8e799a0cacfc23150",
        )
        self.assertEqual(runner.BENCHMARK_BYTES, 186_640)
        self.assertEqual(runner.BENCHMARK_SHA256, "5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64")

    def test_10a_production_readback_hashes_exact_application_bytes(self) -> None:
        image = runner.Image(
            runner.RESTORE_NAME,
            RESTORE_PAYLOAD,
            hashlib.sha256(RESTORE_PAYLOAD).hexdigest(),
        )
        transport = object.__new__(runner.EsptoolSerialTransport)
        calls: list[tuple[str, list[str]]] = []

        def exact_readback(private_port: str, operation: list[str]) -> None:
            calls.append((private_port, operation))
            self.assertEqual(
                operation[:3],
                ["read-flash", "0x10000", str(len(RESTORE_PAYLOAD))],
            )
            Path(operation[3]).write_bytes(RESTORE_PAYLOAD)

        transport._esptool = exact_readback
        transport.verify_application(PORT_A, runner.APPLICATION_OFFSET, image)
        self.assertEqual(len(calls), 1)

        def corrupt_readback(private_port: str, operation: list[str]) -> None:
            del private_port
            Path(operation[3]).write_bytes(b"x" * len(RESTORE_PAYLOAD))

        transport._esptool = corrupt_readback
        with self.assertRaisesRegex(
            runner.RunnerError, "^application readback mismatch$"
        ):
            transport.verify_application(PORT_A, runner.APPLICATION_OFFSET, image)

    def test_11_baud_is_fixed_to_conservative_115200(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            for override in ({"baud": 230_400}, {"flash_baud": 230_400}):
                with self.subTest(override=override):
                    config = runner.RunConfig(
                        **{**fixture.config.__dict__, **override}
                    )
                    with self.assertRaises(runner.ArgumentError):
                        runner._preflight_paths(config)

    def test_12_benchmark_image_cannot_exceed_factory_slot(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            image_path = Path(directory).resolve() / runner.BENCHMARK_NAME
            payload = b"x" * (runner.FACTORY_SLOT_BYTES + 1)
            image_path.write_bytes(payload)
            with self.assertRaises(runner.RunnerError):
                runner._read_exact_image(
                    image_path,
                    runner.BENCHMARK_NAME,
                    hashlib.sha256(payload).hexdigest(),
                )
    def test_13_capture_resynchronizes_first_frame_and_ignores_chatter(self) -> None:
        startup = b"ESP-ROM harmless startup PRIVATE_STARTUP_BYTES "
        first = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        second = runner.frame_contract.PREFIX + b'{"record_kind":"gate"}'

        class Endpoint:
            def __init__(self) -> None:
                self.lines = [b"ordinary boot chatter\r\n", startup + first + b"\r\n", second + b"\n"]
                self.closed = False

            def open(self) -> None:
                pass

            def reset_input_buffer(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        serial_module = types.SimpleNamespace(Serial=lambda *args, **kwargs: endpoint)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(transport, "hard_reset"),
            mock.patch.object(runner.time, "sleep"),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(captured, first + b"\n" + second + b"\n")
        self.assertNotIn(startup, captured)
        self.assertNotIn(b"PRIVATE_STARTUP_BYTES", captured)
        self.assertTrue(endpoint.closed)

    def test_14_capture_accepts_bounded_harmless_preamble_on_later_frame(self) -> None:
        first = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        preamble = b"\x1b[0m later harmless startup bytes\t"
        second = runner.frame_contract.PREFIX + b'{"record_kind":"gate"}'

        class Endpoint:
            def __init__(self) -> None:
                self.lines = [first + b"\n", preamble + second + b"\n"]
                self.closed = False

            def open(self) -> None:
                pass

            def reset_input_buffer(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        serial_module = types.SimpleNamespace(Serial=lambda *args, **kwargs: endpoint)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(transport, "hard_reset"),
            mock.patch.object(runner.time, "sleep"),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(captured, first + b"\n" + second + b"\n")
        self.assertNotIn(preamble, captured)
        self.assertTrue(endpoint.closed)
    def test_15_raw_capture_and_private_startup_data_are_not_persisted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-monocypher-runner-test-") as directory:
            fixture = Fixture(directory)
            receipt = self.run_fixture(fixture, FakeTransport())
            stored = fixture.journal.read_bytes() + fixture.receipt.read_bytes()
            self.assertNotIn(b"safe-capture-A", stored)
            self.assertNotIn(b"safe-capture-B", stored)
            self.assertFalse(receipt["privacy"]["raw_capture_recorded"])
            self.assertTrue(all("capture_sha256" in node for node in receipt["nodes"]))
    def test_16_packed_extraction_rejects_malformed_oversized_and_ambiguous(self) -> None:
        valid = runner.frame_contract.PREFIX + b'{"record_kind":"gate"}'
        self.assertEqual(runner._capture_frames_from_line(valid + b"\n"), [valid])
        self.assertEqual(runner._capture_frames_from_line(b"prefix-free chatter\r\n"), [])

        overlong = b"x" * (runner.MAX_STARTUP_SYNC_BYTES + 1) + valid + b"\n"
        with self.assertRaisesRegex(
            runner.RunnerError, "^serial preamble too long$"
        ):
            runner._capture_frames_from_line(overlong)

        oversized = (
            runner.frame_contract.PREFIX
            + b'{"value":"'
            + b"x" * runner.frame_contract.MAX_FRAME_BYTES
            + b'"}\n'
        )
        ambiguous = runner.frame_contract.PREFIX + b"garbage" + valid + b"\n"
        malformed_second = valid + b" inter " + runner.frame_contract.PREFIX + b"not-json\n"
        noncanonical = runner.frame_contract.PREFIX + b'{ "value":1}\n'
        truncated = runner.frame_contract.PREFIX + b'{"value":1}'
        nonobject = runner.frame_contract.PREFIX + b'[1,2]\n'
        nonfinite = runner.frame_contract.PREFIX + b'{"value":NaN}\n'
        duplicate_key = runner.frame_contract.PREFIX + b'{"value":1,"value":2}\n'
        trailing = valid + b" trailing text\n"
        disallowed = b"later\x00binary " + valid + b"\n"
        too_many = valid * (runner.MAX_PACKED_FRAMES_PER_LINE + 1) + b"\n"
        for line in (
            oversized, ambiguous, malformed_second, noncanonical, truncated,
            nonobject, nonfinite, duplicate_key,
            trailing, disallowed, too_many,
        ):
            with self.subTest(length=len(line)):
                with self.assertRaises(runner.RunnerError):
                    runner._capture_frames_from_line(line)

    def test_17_two_or_more_packed_frames_are_extracted_exactly(self) -> None:
        first = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        second = runner.frame_contract.PREFIX + b'{"record_kind":"gate","gate":1}'
        third = runner.frame_contract.PREFIX + b'{"value":"OTCBXRF2 inside-json"}'
        preamble = b"\x1b[0m startup "
        interframe = b" harmless inter-frame text\t"
        line = preamble + first + interframe + second + third + b"\r\n"
        extracted = runner._capture_frames_from_line(line)
        self.assertEqual(extracted, [first, second, third])
        self.assertNotIn(preamble, b"".join(extracted))
        self.assertNotIn(interframe, b"".join(extracted))
    def test_18_capture_hard_resets_before_fresh_open_without_clearing(self) -> None:
        first = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        second = runner.frame_contract.PREFIX + b'{"record_kind":"gate"}'
        events: list[tuple[object, ...]] = []

        class Endpoint:
            def __init__(self) -> None:
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.lines = [first + b"\n", second + b"\n"]
                self.closed = False

            def open(self) -> None:
                events.append(("open", self.dtr, self.rts))

            def readline(self, size: int = -1) -> bytes:
                events.append(("read", size))
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                self.closed = True
                events.append(("close",))

        endpoint = Endpoint()

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            events.append(("construct", args, kwargs))
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        transport.hard_reset = (
            lambda private_port: events.append(("hard_reset", private_port == PORT_A))
        )
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(
                runner.time, "sleep",
                side_effect=lambda seconds: events.append(("sleep", seconds)),
            ),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)

        self.assertEqual(captured, first + b"\n" + second + b"\n")
        self.assertLess(events.index(("hard_reset", True)), next(
            index for index, event in enumerate(events) if event[0] == "construct"
        ))
        self.assertEqual(events.count(("hard_reset", True)), 1)
        self.assertEqual(events.count(("sleep", runner.CAPTURE_REENUMERATION_SECONDS)), 1)
        self.assertEqual(sum(event[0] == "construct" for event in events), 1)
        self.assertEqual(sum(event[0] == "open" for event in events), 1)
        self.assertFalse(hasattr(endpoint, "clears"))
        self.assertNotIn(("rts", True), events)
        self.assertNotIn(("dtr", True), events)
        self.assertTrue(endpoint.closed)

    def test_19_two_pre_frame_read_failures_are_bounded_closed_and_sanitized(self) -> None:
        class Endpoint:
            def __init__(self) -> None:
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                raise OSError(f"private read failure {PORT_A}")

            def close(self) -> None:
                self.closed = True

        endpoints: list[Endpoint] = []

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            del args, kwargs
            endpoint = Endpoint()
            endpoints.append(endpoint)
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
        ):
            with self.assertRaisesRegex(
                runner.RunnerError, "^serial capture failed$"
            ) as caught:
                transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(hard_reset.call_count, runner.CAPTURE_CYCLE_ATTEMPTS)
        self.assertEqual(len(endpoints), runner.CAPTURE_CYCLE_ATTEMPTS)
        self.assertTrue(all(endpoint.closed for endpoint in endpoints))
        self.assertNotIn(PORT_A, str(caught.exception))

    def test_20_pre_frame_disconnect_gets_one_reset_and_fresh_handle(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'

        class Endpoint:
            def __init__(self, fail_read: bool) -> None:
                self.fail_read = fail_read
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                if self.fail_read:
                    raise OSError(f"private disconnect {PORT_A}")
                return frame + b"\n"

            def close(self) -> None:
                self.closed = True

        endpoints = [Endpoint(True), Endpoint(False)]
        constructed = 0

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal constructed
            del args, kwargs
            endpoint = endpoints[constructed]
            constructed += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 1),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(captured, frame + b"\n")
        self.assertEqual(hard_reset.call_count, 2)
        self.assertEqual(constructed, 2)
        self.assertTrue(all(endpoint.closed for endpoint in endpoints))

    def test_21_second_cycle_open_exhaustion_is_bounded_and_sanitized(self) -> None:
        class Endpoint:
            def __init__(self, fail_open: bool = False) -> None:
                self.fail_open = fail_open
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                if self.fail_open:
                    raise OSError(f"private open failure {PORT_A}")

            def readline(self, size: int = -1) -> bytes:
                del size
                raise OSError(f"private disconnect {PORT_A}")

            def close(self) -> None:
                self.closed = True

        endpoints = [
            Endpoint(),
            *[Endpoint(True) for _ in range(runner.CAPTURE_OPEN_ATTEMPTS)],
        ]
        calls = 0
        sleeps: list[float] = []

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal calls
            self.assertEqual(args, ())
            self.assertIsNone(kwargs["port"])
            endpoint = endpoints[calls]
            calls += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep", side_effect=sleeps.append),
        ):
            with self.assertRaisesRegex(
                runner.RunnerError, "^serial capture failed$"
            ) as caught:
                transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(hard_reset.call_count, 2)
        self.assertEqual(calls, 1 + runner.CAPTURE_OPEN_ATTEMPTS)
        self.assertEqual(
            sleeps,
            [runner.CAPTURE_REENUMERATION_SECONDS,
             runner.CAPTURE_REENUMERATION_SECONDS]
            + [runner.CAPTURE_OPEN_RETRY_DELAY_SECONDS]
            * (runner.CAPTURE_OPEN_ATTEMPTS - 1),
        )
        self.assertTrue(all(endpoint.closed for endpoint in endpoints))
        self.assertNotIn(PORT_A, str(caught.exception))

    def test_22_delayed_first_frame_gets_ten_seconds_on_same_handle(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'

        class Clock:
            now = 0.0

            def monotonic(self) -> float:
                return self.now

        clock = Clock()

        class Endpoint:
            def __init__(self) -> None:
                self.lines = [b""] * 19 + [frame + b"\n"]
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False
                self.reads = 0

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                self.reads += 1
                clock.now += 0.5
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        constructions = 0

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal constructions
            del args, kwargs
            constructions += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 1),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
            mock.patch.object(runner.time, "monotonic", side_effect=clock.monotonic),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(captured, frame + b"\n")
        self.assertGreater(endpoint.reads, runner.CAPTURE_EMPTY_READ_LIMIT)
        self.assertGreaterEqual(clock.now, runner.FIRST_FRAME_GRACE_SECONDS)
        hard_reset.assert_called_once_with(PORT_A)
        self.assertEqual(constructions, 1)
        self.assertTrue(endpoint.closed)
    def test_23_no_reset_or_reopen_after_first_accepted_frame(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'

        class Endpoint:
            def __init__(self) -> None:
                self.lines = [frame + b"\n"]
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                if self.lines:
                    return self.lines.pop(0)
                raise OSError(f"private disconnect {PORT_A}")

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        constructions = 0

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal constructions
            del args, kwargs
            constructions += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
        ):
            with self.assertRaisesRegex(
                runner.RunnerError, "^serial capture failed$"
            ):
                transport.capture_local_primitives(PORT_A, 115_200)
        hard_reset.assert_called_once_with(PORT_A)
        self.assertEqual(constructions, 1)
        self.assertTrue(endpoint.closed)

    def test_23a_empty_reads_after_first_frame_never_reopen(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'

        class Clock:
            now = 0.0

            def monotonic(self) -> float:
                return self.now

        clock = Clock()

        class Endpoint:
            def __init__(self) -> None:
                self.first = True
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                if self.first:
                    self.first = False
                    clock.now += 1.0
                    return frame + b"\n"
                clock.now += 30.0
                return b""

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        constructions = 0

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal constructions
            del args, kwargs
            constructions += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
            mock.patch.object(runner.time, "monotonic", side_effect=clock.monotonic),
            self.assertRaisesRegex(
                runner.RunnerError, "^serial capture incomplete$"
            ),
        ):
            transport.capture_local_primitives(PORT_A, 115_200)
        hard_reset.assert_called_once_with(PORT_A)
        self.assertEqual(constructions, 1)
        self.assertGreaterEqual(clock.now, runner.CAPTURE_DEADLINE_SECONDS)
        self.assertTrue(endpoint.closed)
    def test_24_capture_deadline_starts_only_after_open(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        opened = False

        class Endpoint:
            def __init__(self) -> None:
                self.port: str | None = None
                self.dtr = False
                self.rts = False

            def open(self) -> None:
                nonlocal opened
                opened = True

            def readline(self, size: int = -1) -> bytes:
                del size
                return frame + b"\n"

            def close(self) -> None:
                pass

        def checked_clock() -> float:
            self.assertTrue(opened)
            return 1.0

        transport = object.__new__(runner.EsptoolSerialTransport)
        transport.hard_reset = lambda private_port: None
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=lambda **kwargs: Endpoint())
            }),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 1),
            mock.patch.object(runner.time, "sleep"),
            mock.patch.object(runner.time, "monotonic", side_effect=checked_clock),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(captured, frame + b"\n")

    def test_25_retry_cycle_keeps_original_180_second_deadline(self) -> None:
        class Clock:
            now = 0.0

            def monotonic(self) -> float:
                return self.now

        clock = Clock()

        class Endpoint:
            def __init__(self) -> None:
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False
                self.reads = 0

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                self.reads += 1
                clock.now += 0.5
                return b""

            def close(self) -> None:
                self.closed = True

        endpoints: list[Endpoint] = []

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            del args, kwargs
            endpoint = Endpoint()
            endpoints.append(endpoint)
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
            mock.patch.object(runner.time, "monotonic", side_effect=clock.monotonic),
            self.assertRaisesRegex(
                runner.RunnerError, "^serial capture incomplete$"
            ),
        ):
            transport.capture_local_primitives(PORT_A, 115_200)
        self.assertEqual(hard_reset.call_count, 2)
        self.assertEqual(len(endpoints), 2)
        self.assertGreaterEqual(
            endpoints[0].reads, runner.CAPTURE_EMPTY_READ_LIMIT
        )
        self.assertGreater(endpoints[1].reads, 0)
        self.assertEqual(clock.now, runner.CAPTURE_DEADLINE_SECONDS)
        self.assertTrue(all(endpoint.closed for endpoint in endpoints))

    def test_26_retry_handle_is_not_opened_without_full_grace_remaining(self) -> None:
        class Clock:
            now = 0.0

            def monotonic(self) -> float:
                return self.now

        clock = Clock()

        class Endpoint:
            def __init__(self) -> None:
                self.port: str | None = None
                self.dtr = False
                self.rts = False
                self.closed = False

            def open(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                clock.now += 21.875
                return b""

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        constructions = 0

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            nonlocal constructions
            del args, kwargs
            constructions += 1
            return endpoint

        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {
                "serial": types.SimpleNamespace(Serial=serial_factory)
            }),
            mock.patch.object(transport, "hard_reset") as hard_reset,
            mock.patch.object(runner.time, "sleep"),
            mock.patch.object(runner.time, "monotonic", side_effect=clock.monotonic),
            self.assertRaisesRegex(
                runner.RunnerError, "^serial capture incomplete$"
            ),
        ):
            transport.capture_local_primitives(PORT_A, 115_200)
        hard_reset.assert_called_once_with(PORT_A)
        self.assertEqual(constructions, 1)
        self.assertEqual(clock.now, 175.0)
        self.assertTrue(endpoint.closed)
if __name__ == "__main__":
    unittest.main(verbosity=2)
