#!/usr/bin/env python3
"""Deterministic tests for the OTSL0 secure-LoRa host-only contract."""

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

import secure_lora_contract_admission as admission  # noqa: E402


CONTRACT_PATH = (
    ROOT
    / "tests"
    / "release-plans"
    / "OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0.json"
)
FRAME = b"opaque-sealed-frame"


class TextSubclass(str):
    pass


class DictSubclass(dict):
    pass


class ListSubclass(list):
    pass


def contract() -> dict:
    return json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))


def expect_error(value: dict, contains: str) -> None:
    try:
        admission.validate_contract(value)
    except admission.AdmissionError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected AdmissionError containing {contains!r}")


def inactive_lifecycle() -> admission.SecureLoraLifecycleModel:
    model = admission.SecureLoraLifecycleModel()
    report = model.restore(
        group_present=False,
        epoch=0,
        state_exact=True,
        peer_activation_exact=True,
        traffic_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["outcome"] == "restored_inactive"
    return model


def active_lifecycle(epoch: int = 1) -> admission.SecureLoraLifecycleModel:
    model = admission.SecureLoraLifecycleModel()
    report = model.restore(
        group_present=True,
        epoch=epoch,
        state_exact=True,
        peer_activation_exact=True,
        traffic_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["outcome"] == "restored_active"
    return model


def admit_invitation(
    model: admission.SecureLoraLifecycleModel,
    **changes: object,
) -> dict:
    evidence: dict[str, object] = {
        "invitation_authenticated": True,
        "invitation_binding_exact": True,
        "invitation_unused": True,
        "clock_exact": True,
        "now_ms": 90,
        "deadline_ms": 100,
        "invitation_sequence": 1,
        "attempt_number": 1,
    }
    evidence.update(changes)
    return model.open_invitation(**evidence)


def confirm_candidate(
    model: admission.SecureLoraLifecycleModel,
) -> admission.SecureLoraLifecycleModel:
    assert admit_invitation(model)["outcome"] == "invitation_admitted"
    assert model.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )["outcome"] == "mutual_authentication_verified"
    assert model.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=92,
        clock_exact=True,
    )["outcome"] == "material_release_and_commit_required"
    return model


def ready_transport(
    *,
    epoch: int = 1,
    attempts: int = 3,
) -> admission.DirectTransportModel:
    model = admission.DirectTransportModel(attempts)
    report = model.restore(
        lifecycle_active=True,
        epoch=epoch,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["outcome"] == "transport_ready"
    return model


def begin_outbound(
    model: admission.DirectTransportModel,
    *,
    epoch: int = 1,
    message_id: int = 10,
    counter: int = 20,
    frame: bytes = FRAME,
    **changes: object,
) -> dict:
    evidence: dict[str, object] = {
        "epoch": epoch,
        "message_id": message_id,
        "counter": counter,
        "derivation_context_exact": True,
        "direction_exact": True,
        "lease_exact": True,
        "nonce_binding_exact": True,
        "message_id_consumed_durably": True,
        "counter_consumed_durably": True,
        "sealed_frame": frame,
    }
    evidence.update(changes)
    return model.begin_outbound(**evidence)


def protected_ack(
    model: admission.DirectTransportModel,
    *,
    epoch: int = 1,
    **changes: object,
) -> dict:
    evidence: dict[str, object] = {
        "epoch": epoch,
        "authenticated": True,
        "encrypted": True,
        "integrity_exact": True,
        "source_destination_exact": True,
        "reverse_direction_exact": True,
        "correlation_exact": True,
        "replay_fresh": True,
        "deadline_valid": True,
        "semantic": "peer_device_durably_admitted",
    }
    evidence.update(changes)
    return model.acknowledge(**evidence)


def receive(
    model: admission.DirectTransportModel,
    *,
    epoch: int = 1,
    message_id: int = 30,
    counter: int = 40,
    frame: bytes = FRAME,
    **changes: object,
) -> dict:
    evidence: dict[str, object] = {
        "epoch": epoch,
        "message_id": message_id,
        "counter": counter,
        "sealed_frame": frame,
        "structurally_valid": True,
        "group_exact": True,
        "destination_exact": True,
        "authenticated": True,
        "integrity_exact": True,
        "source_exact": True,
        "direction_exact": True,
    }
    evidence.update(changes)
    return model.receive(**evidence)


def test_checked_in_contract_is_frozen_host_only() -> None:
    report = admission.validate_contract(contract())
    assert report == {
        "schema": "OTSL0",
        "version": 0,
        "artifact_kind": "secure_lora_contract_admission",
        "contract_id": "OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0",
        "contract_sha256": admission.CANONICAL_CONTRACT_SHA256,
        "contract_status": "CONTRACT-FROZEN-HOST-ONLY",
        "implementation_status": "NOT-IMPLEMENTED",
        "physical_acceptance_status": "NOT-EVALUATED",
        "execution_authority_granted": False,
        "score_credit_added": False,
        "open_followup_gates": admission.OPEN_FOLLOWUP_GATES,
    }


def test_canonical_digest_is_exact_and_order_independent() -> None:
    value = contract()
    assert admission.canonical_sha256(value) == admission.CANONICAL_CONTRACT_SHA256
    assert (
        admission.canonical_sha256(dict(reversed(list(value.items()))))
        == admission.CANONICAL_CONTRACT_SHA256
    )


def test_pairwise_scope_ble_boundary_and_selection_gate_are_exact() -> None:
    value = contract()
    assert value["scope"]["topology"] == "exact_two_node_pairwise_direct"
    assert value["scope"]["relay_allowed"] is False
    assert value["scope"]["broadcast_allowed"] is False
    phone = value["provisioning"]["phone_ble_boundary"]
    assert phone["may_carry_public_invitation_and_workflow_requests_only"] is True
    assert phone["may_carry_lora_private_material"] is False
    selection = value["selection_boundary"]
    assert all(
        selection[field]
        == "blocked_by_decision_0003_and_ot_005_exact_target_benchmark"
        for field in (
            "algorithm_selection",
            "crypto_library_selection",
            "kdf_selection",
            "packet_v1_wire_format",
        )
    )
    assert selection["packet_v0_security_claim_allowed"] is False
    assert selection["security_downgrade_allowed"] is False
    assert selection["random_nonce_fallback_allowed"] is False


def test_outer_derivation_context_and_purpose_domains_are_complete() -> None:
    context = contract()["identity_and_context"]
    assert context["outer_pairwise_derivation_context_required"] is True
    assert "both_full_32_byte_identity_fingerprints" in context[
        "outer_context_bindings"
    ]
    assert "ordered_sender_to_receiver_direction" in context[
        "outer_context_bindings"
    ]
    assert context["otkd_v1_role"].endswith(
        "not_sufficient_for_pairwise_destination_binding"
    )
    assert context["separate_output_purposes"] == [
        "identity_signing",
        "static_key_agreement",
        "ephemeral_key_agreement",
        "pairwise_root",
        "directional_data_aead",
        "directional_ack_aead",
        "directional_nonce_prefix",
        "directional_counter_domain_id",
    ]
    assert "ble_pairing_pin" in context["forbidden_derivation_inputs"]
    assert "qr_payload_alone" in context["forbidden_derivation_inputs"]


def test_critical_contract_mutations_fail_closed() -> None:
    mutations = (
        ("scope", "relay_allowed", True, "scope"),
        ("selection_boundary", "plaintext_transport_allowed", True, "authentication"),
        ("provisioning", "ready_before_both_verified_commits", True, "provisioning"),
        ("rekey_and_revocation", "epoch_wrap_allowed", True, "rekey"),
        (
            "storage_and_restart",
            "receiver_replay_checkpoint_durable_before_plaintext",
            False,
            "storage",
        ),
        ("direct_transport", "message_encryption_required", False, "transport"),
        ("direct_transport", "retry_reseals_or_reuses_nonce", True, "delivery"),
        ("execution_authority", "radio_transmission", True, "authority"),
        ("claims", "secure_lora_implemented", True, "claim"),
    )
    for section, field, replacement, expected in mutations:
        value = contract()
        value[section][field] = replacement
        expect_error(value, expected)


def test_initial_provisioning_requires_full_order_before_ready() -> None:
    model = confirm_candidate(inactive_lifecycle())
    assert model.state == "candidate_key_staged"
    committed = model.commit_provisioning("verified_exact")
    assert committed["outcome"] == "peer_activation_required"
    assert committed["traffic_allowed"] is False
    ready = model.activate_peer("verified_exact")
    assert ready["outcome"] == "provisioned_ready"
    assert ready["traffic_allowed"] is True


def test_invitation_deadline_clock_attempt_and_replay_burn() -> None:
    for changes in (
        {"now_ms": 100},
        {"now_ms": 101},
        {"clock_exact": False},
        {"attempt_number": 2},
        {"invitation_unused": False},
        {"deadline_ms": 0},
    ):
        model = inactive_lifecycle()
        rejected = admit_invitation(model, **changes)
        assert rejected["outcome"] == "invitation_rejected_burned"
        assert rejected["invitation_consumed"] is True
        assert admit_invitation(model)["outcome"] == "invitation_rejected_burned"
    fresh = inactive_lifecycle()
    assert admit_invitation(fresh, invitation_sequence=2)[
        "outcome"
    ] == "invitation_admitted"


def test_malformed_invitation_evidence_still_consumes_exact_sequence() -> None:
    model = inactive_lifecycle()
    malformed = admit_invitation(
        model,
        invitation_sequence=7,
        invitation_authenticated=1,
    )
    assert malformed["outcome"] == "invitation_evidence_rejected_burned"
    replay = admit_invitation(model, invitation_sequence=7)
    assert replay["outcome"] == "invitation_rejected_burned"
    fresh = admit_invitation(model, invitation_sequence=8)
    assert fresh["outcome"] == "invitation_admitted"


def test_invitation_deadline_clock_and_busy_replay_cannot_resurrect() -> None:
    same = inactive_lifecycle()
    admit_invitation(same, invitation_sequence=1)
    replay = admit_invitation(same, invitation_sequence=1)
    assert replay["outcome"] == "active_invitation_replay_burned"
    assert replay["state"] == "inactive_no_group"
    assert same.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )["outcome"] == "handshake_not_pending"

    expired = inactive_lifecycle()
    admit_invitation(expired)
    report = expired.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=100,
        clock_exact=True,
    )
    assert report["outcome"] == "handshake_clock_or_deadline_rejected_burned"
    assert expired.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=99,
        clock_exact=True,
    )["outcome"] == "handshake_not_pending"

    rollback = inactive_lifecycle()
    admit_invitation(rollback)
    rollback.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=95,
        clock_exact=True,
    )
    report = rollback.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=94,
        clock_exact=True,
    )
    assert report["outcome"] == "confirmation_clock_or_deadline_rejected_burned"

    busy = inactive_lifecycle()
    admit_invitation(busy, invitation_sequence=1)
    assert admit_invitation(busy, invitation_sequence=2)[
        "outcome"
    ] == "invitation_not_available"
    assert busy.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )["outcome"] == "mutual_authentication_verified"
    busy.confirm_transcript(
        local_confirmed=False,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=92,
        clock_exact=True,
    )
    assert admit_invitation(busy, invitation_sequence=2)[
        "outcome"
    ] == "invitation_rejected_burned"

    confirmed = inactive_lifecycle()
    admit_invitation(confirmed, invitation_sequence=3)
    confirmed.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )
    replay = admit_invitation(confirmed, invitation_sequence=3, now_ms=91)
    assert replay["outcome"] == "active_invitation_replay_burned"
    assert confirmed.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=92,
        clock_exact=True,
    )["outcome"] == "confirmation_not_pending"

    busy_expired = inactive_lifecycle()
    admit_invitation(busy_expired, invitation_sequence=4)
    report = admit_invitation(
        busy_expired,
        invitation_sequence=5,
        now_ms=101,
        deadline_ms=200,
    )
    assert report["outcome"] == (
        "active_invitation_clock_or_deadline_rejected_burned"
    )
    assert report["state"] == "inactive_no_group"
    assert busy_expired.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )["outcome"] == "handshake_not_pending"
    assert admit_invitation(busy_expired, invitation_sequence=5)[
        "outcome"
    ] == "invitation_rejected_burned"

    wrong_phase_expired = inactive_lifecycle()
    admit_invitation(wrong_phase_expired, invitation_sequence=6)
    assert wrong_phase_expired.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )["outcome"] == "mutual_authentication_verified"
    assert wrong_phase_expired.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=101,
        clock_exact=True,
    )["outcome"] == "handshake_clock_or_deadline_rejected_burned"
    assert wrong_phase_expired.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=92,
        clock_exact=True,
    )["outcome"] == "confirmation_not_pending"

    premature = inactive_lifecycle()
    admit_invitation(premature, invitation_sequence=7)
    report = premature.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=91,
        clock_exact=True,
    )
    assert report["outcome"] == "confirmation_wrong_phase_burned"
    assert premature.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=92,
        clock_exact=True,
    )["outcome"] == "handshake_not_pending"

    duplicate_handshake = inactive_lifecycle()
    admit_invitation(duplicate_handshake, invitation_sequence=8)
    duplicate_handshake.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )
    report = duplicate_handshake.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=92,
        clock_exact=True,
    )
    assert report["outcome"] == "handshake_wrong_phase_or_replay_burned"
    assert duplicate_handshake.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=True,
        transcript_exact=True,
        now_ms=93,
        clock_exact=True,
    )["outcome"] == "confirmation_not_pending"


def test_handshake_and_two_local_confirmations_precede_material() -> None:
    model = inactive_lifecycle()
    admit_invitation(model)
    rejected = model.handshake_result(
        mutually_authenticated=False,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )
    assert rejected["outcome"] == "handshake_rejected_burned"
    assert rejected["current_epoch_present"] is False

    model = inactive_lifecycle()
    admit_invitation(model)
    model.handshake_result(
        mutually_authenticated=True,
        peer_binding_exact=True,
        invitation_context_exact=True,
        now_ms=91,
        clock_exact=True,
    )
    rejected = model.confirm_transcript(
        local_confirmed=True,
        peer_confirmed=False,
        transcript_exact=True,
        now_ms=92,
        clock_exact=True,
    )
    assert rejected["outcome"] == "confirmation_rejected_burned"
    assert rejected["current_epoch_present"] is False


def test_provisioning_known_no_change_and_ambiguity_are_distinct() -> None:
    known = confirm_candidate(inactive_lifecycle())
    report = known.commit_provisioning("known_no_change")
    assert report["outcome"] == "provisioning_known_no_change"
    assert report["state"] == "inactive_no_group"
    uncertain = confirm_candidate(inactive_lifecycle())
    report = uncertain.commit_provisioning("possible_mutation")
    assert report["outcome"] == "provisioning_commit_uncertain"
    assert report["reconciliation_required"] is True


def test_peer_activation_ambiguity_and_restart_fail_closed() -> None:
    model = confirm_candidate(inactive_lifecycle())
    model.commit_provisioning("verified_exact")
    uncertain = model.activate_peer("known_no_change")
    assert uncertain["reconciliation_required"] is True
    assert uncertain["traffic_allowed"] is False

    restarted = confirm_candidate(inactive_lifecycle())
    assert restarted.restart()["outcome"] == "restart_reconcile_required"
    assert restarted.invitation_consumed is True
    restored = restarted.restore(
        group_present=False,
        epoch=0,
        state_exact=True,
        peer_activation_exact=True,
        traffic_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert restored["outcome"] == "restored_inactive"


def test_rekey_and_revocation_block_traffic_until_retirement() -> None:
    model = active_lifecycle()
    staged = model.begin_rekey(
        next_epoch=2,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=True,
    )
    assert staged["outcome"] == "rekey_staged_traffic_blocked"
    assert staged["traffic_allowed"] is False
    activated = model.activate_rekey("verified_exact")
    assert activated["outcome"] == "new_epoch_active_old_epoch_rejected"
    assert activated["traffic_allowed"] is False
    ready = model.retire_old_epoch("verified_exact")
    assert ready["outcome"] == "rekey_ready"
    assert ready["traffic_allowed"] is True


def test_rekey_known_no_change_ambiguity_wrap_and_no_fallback() -> None:
    known = active_lifecycle()
    known.begin_rekey(
        next_epoch=2,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=False,
    )
    assert known.activate_rekey("known_no_change")["outcome"] == "old_epoch_retained"

    uncertain = active_lifecycle()
    uncertain.begin_rekey(
        next_epoch=2,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=True,
    )
    assert uncertain.activate_rekey("possible_mutation")[
        "reconciliation_required"
    ] is True

    exhausted = active_lifecycle(admission.MAX_EPOCH)
    assert exhausted.begin_rekey(
        next_epoch=0,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=True,
    )["state"] == "faulted"

    no_fallback = active_lifecycle()
    no_fallback.begin_rekey(
        next_epoch=2,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=True,
    )
    no_fallback.activate_rekey("verified_exact")
    no_fallback.retire_old_epoch("uncertain")
    report = no_fallback.restore(
        group_present=True,
        epoch=1,
        state_exact=True,
        peer_activation_exact=True,
        traffic_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["reconciliation_required"] is True


def test_transport_restore_requires_all_exact_durable_state() -> None:
    for field in (
        "lifecycle_active",
        "derivation_context_exact",
        "counter_lease_exact",
        "replay_checkpoint_exact",
    ):
        model = admission.DirectTransportModel(3)
        evidence = {
            "lifecycle_active": True,
            "epoch": 1,
            "derivation_context_exact": True,
            "counter_lease_exact": True,
            "replay_checkpoint_exact": True,
        }
        evidence[field] = False
        report = model.restore(**evidence)
        assert report["reconciliation_required"] is True
    for attempts in (0, -1, True, 256):
        try:
            admission.DirectTransportModel(attempts)
        except ValueError:
            pass
        else:
            raise AssertionError("non-finite host attempt policy was accepted")


def test_outbound_needs_durable_context_counter_and_exact_seal() -> None:
    for field in (
        "derivation_context_exact",
        "direction_exact",
        "lease_exact",
        "nonce_binding_exact",
        "message_id_consumed_durably",
        "counter_consumed_durably",
    ):
        model = ready_transport()
        report = begin_outbound(model, **{field: False})
        assert report["outcome"] == "outbound_admission_rejected"
        assert report["state"] == "ready"
    model = ready_transport()
    assert begin_outbound(model, frame=b"")["outcome"] == "outbound_admission_rejected"
    model = ready_transport()
    assert begin_outbound(model, frame=b"x" * 256)[
        "outcome"
    ] == "outbound_admission_rejected"


def test_durably_consumed_outbound_values_survive_later_admission_failure() -> None:
    model = ready_transport()
    rejected = begin_outbound(model, derivation_context_exact=False)
    assert rejected["outcome"] == "outbound_admission_rejected"
    reused = begin_outbound(model)
    assert reused["outcome"] == "outbound_admission_rejected"
    fresh = begin_outbound(model, message_id=11, counter=21)
    assert fresh["outcome"] == "sealed_once_pending_transmit"

    malformed = ready_transport()
    rejected = begin_outbound(malformed, derivation_context_exact=1)
    assert rejected["outcome"] == "outbound_evidence_rejected"
    assert begin_outbound(malformed)["outcome"] == "outbound_admission_rejected"


def test_each_durable_outbound_identifier_burns_independently() -> None:
    malformed_counter_flag = ready_transport()
    rejected = begin_outbound(
        malformed_counter_flag,
        message_id=41,
        counter=42,
        message_id_consumed_durably=True,
        counter_consumed_durably=1,
    )
    assert rejected["outcome"] == "outbound_evidence_rejected"
    assert begin_outbound(
        malformed_counter_flag,
        message_id=41,
        counter=42,
    )["outcome"] == "outbound_admission_rejected"

    malformed_message_flag = ready_transport()
    rejected = begin_outbound(
        malformed_message_flag,
        message_id=51,
        counter=52,
        message_id_consumed_durably=1,
        counter_consumed_durably=True,
    )
    assert rejected["outcome"] == "outbound_evidence_rejected"
    assert begin_outbound(
        malformed_message_flag,
        message_id=51,
        counter=52,
    )["outcome"] == "outbound_admission_rejected"

    invalid_counter = ready_transport()
    assert begin_outbound(
        invalid_counter,
        message_id=61,
        counter=0,
    )["outcome"] == "outbound_admission_rejected"
    assert begin_outbound(
        invalid_counter,
        message_id=61,
        counter=62,
    )["outcome"] == "outbound_admission_rejected"

    invalid_message = ready_transport()
    assert begin_outbound(
        invalid_message,
        message_id=0,
        counter=72,
    )["outcome"] == "outbound_admission_rejected"
    assert begin_outbound(
        invalid_message,
        message_id=71,
        counter=72,
    )["outcome"] == "outbound_admission_rejected"


def test_pending_state_still_burns_new_durable_outbound_values() -> None:
    model = ready_transport()
    assert begin_outbound(
        model,
        message_id=10,
        counter=20,
    )["outcome"] == "sealed_once_pending_transmit"
    assert model.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=True,
    )["outcome"] == "queued_not_delivered"
    pending = begin_outbound(
        model,
        message_id=11,
        counter=21,
    )
    assert pending["outcome"] == "outbound_not_available"
    assert protected_ack(model)["outcome"] == "delivery_confirmed"
    reused = begin_outbound(
        model,
        message_id=11,
        counter=21,
    )
    assert reused["outcome"] == "outbound_admission_rejected"


def test_queue_is_not_delivery_and_protected_ack_is_required() -> None:
    model = ready_transport()
    assert begin_outbound(model)["outcome"] == "sealed_once_pending_transmit"
    queued = model.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=True,
    )
    assert queued["outcome"] == "queued_not_delivered"
    assert queued["delivery_confirmed"] is False
    confirmed = protected_ack(model)
    assert confirmed["outcome"] == "delivery_confirmed"
    assert confirmed["delivery_confirmed"] is True
    assert confirmed["state"] == "ready"


def test_ack_requires_send_reverse_direction_epoch_replay_deadline_and_semantic() -> None:
    cases = (
        {"reverse_direction_exact": False},
        {"replay_fresh": False},
        {"epoch": 2},
        {"semantic": "phone_displayed"},
        {"correlation_exact": False},
        {"encrypted": False},
    )
    for changes in cases:
        model = ready_transport()
        begin_outbound(model)
        model.transmit_attempt(exact_sealed_frame=FRAME, transport_queued=True)
        report = protected_ack(model, **changes)
        assert report["outcome"] == "unexpected_ack_rejected"
        assert report["delivery_confirmed"] is False
    before_send = ready_transport()
    begin_outbound(before_send)
    assert protected_ack(before_send)["outcome"] == "unexpected_ack_rejected"


def test_transport_rejection_does_not_make_ack_eligible() -> None:
    model = ready_transport()
    begin_outbound(model)
    rejected = model.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=False,
    )
    assert rejected["outcome"] == "transport_rejected_attempt_bounded"
    assert protected_ack(model)["outcome"] == "unexpected_ack_rejected"
    queued = model.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=True,
    )
    assert queued["outcome"] == "queued_not_delivered"
    assert protected_ack(model)["outcome"] == "delivery_confirmed"


def test_ack_deadline_expiry_is_terminal_even_with_malformed_siblings() -> None:
    model = ready_transport()
    begin_outbound(model)
    model.transmit_attempt(exact_sealed_frame=FRAME, transport_queued=True)
    expired = protected_ack(
        model,
        authenticated=1,
        deadline_valid=False,
        semantic=TextSubclass("peer_device_durably_admitted"),
    )
    assert expired["outcome"] == "ack_deadline_expired"
    assert expired["state"] == "ready"
    assert expired["delivery_confirmed"] is False
    assert protected_ack(model)["outcome"] == "ack_not_pending"
    assert begin_outbound(model)["outcome"] == "outbound_admission_rejected"


def test_retry_is_exact_byte_and_finite_without_freezing_production_count() -> None:
    mismatch_queued = ready_transport(attempts=3)
    begin_outbound(mismatch_queued)
    mismatch = mismatch_queued.transmit_attempt(
        exact_sealed_frame=b"different-seal",
        transport_queued=True,
    )
    assert mismatch["outcome"] == "queued_mismatched_sealed_bytes_uncertain"
    assert mismatch["reconciliation_required"] is True
    assert mismatch["attempts"] == 0
    mismatch_queued.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert begin_outbound(mismatch_queued)[
        "outcome"
    ] == "outbound_admission_rejected"

    malformed_queued = ready_transport(attempts=3)
    begin_outbound(malformed_queued, message_id=11, counter=21)
    report = malformed_queued.transmit_attempt(
        exact_sealed_frame=bytearray(FRAME),
        transport_queued=True,
    )
    assert report["outcome"] == "queued_unknown_sealed_bytes_uncertain"
    assert report["reconciliation_required"] is True

    out_of_state = ready_transport(attempts=3)
    report = out_of_state.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=True,
    )
    assert report["outcome"] == "out_of_order_transport_queue_uncertain"
    assert report["reconciliation_required"] is True

    model = ready_transport(attempts=3)
    begin_outbound(model)
    mismatch = model.transmit_attempt(
        exact_sealed_frame=b"different-seal",
        transport_queued=False,
    )
    assert mismatch["outcome"] == "sealed_bytes_mismatch_rejected"
    assert mismatch["attempts"] == 0
    for expected in (1, 2, 3):
        report = model.transmit_attempt(
            exact_sealed_frame=FRAME,
            transport_queued=True,
        )
        assert report["attempts"] == expected
        assert report["delivery_confirmed"] is False
    exhausted = model.transmit_attempt(
        exact_sealed_frame=FRAME,
        transport_queued=False,
    )
    assert exhausted["outcome"] == "attempts_exhausted"
    assert exhausted["state"] == "ready"
    assert contract()["direct_transport"]["transmission_attempt_limit"].endswith(
        "_pending"
    )


def test_restart_discards_pending_retry_but_consumed_values_cannot_recur() -> None:
    model = ready_transport()
    begin_outbound(model)
    model.transmit_attempt(exact_sealed_frame=FRAME, transport_queued=True)
    restarted = model.restart()
    assert restarted["outcome"] == "interrupted_delivery_unknown_no_retry"
    model.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    reused = begin_outbound(model)
    assert reused["outcome"] == "outbound_admission_rejected"
    fresh = begin_outbound(model, message_id=11, counter=21)
    assert fresh["outcome"] == "sealed_once_pending_transmit"


def test_inbound_replay_persistence_precedes_plaintext_and_ack() -> None:
    model = ready_transport()
    pending = receive(model)
    assert pending["outcome"] == "replay_persistence_required"
    assert pending["plaintext_released"] is False
    assert pending["protected_ack_required"] is False
    admitted = model.persist_inbound("verified_exact")
    assert admitted["outcome"] == "plaintext_released_once"
    assert admitted["plaintext_released"] is True
    assert admitted["protected_ack_required"] is True


def test_malformed_wrong_context_and_unauthenticated_never_release_plaintext() -> None:
    cases = (
        {"structurally_valid": False, "expected": "malformed_rejected"},
        {"group_exact": False, "expected": "wrong_context_rejected"},
        {"destination_exact": False, "expected": "wrong_context_rejected"},
        {"epoch": 2, "expected": "wrong_context_rejected"},
        {"authenticated": False, "expected": "unauthenticated_rejected"},
        {"integrity_exact": False, "expected": "unauthenticated_rejected"},
        {"source_exact": False, "expected": "unauthenticated_rejected"},
        {"direction_exact": False, "expected": "unauthenticated_rejected"},
    )
    for case in cases:
        expected = case.pop("expected")
        report = receive(ready_transport(), **case)
        assert report["outcome"] == expected
        assert report["plaintext_released"] is False
        assert report["protected_ack_required"] is False


def test_exact_duplicate_is_suppressed_and_conflicts_are_rejected() -> None:
    model = ready_transport()
    receive(model)
    model.persist_inbound("verified_exact")
    duplicate = receive(model)
    assert duplicate["outcome"] == "exact_duplicate_suppressed"
    assert duplicate["plaintext_released"] is False
    assert duplicate["protected_ack_required"] is True
    for changes in (
        {"counter": 41},
        {"frame": b"conflicting-seal"},
        {"message_id": 31},
    ):
        conflict = receive(model, **changes)
        assert conflict["outcome"] == "conflicting_duplicate_rejected"
        assert conflict["protected_ack_required"] is False


def test_inbound_storage_failure_ambiguity_and_restart_are_fail_closed() -> None:
    known = ready_transport()
    receive(known)
    report = known.persist_inbound("known_no_change")
    assert report["outcome"] == "persistence_failed_no_plaintext"
    assert report["state"] == "ready"
    assert report["plaintext_released"] is False

    uncertain = ready_transport()
    receive(uncertain)
    report = uncertain.persist_inbound("possible_mutation")
    assert report["reconciliation_required"] is True
    assert report["plaintext_released"] is False
    assert uncertain.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )["outcome"] == "transport_ready"
    replay = receive(uncertain)
    assert replay["outcome"] == "exact_duplicate_suppressed"
    assert replay["plaintext_released"] is False

    durable = ready_transport()
    receive(durable)
    durable.persist_inbound("verified_exact")
    durable.restart()
    durable.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    duplicate = receive(durable)
    assert duplicate["outcome"] == "exact_duplicate_suppressed"
    assert duplicate["plaintext_released"] is False


def test_authoritative_categorical_results_require_exact_builtin_strings() -> None:
    for token in ("known_no_change", "verified_exact"):
        lifecycle = confirm_candidate(inactive_lifecycle())
        report = lifecycle.commit_provisioning(TextSubclass(token))
        assert report["reconciliation_required"] is True

    activation = confirm_candidate(inactive_lifecycle())
    activation.commit_provisioning("verified_exact")
    assert activation.activate_peer(TextSubclass("verified_exact"))[
        "reconciliation_required"
    ] is True

    for token in ("known_no_change", "verified_exact"):
        lifecycle = active_lifecycle()
        lifecycle.begin_rekey(
            next_epoch=2,
            fresh_material=True,
            domains_distinct=True,
            retained_peer_exact=True,
            revoked_peer_excluded=True,
            revocation=True,
        )
        assert lifecycle.activate_rekey(TextSubclass(token))[
            "reconciliation_required"
        ] is True

    retirement = active_lifecycle()
    retirement.begin_rekey(
        next_epoch=2,
        fresh_material=True,
        domains_distinct=True,
        retained_peer_exact=True,
        revoked_peer_excluded=True,
        revocation=True,
    )
    retirement.activate_rekey("verified_exact")
    assert retirement.retire_old_epoch(TextSubclass("verified_exact"))[
        "reconciliation_required"
    ] is True

    for token in ("known_no_change", "verified_exact"):
        transport = ready_transport()
        receive(transport)
        report = transport.persist_inbound(TextSubclass(token))
        assert report["reconciliation_required"] is True
        assert report["plaintext_released"] is False

    transport = ready_transport()
    begin_outbound(transport)
    transport.transmit_attempt(exact_sealed_frame=FRAME, transport_queued=True)
    report = protected_ack(
        transport,
        semantic=TextSubclass("peer_device_durably_admitted"),
    )
    assert report["outcome"] == "ack_evidence_rejected"
    assert report["delivery_confirmed"] is False


def test_out_of_order_authoritative_mutation_results_force_reconciliation() -> None:
    lifecycle_calls = (
        lambda model: model.commit_provisioning("verified_exact"),
        lambda model: model.activate_peer("verified_exact"),
        lambda model: model.activate_rekey("verified_exact"),
        lambda model: model.retire_old_epoch("verified_exact"),
    )
    for call in lifecycle_calls:
        model = active_lifecycle()
        report = call(model)
        assert report["reconciliation_required"] is True
        assert report["traffic_allowed"] is False

    no_change = active_lifecycle()
    report = no_change.activate_rekey("known_no_change")
    assert report["outcome"] == "rekey_activation_not_pending"
    assert report["traffic_allowed"] is True

    transport = ready_transport()
    report = transport.persist_inbound("verified_exact")
    assert report["reconciliation_required"] is True
    assert report["traffic_ready"] is False

    no_change_transport = ready_transport()
    report = no_change_transport.persist_inbound("known_no_change")
    assert report["outcome"] == "inbound_persistence_not_pending"
    assert report["traffic_ready"] is True


def test_inactive_restore_requires_every_exact_absence_proof() -> None:
    fields = (
        "state_exact",
        "peer_activation_exact",
        "traffic_context_exact",
        "counter_lease_exact",
        "replay_checkpoint_exact",
    )
    for field in fields:
        model = admission.SecureLoraLifecycleModel()
        evidence = {
            "group_present": False,
            "epoch": 0,
            "state_exact": True,
            "peer_activation_exact": True,
            "traffic_context_exact": True,
            "counter_lease_exact": True,
            "replay_checkpoint_exact": True,
        }
        evidence[field] = False
        report = model.restore(**evidence)
        assert report["outcome"] == "inactive_state_inexact"
        assert report["reconciliation_required"] is True
        assert report["traffic_allowed"] is False

    active_same = active_lifecycle(epoch=1)
    report = active_same.restore(
        group_present=True,
        epoch=1,
        state_exact=True,
        peer_activation_exact=True,
        traffic_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["outcome"] == "restore_not_available"
    assert report["traffic_allowed"] is True

    for field, value in (
        ("group_present", False),
        ("state_exact", False),
        ("peer_activation_exact", False),
        ("traffic_context_exact", False),
        ("counter_lease_exact", False),
        ("replay_checkpoint_exact", False),
        ("counter_lease_exact", 1),
        ("epoch", 2),
    ):
        active = active_lifecycle(epoch=1)
        evidence = {
            "group_present": True,
            "epoch": 1,
            "state_exact": True,
            "peer_activation_exact": True,
            "traffic_context_exact": True,
            "counter_lease_exact": True,
            "replay_checkpoint_exact": True,
        }
        evidence[field] = value
        report = active.restore(**evidence)
        assert report["outcome"] == "restore_out_of_order_uncertain"
        assert report["reconciliation_required"] is True
        assert report["traffic_allowed"] is False


def test_transport_restart_rejects_epoch_rollback_and_requires_explicit_forward() -> None:
    live_forward = ready_transport(epoch=1)
    report = live_forward.restore(
        lifecycle_active=True,
        epoch=2,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
        forward_rekey_exact=True,
    )
    assert report["outcome"] == "transport_restore_out_of_order_uncertain"
    assert report["reconciliation_required"] is True
    assert begin_outbound(live_forward, epoch=1)[
        "outcome"
    ] == "outbound_not_available"

    live_conflict = ready_transport(epoch=1)
    report = live_conflict.restore(
        lifecycle_active=True,
        epoch=2,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["reconciliation_required"] is True
    assert begin_outbound(live_conflict, epoch=1)[
        "outcome"
    ] == "outbound_not_available"

    for field, value in (
        ("lifecycle_active", False),
        ("derivation_context_exact", False),
        ("counter_lease_exact", False),
        ("replay_checkpoint_exact", False),
        ("counter_lease_exact", 1),
    ):
        live_inexact = ready_transport(epoch=1)
        evidence = {
            "lifecycle_active": True,
            "epoch": 1,
            "derivation_context_exact": True,
            "counter_lease_exact": True,
            "replay_checkpoint_exact": True,
        }
        evidence[field] = value
        report = live_inexact.restore(**evidence)
        assert report["reconciliation_required"] is True
        assert begin_outbound(live_inexact, epoch=1)[
            "outcome"
        ] == "outbound_not_available"

    live_same = ready_transport(epoch=1)
    report = live_same.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert report["outcome"] == "restore_not_available"
    assert report["traffic_ready"] is True

    rollback = ready_transport(epoch=2)
    rollback.restart()
    denied = rollback.restore(
        lifecycle_active=True,
        epoch=1,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
    )
    assert denied["reconciliation_required"] is True
    assert begin_outbound(
        rollback,
        epoch=1,
        message_id=81,
        counter=91,
    )["outcome"] == "outbound_not_available"

    forward = ready_transport(epoch=1)
    forward.restart()
    restored = forward.restore(
        lifecycle_active=True,
        epoch=2,
        derivation_context_exact=True,
        counter_lease_exact=True,
        replay_checkpoint_exact=True,
        forward_rekey_exact=True,
    )
    assert restored["outcome"] == "transport_ready"
    assert begin_outbound(
        forward,
        epoch=2,
        message_id=82,
        counter=92,
    )["outcome"] == "sealed_once_pending_transmit"


def test_model_evidence_types_fail_closed_without_secret_echo() -> None:
    lifecycle = inactive_lifecycle()
    report = admit_invitation(lifecycle, invitation_authenticated=1)
    assert report["outcome"] == "invitation_evidence_rejected_burned"
    transport = ready_transport()
    report = begin_outbound(transport, message_id=True)
    assert report["outcome"] == "outbound_evidence_rejected"
    report = receive(transport, authenticated=1)
    assert report["outcome"] == "inbound_evidence_rejected"
    assert FRAME.decode() not in json.dumps(report)


def test_private_fields_noncanonical_types_and_shapes_are_rejected() -> None:
    value = contract()
    value["private_key"] = "not-public"
    expect_error(value, "prohibited field name")
    value = contract()
    value["version"] = True
    expect_error(value, "version type")
    value = contract()
    value["scope"]["relay_allowed"] = 0
    expect_error(value, "scope")
    value = contract()
    value["claims"] = []
    expect_error(value, "object")
    value = contract()
    value["public_summary"] = 1.5
    expect_error(value, "noncanonical value type")


def test_container_subclasses_are_never_canonical_contract_types() -> None:
    root = DictSubclass(contract())
    expect_error(root, "noncanonical value type")
    nested = contract()
    nested["rekey_and_revocation"] = DictSubclass(
        nested["rekey_and_revocation"]
    )
    expect_error(nested, "noncanonical value type")
    listed = contract()
    listed["open_followup_gates"] = ListSubclass(listed["open_followup_gates"])
    expect_error(listed, "noncanonical value type")


def test_direct_validator_bounds_deep_and_cyclic_containers() -> None:
    deep: list[object] = []
    cursor = deep
    for _ in range(2000):
        child: list[object] = []
        cursor.append(child)
        cursor = child
    value = contract()
    value["open_followup_gates"] = deep
    expect_error(value, "nesting limit")

    cyclic: list[object] = []
    cyclic.append(cyclic)
    value = contract()
    value["open_followup_gates"] = cyclic
    expect_error(value, "nesting limit")

    huge_integer = contract()
    huge_integer["identity_and_context"] = {"adversarial": 10**5000}
    expect_error(huge_integer, "structure or field types")


def test_cli_loader_is_bounded_deterministic_and_sanitized() -> None:
    command = [
        sys.executable,
        str(ROOT / "tools" / "secure_lora_contract_admission.py"),
        "validate-contract",
        "--input",
        str(CONTRACT_PATH),
    ]
    first = subprocess.run(command, check=False, capture_output=True, text=True)
    second = subprocess.run(command, check=False, capture_output=True, text=True)
    assert first.returncode == 0
    assert first.stdout == second.stdout
    assert json.loads(first.stdout)["contract_status"] == "CONTRACT-FROZEN-HOST-ONLY"
    tool_command = command[:2]
    hostile_argv = (
        ["private-COM44-command"],
        [
            "validate-contract",
            "--input",
            str(CONTRACT_PATH),
            "--private-COM44-option=not-public",
        ],
        ["validate-contract", "--input", "private-COM44-missing"],
    )
    for arguments in hostile_argv:
        failed = subprocess.run(
            tool_command + arguments,
            check=False,
            capture_output=True,
            text=True,
        )
        assert failed.returncode == 2
        report = json.loads(failed.stderr)
        assert report["contract_status"] == "CONTRACT-INVALID"
        assert report["error"] in {
            "command arguments are invalid",
            "contract JSON could not be read",
        }
        assert "COM44" not in failed.stderr
        assert "usage:" not in failed.stderr.lower()
        assert "Traceback" not in failed.stderr
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cases = {
            "private-COM44-duplicate.json": b'{"schema":"OTSL0","schema":"OTSL0"}',
            "private-COM44-utf8.json": b"\xff\xfe",
            "private-COM44-large.json": b" " * (admission.MAX_CONTRACT_BYTES + 1),
        }
        for name, payload in cases.items():
            path = root / name
            path.write_bytes(payload)
            failed = subprocess.run(
                command[:-1] + [str(path)],
                check=False,
                capture_output=True,
                text=True,
            )
            assert failed.returncode == 2
            assert json.loads(failed.stderr)["contract_status"] == "CONTRACT-INVALID"
            assert "Traceback" not in failed.stderr
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


def test_loader_reads_only_limit_plus_one_before_rejecting() -> None:
    read_sizes: list[int] = []

    class ProbeStream:
        def __enter__(self) -> "ProbeStream":
            return self

        def __exit__(self, *args: object) -> None:
            return None

        def read(self, size: int) -> bytes:
            read_sizes.append(size)
            return b" " * size

    class ProbePath:
        def open(self, mode: str) -> ProbeStream:
            assert mode == "rb"
            return ProbeStream()

    try:
        admission.load_contract(ProbePath())  # type: ignore[arg-type]
    except admission.AdmissionError as exc:
        assert "65536-byte limit" in str(exc)
    else:
        raise AssertionError("oversize capped read was accepted")
    assert read_sizes == [admission.MAX_CONTRACT_BYTES + 1]


def test_validator_and_models_have_no_execution_or_write_surface() -> None:
    source = (
        ROOT / "tools" / "secure_lora_contract_admission.py"
    ).read_text(encoding="utf-8")
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
        "serial",
        "bleak",
        "adb",
        "esptool",
    ):
        assert re.search(rf"\b{re.escape(forbidden)}\b", source) is None
    assert ".read(MAX_CONTRACT_BYTES + 1)" in source
    assert ".read_bytes()" not in source


def main() -> None:
    tests = [
        test_checked_in_contract_is_frozen_host_only,
        test_canonical_digest_is_exact_and_order_independent,
        test_pairwise_scope_ble_boundary_and_selection_gate_are_exact,
        test_outer_derivation_context_and_purpose_domains_are_complete,
        test_critical_contract_mutations_fail_closed,
        test_initial_provisioning_requires_full_order_before_ready,
        test_invitation_deadline_clock_attempt_and_replay_burn,
        test_malformed_invitation_evidence_still_consumes_exact_sequence,
        test_invitation_deadline_clock_and_busy_replay_cannot_resurrect,
        test_handshake_and_two_local_confirmations_precede_material,
        test_provisioning_known_no_change_and_ambiguity_are_distinct,
        test_peer_activation_ambiguity_and_restart_fail_closed,
        test_rekey_and_revocation_block_traffic_until_retirement,
        test_rekey_known_no_change_ambiguity_wrap_and_no_fallback,
        test_transport_restore_requires_all_exact_durable_state,
        test_outbound_needs_durable_context_counter_and_exact_seal,
        test_durably_consumed_outbound_values_survive_later_admission_failure,
        test_each_durable_outbound_identifier_burns_independently,
        test_pending_state_still_burns_new_durable_outbound_values,
        test_queue_is_not_delivery_and_protected_ack_is_required,
        test_ack_requires_send_reverse_direction_epoch_replay_deadline_and_semantic,
        test_transport_rejection_does_not_make_ack_eligible,
        test_ack_deadline_expiry_is_terminal_even_with_malformed_siblings,
        test_retry_is_exact_byte_and_finite_without_freezing_production_count,
        test_restart_discards_pending_retry_but_consumed_values_cannot_recur,
        test_inbound_replay_persistence_precedes_plaintext_and_ack,
        test_malformed_wrong_context_and_unauthenticated_never_release_plaintext,
        test_exact_duplicate_is_suppressed_and_conflicts_are_rejected,
        test_inbound_storage_failure_ambiguity_and_restart_are_fail_closed,
        test_authoritative_categorical_results_require_exact_builtin_strings,
        test_out_of_order_authoritative_mutation_results_force_reconciliation,
        test_inactive_restore_requires_every_exact_absence_proof,
        test_transport_restart_rejects_epoch_rollback_and_requires_explicit_forward,
        test_model_evidence_types_fail_closed_without_secret_echo,
        test_private_fields_noncanonical_types_and_shapes_are_rejected,
        test_container_subclasses_are_never_canonical_contract_types,
        test_direct_validator_bounds_deep_and_cyclic_containers,
        test_cli_loader_is_bounded_deterministic_and_sanitized,
        test_loader_reads_only_limit_plus_one_before_rejecting,
        test_validator_and_models_have_no_execution_or_write_surface,
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} secure-LoRa contract scenario groups")


if __name__ == "__main__":
    main()
