#!/usr/bin/env python3
"""Adversarial tests for the fail-closed OT-121 Phase 2 authority validator."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import crypto_benchmark_execution_authority as validator  # noqa: E402


def fixture() -> tuple[dict, dict]:
    parents = validator.validate_parent_files()
    return validator.load(validator.AUTHORITY), parents


def rejected(action, label: str) -> None:
    try:
        action()
    except (validator.ValidationError, OSError, KeyError, TypeError, ValueError, IndexError):
        return
    raise AssertionError(f"expected OT-121 rejection: {label}")


def changed(value: dict, path: tuple[object, ...], replacement: object) -> dict:
    result = copy.deepcopy(value)
    cursor = result
    for part in path[:-1]:
        cursor = cursor[part]  # type: ignore[index]
    cursor[path[-1]] = replacement  # type: ignore[index]
    return result


def test_exact_authority_passes_without_claiming_execution() -> None:
    authority, parents = fixture()
    result = validator.validate_authority(authority, parents)
    assert result["schema"] == "OTCBXA1"
    assert result["phase_two_execution_authorized"] is True
    assert result["measurement_ready"] is True
    assert result["benchmark_executed"] is False
    assert hashlib.sha256(validator.AUTHORITY.read_bytes()).hexdigest() == validator.AUTHORITY_PIN[0]
    assert result["canonical_sha256"] == validator.AUTHORITY_PIN[1]


def test_all_parent_paths_and_digests_are_immutable() -> None:
    authority, parents = fixture()
    for parent in validator.PARENTS:
        for field, replacement in (("path", "tests/other.json"), ("raw_sha256", "00" * 32), ("canonical_sha256", "11" * 32)):
            rejected(lambda parent=parent, field=field, replacement=replacement: validator.validate_authority(changed(authority, ("parents", parent, field), replacement), parents), f"{parent}.{field}")


def test_owner_resumption_after_disclosure_is_required() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("owner_authorization", "temporary_benchmark_flash_and_device_access_disclosed_before_resumption"), False),
        (("owner_authorization", "owner_explicitly_resumed_after_disclosure"), False),
        (("owner_authorization", "continuing_authority_created"), True),
        (("owner_authorization", "authorization_source"), "inferred"),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_phase_zero_one_counts_and_independent_admission_are_exact() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("preconditions", "phase_zero_complete"), False),
        (("preconditions", "phase_one_complete"), False),
        (("preconditions", "acceptance_counts", "candidate_import"), 2),
        (("preconditions", "independent_result_admission_still_required"), False),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_target_candidate_operation_and_measurement_scope_cannot_drift() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("target_scope", "measurement_units"), ["OT-DEV-001"]),
        (("target_scope", "supported"), True),
        (("measurement_scope", "candidate_order", 0), "monocypher"),
        (("measurement_scope", "operations"), validator.OPERATIONS[:-1]),
        (("measurement_scope", "minimum_repetitions_per_admitted_operation", "cold"), 99),
        (("measurement_scope", "required_measurements"), validator.MEASUREMENTS[:-1]),
        (("measurement_scope", "unavailable_operations_must_have_no_measurements"), False),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_flash_scope_is_temporary_and_fail_closed() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("temporary_flash_scope", "benchmark_firmware_only"), False),
        (("temporary_flash_scope", "persistent_application_change_authorized"), True),
        (("temporary_flash_scope", "partition_table_change_authorized"), True),
        (("temporary_flash_scope", "security_fuse_or_flash_encryption_change_authorized"), True),
        (("temporary_flash_scope", "restore_pre_execution_opentrail_image_after_measurement_or_abort"), False),
        (("temporary_flash_scope", "verify_flash_and_restore_readback"), False),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_radio_requires_both_antennas_and_exact_us915_two_dbm_close_bench() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("radio_scope", "conditional"), False),
        (("radio_scope", "both_nodes_antenna_preflight_required_before_transmit"), False),
        (("radio_scope", "fail_closed_if_antenna_preflight_is_not_affirmative"), False),
        (("radio_scope", "bench_context"), "field_range"),
        (("radio_scope", "region_code"), "EU868"),
        (("radio_scope", "tx_power_command_setpoint_dbm"), 3),
        (("radio_scope", "range_or_regulatory_claim_authorized"), True),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_one_time_authority_is_exact_and_not_reusable() -> None:
    authority, parents = fixture()
    for path, replacement in (
        (("one_time_authority", "device_access_authorized"), False),
        (("one_time_authority", "temporary_flash_authorized"), False),
        (("one_time_authority", "test_key_or_entropy_operation_authorized"), False),
        (("one_time_authority", "conditional_radio_transmit_authorized"), False),
        (("one_time_authority", "consumed_on_phase_two_completion_or_abort"), False),
        (("one_time_authority", "reusable"), True),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_authority(changed(authority, path, replacement), parents), str(path))


def test_selection_implementation_support_regulatory_production_and_score_stay_withheld() -> None:
    authority, parents = fixture()
    for field in authority["withheld_authority"]:
        if field == "continuing_authority_created":
            continue
        rejected(lambda field=field: validator.validate_authority(changed(authority, ("withheld_authority", field), True), parents), field)
    rejected(lambda: validator.validate_authority(changed(authority, ("withheld_authority", "continuing_authority_created"), True), parents), "continuing authority")


def test_authority_artifact_does_not_fabricate_physical_results() -> None:
    authority, parents = fixture()
    for field in ("benchmark_built", "benchmark_executed", "hardware_or_device_accessed_by_ot121", "firmware_flashed_by_ot121", "radio_used_by_ot121", "key_or_entropy_operation_performed_by_ot121", "candidate_selected", "suite_selected", "wire_format_selected", "secure_lora_implemented", "support_proven", "compatibility_proven", "regulatory_compliance_proven", "production_ready", "physical_measurement_evidence_added", "score_credit_added"):
        rejected(lambda field=field: validator.validate_authority(changed(authority, ("claims", field), True), parents), field)


def test_malformed_types_extra_keys_and_hostile_json_fail_closed() -> None:
    authority, parents = fixture()
    for replacement in (None, "authority", [], 1):
        rejected(lambda replacement=replacement: validator.validate_authority(changed(authority, ("parents",), replacement), parents), "parents type")
    extra = copy.deepcopy(authority); extra["extra"] = True
    rejected(lambda: validator.validate_authority(extra, parents), "extra key")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cases = {
            "duplicate.json": b'{"schema":"OTCBXA1","schema":"OTCBXA1"}',
            "private.json": b'{"path":"C:\\\\Users\\\\operator\\\\secret.txt"}',
            "invalid.json": b"\xff",
            "large.json": b"x" * (validator.MAX_BYTES + 1),
            "null.json": b"null",
            "list.json": b"[]",
        }
        for name, payload in cases.items():
            path = root / name; path.write_bytes(payload)
            rejected(lambda path=path: validator.load(path), name)


def test_cli_is_sanitized_and_has_no_execution_capability() -> None:
    completed = subprocess.run([sys.executable, str(validator.__file__)], cwd=ROOT, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    result = json.loads(completed.stdout)
    assert result["phase_two_execution_authorized"] is True
    assert result["measurement_ready"] is True and result["benchmark_executed"] is False
    with tempfile.TemporaryDirectory() as directory:
        bad = Path(directory) / "bad.json"; bad.write_text("null", encoding="utf-8")
        hostile = subprocess.run([sys.executable, str(validator.__file__), "--authority", str(bad)], cwd=ROOT, capture_output=True, text=True)
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: validation failed" and "Traceback" not in hostile.stderr
    source = Path(validator.__file__).read_text(encoding="utf-8")
    for token in ("import socket", "import requests", "import urllib", "import subprocess", "os.system", "Start-Process", "esptool", "serial"):
        assert token not in source


def main() -> int:
    tests = (
        test_exact_authority_passes_without_claiming_execution,
        test_all_parent_paths_and_digests_are_immutable,
        test_owner_resumption_after_disclosure_is_required,
        test_phase_zero_one_counts_and_independent_admission_are_exact,
        test_target_candidate_operation_and_measurement_scope_cannot_drift,
        test_flash_scope_is_temporary_and_fail_closed,
        test_radio_requires_both_antennas_and_exact_us915_two_dbm_close_bench,
        test_one_time_authority_is_exact_and_not_reusable,
        test_selection_implementation_support_regulatory_production_and_score_stay_withheld,
        test_authority_artifact_does_not_fabricate_physical_results,
        test_malformed_types_extra_keys_and_hostile_json_fail_closed,
        test_cli_is_sanitized_and_has_no_execution_capability,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-121 Phase 2 execution-authority scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
