#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-151-OT005-MBEDTLS-PSA-EXECUTION-ABORT-RECEIPT-V0.json"
)
EVIDENCE = ROOT / "tests" / "hardware" / "OT-151-2026-08-27.md"
DECISION = ROOT / "docs" / "decisions" / (
    "0087-record-ot150-abort-and-correct-mbedtls-psa-successor.md"
)


class Ot151AbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(self.receipt["schema"], "OT151MEAR0")
        self.assertEqual(self.receipt["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "b71e2881883789965c7680c7cc4d854a66725900445f790791a10140ffe926fc",
        )

    def test_02_closed_failure_and_bounded_diagnostics_are_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "mbedtls_psa_execution_aborted")
        self.assertEqual(
            self.receipt["failure"],
            {
                "code": "capture_failed",
                "capture_code": "frame_count_incomplete",
                "capture_diagnostics": {
                    "lifecycle": "stable_continuous",
                    "reset_attempts": 1,
                    "lifecycle_polls": 4,
                    "stable_presence_polls": 3,
                    "open_attempts": 1,
                    "start_write_attempts": 1,
                    "read_calls": 589,
                    "empty_reads": 586,
                    "bytes_observed": 1468,
                    "preamble_lines_ignored": 0,
                    "complete_lines": 6,
                    "frame_lines_buffered": 5,
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
        self.assertFalse(self.receipt["recovery_required"])
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
            "1602d9bf1a306a1b88c701bebd8cff920adf75d80533f09baba5de7b72144b97",
        )
        self.assertEqual(
            bindings["runner"]["sha256"],
            "82c2d8d39220e41d8f0e69bbbebfbcd30929ee0a01a692b996319a9bfdbcba92",
        )
        self.assertEqual(
            bindings["adapter"]["sha256"],
            "8f953e1a9b633a9ac3c443ab47654545cafa1a395e835673661162668228313e",
        )
        self.assertEqual(bindings["benchmark"]["bytes"], 245584)
        self.assertEqual(bindings["restore"]["bytes"], 500944)
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)

    def test_06_privacy_flags_are_false_and_no_private_value_shape_is_present(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))

    def test_07_public_records_preserve_abort_and_successor_boundaries(self) -> None:
        combined = EVIDENCE.read_text(encoding="utf-8") + DECISION.read_text(encoding="utf-8")
        for required in (
            "capture_failed",
            "frame_count_incomplete",
            "bytes_observed=1468",
            "frame_lines_buffered=5",
            "restoration_complete=true",
            "output_length",
            "No further mbedTLS/PSA hardware attempt is authorized",
            "V1 remains exact 43.75%, displayed 44%",
        ):
            self.assertIn(required, combined)
        self.assertNotIn("raw capture", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
