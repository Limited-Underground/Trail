#!/usr/bin/env python3
"""Host-only integrity tests for the OT-128 second corrective-retry abort."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
RECEIPT = CRYPTO / "OT-128-OT005-MONOCYPHER-SECOND-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json"
RUNNER = ROOT / "tools" / "ot127_monocypher_retry_runner.py"
AUTHORITY = CRYPTO / "OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Ot128MonocypherRetryAbortTests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = json.loads(RECEIPT.read_text(encoding="ascii"))

    def test_01_exact_receipt_and_consumed_lineage(self) -> None:
        self.assertEqual(sha(RECEIPT), "b34b4f761d77a5d952e50eda4c48d7629b5185043cc84c146dc9b974ba36f09e")
        self.assertEqual(sha(RUNNER), "ff81188b1f211aaf504192d3827147b2f572b29e895e45eeeee1bc505ffb5438")
        self.assertEqual(sha(AUTHORITY), "d043fc7dc700ce2c43914fe079b4a594b04e730252d8cb490f02097d9472448b")
        self.assertEqual(self.value["runner_sha256"], sha(RUNNER))
        self.assertEqual(self.value["authority_raw_sha256"], sha(AUTHORITY))
        self.assertTrue(self.value["authority"]["consumed_by_abort"])
        self.assertFalse(self.value["authority"]["continuing_authority"])
        self.assertFalse(self.value["authority"]["reusable"])

    def test_02_capture_failure_remains_honestly_unclassified(self) -> None:
        abort = self.value["abort"]
        self.assertEqual(abort["stage"], "node_a_serial_capture")
        self.assertEqual(abort["reason"], "capture_not_validated")
        self.assertFalse(abort["root_cause_confirmed"])
        self.assertFalse(abort["failure_classification_recorded"])
        self.assertFalse(abort["benchmark_result_admitted"])

    def test_03_restoration_runtime_and_privacy_fail_closed(self) -> None:
        self.assertEqual(self.value["touched_node_count"], 1)
        self.assertTrue(self.value["all_touched_nodes_restored"])
        self.assertTrue(self.value["all_devices_trail_application_verified"])
        self.assertTrue(self.value["post_receipt_dual_hard_reset_completed"])
        self.assertTrue(self.value["two_usb_endpoints_returned"])
        self.assertFalse(self.value["owner_observed_both_trail_displays_on_after_abort"])
        self.assertTrue(self.value["nodes"][0]["restore_readback_verified"])
        self.assertTrue(self.value["nodes"][0]["restore_reset_completed"])
        self.assertFalse(self.value["nodes"][1]["benchmark_write_attempted"])
        self.assertFalse(self.value["nodes"][1]["restore_required"])
        self.assertTrue(all(node["post_receipt_reset_completed"] for node in self.value["nodes"]))
        self.assertTrue(self.value["privacy"]["anonymous_role_labels_only"])
        self.assertTrue(all(item is False for key, item in self.value["privacy"].items() if key != "anonymous_role_labels_only"))
        self.assertTrue(all(item is False for item in self.value["claims"].values()))

    def test_04_public_records_preserve_claim_and_next_attempt_boundary(self) -> None:
        decision = (ROOT / "docs" / "decisions" / "0067-record-monocypher-second-corrective-retry-abort.md").read_text(encoding="utf-8")
        hardware = (ROOT / "tests" / "hardware" / "OT-128-2026-08-24.md").read_text(encoding="utf-8")
        for text in (decision, hardware):
            self.assertIn("no Monocypher benchmark result admitted", text)
            self.assertIn("root cause", text)
            self.assertIn("not confirmed", text)
            self.assertIn("fresh", text)
            self.assertIn("authority", text)
        self.assertIn("No further Monocypher hardware attempt is authorized", decision)
        self.assertIn("no post-abort display claim", hardware)
        self.assertIn("Later owner observation", hardware)
        self.assertIn("owner visually confirmed both Trail displays on", hardware)
        self.assertIn("original false observation flag", hardware)


if __name__ == "__main__":
    unittest.main(verbosity=2)
