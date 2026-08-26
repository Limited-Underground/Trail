#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "crypto" / (
    "OT-146-OT005-MONOCYPHER-EXECUTION-RECEIPT-V0.json"
)
RECEIPT_SHA256 = "9a5ea09fa8cdf465f5c83b6a0eb69fb80579806b0635ac63083e1234e4f91464"


class Ot146MonocypherSuccessRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw_bytes = RECEIPT.read_bytes()
        cls.raw = cls.raw_bytes.decode("ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_ascii_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertNotIn("\r", self.raw)
        self.assertEqual(self.raw.count("\n"), 1)
        self.assertEqual(hashlib.sha256(self.raw_bytes).hexdigest(), RECEIPT_SHA256)
        self.assertEqual(self.receipt["schema"], "OT146MESR0")
        self.assertEqual(self.receipt["version"], 0)

    def test_02_terminal_result_and_consumption_are_exact(self) -> None:
        self.assertEqual(self.receipt["result"], "two_node_monocypher_passed_and_restored")
        self.assertIsNone(self.receipt["failure"])
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 2)
        self.assertEqual(
            self.receipt["authority"],
            {"consumed_by_success": True, "continuing_authority": False, "reusable": False},
        )

    def test_03_both_anonymous_node_results_are_exact(self) -> None:
        nodes = self.receipt["nodes"]
        self.assertEqual([node["node"] for node in nodes], ["A", "B"])
        expected_result_hashes = (
            "33b77405776d61634e7fdd8986681332df5828119114b976194fff973ef29b53",
            "671e6dd5408d13c968c4f87c8a5a8a7f64a1d2dfc499850594fa4bbff68070b2",
        )
        for node, expected_hash in zip(nodes, expected_result_hashes):
            self.assertTrue(node["benchmark_readback_verified"])
            self.assertTrue(node["capture_validated"])
            self.assertTrue(node["restore_readback_verified"])
            self.assertTrue(node["restore_reset_completed"])
            self.assertEqual(
                node["capture_diagnostics"],
                {
                    "lifecycle": "stable_continuous",
                    "reset_attempts": 1,
                    "lifecycle_polls": 4,
                    "stable_presence_polls": 3,
                    "open_attempts": 1,
                    "start_write_attempts": 1,
                    "read_calls": 518,
                    "empty_reads": 30,
                    "bytes_observed": 247934,
                    "preamble_lines_ignored": 0,
                    "complete_lines": 1015,
                    "frame_lines_buffered": 1014,
                },
            )
            result = node["result_summary"]
            self.assertEqual(result["candidate_id"], "monocypher")
            self.assertEqual(result["candidate_role"], "comparison")
            self.assertEqual(result["operations_completed"], 5)
            self.assertEqual(result["operations_required"], 5)
            self.assertEqual(result["cold_sample_count"], 500)
            self.assertEqual(result["warm_sample_count"], 500)
            self.assertEqual(result["gate_count"], 1)
            self.assertEqual(result["summary_count"], 10)
            self.assertEqual(
                set(result["summaries"]),
                {
                    "ed25519_sign",
                    "ed25519_verify",
                    "x25519",
                    "chacha20poly1305_encrypt",
                    "chacha20poly1305_decrypt",
                },
            )
            self.assertEqual(
                result["runtime_resources"],
                {
                    "heap_domain": "internal_8bit",
                    "heap_start_free_bytes": 349392,
                    "heap_min_free_bytes": 349372,
                    "peak_dynamic_ram_bytes": 20,
                    "stack_allocation_bytes": 8192,
                    "stack_high_water_free_bytes": 5048,
                    "max_stack_used_bytes": 3144,
                    "watchdog_measurement": "uninterrupted_terminal_frame",
                    "watchdog_resets": 0,
                },
            )
            self.assertEqual(result["result_sha256"], expected_hash)

    def test_04_restoration_and_owner_confirmation_are_exact(self) -> None:
        self.assertTrue(self.receipt["restoration_complete"])
        self.assertFalse(self.receipt["recovery_required"])
        self.assertTrue(self.receipt["owner_observed_both_trail_logos_after_success"])

    def test_05_immutable_bindings_and_private_anchors_are_exact(self) -> None:
        bindings = self.receipt["bindings"]
        expected_hashes = {
            "coordinator": "2e8955c7a425c33208d8b6e944fdef52259cd146274bec44c543e1f4b7af69be",
            "runner": "e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993",
            "frame_parser": "2276aab6246898186804d39b08be342989ad2cf2c804b546b43cbab31350a721",
            "frame_schema": "516cab7753d1ca22f59181480df4eed4aec232150035cee6a7a116fbb539c0e0",
            "adapter": "19eb64c29deda5af9e4d523a8bc1630ae2f9350cba4043e35ac7691146b09b71",
            "authority_tool": "d0598b529438d0b37671d65fd6c17d97791d2ab564806b378203e2834ecd1a52",
        }
        for key, expected in expected_hashes.items():
            self.assertEqual(bindings[key]["sha256"], expected)
        self.assertEqual(
            bindings["preparation"]["raw_sha256"],
            "9736e6c68647da594e31b8c32e076fd40c2fccedd1bbd1d740b5cc0443667455",
        )
        self.assertEqual(
            bindings["authority"]["raw_sha256"],
            "29c84ae8de494be2ac08e35cdeba5c4409f9381bc4f457a29cef64fb391afe31",
        )
        self.assertEqual(
            bindings["benchmark"],
            {
                "name": "ot142_monocypher_corrected_bench.bin",
                "bytes": 149824,
                "sha256": "8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034",
            },
        )
        self.assertEqual(bindings["restore"]["bytes"], 473152)
        self.assertEqual(
            bindings["restore"]["sha256"],
            "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741",
        )
        self.assertEqual(bindings["application_offset"], 65536)
        self.assertEqual(bindings["baud"], 115200)
        self.assertEqual(
            bindings["private_execution_receipt_sha256"],
            "beb9947d04c8ca3557fb1beb3efa4982bfd23f94968292db7e680f14da2369fc",
        )
        self.assertEqual(
            bindings["private_journal_sha256"],
            "da9c0503253249900984ffd46b45edcaeeee08808ee96208d750bf7551404e48",
        )

    def test_06_all_non_result_claims_remain_false(self) -> None:
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_07_privacy_is_strict_and_private_shapes_are_absent(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_role_labels_only"])
        self.assertTrue(
            all(value is False for key, value in privacy.items() if key != "anonymous_role_labels_only")
        )
        self.assertIsNone(
            re.search(
                r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-|usb vid|(?:[0-9A-F]{2}[:-]){5}[0-9A-F]{2}",
                self.raw,
            )
        )
        self.assertNotIn("raw_capture", json.dumps(self.receipt["nodes"]).lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
