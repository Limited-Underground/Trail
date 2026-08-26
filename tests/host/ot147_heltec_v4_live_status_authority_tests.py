#!/usr/bin/env python3
"""Validate the immutable OT-147 two-node live-status flash authority."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
AUTHORITY_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "display"
    / "OT-147-HELTEC-V4-LIVE-STATUS-BUILD-AND-FLASH-AUTHORITY-V0.json"
)
EXPECTED_RAW_SHA256 = "a367ce3a13a89241aa6bd55c2ffec4adb17c15262e6c640c1608b2e43b8a43c5"


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = AUTHORITY_PATH.read_bytes()
        cls.authority = json.loads(cls.raw.decode("ascii"))

    def test_canonical_raw_bytes(self) -> None:
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), EXPECTED_RAW_SHA256)
        self.assertNotIn(b"\r", self.raw)
        self.assertFalse(self.raw.startswith(b"\xef\xbb\xbf"))
        canonical = json.dumps(
            self.authority, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii") + b"\n"
        self.assertEqual(self.raw, canonical)

    def test_identity_and_acceptance(self) -> None:
        self.assertEqual(self.authority["schema"], "OT147LSA0")
        self.assertEqual(self.authority["version"], 0)
        self.assertEqual(
            self.authority["authority_id"],
            "OT-147-HELTEC-V4-LIVE-STATUS-BUILD-AND-FLASH-AUTHORITY-V0",
        )
        self.assertEqual(self.authority["recorded_date"], "2026-08-26")
        self.assertEqual(
            self.authority["status"], "authorized_one_attempt_not_executed"
        )
        owner = self.authority["owner_authorization"]
        self.assertTrue(owner["granted"])
        self.assertFalse(owner["permanent_platform_decision"])
        self.assertEqual(
            owner["scope"], "one_two_node_live_status_application_only_attempt"
        )

    def test_exact_reproducible_build(self) -> None:
        build = self.authority["build"]
        self.assertEqual(build["esp_idf_version"], "v6.0.2")
        self.assertEqual(
            build["esp_idf_commit"], "7101770dc6db2667b3c477cc31365dd1acd6db4e"
        )
        self.assertEqual(build["fixed_project_version"], "ot147-live-status-v0")
        self.assertTrue(build["reproducible"])
        self.assertEqual(
            build["config_defaults"],
            {
                "path": "build/targets/ot093-reproducible.defaults",
                "sha256": "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6",
            },
        )
        self.assertEqual(
            build["application"],
            {
                "bytes": 500944,
                "name": "opentrail_heltec_v4_bench.bin",
                "offset": 65536,
                "partition_capacity_bytes": 5177344,
                "sha256": "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
            },
        )
        self.assertEqual(build["included_units"], ["heltec_v4_battery.cpp", "heltec_v4_gnss.cpp"])
        self.assertEqual(build["image"]["chip"], "esp32s3")
        self.assertEqual(
            build["image"]["validation_hash"],
            "f5dd6c37f7f95fb3cbb2fdc90c8bdeea6b2c5393db068a620a08cded377ee38d",
        )
        self.assertTrue(all(build["validation"].values()))

    def test_live_footer_scope(self) -> None:
        behavior = self.authority["behavior"]
        self.assertEqual(behavior["battery_sample_period_milliseconds"], 30000)
        self.assertEqual(behavior["battery_freshness_milliseconds"], 60000)
        self.assertTrue(behavior["battery_approximate_voltage_derived_percent"])
        self.assertEqual(behavior["gnss_freshness_milliseconds"], 5000)
        self.assertTrue(behavior["gnss_satellite_count_only"])
        self.assertEqual(behavior["gps_valid_zero_display"], "GPS:0")
        self.assertEqual(behavior["gps_unavailable_display"], "GPS:--")
        self.assertTrue(behavior["ble_status_retained"])
        self.assertFalse(behavior["lora_activity_arrow_bound"])

    def test_two_node_application_only_boundary(self) -> None:
        execution = self.authority["execution"]
        self.assertEqual(execution["node_count"], 2)
        self.assertEqual(execution["attempt_count"], 1)
        self.assertTrue(execution["distinct_anonymous_endpoints_required"])
        self.assertTrue(execution["both_installed_application_readbacks_before_first_write"])
        self.assertTrue(execution["application_only_writes"])
        self.assertEqual(execution["application_offset"], 65536)
        self.assertEqual(execution["esptool_version"], "5.3.1")
        self.assertEqual(execution["baud"], 115200)
        self.assertTrue(execution["independent_application_readback_required"])
        self.assertTrue(execution["hard_reset_after_each_node"])
        self.assertTrue(execution["stop_before_first_write_on_preflight_mismatch"])
        self.assertTrue(execution["write_each_node_once"])
        self.assertTrue(execution["same_image_recovery_after_partial_write_required"])
        self.assertEqual(
            execution["expected_installed_application"],
            {
                "bytes": 473152,
                "sha256": "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741",
            },
        )
        self.assertEqual(
            execution["prohibited_writes"],
            ["bootloader", "partition_table", "ota_data", "nvs", "fuses", "radio"],
        )

    def test_one_attempt_consumption_and_privacy(self) -> None:
        self.assertEqual(
            self.authority["consumption"],
            {
                "consumed_on_success_or_abort": True,
                "continuing_authority": False,
                "reusable": False,
            },
        )
        self.assertEqual(
            self.authority["privacy"],
            {
                "anonymous_node_labels_only": True,
                "device_identifiers_forbidden": True,
                "private_receipt_path_not_published": True,
            },
        )

    def test_claims_remain_bounded_before_hardware(self) -> None:
        claims = self.authority["claims"]
        self.assertTrue(claims["build_validated"])
        for name in (
            "battery_accuracy_calibrated",
            "field_ready_proven",
            "gnss_fix_physically_proven",
            "hardware_executed",
            "physical_display_accepted",
            "public_status_changed",
            "radio_activity_proven",
            "support_status_changed",
        ):
            self.assertFalse(claims[name], name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
