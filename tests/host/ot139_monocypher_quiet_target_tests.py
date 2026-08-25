#!/usr/bin/env python3
"""Focused adversarial checks for the OT-139 host-only quiet target."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot139_monocypher_quiet_target_evidence.py"
SPEC = importlib.util.spec_from_file_location("ot139_evidence", MODULE_PATH)
assert SPEC and SPEC.loader
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


class Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = evidence.load_evidence()

    def assert_rejected(self, value: dict) -> None:
        with self.assertRaises(evidence.ValidationError):
            evidence.validate(value)

    def test_canonical_evidence(self) -> None:
        result = evidence.validate(self.value)
        self.assertTrue(result["artifact_tuples_identical"])
        self.assertFalse(result["hardware_accessed"])
        self.assertFalse(result["execution_authority_created"])
        self.assertFalse(self.value["configuration"]["bluetooth_enabled"])
        self.assertFalse(self.value["configuration"]["wifi_enabled"])
        self.assertEqual(
            result["sdkconfig_sha256"],
            "5807fe7fc6d4ef3325f06099674f07080660eabd20e0b078225247605c814817",
        )

    def test_primary_or_secondary_console_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["configuration"]["primary_console"] = "usb_serial_jtag"
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["configuration"]["secondary_console"] = "usb_serial_jtag"
        self.assert_rejected(changed)

    def test_nonzero_logs_or_rom_suppression_rejected(self) -> None:
        for field, replacement in (
            ("bootloader_log_level", "info"),
            ("default_application_log_level", "info"),
            ("rom_log_policy", "always_off"),
        ):
            changed = copy.deepcopy(self.value)
            changed["configuration"][field] = replacement
            self.assert_rejected(changed)

    def test_direct_driver_disable_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["configuration"]["direct_usb_serial_jtag_driver_enabled"] = False
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["configuration"]["bluetooth_enabled"] = True
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["configuration"]["wifi_enabled"] = True
        self.assert_rejected(changed)

    def test_artifact_digest_or_pair_mismatch_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["canonical_artifact_tuple"][0]["sha256"] = "0" * 64
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["runs"][1]["artifacts"][2]["bytes"] += 1
        self.assert_rejected(changed)

    def test_reused_directory_warning_or_forbidden_claim_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["runs"][0]["initial_build_directory_absent"] = False
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["runs"][0]["compiler_warning_count"] = 1
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["claims"]["execution_authority_created"] = True
        self.assert_rejected(changed)

    def test_source_binding_mutation_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["source_inputs"][0]["sha256"] = "f" * 64
        self.assert_rejected(changed)

    def test_target_reuses_frozen_ot129_sources(self) -> None:
        target = (
            ROOT
            / "tests"
            / "benchmarks"
            / "crypto"
            / "esp_idf"
            / "ot121_candidate_benchmarks"
            / "monocypher_ot139_quiet"
        )
        main_cmake = (target / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("OT129_MAIN_DIR", main_cmake)
        self.assertIn("${OT129_MAIN_DIR}/app_main.c", main_cmake)
        self.assertIn("${OT129_MAIN_DIR}/ot129_control_protocol.c", main_cmake)
        self.assertIn("PRIV_REQUIRES esp_driver_usb_serial_jtag", main_cmake)
        self.assertIn("set(COMPONENTS main)", (target / "CMakeLists.txt").read_text(encoding="utf-8"))
        self.assertFalse((target / "main" / "app_main.c").exists())
        self.assertFalse((target / "main" / "ot129_control_protocol.c").exists())
        self.assertFalse((target / "main" / "ot129_control_protocol.h").exists())

        app_main = target.parent / "monocypher_ot129" / "main" / "app_main.c"
        text = app_main.read_text(encoding="utf-8")
        self.assertIn("usb_serial_jtag_driver_install", text)
        self.assertIn("usb_serial_jtag_read_bytes", text)
        self.assertIn("usb_serial_jtag_wait_tx_done", text)
        self.assertLess(text.index("install_buffered_usb_serial_jtag_protocol();"), text.index("xTaskCreatePinnedToCore("))
        self.assertLess(text.index("ot129_wait_for_start();"), text.index("ot121_frame_header();"))

    def test_frozen_transport_and_source_hashes_unchanged(self) -> None:
        expected = {
            "tools/ot129_monocypher_protocol_runner.py": "f95c5e673a698dc1392de08611abd3cb38f63dec45423ae2100f2f59265e9c9e",
            "tools/ot132_monocypher_protocol_runner.py": "d131286969d82f0ddef8b3051b6d64588042a4d49fafa2aca1d164de617f6a3d",
            "tools/ot135_monocypher_protocol_runner.py": "e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/app_main.c": "fac7a9375a5dba5366215dc0eab0a03a83cfd22fd50a2ac563f1c378cb7aae2b",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c": "d10b9e6769676530c4eacd7e31bbf2192f293f1fa1099138673525bc395bdcdf",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h": "14e2896e43e9a873ffb0fbfc4ec01c371095f39b42a6f82b2544ecf0f7c57e76",
        }
        for relative, digest in expected.items():
            self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), digest)

    def test_build_script_is_bounded_and_host_only(self) -> None:
        script = (ROOT / "tools" / "Build-Ot139MonocypherQuietTarget.ps1").read_text(encoding="utf-8")
        for required in (
            "unless -Execute is supplied",
            "OutputRoot must be initially absent",
            "status --porcelain --untracked-files=all",
            "--no-ccache",
            "IDF_COMPONENT_MANAGER = '0'",
            "artifact tuples differ",
            "execution_authority_created = $false",
            "hardware_accessed = $false",
        ):
            self.assertIn(required, script)
        lowered = script.lower()
        self.assertNotIn("write_flash", lowered)
        self.assertNotIn("erase_flash", lowered)
        self.assertNotIn("serial.tools.list_ports", lowered)


if __name__ == "__main__":
    unittest.main(verbosity=2)
