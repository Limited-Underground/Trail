#!/usr/bin/env python3
"""Focused no-hardware tests for the OT-160 corrected adapter composition."""

from __future__ import annotations

import hashlib
import importlib.util
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot160_noise_xk_radio_hardware_adapter.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


adapter = load("ot160_noise_xk_radio_hardware_adapter_tests_module", MODULE_PATH)

PORT_A = "PRIVATE-A-COM77"
PORT_B = "PRIVATE-B-COM88"


class Tests(unittest.TestCase):
    def _backend(self):
        frozen_adapter = adapter.runtime.frozen_adapter
        fake_serial_module = types.SimpleNamespace(Serial=mock.Mock())
        with (
            mock.patch.object(
                frozen_adapter.importlib.metadata,
                "version",
                return_value=frozen_adapter.ESPTOOL_VERSION,
            ),
            mock.patch.dict(sys.modules, {"serial": fake_serial_module}),
        ):
            return adapter.ReconnectableEsptoolSerialBackend((PORT_A, PORT_B))

    @staticmethod
    def _image(payload: bytes):
        return adapter.coordinator.Image(
            "exact-trail-fixture.bin",
            payload,
            hashlib.sha256(payload).hexdigest(),
        )

    def _run_preflight(self, first_readback: bytes):
        payload = b"exact-composed-application-readback"
        restore = self._image(payload)
        benchmark = self._image(b"unused-benchmark")
        binding = object()
        grant = object()
        authority = object()
        config = types.SimpleNamespace(private_endpoints=(PORT_A, PORT_B))
        backend = self._backend()
        commands: list[list[str]] = []
        read_count = 0

        def run(command, **kwargs):
            nonlocal read_count
            del kwargs
            command = list(command)
            commands.append(command)
            if "write-flash" in command or "erase-flash" in command:
                raise AssertionError("preflight attempted a write")
            if "read-flash" in command:
                read_count += 1
                Path(command[-1]).write_bytes(
                    first_readback if read_count == 1 else payload
                )
            return subprocess.CompletedProcess(command, 0, b"", b"")

        with tempfile.TemporaryDirectory(prefix="ot160-preflight-test-") as directory:
            private_root = Path(directory)
            journal = private_root / "journal.json"
            execution_receipt = private_root / "execution-receipt.json"
            recovery_receipt = private_root / "recovery-receipt.json"
            with (
                mock.patch.object(adapter.coordinator, "JOURNAL_PATH", journal),
                mock.patch.object(adapter.coordinator.frozen, "JOURNAL_PATH", journal),
                mock.patch.object(
                    adapter.coordinator,
                    "EXECUTION_RECEIPT_PATH",
                    execution_receipt,
                ),
                mock.patch.object(
                    adapter.coordinator.frozen,
                    "EXECUTION_RECEIPT_PATH",
                    execution_receipt,
                ),
                mock.patch.object(
                    adapter.coordinator,
                    "RECOVERY_RECEIPT_PATH",
                    recovery_receipt,
                ),
                mock.patch.object(
                    adapter.coordinator.frozen,
                    "RECOVERY_RECEIPT_PATH",
                    recovery_receipt,
                ),
                mock.patch.object(
                    adapter.coordinator,
                    "_prepare",
                    return_value=(binding, grant, benchmark, restore),
                ) as prepare,
                mock.patch.object(
                    adapter.runtime.frozen_adapter.subprocess,
                    "run",
                    side_effect=run,
                ),
                mock.patch.object(
                    backend,
                    "write_application",
                    side_effect=AssertionError("preflight attempted a write"),
                ) as write_application,
                mock.patch.object(
                    backend,
                    "open_radio_endpoint",
                    side_effect=AssertionError("preflight opened radio"),
                ) as open_radio,
            ):
                error = None
                try:
                    adapter.preflight_only(config, backend, authority)
                except BaseException as exc:  # asserted by each caller
                    error = exc
            prepare.assert_called_once_with(config, authority, recovery=False)
            write_application.assert_not_called()
            open_radio.assert_not_called()
            self.assertFalse(journal.exists())
            self.assertFalse(execution_receipt.exists())
            self.assertFalse(recovery_receipt.exists())
        return payload, commands, error

    def _assert_two_reads_and_resets(self, payload: bytes, commands: list[list[str]]):
        reads = [command for command in commands if "read-flash" in command]
        resets = [command for command in commands if "hard-reset" in command]
        self.assertEqual(len(reads), 2)
        self.assertEqual(len(resets), 2)
        self.assertEqual(
            [command[command.index("--port") + 1] for command in reads],
            [PORT_A, PORT_B],
        )
        self.assertEqual(
            [command[command.index("--port") + 1] for command in resets],
            [PORT_A, PORT_B],
        )
        for command in reads:
            index = command.index("read-flash")
            self.assertEqual(
                command[index : index + 3],
                ["read-flash", "0x10000", str(len(payload))],
            )
            self.assertIn("--no-stub", command)
        self.assertTrue(
            all("write-flash" not in command and "erase-flash" not in command for command in commands)
        )

    def test_01_sources_and_byte_hash_contract_are_exact(self) -> None:
        self.assertTrue(adapter.sources_match())
        for path, expected in (
            (adapter.FROZEN_ADAPTER_PATH, adapter.FROZEN_ADAPTER_SHA256),
            (adapter.COORDINATOR_PATH, adapter.COORDINATOR_SHA256),
            (adapter.RUNTIME_PATH, adapter.RUNTIME_SHA256),
        ):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)
        self.assertNotIn("_sha256", adapter.coordinator.__dict__)
        self.assertIs(adapter.coordinator._sha256, adapter.coordinator.frozen._sha256)
        payload = b"exact-readback"
        self.assertEqual(
            adapter.coordinator._sha256(payload),
            hashlib.sha256(payload).hexdigest(),
        )
        with self.assertRaises(TypeError):
            adapter.coordinator._sha256(adapter.COORDINATOR_PATH)

    def test_02_composition_rebinds_both_inherited_readback_paths(self) -> None:
        self.assertIs(adapter.frozen.coordinator, adapter.coordinator)
        self.assertIs(
            adapter.runtime.frozen_adapter.coordinator,
            adapter.coordinator,
        )
        self.assertIs(
            adapter.ReconnectableEsptoolSerialBackend.verify_application,
            adapter.runtime.frozen_adapter.EsptoolSerialBackend.verify_application,
        )
        self.assertIs(
            adapter.ReconnectableEsptoolSerialBackend.write_application,
            adapter.runtime.frozen_adapter.EsptoolSerialBackend.write_application,
        )
        self.assertIs(
            adapter.ReconnectableEsptoolSerialBackend.hard_reset,
            adapter.runtime.frozen_adapter.EsptoolSerialBackend.hard_reset,
        )

    def test_03_actual_composed_preflight_reads_and_resets_both_nodes(self) -> None:
        payload, commands, error = self._run_preflight(
            b"exact-composed-application-readback"
        )
        self.assertIsNone(error)
        self._assert_two_reads_and_resets(payload, commands)

    def test_04_corrupt_and_short_readback_fail_closed_without_side_effects(self) -> None:
        exact = b"exact-composed-application-readback"
        for label, first_readback in (
            ("corrupt", exact[:-1] + bytes([exact[-1] ^ 1])),
            ("short", exact[:-1]),
        ):
            with self.subTest(label=label):
                payload, commands, error = self._run_preflight(first_readback)
                self.assertIsInstance(error, adapter.AdapterError)
                self.assertEqual(str(error), "preflight_failed")
                self._assert_two_reads_and_resets(payload, commands)

    def test_05_ot162_authority_is_fixed_and_missing_paths_fail_closed(self) -> None:
        self.assertEqual(
            adapter.FUTURE_AUTHORITY_TOOL_PATH.name,
            "ot162_noise_xk_radio_execution_authority.py",
        )
        self.assertEqual(
            adapter.FUTURE_AUTHORITY_PATH.name,
            "OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json",
        )
        authority_contract = adapter._load_authority_contract()
        self.assertEqual(
            authority_contract.AUTHORITY_RELATIVE,
            adapter.FUTURE_AUTHORITY_RELATIVE,
        )
        authority_raw = adapter.FUTURE_AUTHORITY_PATH.read_bytes()
        authority = authority_contract.decode_canonical(authority_raw, "authority")
        self.assertEqual(authority["schema"], "OT162NXRA0")
        self.assertEqual(authority["authority_id"], authority_contract.AUTHORITY_ID)
        self.assertEqual(authority["status"], "authorized_one_attempt_not_executed")
        self.assertFalse(authority["consumption"]["reusable"])
        with tempfile.TemporaryDirectory(prefix="ot160-missing-authority-") as directory:
            missing_root = Path(directory)
            with (
                mock.patch.object(
                    adapter.frozen,
                    "FUTURE_AUTHORITY_TOOL_PATH",
                    missing_root / adapter.FUTURE_AUTHORITY_TOOL_PATH.name,
                ),
                mock.patch.object(
                    adapter.frozen,
                    "FUTURE_AUTHORITY_PATH",
                    missing_root / adapter.FUTURE_AUTHORITY_PATH.name,
                ),
            ):
                with self.assertRaisesRegex(
                    adapter.AdapterError, "authority_unavailable"
                ):
                    adapter._load_authority_contract()

    def test_06_surface_has_no_authority_override_or_broad_flash_path(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("--authority", source)
        self.assertNotIn("erase_flash", source)
        self.assertNotIn("erase-flash", source)
        self.assertNotIn("write_application", source)
        self.assertNotIn("verify_application", source)
        self.assertNotIn("hard_reset", source)
        self.assertIn("ot162_noise_xk_radio_execution_authority.py", source)
        self.assertIn("ReconnectableEsptoolSerialBackend", source)
        self.assertEqual(adapter.ADAPTER_PATH, MODULE_PATH)


if __name__ == "__main__":
    unittest.main(verbosity=2)
