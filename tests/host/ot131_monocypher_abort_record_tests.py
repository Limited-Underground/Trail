#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-131-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json"
)
EVIDENCE = ROOT / "tests" / "hardware" / "OT-131-2026-08-24.md"
DECISION = ROOT / "docs" / "decisions" / (
    "0070-record-ot131-monocypher-execution-abort.md"
)


class Ot131AbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_closed_failure_and_bounded_diagnostics_are_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "monocypher_execution_aborted")
        self.assertEqual(
            self.receipt["failure"],
            {
                "code": "capture_failed",
                "capture_code": "preamble_invalid",
                "capture_diagnostics": {
                    "lifecycle": "stable_continuous",
                    "reset_attempts": 1,
                    "open_attempts": 1,
                    "start_write_attempts": 1,
                    "bytes_observed": 512,
                    "complete_lines": 1,
                    "frame_lines_buffered": 0,
                },
            },
        )

    def test_02_only_node_a_was_touched_and_it_restored(self) -> None:
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 1)
        node_a, node_b = self.receipt["nodes"]
        self.assertEqual(node_a["node"], "A")
        self.assertTrue(node_a["benchmark_readback_verified"])
        self.assertFalse(node_a["capture_validated"])
        self.assertTrue(node_a["restore_readback_verified"])
        self.assertTrue(node_a["restore_reset_completed"])
        self.assertEqual(
            node_b,
            {
                "node": "B",
                "benchmark_write_attempted": False,
                "trail_application_remained_installed": True,
            },
        )
        self.assertTrue(self.receipt["restoration_complete"])
        self.assertTrue(self.receipt["owner_observed_both_trail_logos_after_abort"])

    def test_03_authority_is_consumed_and_every_claim_remains_false(self) -> None:
        self.assertEqual(
            self.receipt["authority"],
            {"consumed_by_abort": True, "continuing_authority": False, "reusable": False},
        )
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_04_immutable_public_projection_bindings_are_exact(self) -> None:
        self.assertEqual(
            self.receipt["bindings"],
            {
                "adapter": {
                    "name": "ot131_monocypher_hardware_adapter.py",
                    "sha256": "fc2012d5aaa13e53c70ff9c68059231e2c4945840435d4add49a22a7081e1b96",
                },
                "authority": {
                    "name": "OT-131-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json",
                    "raw_sha256": "c134107cf98c75045e033cb087b58dce9e693e77542e1e70406a8896a2532eb9",
                },
                "benchmark": {
                    "name": "ot129_monocypher_protocol_bench.bin",
                    "bytes": 187680,
                    "sha256": "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268",
                },
                "restore": {
                    "name": "opentrail_heltec_v4_bench.bin",
                    "bytes": 473152,
                    "sha256": "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741",
                },
                "application_offset": 65536,
                "baud": 115200,
                "private_execution_receipt_sha256": "6284616667fd5665bcb5a88a028ad941443a033883f24330662fff570b66cff6",
                "private_journal_sha256": "937ab3a89c5872abeb1dc6190a31c8ca2b8083b16951e79cebbf405736072aee",
            },
        )

    def test_05_privacy_flags_are_false_and_no_private_value_shape_is_present(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))

    def test_06_public_records_preserve_the_no_result_boundary(self) -> None:
        combined = EVIDENCE.read_text(encoding="utf-8") + DECISION.read_text(encoding="utf-8")
        for required in (
            "capture_failed",
            "preamble_invalid",
            "restoration_complete=true",
            "No further Monocypher hardware attempt is authorized",
            "V1 remains exact 43.75%, displayed 44%",
        ):
            self.assertIn(required, combined)
        self.assertNotIn("raw capture", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
