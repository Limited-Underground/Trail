#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "display" / (
    "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-EXECUTION-RECEIPT-V1.json"
)
RECEIPT_SHA256 = "3a6410b36eee38378c5a0c741a40c7eda567f105bfe9d6ded7411060b7169dca"
AUTHORITY_RAW_SHA256 = "0e4152642bac86a1dd9502ea4e5b34fc487cd98d4db048dd24d757d316f0228a"
OLD_APPLICATION = {
    "bytes": 507168,
    "sha256": "6ba4c1f6256e8d94cb7ffb24f5d10640d95b21a1b00e6d131833f2f2ddc63ebd",
}
NEW_APPLICATION = {
    "bytes": 507296,
    "sha256": "69ae6546724d2ebbd6a3b8a133d1fc267383839c632b3b73e6a1ca72abf4678a",
}


class Ot164PairingWindowV1SuccessRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw_bytes = RECEIPT.read_bytes()
        cls.raw = cls.raw_bytes.decode("ascii")
        cls.receipt = json.loads(cls.raw)

    def test_01_receipt_is_canonical_single_line_ascii_json(self) -> None:
        self.assertEqual(
            self.raw,
            json.dumps(self.receipt, sort_keys=True, ensure_ascii=True, separators=(",", ":")) + "\n",
        )
        self.assertNotIn("\r", self.raw)
        self.assertEqual(self.raw.count("\n"), 1)
        self.assertEqual(hashlib.sha256(self.raw_bytes).hexdigest(), RECEIPT_SHA256)
        self.assertEqual(self.receipt["schema"], "OT164PWR1")
        self.assertEqual(self.receipt["version"], 1)

    def test_02_authority_is_consumed_once_without_inheritance(self) -> None:
        self.assertEqual(
            self.receipt["authority"],
            {
                "accepted_authority_id": "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V1",
                "accepted_raw_sha256": AUTHORITY_RAW_SHA256,
                "consumed_by_success": True,
                "continuing_authority": False,
                "reusable": False,
            },
        )
        self.assertFalse(self.receipt["predecessor"]["replacement_attempt_inherited"])
        self.assertIsNone(self.receipt["failure"])

    def test_03_two_nodes_have_exact_application_only_install_and_readback(self) -> None:
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 2)
        self.assertEqual([node["node"] for node in self.receipt["nodes"]], ["A", "B"])
        for node in self.receipt["nodes"]:
            self.assertEqual(node["pre_read"], OLD_APPLICATION)
            self.assertEqual(node["application_write_count"], 1)
            self.assertEqual(
                node["written_application"],
                {"bytes": NEW_APPLICATION["bytes"], "offset": 65536, "sha256": NEW_APPLICATION["sha256"]},
            )
            self.assertTrue(node["independent_application_readback_verified"])
            self.assertTrue(node["hard_reset_completed"])
        self.assertEqual(
            self.receipt["bindings"],
            {"application_offset": 65536, "installed_application": OLD_APPLICATION, "new_application": NEW_APPLICATION},
        )
        self.assertEqual(self.receipt["operations"]["recovery_write_count"], 0)
        self.assertTrue(self.receipt["operations"]["both_pre_reads_completed_before_first_write"])
        self.assertTrue(self.receipt["operations"]["application_only_writes"])
        self.assertFalse(self.receipt["operations"]["non_application_writes"])

    def test_04_owner_acceptance_is_exact_and_pin_free(self) -> None:
        observation = self.receipt["owner_observation"]
        for name in (
            "both_displays_restarted",
            "hold_release_opened_pairing_on_both",
            "pairing_window_physically_accepted",
            "reset_concealed_pairing_on_both",
            "short_press_rejected_on_both",
            "six_digits_observed_on_both",
            "timeout_approximately_30_seconds_on_both",
            "timeout_concealed_pairing_on_both",
        ):
            self.assertTrue(observation[name], name)
        self.assertFalse(observation["pairing_pin_values_recorded"])

    def test_05_claims_remain_bounded(self) -> None:
        claims = self.receipt["claims"]
        self.assertTrue(claims["hardware_executed"])
        self.assertTrue(claims["pairing_window_physically_accepted"])
        self.assertTrue(claims["public_status_changed"])
        for name in (
            "android_pairing_proven",
            "bond_ownership_proven",
            "durable_bond_proven",
            "field_ready_proven",
            "full_product_readiness_changed",
            "protected_gatt_authority_proven",
            "replacement_proven",
        ):
            self.assertFalse(claims[name], name)

    def test_06_privacy_is_strict(self) -> None:
        privacy = self.receipt["privacy"]
        self.assertTrue(privacy["anonymous_node_labels_only"])
        self.assertTrue(all(value is False for name, value in privacy.items() if name != "anonymous_node_labels_only"))
        self.assertIsNone(
            re.search(
                r"(?i)COM\d+|/dev/|[A-Z]:\\|\.private|PRIVATE-|usb vid|(?:[0-9A-F]{2}[:-]){5}[0-9A-F]{2}",
                self.raw,
            )
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
