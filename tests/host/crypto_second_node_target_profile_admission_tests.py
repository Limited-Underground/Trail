#!/usr/bin/env python3
"""Adversarial host tests for OT-119 second-node exact-profile admission."""
from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import crypto_second_node_target_profile_admission as validator  # noqa: E402


RECEIPT = ROOT / "tests/benchmarks/crypto/OT-119-OT005-SECOND-NODE-EXACT-PROFILE-USB-RECEIPT-V0.json"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-119-OT005-SECOND-NODE-EXACT-PROFILE-EVIDENCE-V1.json"
ADMISSION = ROOT / "tests/benchmarks/crypto/OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1.json"


def artifacts():
    return (
        validator.load(RECEIPT, validator.EXPECTED_RECEIPT_RAW_SHA256),
        validator.load(EVIDENCE, validator.EXPECTED_EVIDENCE_RAW_SHA256),
        validator.load(ADMISSION, validator.EXPECTED_ADMISSION_RAW_SHA256),
    )


def expect_error(action, text: str = "") -> None:
    try:
        action()
    except validator.ValidationError as exc:
        assert text in str(exc), (text, str(exc))
        return
    raise AssertionError("expected validation failure")


def test_exact_artifacts_parent_chain_and_hashes() -> None:
    receipt, evidence, admission = artifacts()
    result = validator.validate_admission(admission, evidence, receipt)
    assert result == {
        "schema": "OTRTPA1",
        "version": 1,
        "admission_id": "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1",
        "accepted_exact_profile_units": 2,
        "source_count": 3,
        "api_config_count": 3,
        "candidate_import_count": 0,
        "phase_zero_complete": True,
        "measurement_ready": False,
        "execution_authorized": False,
        "selection_authorized": False,
        "score_credit_added": False,
        "admission_sha256": validator.EXPECTED_ADMISSION_SHA256,
    }
    for path, raw, canonical in (
        (RECEIPT, validator.EXPECTED_RECEIPT_RAW_SHA256, validator.EXPECTED_RECEIPT_SHA256),
        (EVIDENCE, validator.EXPECTED_EVIDENCE_RAW_SHA256, validator.EXPECTED_EVIDENCE_SHA256),
        (ADMISSION, validator.EXPECTED_ADMISSION_RAW_SHA256, validator.EXPECTED_ADMISSION_SHA256),
    ):
        value = validator.load(path, raw)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == raw
        assert validator.canonical_sha256(value) == canonical


def test_selected_unit_usb_and_photo_are_exact() -> None:
    receipt, _, _ = artifacts()
    assert receipt["target_selection"]["evidence_unit"] == "OT-DEV-002"
    assert receipt["target_selection"]["distinct_from_ot_dev_001"] is True
    assert receipt["usb_observation"]["normalized_facts"] == {
        "mcu_family": "ESP32-S3",
        "processor_revision": "v0.2",
        "crystal_mhz": 40,
        "embedded_psram_bytes": 2097152,
        "flash_bytes": 16777216,
    }
    assert receipt["owner_photo_observation"]["visible_markings"] == [
        "HTIT-WB32LAF",
        "V4.2",
    ]
    changed = copy.deepcopy(receipt)
    changed["target_selection"]["evidence_unit"] = "OT-DEV-001"
    expect_error(lambda: validator.validate_receipt(changed), "selected unit")
    changed = copy.deepcopy(receipt)
    changed["owner_photo_observation"]["visible_markings"] = ["V4.2"]
    expect_error(lambda: validator.validate_receipt(changed), "owner photo")


def test_usb_and_physical_sources_are_both_required() -> None:
    receipt, evidence, _ = artifacts()
    changed = copy.deepcopy(receipt)
    changed["usb_observation"]["normalized_facts"]["flash_bytes"] = 8388608
    expect_error(lambda: validator.validate_evidence(evidence, changed), "USB observation")
    changed = copy.deepcopy(receipt)
    changed["owner_photo_observation"]["same_selected_unit"] = False
    expect_error(lambda: validator.validate_evidence(evidence, changed), "owner photo")
    changed_evidence = copy.deepcopy(evidence)
    changed_evidence["corroboration"]["corroboration_closes_profile_without_usb_and_marking_evidence"] = True
    expect_error(
        lambda: validator.validate_evidence(changed_evidence, receipt),
        "corroboration",
    )


def test_exact_profile_matches_ot103_without_cross_device_aliasing() -> None:
    receipt, evidence, admission = artifacts()
    ot103 = validator.load(
        validator.HISTORICAL["otrtpe0"][0],
        validator.HISTORICAL["otrtpe0"][1],
    )
    assert evidence["exact_profile"] == ot103["exact_profile"]
    expected_target = dict(ot103["target_binding"])
    expected_target["evidence_unit"] = "OT-DEV-002"
    assert evidence["target_binding"] == expected_target
    changed = copy.deepcopy(evidence)
    changed["accepted_profile_registry"][0]["evidence_unit"] = "OT-DEV-002"
    expect_error(lambda: validator.validate_evidence(changed, receipt), "profile registry")
    changed_admission = copy.deepcopy(admission)
    changed_admission["accepted_target_profiles"].append(
        copy.deepcopy(changed_admission["accepted_target_profiles"][1])
    )
    expect_error(
        lambda: validator.validate_admission(changed_admission, evidence, receipt),
        "accepted target registry",
    )


def test_privacy_allowlist_rejects_identifiers_and_paths() -> None:
    receipt, evidence, _ = artifacts()
    for hostile in (
        "COM7",
        ":".join(("aa", "bb", "cc", "dd", "ee", "ff")),
        "C:\\Users\\operator\\probe.txt",
        "/home/operator/probe.txt",
        "private_key=not-allowed",
    ):
        changed = copy.deepcopy(receipt)
        changed["public_result"] = hostile
        expect_error(lambda changed=changed: validator.validate_receipt(changed), "private")
    changed = copy.deepcopy(receipt)
    changed["privacy"]["raw_probe_output_retained"] = True
    expect_error(lambda: validator.validate_receipt(changed), "privacy")
    changed_evidence = copy.deepcopy(evidence)
    changed_evidence["boundaries"]["private_device_identifier_retained"] = True
    expect_error(lambda: validator.validate_evidence(changed_evidence, receipt), "boundary")


def test_read_only_recovery_and_no_mutation_are_exact() -> None:
    receipt, evidence, _ = artifacts()
    usb = receipt["usb_observation"]
    assert usb["transient_rom_entry_or_reset"] is True
    assert usb["persistent_state_changed"] is False
    assert usb["firmware_heartbeat_after_probe"] == "ot_bench"
    assert usb["heartbeat_restored"] is True
    for field in ("firmware_changed", "flashed", "flash_read", "radio_used"):
        changed = copy.deepcopy(receipt)
        changed["boundaries"][field] = True
        expect_error(lambda changed=changed: validator.validate_receipt(changed), "authority")
    changed_evidence = copy.deepcopy(evidence)
    changed_evidence["claims"]["persistent_device_state_changed"] = True
    expect_error(lambda: validator.validate_evidence(changed_evidence, receipt), "claims")


def test_append_only_counts_and_registry_are_exact() -> None:
    receipt, evidence, admission = artifacts()
    assert [item["evidence_unit"] for item in admission["accepted_target_profiles"]] == [
        "OT-DEV-001",
        "OT-DEV-002",
    ]
    assert admission["acceptance_counts"] == {
        "exact_profile_units": 2,
        "source": 3,
        "api_config": 3,
        "candidate_import": 0,
    }
    changed = copy.deepcopy(admission)
    changed["acceptance_counts"]["candidate_import"] = 1
    expect_error(
        lambda: validator.validate_admission(changed, evidence, receipt),
        "acceptance count",
    )
    changed = copy.deepcopy(admission)
    changed["accepted_target_profiles"].reverse()
    expect_error(
        lambda: validator.validate_admission(changed, evidence, receipt),
        "accepted target registry",
    )


def test_phase_zero_transition_and_blockers_are_exact() -> None:
    receipt, evidence, admission = artifacts()
    assert admission["phase_zero"]["prior_remaining"] == [
        "independent_second_node_exact_profile_admission"
    ]
    assert admission["phase_zero"]["complete"] is True
    assert admission["phase_zero"]["remaining"] == []
    assert admission["measurement_blockers"] == [
        "candidate_import_and_build_admissions_absent",
        "fresh_benchmark_execution_authority_absent",
    ]
    changed = copy.deepcopy(admission)
    changed["measurement_blockers"].insert(0, "phase_zero_incomplete")
    expect_error(
        lambda: validator.validate_admission(changed, evidence, receipt),
        "measurement blocker",
    )
    changed = copy.deepcopy(admission)
    changed["claims"]["measurement_ready"] = True
    expect_error(lambda: validator.validate_admission(changed, evidence, receipt), "claims")


def test_authority_support_selection_and_score_remain_false() -> None:
    receipt, evidence, admission = artifacts()
    assert not any(admission["authority"].values())
    for field in admission["authority"]:
        changed = copy.deepcopy(admission)
        changed["authority"][field] = True
        expect_error(
            lambda changed=changed: validator.validate_admission(changed, evidence, receipt),
            "authority",
        )
    for field in (
        "candidate_benchmark_executed",
        "candidate_selected",
        "hardware_support_proven",
        "compatibility_proven",
        "regulatory_compliance_proven",
        "score_credit_added",
    ):
        changed = copy.deepcopy(admission)
        changed["claims"][field] = True
        expect_error(
            lambda changed=changed: validator.validate_admission(changed, evidence, receipt),
            "claims",
        )


def test_historical_bytes_cli_and_raw_tamper_fail_closed() -> None:
    for _, (path, raw, canonical) in validator.HISTORICAL.items():
        value = validator.load(path, raw)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == raw
        assert validator.canonical_sha256(value) == canonical
    assert hashlib.sha256(validator.OT115.read_bytes()).hexdigest() == validator.OT115_RAW_SHA256
    command = [
        sys.executable,
        str(validator.__file__),
        "--receipt", str(RECEIPT),
        "--evidence", str(EVIDENCE),
        "--admission", str(ADMISSION),
    ]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    payload = json.loads(completed.stdout)
    assert payload["phase_zero_complete"] is True
    with tempfile.TemporaryDirectory() as directory:
        different_cwd = subprocess.run(
            command,
            cwd=directory,
            capture_output=True,
            text=True,
        )
        assert different_cwd.returncode == 0, different_cwd.stderr
        hostile = subprocess.run(
            [sys.executable, str(validator.__file__), "--private=C:\\Users\\operator\\secret.json"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        assert hostile.returncode == 2
        assert hostile.stdout == ""
        assert hostile.stderr.strip() == "ERROR: invalid arguments"
        for option, source in (("--receipt", RECEIPT), ("--evidence", EVIDENCE), ("--admission", ADMISSION)):
            target = Path(directory) / source.name
            target.write_bytes(source.read_bytes().replace(b"\n", b"\r\n"))
            changed = list(command)
            changed[changed.index(option) + 1] = str(target)
            rejected = subprocess.run(changed, cwd=ROOT, capture_output=True, text=True)
            assert rejected.returncode == 1
            assert rejected.stdout == ""
            assert rejected.stderr.strip() == "ERROR: validation failed"
        duplicate = Path(directory) / "duplicate.json"
        duplicate.write_text('{"schema":"OTRTPR0","schema":"OTRTPR0"}', encoding="utf-8")
        expect_error(lambda: validator.load(duplicate), "duplicate key")


def main() -> int:
    tests = (
        test_exact_artifacts_parent_chain_and_hashes,
        test_selected_unit_usb_and_photo_are_exact,
        test_usb_and_physical_sources_are_both_required,
        test_exact_profile_matches_ot103_without_cross_device_aliasing,
        test_privacy_allowlist_rejects_identifiers_and_paths,
        test_read_only_recovery_and_no_mutation_are_exact,
        test_append_only_counts_and_registry_are_exact,
        test_phase_zero_transition_and_blockers_are_exact,
        test_authority_support_selection_and_score_remain_false,
        test_historical_bytes_cli_and_raw_tamper_fail_closed,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-119 second-node exact-profile admission scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
