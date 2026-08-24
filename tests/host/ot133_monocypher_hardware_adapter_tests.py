#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot133_monocypher_hardware_adapter.py"
SPEC = importlib.util.spec_from_file_location("ot133_hardware_adapter_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("adapter module unavailable")
adapter = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = adapter
SPEC.loader.exec_module(adapter)


PORT_A = "PRIVATE-A"
PORT_B = "PRIVATE-B"


class FakeBackend:
    def __init__(self, endpoints: tuple[str, str]) -> None:
        self.endpoints = endpoints


class AdapterTests(unittest.TestCase):
    def _backend(self) -> adapter.EsptoolSerialBackend:
        with mock.patch.object(adapter.importlib.metadata, "version", return_value="5.3.1"):
            return adapter.EsptoolSerialBackend((PORT_A, PORT_B))

    def _run_main(self, arguments, *, factory=FakeBackend):
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = adapter.main(arguments, backend_factory=factory)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_01_authority_gate_returns_exact_nonreusable_grant(self) -> None:
        gate = adapter.ExecutionAuthorityGate(
            ROOT / adapter.authority_contract.AUTHORITY_RELATIVE,
            Path("C:/private/ot129_monocypher_protocol_bench.bin"),
            Path("C:/private/opentrail_heltec_v4_bench.bin"),
        )
        with mock.patch.object(
            adapter.authority_contract,
            "validate_execution_authority",
            return_value="a" * 64,
        ) as validate:
            grant = gate.validate(adapter.coordinator._binding(), recovery=False)
        self.assertEqual(grant.raw_sha256, "a" * 64)
        self.assertEqual(grant.attempt_count, 1)
        self.assertFalse(grant.reusable)
        self.assertFalse(grant.radio_allowed)
        self.assertEqual(validate.call_args.kwargs, {"recovery": False})
        self.assertEqual(validate.call_args.args[3], adapter.ADAPTER_PATH)

    def test_02_authority_gate_rejects_binding_before_validator(self) -> None:
        gate = adapter.ExecutionAuthorityGate(Path("C:/x"), None, Path("C:/y"))
        with mock.patch.object(
            adapter.authority_contract, "validate_execution_authority"
        ) as validate:
            with self.assertRaises(adapter.AdapterError):
                gate.validate(object(), recovery=True)
        validate.assert_not_called()

    def test_03_preflight_only_validates_then_readbacks_and_resets_without_journal(self) -> None:
        config = object()
        backend = object()
        authority = object()
        restore = object()
        with (
            mock.patch.object(Path, "exists", return_value=False),
            mock.patch.object(
                adapter.coordinator,
                "_prepare",
                return_value=(object(), object(), object(), restore),
            ) as prepare,
            mock.patch.object(adapter.coordinator, "_preflight", return_value=True) as preflight,
            mock.patch.object(adapter.coordinator, "execute") as execute,
        ):
            adapter.preflight_only(config, backend, authority)
        prepare.assert_called_once_with(config, authority, recovery=False)
        preflight.assert_called_once_with(config, backend, restore)
        execute.assert_not_called()

    def test_04_preflight_only_refuses_consumed_authority_before_device_io(self) -> None:
        with (
            mock.patch.object(Path, "exists", return_value=True),
            mock.patch.object(adapter.coordinator, "_prepare") as prepare,
            mock.patch.object(adapter.coordinator, "_preflight") as preflight,
        ):
            with self.assertRaises(adapter.AdapterError):
                adapter.preflight_only(object(), object(), object())
        prepare.assert_not_called()
        preflight.assert_not_called()

    def test_05_execute_requires_visual_confirmation_before_backend_creation(self) -> None:
        factory = mock.Mock()
        code, stdout, stderr = self._run_main(
            [
                "--port-a", PORT_A,
                "--port-b", PORT_B,
                "--benchmark-app", "C:/private/ot129_monocypher_protocol_bench.bin",
                "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                "--execute",
            ],
            factory=factory,
        )
        self.assertEqual(code, 2)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "ERROR: OT-133 hardware operation failed\n")
        factory.assert_not_called()

    def test_06_preflight_cli_uses_one_backend_and_never_executes(self) -> None:
        created: list[FakeBackend] = []

        def factory(endpoints):
            created.append(FakeBackend(endpoints))
            return created[0]

        with (
            mock.patch.object(adapter, "preflight_only") as preflight,
            mock.patch.object(adapter.coordinator, "execute") as execute,
        ):
            code, stdout, stderr = self._run_main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--benchmark-app", "C:/private/ot129_monocypher_protocol_bench.bin",
                    "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                    "--preflight-only",
                ],
                factory=factory,
            )
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-133 preflight reset completed on both nodes\n")
        self.assertEqual(len(created), 1)
        self.assertIs(preflight.call_args.args[1], created[0])
        execute.assert_not_called()

    def test_07_execute_cli_passes_same_backend_to_untouched_coordinator(self) -> None:
        created: list[FakeBackend] = []

        def factory(endpoints):
            created.append(FakeBackend(endpoints))
            return created[0]

        with mock.patch.object(adapter.coordinator, "execute", return_value={}) as execute:
            code, stdout, stderr = self._run_main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--benchmark-app", "C:/private/ot129_monocypher_protocol_bench.bin",
                    "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                    "--execute",
                    "--visual-preflight-confirmed",
                ],
                factory=factory,
            )
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-133 two-node attempt completed and restored\n")
        self.assertEqual(len(created), 1)
        config, backend, gate = execute.call_args.args
        self.assertIs(backend, created[0])
        self.assertEqual(config.private_endpoints, (PORT_A, PORT_B))
        self.assertIsInstance(gate, adapter.ExecutionAuthorityGate)

    def test_08_recovery_needs_no_benchmark_or_visual_flag(self) -> None:
        with mock.patch.object(adapter.coordinator, "recover", return_value={}) as recover:
            code, stdout, stderr = self._run_main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                    "--recover",
                ]
            )
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-133 recovery restored all touched nodes\n")
        config, unused_backend, gate = recover.call_args.args
        self.assertTrue(config.benchmark_path.is_absolute())
        self.assertIsNone(gate._benchmark_path)

    def test_09_cli_errors_never_echo_private_values(self) -> None:
        private = "PRIVATE-COM-AND-PATH"
        with mock.patch.object(
            adapter.coordinator, "execute", side_effect=RuntimeError(private)
        ):
            code, stdout, stderr = self._run_main(
                [
                    "--port-a", private,
                    "--port-b", PORT_B,
                    "--benchmark-app", f"C:/{private}/ot129_monocypher_protocol_bench.bin",
                    "--restore-app", f"C:/{private}/opentrail_heltec_v4_bench.bin",
                    "--execute",
                    "--visual-preflight-confirmed",
                ]
            )
        self.assertEqual(code, 2)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "ERROR: OT-133 hardware operation failed\n")
        self.assertNotIn(private, stderr)

    def test_10_backend_uses_only_exact_application_write_and_readback(self) -> None:
        backend = self._backend()
        payload = b"bounded-image"
        image = adapter.coordinator.Image(
            "test.bin", payload, hashlib.sha256(payload).hexdigest()
        )
        commands: list[list[str]] = []

        def run(command, **kwargs):
            commands.append(command)
            if "read-flash" in command:
                Path(command[-1]).write_bytes(payload)
            return subprocess.CompletedProcess(command, 0, b"PRIVATE", b"PRIVATE")

        with mock.patch.object(adapter.subprocess, "run", side_effect=run):
            backend.write_application(PORT_A, 0x10000, image)
            backend.verify_application(PORT_A, 0x10000, image)
        self.assertEqual(len(commands), 2)
        self.assertIn("write-flash", commands[0])
        self.assertIn("read-flash", commands[1])
        self.assertIn("0x10000", commands[0])
        self.assertIn("0x10000", commands[1])
        self.assertNotIn("erase-flash", " ".join(sum(commands, [])))
        self.assertTrue(all("--no-stub" in command for command in commands))

    def test_11_backend_provider_is_endpoint_bound(self) -> None:
        backend = self._backend()
        backend._list_ports = types.SimpleNamespace(
            comports=lambda: [types.SimpleNamespace(device=PORT_A)]
        )
        self.assertTrue(backend.is_present(PORT_A))
        self.assertFalse(backend.is_present(PORT_B))
        with self.assertRaises(adapter.AdapterError):
            backend.is_present("PRIVATE-UNBOUND")

    def test_12_backend_reset_uses_hard_reset_and_fixed_toolchain(self) -> None:
        backend = self._backend()
        with mock.patch.object(
            adapter.subprocess,
            "run",
            return_value=subprocess.CompletedProcess([], 0, b"PRIVATE", b"PRIVATE"),
        ) as run:
            backend.reset(PORT_B)
        command = run.call_args.args[0]
        self.assertEqual(command[0:3], [sys.executable, "-m", "esptool"])
        self.assertIn("esp32s3", command)
        self.assertIn("hard-reset", command)
        self.assertEqual(command[-1], "run")

    def test_13_duplicate_endpoint_aliases_fail_before_backend_or_device_io(self) -> None:
        factory = mock.Mock()
        code, stdout, stderr = self._run_main(
            [
                "--port-a", "COM7",
                "--port-b", "com7",
                "--benchmark-app", "C:/private/ot129_monocypher_protocol_bench.bin",
                "--restore-app", "C:/private/opentrail_heltec_v4_bench.bin",
                "--preflight-only",
            ],
            factory=factory,
        )
        self.assertEqual((code, stdout), (2, ""))
        self.assertEqual(stderr, "ERROR: OT-133 hardware operation failed\n")
        factory.assert_not_called()

    def test_14_adapter_has_no_broad_flash_or_private_output_surface(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("erase-flash", source)
        self.assertNotIn("stdout=", source.replace("stdout=subprocess.PIPE", ""))
        self.assertIn("stdout=subprocess.PIPE", source)
        self.assertIn("stderr=subprocess.PIPE", source)
        self.assertIn("coordinator.execute(config, backend, gate)", source)
        self.assertIn("coordinator.recover(config, backend, gate)", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)


