#!/usr/bin/env python3
"""Host-only integrity tests for OT-123 Monocypher comparison preparation."""
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HISTORICAL_COMMON_CONFIG_SHA256 = "a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb"
CURRENT_COMMON_CONFIG_SHA256 = "9186abaa6bd99429bb6d7d32f52f772b02dc122145438dc1547d2b94b948fe4a"
CRYPTO = ROOT / "tests/benchmarks/crypto"
PREPARATION = CRYPTO / "OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json"
CONTRACT = CRYPTO / "OT-123-OT005-MATCHED-RESOURCE-ACCOUNTING-CONTRACT-V0.json"
RECIPE = CRYPTO / "OT-123-OT005-MONOCYPHER-BUILD-RECIPE-V0.json"
PRIVATE = re.compile(r"[A-Za-z]:\\|/(?:Users|home)/|\\Users\\|\bCOM\d+\b|(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", re.I)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Ot123MonocypherPreparationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.preparation = json.loads(PREPARATION.read_text(encoding="utf-8"))
        self.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        self.recipe = json.loads(RECIPE.read_text(encoding="utf-8"))

    def test_identity_scope_and_privacy_are_exact(self) -> None:
        self.assertEqual(self.preparation["schema"], "OTMCP0")
        self.assertEqual(self.preparation["version"], 0)
        self.assertEqual(self.preparation["candidate_id"], "monocypher")
        self.assertEqual(self.preparation["candidate_role"], "comparison")
        self.assertFalse(self.preparation["selection_eligible"])
        self.assertIsNone(PRIVATE.search(PREPARATION.read_text(encoding="utf-8")))
        self.assertIsNone(PRIVATE.search(CONTRACT.read_text(encoding="utf-8")))

    def test_exact_five_of_eight_boundary_remains_nonselectable(self) -> None:
        self.assertEqual(self.preparation["operations"], [
            "ed25519_sign", "ed25519_verify", "x25519",
            "chacha20poly1305_encrypt", "chacha20poly1305_decrypt",
        ])
        self.assertEqual(self.preparation["unavailable_operations"], [
            "sha256", "hkdf_sha256", "noise_xk_handshake",
        ])
        claims = self.preparation["claims"]
        self.assertTrue(claims["candidate_harness_prepared"])
        self.assertTrue(claims["candidate_build_reproduced_twice"])
        for key in (
            "matched_resource_control_prepared", "resource_delta_admitted",
            "benchmark_executed", "hardware_accessed", "radio_used",
            "candidate_selected", "phase_two_complete", "score_credit_added",
        ):
            self.assertFalse(claims[key], key)

    def test_builds_are_fresh_zero_warning_and_bit_identical(self) -> None:
        builds = self.preparation["candidate_builds"]
        self.assertEqual([item["run"] for item in builds], ["A", "B"])
        for item in builds:
            self.assertTrue(item["initial_build_directory_absent"])
            self.assertEqual(item["compiler_warnings"], 0)
        normalized = [
            {
                k: v for k, v in item.items()
                if k not in {"run", "raw_build_log_sha256"}
            }
            for item in builds
        ]
        self.assertEqual(normalized[0], normalized[1])
        self.assertNotEqual(
            builds[0]["raw_build_log_sha256"],
            builds[1]["raw_build_log_sha256"],
        )
        for item in builds:
            self.assertRegex(item["raw_build_log_sha256"], r"^[0-9a-f]{64}$")
            self.assertEqual(
                item["normalized_receipt_sha256"],
                "83b5a21d7f9fa0b00cbca6c6fa3934b676a8bf6fdd77ee693bc8944f9f8327d9",
            )
        self.assertEqual(builds[0]["application_bin"], {
            "bytes": 186640,
            "sha256": "5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64",
        })
        self.assertEqual(builds[0]["generated_sdkconfig"]["sha256"],
                         "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f")

    def test_official_size_report_is_hash_bound_and_recomputed(self) -> None:
        record = self.preparation["candidate_size_report"]
        report_path = ROOT / record["path"]
        self.assertEqual(sha(report_path), record["raw_sha256"])
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(report["version"], record["format_version"])
        self.assertEqual(report["total_size"], record["linked_flash_candidate_bytes"])
        diram = next(item for item in report["layout"] if item["name"] == "DIRAM")
        self.assertEqual(diram["parts"][".data"]["size"], record["diram_data_bytes"])
        self.assertEqual(diram["parts"][".bss"]["size"], record["diram_bss_bytes"])
        tls = sum(
            item["parts"].get(".tdata", {}).get("size", 0)
            + item["parts"].get(".tbss", {}).get("size", 0)
            for item in report["layout"]
        )
        noinit = diram["parts"].get(".noinit", {}).get("size", 0)
        self.assertEqual(noinit, record["diram_noinit_bytes"])
        self.assertEqual(tls, record["tls_tdata_tbss_bytes"])
        self.assertEqual(
            record["diram_data_bytes"] + record["diram_bss_bytes"] + noinit + tls,
            record["static_ram_candidate_bytes"],
        )

    def test_matched_accounting_contract_rejects_shortcuts(self) -> None:
        self.assertEqual(self.contract["schema"], "OTMRAC0")
        self.assertEqual(self.contract["status"], "frozen_unexecuted")
        rules = self.contract["matched_build_rules"]
        self.assertEqual(rules["fresh_initially_absent_builds_per_side"], 2)
        self.assertFalse(rules["ccache_allowed"])
        self.assertFalse(rules["component_manager_network_allowed"])
        self.assertTrue(rules["a_b_artifact_and_json2_equality_required"])
        formulas = self.contract["formulas"]
        self.assertTrue(formulas["deltas_are_signed"])
        self.assertEqual(formulas["linked_flash_delta_bytes"],
                         "linked_flash_candidate_bytes - linked_flash_control_bytes")
        self.assertEqual(formulas["static_ram_delta_bytes"],
                         "static_ram_candidate_bytes - static_ram_control_bytes")
        self.assertIn(".noinit", formulas["static_ram_candidate_bytes"])
        self.assertIn(".tdata", formulas["static_ram_candidate_bytes"])
        self.assertIn(".tbss", formulas["static_ram_candidate_bytes"])
        self.assertTrue(formulas["missing_named_parts_count_as_zero"])
        self.assertTrue(formulas["duplicate_named_tls_parts_rejected"])
        validator = self.contract["result_validator_transition"]
        self.assertFalse(validator["current_validator_can_admit_this_signed_delta_contract"])
        self.assertTrue(validator["successor_schema_and_validator_required"])
        self.assertIn("zero or negative", validator["successor_requirement"])
        rejected = set(self.contract["fail_closed_rejections"])
        self.assertIn("application_bin_file_length_as_linked_flash", rejected)
        self.assertIn("restored_trail_or_ot093_full_product_as_control", rejected)
        self.assertIn("forced_positive_or_absolute_valued_delta", rejected)
        self.assertIn("missing_noinit_tdata_or_tbss_accounting", rejected)
        self.assertIn("old_validator_used_for_signed_delta_admission", rejected)
        self.assertFalse(self.contract["claims"]["matched_control_built"])
        self.assertFalse(self.contract["claims"]["resource_delta_admitted"])

    def test_repository_bindings_and_runner_pin_are_exact(self) -> None:
        bindings = self.preparation["bindings"]
        paths = {
            "matched_resource_contract_raw_sha256": CONTRACT,
            "build_recipe_raw_sha256": RECIPE,
            "benchmark_plan_raw_sha256": CRYPTO / "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json",
            "phase_two_authority_raw_sha256": CRYPTO / "OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json",
            "benchmark_source_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/monocypher/main/app_main.c",
            "benchmark_top_cmake_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/monocypher/CMakeLists.txt",
            "benchmark_main_cmake_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/monocypher/main/CMakeLists.txt",
            "partition_raw_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/monocypher/partitions.csv",
            "reproducible_defaults_sha256": CRYPTO / "esp_idf/ot120_candidate_builds/reproducible.defaults",
            "continuation_parent_raw_sha256": CRYPTO / "OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json",
            "shared_frame_header_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h",
            "adapter_sha256": CRYPTO / "adapters/monocypher_api_v0/monocypher_benchmark_api.c",
            "monocypher_core_sha256": CRYPTO / "monocypher/4.0.3/source/src/monocypher.c",
            "monocypher_ed25519_sha256": CRYPTO / "monocypher/4.0.3/source/src/optional/monocypher-ed25519.c",
            "frame_parser_sha256": ROOT / "tools/ot123_monocypher_frames.py",
            "frame_schema_sha256": CRYPTO / "esp_idf/ot121_candidate_benchmarks/monocypher-result-frame.schema.json",
            "runner_sha256": ROOT / "tools/ot123_monocypher_runner.py",
        }
        for key, path in paths.items():
            self.assertEqual(sha(path), bindings[key], key)
        self.assertEqual(
            bindings["common_sdkconfig_defaults_sha256"],
            HISTORICAL_COMMON_CONFIG_SHA256,
        )
        self.assertEqual(
            sha(ROOT / "firmware/targets/heltec_v4_bench/sdkconfig.defaults"),
            CURRENT_COMMON_CONFIG_SHA256,
        )
        runner = (ROOT / "tools/ot123_monocypher_runner.py").read_text(encoding="utf-8")
        self.assertIn('BENCHMARK_SHA256 = "5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64"', runner)
        self.assertIn("BENCHMARK_BYTES = 186_640", runner)
        self.assertIn("verify_application(", runner)
        self.assertIn('"read-flash"', runner)
        self.assertNotIn('parser.add_argument("--benchmark-sha256"', runner)
        self.assertIn('"--no-stub"', runner)
        self.assertIn('"0x10000"', runner)

    def test_build_recipe_is_reusable_and_path_independent(self) -> None:
        self.assertEqual(self.recipe["schema"], "OTMBR0")
        self.assertEqual(self.recipe["candidate_id"], "monocypher")
        self.assertEqual(self.recipe["candidate_role"], "comparison")
        self.assertFalse(self.recipe["selection_eligible"])
        self.assertEqual(self.recipe["execution"]["independent_runs"], ["A", "B"])
        self.assertTrue(self.recipe["execution"]["output_root_must_be_initially_absent"])
        self.assertFalse(self.recipe["execution"]["ccache_allowed"])
        self.assertEqual(self.recipe["execution"]["idf_component_manager"], "0")
        self.assertEqual(
            self.recipe["application_name"],
            "ot123_monocypher_candidate_bench",
        )
        expected_inputs = {
            "firmware/targets/heltec_v4_bench/sdkconfig.defaults",
            "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.c",
            "tests/benchmarks/crypto/adapters/monocypher_api_v0/monocypher_benchmark_api.h",
            "tests/benchmarks/crypto/esp_idf/ot120_candidate_builds/reproducible.defaults",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/CMakeLists.txt",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/main/CMakeLists.txt",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/main/app_main.c",
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/partitions.csv",
            "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.c",
            "tests/benchmarks/crypto/monocypher/4.0.3/source/src/monocypher.h",
            "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.c",
            "tests/benchmarks/crypto/monocypher/4.0.3/source/src/optional/monocypher-ed25519.h",
        }
        self.assertEqual(
            {item["path"] for item in self.recipe["inputs"]}, expected_inputs
        )
        for item in self.recipe["inputs"]:
            if item["path"] == "firmware/targets/heltec_v4_bench/sdkconfig.defaults":
                self.assertEqual(item["raw_sha256"], HISTORICAL_COMMON_CONFIG_SHA256)
                self.assertEqual(sha(ROOT / item["path"]), CURRENT_COMMON_CONFIG_SHA256)
            else:
                self.assertEqual(sha(ROOT / item["path"]), item["raw_sha256"])
        public_text = RECIPE.read_text(encoding="utf-8")
        self.assertIsNone(PRIVATE.search(public_text))
        self.assertNotIn("run-a", public_text.lower())
        self.assertNotIn("run-b", public_text.lower())

    def test_preparation_does_not_fabricate_missing_control_values(self) -> None:
        control = self.preparation["matched_control"]
        self.assertEqual(control["status"], "not_built")
        for key, value in control.items():
            if key != "status":
                self.assertIsNone(value, key)
        execution = self.preparation["execution_preparation"]
        self.assertEqual(execution["expected_frame_count"], 1014)
        self.assertEqual(execution["application_only_offset"], 65536)
        self.assertTrue(execution["runner_pins_benchmark_digest"])
        self.assertFalse(execution["caller_supplied_benchmark_digest_allowed"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
