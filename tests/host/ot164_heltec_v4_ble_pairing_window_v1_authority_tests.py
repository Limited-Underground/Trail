#!/usr/bin/env python3
"""Validate the corrected immutable OT-164 two-node pairing-window flash authority."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
AUTHORITY_PATH = ROOT / "tests" / "benchmarks" / "display" / (
    "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V1.json"
)
EXPECTED_RAW_SHA256 = "0e4152642bac86a1dd9502ea4e5b34fc487cd98d4db048dd24d757d316f0228a"


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

    def test_identity_authorization_and_supersession(self) -> None:
        self.assertEqual(self.authority["schema"], "OT164PWA1")
        self.assertEqual(self.authority["version"], 1)
        self.assertEqual(
            self.authority["authority_id"],
            "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V1",
        )
        self.assertEqual(self.authority["recorded_date"], "2026-08-29")
        self.assertEqual(self.authority["status"], "authorized_one_attempt_not_executed")
        self.assertEqual(
            self.authority["supersedes"],
            {
                "authority_id": "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V0",
                "authority_raw_sha256": "e491c67d4988bb79db283f50adb29f488e43c730ce63bedabb3860032241f8cb",
                "consumed": True,
                "reason": "preacceptance_fail_closed_display_concealment_correction",
                "replacement_attempt_inherited": False,
                "reusable": False,
                "terminal_receipt_path": "tests/benchmarks/display/OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-EXECUTION-RECEIPT-V0.json",
                "terminal_receipt_raw_sha256": "e2a51fa64471de04c9619d78b757565dc586272ac53abde57a66294a8428ba4e",
            },
        )
        self.assertTrue(self.authority["owner_authorization"]["granted"])

    def test_exact_reproducible_corrected_build(self) -> None:
        build = self.authority["build"]
        self.assertEqual(build["esp_idf_version"], "v6.0.2")
        self.assertEqual(build["esp_idf_commit"], "7101770dc6db2667b3c477cc31365dd1acd6db4e")
        self.assertEqual(build["fixed_project_version"], "ot164-pairing-window-v0")
        self.assertTrue(build["reproducible"])
        self.assertEqual(
            build["application"],
            {
                "bytes": 507296,
                "name": "opentrail_heltec_v4_bench.bin",
                "offset": 65536,
                "partition_capacity_bytes": 5177344,
                "sha256": "69ae6546724d2ebbd6a3b8a133d1fc267383839c632b3b73e6a1ca72abf4678a",
            },
        )
        self.assertEqual(build["image"]["validation_hash"], "6e7c31b54b0723e537ab3f22ce0c56a31cd549d9d5875bded96bb9d35d6138e7")
        self.assertEqual(build["image"]["elf_sha256"], "b8a53ba8bb5f75379d951856438ee38d4dd348eb220dd1f4c1909d75e64edefc")
        self.assertTrue(all(bool(value) for value in build["validation"].values()))

    def test_pairing_window_and_emergency_concealment_are_exact(self) -> None:
        behavior = self.authority["behavior"]
        self.assertEqual(behavior["button"]["gpio"], 0)
        self.assertEqual(behavior["button"]["hold_milliseconds"], 3000)
        self.assertTrue(behavior["button"]["release_required"])
        self.assertEqual(behavior["display"]["digits"], 6)
        self.assertEqual(behavior["display"]["duration_milliseconds"], 30000)
        self.assertTrue(behavior["display"]["exact_duration"])
        self.assertTrue(all(behavior["clearing"].values()))
        self.assertEqual(behavior["pairing"]["random_source"], "esp_fill_random_after_nimble_sync")
        self.assertTrue(behavior["pairing"]["rejection_sampling"])

    def test_two_node_application_only_boundary(self) -> None:
        execution = self.authority["execution"]
        self.assertEqual(execution["node_count"], 2)
        self.assertEqual(execution["attempt_count"], 1)
        self.assertTrue(execution["both_installed_application_readbacks_before_first_write"])
        self.assertTrue(execution["application_only_writes"])
        self.assertEqual(execution["application_offset"], 65536)
        self.assertEqual(
            execution["expected_installed_application"],
            {
                "bytes": 507168,
                "sha256": "6ba4c1f6256e8d94cb7ffb24f5d10640d95b21a1b00e6d131833f2f2ddc63ebd",
            },
        )
        self.assertEqual(
            execution["prohibited_writes"],
            ["bootloader", "partition_table", "ota_data", "nvs", "fuses", "radio"],
        )

    def test_one_attempt_consumption_privacy_and_bounded_claims(self) -> None:
        self.assertEqual(
            self.authority["consumption"],
            {"consumed_on_success_or_abort": True, "continuing_authority": False, "reusable": False},
        )
        self.assertTrue(self.authority["privacy"]["pairing_pin_capture_forbidden"])
        self.assertTrue(self.authority["claims"]["build_validated"])
        for name, value in self.authority["claims"].items():
            if name != "build_validated":
                self.assertFalse(value, name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
