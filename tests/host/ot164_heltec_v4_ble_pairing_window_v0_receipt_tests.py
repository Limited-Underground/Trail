#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RECEIPT = ROOT / "tests" / "benchmarks" / "display" / (
    "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-EXECUTION-RECEIPT-V0.json"
)
RECEIPT_SHA256 = "e2a51fa64471de04c9619d78b757565dc586272ac53abde57a66294a8428ba4e"
AUTHORITY_RAW_SHA256 = "e491c67d4988bb79db283f50adb29f488e43c730ce63bedabb3860032241f8cb"
OLD_APPLICATION = {
    "bytes": 500944,
    "sha256": "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
}
REJECTED_APPLICATION = {
    "bytes": 507168,
    "sha256": "6ba4c1f6256e8d94cb7ffb24f5d10640d95b21a1b00e6d131833f2f2ddc63ebd",
}


class Ot164PairingWindowV0ReceiptTests(unittest.TestCase):
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
        self.assertEqual(self.receipt["schema"], "OT164PWR0")
        self.assertEqual(self.receipt["version"], 0)

    def test_02_v0_is_consumed_and_explicitly_rejected_before_acceptance(self) -> None:
        self.assertEqual(
            self.receipt["result"],
            "two_node_application_flash_installed_but_preacceptance_audit_rejected",
        )
        self.assertEqual(
            self.receipt["failure"],
            {
                "classification": "preacceptance_fail_closed_display_concealment_defect",
                "detected_after_verified_installation": True,
                "hardware_write_failed": False,
            },
        )
        self.assertEqual(
            self.receipt["authority"],
            {
                "accepted_raw_sha256": AUTHORITY_RAW_SHA256,
                "consumed_by_abort": True,
                "continuing_authority": False,
                "reusable": False,
            },
        )
        self.assertEqual(
            self.receipt["superseded_by"],
            "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V1",
        )

    def test_03_both_nodes_have_exact_verified_application_only_writes(self) -> None:
        self.assertEqual(self.receipt["node_count"], 2)
        self.assertEqual(self.receipt["touched_node_count"], 2)
        self.assertEqual([node["node"] for node in self.receipt["nodes"]], ["A", "B"])
        for node in self.receipt["nodes"]:
            self.assertEqual(node["pre_read"], OLD_APPLICATION)
            self.assertEqual(node["application_write_count"], 1)
            self.assertEqual(
                node["written_application"],
                {"bytes": 507168, "offset": 65536, "sha256": REJECTED_APPLICATION["sha256"]},
            )
            self.assertTrue(node["independent_application_readback_verified"])
            self.assertTrue(node["hard_reset_completed"])
        self.assertEqual(
            self.receipt["bindings"],
            {
                "application_offset": 65536,
                "installed_application": OLD_APPLICATION,
                "new_application": REJECTED_APPLICATION,
            },
        )
        self.assertTrue(self.receipt["operations"]["application_only_writes"])
        self.assertFalse(self.receipt["operations"]["non_application_writes"])

    def test_04_no_acceptance_or_readiness_claim_is_inferred(self) -> None:
        self.assertFalse(self.receipt["owner_observation"]["pairing_window_physically_accepted"])
        self.assertTrue(all(value is False for value in self.receipt["claims"].values()))

    def test_05_privacy_is_strict(self) -> None:
        self.assertEqual(
            self.receipt["privacy"],
            {
                "anonymous_node_labels_only": True,
                "device_identifiers_included": False,
                "device_paths_included": False,
                "mac_addresses_included": False,
                "pairing_pins_included": False,
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
