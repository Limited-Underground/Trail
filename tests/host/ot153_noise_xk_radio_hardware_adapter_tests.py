#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-153 concrete adapter."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import subprocess
import sys
import types
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot153_noise_xk_radio_hardware_adapter.py"
SPEC = importlib.util.spec_from_file_location("ot153_radio_adapter_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-153 adapter unavailable")
adapter = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = adapter
SPEC.loader.exec_module(adapter)


PORT_A = "PRIVATE-A"
PORT_B = "PRIVATE-B"
RUNNER_SHA256 = hashlib.sha256(adapter.RUNNER_PATH.read_bytes()).hexdigest()
BINDING_FIELDS = {
    "benchmark_name": "ot153-noise-xk-radio-v0.bin",
    "benchmark_bytes": 300_000,
    "benchmark_sha256": "b" * 64,
    "restore_name": adapter.coordinator.RESTORE_NAME,
    "restore_bytes": adapter.coordinator.RESTORE_BYTES,
    "restore_sha256": adapter.coordinator.RESTORE_SHA256,
    "application_offset": adapter.coordinator.APPLICATION_OFFSET,
    "baud": adapter.coordinator.BAUD,
    "runner_name": adapter.coordinator.RUNNER_NAME,
    "runner_sha256": RUNNER_SHA256,
    "runner_schema": adapter.coordinator.runner.SCHEMA,
}
BINDING = adapter.coordinator.ExecutionBinding(**BINDING_FIELDS)


class FakeAuthorityContract:
    AUTHORITY_RELATIVE = adapter.FUTURE_AUTHORITY_RELATIVE

    def __init__(self) -> None:
        self.validate_calls = []

    def load_preparation_binding(self, benchmark, restore, adapter_path, *, recovery):
        del benchmark, restore, adapter_path, recovery
        return dict(BINDING_FIELDS)

    def validate_execution_authority(self, *args, **kwargs):
        self.validate_calls.append((args, kwargs))
        return "a" * 64


class FakeBackend:
    def __init__(self, endpoints: tuple[str, str]) -> None:
        self.endpoints = endpoints


class FakeSerial:
    def __init__(self, lines: list[bytes] | None = None) -> None:
        self.lines = list(lines or [])
        self.writes: list[bytes] = []
        self.closed = False
        self.dtr = True
        self.rts = True

    def write(self, value: bytes) -> None:
        self.writes.append(value)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return self.lines.pop(0) if self.lines else b""

    def close(self) -> None:
        self.closed = True


class Tests(unittest.TestCase):
    def _backend(self) -> adapter.EsptoolSerialBackend:
        fake_serial_module = types.SimpleNamespace(Serial=mock.Mock())
        with (
            mock.patch.object(adapter.importlib.metadata, "version", return_value="5.3.1"),
            mock.patch.dict(sys.modules, {"serial": fake_serial_module}),
        ):
            return adapter.EsptoolSerialBackend((PORT_A, PORT_B))

    def _run_main(self, arguments, *, factory=FakeBackend, contract=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        authority_contract = contract or FakeAuthorityContract()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = adapter.main(
                arguments,
                backend_factory=factory,
                authority_loader=lambda: authority_contract,
            )
        return code, stdout.getvalue(), stderr.getvalue(), authority_contract

    def test_01_future_authority_is_fixed_and_missing_authority_refuses(self) -> None:
        self.assertEqual(
            adapter.FUTURE_AUTHORITY_PATH.name,
            "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json",
        )
        with (
            mock.patch.object(Path, "is_file", return_value=False),
            self.assertRaises(adapter.AdapterError),
        ):
            adapter._load_authority_contract()

    def test_02_gate_returns_only_one_nonreusable_radio_grant(self) -> None:
        contract = FakeAuthorityContract()
        gate = adapter.ExecutionAuthorityGate(
            contract,
            BINDING,
            Path("C:/private/ot153-noise-xk-radio-v0.bin"),
            Path("C:/private/opentrail_heltec_v4_bench.bin"),
        )
        grant = gate.validate(BINDING, recovery=False)
        self.assertEqual(grant.raw_sha256, "a" * 64)
        self.assertEqual(grant.attempt_count, 1)
        self.assertFalse(grant.reusable)
        self.assertTrue(grant.radio_allowed)
        call = contract.validate_calls[0]
        self.assertEqual(call[0][0], adapter.FUTURE_AUTHORITY_PATH)
        self.assertEqual(call[0][3], adapter.ADAPTER_PATH)
        self.assertEqual(call[1], {"recovery": False})

    def test_03_binding_mismatch_rejected_before_authority_validator(self) -> None:
        contract = FakeAuthorityContract()
        gate = adapter.ExecutionAuthorityGate(contract, BINDING, None, Path("C:/restore.bin"))
        with self.assertRaises(adapter.AdapterError):
            gate.validate(object(), recovery=True)
        self.assertEqual(contract.validate_calls, [])

    def test_04_execute_without_visual_confirmation_precedes_authority_and_backend(self) -> None:
        authority_loader = mock.Mock()
        factory = mock.Mock()
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = adapter.main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--benchmark-app", "C:/private/ot153-noise-xk-radio-v0.bin",
                    "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                    "--execute",
                ],
                backend_factory=factory,
                authority_loader=authority_loader,
            )
        self.assertEqual((code, stdout.getvalue()), (2, ""))
        self.assertEqual(stderr.getvalue(), "ERROR: OT-153 hardware operation failed\n")
        authority_loader.assert_not_called()
        factory.assert_not_called()

    def test_05_execute_uses_exact_future_gate_and_same_backend(self) -> None:
        created: list[FakeBackend] = []

        def factory(endpoints):
            created.append(FakeBackend(endpoints))
            return created[0]

        with mock.patch.object(adapter.coordinator, "execute", return_value={}) as execute:
            code, stdout, stderr, unused_contract = self._run_main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--benchmark-app", "C:/private/ot153-noise-xk-radio-v0.bin",
                    "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                    "--execute", "--visual-preflight-confirmed",
                ],
                factory=factory,
            )
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-153 two-node radio attempt completed and restored\n")
        config, backend, gate = execute.call_args.args
        self.assertIs(backend, created[0])
        self.assertEqual(config.private_endpoints, (PORT_A, PORT_B))
        self.assertEqual(config.binding, BINDING)
        self.assertIsInstance(gate, adapter.ExecutionAuthorityGate)

    def test_06_recovery_needs_no_benchmark_and_never_executes(self) -> None:
        with (
            mock.patch.object(adapter.coordinator, "recover", return_value={}) as recover,
            mock.patch.object(adapter.coordinator, "execute") as execute,
        ):
            code, stdout, stderr, unused_contract = self._run_main([
                "--port-a", PORT_A,
                "--port-b", PORT_B,
                "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                "--recover",
            ])
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-153 recovery restored all touched nodes\n")
        config, unused_backend, gate = recover.call_args.args
        self.assertTrue(config.benchmark_path.is_absolute())
        self.assertIsNone(gate._benchmark_path)
        execute.assert_not_called()

    def test_07_backend_pins_esptool_and_exact_app_only_commands(self) -> None:
        backend = self._backend()
        payload = b"bounded-image"
        image = adapter.coordinator.Image("test.bin", payload, hashlib.sha256(payload).hexdigest())
        commands: list[list[str]] = []

        def run(command, **kwargs):
            del kwargs
            commands.append(command)
            if "read-flash" in command:
                Path(command[-1]).write_bytes(payload)
            return subprocess.CompletedProcess(command, 0, b"PRIVATE", b"PRIVATE")

        with mock.patch.object(adapter.subprocess, "run", side_effect=run):
            backend.write_application(PORT_A, 0x10000, image)
            backend.verify_application(PORT_A, 0x10000, image)
            backend.hard_reset(PORT_A)
        self.assertEqual(commands[0][0:3], [sys.executable, "-m", "esptool"])
        self.assertIn("write-flash", commands[0])
        self.assertIn("read-flash", commands[1])
        self.assertIn("0x10000", commands[0])
        self.assertIn("0x10000", commands[1])
        self.assertTrue(all("erase-flash" not in command for command in commands))
        self.assertTrue(all("--no-stub" in command for command in commands[:2]))
        self.assertIn("hard-reset", commands[2])

    def test_08_serial_endpoint_extracts_prefixed_receipt_and_writes_ascii(self) -> None:
        serial = FakeSerial([
            b"I (1) ot153: OT153 PROFILE configured=yes\r\n",
            b"I (2) ot153: OT153 ABORT session_hash=aa attempt_hash=bb wiped=yes tx=no\r\n",
        ])
        endpoint = adapter.SerialRadioEndpoint(serial)
        endpoint.write_command("abort 0000000000000001 0000000000000002")
        receipt = endpoint.expect("ABORT", 1000)
        self.assertEqual(receipt.kind, "ABORT")
        self.assertEqual(serial.writes, [b"abort 0000000000000001 0000000000000002\n"])
        endpoint.close()
        self.assertTrue(serial.closed)
        self.assertFalse(serial.dtr)
        self.assertFalse(serial.rts)

    def test_09_serial_endpoint_rejects_unbound_commands_and_receipt_sequence(self) -> None:
        command_serial = FakeSerial()
        endpoint = adapter.SerialRadioEndpoint(command_serial)
        for command in ("restart", "profile", "status"):
            endpoint.write_command(command)
        self.assertEqual(
            command_serial.writes, [b"restart\n", b"profile\n", b"status\n"]
        )
        with self.assertRaises(adapter.AdapterError):
            endpoint.write_command("commands")
        serial = FakeSerial([b"OT153 REJECT command=send reason=private\n"])
        endpoint = adapter.SerialRadioEndpoint(serial)
        with self.assertRaises(adapter.AdapterError):
            endpoint.expect("TX_START", 1000)

    def test_10_duplicate_alias_and_private_errors_are_closed(self) -> None:
        factory = mock.Mock()
        code, stdout, stderr, unused_contract = self._run_main(
            [
                "--port-a", "COM7", "--port-b", "com7",
                "--benchmark-app", "C:/PRIVATE/ot153-noise-xk-radio-v0.bin",
                "--restore-app", "C:/PRIVATE/opentrail_heltec_v4_bench.bin",
                "--preflight-only",
            ],
            factory=factory,
        )
        self.assertEqual((code, stdout), (2, ""))
        self.assertEqual(stderr, "ERROR: OT-153 hardware operation failed\n")
        self.assertNotIn("PRIVATE", stderr)
        factory.assert_not_called()

    def test_11_adapter_surface_has_no_broad_flash_or_authority_override(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("erase-flash", source)
        self.assertNotIn("--authority", source)
        self.assertIn("stdout=subprocess.PIPE", source)
        self.assertIn("stderr=subprocess.PIPE", source)
        self.assertIn("coordinator.execute(config, backend, gate)", source)
        self.assertIn("coordinator.recover(config, backend, gate)", source)
        self.assertEqual(adapter.ESPTOOL_VERSION, "5.3.1")


if __name__ == "__main__":
    unittest.main(verbosity=2)
