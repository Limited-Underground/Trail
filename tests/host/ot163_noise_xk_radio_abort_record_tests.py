#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-163-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTION-ABORT-RECEIPT-V0.json"
)
EVIDENCE = ROOT / "tests" / "hardware" / "OT-163-2026-08-28.md"
DECISION = ROOT / "docs" / "decisions" / (
    "0099-record-ot163-noise-xk-radio-execution-abort.md"
)


class Ot163NoiseXkRadioAbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(self.receipt["schema"], "OT163NXAR0")
        self.assertEqual(self.receipt["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "e9927cd3f3b266cd3cec6cbfeac2d0e400c036efa07b791eff857b2217988ca8",
        )

    def test_02_closed_failure_and_allowlisted_stage_are_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "noise_xk_radio_execution_aborted")
        self.assertEqual(
            self.receipt["failure"],
            {
                "code": "radio_run_failed",
                "stage": "restart_ack_a",
                "radio_run_invoked": True,
                "radio_result_validated": False,
            },
        )

    def test_03_both_nodes_were_touched_and_restored(self) -> None:
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 2)
        self.assertEqual([node["node"] for node in self.receipt["nodes"]], ["A", "B"])
        for node in self.receipt["nodes"]:
            for key in (
                "installed_app_readback_verified",
                "preflight_reset_completed",
                "benchmark_readback_verified",
                "benchmark_reset_completed",
                "restore_readback_verified",
                "restore_reset_completed",
            ):
                self.assertTrue(node[key])
        self.assertTrue(self.receipt["restoration_complete"])
        self.assertFalse(self.receipt["recovery_required"])
        self.assertTrue(self.receipt["owner_observed_both_trail_logos_after_abort"])

    def test_04_authority_consumed_and_every_claim_remains_false(self) -> None:
        self.assertEqual(
            self.receipt["authority"],
            {"consumed_by_abort": True, "continuing_authority": False, "reusable": False},
        )
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_05_immutable_bindings_and_private_source_hashes_are_exact(self) -> None:
        bindings = self.receipt["bindings"]
        self.assertEqual(
            bindings["coordinator"]["sha256"],
            "444528fd341b3d55f3a5b3224b217620e1b37e3c7960d224aefbe01d9953a02d",
        )
        self.assertEqual(
            bindings["runner"]["sha256"],
            "81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244",
        )
        self.assertEqual(
            bindings["serial_runtime"]["sha256"],
            "bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8",
        )
        self.assertEqual(
            bindings["adapter"]["sha256"],
            "24d75806cdf7ae28c47fe427cac12a7ef3564d76d68a926ad58bd610c9e8f4b9",
        )
        self.assertEqual(
            bindings["authority"]["raw_sha256"],
            "58964547c9f38ff2688da14f31421216eb2bc2705916abeee75e202ffa876a58",
        )
        self.assertEqual(
            bindings["preparation"]["raw_sha256"],
            "942f7bda82273e8d06901827934eac6dc2c30ac3135ba614c2067eecb8cb171c",
        )
        self.assertEqual(bindings["benchmark"]["bytes"], 296640)
        self.assertEqual(bindings["restore"]["bytes"], 500944)
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)
        self.assertEqual(
            bindings["private_execution_receipt_sha256"],
            "2d4a87df33cd41d798a5be96d2c8025cfe8ea3742408709610a4e65c7702825b",
        )
        self.assertEqual(
            bindings["private_journal_sha256"],
            "69bcf71a3da4e4e39376b5c8f46b2b4227a628dc33a8b4f14dc041b2018436d7",
        )

    def test_06_public_projection_is_privacy_safe(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))

    def test_07_public_records_preserve_consumed_abort_boundary(self) -> None:
        combined = EVIDENCE.read_text(encoding="utf-8") + DECISION.read_text(encoding="utf-8")
        for required in (
            "restart_ack_a",
            "restoration is complete",
            "authority as consumed",
            "cannot authorize a retry",
            "does not require a public website status update",
        ):
            self.assertIn(required, combined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
