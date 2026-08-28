#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-155-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTION-ABORT-RECEIPT-V0.json"
)


class Ot155NoiseXkRadioAbortRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = RECEIPT.read_text(encoding="ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertEqual(self.receipt["schema"], "OT155NXAR0")
        self.assertEqual(self.receipt["version"], 0)
        self.assertEqual(
            hashlib.sha256(self.raw.encode("ascii")).hexdigest(),
            "ae18a4bcc6f43b227ea7169dd3b77828d16440b7ca8216e64d0f4d39bf57d8de",
        )

    def test_02_closed_failure_is_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "noise_xk_radio_execution_aborted")
        self.assertEqual(
            self.receipt["failure"],
            {
                "code": "radio_run_failed",
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
        self.assertFalse(self.receipt["owner_observed_both_trail_logos_after_abort"])

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
            "6635c73c6952b322ec1d72043a80f637b2cc04b70c1763f578ea4f38559aeaf3",
        )
        self.assertEqual(
            bindings["runner"]["sha256"],
            "8b20512bf25f06247bb59defa092b8db82fde753484a3936d9e4aee2fba808be",
        )
        self.assertEqual(
            bindings["adapter"]["sha256"],
            "d84aa9a1c0556f6421141a25336a3e87dab054cc971722b3f8058fe0a254f94b",
        )
        self.assertEqual(bindings["benchmark"]["bytes"], 296640)
        self.assertEqual(bindings["restore"]["bytes"], 500944)
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)
        self.assertEqual(
            bindings["private_execution_receipt_sha256"],
            "be5c9a4b9b92979d37cb168a91ea46100b301d2b8ee3afb3f3dcaa507e6c56af",
        )
        self.assertEqual(
            bindings["private_journal_sha256"],
            "8e71fc9bc3b73e8825281f4e58b7da23daba9cc020dc737bf90986b38ddad757",
        )

    def test_06_public_projection_is_privacy_safe(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(re.search(r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-", self.raw))
        self.assertNotIn("exception", self.raw.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
