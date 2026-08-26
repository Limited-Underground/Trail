#!/usr/bin/env python3
"""Focused adversarial checks for the OT-143 corrected-target build evidence."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot143_monocypher_corrected_target_evidence.py"
SPEC = importlib.util.spec_from_file_location("ot143_evidence", MODULE_PATH)
assert SPEC and SPEC.loader
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


def _source_bytes(relative: str, root: Path = ROOT) -> bytes:
    raw = (root / relative).read_bytes()
    if relative in evidence.CANONICAL_LF_INPUTS:
        raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return raw


def _artifact_list() -> list[dict[str, object]]:
    return [
        {"role": role, "name": name, "bytes": size, "sha256": digest}
        for role, name, size, digest in evidence.EXPECTED_ARTIFACTS
    ]


def build_fixture(root: Path = ROOT) -> dict:
    sources = []
    for relative in evidence.EXPECTED_SOURCE_INPUTS:
        raw = _source_bytes(relative, root)
        sources.append(
            {
                "path": relative,
                "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
        )
    artifacts = _artifact_list()
    return {
        "schema": "OT143CTB0",
        "version": 0,
        "artifact_kind": "monocypher_corrected_target_build_evidence",
        "evidence_id": "OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0",
        "recorded_date": "2026-08-26",
        "status": "corrected_target_build_complete_host_only",
        "source_project": (
            "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
            "monocypher_ot142_corrected"
        ),
        "lineage": {
            "predecessor": "OT-129",
            "accepted_direction": "OT-142",
            "frozen_protocol_runners_modified": False,
            "corrected_ot142_source_bound_directly": True,
        },
        "configuration": {
            "primary_console": "none",
            "secondary_console": "none",
            "bootloader_log_level": "none",
            "default_application_log_level": "none",
            "rom_log_policy": "always_on_unchanged",
            "direct_usb_serial_jtag_driver_enabled": True,
            "bluetooth_enabled": False,
            "wifi_enabled": False,
            "required_resolved_lines": list(evidence.EXPECTED_REQUIRED_LINES),
            "forbidden_enabled_lines": list(evidence.EXPECTED_FORBIDDEN_LINES),
        },
        "toolchain": {
            "esp_idf_version": "v6.0.2",
            "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
            "esp_idf_worktree_clean": True,
            "idf_target": "esp32s3",
            "project_version": "ot142-corrected-v0",
            "ccache_allowed": False,
            "component_manager_network_allowed": False,
        },
        "source_inputs": sources,
        "build_reproducibility": {
            "run_count": 2,
            "initial_build_directories_absent": True,
            "artifact_roles": [item[0] for item in evidence.EXPECTED_ARTIFACTS],
            "required_four_artifact_roles": [
                "application_bin",
                "application_elf",
                "linker_map",
                "generated_sdkconfig",
            ],
            "artifact_tuples_identical": True,
            "canonical_artifact_tuple": copy.deepcopy(artifacts),
            "runs": [
                {
                    "run": label,
                    "initial_build_directory_absent": True,
                    "build_exit_code": 0,
                    "compiler_warning_count": 0,
                    "raw_build_log_sha256": str(index + 1) * 64,
                    "artifacts": copy.deepcopy(artifacts),
                }
                for index, label in enumerate(("A", "B"))
            ],
        },
        "preserved_boundaries": {
            "pre_ready_budget_bytes": 512,
            "start_retry_milliseconds": 250,
            "exact_ready_required": True,
            "frame_before_ready_rejected": True,
            "duplicate_and_post_ready_strict": True,
            "privacy_safe_diagnostics_required": True,
            "real_frame_count": 1014,
        },
        "limitations": {
            "physical_usb_silence_proven": False,
            "initial_rom_output_suppression_proven": False,
            "efuse_or_strap_change_proposed": False,
            "reason": (
                "ESP-IDF host-only configuration cannot prove physical initial "
                "ROM output behavior."
            ),
        },
        "claims": {
            "hardware_accessed": False,
            "phone_accessed": False,
            "firmware_flashed": False,
            "benchmark_executed": False,
            "radio_used": False,
            "execution_authority_created": False,
            "candidate_selected": False,
            "phase_two_complete": False,
            "score_credit_added": False,
        },
    }


class Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = build_fixture()

    def assert_rejected(self, value: dict) -> None:
        with self.assertRaises(evidence.ValidationError):
            evidence.validate(value)

    def test_exact_corrected_tuple_validates(self) -> None:
        result = evidence.validate(self.value)
        self.assertEqual(result["schema"], "OT143CTB0")
        self.assertEqual(result["application_sha256"], evidence.EXPECTED_ARTIFACTS[0][3])
        self.assertEqual(result["sdkconfig_sha256"], evidence.EXPECTED_ARTIFACTS[-1][3])
        self.assertFalse(result["hardware_accessed"])
        self.assertFalse(result["execution_authority_created"])

    def test_old_ot139_artifacts_and_pair_mismatch_are_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["canonical_artifact_tuple"][0].update(
            {
                "bytes": 149920,
                "sha256": (
                    "29eee8c7294064d772770e2b4591c352"
                    "eb0a9068b63f5a1fc62d89481ec5f204"
                ),
            }
        )
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["build_reproducibility"]["runs"][1]["artifacts"][2]["bytes"] += 1
        self.assert_rejected(changed)

    def test_identity_lineage_and_forbidden_claim_are_rejected(self) -> None:
        for mutate in (
            lambda value: value.__setitem__("evidence_id", "OT-139"),
            lambda value: value["lineage"].__setitem__("accepted_direction", "OT-138"),
            lambda value: value["claims"].__setitem__("execution_authority_created", True),
        ):
            changed = copy.deepcopy(self.value)
            mutate(changed)
            self.assert_rejected(changed)

    def test_source_membership_order_and_canonical_digest_are_rejected(self) -> None:
        changed = copy.deepcopy(self.value)
        changed["source_inputs"][0], changed["source_inputs"][1] = (
            changed["source_inputs"][1],
            changed["source_inputs"][0],
        )
        self.assert_rejected(changed)
        changed = copy.deepcopy(self.value)
        changed["source_inputs"][4]["sha256"] = "f" * 64
        self.assert_rejected(changed)

    def test_stale_crlf_checkout_normalizes_to_canonical_lf(self) -> None:
        app = evidence.EXPECTED_SOURCE_INPUTS[4]
        item = next(entry for entry in self.value["source_inputs"] if entry["path"] == app)
        self.assertEqual(
            (item["bytes"], item["sha256"]),
            evidence.CANONICAL_CORRECTED_INPUTS[app],
        )
        self.assertEqual(evidence.validate(self.value)["schema"], "OT143CTB0")

    def test_utf8_bom_in_canonical_lf_input_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp_root = Path(directory)
            for relative in evidence.EXPECTED_SOURCE_INPUTS:
                destination = temp_root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((ROOT / relative).read_bytes())
            app = evidence.EXPECTED_SOURCE_INPUTS[4]
            app_path = temp_root / app
            app_path.write_bytes(b"\xef\xbb\xbf" + app_path.read_bytes())
            with self.assertRaises(evidence.ValidationError):
                evidence.validate(self.value, root=temp_root)

    def test_corrected_target_uses_local_app_and_frozen_control(self) -> None:
        target = (
            ROOT
            / "tests"
            / "benchmarks"
            / "crypto"
            / "esp_idf"
            / "ot121_candidate_benchmarks"
            / "monocypher_ot142_corrected"
        )
        main_cmake = (target / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertTrue((target / "main" / "app_main.c").is_file())
        self.assertIn('"app_main.c"', main_cmake)
        self.assertNotIn('"${OT129_MAIN_DIR}/app_main.c"', main_cmake)
        self.assertIn('"${OT129_MAIN_DIR}/ot129_control_protocol.c"', main_cmake)

    def test_builder_is_bounded_host_only_and_pins_exact_tuple(self) -> None:
        script = (
            ROOT / "tools" / "Build-Ot143MonocypherCorrectedTarget.ps1"
        ).read_text(encoding="utf-8")
        for required in (
            "unless -Execute is supplied",
            "OutputRoot must be initially absent",
            "status --porcelain --untracked-files=all",
            "--no-ccache",
            "IDF_COMPONENT_MANAGER = '0'",
            "PROJECT_VER=ot142-corrected-v0",
            "Get-CanonicalLfBytes",
            "canonical CRLF partition input differs",
            "artifact tuple differs from the accepted OT-142 tuple",
            evidence.EXPECTED_ARTIFACTS[0][3],
            evidence.EXPECTED_ARTIFACTS[1][3],
            evidence.EXPECTED_ARTIFACTS[2][3],
            "execution_authority_created = $false",
            "hardware_accessed = $false",
        ):
            self.assertIn(required, script)
        lowered = script.lower()
        self.assertNotIn("write_flash", lowered)
        self.assertNotIn("erase_flash", lowered)
        self.assertNotIn("serial.tools.list_ports", lowered)
        self.assertNotIn("ot139-quiet-v0", lowered)

    def test_duplicate_json_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text('{"schema":"OT143CTB0","schema":"duplicate"}', encoding="utf-8")
            with self.assertRaises(evidence.ValidationError):
                evidence.load_evidence(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)



