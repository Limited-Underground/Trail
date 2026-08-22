#!/usr/bin/env python3
"""Adversarial tests for the fail-closed OT-116 readiness/plan validator."""

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
import crypto_benchmark_execution_plan as validator  # noqa: E402


def fixtures() -> tuple[dict, dict]:
    return validator.load(validator.READINESS), validator.load(validator.CONTRACT)


def rejected(action, label: str) -> None:
    try:
        action()
    except (validator.ValidationError, OSError, KeyError, TypeError, ValueError):
        return
    raise AssertionError(f"expected OT-116 rejection: {label}")


def changed(value: dict, path: tuple[object, ...], replacement: object) -> dict:
    result = copy.deepcopy(value)
    cursor = result
    for part in path[:-1]:
        cursor = cursor[part]  # type: ignore[index]
    cursor[path[-1]] = replacement  # type: ignore[index]
    return result


def test_exact_chain_passes_without_advancing_readiness() -> None:
    readiness, contract = fixtures()
    rr = validator.validate_readiness(readiness)
    cr = validator.validate_contract(contract, readiness)
    assert rr == {
        "schema": "OTCBR1", "status": "requirements_closed_execution_blocked",
        "requirements_closed": True, "measurement_ready": False,
        "readiness_advanced": False, "canonical_sha256": validator.PINS["readiness"][1],
    }
    assert cr["schema"] == "OTCBX1" and cr["execution_authorized"] is False
    assert cr["canonical_sha256"] == validator.PINS["contract"][1]
    assert hashlib.sha256(validator.READINESS.read_bytes()).hexdigest() == validator.PINS["readiness"][0]
    assert hashlib.sha256(validator.CONTRACT.read_bytes()).hexdigest() == validator.PINS["contract"][0]


def test_every_parent_and_closure_is_immutable() -> None:
    readiness, _ = fixtures()
    roles = [item["role"] for item in readiness["append_only_parents"]]
    assert roles == [item[0] for item in validator.PARENTS]
    assert {"preselection_build_baseline", "per_candidate_api_config_acceptance_contract", "direct_radio_execution_contract", "direct_radio_execution_receipt", "direct_radio_profile_evidence", "direct_radio_profile_admission"}.issubset(roles)
    assert len(roles) == len(set(roles)) == 13
    for field, replacement in (("path", "tests/benchmarks/crypto/other.json"), ("raw_sha256", "00" * 32), ("canonical_sha256", "11" * 32)):
        rejected(lambda field=field, replacement=replacement: validator.validate_readiness(changed(readiness, ("append_only_parents", 0, field), replacement), False), field)
    rejected(lambda: validator.validate_readiness(changed(readiness, ("closure_map", 5, "parent_role"), "historical_six_requirement_ledger"), False), "wrong closure")
    missing = copy.deepcopy(readiness); missing["closure_map"].pop()
    rejected(lambda: validator.validate_readiness(missing, False), "missing closure")


def test_historical_state_counts_and_claims_cannot_be_rewritten() -> None:
    readiness, _ = fixtures()
    for path, replacement in (
        (("historical_six_requirements", 0), "closed"),
        (("current_requirements",), ["invented"]),
        (("acceptance_counts", "candidate_import"), 1),
        (("successor_boundary", "readiness_advanced"), True),
        (("successor_boundary", "independent_second_node_exact_profile_admission_required"), False),
        (("claims", "measurement_ready"), True),
        (("authority", "benchmark_execution_authorized"), True),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_readiness(changed(readiness, path, replacement), False), str(path))


def test_candidate_dispositions_remain_premeasurement() -> None:
    readiness, _ = fixtures()
    rejected(lambda: validator.validate_readiness(changed(readiness, ("candidate_dispositions", 0, "api_config_admission"), "accepted_complete"), False), "fabricated libsodium API admission")
    rejected(lambda: validator.validate_readiness(changed(readiness, ("candidate_dispositions", 1, "api_config_admission"), "accepted_complete_8_of_8"), False), "fabricated mbed coverage")
    rejected(lambda: validator.validate_readiness(changed(readiness, ("candidate_dispositions", 2, "candidate_import_admission"), "accepted"), False), "fabricated import")


def test_target_toolchain_and_radio_are_exact() -> None:
    readiness, contract = fixtures()
    for path, replacement in (
        (("target", "accepted_profile_unit"), "OT-DEV-002"),
        (("target", "second_node_exact_profile_admitted"), True),
        (("target", "supported"), True),
        (("toolchain", "esp_idf_commit"), "0" * 40),
        (("radio_profile", "frequency_hz"), 914999999),
        (("radio_profile", "benchmark_total_wire_bytes"), 255),
        (("radio_profile", "first_locally_rejected_total_wire_bytes"), 257),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_contract(changed(contract, path, replacement), readiness), str(path))


def test_candidate_locks_configs_and_order_cannot_drift() -> None:
    readiness, contract = fixtures()
    for path, replacement in (
        (("candidates", 0, "dependency_lock_sha256"), "00" * 32),
        (("candidates", 1, "generated_sdkconfig_sha256"), contract["candidates"][0]["generated_sdkconfig_sha256"]),
        (("candidates", 2, "candidate_id"), "espressif_libsodium"),
        (("candidates", 0, "import_state"), "admitted"),
        (("candidates", 0, "selection_eligible"), True),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_contract(changed(contract, path, replacement), readiness), str(path))


def test_mbedtls_partial_operations_are_nonselectable() -> None:
    readiness, contract = fixtures()
    partial = contract["candidates"][1]
    assert len(partial["eligible_operations"]) == 5
    assert partial["unavailable_operations"] == ["ed25519_sign", "ed25519_verify", "noise_xk_handshake"]
    rejected(lambda: validator.validate_contract(changed(contract, ("candidates", 1, "eligible_operations"), validator.OPERATIONS), readiness), "fabricated 8/8")
    rejected(lambda: validator.validate_contract(changed(contract, ("candidates", 1, "unavailable_operations"), []), readiness), "removed unavailable set")
    rejected(lambda: validator.validate_contract(changed(contract, ("candidates", 1, "selection_eligible"), True), readiness), "partial candidate selection")


def test_phase_order_admission_barriers_and_counts_are_exact() -> None:
    readiness, contract = fixtures()
    for path, replacement in (
        (("ordered_phases", 0, "phase"), 1),
        (("ordered_phases", 0, "required_before_next_phase"), ["independent_libsodium_api_configuration_admission", "independent_monocypher_api_configuration_admission", "preserve_mbedtls_psa_five_of_eight_partial_nonselectable_state"]),
        (("ordered_phases", 0, "hardware_access"), True),
        (("ordered_phases", 1, "required_before_next_phase"), []),
        (("ordered_phases", 2, "requires_separate_authority"), False),
        (("ordered_phases", 2, "minimum_repetitions_per_admitted_operation", "cold"), 99),
        (("ordered_phases", 2, "unavailable_operations_must_have_no_measurements"), False),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_contract(changed(contract, path, replacement), readiness), str(path))


def test_gates_result_admission_and_authority_fail_closed() -> None:
    readiness, contract = fixtures()
    for path, replacement in (
        (("required_gates", 0), "host_self_test_only"),
        (("result_contract", "independent_admission_required"), False),
        (("result_contract", "partial_candidate_cannot_pass_selection_gate"), False),
        (("authority", "phase_0_execution_authorized"), True),
        (("authority", "device_access_authorized"), True),
        (("claims", "benchmark_executed"), True),
        (("claims", "score_credit_added"), True),
    ):
        rejected(lambda path=path, replacement=replacement: validator.validate_contract(changed(contract, path, replacement), readiness), str(path))


def test_readiness_binding_is_raw_and_canonical() -> None:
    readiness, contract = fixtures()
    rejected(lambda: validator.validate_contract(changed(contract, ("readiness_review", "raw_sha256"), "00" * 32), readiness), "raw readiness drift")
    rejected(lambda: validator.validate_contract(changed(contract, ("readiness_review", "canonical_sha256"), "11" * 32), readiness), "canonical readiness drift")


def test_hostile_json_is_rejected() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cases = {
            "duplicate.json": b'{"schema":"OTCBR1","schema":"OTCBR1"}',
            "private.json": b'{"path":"C:\\\\Users\\\\operator\\\\secret.txt"}',
            "invalid.json": b"\xff",
            "large.json": b"x" * (validator.MAX_BYTES + 1),
        }
        for name, payload in cases.items():
            path = root / name; path.write_bytes(payload)
            rejected(lambda path=path: validator.load(path), name)


def test_cli_is_sanitized_and_validator_has_no_execution_capability() -> None:
    completed = subprocess.run([sys.executable, str(validator.__file__)], cwd=ROOT, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    payload = json.loads(completed.stdout)
    assert payload["measurement_ready"] is False and payload["execution_authorized"] is False
    hostile = subprocess.run([sys.executable, str(validator.__file__), "--private=C:\\Users\\operator\\secret.json"], cwd=ROOT, capture_output=True, text=True)
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: validation failed" and "Users" not in hostile.stderr
    source = Path(validator.__file__).read_text(encoding="utf-8")
    for token in ("import socket", "import requests", "import urllib", "import subprocess", "os.system", "Start-Process"):
        assert token not in source


def main() -> int:
    tests = (
        test_exact_chain_passes_without_advancing_readiness,
        test_every_parent_and_closure_is_immutable,
        test_historical_state_counts_and_claims_cannot_be_rewritten,
        test_candidate_dispositions_remain_premeasurement,
        test_target_toolchain_and_radio_are_exact,
        test_candidate_locks_configs_and_order_cannot_drift,
        test_mbedtls_partial_operations_are_nonselectable,
        test_phase_order_admission_barriers_and_counts_are_exact,
        test_gates_result_admission_and_authority_fail_closed,
        test_readiness_binding_is_raw_and_canonical,
        test_hostile_json_is_rejected,
        test_cli_is_sanitized_and_validator_has_no_execution_capability,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-116 successor-readiness/plan scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
