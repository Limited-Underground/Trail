#!/usr/bin/env python3
"""Focused adversarial tests for the OT-152 Noise XK radio preparation."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/crypto_noise_xk_radio_cost_preparation.py"
RECORD = ROOT / "tests/benchmarks/crypto/OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0.json"

SPEC = importlib.util.spec_from_file_location("crypto_noise_xk_radio_cost_preparation", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class Tests(unittest.TestCase):
    def record(self) -> dict:
        return json.loads(RECORD.read_bytes())

    def assert_rejected(self, mutation) -> None:
        value = copy.deepcopy(self.record())
        mutation(value)
        with self.assertRaises(MODULE.PreparationError):
            MODULE.validate(value)

    def test_canonical_record(self) -> None:
        result = MODULE.validate(self.record())
        self.assertEqual(result["schema"], "OTNXRP0")
        self.assertEqual(result["handshake_total_wire_bytes"], 160)
        self.assertEqual(result["fragments"], 3)
        self.assertEqual(result["bounded_retry_total_wire_bytes"], 208)
        self.assertEqual(result["bounded_retry_fragments"], 4)
        self.assertEqual(result["role_cycles"], 2)
        self.assertFalse(result["execution_authorized"])
        self.assertFalse(result["radio_measurement_admitted"])

    def test_record_identities_are_pinned(self) -> None:
        self.assertEqual(hashlib.sha256(RECORD.read_bytes()).hexdigest(), MODULE.EXPECTED_RECORD_RAW_SHA256)
        self.assertEqual(MODULE.canonical_sha256(self.record()), MODULE.EXPECTED_RECORD_CANONICAL_SHA256)

    def test_cli(self) -> None:
        completed = subprocess.run([sys.executable, str(TOOL)], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["successful_handshake_theoretical_airtime_us"], 313088)
        self.assertEqual(result["bounded_retry_theoretical_airtime_us"], 410624)

    def test_airtime_reproduces_ot114_and_noise_lengths(self) -> None:
        profile = self.record()["radio_profile"]
        self.assertEqual(MODULE.lora_airtime_us(48, profile), 97536)
        self.assertEqual(MODULE.lora_airtime_us(64, profile), 118016)
        self.assertEqual(MODULE.lora_airtime_us(163, profile), 266496)
        self.assertEqual(MODULE.lora_airtime_us(255, profile), 399616)

    def test_message_size_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["handshake_surface"]["messages"][2].__setitem__("wire_bytes", 65))

    def test_fragment_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["handshake_surface"].__setitem__("fragments", 1))

    def test_packet_v1_selection_rejected(self) -> None:
        self.assert_rejected(lambda value: value["handshake_surface"].__setitem__("packet_v1_framing_selected", True))

    def test_airtime_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["airtime_model"]["payload_airtime_us_by_wire_bytes"].__setitem__("48", 97535))

    def test_timeout_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["airtime_model"].__setitem__("message_2_response_timeout_ms", 2195))

    def test_role_reversal_removed_rejected(self) -> None:
        self.assert_rejected(lambda value: value["role_reversed_execution"].pop())

    def test_retry_expansion_rejected(self) -> None:
        self.assert_rejected(lambda value: value["bounded_retry_scenario_per_role_cycle"].__setitem__("maximum_retries", 2))

    def test_partial_message_retry_rejected(self) -> None:
        self.assert_rejected(lambda value: value["bounded_retry_scenario_per_role_cycle"].__setitem__("retry_scope", "retry_message_2_only"))

    def test_stale_attempt_acceptance_rejected(self) -> None:
        self.assert_rejected(lambda value: value["bounded_retry_scenario_per_role_cycle"].__setitem__("stale_first_attempt_frames_must_be_rejected", False))

    def test_parent_digest_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["bindings"]["direct_radio_evidence"].__setitem__("raw_sha256", "0" * 64))

    def test_hardware_authority_rejected(self) -> None:
        self.assert_rejected(lambda value: value["authority"].__setitem__("radio_transmit_authorized", True))

    def test_phase_two_completion_rejected(self) -> None:
        self.assert_rejected(lambda value: value["claims"].__setitem__("phase_two_complete", True))

    def test_selection_rejected(self) -> None:
        self.assert_rejected(lambda value: value["claims"].__setitem__("candidate_selected", True))

    def test_private_path_rejected(self) -> None:
        self.assert_rejected(lambda value: value.__setitem__("public_result", "C:\\private\\capture.bin"))

    def test_nonfinite_number_rejected(self) -> None:
        self.assert_rejected(lambda value: value.__setitem__("unsafe_number", float("nan")))

    def test_mutated_file_fails_cli(self) -> None:
        value = self.record()
        value["complete_execution_totals"]["fragments"] = 13
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            completed = subprocess.run([sys.executable, str(TOOL), "--record", str(path)], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["schema"], "OTNXRP0")


if __name__ == "__main__":
    unittest.main()
