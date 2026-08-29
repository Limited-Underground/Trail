#!/usr/bin/env python3
"""Validate the immutable OT-164 two-node pairing-window flash authority."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
AUTHORITY_PATH = ROOT / "tests" / "benchmarks" / "display" / (
    "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V0.json"
)
EXPECTED_RAW_SHA256 = "e491c67d4988bb79db283f50adb29f488e43c730ce63bedabb3860032241f8cb"


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

    def test_identity_and_authorization(self) -> None:
        self.assertEqual(self.authority["schema"], "OT164PWA0")
        self.assertEqual(self.authority["version"], 0)
        self.assertEqual(
            self.authority["authority_id"],
            "OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V0",
        )
        self.assertEqual(self.authority["recorded_date"], "2026-08-29")
        self.assertEqual(
            self.authority["status"], "authorized_one_attempt_not_executed"
        )
        self.assertEqual(
            self.authority["owner_authorization"],
            {
                "granted": True,
                "permanent_platform_decision": False,
                "scope": "one_two_node_ble_pairing_window_application_only_attempt",
            },
        )

    def test_exact_reproducible_build(self) -> None:
        build = self.authority["build"]
        self.assertEqual(build["esp_idf_version"], "v6.0.2")
        self.assertEqual(
            build["esp_idf_commit"], "7101770dc6db2667b3c477cc31365dd1acd6db4e"
        )
        self.assertEqual(build["fixed_project_version"], "ot164-pairing-window-v0")
        self.assertTrue(build["reproducible"])
        self.assertEqual(
            build["application"],
            {
                "bytes": 507168,
                "name": "opentrail_heltec_v4_bench.bin",
                "offset": 65536,
                "partition_capacity_bytes": 5177344,
                "sha256": "6ba4c1f6256e8d94cb7ffb24f5d10640d95b21a1b00e6d131833f2f2ddc63ebd",
            },
        )
        self.assertEqual(
            build["image"]["validation_hash"],
            "ca49eaa2922e8a4a44638431a0b47f58b0dd3c5d3712de1e9ecd8d84fec5983a",
        )
        self.assertEqual(
            build["image"]["elf_sha256"],
            "b5f1c76d5a3fc1a6fe62d6e990a71b336245588b0281957933dbecea48ad6801",
        )
        self.assertEqual(build["image"]["chip"], "esp32s3")
        self.assertTrue(all(bool(value) for value in build["validation"].values()))

    def test_security_and_pairing_window_are_exact(self) -> None:
        self.assertEqual(
            self.authority["build"]["security"],
            {
                "bond_persistence": False,
                "max_bonds": 1,
                "secure_connections_only": True,
                "secret_debug_logging_compiled_out": True,
            },
        )
        behavior = self.authority["behavior"]
        self.assertEqual(
            behavior["button"],
            {
                "active_low": True,
                "boot_release_required": True,
                "debounce_milliseconds": 40,
                "gpio": 0,
                "hold_milliseconds": 3000,
                "release_required": True,
            },
        )
        self.assertEqual(behavior["display"]["digits"], 6)
        self.assertEqual(behavior["display"]["duration_milliseconds"], 30000)
        self.assertTrue(behavior["display"]["exact_duration"])
        self.assertTrue(all(behavior["clearing"].values()))
        pairing = behavior["pairing"]
        self.assertEqual(pairing["random_source"], "esp_fill_random_after_nimble_sync")
        self.assertTrue(pairing["fresh_per_window"])
        self.assertTrue(pairing["one_candidate_per_window"])
        self.assertTrue(pairing["one_passkey_action_per_window"])
        self.assertTrue(pairing["rejection_sampling"])

    def test_two_node_application_only_boundary(self) -> None:
        execution = self.authority["execution"]
        self.assertEqual(execution["node_count"], 2)
        self.assertEqual(execution["attempt_count"], 1)
        self.assertTrue(execution["distinct_anonymous_endpoints_required"])
        self.assertTrue(execution["both_installed_application_readbacks_before_first_write"])
        self.assertTrue(execution["application_only_writes"])
        self.assertEqual(execution["application_offset"], 65536)
        self.assertEqual(execution["esptool_version"], "5.3.1")
        self.assertEqual(
            execution["expected_installed_application"],
            {
                "bytes": 500944,
                "sha256": "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
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
                "pairing_pin_capture_forbidden": True,
                "private_receipt_path_not_published": True,
            },
        )

    def test_claims_remain_bounded_before_hardware(self) -> None:
        claims = self.authority["claims"]
        self.assertTrue(claims["build_validated"])
        for name, value in claims.items():
            if name != "build_validated":
                self.assertFalse(value, name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
