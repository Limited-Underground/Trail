#!/usr/bin/env python3
"""Focused adversarial tests for the OT-148 Phase 2 reconciliation."""

from __future__ import annotations

import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/crypto_phase_two_reconciliation.py"
RECORD = ROOT / "tests/benchmarks/crypto/OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json"

SPEC = importlib.util.spec_from_file_location("crypto_phase_two_reconciliation", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class Tests(unittest.TestCase):
    def record(self) -> dict:
        return json.loads(RECORD.read_bytes())

    def assert_rejected(self, mutation) -> None:
        value = copy.deepcopy(self.record())
        mutation(value)
        with self.assertRaises(MODULE.ReconciliationError):
            MODULE.validate(value)

    def test_canonical_record(self) -> None:
        result = MODULE.validate(self.record())
        self.assertEqual(result["schema"], "OTP2CR0")
        self.assertEqual(result["unresolved_evidence_count"], 5)
        self.assertEqual(result["unreconciled_gate_count"], 8)
        self.assertFalse(result["phase_two_complete"])
        self.assertFalse(result["candidate_selected"])

    def test_cli(self) -> None:
        completed = subprocess.run([sys.executable, str(TOOL), "--record", str(RECORD)], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["recommended_candidate"], "espressif_libsodium")

    def test_phase_two_completion_overclaim_rejected(self) -> None:
        self.assert_rejected(lambda value: value["claims"].__setitem__("phase_two_complete", True))

    def test_phase_three_completion_overclaim_rejected(self) -> None:
        self.assert_rejected(lambda value: value["claims"].__setitem__("phase_three_admission_complete", True))

    def test_selection_overclaim_rejected(self) -> None:
        self.assert_rejected(lambda value: value["candidate_corpus"][0].__setitem__("selected", True))

    def test_missing_mbedtls_measurement_blocker_rejected(self) -> None:
        self.assert_rejected(lambda value: value["unresolved_required_evidence"].pop(0))

    def test_missing_radio_blocker_rejected(self) -> None:
        self.assert_rejected(lambda value: value["unresolved_required_evidence"].pop(2))

    def test_missing_named_gate_rejected(self) -> None:
        self.assert_rejected(lambda value: value["unreconciled_required_gates"].pop())

    def test_parent_digest_drift_rejected(self) -> None:
        self.assert_rejected(lambda value: value["bindings"]["benchmark_plan"].__setitem__("raw_sha256", "0" * 64))

    def test_partial_candidate_cannot_become_selectable(self) -> None:
        self.assert_rejected(lambda value: value["candidate_corpus"][2].__setitem__("selection_eligible", True))

    def test_completion_sequence_cannot_skip_phase_three(self) -> None:
        self.assert_rejected(lambda value: value["shortest_valid_sequence"].pop(4))

    def test_no_hardware_authority(self) -> None:
        self.assert_rejected(lambda value: value["authority"].__setitem__("device_access_authorized", True))

    def test_private_trace_custody_remains_explicit(self) -> None:
        self.assertIn("private_raw_trace_custody_verification", self.record()["unresolved_required_evidence"])

    def test_mutated_file_fails_cli(self) -> None:
        value = self.record()
        value["claims"]["suite_selected"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            completed = subprocess.run([sys.executable, str(TOOL), "--record", str(path)], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["schema"], "OTP2CR0")


if __name__ == "__main__":
    unittest.main()
