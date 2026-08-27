#!/usr/bin/env python3
"""Focused adversarial tests for OTMRAC1/OTMRAR1 matched resources."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import crypto_matched_resource_accounting as accounting  # noqa: E402


CONTRACT_PATH = (
    ROOT
    / "tests/benchmarks/crypto"
    / "OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json"
)
HISTORICAL_REPORT = (
    ROOT
    / "tests/benchmarks/crypto"
    / "OT-123-OT005-MONOCYPHER-CANDIDATE-SIZE-REPORT-V0.json"
)


def digest(label: str) -> str:
    return hashlib.sha256(label.encode("utf-8")).hexdigest()


def size_report(
    linked_flash: int,
    *,
    data: int,
    bss: int,
    noinit: int | None = 0,
    tdata: int | None = 0,
    tbss: int | None = 0,
) -> dict:
    diram_parts: dict[str, dict[str, int]] = {
        ".data": {"size": data},
        ".bss": {"size": bss},
        ".text": {"size": 7},
    }
    if noinit is not None:
        diram_parts[".noinit"] = {"size": noinit}
    flash_parts: dict[str, dict[str, int]] = {".rodata": {"size": 11}}
    if tdata is not None:
        flash_parts[".tdata"] = {"size": tdata}
    if tbss is not None:
        flash_parts[".tbss"] = {"size": tbss}
    return {
        "version": "1.2",
        "total_size": linked_flash,
        "layout": [
            {
                "name": "DIRAM",
                "total": 341_760,
                "used": sum(item["size"] for item in diram_parts.values()),
                "free": 300_000,
                "parts": diram_parts,
            },
            {
                "name": "Flash Data",
                "total": 0,
                "used": sum(item["size"] for item in flash_parts.values()),
                "free": 0,
                "parts": flash_parts,
            },
        ],
    }


class Fixture:
    def __init__(self, directory: str, candidate_report: dict, control_report: dict) -> None:
        self.root = Path(directory)
        evidence = self.root / "tests/benchmarks/crypto/fixture"
        evidence.mkdir(parents=True)
        self.candidate_binding = self._write_report(
            evidence / "candidate-size.json", candidate_report
        )
        self.control_binding = self._write_report(
            evidence / "control-size.json", control_report
        )

    def _write_report(self, path: Path, report: dict) -> dict:
        raw = (json.dumps(report, indent=2) + "\n").encode("utf-8")
        path.write_bytes(raw)
        relative = path.relative_to(self.root).as_posix()
        return {
            "path": relative,
            "bytes": len(raw),
            "raw_sha256": hashlib.sha256(raw).hexdigest(),
        }


class MatchedResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = accounting.load_json(CONTRACT_PATH)

    def metadata(self, candidate: dict) -> dict[str, str]:
        return {
            "benchmark_harness_source": digest("harness"),
            "frame_and_runtime_instrumentation": digest("instrumentation"),
            "generated_sdkconfig": candidate["generated_sdkconfig_sha256"],
            "partition_layout": digest("partition"),
            "idf_target": "esp32s3",
            "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
            "compiler": "xtensa-esp32s3-elf-gcc 15.2.0",
            "cmake": "4.0.3",
            "ninja": "1.12.1",
            "python": "3.14.6",
            "esp_idf_size": "2.3.1",
            "compile_flags": digest("flags"),
            "project_version": "ot149-test-v1",
            "cache_policy": "disabled",
            "component_manager_network_policy": "disabled",
        }

    def build_side(
        self,
        side: str,
        candidate: dict,
        report_binding: dict,
    ) -> dict:
        artifacts = {
            "application_bin": {"bytes": 1000, "sha256": digest(side + ":bin")},
            "application_elf": {"bytes": 2000, "sha256": digest(side + ":elf")},
            "linker_map": {"bytes": 3000, "sha256": digest(side + ":map")},
            "generated_sdkconfig": {
                "bytes": 4000,
                "sha256": candidate["generated_sdkconfig_sha256"],
            },
            "partition_csv": {"bytes": 443, "sha256": digest("partition-csv")},
            "size_report": {
                "bytes": report_binding["bytes"],
                "sha256": report_binding["raw_sha256"],
            },
        }
        receipt = digest(side + ":normalized")
        return {
            "matched_metadata": self.metadata(candidate),
            "linkage_manifest_sha256": digest(side + ":linkage"),
            "runs": [
                {
                    "run": run,
                    "initial_build_directory_absent": True,
                    "compiler_warnings": 0,
                    "raw_build_log_sha256": digest(side + ":log:" + run),
                    "normalized_receipt_sha256": receipt,
                    "artifacts": copy.deepcopy(artifacts),
                }
                for run in ("A", "B")
            ],
        }

    def result(self, fixture: Fixture, candidate_index: int = 0) -> dict:
        candidate = accounting.CANDIDATES[candidate_index]
        candidate_metrics, _ = accounting._report(
            fixture.candidate_binding, fixture.root, "fixture.candidate"
        )
        control_metrics, _ = accounting._report(
            fixture.control_binding, fixture.root, "fixture.control"
        )
        measurements = {
            "linked_flash_candidate_bytes": candidate_metrics["linked_flash_bytes"],
            "linked_flash_control_bytes": control_metrics["linked_flash_bytes"],
            "linked_flash_delta_bytes": (
                candidate_metrics["linked_flash_bytes"]
                - control_metrics["linked_flash_bytes"]
            ),
            "static_ram_candidate_bytes": candidate_metrics["static_ram_bytes"],
            "static_ram_control_bytes": control_metrics["static_ram_bytes"],
            "static_ram_delta_bytes": (
                candidate_metrics["static_ram_bytes"]
                - control_metrics["static_ram_bytes"]
            ),
        }
        raw_contract = CONTRACT_PATH.read_bytes()
        return {
            "schema": "OTMRAR1",
            "version": 1,
            "artifact_kind": "matched_candidate_resource_accounting_result",
            "result_id": "OT-150-OT005-MATCHED-RESOURCE-RESULT-V1",
            "recorded_date": "2026-08-26",
            "status": "matched_resource_result_admitted",
            "contract": {
                "path": accounting.CONTRACT_REPO_PATH,
                "raw_sha256": hashlib.sha256(raw_contract).hexdigest(),
                "canonical_sha256": accounting.canonical_sha256(self.contract),
            },
            "candidate_id": candidate["candidate_id"],
            "candidate_role": candidate["role"],
            "selection_eligible": candidate["selection_eligible"],
            "builds": {
                "candidate": self.build_side(
                    "candidate", candidate, fixture.candidate_binding
                ),
                "control": self.build_side(
                    "control", candidate, fixture.control_binding
                ),
                "only_permitted_difference": accounting.ONLY_DIFFERENCE,
            },
            "reports": {
                "candidate": copy.deepcopy(fixture.candidate_binding),
                "control": copy.deepcopy(fixture.control_binding),
            },
            "measurements": measurements,
            "phase_two_projection": {
                "linked_flash_delta_bytes": measurements[
                    "linked_flash_delta_bytes"
                ],
                "static_ram_bytes": measurements["static_ram_candidate_bytes"],
            },
            "claims": {
                "resource_delta_admitted": True,
                "benchmark_executed": False,
                "hardware_or_device_accessed": False,
                "candidate_selected": False,
                "phase_two_complete": False,
                "score_credit_added": False,
            },
        }

    def evaluate(self, fixture: Fixture, result: dict) -> dict:
        return accounting.validate_result(
            self.contract,
            result,
            root=fixture.root,
            contract_path=CONTRACT_PATH,
        )

    def expect_error(self, fixture: Fixture, result: dict, text: str) -> None:
        with self.assertRaisesRegex(accounting.ContractError, text):
            self.evaluate(fixture, result)

    def test_contract_exact_pins_and_unexecuted_claims(self) -> None:
        info = accounting.validate_contract(self.contract)
        self.assertEqual(info["schema"], "OTMRAC1")
        self.assertEqual(info["version"], 1)
        self.assertEqual(
            info["contract_canonical_sha256"],
            "1c44d3d6f0c0d7c38ce83c51f1b79c75130455f2bbf99fed7edb4ffa1b9efaf2",
        )
        mbedtls = self.contract["candidates"][1]
        self.assertEqual(
            mbedtls["api_config_baseline_sha256"],
            "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686",
        )
        self.assertEqual(
            mbedtls["generated_sdkconfig_sha256"],
            "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e",
        )
        self.assertEqual(
            self.contract["toolchain"]["esp_idf_size_module_sha256"],
            "ba38639de2a4d1f4fa48657e94cd3f250a3744ea8595e81fc74e9da0431a15c9",
        )
        self.assertFalse(self.contract["claims"]["matched_controls_built"])
        self.assertFalse(self.contract["claims"]["resource_delta_admitted"])
        self.assertFalse(self.contract["authority"]["device_access_authorized"])
        changed_rejection = copy.deepcopy(self.contract)
        changed_rejection["fail_closed_rejections"][0] = "weakened"
        with self.assertRaisesRegex(accounting.ContractError, "rejection set drift"):
            accounting.validate_contract(changed_rejection)

    def test_historical_json2_report_recomputes_exact_values(self) -> None:
        report = accounting.load_json(HISTORICAL_REPORT)
        self.assertEqual(
            accounting.parse_size_report(report),
            {"linked_flash_bytes": 186_516, "static_ram_bytes": 50_973},
        )

    def test_missing_named_static_parts_count_as_zero(self) -> None:
        report = size_report(
            100,
            data=10,
            bss=20,
            noinit=None,
            tdata=None,
            tbss=None,
        )
        self.assertEqual(
            accounting.parse_size_report(report),
            {"linked_flash_bytes": 100, "static_ram_bytes": 30},
        )

    def test_duplicate_diram_and_tls_parts_fail_closed(self) -> None:
        duplicate_diram = size_report(100, data=1, bss=2)
        duplicate_diram["layout"].append(copy.deepcopy(duplicate_diram["layout"][0]))
        with self.assertRaisesRegex(accounting.ContractError, "one unique DIRAM"):
            accounting.parse_size_report(duplicate_diram)
        duplicate_tls = size_report(100, data=1, bss=2, tdata=3)
        duplicate_tls["layout"].append(
            {
                "name": "Other",
                "total": 0,
                "used": 4,
                "free": 0,
                "parts": {".tdata": {"size": 4}},
            }
        )
        with self.assertRaisesRegex(accounting.ContractError, "duplicate TLS"):
            accounting.parse_size_report(duplicate_tls)

    def test_report_types_structure_and_encoding_fail_closed(self) -> None:
        wrong_type = size_report(100, data=1, bss=2)
        wrong_type["total_size"] = True
        with self.assertRaisesRegex(accounting.ContractError, "exact integer"):
            accounting.parse_size_report(wrong_type)
        wrong_sum = size_report(100, data=1, bss=2)
        wrong_sum["layout"][0]["used"] += 1
        with self.assertRaisesRegex(accounting.ContractError, "part sum"):
            accounting.parse_size_report(wrong_sum)
        with self.assertRaisesRegex(accounting.ContractError, "duplicate key"):
            accounting._decode_json(b'{"version":"1.2","version":"1.2"}', "fixture")
        with self.assertRaisesRegex(accounting.ContractError, "BOM"):
            accounting._decode_json(b"\xef\xbb\xbf{}", "fixture")

    def test_positive_deltas_and_absolute_static_projection_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(
                directory,
                size_report(200, data=20, bss=30, noinit=2, tdata=3, tbss=4),
                size_report(150, data=10, bss=20, noinit=1, tdata=1, tbss=2),
            )
            verdict = self.evaluate(fixture, self.result(fixture))
            self.assertEqual(verdict["verdict"], "pass")
            self.assertEqual(verdict["measurements"]["linked_flash_delta_bytes"], 50)
            self.assertEqual(verdict["measurements"]["static_ram_candidate_bytes"], 59)
            self.assertEqual(verdict["measurements"]["static_ram_delta_bytes"], 25)

    def test_zero_deltas_are_admitted_without_coercion(self) -> None:
        report = size_report(200, data=20, bss=30, noinit=2, tdata=3, tbss=4)
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(directory, report, copy.deepcopy(report))
            verdict = self.evaluate(fixture, self.result(fixture))
            self.assertEqual(verdict["measurements"]["linked_flash_delta_bytes"], 0)
            self.assertEqual(verdict["measurements"]["static_ram_delta_bytes"], 0)

    def test_negative_deltas_are_admitted_without_absolute_value(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(
                directory,
                size_report(100, data=10, bss=10),
                size_report(160, data=20, bss=30),
            )
            verdict = self.evaluate(fixture, self.result(fixture))
            self.assertEqual(verdict["measurements"]["linked_flash_delta_bytes"], -60)
            self.assertEqual(verdict["measurements"]["static_ram_delta_bytes"], -30)

    def test_delta_tamper_and_static_delta_projection_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(
                directory,
                size_report(100, data=10, bss=10),
                size_report(160, data=20, bss=30),
            )
            absolute = self.result(fixture)
            absolute["measurements"]["linked_flash_delta_bytes"] = 60
            absolute["phase_two_projection"]["linked_flash_delta_bytes"] = 60
            self.expect_error(fixture, absolute, "recomputation")
            projected_delta = self.result(fixture)
            projected_delta["phase_two_projection"]["static_ram_bytes"] = -30
            self.expect_error(fixture, projected_delta, "exact integer")

    def test_unmatched_builds_metadata_and_substitution_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(
                directory,
                size_report(200, data=20, bss=30),
                size_report(150, data=10, bss=20),
            )
            a_b = self.result(fixture)
            a_b["builds"]["candidate"]["runs"][1]["artifacts"]["application_bin"]["bytes"] += 1
            self.expect_error(fixture, a_b, "A/B normalized artifacts differ")
            metadata = self.result(fixture)
            metadata["builds"]["control"]["matched_metadata"]["project_version"] = "other"
            self.expect_error(fixture, metadata, "matched metadata differ")
            reused_log = self.result(fixture)
            reused_log["builds"]["control"]["runs"][0]["raw_build_log_sha256"] = (
                reused_log["builds"]["candidate"]["runs"][0]["raw_build_log_sha256"]
            )
            self.expect_error(fixture, reused_log, "four matched builds")
            substituted = self.result(fixture)
            substituted["builds"]["candidate"]["runs"][0]["artifacts"]["size_report"] = copy.deepcopy(
                substituted["builds"]["candidate"]["runs"][0]["artifacts"]["application_bin"]
            )
            substituted["builds"]["candidate"]["runs"][1]["artifacts"]["size_report"] = copy.deepcopy(
                substituted["builds"]["candidate"]["runs"][0]["artifacts"]["size_report"]
            )
            self.expect_error(fixture, substituted, "does not match its A/B build tuple")

    def test_report_path_hash_bytes_candidate_and_claims_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(
                directory,
                size_report(200, data=20, bss=30),
                size_report(150, data=10, bss=20),
            )
            escaped = self.result(fixture)
            escaped["reports"]["candidate"]["path"] = "../candidate.json"
            self.expect_error(fixture, escaped, "escapes|outside")
            same_path = self.result(fixture)
            same_path["reports"]["control"]["path"] = same_path["reports"]["candidate"]["path"]
            self.expect_error(fixture, same_path, "distinct retained paths")
            bad_hash = self.result(fixture)
            bad_hash["reports"]["candidate"]["raw_sha256"] = "0" * 64
            self.expect_error(fixture, bad_hash, "digest binding")
            bad_candidate = self.result(fixture)
            bad_candidate["candidate_id"] = "unknown"
            self.expect_error(fixture, bad_candidate, "not admitted")
            bad_claim = self.result(fixture)
            bad_claim["claims"]["phase_two_complete"] = True
            self.expect_error(fixture, bad_claim, "claims exceed")

    def test_cli_is_bounded_and_errors_are_sanitized(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/crypto_matched_resource_accounting.py"),
                "validate-contract",
                str(CONTRACT_PATH),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn('"schema": "OTMRAC1"', completed.stdout)
        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory) / "bad.json"
            bad.write_text('{"schema":"OTMRAC1","schema":"OTMRAC1"}', encoding="utf-8")
            rejected = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/crypto_matched_resource_accounting.py"),
                    "validate-contract",
                    str(bad),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(rejected.returncode, 2)
            self.assertEqual(rejected.stdout, "")
            self.assertEqual(
                rejected.stderr.strip(),
                "ERROR: matched resource evidence is invalid or unaccepted",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
