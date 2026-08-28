#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-157 concrete successor adapter."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import inspect
import io
import sys
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools/ot157_noise_xk_radio_hardware_adapter.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


adapter = load("ot157_noise_xk_radio_hardware_adapter_tests_module", MODULE_PATH)

PORT_A = "PRIVATE-A-COM77"
PORT_B = "PRIVATE-B-COM88"
RUNNER_SHA256 = hashlib.sha256(adapter.coordinator.SUCCESSOR_RUNNER_PATH.read_bytes()).hexdigest()
BINDING_FIELDS = {
    "benchmark_name": "ot153_noise_xk_radio_cost.bin",
    "benchmark_bytes": 296_640,
    "benchmark_sha256": "ed2eef319d5bca22d1d89a0be61e63463ada1a8fb3277238cdf95cf93093cd3c",
    "restore_name": adapter.coordinator.RESTORE_NAME,
    "restore_bytes": adapter.coordinator.RESTORE_BYTES,
    "restore_sha256": adapter.coordinator.RESTORE_SHA256,
    "application_offset": adapter.coordinator.APPLICATION_OFFSET,
    "baud": adapter.coordinator.BAUD,
    "runner_name": adapter.coordinator.RUNNER_NAME,
    "runner_sha256": RUNNER_SHA256,
    "runner_schema": adapter.coordinator.runner.SCHEMA,
}


class FakeAuthorityContract:
    AUTHORITY_RELATIVE = adapter.FUTURE_AUTHORITY_RELATIVE

    def load_preparation_binding(self, benchmark, restore, adapter_path, *, recovery):
        del benchmark, restore, adapter_path, recovery
        return dict(BINDING_FIELDS)

    def validate_execution_authority(self, *args, **kwargs):
        del args, kwargs
        return "a" * 64


class FakeBackend:
    def __init__(self, endpoints: tuple[str, str]) -> None:
        self.endpoints = endpoints


class Tests(unittest.TestCase):
    def run_main(self, arguments, *, factory=FakeBackend, authority_loader=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        loader = authority_loader or (lambda: FakeAuthorityContract())
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = adapter.main(
                arguments,
                backend_factory=factory,
                authority_loader=loader,
            )
        return code, stdout.getvalue(), stderr.getvalue()

    def test_01_source_hashes_and_successor_composition_are_exact(self) -> None:
        self.assertTrue(adapter.sources_match())
        for path, expected in (
            (adapter.FROZEN_ADAPTER_PATH, adapter.FROZEN_ADAPTER_SHA256),
            (adapter.COORDINATOR_PATH, adapter.COORDINATOR_SHA256),
            (adapter.RUNTIME_PATH, adapter.RUNTIME_SHA256),
        ):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)
        self.assertEqual(adapter.coordinator.JOURNAL_SCHEMA, "OT157NXJ0")
        self.assertEqual(adapter.coordinator.RUNNER_NAME, "ot156_noise_xk_radio_runner.py")
        self.assertIs(
            adapter.ReconnectableEsptoolSerialBackend,
            adapter.runtime.ReconnectableEsptoolSerialBackend,
        )
        self.assertIs(
            adapter.ReconnectableSerialRadioEndpoint,
            adapter.runtime.ReconnectableSerialRadioEndpoint,
        )
        default_backend = inspect.signature(adapter.main).parameters[
            "backend_factory"
        ].default
        self.assertIs(default_backend, adapter.ReconnectableEsptoolSerialBackend)

    def test_02_reconnectable_backend_inherits_only_frozen_device_operations(self) -> None:
        successor = adapter.ReconnectableEsptoolSerialBackend
        frozen = adapter.runtime.frozen_adapter.EsptoolSerialBackend
        self.assertTrue(issubclass(successor, frozen))
        self.assertIs(successor.write_application, frozen.write_application)
        self.assertIs(successor.verify_application, frozen.verify_application)
        self.assertIs(successor.hard_reset, frozen.hard_reset)
        self.assertIsNot(successor.open_radio_endpoint, frozen.open_radio_endpoint)
        self.assertTrue(adapter.runtime.frozen_sources_match())

    def test_03_ot158_authority_contract_and_record_are_exact(self) -> None:
        self.assertEqual(
            adapter.FUTURE_AUTHORITY_PATH.name,
            "OT-158-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json",
        )
        self.assertTrue(adapter.FUTURE_AUTHORITY_TOOL_PATH.is_file())
        self.assertTrue(adapter.FUTURE_AUTHORITY_PATH.is_file())
        contract = adapter._load_authority_contract()
        self.assertEqual(contract.AUTHORITY_SCHEMA, "OT158NXRA0")
        self.assertEqual(
            contract.AUTHORITY_RELATIVE,
            adapter.FUTURE_AUTHORITY_RELATIVE,
        )
        authority, raw = contract._load_canonical(
            adapter.FUTURE_AUTHORITY_PATH, "authority"
        )
        self.assertEqual(authority["schema"], "OT158NXRA0")
        self.assertEqual(authority["authority_id"], contract.AUTHORITY_ID)
        self.assertEqual(raw, contract.canonical_document(authority))
        self.assertTrue(authority["owner_authorization"]["granted"])
        self.assertEqual(authority["execution"]["attempt_count"], 1)
        self.assertFalse(authority["consumption"]["reusable"])
        self.assertTrue(all(value is False for value in authority["claims"].values()))

    def test_04_fake_future_gate_composes_ot157_coordinator_and_same_backend(self) -> None:
        created: list[FakeBackend] = []

        def factory(endpoints):
            created.append(FakeBackend(endpoints))
            return created[-1]

        with mock.patch.object(adapter.coordinator, "execute", return_value={}) as execute:
            code, stdout, stderr = self.run_main(
                [
                    "--port-a", PORT_A,
                    "--port-b", PORT_B,
                    "--benchmark-app", "C:/PRIVATE/ot153_noise_xk_radio_cost.bin",
                    "--restore-app", "C:/PRIVATE/opentrail_heltec_v4_bench.bin",
                    "--execute", "--visual-preflight-confirmed",
                ],
                factory=factory,
            )
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual(stdout, "OK: OT-153 two-node radio attempt completed and restored\n")
        config, backend, gate = execute.call_args.args
        self.assertIs(backend, created[0])
        self.assertEqual(config.private_endpoints, (PORT_A, PORT_B))
        self.assertEqual(config.binding.runner_name, "ot156_noise_xk_radio_runner.py")
        self.assertEqual(config.binding.runner_sha256, RUNNER_SHA256)
        self.assertIsInstance(gate, adapter.ExecutionAuthorityGate)

    def test_05_invalid_private_cli_is_generic_and_precedes_authority(self) -> None:
        authority_loader = mock.Mock()
        factory = mock.Mock()
        code, stdout, stderr = self.run_main(
            [
                "--port-a", "COM77", "--port-b", "com77",
                "--benchmark-app", "C:/PRIVATE/benchmark.bin",
                "--restore-app", "C:/PRIVATE/restore.bin",
                "--preflight-only",
            ],
            factory=factory,
            authority_loader=authority_loader,
        )
        self.assertEqual((code, stdout), (2, ""))
        self.assertEqual(stderr, "ERROR: OT-153 hardware operation failed\n")
        self.assertNotIn("PRIVATE", stderr)
        self.assertNotIn("COM77", stderr)
        authority_loader.assert_not_called()
        factory.assert_not_called()

    def test_06_surface_has_no_authority_override_or_broad_flash_path(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("--authority", source)
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("erase-flash", source)
        self.assertNotIn("write_application", source)
        self.assertNotIn("verify_application", source)
        self.assertNotIn("hard_reset", source)
        self.assertIn("ot158_noise_xk_radio_execution_authority.py", source)
        self.assertIn("ReconnectableEsptoolSerialBackend", source)
        self.assertEqual(adapter.ADAPTER_PATH, MODULE_PATH)


if __name__ == "__main__":
    unittest.main(verbosity=2)
