#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-137-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json"
)
EVIDENCE = ROOT / "tests" / "hardware" / "OT-137-2026-08-25.md"
DECISION = ROOT / "docs" / "decisions" / (
    "0076-record-ot137-ot136-execution-abort.md"
)


class Ot137AbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(self.receipt["schema"], "OT137MEAR0")
        self.assertEqual(self.receipt["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "1f6a75e2941045eb3585161769bb2a3ae544b1192d04594dc9d9bec53d77212c",
        )

    def test_02_closed_failure_and_bounded_diagnostics_are_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "monocypher_execution_aborted")
        self.assertEqual(
            self.receipt["failure"],
            {
                "code": "capture_failed",
                "capture_code": "preamble_invalid",
                "capture_diagnostics": {
                    "lifecycle": "stable_continuous",
                    "reset_attempts": 1,
                    "lifecycle_polls": 4,
                    "stable_presence_polls": 3,
                    "open_attempts": 1,
                    "start_write_attempts": 1,
                    "read_calls": 2,
                    "empty_reads": 0,
                    "bytes_observed": 1024,
                    "preamble_lines_ignored": 11,
                    "complete_lines": 11,
                    "frame_lines_buffered": 0,
                },
            },
        )

    def test_03_only_node_a_was_touched_and_it_restored(self) -> None:
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

    def test_04_authority_consumed_and_every_claim_remains_false(self) -> None:
        self.assertEqual(
            self.receipt["authority"],
            {"consumed_by_abort": True, "continuing_authority": False, "reusable": False},
        )
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_05_immutable_public_projection_bindings_are_exact(self) -> None:
        bindings = self.receipt["bindings"]
        self.assertEqual(
            bindings["coordinator"]["sha256"],
            "3d340194f98b9d99d7833510d19b142e660ee2999d2d4e20000ca13d7f380867",
        )
        self.assertEqual(
            bindings["runner"]["sha256"],
            "e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993",
        )
        self.assertEqual(
            bindings["adapter"]["sha256"],
            "73a3cfd606ae2249c1920b27fa94daccecde20154f8b484de03fdd003c13aba8",
        )
        self.assertEqual(bindings["benchmark"]["bytes"], 187680)
        self.assertEqual(bindings["restore"]["bytes"], 473152)
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)

    def test_06_privacy_flags_are_false_and_no_private_value_shape_is_present(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))

    def test_07_public_records_preserve_the_no_result_boundary(self) -> None:
        combined = EVIDENCE.read_text(encoding="utf-8") + DECISION.read_text(encoding="utf-8")
        for required in (
            "capture_failed",
            "preamble_invalid",
            "preamble_lines_ignored=11",
            "bytes_observed=1024",
            "restoration_complete=true",
            "No further Monocypher hardware attempt is authorized",
            "V1 remains exact 43.75%, displayed 44%",
        ):
            self.assertIn(required, combined)
        self.assertNotIn("raw capture", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
