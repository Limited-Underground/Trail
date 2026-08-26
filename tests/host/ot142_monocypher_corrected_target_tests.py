#!/usr/bin/env python3
"""Focused checks for the OT-142 corrected Monocypher benchmark target."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASE = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot121_candidate_benchmarks"
)
FROZEN = BASE / "monocypher_ot129"
QUIET = BASE / "monocypher_ot139_quiet"
CORRECTED = BASE / "monocypher_ot142_corrected"

WRONG_SEED_TAIL = """        0x44, 0x9c, 0x56, 0x97, 0xb3, 0x26, 0x91, 0x97,
        0x03, 0xba, 0xc0, 0x31, 0xca, 0xe7, 0xf6, 0x60"""
RFC8032_SEED_TAIL = """        0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
        0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60"""


class Tests(unittest.TestCase):
    def test_successor_diff_is_only_the_rfc_seed_correction(self) -> None:
        frozen = (FROZEN / "main" / "app_main.c").read_text(encoding="utf-8")
        corrected = (CORRECTED / "main" / "app_main.c").read_text(encoding="utf-8")
        self.assertEqual(frozen.count(WRONG_SEED_TAIL), 1)
        self.assertNotIn(RFC8032_SEED_TAIL, frozen)
        self.assertEqual(corrected, frozen.replace(WRONG_SEED_TAIL, RFC8032_SEED_TAIL, 1))

    def test_successor_reuses_control_library_and_quiet_configuration(self) -> None:
        main_cmake = (CORRECTED / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"app_main.c"', main_cmake)
        self.assertIn('"${OT129_MAIN_DIR}/ot129_control_protocol.c"', main_cmake)
        self.assertNotIn('"${OT129_MAIN_DIR}/app_main.c"', main_cmake)
        self.assertEqual(
            (CORRECTED / "sdkconfig.defaults").read_bytes(),
            (QUIET / "sdkconfig.defaults").read_bytes(),
        )
        self.assertEqual(
            (CORRECTED / "partitions.csv").read_bytes(),
            (QUIET / "partitions.csv").read_bytes(),
        )

    def test_target_is_host_only_and_has_no_authority_or_device_tool(self) -> None:
        files = {path.name.lower() for path in CORRECTED.rglob("*") if path.is_file()}
        self.assertNotIn("authority.json", files)
        self.assertFalse(any("adapter" in name or "coordinator" in name for name in files))
        root_cmake = (CORRECTED / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("project(ot142_monocypher_corrected_bench)", root_cmake)


if __name__ == "__main__":
    unittest.main(verbosity=2)
