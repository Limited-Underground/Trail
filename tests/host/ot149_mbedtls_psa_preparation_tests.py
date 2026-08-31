#!/usr/bin/env python3
"""Focused integrity checks for the OT-149 host-only preparation record."""

from __future__ import annotations

import copy
import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HISTORICAL_COMMON_CONFIG_BYTES = 1409
HISTORICAL_COMMON_CONFIG_SHA256 = "a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb"
HISTORICAL_TOP_CMAKE = {
    "tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/CMakeLists.txt",
}
HISTORICAL_TOP_CMAKE_BYTES = 1528
HISTORICAL_TOP_CMAKE_SHA256 = (
    "be533296c54c2a0d91c6f0c1c5c8f10197b96886fb5d6cac04e31ae0344474e2"
)
RECORD = (
    ROOT
    / "tests/benchmarks/crypto"
    / "OT-149-OT005-MBEDTLS-PSA-TARGET-PREPARATION-V0.json"
)
OPERATIONS = [
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
]
FALSE_AUTHORITY = {
    "matched_resource_execution_authorized",
    "benchmark_execution_authorized",
    "device_access_authorized",
    "flash_authorized",
    "radio_transmit_authorized",
    "candidate_selection_authorized",
    "score_credit_added",
}
FALSE_CLAIMS = {
    "benchmark_executed",
    "matched_resource_result_admitted",
    "hardware_or_device_accessed",
    "radio_used",
    "candidate_selected",
    "phase_two_complete",
    "score_credit_added",
}


def canonical_sha256(value: object) -> str:
    raw = json.dumps(
        value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


class Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = json.loads(RECORD.read_text(encoding="utf-8"))

    def test_identity_scope_and_false_claims_are_exact(self) -> None:
        self.assertEqual(
            (
                self.value["schema"],
                self.value["version"],
                self.value["evidence_id"],
                self.value["status"],
            ),
            (
                "OT149MPTP0",
                0,
                "OT-149-OT005-MBEDTLS-PSA-TARGET-PREPARATION-V0",
                "host_target_compile_validated_resource_successor_frozen_unexecuted",
            ),
        )
        self.assertEqual(self.value["target"]["operation_order"], OPERATIONS)
        self.assertEqual(self.value["target"]["exact_result_frame_count"], 1015)
        self.assertEqual(self.value["target"]["cold_repetitions_per_operation"], 100)
        self.assertEqual(self.value["target"]["warm_repetitions_per_operation"], 100)
        self.assertEqual(
            set(self.value["target"]["unavailable_operations"]),
            {"ed25519_sign", "ed25519_verify", "noise_xk_handshake"},
        )
        for field in FALSE_AUTHORITY:
            self.assertIs(self.value["authority"][field], False)
        for field in FALSE_CLAIMS:
            self.assertIs(self.value["claims"][field], False)

    def test_bound_repo_inputs_match_exact_bytes(self) -> None:
        entries = list(self.value["bindings"].values())
        entries += self.value["configuration_lineage"]["inputs"]
        entries += self.value["source_inputs"]
        seen: set[str] = set()
        for entry in entries:
            path = entry["path"]
            self.assertNotIn(path, seen)
            seen.add(path)
            if path == "firmware/targets/heltec_v4_bench/sdkconfig.defaults":
                self.assertEqual(entry["bytes"], HISTORICAL_COMMON_CONFIG_BYTES)
                self.assertEqual(entry["sha256"], HISTORICAL_COMMON_CONFIG_SHA256)
                continue
            if path in HISTORICAL_TOP_CMAKE:
                self.assertEqual(entry["bytes"], HISTORICAL_TOP_CMAKE_BYTES)
                self.assertEqual(entry["sha256"], HISTORICAL_TOP_CMAKE_SHA256)
                continue
            raw = (ROOT / path).read_bytes()
            if "bytes" in entry:
                self.assertEqual(len(raw), entry["bytes"], path)
            expected = entry.get("raw_sha256", entry.get("sha256"))
            self.assertEqual(hashlib.sha256(raw).hexdigest(), expected, path)

    def test_json_parent_canonical_hashes_match(self) -> None:
        for name in (
            "benchmark_plan",
            "mbedtls_api_config",
            "phase_two_reconciliation",
            "resource_successor",
        ):
            entry = self.value["bindings"][name]
            parsed = json.loads((ROOT / entry["path"]).read_text(encoding="utf-8"))
            self.assertEqual(canonical_sha256(parsed), entry["canonical_sha256"], name)

    def test_configuration_is_a_bound_successor_not_false_baseline_identity(self) -> None:
        config = self.value["configuration_lineage"]
        self.assertEqual(
            config["accepted_api_config_baseline_sha256"],
            "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686",
        )
        self.assertEqual(config["generated_successor_bytes"], 106890)
        self.assertEqual(
            config["generated_successor_sha256"],
            "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e",
        )
        self.assertNotEqual(
            config["accepted_api_config_baseline_sha256"],
            config["generated_successor_sha256"],
        )
        self.assertIs(config["candidate_control_byte_identical"], True)
        self.assertEqual(len(config["quiet_successor_delta"]), 7)

    def test_candidate_control_compile_receipts_are_bounded(self) -> None:
        build = self.value["compile_validation"]
        self.assertIs(build["fresh_candidate_and_control_builds"], True)
        self.assertEqual(build["full_esp_idf_target_count_each"], 1199)
        self.assertEqual(build["compiler_warning_count_each"], 0)
        self.assertEqual(
            build["candidate"]["generated_sdkconfig"],
            build["control"]["generated_sdkconfig"],
        )
        self.assertEqual(
            build["candidate"]["generated_sdkconfig"]["sha256"],
            self.value["configuration_lineage"]["generated_successor_sha256"],
        )
        self.assertNotEqual(
            build["candidate"]["application_bin"]["sha256"],
            build["control"]["application_bin"]["sha256"],
        )
        self.assertIs(build["artifact_paths_publicly_recorded"], False)
        self.assertIs(build["matched_size_reports_generated"], False)
        self.assertIs(build["resource_delta_calculated"], False)

    def test_mutated_source_or_claim_is_detectable(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["source_inputs"][0]["sha256"] = "0" * 64
        raw = (ROOT / changed["source_inputs"][0]["path"]).read_bytes()
        self.assertNotEqual(
            hashlib.sha256(raw).hexdigest(), changed["source_inputs"][0]["sha256"]
        )
        changed = copy.deepcopy(self.value)
        changed["claims"]["phase_two_complete"] = True
        self.assertIsNot(changed["claims"]["phase_two_complete"], False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
