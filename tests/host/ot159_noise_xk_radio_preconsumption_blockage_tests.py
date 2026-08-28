#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-159-OT005-LIBSODIUM-NOISE-XK-RADIO-PRECONSUMPTION-BLOCKAGE-V0.json"
)


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class Ot159NoiseXkRadioPreconsumptionBlockageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECORD.read_text(encoding="ascii")
        cls.record = json.loads(cls.raw)

    def test_01_record_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.record, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(self.record["schema"], "OT159NXB0")
        self.assertEqual(self.record["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "059de31dc9dbeaa9577d816ceef73a1e0f5e039494854e730e9dfd5ceaeb3469",
        )

    def test_02_failure_is_preconsumption_and_deterministically_classified(self) -> None:
        self.assertEqual(
            self.record["result"],
            "noise_xk_radio_execution_blocked_before_consumption",
        )
        self.assertEqual(
            self.record["failure"],
            {
                "code": "preflight_failed",
                "diagnosis": (
                    "ot157_coordinator_file_hash_helper_shadows_inherited_byte_hash_helper"
                ),
                "official_bound_preflight_attempts": 2,
            },
        )

    def test_03_both_anonymous_nodes_remained_exactly_on_trail(self) -> None:
        self.assertEqual(self.record["node_count"], 2)
        self.assertEqual([node["node"] for node in self.record["nodes"]], ["A", "B"])
        for node in self.record["nodes"]:
            self.assertTrue(node["direct_trail_readback_verified"])
            self.assertTrue(node["direct_reset_completed"])

    def test_04_no_consumption_write_radio_receipt_or_recovery_occurred(self) -> None:
        boundary = self.record["preconsumption"]
        for key in (
            "matching_anonymous_nodes_present",
            "antennas_connected",
            "authority_validated",
            "benchmark_artifact_validated",
            "restore_artifact_validated",
            "journal_absent_before",
            "journal_absent_after",
            "execution_receipt_absent_before",
            "execution_receipt_absent_after",
            "recovery_receipt_absent_before",
            "recovery_receipt_absent_after",
        ):
            self.assertTrue(boundary[key])
        for key in (
            "benchmark_write_started",
            "radio_run_invoked",
            "recovery_required",
        ):
            self.assertFalse(boundary[key])
        self.assertEqual(
            self.record["authority"],
            {
                "used": False,
                "consumed": False,
                "executable_as_accepted": False,
                "continuing_authority": False,
                "reusable": False,
            },
        )
        self.assertTrue(all(value is False for value in self.record["claims"].values()))

    def test_05_immutable_bindings_are_exact_and_still_match_files(self) -> None:
        bindings = self.record["bindings"]
        paths = {
            "coordinator": ROOT / "tools" / "ot157_noise_xk_radio_coordinator.py",
            "runner": ROOT / "tools" / "ot156_noise_xk_radio_runner.py",
            "serial_runtime": ROOT / "tools" / "ot156_noise_xk_radio_runtime.py",
            "adapter": ROOT / "tools" / "ot157_noise_xk_radio_hardware_adapter.py",
            "authority_tool": ROOT / "tools" / "ot158_noise_xk_radio_execution_authority.py",
        }
        for key, path in paths.items():
            self.assertEqual(path.name, bindings[key]["name"])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), bindings[key]["sha256"])
        self.assertEqual(bindings["benchmark"]["bytes"], 296640)
        self.assertEqual(bindings["restore"]["bytes"], 500944)
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)

    def test_06_runtime_composition_proves_the_hash_helper_shadow(self) -> None:
        coordinator = (ROOT / "tools" / "ot157_noise_xk_radio_coordinator.py").read_text(
            encoding="utf-8"
        )
        adapter = (ROOT / "tools" / "ot157_noise_xk_radio_hardware_adapter.py").read_text(
            encoding="utf-8"
        )
        inherited = (ROOT / "tools" / "ot153_noise_xk_radio_hardware_adapter.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("def _sha256(path: Path) -> str:", coordinator)
        self.assertIn("hashlib.sha256(path.read_bytes()).hexdigest()", coordinator)
        self.assertIn("frozen.coordinator = coordinator", adapter)
        self.assertIn("runtime.frozen_adapter.coordinator = coordinator", adapter)
        self.assertIn("coordinator._sha256(readback) != expected_sha256", inherited)

        runtime_adapter = load(
            "ot159_runtime_adapter_probe",
            ROOT / "tools" / "ot157_noise_xk_radio_hardware_adapter.py",
        )
        payload = b"exact-readback"
        self.assertIs(runtime_adapter.frozen.coordinator, runtime_adapter.coordinator)
        self.assertIs(
            runtime_adapter.runtime.frozen_adapter.coordinator,
            runtime_adapter.coordinator,
        )
        self.assertIs(
            runtime_adapter.ReconnectableEsptoolSerialBackend.verify_application,
            runtime_adapter.runtime.frozen_adapter.EsptoolSerialBackend.verify_application,
        )
        with self.assertRaisesRegex(AttributeError, "read_bytes"):
            runtime_adapter.coordinator._sha256(payload)
        self.assertEqual(
            runtime_adapter.coordinator.frozen._sha256(payload),
            hashlib.sha256(payload).hexdigest(),
        )

    def test_07_public_projection_is_privacy_safe(self) -> None:
        privacy = self.record["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(
                value is False
                for key, value in privacy.items()
                if key != "anonymous_role_labels_only"
            )
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))
        self.assertNotIn("exception", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
