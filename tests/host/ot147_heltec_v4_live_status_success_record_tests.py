#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "display" / (
    "OT-147-HELTEC-V4-LIVE-STATUS-EXECUTION-RECEIPT-V0.json"
)
RECEIPT_SHA256 = "165893b72537b04da210e1dbec268b7681f98c2a8aa0ba6bcf0906c3a28897cd"
AUTHORITY_RAW_SHA256 = "a367ce3a13a89241aa6bd55c2ffec4adb17c15262e6c640c1608b2e43b8a43c5"
OLD_APPLICATION = {
    "bytes": 473152,
    "sha256": "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741",
}
NEW_APPLICATION = {
    "bytes": 500944,
    "sha256": "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
}


class Ot147HeltecV4LiveStatusSuccessRecordTests(unittest.TestCase):
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
        self.assertEqual(self.receipt["schema"], "OT147LSR0")
        self.assertEqual(self.receipt["version"], 0)

    def test_02_terminal_result_and_authority_consumption_are_exact(self) -> None:
        self.assertEqual(
            self.receipt["result"], "two_node_live_status_application_flash_passed"
        )
        self.assertIsNone(self.receipt["failure"])
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 2)
        self.assertEqual(
            self.receipt["authority"],
            {
                "accepted_raw_sha256": AUTHORITY_RAW_SHA256,
                "consumed_by_success": True,
                "continuing_authority": False,
                "reusable": False,
            },
        )

    def test_03_both_anonymous_nodes_have_exact_pre_read_write_and_readback(self) -> None:
        nodes = self.receipt["nodes"]
        self.assertEqual([node["node"] for node in nodes], ["A", "B"])
        for node in nodes:
            self.assertEqual(node["pre_read"], OLD_APPLICATION)
            self.assertEqual(node["application_write_count"], 1)
            self.assertEqual(
                node["written_application"],
                {"bytes": 500944, "offset": 65536, "sha256": NEW_APPLICATION["sha256"]},
            )
            self.assertTrue(node["independent_application_readback_verified"])
            self.assertTrue(node["hard_reset_completed"])

    def test_04_sequence_and_write_boundary_are_exact(self) -> None:
        self.assertEqual(
            self.receipt["bindings"],
            {
                "application_offset": 65536,
                "installed_application": OLD_APPLICATION,
                "new_application": NEW_APPLICATION,
            },
        )
        self.assertEqual(
            self.receipt["operations"],
            {
                "application_only_writes": True,
                "both_pre_reads_completed_before_consumption_marker_and_first_write": True,
                "non_application_writes": False,
                "phone_operations": False,
                "radio_operations": False,
            },
        )

    def test_05_owner_acceptance_is_bounded_to_visible_live_footers(self) -> None:
        self.assertEqual(
            self.receipt["owner_observation"],
            {
                "both_battery_values_visible": True,
                "both_gps_values_visible": True,
                "both_live_footers_physically_accepted": True,
            },
        )

    def test_06_unproven_accuracy_radio_phone_and_readiness_claims_remain_false(self) -> None:
        self.assertEqual(
            set(self.receipt["claims"]),
            {
                "battery_accuracy_calibrated",
                "field_ready_proven",
                "full_product_readiness_changed",
                "gnss_fix_or_accuracy_physically_proven",
                "lora_activity_arrow_proven",
                "phone_integration_proven",
                "radio_activity_proven",
                "support_status_changed",
            },
        )
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_07_privacy_is_strict_and_raw_capture_shapes_are_absent(self) -> None:
        self.assertEqual(
            self.receipt["privacy"],
            {
                "anonymous_node_labels_only": True,
                "device_identifiers_included": False,
                "device_paths_included": False,
                "mac_addresses_included": False,
                "ports_included": False,
                "raw_captures_included": False,
            },
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
