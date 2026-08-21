#!/usr/bin/env python3
import copy
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unicodedata
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/crypto_received_target_profile_admission.py"
ART = ROOT / "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json"
OT094 = ROOT / "tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json"
OT102 = ROOT / "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
OT059 = ROOT / "tests/hardware/OT-059-2026-08-15.md"
OT061 = ROOT / "tests/hardware/OT-061-2026-08-16.md"
spec = importlib.util.spec_from_file_location("otrtpa", TOOL)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.admission = json.loads(ART.read_text(encoding="utf-8"))
        cls.evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))

    def mutate_admission(self, action):
        value = copy.deepcopy(self.admission)
        action(value)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "admission.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(m.AdmissionError):
                m.validate(path, enforce_digest=False)

    def mutate_evidence(self, action):
        value = copy.deepcopy(self.evidence)
        action(value)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(m.AdmissionError):
                m.validate(ART, path, enforce_digest=False)

    def test_exact_chain_digests_and_three_blockers(self):
        data = m.validate()
        self.assertEqual(data["current_three_blockers"], m.CURRENT_THREE)
        self.assertEqual((len(data["historical_six_blockers"]), len(data["prior_current_four_blockers"]), len(data["current_three_blockers"])), (6, 4, 3))
        self.assertEqual(data["preserved_crypto_acceptance_counts"], {"source": 2, "api_config": 0, "candidate_import": 0})
        self.assertTrue(data["claims"]["exact_received_target_profile_accepted"])
        self.assertFalse(data["claims"]["readiness_accepted"])
        for path, expected in ((OT094, m.OT094_SHA), (OT102, m.OT102_SHA), (OT059, m.OT059_SHA), (OT061, m.OT061_SHA), (EVIDENCE, m.EVIDENCE_SHA), (ART, m.ADMISSION_SHA)):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)
        result = subprocess.run([sys.executable, str(TOOL)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn("THREE-OTCBR0-REQUIREMENTS-REMAIN", result.stdout)

    def test_exact_profile_and_photo_bindings(self):
        evidence = m._validate_evidence(m._json(EVIDENCE, m.EVIDENCE_SHA))
        self.assertEqual((evidence["target_binding"]["target_id"], evidence["target_binding"]["evidence_unit"]), ("heltec-v4-bench-candidate", "OT-DEV-001"))
        self.assertEqual((evidence["target_binding"]["pcb_model"], evidence["target_binding"]["exact_received_revision"]), ("HTIT-WB32LAF", "V4.2"))
        self.assertEqual((evidence["exact_profile"]["flash_bytes"], evidence["exact_profile"]["psram_bytes"]), (16777216, 2097152))
        photos = evidence["owner_photo_evidence"]
        self.assertEqual(len(photos), 5)
        self.assertEqual(sum(item["closure_input"] for item in photos), 1)
        self.assertEqual(len({item["sha256"] for item in photos}), 5)
        self.assertEqual([(item["bytes"], item["width_px"], item["height_px"]) for item in photos], [(205908, 960, 1280), (241142, 960, 1280), (120968, 1280, 960), (128451, 1280, 960), (98820, 960, 1280)])
        self.assertFalse(photos[-1]["printed_checkbox_state_claimed"])

    def test_manufacturer_tables_remain_distinct_and_bounded(self):
        source = self.evidence["official_manufacturer_source"]
        self.assertEqual((source["url"], source["sha256"], source["bytes"]), (m.PDF_URL, m.PDF_SHA, 1349532))
        self.assertFalse(source["retained_in_repository"])
        table_15, table_351, general = source["facts"]
        self.assertEqual((table_15["table"], table_15["frequency_min_mhz"], table_15["frequency_max_mhz"]), ("1.5", 868, 928))
        self.assertEqual((table_351["table"], table_351["frequency_min_mhz"], table_351["frequency_max_mhz"]), ("3.5.1", 863, 928))
        self.assertEqual((general["master_chip"], general["lora_chip"]), ("ESP32-S3R2", "SX1262"))
        self.assertFalse(self.evidence["boundaries"]["band_values_normalized_or_reconciled"])
        self.assertFalse(self.evidence["boundaries"]["manufacturer_lora_chip_electrically_verified"])
        self.assertFalse(self.evidence["boundaries"]["direct_radio_profile_resolved"])

    def test_admission_mutations_fail_closed(self):
        actions = [
            lambda d: d.__setitem__("schema", "changed"),
            lambda d: d.__setitem__("version", False),
            lambda d: d.__setitem__("unknown", False),
            lambda d: d["parents"].__setitem__("ot061_raw_sha256", "0" * 64),
            lambda d: d["profile_evidence"].__setitem__("sha256", "0" * 64),
            lambda d: d["accepted_target_profile"].__setitem__("board_model", "HTIT-WB32LAF-N-HF"),
            lambda d: d["accepted_target_profile"].__setitem__("supported", True),
            lambda d: d["accepted_target_profile"].__setitem__("supported", 0),
            lambda d: d["evidence_counts"].__setitem__("owner_photos", True),
            lambda d: d["evidence_counts"].__setitem__("closure_input_photos", True),
            lambda d: d["preserved_crypto_acceptance_counts"].__setitem__("candidate_import", 1),
            lambda d: d["preserved_crypto_acceptance_counts"].__setitem__("api_config", False),
            lambda d: d["historical_six_blockers"].pop(),
            lambda d: d["prior_current_four_blockers"].pop(),
            lambda d: d["current_three_blockers"].append("exact_received_target_profile_unresolved"),
            lambda d: d["closed_by_this_delta"].append({"blocker_id": "direct_radio_mtu_phy_region_unresolved", "closure_evidence_sha256": m.EVIDENCE_SHA}),
            lambda d: d["owner_evidence"].__setitem__("new_device_access_for_this_increment", True),
            lambda d: d["owner_evidence"].__setitem__("new_device_access_for_this_increment", 0),
            lambda d: d["owner_evidence"].__setitem__("owner_supplied_photo_set", 1),
            lambda d: d["authority"].__setitem__("benchmark_execution_authorized", True),
            lambda d: d["authority"].pop("device_access_authorized"),
            lambda d: d["authority"].__setitem__("invented", False),
            lambda d: d["claims"].__setitem__("readiness_accepted", True),
            lambda d: d["claims"].__setitem__("exact_received_target_profile_accepted", False),
            lambda d: d["claims"].__setitem__("hardware_support_claimed", True),
            lambda d: d["claims"].__setitem__("regulatory_acceptance_claimed", True),
            lambda d: d["claims"].__setitem__("direct_radio_profile_resolved", True),
            lambda d: d["claims"].pop("score_credit_added"),
            lambda d: d["claims"].__setitem__("invented", False),
        ]
        for action in actions:
            with self.subTest(action=action):
                self.mutate_admission(action)

    def test_evidence_mutations_fail_closed(self):
        actions = [
            lambda d: d.__setitem__("schema", "changed"),
            lambda d: d["target_binding"].__setitem__("evidence_unit", "OT-DEV-002"),
            lambda d: d["target_binding"].__setitem__("exact_received_revision", "V4"),
            lambda d: d["exact_profile"].__setitem__("manufacturer_lora_chip_family", "SX1262-confirmed"),
            lambda d: d["parent_evidence"].__setitem__("ot059_raw_sha256", "0" * 64),
            lambda d: d["owner_photo_evidence"][0].__setitem__("sha256", "0" * 64),
            lambda d: d["owner_photo_evidence"][0].__setitem__("closure_input", 0),
            lambda d: d["owner_photo_evidence"][1].__setitem__("closure_input", 1),
            lambda d: d["owner_photo_evidence"][1]["privacy_safe_markings"].append("lot-string"),
            lambda d: d["owner_photo_evidence"][2]["assembly_observations"].append("new-model-claim"),
            lambda d: d["owner_photo_evidence"][4].__setitem__("printed_checkbox_state_claimed", True),
            lambda d: d["owner_photo_evidence"][4].__setitem__("printed_checkbox_state_claimed", 0),
            lambda d: d["official_manufacturer_source"].__setitem__("bytes", 1),
            lambda d: d["official_manufacturer_source"].__setitem__("retained_in_repository", 0),
            lambda d: d["official_manufacturer_source"]["facts"][0].__setitem__("frequency_min_mhz", 863),
            lambda d: d["official_manufacturer_source"]["facts"][0].__setitem__("maximum_tx_power_tolerance_db", True),
            lambda d: d["official_manufacturer_source"]["facts"][2].__setitem__("lora_chip", "SX1262-electrically-verified"),
            lambda d: d["closure_basis"]["resolved_field_set"].append("legal_region"),
            lambda d: d["boundaries"].__setitem__("hardware_support_claimed", True),
            lambda d: d["boundaries"].__setitem__("direct_radio_profile_resolved", True),
            lambda d: d["boundaries"].pop("local_paths_retained"),
        ]
        for action in actions:
            with self.subTest(action=action):
                self.mutate_evidence(action)

    def test_parent_tamper_json_bounds_and_duplicate_keys_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            paths = []
            for index, source in enumerate((OT094, OT102, OT059, OT061)):
                changed = directory / f"changed-{index}"
                changed.write_bytes(source.read_bytes() + b" ")
                paths.append(changed)
            for supplied in ((paths[0], OT102, OT059, OT061), (OT094, paths[1], OT059, OT061), (OT094, OT102, paths[2], OT061), (OT094, OT102, OT059, paths[3])):
                with self.subTest(supplied=supplied), self.assertRaises(m.AdmissionError):
                    m.validate(ART, EVIDENCE, *supplied)
            cases = {
                "duplicate": b'{"schema":"OTRTPA0","schema":"OTRTPA0"}',
                "malformed": b"{",
                "oversized": b"x" * (m.MAX_BYTES + 1),
            }
            for name, raw in cases.items():
                path = directory / f"{name}.json"
                path.write_bytes(raw)
                with self.subTest(name=name), self.assertRaises(m.AdmissionError):
                    m._json(path)
        for value in ("x" * (m.MAX_STRING + 1), unicodedata.normalize("NFD", "caf\u00e9"), "C:\\private", "/Users/private", "COM77", "secret=value", ":".join(("00", "11", "22", "33", "44", "55"))):
            with self.subTest(value=value[:16]), self.assertRaises(m.AdmissionError):
                m._scan(value)

    def test_offline_pure_surface_privacy_and_sanitized_cli(self):
        source = TOOL.read_text(encoding="utf-8")
        for token in ("import socket", "import requests", "import urllib", "import subprocess", "os.system", "idf.py", "esptool", "serial.Serial", "git clone"):
            self.assertNotIn(token, source)
        serialized = json.dumps(self.evidence, sort_keys=True)
        for token in (".codex-remote-attachments", "\\Users\\", "D:\\", "exif_data", "latitude=", "longitude="):
            self.assertNotIn(token, serialized)
        tracked = subprocess.run(["git", "ls-files", "tests/benchmarks/crypto"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.lower()
        self.assertNotIn("ot-103", "\n".join(line for line in tracked.splitlines() if line.endswith((".jpg", ".jpeg", ".pdf"))))
        result = subprocess.run([sys.executable, str(TOOL), str(ART), "--evidence", r"Z:\restricted\private.json"], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout, result.stderr.strip()), (2, "", "OTRTPA0 validation failed"))
        self.assertNotIn("restricted", result.stderr)


if __name__ == "__main__":
    unittest.main()
