#!/usr/bin/env python3
"""Static checks for the bounded OT-123 Monocypher comparison harness."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks"
MONOCYPHER = ROOT / "monocypher"
OPERATIONS = [
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
]
SOURCE_HASHES = {
    "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c":
        "e12b800841c6c8347cdf08d05768f2cfbc83ee271fdae7616f8a3b16e4263e59",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c":
        "f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123",
    "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c":
        "ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453",
}


class Ot123MonocypherHarnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = (MONOCYPHER / "main/app_main.c").read_text(encoding="utf-8")
        self.schema = json.loads(
            (ROOT / "monocypher-result-frame.schema.json").read_text(encoding="utf-8")
        )

    def test_exact_files_partition_and_accepted_sources(self) -> None:
        for relative in (
            "CMakeLists.txt", "partitions.csv", "main/CMakeLists.txt", "main/app_main.c"
        ):
            self.assertTrue((MONOCYPHER / relative).is_file(), relative)
        self.assertEqual(
            (MONOCYPHER / "partitions.csv").read_bytes(),
            (REPO / "firmware/targets/heltec_v4_bench/partitions.csv").read_bytes(),
        )
        for relative, expected in SOURCE_HASHES.items():
            self.assertEqual(hashlib.sha256((REPO / relative).read_bytes()).hexdigest(), expected)

    def test_candidate_specific_frame_contract_is_closed(self) -> None:
        self.assertEqual(
            self.schema["$id"],
            "urn:opentrail:benchmark:OTCBXRF2:monocypher",
        )
        self.assertEqual(len(self.schema["oneOf"]), 6)
        for branch in self.schema["oneOf"]:
            self.assertFalse(branch.get("additionalProperties", True))
            self.assertEqual(branch["properties"]["candidate_id"], {"const": "monocypher"})
            if "operations_required" in branch["properties"]:
                self.assertEqual(branch["properties"]["operations_required"], {"const": 5})
        operation_rules = [
            branch["properties"]["operation"]["enum"]
            for branch in self.schema["oneOf"]
            if "operation" in branch["properties"]
        ]
        self.assertEqual(operation_rules, [OPERATIONS, OPERATIONS])
        gate = next(
            branch for branch in self.schema["oneOf"]
            if branch["properties"]["record_kind"].get("const") == "gate"
        )
        self.assertEqual(
            gate["properties"]["gate"]["enum"],
            ["primitive_vectors_and_negative_cases"],
        )
        sample = next(
            branch for branch in self.schema["oneOf"]
            if branch["properties"]["record_kind"].get("const") == "sample"
        )
        self.assertEqual(sample["properties"]["duration_us"]["minimum"], 1)
        self.assertEqual(
            sample["properties"]["duration_us"]["maximum"],
            (1 << 63) - 1,
        )
        summary = next(
            branch for branch in self.schema["oneOf"]
            if branch["properties"]["record_kind"].get("const") == "operation_summary"
        )
        for name in ("min_us", "median_us", "p95_us", "max_us"):
            self.assertEqual(summary["properties"][name]["maximum"], (1 << 63) - 1)
        resources = next(
            branch for branch in self.schema["oneOf"]
            if branch["properties"]["record_kind"].get("const") == "runtime_resources"
        )
        for name in (
            "heap_start_free_bytes", "heap_min_free_bytes",
            "peak_dynamic_ram_bytes",
        ):
            self.assertEqual(resources["properties"][name]["maximum"], (1 << 63) - 1)

    def test_exact_five_operations_without_substitution(self) -> None:
        self.assertIn('#define OT121_CANDIDATE_ID "monocypher"', self.source)
        self.assertIn("#define OT121_LOCAL_OPERATIONS_REQUIRED 5U", self.source)
        for operation in OPERATIONS:
            self.assertEqual(self.source.count(f'{{ "{operation}",'), 1)
        operation_table = self.source[
            self.source.index("static const ot123_operation k_operations[]"):
            self.source.index("_Static_assert", self.source.index("static const ot123_operation k_operations[]"))
        ]
        for unavailable in ("sha256", "hkdf_sha256", "noise_xk_handshake"):
            self.assertNotIn(unavailable, operation_table.lower())
        self.assertIn("ot_monocypher_ed25519_sign", self.source)
        self.assertIn("ot_monocypher_ed25519_verify", self.source)
        self.assertIn("ot_monocypher_x25519", self.source)
        self.assertIn("ot_monocypher_chacha20poly1305_ietf_encrypt", self.source)
        self.assertIn("ot_monocypher_chacha20poly1305_ietf_decrypt", self.source)

    def test_vectors_measurement_and_cleanup_follow_proven_path(self) -> None:
        for marker in (
            "k_x_public_a", "k_x_public_b", "k_x_shared",
            "k_ed_seed", "k_ed_public", "k_ed_signature",
            "k_low_order_scalar", "k_low_order_public",
            "k_aead_key", "k_aead_nonce", "k_aead_ad", "k_aead_plain",
            "k_aead_cipher_and_tag",
            "RFC 8032 Section 7.1, TEST 1.", "RFC 8439 Section 2.8.2",
            "all-zero result is rejected",
            "crypto_ed25519_key_pair", "g_signature[0] ^= 0x01U",
            "tamper_rejected", "scratch_size != 0U",
            "crypto_wipe(g_sign_seed", "crypto_wipe(g_sign_secret",
            "crypto_wipe(g_x_secret_a", "crypto_wipe(g_aead_key",
            "crypto_wipe(g_samples", "crypto_wipe(g_sorted",
            "heap_caps_monitor_local_minimum_free_size_start",
            "uxTaskGetStackHighWaterMark2", "ot121_frame_runtime_resources",
            "memcmp(ed_public, k_ed_public",
            "memcmp(ed_signature, k_ed_signature",
            "any_nonzero(low_order_shared",
            "memcmp(aead_cipher, k_aead_cipher_and_tag",
        ):
            self.assertIn(marker, self.source)
        run_phase = self.source[
            self.source.index("static bool run_phase"):
            self.source.index("static int ot123_discard_log_vprintf")
        ]
        first = run_phase.index("for (unsigned iteration = 0; iteration < repetitions; ++iteration)")
        second = run_phase.index(
            "for (unsigned iteration = 0; iteration < repetitions; ++iteration)", first + 1
        )
        self.assertNotIn("ot121_frame_sample", run_phase[first:second])
        self.assertIn("ot121_frame_sample", run_phase[second:])
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(3000U));", self.source)
        self.assertIn('ot121_frame_gate("primitive_vectors_and_negative_cases"', self.source)
        self.assertIn("ot121_frame_local_complete(completed, passed);", self.source)

    def test_no_radio_identity_random_or_selection_surface(self) -> None:
        combined = "\n".join(
            path.read_text(encoding="utf-8")
            for path in MONOCYPHER.rglob("*") if path.is_file()
        ).lower()
        for forbidden in (
            "esp_wifi", "esp_bt", "nimble", "lora", "sx126", "radio_init",
            "randombytes", "esp_read_mac", "device_id", "winner",
        ):
            self.assertNotIn(forbidden, combined)
        self.assertNotIn("sodium_init", combined)
        self.assertNotIn("dependencies.lock", combined)
        self.assertNotIn("sdkconfig.overlay", combined)


if __name__ == "__main__":
    unittest.main()