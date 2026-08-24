#!/usr/bin/env python3
"""Host-only integrity tests for the OT-126 corrective-retry abort."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
RECEIPT = (
    CRYPTO
    / "OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json"
)
RUNNER = ROOT / "tools" / "ot125_monocypher_retry_runner.py"
AUTHORITY = (
    CRYPTO / "OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"
)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Ot126MonocypherRetryAbortTests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = json.loads(RECEIPT.read_text(encoding="ascii"))

    def test_01_exact_receipt_and_consumed_lineage(self) -> None:
        self.assertEqual(
            sha(RECEIPT),
            "247b0b80e64a3f6bf6654be279e90dcbd80a067c52ef861313a6f370c0355941",
        )
        self.assertEqual(
            sha(RUNNER),
            "47022c46ce6d911998b5457516e250e3dec7dcc8dbe1e0c8e799a0cacfc23150",
        )
        self.assertEqual(
            sha(AUTHORITY),
            "b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf",
        )
        self.assertEqual(self.value["runner_sha256"], sha(RUNNER))
        self.assertEqual(self.value["authority_raw_sha256"], sha(AUTHORITY))
        self.assertTrue(self.value["authority"]["consumed_by_abort"])
        self.assertFalse(self.value["authority"]["continuing_authority"])
        self.assertFalse(self.value["authority"]["reusable"])

    def test_02_deterministic_first_frame_race_is_bounded_exactly(self) -> None:
        abort = self.value["abort"]
        self.assertEqual(abort["stage"], "node_a_fresh_serial_capture")
        self.assertEqual(
            abort["reason"],
            "pre_frame_empty_read_cycle_ended_before_firmware_first_frame",
        )
        self.assertTrue(abort["root_cause_confirmed"])
        self.assertEqual(abort["serial_read_timeout_ms"], 250)
        self.assertEqual(abort["empty_read_limit"], 8)
        self.assertEqual(abort["maximum_empty_read_window_ms"], 2000)
        self.assertEqual(abort["firmware_task_delay_before_first_frame_ms"], 3000)
        self.assertLess(
            abort["maximum_empty_read_window_ms"],
            abort["firmware_task_delay_before_first_frame_ms"],
        )
        self.assertFalse(abort["benchmark_result_admitted"])

    def test_03_restoration_runtime_and_privacy_fail_closed(self) -> None:
        self.assertTrue(self.value["all_touched_nodes_restored"])
        self.assertTrue(self.value["all_devices_trail_application_verified"])
        self.assertTrue(self.value["all_devices_runtime_reset_complete"])
        self.assertTrue(self.value["two_usb_endpoints_returned"])
        self.assertTrue(self.value["owner_observed_both_trail_displays_on"])
        self.assertEqual(self.value["touched_node_count"], 1)
        self.assertTrue(self.value["nodes"][0]["restore_readback_verified"])
        self.assertTrue(self.value["nodes"][0]["restore_reset_completed"])
        self.assertFalse(self.value["nodes"][1]["benchmark_write_attempted"])
        self.assertTrue(self.value["nodes"][1]["restore_reset_completed"])
        self.assertTrue(self.value["privacy"]["anonymous_role_labels_only"])
        self.assertTrue(
            all(
                item is False
                for key, item in self.value["privacy"].items()
                if key != "anonymous_role_labels_only"
            )
        )
        self.assertTrue(all(item is False for item in self.value["claims"].values()))

    def test_04_public_decision_and_hardware_note_preserve_claim_boundary(self) -> None:
        decision = (
            ROOT / "docs" / "decisions"
            / "0065-monocypher-corrective-retry-execution-abort.md"
        ).read_text(encoding="utf-8")
        hardware = (
            ROOT / "tests" / "hardware" / "OT-126-2026-08-24.md"
        ).read_text(encoding="utf-8")
        for text in (decision, hardware):
            self.assertIn("no Monocypher benchmark result admitted", text)
            self.assertIn("both", text)
            self.assertIn("Trail", text)
        self.assertIn("3,000 ms", decision)
        self.assertIn("2,000 ms", decision)
        self.assertIn("fresh one-attempt authority", decision)


if __name__ == "__main__":
    unittest.main(verbosity=2)
