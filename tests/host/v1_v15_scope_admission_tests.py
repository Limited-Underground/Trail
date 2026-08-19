#!/usr/bin/env python3
"""Deterministic tests for the OTVS0 V1/V1.5 scope boundary."""

from __future__ import annotations

import ast
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import android_release_admission as android_admission  # noqa: E402
import v1_v15_scope_admission as admission  # noqa: E402


PLAN_PATH = (
    ROOT
    / "tests"
    / "release-plans"
    / "OT-089-V1-V15-ACCEPTANCE-SCOPE-V0.json"
)
ANDROID_PLAN_PATH = (
    ROOT
    / "tests"
    / "release-plans"
    / "OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json"
)


def plan() -> dict:
    return json.loads(PLAN_PATH.read_text(encoding="utf-8"))


def expect_error(value: dict, contains: str) -> None:
    try:
        admission.validate_plan(value)
    except admission.AdmissionError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected AdmissionError containing {contains!r}")


def test_checked_in_scope_is_adopted_but_not_evaluated() -> None:
    report = admission.validate_plan(plan())
    assert report["plan_status"] == "OWNER-APPROVED-SCOPE-ADOPTED"
    assert (
        report["implementation_status"]
        == "IMPLEMENTATION-AND-PHYSICAL-ACCEPTANCE-OPEN"
    )
    assert report["v1_status"] == "NOT-EVALUATED"
    assert report["v1_5_status"] == "NOT-EVALUATED"
    assert report["execution_authority_granted"] is False
    assert report["score_credit_added"] is False
    assert report["open_followup_gates"] == [
        "ble_pairing_protocol_not_frozen",
        "lora_key_provisioning_workflow_not_frozen",
        "implementation_and_physical_acceptance_open",
    ]


def test_canonical_digest_is_exact_and_key_order_independent() -> None:
    value = plan()
    assert admission.canonical_sha256(value) == admission.CANONICAL_PLAN_SHA256
    reordered = dict(reversed(list(value.items())))
    assert admission.canonical_sha256(reordered) == admission.CANONICAL_PLAN_SHA256


def test_exact_top_level_shape_and_public_summary_fail_closed() -> None:
    value = plan()
    del value["v1"]
    expect_error(value, "keys differ")
    value = plan()
    value["unexpected"] = False
    expect_error(value, "keys differ")
    value = plan()
    value["public_summary"] = "V1 is complete"
    expect_error(value, "public summary")


def test_v1_exact_two_pair_topology_cannot_return_to_four_pairs() -> None:
    value = plan()
    hardware = value["v1"]["hardware"]
    assert hardware["heltec_nodes_required"] == 2
    assert hardware["android_phones_required"] == 2
    assert hardware["authorized_phones_per_heltec"] == 1
    assert value["v1"]["topology"]["path"] == [
        "phone-a",
        "ble",
        "heltec-a",
        "lora",
        "heltec-b",
        "ble",
        "phone-b",
    ]
    for field in ("heltec_nodes_required", "android_phones_required"):
        drift = plan()
        drift["v1"]["hardware"][field] = 4
        expect_error(drift, "canonical accepted scope")
    value["v1"]["topology"]["repeater_required"] = True
    expect_error(value, "canonical accepted scope")


def test_v1_practical_ble_authorization_and_limitation_are_exact() -> None:
    policy = plan()["v1"]["ble_authorization"]
    assert policy["closed_to_new_pairing_by_default"] is True
    assert policy["deliberate_physical_action_required"] is True
    assert policy["pairing_window"] == "short_bounded"
    assert policy["pairing_window_duration"] == "unfrozen_blocker"
    assert policy["pin_generation"] == "fresh_uniform_csprng_six_decimal_digits"
    assert policy["pin_display"] == "local_heltec_only"
    assert policy["pairing_method"] == (
        "authenticated_ble_le_secure_connections_passkey_and_bond"
    )
    assert policy["current_controller_limit"] == 1
    assert policy["saved_bond_reconnect"] is True
    assert policy["replacement_removes_previous_authorization"] is True
    assert policy["pairing_timeout_fails_closed"] is True
    assert policy["rollback_proof_against_physical_firmware_access"] is False
    assert "may_reset_or_rollback_ownership" in policy["accepted_limitation"]

    value = plan()
    value["v1"]["hardware"]["secure_element_required"] = True
    expect_error(value, "canonical accepted scope")
    value = plan()
    value["v1"]["ble_authorization"][
        "rollback_proof_against_physical_firmware_access"
    ] = True
    expect_error(value, "canonical accepted scope")
    value = plan()
    value["v1"]["ble_authorization"]["pairing_method"] = "just_works"
    expect_error(value, "canonical accepted scope")


def test_lora_security_is_separate_complete_and_still_unfrozen() -> None:
    security = plan()["v1"]["lora_security"]
    required_true = (
        "separate_from_ble_authorization",
        "network_or_conversation_authentication",
        "message_encryption",
        "sender_identity",
        "destination_identity",
        "unique_message_identifiers",
        "duplicate_suppression",
        "integrity_verification",
        "acknowledgement_and_bounded_retry",
        "reject_malformed",
        "reject_unauthenticated",
        "reject_wrong_network",
        "reject_replayed",
        "ordinary_protected_application_storage_allowed",
    )
    assert all(security[field] is True for field in required_true)
    assert security["physical_flash_rollback_resistance_required"] is False
    assert (
        security["key_provisioning_and_replacement_workflow"]
        == "unfrozen_blocker"
    )
    for field in (
        "separate_from_ble_authorization",
        "message_encryption",
        "reject_replayed",
    ):
        value = plan()
        value["v1"]["lora_security"][field] = False
        expect_error(value, "canonical accepted scope")


def test_v1_physical_acceptance_matrix_is_complete_and_not_a_pass() -> None:
    value = plan()["v1"]
    assert len(value["pairing_acceptance_checks"]) == 9
    assert len(value["communication_acceptance_checks"]) == 8
    assert len(value["android_release_acceptance_checks"]) == 6
    assert value["completion_status"] == "not_evaluated"
    assert "physical_phone_replacement_exercised" in value[
        "pairing_acceptance_checks"
    ]
    assert (
        "temporary_lora_interruption_uses_bounded_retry_or_failure_and_recovers"
        in value["communication_acceptance_checks"]
    )
    assert (
        "signed_artifact_installed_on_both_approved_phones"
        in value["android_release_acceptance_checks"]
    )
    drift = plan()
    drift["v1"]["communication_acceptance_checks"] = drift["v1"][
        "communication_acceptance_checks"
    ][:-1]
    expect_error(drift, "canonical accepted scope")


def test_v1_5_is_four_supported_nodes_without_a_mandatory_purchase_list() -> None:
    scope = plan()["v1_5"]
    assert scope["supported_nodes_required"] == 4
    assert scope["hardware_mixture_allowed"] is True
    assert scope["heterogeneous_hardware_preferred"] is True
    assert scope["heterogeneous_hardware_required"] is False
    assert scope["four_identical_supported_nodes_allowed"] is True
    assert scope["predetermined_model_list_required"] is False
    assert scope["four_phones_required"] is False
    assert scope["v1_endpoint_phones_may_remain"] == 2
    assert scope["measurement_status"] == "unmeasured"
    assert scope["completion_status"] == "not_evaluated"


def test_v1_5_rejects_model_phone_and_relay_scope_regressions() -> None:
    mutations = (
        ("predetermined_model_list_required", True),
        ("four_phones_required", True),
        ("heterogeneous_hardware_required", True),
        ("four_identical_supported_nodes_allowed", False),
    )
    for field, replacement in mutations:
        value = plan()
        value["v1_5"][field] = replacement
        expect_error(value, "canonical accepted scope")
    value = plan()
    value["v1_5"]["relay_claim"]["minimum_radios_if_claimed"] = 2
    expect_error(value, "canonical accepted scope")
    value = plan()
    value["v1_5"]["supported_nodes_required"] = 8
    expect_error(value, "canonical accepted scope")


def test_supersession_is_narrow_and_historical_records_remain() -> None:
    scope = plan()["supersession"]
    assert scope["preserve_historical_records"] is True
    assert scope["superseded_portions"] == [
        "decision-0004-initial-release-repeater-claim",
        "decision-0007-four-standalone-current-first-release",
        "decision-0009-standalone-four-unit-v1-unchanged",
        "decision-0017-four-person-companion-field-proof",
        "decision-0028-current-heltec-practical-authorization-deferral",
        "decision-0030-four-phone-private-sideload-and-four-pair-gate",
        "decision-0032-four-phone-support-scope",
        "first-release-capacity-v0-four-and-eight-client-sequence",
    ]
    assert "decisions-0026-0027-floor-rejections" in scope[
        "preserved_foundations"
    ]
    assert "otar-ot088-policy-evidence" in scope["preserved_foundations"]

    decision_0028 = (
        ROOT
        / "docs"
        / "decisions"
        / "0028-defer-rollback-protected-companion-authorization-beyond-current-heltec-v1.md"
    ).read_text(encoding="utf-8")
    assert "Rollback-protected companion authorization is deferred" in decision_0028
    decision_0017 = (
        ROOT / "docs" / "decisions" / "0017-v1-companion-release-measurement.md"
    ).read_text(encoding="utf-8")
    assert "Four-person Companion field proof" in decision_0017


def test_progress_privacy_authority_and_claims_add_no_credit() -> None:
    value = plan()
    assert value["progress"] == {
        "v1_milestone_weights": [15, 20, 15, 15, 20, 15],
        "v1_milestone_completions": [85, 65, 15, 25, 60, 0],
        "android_completion": 60,
        "v1_overall_exact": 43.75,
        "v1_overall_display": 44,
        "score_credit_added": False,
        "v1_5_measured": False,
    }
    assert all(flag is False for flag in value["execution_authority"].values())
    assert all(flag is False for flag in value["claims"].values())
    assert value["privacy"]["aggregate_public_evidence_only"] is True
    assert all(
        flag is False
        for key, flag in value["privacy"].items()
        if key != "aggregate_public_evidence_only"
    )


def test_current_otar_is_exactly_two_phone_and_remains_blocked() -> None:
    android_plan = json.loads(ANDROID_PLAN_PATH.read_text(encoding="utf-8"))
    assert android_plan["supported_platforms"] == {
        "minimum_api_level": 31,
        "required_phone_roles": ["phone-a", "phone-b"],
        "required_physical_phones": 2,
        "matrix_approved": False,
    }
    assert android_plan["operational_policy"]["support"]["scope"] == (
        "two_owner_approved_private_pilot_phones_only"
    )
    report = android_admission.validate_plan(android_plan)
    assert report["blockers"] == [
        "physical_acceptance_matrix_not_approved",
        "release_identity_not_approved",
        "signer_and_custody_not_approved",
    ]
    assert report["plan_status"] == "PLAN-ACCEPTED-EXECUTION-BLOCKED"
    assert report["release_gate_status"] == "NOT-EVALUATED"
    assert report["execution_authority_granted"] is False


def test_canonical_v1_progress_projects_scope_without_reweighting() -> None:
    progress = json.loads(
        (ROOT / "docs" / "V1_PROGRESS.json").read_text(encoding="utf-8")
    )
    tracks = {track["id"]: track for track in progress["release_tracks"]}
    v1 = tracks["v1-companion"]
    assert "two" in v1["definition"].lower()
    assert "four" not in v1["definition"].lower()
    assert [milestone["weight"] for milestone in v1["milestones"]] == [
        15,
        20,
        15,
        15,
        20,
        15,
    ]
    assert [milestone["completion"] for milestone in v1["milestones"]] == [
        85,
        65,
        15,
        25,
        60,
        0,
    ]
    assert v1["milestones"][-1]["id"] == "two-pair-end-to-end-acceptance"
    exact = sum(
        milestone["weight"] * milestone["completion"] / 100
        for milestone in v1["milestones"]
    )
    assert exact == 43.75
    assert v1["change_log"][-1]["overall_exact"] == 43.75
    assert v1["change_log"][-1]["overall"] == 44

    v1_5 = tracks["v1-5-multinode-interoperability"]
    assert v1_5["status"] == "unmeasured"
    assert v1_5["milestones"] == []


def test_private_fields_and_values_are_rejected_without_echo() -> None:
    value = plan()
    value["private_key"] = "not-public"
    expect_error(value, "prohibited field name")
    value = plan()
    private_value = "captured on COM44"
    value["public_summary"] = private_value
    try:
        admission.validate_plan(value)
    except admission.AdmissionError as exc:
        assert private_value not in str(exc)
        assert "COM44" not in str(exc)
    else:
        raise AssertionError("private transport value should fail")


def test_cli_and_bounded_loader_are_deterministic_and_sanitized() -> None:
    command = [
        sys.executable,
        str(ROOT / "tools" / "v1_v15_scope_admission.py"),
        "validate-plan",
        "--input",
        str(PLAN_PATH),
    ]
    first = subprocess.run(command, check=False, capture_output=True, text=True)
    second = subprocess.run(command, check=False, capture_output=True, text=True)
    assert first.returncode == 0
    assert first.stdout == second.stdout
    assert json.loads(first.stdout)["plan_status"] == "OWNER-APPROVED-SCOPE-ADOPTED"

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cases = {
            "private-COM44-duplicate.json": b'{"schema":"OTVS0","schema":"OTVS0"}',
            "private-COM44-utf8.json": b"\xff\xfe",
            "private-COM44-large.json": b" " * (admission.MAX_PLAN_BYTES + 1),
        }
        for name, encoded in cases.items():
            path = root / name
            path.write_bytes(encoded)
            failed = subprocess.run(
                command[:-1] + [str(path)],
                check=False,
                capture_output=True,
                text=True,
            )
            assert failed.returncode == 2
            assert json.loads(failed.stderr)["plan_status"] == "PLAN-INVALID"
            assert "COM44" not in failed.stderr
            assert str(root) not in failed.stderr

        deep = root / "private-COM44-deep.json"
        deep.write_text("[" * 2000 + "0" + "]" * 2000, encoding="utf-8")
        failed = subprocess.run(
            command[:-1] + [str(deep)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert failed.returncode == 2
        assert "Traceback" not in failed.stderr
        assert "COM44" not in failed.stderr
        assert str(root) not in failed.stderr


def test_validator_has_no_execution_or_output_creation_surface() -> None:
    source = (ROOT / "tools" / "v1_v15_scope_admission.py").read_text(
        encoding="utf-8"
    )
    tree = ast.parse(source)
    imports = {
        alias.name.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, ast.Import)
        for alias in node.names
    }
    imports.update(
        node.module.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom) and node.module
    )
    assert imports <= {
        "__future__",
        "argparse",
        "hashlib",
        "json",
        "pathlib",
        "re",
        "sys",
        "typing",
    }
    for forbidden in (
        "subprocess",
        "socket",
        "requests",
        "urllib",
        "write_text",
        "write_bytes",
        "adb",
        "apksigner",
        "keytool",
    ):
        assert forbidden not in source


def main() -> None:
    tests = [
        test_checked_in_scope_is_adopted_but_not_evaluated,
        test_canonical_digest_is_exact_and_key_order_independent,
        test_exact_top_level_shape_and_public_summary_fail_closed,
        test_v1_exact_two_pair_topology_cannot_return_to_four_pairs,
        test_v1_practical_ble_authorization_and_limitation_are_exact,
        test_lora_security_is_separate_complete_and_still_unfrozen,
        test_v1_physical_acceptance_matrix_is_complete_and_not_a_pass,
        test_v1_5_is_four_supported_nodes_without_a_mandatory_purchase_list,
        test_v1_5_rejects_model_phone_and_relay_scope_regressions,
        test_supersession_is_narrow_and_historical_records_remain,
        test_progress_privacy_authority_and_claims_add_no_credit,
        test_current_otar_is_exactly_two_phone_and_remains_blocked,
        test_canonical_v1_progress_projects_scope_without_reweighting,
        test_private_fields_and_values_are_rejected_without_echo,
        test_cli_and_bounded_loader_are_deterministic_and_sanitized,
        test_validator_has_no_execution_or_output_creation_surface,
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} V1/V1.5 scope admission scenario groups")


if __name__ == "__main__":
    main()
