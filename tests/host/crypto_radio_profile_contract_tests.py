#!/usr/bin/env python3
"""Adversarial host tests for the fail-closed OT-110 radio-profile contract."""

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
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
sys.path.insert(0, str(TOOLS))

import crypto_radio_profile_contract as radio_contract  # noqa: E402


CONTRACT_PATH = (
    CRYPTO
    / "OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0.json"
)
HISTORICAL_RAW = {
    "OT-094-OT005-CANDIDATE-READINESS-V0.json":
        "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae",
    "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json":
        "517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e",
    "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json":
        "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json":
        "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
    "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json":
        "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
    "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json":
        "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0",
}


def fixture() -> dict:
    return radio_contract.load(CONTRACT_PATH)


def expect_error(action, label: str) -> None:
    try:
        action()
    except radio_contract.ContractError:
        return
    raise AssertionError(f"expected OT-110 rejection: {label}")


def mutation(path: tuple[object, ...], value: object) -> dict:
    changed = copy.deepcopy(fixture())
    cursor = changed
    for part in path[:-1]:
        cursor = cursor[part]  # type: ignore[index]
    cursor[path[-1]] = value  # type: ignore[index]
    return changed


def validate_changed(path: tuple[object, ...], value: object, label: str) -> None:
    changed = mutation(path, value)
    expect_error(lambda: radio_contract.validate_contract(changed), label)


def test_canonical_contract_is_exact_and_host_only() -> None:
    contract = fixture()
    result = radio_contract.validate_contract(contract)
    assert result == {
        "schema": "OTRPF0",
        "version": 0,
        "contract_id": radio_contract.CONTRACT_ID,
        "status": "direct_radio_profile_evidence_contract_frozen_host_only",
        "blocker_id": "direct_radio_mtu_phy_region_unresolved",
        "region_code": "US915",
        "physical_node_count": 2,
        "measured_values_status": "unmeasured",
        "blocker_closed": False,
        "radio_transmit_authorized": False,
        "readiness_advanced": False,
        "contract_canonical_sha256": radio_contract.canonical_sha256(contract),
    }
    assert result["contract_canonical_sha256"] == radio_contract.EXPECTED_CONTRACT_SHA256
    assert (
        hashlib.sha256(CONTRACT_PATH.read_bytes()).hexdigest()
        == radio_contract.EXPECTED_CONTRACT_RAW_SHA256
    )


def test_contract_freeze_cannot_invent_measurements_or_close_readiness() -> None:
    contract = fixture()
    assert set(contract["profile_requirements"]["frozen_measured_values"].values()) == {None}
    assert contract["closure_boundary"]["contract_freeze_closes_blocker"] is False
    assert contract["closure_boundary"]["independent_admission_required"] is True
    assert contract["closure_boundary"]["physical_evidence_required"] is True
    cases = (
        (("profile_requirements", "frozen_measured_values", "frequency_hz"), 915000000, "invented frequency"),
        (("profile_requirements", "frozen_measured_values", "tx_power_dbm"), 22, "invented power"),
        (("profile_requirements", "frozen_measured_values", "direct_payload_ceiling_bytes"), 255, "invented ceiling"),
        (("closure_boundary", "measured_values_status"), "measured", "false measurement"),
        (("closure_boundary", "contract_freeze_closes_blocker"), True, "self-closing contract"),
        (("closure_boundary", "readiness_after_contract"), "ready", "false readiness"),
        (("claims", "physical_evidence_generated"), True, "false physical evidence"),
        (("claims", "blocker_closed"), True, "false blocker closure"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)


def test_profile_region_mtu_and_full_phy_requirements_are_immutable() -> None:
    cases = (
        (("closure_boundary", "region_code"), "EU868", "region substitution"),
        (("profile_requirements", "region_code"), "AU915", "profile region substitution"),
        (("profile_requirements", "frequency_hz", "minimum"), 902000001, "frequency bound"),
        (("profile_requirements", "frequency_hz", "occupied_band_must_fit"), False, "occupied band"),
        (("profile_requirements", "tx_power_dbm", "maximum"), 31, "unapproved power"),
        (("profile_requirements", "benchmark_mtu_protocol_test_requirement", "bytes"), 255, "benchmark MTU"),
        (("profile_requirements", "benchmark_mtu_must_not_exceed_direct_ceiling"), False, "MTU ceiling relation"),
        (("profile_requirements", "exact_boolean_fields"), ["explicit_header", "crc_enabled"], "incomplete PHY"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)
    contract = fixture()
    assert "benchmark_mtu_bytes" not in contract["profile_requirements"]["ordered_fields"]
    assert contract["profile_requirements"]["benchmark_mtu_protocol_test_requirement"] == {
        "bytes": 163, "classification": "protocol_test_requirement_not_measurement"
    }
    changed = fixture()
    changed["profile_requirements"]["ordered_fields"][0], changed["profile_requirements"]["ordered_fields"][1] = (
        changed["profile_requirements"]["ordered_fields"][1],
        changed["profile_requirements"]["ordered_fields"][0],
    )
    expect_error(lambda: radio_contract.validate_contract(changed), "PHY field reordering")


def test_two_exact_devices_and_fresh_identity_are_required() -> None:
    contract = fixture()
    assert contract["device_requirements"]["physical_node_count"] == 2
    assert contract["device_requirements"]["primary"]["identity_parent_required"] is True
    assert contract["device_requirements"]["peer"] == {
        "identity_state": "must_be_independently_resolved_before_execution",
        "same_model_or_revision_assumed": False,
    }
    cases = (
        (("device_requirements", "physical_node_count"), 1, "single node"),
        (("device_requirements", "primary", "evidence_unit"), "OT-DEV-002", "stale device substitution"),
        (("device_requirements", "primary", "identity_parent_required"), False, "unbound primary identity"),
        (("device_requirements", "peer", "identity_state"), "assumed_same", "assumed peer identity"),
        (("device_requirements", "peer", "same_model_or_revision_assumed"), True, "mixed identity assumption"),
        (("device_requirements", "same_exact_profile_on_both_nodes"), False, "mixed profile"),
        (("device_requirements", "both_nodes_require_direct_test_firmware"), False, "mixed firmware"),
        (("device_requirements", "restore_evidence", "successor_recovery_contract_required"), False, "missing recovery contract"),
        (("device_requirements", "restore_evidence", "pre_write_restore_evidence_must_be_admitted"), False, "unadmitted restore evidence"),
        (("device_requirements", "restore_evidence", "post_test_restore_readback_required"), False, "missing post-restore readback"),
        (("device_requirements", "restore_evidence", "per_node_required", 0), "restore_route", "inexact restore field"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)


def test_preflight_denies_silent_or_unapproved_transmit() -> None:
    contract = fixture()
    assert contract["preflight_requirements"]["fresh_owner_authority_required"] == [
        "device_access_authorized", "flash_authorized", "radio_transmit_authorized"
    ]
    assert contract["preflight_requirements"]["off_air_validation_required_before_transmit"] is True
    assert contract["preflight_requirements"]["receiver_configured_before_transmitter"] is True
    cases = (
        (("preflight_requirements", "fresh_owner_authority_required"), ["radio_transmit_authorized"], "partial authority"),
        (("preflight_requirements", "off_air_validation_required_before_transmit"), False, "silent preflight bypass"),
        (("preflight_requirements", "receiver_configured_before_transmitter"), False, "transmit before receiver"),
        (("preflight_requirements", "legal_or_regulatory_acceptance_not_inferred"), False, "inferred compliance"),
        (("device_requirements", "us915_capable_antenna_attached_before_transmit"), False, "antenna bypass"),
        (("authority", "device_access_authorized"), True, "device authority"),
        (("authority", "flash_authorized"), True, "flash authority"),
        (("authority", "radio_transmit_authorized"), True, "transmit authority"),
        (("authority", "regulatory_acceptance_claimed"), True, "regulatory claim"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)


def test_required_sequence_proves_over_air_delivery_and_local_oversize_reject() -> None:
    contract = fixture()
    sequence = contract["evidence_contract"]["required_sequence"]
    assert [step["step"] for step in sequence] == list(range(1, 9))
    assert sequence[0]["transmit"] is False and sequence[1]["transmit"] is False
    assert sequence[2]["operation"] == "one_byte_probe_each_direction"
    assert sequence[3]["frames_per_direction"] == 100 and sequence[3]["payload_bytes"] == 163
    assert sequence[5] == {
        "step": 6, "operation": "oversize_local_reject", "transmit": False,
        "attempts_per_node": 1, "payload_bytes": 256,
    }
    assert sequence[7]["operation"] == "post_restart_benchmark_mtu_each_direction"
    cases = (
        (("evidence_contract", "required_sequence", 0, "transmit"), True, "preflight transmission"),
        (("evidence_contract", "required_sequence", 1, "transmit"), True, "receiver setup transmission"),
        (("evidence_contract", "required_sequence", 2, "frames_per_direction"), 0, "missing probe"),
        (("evidence_contract", "required_sequence", 3, "frames_per_direction"), 1, "truncated MTU run"),
        (("evidence_contract", "required_sequence", 5, "transmit"), True, "oversize transmitted"),
        (("evidence_contract", "required_sequence", 5, "payload_bytes"), 255, "wrong oversize boundary"),
        (("evidence_contract", "acceptance", "oversize_256_local_reject_without_transmit"), False, "oversize rejection absent"),
        (("evidence_contract", "acceptance", "restart_retains_exact_profile"), False, "stale post-restart profile"),
        (("evidence_contract", "acceptance", "unexpected"), 1, "unexpected traffic"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)


def test_proof_and_admission_boundaries_cannot_promote_the_result() -> None:
    contract = fixture()
    assert "regulatory_compliance" in contract["evidence_contract"]["does_not_prove"]
    assert "benchmark_readiness" in contract["evidence_contract"]["does_not_prove"]
    assert contract["admission_contract"]["new_immutable_executable_plan_required"] is True
    assert contract["admission_contract"]["readiness_advanced_by_admission"] is False
    cases = (
        (("evidence_contract", "does_not_prove"), ["range"], "overclaimed proof"),
        (("admission_contract", "must_bind_raw_and_canonical_sha256"), ["evidence"], "unbound contract"),
        (("admission_contract", "only_closable_blocker_id"), "all", "broad closure"),
        (("admission_contract", "new_immutable_executable_plan_required"), False, "legacy plan reuse"),
        (("admission_contract", "readiness_advanced_by_admission"), True, "false readiness advancement"),
        (("admission_contract", "benchmark_or_selection_authority"), True, "benchmark authority"),
        (("claims", "production_support_proven"), True, "support overclaim"),
        (("claims", "regulatory_compliance_proven"), True, "compliance overclaim"),
    )
    for path, value, label in cases:
        validate_changed(path, value, label)


def test_privacy_duplicate_keys_and_raw_format_are_fail_closed() -> None:
    for private_value in (
        "C:" + "\\Users\\operator\\radio.json",
        "COM44",
        ":".join(("AA", "BB", "CC", "DD", "EE", "FF")),
        "/home/operator/private.json",
    ):
        validate_changed(("public_result",), private_value, "private identity or path")

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        duplicate = temporary / "duplicate.json"
        duplicate.write_text('{"schema":"OTRPF0","schema":"OTRPF0"}', encoding="utf-8")
        expect_error(lambda: radio_contract.load(duplicate), "duplicate JSON key")

        reformatted = temporary / CONTRACT_PATH.name
        reformatted.write_text(
            json.dumps(fixture(), indent=4, ensure_ascii=False).replace("\n", "\r\n") + "\r\n",
            encoding="utf-8", newline="",
        )
        assert radio_contract.canonical_sha256(radio_contract.load(reformatted)) == radio_contract.EXPECTED_CONTRACT_SHA256
        expect_error(
            lambda: radio_contract.load(reformatted, radio_contract.EXPECTED_CONTRACT_RAW_SHA256),
            "canonical-equivalent raw substitution",
        )


def test_cli_is_pinned_and_hostile_arguments_are_private() -> None:
    command = [sys.executable, str(radio_contract.__file__), "--contract", str(CONTRACT_PATH)]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    result = json.loads(completed.stdout)
    assert result["contract_raw_sha256"] == radio_contract.EXPECTED_CONTRACT_RAW_SHA256
    assert result["contract_canonical_sha256"] == radio_contract.EXPECTED_CONTRACT_SHA256
    assert result["radio_transmit_authorized"] is False
    assert result["blocker_closed"] is False

    hostile = subprocess.run(
        [sys.executable, str(radio_contract.__file__), "--private=C:" + "\\Users\\operator\\secret.json"],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: validation failed"
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr

    with tempfile.TemporaryDirectory() as directory:
        reformatted = Path(directory) / CONTRACT_PATH.name
        reformatted.write_text(json.dumps(fixture(), indent=1) + "\n", encoding="utf-8")
        failed = subprocess.run(
            [sys.executable, str(radio_contract.__file__), "--contract", str(reformatted)],
            cwd=ROOT, capture_output=True, text=True,
        )
        assert failed.returncode == 2 and failed.stdout == ""
        assert failed.stderr.strip() == "ERROR: validation failed"
        assert "TemporaryDirectory" not in failed.stderr and "Traceback" not in failed.stderr
        assert hashlib.sha256(reformatted.read_bytes()).hexdigest() != radio_contract.EXPECTED_CONTRACT_RAW_SHA256


def test_parent_chain_and_historical_artifacts_are_immutable() -> None:
    contract = fixture()
    parent_digests = {item["path"]: item["raw_sha256"] for item in contract["parents"]}
    for name, expected in HISTORICAL_RAW.items():
        assert hashlib.sha256((CRYPTO / name).read_bytes()).hexdigest() == expected
        if name in {
            "OT-094-OT005-CANDIDATE-READINESS-V0.json",
            "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json",
            "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json",
            "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json",
            "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json",
        }:
            path = f"tests/benchmarks/crypto/{name}"
            assert parent_digests[path] == expected
    changed = fixture()
    changed["parents"][1]["raw_sha256"] = "00" * 32
    expect_error(lambda: radio_contract.validate_contract(changed), "stale parent digest")
    changed = fixture()
    changed["parents"][1], changed["parents"][2] = changed["parents"][2], changed["parents"][1]
    expect_error(lambda: radio_contract.validate_contract(changed), "parent reordering")


def main() -> int:
    tests = (
        test_canonical_contract_is_exact_and_host_only,
        test_contract_freeze_cannot_invent_measurements_or_close_readiness,
        test_profile_region_mtu_and_full_phy_requirements_are_immutable,
        test_two_exact_devices_and_fresh_identity_are_required,
        test_preflight_denies_silent_or_unapproved_transmit,
        test_required_sequence_proves_over_air_delivery_and_local_oversize_reject,
        test_proof_and_admission_boundaries_cannot_promote_the_result,
        test_privacy_duplicate_keys_and_raw_format_are_fail_closed,
        test_cli_is_pinned_and_hostile_arguments_are_private,
        test_parent_chain_and_historical_artifacts_are_immutable,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-110 radio-profile contract scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
