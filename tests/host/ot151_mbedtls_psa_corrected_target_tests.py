#!/usr/bin/env python3
"""Focused checks for the OT-151 corrected mbedTLS/PSA target."""

from __future__ import annotations

import hashlib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "tests" / "benchmarks" / "crypto" / "esp_idf"
FROZEN = BASE / "ot149_mbedtls_psa" / "candidate"
CORRECTED = BASE / "ot151_mbedtls_psa_corrected"
HISTORICAL_BASE_DEFAULTS = (
    BASE / "ot149_mbedtls_psa" / "common" / "heltec_v4_sdkconfig.defaults"
)
HISTORICAL_BASE_DEFAULTS_BYTES = 1368
HISTORICAL_BASE_DEFAULTS_SHA256 = (
    "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0"
)

FROZEN_LOW_ORDER_CHECK = (
    "if (low_order_status != PSA_ERROR_INVALID_ARGUMENT || output_length != 0U) {"
)
CORRECTED_LOW_ORDER_CHECK = (
    "if (low_order_status != PSA_ERROR_INVALID_ARGUMENT) {"
)


class Tests(unittest.TestCase):
    def test_successor_diff_is_only_the_failure_length_correction(self) -> None:
        frozen = (FROZEN / "main" / "mbedtls_psa_benchmark_api.c").read_text(
            encoding="utf-8"
        )
        corrected = (
            CORRECTED / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")

        self.assertEqual(frozen.count(FROZEN_LOW_ORDER_CHECK), 1)
        self.assertNotIn(CORRECTED_LOW_ORDER_CHECK, frozen)
        self.assertEqual(
            corrected,
            frozen.replace(
                FROZEN_LOW_ORDER_CHECK,
                CORRECTED_LOW_ORDER_CHECK,
                1,
            ),
        )

    def test_status_rejection_remains_mandatory_and_length_contract_is_absent(self) -> None:
        corrected = (
            CORRECTED / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")

        self.assertIn(CORRECTED_LOW_ORDER_CHECK, corrected)
        self.assertNotIn(FROZEN_LOW_ORDER_CHECK, corrected)
        self.assertNotIn("low_order_status == PSA_SUCCESS", corrected)

    def test_successor_reuses_frozen_build_inputs_and_shared_harness(self) -> None:
        self.assertEqual(
            (CORRECTED / "main" / "CMakeLists.txt").read_bytes(),
            (FROZEN / "main" / "CMakeLists.txt").read_bytes(),
        )
        self.assertEqual(
            (CORRECTED / "sdkconfig.defaults").read_bytes(),
            (FROZEN / "sdkconfig.defaults").read_bytes(),
        )
        self.assertEqual(
            (CORRECTED / "partitions.csv").read_bytes(),
            (FROZEN / "partitions.csv").read_bytes(),
        )

        root_cmake = (CORRECTED / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("project(ot151_mbedtls_psa_corrected_bench)", root_cmake)
        self.assertIn(
            "get_filename_component(OT149_ROOT "
            '"${CMAKE_CURRENT_LIST_DIR}/../ot149_mbedtls_psa" ABSOLUTE)',
            root_cmake,
        )
        self.assertIn(
            "${OT149_ROOT}/common/heltec_v4_sdkconfig.defaults", root_cmake
        )
        self.assertNotIn(
            "firmware/targets/heltec_v4_bench/sdkconfig.defaults", root_cmake
        )
        main_cmake = (CORRECTED / "main" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn('"${OT149_ROOT}/common/app_main.c"', main_cmake)

    def test_successor_pins_the_exact_ot149_era_base_defaults(self) -> None:
        raw = HISTORICAL_BASE_DEFAULTS.read_bytes()
        self.assertEqual(len(raw), HISTORICAL_BASE_DEFAULTS_BYTES)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), HISTORICAL_BASE_DEFAULTS_SHA256)
        defaults = raw.decode("utf-8")
        self.assertIn("CONFIG_BT_NIMBLE_NVS_PERSIST=n", defaults)
        self.assertNotIn("CONFIG_BT_NIMBLE_NVS_PERSIST=y", defaults)
        self.assertNotIn("CONFIG_BT_NIMBLE_MAX_BONDS=2", defaults)

    def test_successor_is_host_only_and_has_no_authority_or_device_tool(self) -> None:
        files = {path.name.lower() for path in CORRECTED.rglob("*") if path.is_file()}
        self.assertNotIn("authority.json", files)
        self.assertFalse(any("adapter" in name or "coordinator" in name for name in files))


if __name__ == "__main__":
    unittest.main(verbosity=2)
