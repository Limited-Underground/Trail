#!/usr/bin/env python3
"""Adversarial, hardware-free tests for the OT-121 execution coordinator."""

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
SPEC = importlib.util.spec_from_file_location(
    "ot121_local_primitive_runner", TOOLS / "ot121_local_primitive_runner.py"
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
        if digest != hashlib.sha256(BENCHMARK_PAYLOAD).hexdigest():
            raise runner.RunnerError("image digest mismatch")
        return runner.Image(name, BENCHMARK_PAYLOAD, digest)
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
        self.current: dict[str, bytes] = {}

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
        self, private_port: str, baud: int, timeout_seconds: float
    ) -> bytes:
        node = self._node(private_port)
        self.calls.append(("capture", node, baud, timeout_seconds))
        self._raise(f"capture:{node}")
        return f"safe-capture-{node}".encode("ascii")


def fake_parser(raw: bytes) -> dict[str, object]:
    node = raw.decode("ascii")[-1]
    if node not in {"A", "B"}:
        raise runner.RunnerError("frame parse failed")
    return {
        "schema": "OTCBXRF1",
        "version": 1,
        "candidate_id": "espressif_libsodium",
        "scope": "local_primitives_v1",
        "operations_completed": 7,
        "phase2_complete": False,
        "radio_used": False,
        "candidate_selected": False,
        "test_node": node,
    }


class Fixture:
    def __init__(self, directory: str) -> None:
        base = Path(directory).resolve()
        self.benchmark = base / runner.BENCHMARK_NAME
        self.restore = base / runner.RESTORE_NAME
        self.authority = runner.AUTHORITY_PATH.resolve()
        self.journal = base / "private-journal.json"
        self.receipt = base / "public-receipt.json"
        self.config = runner.RunConfig(
            private_ports=(PORT_A, PORT_B),
            authority_path=self.authority,
            authority_sha256=runner.AUTHORITY_RAW_SHA256,
            benchmark_path=self.benchmark,
            benchmark_sha256=hashlib.sha256(BENCHMARK_PAYLOAD).hexdigest(),
            restore_path=self.restore,
            journal_path=self.journal,
            receipt_path=self.receipt,
            capture_timeout_seconds=30.0,
        )


class Ot121LocalPrimitiveRunnerTests(unittest.TestCase):
    def run_fixture(
        self, fixture: Fixture, transport: FakeTransport,
        *, recover: bool = False,
    ) -> dict[str, object]:
        config = fixture.config
        if recover:
            config = runner.RunConfig(**{**config.__dict__, "recover": True})
        with (
            mock.patch.object(runner, "_validate_authority"),
            mock.patch.object(runner, "_read_exact_image", side_effect=fake_images),
        ):
            return runner.execute(config, transport, fake_parser)

    def test_01_exact_authority_validates_against_all_pinned_parents(self) -> None:
        runner._validate_authority(
            runner.AUTHORITY_PATH.resolve(), runner.AUTHORITY_RAW_SHA256
        )
        with self.assertRaises(runner.RunnerError):
            runner._validate_authority(
                runner.AUTHORITY_PATH.resolve(), "0" * 64
            )

    def test_02_happy_path_is_app_only_two_node_and_restores(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
            fixture, transport = Fixture(directory), FakeTransport()
            receipt = self.run_fixture(fixture, transport)
            self.assertEqual(receipt["result"], "two_node_local_primitives_passed_and_restored")
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(receipt["application_offset"], 0x10000)
            self.assertFalse(receipt["claims"]["phase_two_complete"])
            self.assertFalse(receipt["claims"]["radio_used"])
            self.assertFalse(receipt["claims"]["candidate_selected"])
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
                    ("write", "A", "benchmark", 0x10000),
                    ("verify", "A", "benchmark", 0x10000, len(BENCHMARK_PAYLOAD)),
                    ("capture", "A", 115_200, 30.0),
                    ("write", "A", "restore", 0x10000),
                    ("verify", "A", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("reset", "A", "restore"),
                    ("write", "B", "benchmark", 0x10000),
                    ("verify", "B", "benchmark", 0x10000, len(BENCHMARK_PAYLOAD)),
                    ("capture", "B", 115_200, 30.0),
                    ("write", "B", "restore", 0x10000),
                    ("verify", "B", "restore", 0x10000, len(RESTORE_PAYLOAD)),
                    ("reset", "B", "restore"),
                ],
            )
            journal = json.loads(fixture.journal.read_text(encoding="ascii"))
            self.assertEqual(journal["state"], "restored")
            self.assertRegex(journal["run_nonce"], r"^[0-9a-f]{32}$")
            stored = fixture.journal.read_text(encoding="ascii") + fixture.receipt.read_text(encoding="ascii")
            for private in (PORT_A, PORT_B, str(fixture.benchmark), str(fixture.restore)):
                self.assertNotIn(private, stored)

    def test_03_each_primary_failure_restores_every_touched_node(self) -> None:
        cases = (
            "write:benchmark:A", "verify:benchmark:A", "mismatch:benchmark:A",
            "capture:A", "write:benchmark:B", "verify:benchmark:B",
            "mismatch:benchmark:B", "capture:B",
        )
        for failure in cases:
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
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
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
            fixture = Fixture(directory)
            broken = FakeTransport("write:restore:A")
            with self.assertRaises(runner.RunnerError):
                self.run_fixture(fixture, broken)
            receipt = json.loads(fixture.receipt.read_text(encoding="ascii"))
            self.assertFalse(receipt["restoration_complete"])
            self.assertEqual(receipt["result"], "local_primitive_execution_aborted")
            fixture.receipt.unlink()
            recovered = self.run_fixture(fixture, FakeTransport(), recover=True)
            self.assertEqual(recovered["result"], "recovery_only_restored")
            self.assertTrue(recovered["restoration_complete"])

    def test_05_consumed_or_complete_authority_cannot_run_or_recover(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
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
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
            fixture = Fixture(directory)
            for ports in ((PORT_A,), (PORT_A, PORT_A), (PORT_A, PORT_B, "third")):
                with self.subTest(count=len(ports)):
                    config = runner.RunConfig(
                        **{**fixture.config.__dict__, "private_ports": ports}
                    )
                    with self.assertRaises(runner.ArgumentError):
                        runner._preflight_paths(config)

    def test_07_artifact_preflight_failure_does_not_consume_authority(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
            fixture = Fixture(directory)
            with (
                mock.patch.object(runner, "_validate_authority"),
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
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
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
        source = (TOOLS / "ot121_local_primitive_runner.py").read_text(encoding="utf-8")
        for forbidden in (
            "erase-flash", "erase_flash", "write_flash", "read-flash", "read_flash", "0x00000000",
            "0x00008000", "0x00009000", "partition-table.bin",
            "bootloader.bin", "ota_data_initial.bin", "915000000",
            "frequency_hz", "power_dbm",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn('"write-flash"', source)
        self.assertIn('"verify-flash"', source)
        self.assertIn('"0x10000"', source)
        self.assertNotIn("--offset", source)
        execute_source = source[source.index("def execute("):source.index("def _allowed_discarded_serial_bytes")]
        self.assertNotIn("transport.hard_reset(private_port)", execute_source)
        self.assertIn("raw_capture = transport.capture_local_primitives(", execute_source)
        self.assertIn("endpoint = serial.Serial(", source)
        self.assertIn("port=None", source)
        self.assertIn("endpoint.dtr = False", source)
        self.assertIn("endpoint.rts = True", source)
        self.assertIn("endpoint.dtr = inactive_dtr", source)
        self.assertEqual(source.count("endpoint.reset_input_buffer()"), 1)
        self.assertIn("CAPTURE_REOPEN_ATTEMPTS = 3", source)
        self.assertIn("CAPTURE_REOPEN_DELAY_SECONDS = 0.50", source)

    def test_11_baud_is_fixed_to_conservative_115200(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
            fixture = Fixture(directory)
            for override in ({"baud": 230_400}, {"flash_baud": 230_400}):
                with self.subTest(override=override):
                    config = runner.RunConfig(
                        **{**fixture.config.__dict__, **override}
                    )
                    with self.assertRaises(runner.ArgumentError):
                        runner._preflight_paths(config)

    def test_12_benchmark_image_cannot_exceed_factory_slot(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
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
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200, 5.0)
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
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200, 5.0)
        self.assertEqual(captured, first + b"\n" + second + b"\n")
        self.assertNotIn(preamble, captured)
        self.assertTrue(endpoint.closed)
    def test_15_raw_capture_and_private_startup_data_are_not_persisted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot121-runner-test-") as directory:
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
        third = runner.frame_contract.PREFIX + b'{"value":"OTCBXRF1 inside-json"}'
        preamble = b"\x1b[0m startup "
        interframe = b" harmless inter-frame text\t"
        line = preamble + first + interframe + second + third + b"\r\n"
        extracted = runner._capture_frames_from_line(line)
        self.assertEqual(extracted, [first, second, third])
        self.assertNotIn(preamble, b"".join(extracted))
        self.assertNotIn(interframe, b"".join(extracted))
    def test_18_capture_opens_inactive_resets_once_clears_and_keeps_open(self) -> None:
        first = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'
        second = runner.frame_contract.PREFIX + b'{"record_kind":"gate"}'
        events: list[tuple[object, ...]] = []

        class Endpoint:
            def __init__(self) -> None:
                self._port: str | None = None
                self._dtr: bool | None = None
                self._rts: bool | None = None
                self.lines = [first + b"\n", second + b"\n"]

            @property
            def port(self) -> str | None:
                return self._port

            @port.setter
            def port(self, value: str) -> None:
                self._port = value
                events.append(("port_selected", value == PORT_A))

            @property
            def dtr(self) -> bool | None:
                events.append(("dtr_read", self._dtr))
                return self._dtr

            @dtr.setter
            def dtr(self, value: bool) -> None:
                self._dtr = value
                events.append(("dtr", value))

            @property
            def rts(self) -> bool | None:
                return self._rts

            @rts.setter
            def rts(self, value: bool) -> None:
                self._rts = value
                events.append(("rts", value))

            def open(self) -> None:
                events.append(("open", self._dtr, self._rts))

            def reset_input_buffer(self) -> None:
                events.append(("clear",))

            def readline(self, size: int = -1) -> bytes:
                events.append(("read", size))
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                events.append(("close",))

        endpoint = Endpoint()

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            events.append(("construct", args, kwargs))
            return endpoint

        def bounded_sleep(seconds: float) -> None:
            events.append(("sleep", seconds))

        serial_module = types.SimpleNamespace(Serial=serial_factory)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 2),
            mock.patch.object(runner.time, "sleep", side_effect=bounded_sleep),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200, 5.0)

        self.assertEqual(captured, first + b"\n" + second + b"\n")
        self.assertEqual(events, [
            ("construct", (), {
                "port": None,
                "baudrate": 115_200,
                "timeout": runner.SERIAL_READ_TIMEOUT_SECONDS,
            }),
            ("dtr", False),
            ("rts", False),
            ("port_selected", True),
            ("open", False, False),
            ("clear",),
            ("rts", True),
            ("dtr_read", False),
            ("dtr", False),
            ("sleep", runner.CAPTURE_RESET_ASSERT_SECONDS),
            ("rts", False),
            ("dtr_read", False),
            ("dtr", False),
            ("sleep", runner.CAPTURE_BOOT_CHATTER_SECONDS),
            ("read", runner.MAX_PACKED_LINE_BYTES + 1),
            ("read", runner.MAX_PACKED_LINE_BYTES + 1),
            ("rts", False),
            ("dtr", False),
            ("close",),
        ])
        self.assertEqual(events.count(("open", False, False)), 1)
        self.assertEqual(events.count(("rts", True)), 1)
        self.assertNotIn(("dtr", True), events)
        self.assertEqual(events.count(("clear",)), 1)

    def test_19_capture_failure_closes_and_sanitizes(self) -> None:
        class Endpoint:
            def __init__(self) -> None:
                self.closed = False
                self.dtr: bool | None = None
                self.rts: bool | None = None

            def open(self) -> None:
                pass

            def reset_input_buffer(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                raise RuntimeError(f"private read failure {PORT_A}")

            def close(self) -> None:
                self.closed = True

        endpoint = Endpoint()
        serial_module = types.SimpleNamespace(Serial=lambda *args, **kwargs: endpoint)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.time, "sleep"),
        ):
            with self.assertRaisesRegex(
                runner.RunnerError, "^serial capture failed$"
            ) as caught:
                transport.capture_local_primitives(PORT_A, 115_200, 5.0)
        self.assertTrue(endpoint.closed)
        self.assertFalse(endpoint.rts)
        self.assertFalse(endpoint.dtr)
        self.assertNotIn(PORT_A, str(caught.exception))
    def test_20_disconnect_before_first_frame_reopens_without_second_reset(self) -> None:
        frame = runner.frame_contract.PREFIX + b'{"record_kind":"header"}'

        class Endpoint:
            def __init__(self, *, fail_read: bool = False) -> None:
                self.fail_read = fail_read
                self.lines = [frame + b"\n"]
                self.closed = False
                self.port: str | None = None
                self.dtr_history: list[bool] = []
                self.rts_history: list[bool] = []
                self.clears = 0

            @property
            def dtr(self) -> bool:
                return self.dtr_history[-1]

            @dtr.setter
            def dtr(self, value: bool) -> None:
                self.dtr_history.append(value)

            @property
            def rts(self) -> bool:
                return self.rts_history[-1]

            @rts.setter
            def rts(self, value: bool) -> None:
                self.rts_history.append(value)

            def open(self) -> None:
                pass

            def reset_input_buffer(self) -> None:
                self.clears += 1

            def readline(self, size: int = -1) -> bytes:
                del size
                if self.fail_read:
                    self.fail_read = False
                    raise OSError(f"private disconnect {PORT_A}")
                return self.lines.pop(0) if self.lines else b""

            def close(self) -> None:
                self.closed = True

        initial = Endpoint(fail_read=True)
        reopened = Endpoint()
        endpoints = [initial, reopened]
        constructed: list[dict[str, object]] = []
        sleeps: list[float] = []

        def serial_factory(*args: object, **kwargs: object) -> Endpoint:
            self.assertEqual(args, ())
            constructed.append(dict(kwargs))
            return endpoints[len(constructed) - 1]

        serial_module = types.SimpleNamespace(Serial=serial_factory)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.frame_contract, "EXPECTED_FRAME_COUNT", 1),
            mock.patch.object(runner.time, "sleep", side_effect=sleeps.append),
        ):
            captured = transport.capture_local_primitives(PORT_A, 115_200, 5.0)

        self.assertEqual(captured, frame + b"\n")
        self.assertEqual(len(constructed), 2)
        self.assertTrue(all(call["port"] is None for call in constructed))
        self.assertEqual(initial.port, PORT_A)
        self.assertEqual(reopened.port, PORT_A)
        self.assertEqual(initial.rts_history.count(True), 1)
        self.assertEqual(reopened.rts_history.count(True), 0)
        self.assertNotIn(True, initial.dtr_history)
        self.assertNotIn(True, reopened.dtr_history)
        self.assertEqual(initial.clears, 1)
        self.assertEqual(reopened.clears, 0)
        self.assertEqual(sleeps, [
            runner.CAPTURE_RESET_ASSERT_SECONDS,
            runner.CAPTURE_BOOT_CHATTER_SECONDS,
            runner.CAPTURE_REOPEN_DELAY_SECONDS,
        ])
        self.assertTrue(initial.closed)
        self.assertTrue(reopened.closed)

    def test_21_reopen_exhaustion_is_bounded_closed_and_sanitized(self) -> None:
        class Endpoint:
            def __init__(self, *, fail_read: bool = False, fail_open: bool = False) -> None:
                self.fail_read = fail_read
                self.fail_open = fail_open
                self.closed = False
                self.port: str | None = None
                self.dtr_history: list[bool] = []
                self.rts_history: list[bool] = []

            @property
            def dtr(self) -> bool:
                return self.dtr_history[-1]

            @dtr.setter
            def dtr(self, value: bool) -> None:
                self.dtr_history.append(value)

            @property
            def rts(self) -> bool:
                return self.rts_history[-1]

            @rts.setter
            def rts(self, value: bool) -> None:
                self.rts_history.append(value)

            def open(self) -> None:
                if self.fail_open:
                    raise OSError(f"private open failure {PORT_A}")

            def reset_input_buffer(self) -> None:
                pass

            def readline(self, size: int = -1) -> bytes:
                del size
                if self.fail_read:
                    raise OSError(f"private disconnect {PORT_A}")
                return b""

            def close(self) -> None:
                self.closed = True

        endpoints = [
            Endpoint(fail_read=True),
            *[Endpoint(fail_open=True) for _ in range(runner.CAPTURE_REOPEN_ATTEMPTS)],
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

        serial_module = types.SimpleNamespace(Serial=serial_factory)
        transport = object.__new__(runner.EsptoolSerialTransport)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(runner.time, "sleep", side_effect=sleeps.append),
        ):
            with self.assertRaisesRegex(
                runner.RunnerError, "^serial capture failed$"
            ) as caught:
                transport.capture_local_primitives(PORT_A, 115_200, 5.0)

        self.assertEqual(calls, 1 + runner.CAPTURE_REOPEN_ATTEMPTS)
        self.assertEqual(
            sleeps,
            [
                runner.CAPTURE_RESET_ASSERT_SECONDS,
                runner.CAPTURE_BOOT_CHATTER_SECONDS,
                *[
                    runner.CAPTURE_REOPEN_DELAY_SECONDS
                    for _ in range(runner.CAPTURE_REOPEN_ATTEMPTS)
                ],
            ],
        )
        self.assertTrue(all(endpoint.closed for endpoint in endpoints))
        self.assertEqual(endpoints[0].rts_history.count(True), 1)
        self.assertTrue(all(endpoint.rts_history.count(True) == 0 for endpoint in endpoints[1:]))
        self.assertTrue(all(True not in endpoint.dtr_history for endpoint in endpoints))
        self.assertNotIn(PORT_A, str(caught.exception))
if __name__ == "__main__":
    unittest.main(verbosity=2)
