#!/usr/bin/env python3

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "heltec_development_identity.py"
SPEC = importlib.util.spec_from_file_location("heltec_development_identity", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

# Synthetic octets only; no physical device identifier is part of these tests.
PARSE_MAC = ":".join(("10", "20", "30", "AA", "BB", "CC"))
MAC_A = ":".join(("10", "20", "30", "40", "50", "60"))
MAC_B = ":".join(("10", "20", "30", "40", "50", "61"))
MAC_C = ":".join(("10", "20", "30", "40", "50", "62"))


class HeltecDevelopmentIdentityTests(unittest.TestCase):
    def test_parses_exact_esptool_mac(self):
        self.assertEqual(
            PARSE_MAC,
            MODULE.parse_base_mac(f"esptool v5.3.1\nMAC: {PARSE_MAC.lower()}\n"),
        )

    def test_accepts_repeated_matching_mac_and_rejects_ambiguity(self):
        self.assertEqual(
            MAC_A,
            MODULE.parse_base_mac(f"MAC: {MAC_A}\nMAC: {MAC_A}"),
        )
        with self.assertRaisesRegex(MODULE.IdentityError, "read_mac_output_invalid"):
            MODULE.parse_base_mac("no identity")
        with self.assertRaisesRegex(MODULE.IdentityError, "read_mac_output_invalid"):
            MODULE.parse_base_mac(f"MAC: {MAC_A}\nMAC: {MAC_B}")

    def test_enrollment_is_idempotent_and_updates_verification_time(self):
        registry = MODULE.new_registry()
        self.assertEqual(
            "ENROLLED",
            MODULE.enroll(registry, "OT-DEV-001", "102030405060", "first"),
        )
        self.assertEqual(
            "MATCHED",
            MODULE.enroll(registry, "ot-dev-001", MAC_A, "second"),
        )
        self.assertEqual(1, len(registry["devices"]))
        self.assertEqual("second", registry["devices"][0]["last_verified_at"])

    def test_refuses_id_or_mac_rebinding(self):
        registry = MODULE.new_registry()
        MODULE.enroll(registry, "OT-DEV-001", MAC_A, "first")
        with self.assertRaisesRegex(MODULE.IdentityError, "inventory_id_already_bound"):
            MODULE.enroll(registry, "OT-DEV-001", MAC_B, "second")
        with self.assertRaisesRegex(MODULE.IdentityError, "base_mac_already_bound"):
            MODULE.enroll(registry, "OT-DEV-002", MAC_A, "second")

    def test_verify_fails_closed(self):
        registry = MODULE.new_registry()
        MODULE.enroll(registry, "OT-DEV-001", MAC_A, "first")
        MODULE.verify(registry, "OT-DEV-001", MAC_A)
        with self.assertRaisesRegex(MODULE.IdentityError, "device_identity_mismatch"):
            MODULE.verify(registry, "OT-DEV-001", MAC_B)
        with self.assertRaisesRegex(MODULE.IdentityError, "inventory_id_not_enrolled"):
            MODULE.verify(registry, "OT-DEV-002", MAC_C)

    def test_registry_round_trip_and_duplicate_rejection(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "identities.json"
            registry = MODULE.new_registry()
            MODULE.enroll(registry, "OT-DEV-001", MAC_A, "first")
            MODULE.save_registry(path, registry)
            self.assertEqual(registry, MODULE.load_registry(path))

            payload = json.loads(path.read_text(encoding="utf-8"))
            payload["devices"].append(
                {
                    "inventory_id": "OT-DEV-002",
                    "esp32s3_base_mac": MAC_A,
                }
            )
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(MODULE.IdentityError, "registry_identity_duplicate"):
                MODULE.load_registry(path)


if __name__ == "__main__":
    unittest.main()
