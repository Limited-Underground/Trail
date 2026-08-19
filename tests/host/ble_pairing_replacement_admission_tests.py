#!/usr/bin/env python3
"""Deterministic tests for the OTBP0 practical BLE pairing contract."""

from __future__ import annotations

import ast
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import ble_pairing_replacement_admission as admission  # noqa: E402


CONTRACT_PATH = (
    ROOT
    / "tests"
    / "release-plans"
    / "OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0.json"
)


def contract() -> dict:
    return json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))


def expect_error(value: dict, contains: str) -> None:
    try:
        admission.validate_contract(value)
    except admission.AdmissionError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected AdmissionError containing {contains!r}")


def model(owner: bool = False) -> admission.PairingReplacementModel:
    value = admission.PairingReplacementModel()
    restored = value.restore(
        owner_present=owner,
        exact_state=True,
        bond_roster_exact=True,
        orphan_candidate_present=False,
    )
    assert restored["outcome"] == "restored"
    return value


def open_window(
    value: admission.PairingReplacementModel,
    now_ms: int = 0,
    event: int = 1,
    rng_ready: object = True,
    rng_succeeded: object = True,
) -> dict:
    return value.open_window(
        now_ms=now_ms,
        physical_event=event,
        hold_ms=3000,
        released=True,
        secure_random_ready=rng_ready,
        secure_random_sample_succeeded=rng_succeeded,
    )


def secure_pair(
    value: admission.PairingReplacementModel,
    now_ms: int = 1,
    **changes: object,
) -> dict:
    evidence: dict[str, object] = {
        "secure_connections": True,
        "mitm_authenticated": True,
        "bonded": True,
        "key_bytes": 16,
        "candidate_binding_exact": True,
    }
    evidence.update(changes)
    return value.pairing_result(now_ms=now_ms, **evidence)


def pending_replacement() -> admission.PairingReplacementModel:
    value = model(owner=True)
    assert open_window(value)["state"] == "replacement_window_open"
    assert secure_pair(value)["outcome"] == "replacement_confirmation_required"
    return value


def test_checked_in_contract_is_frozen_host_only() -> None:
    report = admission.validate_contract(contract())
    assert report == {
        "schema": "OTBP0",
        "version": 0,
        "artifact_kind": "ble_pairing_replacement_contract_admission",
        "contract_id": "OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0",
        "contract_sha256": admission.CANONICAL_CONTRACT_SHA256,
        "contract_status": "CONTRACT-FROZEN-HOST-ONLY",
        "implementation_status": "NOT-IMPLEMENTED",
        "physical_acceptance_status": "NOT-EVALUATED",
        "execution_authority_granted": False,
        "score_credit_added": False,
        "open_followup_gates": contract()["open_followup_gates"],
    }


def test_canonical_digest_is_exact_and_order_independent() -> None:
    value = contract()
    assert admission.canonical_sha256(value) == admission.CANONICAL_CONTRACT_SHA256
    assert (
        admission.canonical_sha256(dict(reversed(list(value.items()))))
        == admission.CANONICAL_CONTRACT_SHA256
    )


def test_exact_scope_security_storage_and_authority_fail_closed() -> None:
    mutations = [
        ("timing", "pairing_window_ms", 30001, "policy"),
        ("ble_security", "secure_connections_only", False, "policy"),
        ("ble_security", "required_key_bytes", 15, "policy"),
        ("ble_security", "static_or_debug_passkey_allowed", True, "policy"),
        ("android_os_pairing", "application_receives_pin", True, "Android"),
        ("target_pairing_adapter", "static_or_debug_passkey_allowed", True, "target"),
        ("candidate_cleanup", "verified_absence_required", False, "cleanup"),
        ("storage", "independent_monotonic_floor_required", True, "boundary"),
        ("storage", "rollback_proof_against_physical_firmware_access", True, "boundary"),
        ("claims", "target_pairing_implemented", True, "contract"),
        ("execution_authority", "bluetooth_pairing", True, "contract"),
    ]
    for section, field, replacement, expected in mutations:
        value = contract()
        value[section][field] = replacement
        expect_error(value, expected)


def test_physical_hold_release_and_replay_are_exact() -> None:
    value = model()
    assert value.open_window(
        now_ms=0,
        physical_event=1,
        hold_ms=2999,
        released=True,
        secure_random_ready=True,
        secure_random_sample_succeeded=True,
    )["outcome"] == "physical_gesture_rejected"
    assert value.open_window(
        now_ms=0,
        physical_event=1,
        hold_ms=3000,
        released=False,
        secure_random_ready=True,
        secure_random_sample_succeeded=True,
    )["outcome"] == "physical_gesture_rejected"
    opened = open_window(value)
    assert opened["state"] == "claim_window_open"
    assert opened["pin_displayed"] is True
    assert value.pin_samples == 1
    assert value.disconnect()["state"] == "closed_unowned"
    replay = open_window(value, now_ms=2, event=1)
    assert replay["outcome"] == "stale_physical_event"
    assert replay["pin_displayed"] is False


def test_window_boundary_is_29999_open_and_30000_closed() -> None:
    before = model()
    open_window(before)
    assert secure_pair(before, now_ms=29999)["outcome"] == "claim_commit_required"
    at_deadline = model()
    open_window(at_deadline)
    expired = at_deadline.tick(30000)
    assert expired["outcome"] == "window_expired"
    assert expired["state"] == "closed_unowned"
    assert expired["pin_displayed"] is False
    bonded_at_deadline = model()
    open_window(bonded_at_deadline)
    cleanup = secure_pair(bonded_at_deadline, now_ms=30000)
    assert cleanup["outcome"] == "window_expired_cleanup_required"
    assert cleanup["state"] == "candidate_cleanup_required"
    closed = bonded_at_deadline.candidate_cleanup_result(verified_absent=True)
    assert closed["state"] == "closed_unowned"


def test_initial_claim_needs_physical_bond_binding_and_exact_commit() -> None:
    value = model()
    assert secure_pair(value)["outcome"] == "window_closed"
    open_window(value, now_ms=2)
    pending = secure_pair(value, now_ms=3)
    assert pending["outcome"] == "claim_commit_required"
    assert pending["owner_present"] is False
    assert pending["controller_active"] is False
    accepted = value.commit_result("verified_exact")
    assert accepted["outcome"] == "accepted"
    assert accepted["owner_present"] is True
    assert accepted["controller_active"] is True


def test_weak_or_inexact_pairing_consumes_the_one_attempt() -> None:
    cases = (
        {"secure_connections": False},
        {"mitm_authenticated": False},
        {"bonded": False},
        {"key_bytes": 15},
        {"key_bytes": 17},
        {"candidate_binding_exact": False},
    )
    for changes in cases:
        value = model()
        open_window(value)
        rejected = secure_pair(value, **changes)
        if changes.get("bonded") is False:
            assert rejected["outcome"] == "pairing_security_rejected_no_bond"
            assert rejected["state"] == "closed_unowned"
        elif changes.get("candidate_binding_exact") is False:
            assert rejected["outcome"] == "candidate_binding_ambiguous"
            assert rejected["state"] == "reconcile_required"
        else:
            assert rejected["outcome"] == "pairing_cleanup_required"
            assert rejected["state"] == "candidate_cleanup_required"
        assert secure_pair(value, now_ms=2)["outcome"] == "window_closed"
        if rejected["state"] == "candidate_cleanup_required":
            cleaned = value.candidate_cleanup_result(verified_absent=True)
            assert cleaned["state"] == "closed_unowned"


def test_initial_commit_failure_and_uncertainty_are_distinct() -> None:
    known = model()
    open_window(known)
    secure_pair(known)
    retained = known.commit_result("known_no_change")
    assert retained["state"] == "candidate_cleanup_required"
    assert known.candidate_cleanup_result(verified_absent=True)["state"] == "closed_unowned"
    uncertain = model()
    open_window(uncertain)
    secure_pair(uncertain)
    result = uncertain.commit_result("uncertain")
    assert result["state"] == "reconcile_required"
    assert result["controller_active"] is False


def test_saved_owner_reconnect_and_single_controller() -> None:
    value = model(owner=True)
    denied = value.reconnect(
        now_ms=0,
        exact_owner_binding=False,
        secure_connections=True,
        mitm_authenticated=True,
        bonded=True,
        key_bytes=16,
        fresh_session=True,
    )
    assert denied["outcome"] == "reconnect_rejected"
    args = dict(
        now_ms=1,
        exact_owner_binding=True,
        secure_connections=True,
        mitm_authenticated=True,
        bonded=True,
        key_bytes=16,
        fresh_session=True,
    )
    assert value.reconnect(**args)["outcome"] == "authorized_reconnect"
    args["now_ms"] = 2
    assert value.reconnect(**args)["outcome"] == "controller_in_use"
    released = value.disconnect()
    assert released["state"] == "closed_owned"
    assert released["owner_present"] is True


def test_replacement_requires_distinct_candidate_and_second_gesture() -> None:
    same = model(owner=True)
    open_window(same)
    assert secure_pair(same, same_owner=True)["outcome"] == "same_owner_replacement_rejected"
    value = pending_replacement()
    assert value.commit_result("verified_exact")["outcome"] == "commit_not_pending"
    weak = value.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=2999, released=True
    )
    assert weak["outcome"] == "confirmation_gesture_rejected"
    confirmed = value.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    assert confirmed["outcome"] == "replacement_commit_required"


def test_replacement_confirmation_uses_original_deadline() -> None:
    value = pending_replacement()
    stale = value.confirm_replacement(
        now_ms=29999, physical_event=1, hold_ms=3000, released=True
    )
    assert stale["outcome"] == "stale_confirmation"
    expired = value.confirm_replacement(
        now_ms=30000, physical_event=2, hold_ms=3000, released=True
    )
    assert expired["outcome"] == "confirmation_expired_cleanup_required"
    assert expired["state"] == "candidate_cleanup_required"
    cleaned = value.candidate_cleanup_result(verified_absent=True)
    assert cleaned["state"] == "closed_owned"


def test_replacement_publishes_only_after_old_bond_cleanup() -> None:
    value = pending_replacement()
    value.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    committed = value.commit_result("verified_exact")
    assert committed["outcome"] == "old_bond_cleanup_required"
    assert committed["controller_active"] is False
    replaced = value.old_bond_cleanup_result(verified_absent=True)
    assert replaced["outcome"] == "replaced"
    assert replaced["controller_active"] is True


def test_replacement_abort_uncertainty_and_cleanup_failure_close() -> None:
    known = pending_replacement()
    known.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    retained = known.commit_result("known_no_change")
    assert retained["state"] == "candidate_cleanup_required"
    assert retained["owner_present"] is True
    assert known.candidate_cleanup_result(verified_absent=True)["state"] == "closed_owned"
    uncertain = pending_replacement()
    uncertain.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    assert uncertain.commit_result("uncertain")["state"] == "reconcile_required"
    cleanup = pending_replacement()
    cleanup.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    cleanup.commit_result("verified_exact")
    assert cleanup.old_bond_cleanup_result(verified_absent=False)["state"] == "reconcile_required"


def test_disconnect_after_candidate_or_during_commit_fails_closed() -> None:
    replacement = pending_replacement()
    disconnected = replacement.disconnect()
    assert disconnected["outcome"] == "disconnect_cleanup_required"
    assert disconnected["state"] == "candidate_cleanup_required"
    assert replacement.disconnect()["outcome"] == "candidate_cleanup_still_required"
    uncertain = replacement.candidate_cleanup_result(verified_absent=False)
    assert uncertain["state"] == "reconcile_required"
    assert uncertain["controller_active"] is False
    claim = model()
    open_window(claim)
    secure_pair(claim)
    interrupted = claim.disconnect()
    assert interrupted["outcome"] == "disconnect_commit_uncertain"
    assert interrupted["state"] == "reconcile_required"
    assert interrupted["controller_active"] is False


def test_restart_clears_transient_state_and_requires_exact_reconcile() -> None:
    value = model()
    open_window(value)
    restarted = value.restart()
    assert restarted["state"] == "boot_reconcile"
    assert restarted["pin_displayed"] is False
    assert restarted["bond_roster_reconciliation_required"] is True
    restored = value.restore(
        owner_present=False,
        exact_state=True,
        bond_roster_exact=True,
        orphan_candidate_present=False,
    )
    assert restored["state"] == "closed_unowned"
    owned = pending_replacement()
    assert owned.restart()["bond_roster_reconciliation_required"] is True
    unresolved = owned.restore(
        owner_present=True,
        exact_state=True,
        bond_roster_exact=False,
        orphan_candidate_present=False,
    )
    assert unresolved["state"] == "reconcile_required"
    recovered = owned.restore(
        owner_present=True,
        exact_state=True,
        bond_roster_exact=True,
        orphan_candidate_present=False,
    )
    assert recovered["state"] == "closed_owned"
    orphan = admission.PairingReplacementModel()
    result = orphan.restore(
        owner_present=True,
        exact_state=True,
        bond_roster_exact=True,
        orphan_candidate_present=True,
    )
    assert result["state"] == "reconcile_required"
    assert result["controller_active"] is False
    committed = pending_replacement()
    committed.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    committed.commit_result("verified_exact")
    committed.restart()
    pending_cleanup = committed.restore(
        owner_present=True,
        exact_state=True,
        bond_roster_exact=True,
        orphan_candidate_present=True,
    )
    assert pending_cleanup["state"] == "reconcile_required"
    assert pending_cleanup["controller_active"] is False


def test_clock_rollback_faults_without_controller() -> None:
    value = model(owner=True)
    open_window(value, now_ms=10)
    fault = value.tick(9)
    assert fault["state"] == "faulted"
    assert fault["controller_active"] is False
    assert fault["pin_displayed"] is False


def test_secure_random_and_uint64_clock_fail_closed() -> None:
    for ready, succeeded in ((False, True), (True, False)):
        value = model()
        denied = open_window(
            value,
            rng_ready=ready,
            rng_succeeded=succeeded,
        )
        assert denied["outcome"] == "secure_random_unavailable"
        assert denied["state"] == "closed_unowned"
        assert denied["pin_displayed"] is False
        assert value.pin_samples == 0
    malformed_rng = model()
    rejected = open_window(malformed_rng, rng_ready="true")
    assert rejected["outcome"] == "window_evidence_rejected"
    assert rejected["state"] == "closed_unowned"
    overflow = model()
    fault = open_window(
        overflow,
        now_ms=admission.MAX_MONOTONIC_MS - 29999,
    )
    assert fault["outcome"] == "deadline_overflow_faulted"
    assert fault["state"] == "faulted"
    upper_bound = model()
    opened = open_window(
        upper_bound,
        now_ms=admission.MAX_MONOTONIC_MS - 30000,
    )
    assert opened["state"] == "claim_window_open"
    assert upper_bound.tick(admission.MAX_MONOTONIC_MS)["state"] == "closed_unowned"
    invalid_range = model()
    assert invalid_range.tick(admission.MAX_MONOTONIC_MS + 1)["state"] == "faulted"
    try:
        admission.PairingReplacementModel(window_ms=30000.0)
    except ValueError:
        pass
    else:
        raise AssertionError("float window duration must be rejected")


def test_malformed_model_evidence_fails_closed() -> None:
    pairing_cases = (
        {"secure_connections": "true"},
        {"mitm_authenticated": 1},
        {"bonded": "true"},
        {"key_bytes": 16.0},
        {"candidate_binding_exact": "true"},
        {"same_owner": 0},
    )
    for changes in pairing_cases:
        value = model()
        open_window(value)
        rejected = secure_pair(value, **changes)
        assert rejected["outcome"] == "pairing_evidence_rejected"
        assert rejected["state"] == "reconcile_required"
        assert rejected["controller_active"] is False
    reconnect_args: dict[str, object] = {
        "now_ms": 0,
        "exact_owner_binding": True,
        "secure_connections": True,
        "mitm_authenticated": True,
        "bonded": True,
        "key_bytes": 16,
        "fresh_session": True,
    }
    for field, replacement in (
        ("exact_owner_binding", "true"),
        ("secure_connections", 1),
        ("mitm_authenticated", "true"),
        ("bonded", 1),
        ("key_bytes", 16.0),
        ("fresh_session", "true"),
    ):
        value = model(owner=True)
        evidence = dict(reconnect_args)
        evidence[field] = replacement
        rejected = value.reconnect(**evidence)
        assert rejected["outcome"] == "reconnect_evidence_rejected"
        assert rejected["state"] == "closed_owned"
        assert rejected["controller_active"] is False
    for field, replacement in (
        ("owner_present", "false"),
        ("exact_state", 1),
        ("bond_roster_exact", "true"),
        ("orphan_candidate_present", 0),
    ):
        value = admission.PairingReplacementModel()
        evidence: dict[str, object] = {
            "owner_present": False,
            "exact_state": True,
            "bond_roster_exact": True,
            "orphan_candidate_present": False,
        }
        evidence[field] = replacement
        rejected = value.restore(**evidence)
        assert rejected["outcome"] == "restore_evidence_rejected"
        assert rejected["state"] == "reconcile_required"
    candidate = model()
    open_window(candidate)
    secure_pair(candidate, secure_connections=False)
    rejected = candidate.candidate_cleanup_result(verified_absent="false")
    assert rejected["outcome"] == "candidate_cleanup_evidence_rejected"
    assert rejected["state"] == "candidate_cleanup_required"
    replacement = pending_replacement()
    replacement.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=True
    )
    replacement.commit_result("verified_exact")
    rejected = replacement.old_bond_cleanup_result(verified_absent="false")
    assert rejected["outcome"] == "cleanup_evidence_rejected"
    assert rejected["state"] == "commit_in_progress"
    assert rejected["controller_active"] is False
    confirmation = pending_replacement()
    rejected = confirmation.confirm_replacement(
        now_ms=2, physical_event=2, hold_ms=3000, released=1
    )
    assert rejected["outcome"] == "confirmation_evidence_rejected"
    assert rejected["state"] == "replacement_confirmation_pending"


def test_private_fields_and_cli_failures_are_sanitized() -> None:
    value = contract()
    value["pairing_pin"] = "123456"
    expect_error(value, "prohibited field name")
    missing_nested = contract()
    del missing_nested["pin"]["source"]
    expect_error(missing_nested, "structure or field types")
    wrong_nested_type = contract()
    wrong_nested_type["execution_authority"] = 0
    expect_error(wrong_nested_type, "structure or field types")
    command = [
        sys.executable,
        str(ROOT / "tools" / "ble_pairing_replacement_admission.py"),
        "validate-contract",
        "--input",
        str(CONTRACT_PATH),
    ]
    good = subprocess.run(command, check=False, capture_output=True, text=True)
    assert good.returncode == 0
    assert json.loads(good.stdout)["contract_status"] == "CONTRACT-FROZEN-HOST-ONLY"
    payloads = (
        b'{"schema":"OTBP0","schema":"OTBP0"}',
        json.dumps(missing_nested).encode("utf-8"),
        json.dumps(wrong_nested_type).encode("utf-8"),
    )
    with tempfile.TemporaryDirectory() as directory:
        for index, payload in enumerate(payloads):
            path = Path(directory) / f"private-COM44-{index}.json"
            path.write_bytes(payload)
            failed = subprocess.run(
                command[:-1] + [str(path)],
                check=False,
                capture_output=True,
                text=True,
            )
            assert failed.returncode == 2
            report = json.loads(failed.stderr)
            assert report["contract_status"] == "CONTRACT-INVALID"
            assert "Traceback" not in failed.stderr
            assert "COM44" not in failed.stderr
            assert str(path.parent) not in failed.stderr


def test_validator_and_reference_model_have_no_execution_surface() -> None:
    source = (ROOT / "tools" / "ble_pairing_replacement_admission.py").read_text(
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
        assert re.search(rf"\b{re.escape(forbidden)}\b", source) is None


def main() -> None:
    tests = [
        test_checked_in_contract_is_frozen_host_only,
        test_canonical_digest_is_exact_and_order_independent,
        test_exact_scope_security_storage_and_authority_fail_closed,
        test_physical_hold_release_and_replay_are_exact,
        test_window_boundary_is_29999_open_and_30000_closed,
        test_initial_claim_needs_physical_bond_binding_and_exact_commit,
        test_weak_or_inexact_pairing_consumes_the_one_attempt,
        test_initial_commit_failure_and_uncertainty_are_distinct,
        test_saved_owner_reconnect_and_single_controller,
        test_replacement_requires_distinct_candidate_and_second_gesture,
        test_replacement_confirmation_uses_original_deadline,
        test_replacement_publishes_only_after_old_bond_cleanup,
        test_replacement_abort_uncertainty_and_cleanup_failure_close,
        test_disconnect_after_candidate_or_during_commit_fails_closed,
        test_restart_clears_transient_state_and_requires_exact_reconcile,
        test_clock_rollback_faults_without_controller,
        test_secure_random_and_uint64_clock_fail_closed,
        test_malformed_model_evidence_fails_closed,
        test_private_fields_and_cli_failures_are_sanitized,
        test_validator_and_reference_model_have_no_execution_surface,
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} BLE pairing/replacement contract scenario groups")


if __name__ == "__main__":
    main()
