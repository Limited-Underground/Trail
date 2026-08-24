#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json"
)
EVIDENCE = ROOT / "tests" / "hardware" / "OT-133-2026-08-24.md"
DECISION = ROOT / "docs" / "decisions" / (
    "0072-record-ot133-immutable-successor-execution-abort.md"
)


class Ot133AbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(cls := self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(cls["schema"], "OT133MEAR0")
        self.assertEqual(cls["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "5a57675d1d367968fa1af20c97ab0a7ca4eb005a5b3f1fcf77c52fc552af6d04",
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
                    "read_calls": 1,
                    "empty_reads": 0,
                    "bytes_observed": 512,
                    "preamble_lines_ignored": 9,
                    "complete_lines": 9,
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
            "04d050d540f49203f2d041cb87dfa4f0d8f741aa268d43b75866ce67a4458cff",
        )
        self.assertEqual(
            bindings["runner"]["sha256"],
            "d131286969d82f0ddef8b3051b6d64588042a4d49fafa2aca1d164de617f6a3d",
        )
        self.assertEqual(
            bindings["adapter"]["sha256"],
            "1dd32656743a4ec3486cab110158d07824b8f2c0cbda2b4e2a8dd8420187086a",
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
            "preamble_lines_ignored=9",
            "restoration_complete=true",
            "No further Monocypher hardware attempt is authorized",
            "V1 remains exact 43.75%, displayed 44%",
        ):
            self.assertIn(required, combined)
        self.assertNotIn("raw capture", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
