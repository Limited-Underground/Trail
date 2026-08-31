#!/usr/bin/env python3
"""Static contract checks for the host-only OT-149 mbedTLS/PSA targets."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest


REPO = Path(__file__).resolve().parents[2]
ROOT = (
    REPO
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot149_mbedtls_psa"
)
COMMON = ROOT / "common"
HISTORICAL_BASE_DEFAULTS = COMMON / "heltec_v4_sdkconfig.defaults"
HISTORICAL_BASE_DEFAULTS_BYTES = 1368
HISTORICAL_BASE_DEFAULTS_SHA256 = (
    "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0"
)
CANDIDATE = ROOT / "candidate"
CONTROL = ROOT / "control"
SHARED = (
    REPO
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot121_candidate_benchmarks"
)
OPERATIONS = [
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
]
UNAVAILABLE = ["ed25519_sign", "ed25519_verify", "noise_xk_handshake"]


class Ot149MbedtlsPsaHarnessTests(unittest.TestCase):
    def test_required_candidate_control_and_common_files_exist(self) -> None:
        required = [
            COMMON / "app_main.c",
            COMMON / "ot149_candidate_api.h",
            HISTORICAL_BASE_DEFAULTS,
            CANDIDATE / "CMakeLists.txt",
            CANDIDATE / "sdkconfig.defaults",
            CANDIDATE / "partitions.csv",
            CANDIDATE / "main" / "CMakeLists.txt",
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c",
            CONTROL / "CMakeLists.txt",
            CONTROL / "sdkconfig.defaults",
            CONTROL / "partitions.csv",
            CONTROL / "main" / "CMakeLists.txt",
            CONTROL / "main" / "no_candidate_benchmark_api.c",
        ]
        self.assertTrue(all(path.is_file() for path in required))
        self.assertTrue((SHARED / "include" / "ot121_benchmark_frame.h").is_file())
        self.assertTrue(
            (SHARED / "monocypher_ot129" / "main" / "ot129_control_protocol.c").is_file()
        )

    def test_candidate_and_control_are_structurally_matched(self) -> None:
        candidate_top = (CANDIDATE / "CMakeLists.txt").read_text(encoding="utf-8")
        control_top = (CONTROL / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(
            re.findall(r"project\(([^)]+)\)", candidate_top),
            ["ot149_mbedtls_psa_bench"],
        )
        self.assertEqual(
            re.findall(r"project\(([^)]+)\)", control_top),
            ["ot149_mbedtls_psa_bench"],
        )
        self.assertNotIn("set(COMPONENTS", candidate_top)
        self.assertNotIn("set(COMPONENTS", control_top)
        self.assertIn(
            'set(IDF_TARGET "esp32s3" CACHE STRING "OpenTrail OT-149 target" FORCE)',
            candidate_top,
        )
        self.assertEqual(candidate_top, control_top)
        self.assertIn('set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig"', candidate_top)
        for marker in (
            "${OT149_ROOT}/common/heltec_v4_sdkconfig.defaults",
            "ot120_candidate_builds/reproducible.defaults",
            "ot120_candidate_builds/esp_idf_mbedtls_psa/sdkconfig.overlay",
            "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults",
        ):
            self.assertIn(marker, candidate_top)
        self.assertNotIn(
            "firmware/targets/heltec_v4_bench/sdkconfig.defaults", candidate_top
        )
        self.assertIn("set(OT149_EXPECTED_SDKCONFIG_BYTES 106890)", candidate_top)
        self.assertIn(
            'set(OT149_EXPECTED_SDKCONFIG_SHA256 "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e")',
            candidate_top,
        )
        self.assertIn('file(SIZE "${SDKCONFIG}"', candidate_top)
        self.assertIn('file(SHA256 "${SDKCONFIG}"', candidate_top)
        self.assertIn(
            'message(FATAL_ERROR "OT-149 generated sdkconfig successor binding mismatch")',
            candidate_top,
        )
        self.assertEqual(
            (CANDIDATE / "sdkconfig.defaults").read_bytes(),
            (CONTROL / "sdkconfig.defaults").read_bytes(),
        )
        self.assertEqual(
            (CANDIDATE / "partitions.csv").read_bytes(),
            (CONTROL / "partitions.csv").read_bytes(),
        )
        for cmake in (
            CANDIDATE / "main" / "CMakeLists.txt",
            CONTROL / "main" / "CMakeLists.txt",
        ):
            text = cmake.read_text(encoding="utf-8")
            self.assertIn('"${OT149_ROOT}/common/app_main.c"', text)
            self.assertIn("ot129_control_protocol.c", text)
            self.assertIn("ot121_candidate_benchmarks/include", text)
            self.assertIn("-Wall -Wextra -Werror", text)

    def test_historical_base_defaults_are_byte_pinned_and_source_shaped(self) -> None:
        raw = HISTORICAL_BASE_DEFAULTS.read_bytes()
        self.assertEqual(len(raw), HISTORICAL_BASE_DEFAULTS_BYTES)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), HISTORICAL_BASE_DEFAULTS_SHA256)
        self.assertFalse(raw.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r", raw)
        self.assertTrue(raw.endswith(b"\n"))

        defaults = raw.decode("utf-8")
        for marker in (
            "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
            "CONFIG_BT_NIMBLE_SECURITY_ENABLE=y",
            "CONFIG_BT_NIMBLE_SM_SC=y",
            "CONFIG_BT_NIMBLE_SM_SC_ONLY=1",
            "CONFIG_BT_NIMBLE_NVS_PERSIST=n",
        ):
            self.assertEqual(defaults.count(marker), 1, marker)
        for later_active_target_marker in (
            "CONFIG_BT_NIMBLE_MAX_BONDS=2",
            "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
        ):
            self.assertNotIn(later_active_target_marker, defaults)

    def test_only_candidate_links_or_references_mbedtls_psa(self) -> None:
        candidate_cmake = (CANDIDATE / "main" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        control_cmake = (CONTROL / "main" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        candidate_source = (
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")
        control_source = (
            CONTROL / "main" / "no_candidate_benchmark_api.c"
        ).read_text(encoding="utf-8")
        self.assertIn("PRIV_REQUIRES mbedtls", candidate_cmake)
        self.assertNotIn("PRIV_REQUIRES mbedtls", control_cmake)
        self.assertIn('#include "psa/crypto.h"', candidate_source)
        self.assertIn('#include "mbedtls/platform_util.h"', candidate_source)
        for marker in ('"psa/', 'psa_', 'mbedtls_', "PSA_"):
            self.assertNotIn(marker, control_source)

    def test_common_harness_fixes_scope_order_and_measurement_shape(self) -> None:
        source = (COMMON / "app_main.c").read_text(encoding="utf-8")
        self.assertIn('#define OT121_CANDIDATE_ID "esp_idf_mbedtls_psa"', source)
        self.assertIn("#define OT121_LOCAL_OPERATIONS_REQUIRED 5U", source)
        operation_positions = [source.index(f'{{ "{name}",') for name in OPERATIONS]
        self.assertEqual(operation_positions, sorted(operation_positions))
        for operation in UNAVAILABLE:
            self.assertNotIn(f'{{ "{operation}",', source)
        self.assertIn('ot121_frame_gate("psa_crypto_init", initialized)', source)
        self.assertIn(
            'ot121_frame_gate("primitive_vectors_and_negative_cases", vectors_passed)',
            source,
        )
        self.assertIn("#define OT149_COLD_SWEEP_BYTES (32U * 1024U)", source)
        self.assertIn("#define OT149_BENCHMARK_TASK_STACK_BYTES (8U * 1024U)", source)
        self.assertIn('"cold", OT121_COLD_REPETITIONS, true', source)
        self.assertIn('"warm", OT121_WARM_REPETITIONS, false', source)
        self.assertIn("if (!conditioned && operation->invoke() != 0)", source)
        self.assertIn("heap_caps_monitor_local_minimum_free_size_start", source)
        self.assertIn("uxTaskGetStackHighWaterMark2", source)
        self.assertIn("ot121_frame_runtime_resources", source)
        self.assertIn("ot121_frame_local_complete", source)
        self.assertNotIn("phase2_complete=true", source)
        self.assertNotRegex(source, r"(?i)\b(?:lora|sx126|radio_transmit)\b")

    def test_common_harness_reuses_proven_start_ready_direct_usb_path(self) -> None:
        source = (COMMON / "app_main.c").read_text(encoding="utf-8")
        self.assertIn('#include "ot129_control_protocol.h"', source)
        self.assertIn("ot129_control_feed", source)
        self.assertIn("OT129_CONTROL_READY", source)
        self.assertIn("usb_serial_jtag_driver_install", source)
        self.assertIn("usb_serial_jtag_read_bytes", source)
        self.assertIn("usb_serial_jtag_write_bytes", source)
        self.assertIn("usb_serial_jtag_wait_tx_done", source)
        self.assertNotIn("usb_serial_jtag_vfs", source)
        self.assertNotRegex(source, r"(?m)^\s*(?:printf|puts|fwrite)\(")
        self.assertLess(source.index("wait_for_start();"), source.index("ot121_frame_header();"))
        self.assertLess(
            source.index("ot121_frame_header();"),
            source.index("ot149_candidate_initialize();"),
        )

    def test_candidate_uses_exact_five_admitted_psa_operations(self) -> None:
        source = (
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")
        required = [
            "psa_crypto_init",
            "psa_raw_key_agreement",
            "PSA_ALG_ECDH",
            "PSA_ECC_FAMILY_MONTGOMERY",
            "psa_hash_compute",
            "PSA_ALG_SHA_256",
            "psa_key_derivation_setup",
            "psa_key_derivation_input_bytes",
            "psa_key_derivation_input_key",
            "psa_key_derivation_output_bytes",
            "psa_key_derivation_abort",
            "PSA_ALG_HKDF(PSA_ALG_SHA_256)",
            "psa_aead_encrypt",
            "psa_aead_decrypt",
            "PSA_KEY_TYPE_CHACHA20",
            "PSA_ALG_CHACHA20_POLY1305",
        ]
        for marker in required:
            self.assertIn(marker, source)
        for marker in ("psa_sign", "psa_verify", "PURE_EDDSA", "noise_xk"):
            self.assertNotIn(marker, source)
        for function in (
            "ot149_candidate_x25519",
            "ot149_candidate_sha256",
            "ot149_candidate_hkdf_sha256",
            "ot149_candidate_chacha20poly1305_encrypt",
            "ot149_candidate_chacha20poly1305_decrypt",
        ):
            self.assertIn(f"__attribute__((noinline)) int {function}", source)

    def test_candidate_pins_vectors_negative_cases_and_cleanup(self) -> None:
        source = (
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")
        for marker in (
            "k_x25519_private",
            "k_x25519_peer_public",
            "k_x25519_shared",
            "k_sha256_abc",
            "k_hkdf_okm",
            "k_aead_ciphertext",
            "low_order",
            "tampered",
            "psa_destroy_key",
            "psa_reset_key_attributes",
            "mbedtls_platform_zeroize",
        ):
            self.assertIn(marker, source)
        self.assertIn(
            "low_order_status != PSA_ERROR_INVALID_ARGUMENT || output_length != 0U",
            source,
        )
        self.assertIn("tampered[sizeof(tampered) - 1U] ^= 0x01U", source)
        self.assertIn("tampered_status != PSA_ERROR_INVALID_SIGNATURE", source)
        self.assertGreaterEqual(source.count("psa_destroy_key"), 3)
        self.assertGreaterEqual(source.count("mbedtls_platform_zeroize"), 10)

    def test_timed_wrappers_exclude_exact_kat_comparisons(self) -> None:
        source = (
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")
        boundaries = [
            ("ot149_candidate_x25519", "ot149_candidate_sha256"),
            ("ot149_candidate_hkdf_sha256", "ot149_candidate_chacha20poly1305_encrypt"),
            (
                "ot149_candidate_chacha20poly1305_decrypt",
                "ot149_candidate_vectors_and_negative_cases",
            ),
        ]
        for function, next_function in boundaries:
            body = source[
                source.index(f"int {function}(void)") : source.index(
                    f"{next_function}(void)"
                )
            ]
            self.assertNotIn("memcmp(", body, function)

        gate = source[
            source.index("bool ot149_candidate_vectors_and_negative_cases(void)") :
            source.index("bool ot149_candidate_cleanup(void)")
        ]
        self.assertIn("memcmp(shared, k_x25519_shared, sizeof(shared))", gate)
        self.assertIn("memcmp(hkdf_output, k_hkdf_okm, sizeof(hkdf_output))", gate)
        self.assertIn("memcmp(plaintext, k_aead_plaintext, sizeof(plaintext))", gate)

    def test_cleanup_status_reaches_the_terminal_result_without_abort(self) -> None:
        header = (COMMON / "ot149_candidate_api.h").read_text(encoding="utf-8")
        app = (COMMON / "app_main.c").read_text(encoding="utf-8")
        candidate = (
            CANDIDATE / "main" / "mbedtls_psa_benchmark_api.c"
        ).read_text(encoding="utf-8")
        control = (
            CONTROL / "main" / "no_candidate_benchmark_api.c"
        ).read_text(encoding="utf-8")
        self.assertIn("bool ot149_candidate_cleanup(void);", header)
        self.assertIn("bool cleanup_passed = ot149_candidate_cleanup();", app)
        self.assertIn("passed = passed && cleanup_passed;", app)
        self.assertNotIn(
            "ESP_ERROR_CHECK(heap_caps_monitor_local_minimum_free_size_stop())",
            app,
        )
        self.assertLess(
            app.index("heap_caps_monitor_local_minimum_free_size_stop()"),
            app.index("ot149_candidate_cleanup()"),
        )
        self.assertLess(
            app.index("passed = passed && cleanup_passed;"),
            app.index("ot121_frame_local_complete(completed, passed);"),
        )
        self.assertIn("bool ot149_candidate_cleanup(void)", candidate)
        self.assertIn("bool passed = true;", candidate)
        self.assertGreaterEqual(
            candidate.count("== PSA_SUCCESS && passed"), 3
        )
        self.assertIn("return passed;", candidate)
        self.assertIn("bool ot149_candidate_cleanup(void)", control)
        self.assertIn("return true;", control)

    def test_defaults_are_the_narrow_quiet_successor_delta(self) -> None:
        defaults = (CANDIDATE / "sdkconfig.defaults").read_text(encoding="utf-8")
        required = [
            "CONFIG_ESP_CONSOLE_NONE=y",
            "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",
            "CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y",
            "CONFIG_LOG_DEFAULT_LEVEL_NONE=y",
            "CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y",
            "CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y",
            "CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y",
        ]
        for marker in required:
            self.assertIn(marker, defaults)
        for forbidden in (
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
            "CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y",
            "CONFIG_LOG_DEFAULT_LEVEL_INFO=y",
            "CONFIG_MBEDTLS_CHACHA20_C=y",
            "CONFIG_MBEDTLS_CHACHAPOLY_C=y",
            "CONFIG_SPIRAM=y",
        ):
            self.assertNotIn(forbidden, defaults)


if __name__ == "__main__":
    unittest.main(verbosity=2)
