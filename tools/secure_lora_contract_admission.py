#!/usr/bin/env python3
"""Validate and exercise the host-only OpenTrail secure-LoRa contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTSL0"
VERSION = 0
ARTIFACT_KIND = "secure_lora_provisioning_transport_contract"
CONTRACT_ID = "OT-091-SECURE-LORA-KEY-TRANSPORT-CONTRACT-V0"
WORK_ITEM = "OT-091"
AUTHORITY_DECISION = "0035"
GOVERNING_SCOPE_DECISION = "0033"
CONTRACT_STATUS = "host_frozen_not_implemented"
PUBLIC_SUMMARY = (
    "The separate V1 secure-LoRa provisioning and direct transport contract "
    "is frozen and host-tested; target implementation and physical acceptance "
    "remain open."
)
CANONICAL_CONTRACT_SHA256 = (
    "6e9a292ed6a16be1f97b39a9d8812060a4cd5f72560e538d5721c4b4a4717850"
)
MAX_CONTRACT_BYTES = 64 * 1024
MAX_JSON_DEPTH = 64
MAX_EPOCH = (1 << 32) - 1
MAX_COUNTER = (1 << 64) - 1
MAX_FRAME_BYTES = 255
MAX_TEST_TRANSMISSION_ATTEMPTS = 255

TOP_LEVEL_KEYS = {
    "schema",
    "version",
    "artifact_kind",
    "contract_id",
    "work_item",
    "authority_decision",
    "governing_scope_decision",
    "contract_status",
    "public_summary",
    "scope",
    "selection_boundary",
    "identity_and_context",
    "provisioning",
    "rekey_and_revocation",
    "storage_and_restart",
    "direct_transport",
    "inbound_order",
    "outbound_order",
    "lifecycle_states",
    "transport_states",
    "failure_and_ambiguity",
    "privacy",
    "execution_authority",
    "claims",
    "open_followup_gates",
}
LIFECYCLE_STATES = [
    "boot_reconcile",
    "inactive_no_group",
    "active_current_epoch",
    "invitation_open",
    "handshake_pending",
    "human_confirmation_pending",
    "candidate_key_staged",
    "activation_pending",
    "rekey_staging",
    "retirement_required",
    "reconcile_required",
    "faulted",
]
TRANSPORT_STATES = [
    "blocked",
    "ready",
    "outbound_pending_ack",
    "inbound_persistence_pending",
    "reconcile_required",
    "faulted",
]
OPEN_FOLLOWUP_GATES = [
    "secure_lora_implementation_and_physical_acceptance_open",
    "complete_two_pair_v1_acceptance_open",
]
FORBIDDEN_KEYS = {
    "access_token",
    "ble_address",
    "counter_domain_value",
    "device_id",
    "device_serial",
    "full_identity_fingerprint",
    "group_identifier",
    "handshake_transcript",
    "identity_secret",
    "invitation_nonce",
    "key_password",
    "local_path",
    "lora_key",
    "mac_address",
    "message_identifier",
    "nonce_prefix_value",
    "pairing_pin",
    "password",
    "plaintext",
    "private_key",
    "refresh_token",
    "sealed_frame_bytes",
    "store_password",
    "store_token",
    "traffic_secret",
    "transport_port",
}
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
    re.compile(
        r"\b(?:password|private[_ -]?key|access[_ -]?token)\s*[:=]",
        re.IGNORECASE,
    ),
)


class AdmissionError(ValueError):
    pass


class SanitizedArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise AdmissionError("command arguments are invalid")


def _is_exact_bool(value: Any) -> bool:
    return type(value) is bool


def _is_exact_int(value: Any) -> bool:
    return type(value) is int


def _reject_duplicate_object_pairs(
    pairs: list[tuple[str, Any]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AdmissionError("contract JSON contains duplicate fields")
        result[key] = value
    return result


def _object(value: Any, path: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise AdmissionError(f"{path} must be an object")
    return value


def _same_exact(value: Any, expected: Any) -> bool:
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(value) == set(expected) and all(
            _same_exact(value[key], expected[key]) for key in expected
        )
    if isinstance(expected, list):
        return len(value) == len(expected) and all(
            _same_exact(item, wanted)
            for item, wanted in zip(value, expected)
        )
    return value == expected


def _reject_excessive_nesting(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        item, depth = pending.pop()
        if depth > MAX_JSON_DEPTH:
            raise AdmissionError("contract JSON exceeds the nesting limit")
        if type(item) is dict:
            pending.extend((child, depth + 1) for child in item.values())
        elif type(item) is list:
            pending.extend((child, depth + 1) for child in item)


def _reject_noncanonical_types(value: Any, path: str = "contract") -> None:
    if type(value) is dict:
        for key, item in value.items():
            if type(key) is not str:
                raise AdmissionError(f"{path} contains a noncanonical field name")
            _reject_noncanonical_types(item, f"{path}.field")
    elif type(value) is list:
        for item in value:
            _reject_noncanonical_types(item, f"{path}.item")
    elif type(value) not in {str, bool, int}:
        raise AdmissionError(f"{path} contains a noncanonical value type")


def _scan_public(value: Any, path: str = "contract") -> None:
    if isinstance(value, dict):
        for key in value:
            if key in FORBIDDEN_KEYS or any(
                pattern.search(key) for pattern in PRIVATE_TEXT
            ):
                raise AdmissionError(f"{path} contains a prohibited field name")
        for item in value.values():
            _scan_public(item, f"{path}.field")
    elif isinstance(value, list):
        for item in value:
            _scan_public(item, f"{path}.item")
    elif isinstance(value, str):
        for pattern in PRIVATE_TEXT:
            if pattern.search(value):
                raise AdmissionError(
                    f"{path} contains private machine, device, or credential text"
                )


def canonical_sha256(value: dict[str, Any]) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _require_exact_contract_semantics(contract: dict[str, Any]) -> None:
    if set(contract) != TOP_LEVEL_KEYS:
        raise AdmissionError("contract keys differ from the canonical shape")
    if not _is_exact_int(contract["version"]):
        raise AdmissionError("contract version type is not canonical")
    if contract["schema"] != SCHEMA or contract["version"] != VERSION:
        raise AdmissionError("contract schema/version mismatch")
    if (
        contract["artifact_kind"] != ARTIFACT_KIND
        or contract["contract_id"] != CONTRACT_ID
        or contract["work_item"] != WORK_ITEM
    ):
        raise AdmissionError("contract identity mismatch")
    if (
        contract["authority_decision"] != AUTHORITY_DECISION
        or contract["governing_scope_decision"] != GOVERNING_SCOPE_DECISION
    ):
        raise AdmissionError("contract decision authority mismatch")
    if contract["contract_status"] != CONTRACT_STATUS:
        raise AdmissionError("contract status mismatch")
    if contract["public_summary"] != PUBLIC_SUMMARY:
        raise AdmissionError("contract public summary mismatch")

    scope = _object(contract["scope"], "contract.scope")
    if not _same_exact(scope, {
        "release": "v1_companion",
        "topology": "exact_two_node_pairwise_direct",
        "node_roles": ["node-a", "node-b"],
        "radio_transport": "opaque_nonblocking_binary_frames",
        "ble_authorization_is_separate": True,
        "server_required": False,
        "internet_required": False,
        "relay_allowed": False,
        "broadcast_allowed": False,
        "multi_hop_allowed": False,
        "group_fanout_allowed": False,
    }):
        raise AdmissionError("V1 direct pairwise scope differs from policy")

    selection = _object(
        contract["selection_boundary"], "contract.selection_boundary"
    )
    blocked = "blocked_by_decision_0003_and_ot_005_exact_target_benchmark"
    for field in (
        "algorithm_selection",
        "crypto_library_selection",
        "kdf_selection",
        "packet_v1_wire_format",
    ):
        if selection.get(field) != blocked:
            raise AdmissionError("cryptographic selection gate was bypassed")
    for field in (
        "handshake_candidate_is_not_selected",
        "source_signature_candidate_is_not_selected",
    ):
        if selection.get(field) is not True:
            raise AdmissionError("cryptographic candidate was treated as selected")
    for field in (
        "packet_v0_security_claim_allowed",
        "plaintext_transport_allowed",
        "security_downgrade_allowed",
        "on_air_algorithm_negotiation_allowed",
        "random_nonce_fallback_allowed",
        "caller_supplied_authentication_boolean_allowed_in_production",
    ):
        if selection.get(field) is not False:
            raise AdmissionError("unimplemented authentication was claimed")

    provisioning = _object(contract["provisioning"], "contract.provisioning")
    phone_ble = _object(
        provisioning.get("phone_ble_boundary"),
        "contract.provisioning.phone_ble_boundary",
    )
    invitation = _object(provisioning.get("invitation"), "contract.provisioning.invitation")
    if (
        not _same_exact(phone_ble, {
            "may_carry_public_invitation_and_workflow_requests_only": True,
            "may_carry_lora_private_material": False,
            "ble_bond_is_lora_authority": False,
            "phone_is_key_escrow_or_derivation_source": False,
        })
        or invitation.get("expiry_rule")
        != "now_greater_than_or_equal_to_deadline_rejects"
        or invitation.get("clock_invalid_or_rollback") != "reject_and_burn"
        or
        invitation.get("single_use") is not True
        or invitation.get("contains_secret_material") is not False
        or invitation.get("one_candidate_per_invitation") is not True
        or invitation.get("one_attempt_per_invitation") is not True
        or invitation.get("failure_burns_invitation") is not True
        or invitation.get("replay_burns_invitation") is not True
        or invitation.get("restart_burns_open_invitation") is not True
        or invitation.get("deadline_extension_allowed") is not False
        or provisioning.get("mutual_authentication_before_material_release")
        is not True
        or provisioning.get("both_nodes_local_transcript_confirmation_required")
        is not True
        or provisioning.get("group_or_pairwise_material_release_before_confirmation")
        is not False
        or provisioning.get("candidate_commit_and_exact_readback_required")
        is not True
        or provisioning.get("exact_peer_activation_and_readback_required")
        is not True
        or provisioning.get("ready_before_both_verified_commits") is not False
    ):
        raise AdmissionError("provisioning order or fail-closed policy changed")

    rekey = _object(
        contract["rekey_and_revocation"], "contract.rekey_and_revocation"
    )
    if (
        rekey.get("next_epoch_rule") != "exact_current_plus_one"
        or not _is_exact_int(rekey.get("epoch_width_bits"))
        or rekey.get("epoch_width_bits") != 32
        or rekey.get("epoch_wrap_allowed") is not False
        or rekey.get("fresh_distinct_epoch_material_required") is not True
        or rekey.get("revoked_peer_excluded_from_new_epoch") is not True
        or rekey.get("routine_traffic_while_unresolved") != "blocked"
        or rekey.get("possible_mutation") != "reconcile_required_no_traffic"
        or rekey.get("fallback_to_old_epoch_after_new_activation") is not False
        or rekey.get("logical_retirement_and_exact_readback_required") is not True
        or rekey.get("physical_secure_erasure_claimed") is not False
    ):
        raise AdmissionError("rekey or revocation policy changed")

    storage = _object(
        contract["storage_and_restart"], "contract.storage_and_restart"
    )
    if (
        storage.get("model") != "ordinary_application_protected_storage"
        or storage.get("outbound_counter_store") != "OTCN/v1"
        or storage.get("outbound_counter_reservation_before_use") is not True
        or storage.get("receiver_replay_checkpoint_durable_before_plaintext")
        is not True
        or storage.get("receiver_replay_window_is_per_direction_and_epoch")
        is not True
        or storage.get("consumed_outbound_message_ids_and_counters_are_durable")
        is not True
        or storage.get("restart_restore_before_traffic") is not True
        or storage.get("pending_delivery_on_restart")
        != "interrupted_delivery_unknown_no_automatic_retry"
        or storage.get("physical_flash_rollback_resistance_required") is not False
        or storage.get("rollback_proof_against_physical_firmware_access_claimed")
        is not False
    ):
        raise AdmissionError("storage, replay, or physical-flash boundary changed")

    transport = _object(contract["direct_transport"], "contract.direct_transport")
    required_transport_true = (
        "network_or_conversation_authentication_required",
        "message_encryption_required",
        "integrity_verification_required",
        "sender_identity_required",
        "destination_identity_required",
        "current_epoch_required",
        "unique_message_identifier_required",
        "duplicate_suppression_required",
        "replay_rejection_required",
        "malformed_rejection_required",
        "wrong_network_rejection_required",
        "wrong_destination_rejection_required",
        "unauthenticated_rejection_required",
        "acknowledgement_required",
        "acknowledgement_is_authenticated_and_encrypted",
        "acknowledgement_binds_epoch_sender_destination_and_message",
        "acknowledgement_uses_reverse_direction_context",
        "acknowledgement_requires_at_least_one_send",
        "acknowledgement_requires_current_epoch_fresh_replay_and_deadline",
        "retry_reuses_exact_sealed_bytes",
    )
    if not all(transport.get(field) is True for field in required_transport_true):
        raise AdmissionError("direct transport protection was weakened")
    if (
        transport.get("plaintext_release_before_full_admission") is not False
        or transport.get("radio_queue_acceptance_is_delivery") is not False
        or transport.get("retry_reseals_or_reuses_nonce") is not False
        or transport.get("automatic_retry_after_restart") is not False
        or transport.get("transmission_attempt_limit")
        != "finite_target_measured_value_pending"
        or transport.get("retry_timing") != "finite_target_measured_value_pending"
        or not _is_exact_int(transport.get("opaque_frame_storage_ceiling_bytes"))
        or transport.get("opaque_frame_storage_ceiling_bytes") != MAX_FRAME_BYTES
        or transport.get("target_radio_mtu_selected") is not False
        or transport.get("acknowledgement_success_semantic")
        != "peer_device_durably_admitted"
    ):
        raise AdmissionError("delivery or exact-byte retry policy changed")

    if contract["lifecycle_states"] != LIFECYCLE_STATES:
        raise AdmissionError("lifecycle state set/order differs from policy")
    if contract["transport_states"] != TRANSPORT_STATES:
        raise AdmissionError("transport state set/order differs from policy")
    if contract["open_followup_gates"] != OPEN_FOLLOWUP_GATES:
        raise AdmissionError("open follow-up gates differ from policy")

    authority = _object(contract["execution_authority"], "contract.execution_authority")
    claims = _object(contract["claims"], "contract.claims")
    if not authority or any(type(value) is not bool or value for value in authority.values()):
        raise AdmissionError("contract grants execution authority")
    if not claims or any(type(value) is not bool or value for value in claims.values()):
        raise AdmissionError("contract makes an implementation or acceptance claim")


def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    try:
        _reject_excessive_nesting(contract)
        _reject_noncanonical_types(contract)
        _scan_public(contract)
        _require_exact_contract_semantics(contract)
    except AdmissionError:
        raise
    except (KeyError, TypeError, AttributeError, RecursionError, ValueError) as exc:
        raise AdmissionError(
            "contract structure or field types differ from the frozen policy"
        ) from exc
    try:
        digest = canonical_sha256(contract)
    except (TypeError, ValueError, RecursionError, OverflowError) as exc:
        raise AdmissionError(
            "contract structure or field types differ from the frozen policy"
        ) from exc
    if digest != CANONICAL_CONTRACT_SHA256:
        raise AdmissionError("contract differs from the canonical frozen policy")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "secure_lora_contract_admission",
        "contract_id": CONTRACT_ID,
        "contract_sha256": digest,
        "contract_status": "CONTRACT-FROZEN-HOST-ONLY",
        "implementation_status": "NOT-IMPLEMENTED",
        "physical_acceptance_status": "NOT-EVALUATED",
        "execution_authority_granted": False,
        "score_credit_added": False,
        "open_followup_gates": contract["open_followup_gates"],
    }


def load_contract(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(MAX_CONTRACT_BYTES + 1)
    except OSError as exc:
        raise AdmissionError("contract JSON could not be read") from exc
    if len(encoded) > MAX_CONTRACT_BYTES:
        raise AdmissionError("contract JSON exceeds the 65536-byte limit")
    try:
        text = encoded.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise AdmissionError("contract JSON is not valid UTF-8") from exc
    try:
        value = json.loads(text, object_pairs_hook=_reject_duplicate_object_pairs)
        _reject_excessive_nesting(value)
    except AdmissionError as exc:
        raise AdmissionError(
            "contract JSON is malformed or contains duplicate fields"
        ) from exc
    except (ValueError, RecursionError) as exc:
        raise AdmissionError(
            "contract JSON is malformed or contains duplicate fields"
        ) from exc
    return _object(value, "contract")


class SecureLoraLifecycleModel:
    """Pure categorical provisioning/rekey model; it accepts no secret material."""

    def __init__(self) -> None:
        self.state = "boot_reconcile"
        self.durable_epoch: int | None = None
        self.candidate_epoch: int | None = None
        self.invitation_consumed = False
        self._consumed_invitation_sequences: set[int] = set()
        self._active_invitation_sequence: int | None = None
        self._invitation_deadline_ms: int | None = None
        self._invitation_last_now_ms: int | None = None

    def _report(self, outcome: str) -> dict[str, Any]:
        return {
            "outcome": outcome,
            "state": self.state,
            "traffic_allowed": self.state == "active_current_epoch",
            "current_epoch_present": self.durable_epoch is not None,
            "invitation_consumed": self.invitation_consumed,
            "reconciliation_required": self.state == "reconcile_required",
        }

    def _reconcile(self, outcome: str) -> dict[str, Any]:
        self.candidate_epoch = None
        self._active_invitation_sequence = None
        self._invitation_deadline_ms = None
        self._invitation_last_now_ms = None
        self.state = "reconcile_required"
        return self._report(outcome)

    def _burn_invitation(self, outcome: str) -> dict[str, Any]:
        self.candidate_epoch = None
        self._active_invitation_sequence = None
        self._invitation_deadline_ms = None
        self._invitation_last_now_ms = None
        self.state = "inactive_no_group"
        return self._report(outcome)

    def _invitation_time_is_valid(
        self, *, now_ms: int, clock_exact: bool
    ) -> bool:
        if (
            not _is_exact_int(now_ms)
            or not _is_exact_bool(clock_exact)
            or clock_exact is not True
            or self._invitation_deadline_ms is None
            or self._invitation_last_now_ms is None
            or now_ms < self._invitation_last_now_ms
            or now_ms >= self._invitation_deadline_ms
        ):
            return False
        self._invitation_last_now_ms = now_ms
        return True

    def _observe_active_invitation_time(
        self, *, now_ms: int, clock_exact: bool, failure_outcome: str
    ) -> dict[str, Any] | None:
        if self.state not in {
            "invitation_open",
            "handshake_pending",
            "human_confirmation_pending",
        }:
            return None
        if not self._invitation_time_is_valid(
            now_ms=now_ms, clock_exact=clock_exact
        ):
            return self._burn_invitation(failure_outcome)
        return None

    def restore(
        self,
        *,
        group_present: bool,
        epoch: int,
        state_exact: bool,
        peer_activation_exact: bool,
        traffic_context_exact: bool,
        counter_lease_exact: bool,
        replay_checkpoint_exact: bool,
    ) -> dict[str, Any]:
        bools = (
            group_present,
            state_exact,
            peer_activation_exact,
            traffic_context_exact,
            counter_lease_exact,
            replay_checkpoint_exact,
        )
        if self.state not in {"boot_reconcile", "reconcile_required"}:
            exact_active_no_change = (
                self.state == "active_current_epoch"
                and all(_is_exact_bool(value) for value in bools)
                and all(value is True for value in bools)
                and _is_exact_int(epoch)
                and self.durable_epoch is not None
                and epoch == self.durable_epoch
            )
            exact_inactive_no_change = (
                self.state == "inactive_no_group"
                and _is_exact_bool(group_present)
                and group_present is False
                and all(_is_exact_bool(value) for value in bools[1:])
                and all(value is True for value in bools[1:])
                and _is_exact_int(epoch)
                and epoch == 0
                and self.durable_epoch is None
            )
            if exact_active_no_change or exact_inactive_no_change:
                return self._report("restore_not_available")
            if self.state == "faulted":
                return self._report("restore_not_available")
            return self._reconcile("restore_out_of_order_uncertain")
        if not all(_is_exact_bool(value) for value in bools) or not _is_exact_int(epoch):
            return self._reconcile("restore_evidence_rejected")
        if group_present is False:
            if (
                epoch != 0
                or not all(value is True for value in bools[1:])
                or self.durable_epoch is not None
            ):
                return self._reconcile("inactive_state_inexact")
            self.durable_epoch = None
            self.candidate_epoch = None
            self.state = "inactive_no_group"
            return self._report("restored_inactive")
        if (
            epoch <= 0
            or epoch > MAX_EPOCH
            or (
                self.durable_epoch is not None
                and epoch != self.durable_epoch
            )
            or not all(value is True for value in bools[1:])
        ):
            return self._reconcile("active_state_inexact")
        self.durable_epoch = epoch
        self.candidate_epoch = None
        self.state = "active_current_epoch"
        return self._report("restored_active")

    def open_invitation(
        self,
        *,
        invitation_authenticated: bool,
        invitation_binding_exact: bool,
        invitation_unused: bool,
        clock_exact: bool,
        now_ms: int,
        deadline_ms: int,
        invitation_sequence: int,
        attempt_number: int,
    ) -> dict[str, Any]:
        sequence_is_valid = (
            _is_exact_int(invitation_sequence)
            and 0 < invitation_sequence <= MAX_COUNTER
        )
        replayed = (
            sequence_is_valid
            and invitation_sequence in self._consumed_invitation_sequences
        )
        self.invitation_consumed = True
        if sequence_is_valid:
            self._consumed_invitation_sequences.add(invitation_sequence)
        clock_failure = self._observe_active_invitation_time(
            now_ms=now_ms,
            clock_exact=clock_exact,
            failure_outcome="active_invitation_clock_or_deadline_rejected_burned",
        )
        if clock_failure is not None:
            return clock_failure
        if (
            sequence_is_valid
            and replayed
            and invitation_sequence == self._active_invitation_sequence
            and self.state
            in {
                "invitation_open",
                "handshake_pending",
                "human_confirmation_pending",
            }
        ):
            return self._burn_invitation("active_invitation_replay_burned")
        if self.state != "inactive_no_group":
            return self._report("invitation_not_available")
        evidence = (
            invitation_authenticated,
            invitation_binding_exact,
            invitation_unused,
            clock_exact,
        )
        if (
            not all(_is_exact_bool(value) for value in evidence)
            or not _is_exact_int(now_ms)
            or not _is_exact_int(deadline_ms)
            or not _is_exact_int(invitation_sequence)
            or not _is_exact_int(attempt_number)
        ):
            self.invitation_consumed = True
            return self._report("invitation_evidence_rejected_burned")
        if (
            attempt_number != 1
            or invitation_sequence <= 0
            or invitation_sequence > MAX_COUNTER
            or replayed
            or now_ms < 0
            or deadline_ms <= 0
            or now_ms >= deadline_ms
            or not all(value is True for value in evidence)
        ):
            return self._report("invitation_rejected_burned")
        self._invitation_deadline_ms = deadline_ms
        self._invitation_last_now_ms = now_ms
        self._active_invitation_sequence = invitation_sequence
        self.state = "invitation_open"
        return self._report("invitation_admitted")

    def handshake_result(
        self,
        *,
        mutually_authenticated: bool,
        peer_binding_exact: bool,
        invitation_context_exact: bool,
        now_ms: int,
        clock_exact: bool,
    ) -> dict[str, Any]:
        clock_failure = self._observe_active_invitation_time(
            now_ms=now_ms,
            clock_exact=clock_exact,
            failure_outcome="handshake_clock_or_deadline_rejected_burned",
        )
        if clock_failure is not None:
            return clock_failure
        if self.state != "invitation_open":
            if self.state in {
                "handshake_pending",
                "human_confirmation_pending",
            }:
                return self._burn_invitation(
                    "handshake_wrong_phase_or_replay_burned"
                )
            return self._report("handshake_not_pending")
        evidence = (
            mutually_authenticated,
            peer_binding_exact,
            invitation_context_exact,
        )
        if not all(_is_exact_bool(value) for value in evidence) or not all(
            value is True for value in evidence
        ):
            return self._burn_invitation("handshake_rejected_burned")
        self.state = "human_confirmation_pending"
        return self._report("mutual_authentication_verified")

    def confirm_transcript(
        self,
        *,
        local_confirmed: bool,
        peer_confirmed: bool,
        transcript_exact: bool,
        now_ms: int,
        clock_exact: bool,
    ) -> dict[str, Any]:
        clock_failure = self._observe_active_invitation_time(
            now_ms=now_ms,
            clock_exact=clock_exact,
            failure_outcome="confirmation_clock_or_deadline_rejected_burned",
        )
        if clock_failure is not None:
            return clock_failure
        if self.state != "human_confirmation_pending":
            if self.state in {"invitation_open", "handshake_pending"}:
                return self._burn_invitation("confirmation_wrong_phase_burned")
            return self._report("confirmation_not_pending")
        evidence = (local_confirmed, peer_confirmed, transcript_exact)
        if not all(_is_exact_bool(value) for value in evidence) or not all(
            value is True for value in evidence
        ):
            return self._burn_invitation("confirmation_rejected_burned")
        self.candidate_epoch = 1
        self._active_invitation_sequence = None
        self._invitation_deadline_ms = None
        self._invitation_last_now_ms = None
        self.state = "candidate_key_staged"
        return self._report("material_release_and_commit_required")

    def commit_provisioning(self, result: str) -> dict[str, Any]:
        if self.state != "candidate_key_staged":
            if type(result) is str and result == "known_no_change":
                return self._report("provisioning_commit_not_pending")
            return self._reconcile("provisioning_commit_out_of_order_uncertain")
        if type(result) is not str:
            return self._reconcile("provisioning_commit_uncertain")
        if result == "known_no_change":
            self.candidate_epoch = None
            self.state = "inactive_no_group"
            return self._report("provisioning_known_no_change")
        if result != "verified_exact":
            return self._reconcile("provisioning_commit_uncertain")
        self.durable_epoch = self.candidate_epoch
        self.state = "activation_pending"
        return self._report("peer_activation_required")

    def activate_peer(self, result: str) -> dict[str, Any]:
        if self.state != "activation_pending":
            if type(result) is str and result == "known_no_change":
                return self._report("peer_activation_not_pending")
            return self._reconcile("peer_activation_out_of_order_uncertain")
        if type(result) is not str:
            return self._reconcile("peer_activation_uncertain")
        if result != "verified_exact":
            return self._reconcile("peer_activation_uncertain")
        self.candidate_epoch = None
        self.state = "active_current_epoch"
        return self._report("provisioned_ready")

    def begin_rekey(
        self,
        *,
        next_epoch: int,
        fresh_material: bool,
        domains_distinct: bool,
        retained_peer_exact: bool,
        revoked_peer_excluded: bool,
        revocation: bool,
    ) -> dict[str, Any]:
        if self.state != "active_current_epoch" or self.durable_epoch is None:
            return self._report("rekey_not_available")
        bools = (
            fresh_material,
            domains_distinct,
            retained_peer_exact,
            revoked_peer_excluded,
            revocation,
        )
        if not all(_is_exact_bool(value) for value in bools) or not _is_exact_int(
            next_epoch
        ):
            return self._report("rekey_evidence_rejected")
        if self.durable_epoch == MAX_EPOCH:
            self.state = "faulted"
            return self._report("epoch_exhausted")
        if (
            next_epoch != self.durable_epoch + 1
            or fresh_material is not True
            or domains_distinct is not True
            or retained_peer_exact is not True
            or revoked_peer_excluded is not True
        ):
            return self._report("rekey_rejected")
        self.candidate_epoch = next_epoch
        self.state = "rekey_staging"
        return self._report("rekey_staged_traffic_blocked")

    def activate_rekey(self, result: str) -> dict[str, Any]:
        if self.state != "rekey_staging" or self.candidate_epoch is None:
            if type(result) is str and result == "known_no_change":
                return self._report("rekey_activation_not_pending")
            return self._reconcile("rekey_activation_out_of_order_uncertain")
        if type(result) is not str:
            return self._reconcile("rekey_activation_uncertain")
        if result == "known_no_change":
            self.candidate_epoch = None
            self.state = "active_current_epoch"
            return self._report("old_epoch_retained")
        if result != "verified_exact":
            return self._reconcile("rekey_activation_uncertain")
        self.durable_epoch = self.candidate_epoch
        self.candidate_epoch = None
        self.state = "retirement_required"
        return self._report("new_epoch_active_old_epoch_rejected")

    def retire_old_epoch(self, result: str) -> dict[str, Any]:
        if self.state != "retirement_required":
            if type(result) is str and result == "known_no_change":
                return self._report("retirement_not_pending")
            return self._reconcile("old_epoch_retirement_out_of_order_uncertain")
        if type(result) is not str:
            return self._reconcile("old_epoch_retirement_uncertain")
        if result != "verified_exact":
            return self._reconcile("old_epoch_retirement_uncertain")
        self.state = "active_current_epoch"
        return self._report("rekey_ready")

    def restart(self) -> dict[str, Any]:
        self.candidate_epoch = None
        self._active_invitation_sequence = None
        self._invitation_deadline_ms = None
        self._invitation_last_now_ms = None
        self.state = "boot_reconcile"
        return self._report("restart_reconcile_required")


class DirectTransportModel:
    """Pure direct-transport admission model; reports never expose input bytes."""

    def __init__(self, maximum_attempts: int) -> None:
        if (
            not _is_exact_int(maximum_attempts)
            or maximum_attempts <= 0
            or maximum_attempts > MAX_TEST_TRANSMISSION_ATTEMPTS
        ):
            raise ValueError("host scenario attempt limit must be finite")
        self.maximum_attempts = maximum_attempts
        self.state = "blocked"
        self.current_epoch: int | None = None
        self._durable_seen_by_message: dict[int, tuple[int, bytes]] = {}
        self._durable_seen_by_counter: dict[int, tuple[int, bytes]] = {}
        self._consumed_outbound_ids: set[int] = set()
        self._consumed_outbound_counters: set[int] = set()
        self._pending_inbound: tuple[int, int, bytes] | None = None
        self._pending_outbound: tuple[int, int, int, bytes] | None = None
        self._attempts = 0
        self._queued_sends = 0

    def _report(
        self,
        outcome: str,
        *,
        plaintext_released: bool = False,
        protected_ack_required: bool = False,
        delivery_confirmed: bool = False,
    ) -> dict[str, Any]:
        return {
            "outcome": outcome,
            "state": self.state,
            "traffic_ready": self.state == "ready",
            "plaintext_released": plaintext_released,
            "protected_ack_required": protected_ack_required,
            "delivery_confirmed": delivery_confirmed,
            "attempts": self._attempts,
            "reconciliation_required": self.state == "reconcile_required",
        }

    def _reconcile(self, outcome: str) -> dict[str, Any]:
        if self._pending_inbound is not None:
            message_id, counter, sealed_frame = self._pending_inbound
            self._durable_seen_by_message[message_id] = (counter, sealed_frame)
            self._durable_seen_by_counter[counter] = (message_id, sealed_frame)
        self._pending_inbound = None
        self._pending_outbound = None
        self.state = "reconcile_required"
        return self._report(outcome)

    def restore(
        self,
        *,
        lifecycle_active: bool,
        epoch: int,
        derivation_context_exact: bool,
        counter_lease_exact: bool,
        replay_checkpoint_exact: bool,
        forward_rekey_exact: bool = False,
    ) -> dict[str, Any]:
        required_bools = (
            lifecycle_active,
            derivation_context_exact,
            counter_lease_exact,
            replay_checkpoint_exact,
        )
        if self.state not in {"blocked", "reconcile_required"}:
            exact_known_no_change = (
                all(_is_exact_bool(value) for value in required_bools)
                and all(value is True for value in required_bools)
                and _is_exact_bool(forward_rekey_exact)
                and forward_rekey_exact is False
                and _is_exact_int(epoch)
                and self.current_epoch is not None
                and epoch == self.current_epoch
            )
            if exact_known_no_change:
                return self._report("restore_not_available")
            return self._reconcile("transport_restore_out_of_order_uncertain")
        if (
            not all(_is_exact_bool(value) for value in required_bools)
            or not _is_exact_bool(forward_rekey_exact)
            or not _is_exact_int(epoch)
            or epoch <= 0
            or epoch > MAX_EPOCH
            or not all(value is True for value in required_bools)
        ):
            return self._reconcile("transport_restore_rejected")
        if self.current_epoch is None:
            if forward_rekey_exact is True:
                return self._reconcile("transport_restore_rejected")
        else:
            expected_epoch = self.current_epoch
            if forward_rekey_exact is True:
                if self.current_epoch == MAX_EPOCH:
                    return self._reconcile("transport_restore_rejected")
                expected_epoch += 1
            if epoch != expected_epoch:
                return self._reconcile("transport_restore_rejected")
        self.current_epoch = epoch
        self._pending_inbound = None
        self._pending_outbound = None
        self._attempts = 0
        self._queued_sends = 0
        self.state = "ready"
        return self._report("transport_ready")

    def begin_outbound(
        self,
        *,
        epoch: int,
        message_id: int,
        counter: int,
        derivation_context_exact: bool,
        direction_exact: bool,
        lease_exact: bool,
        nonce_binding_exact: bool,
        message_id_consumed_durably: bool,
        counter_consumed_durably: bool,
        sealed_frame: bytes,
    ) -> dict[str, Any]:
        durable_bools = (
            message_id_consumed_durably,
            counter_consumed_durably,
        )
        message_id_is_valid = (
            _is_exact_int(message_id)
            and 0 < message_id <= MAX_COUNTER
        )
        counter_is_valid = (
            _is_exact_int(counter)
            and 0 < counter <= MAX_COUNTER
        )
        message_id_reused = (
            message_id_is_valid
            and message_id in self._consumed_outbound_ids
        )
        counter_reused = (
            counter_is_valid
            and counter in self._consumed_outbound_counters
        )
        if message_id_is_valid and message_id_consumed_durably is True:
            self._consumed_outbound_ids.add(message_id)
        if counter_is_valid and counter_consumed_durably is True:
            self._consumed_outbound_counters.add(counter)
        if self.state != "ready" or self.current_epoch is None:
            return self._report("outbound_not_available")

        protection_bools = (
            derivation_context_exact,
            direction_exact,
            lease_exact,
            nonce_binding_exact,
        )
        if (
            not all(_is_exact_bool(value) for value in durable_bools)
            or not all(_is_exact_bool(value) for value in protection_bools)
            or not _is_exact_int(epoch)
            or not _is_exact_int(message_id)
            or not _is_exact_int(counter)
            or type(sealed_frame) is not bytes
        ):
            return self._report("outbound_evidence_rejected")
        if (
            epoch != self.current_epoch
            or not message_id_is_valid
            or not counter_is_valid
            or message_id_reused
            or counter_reused
            or not all(value is True for value in durable_bools)
            or not all(value is True for value in protection_bools)
            or not 1 <= len(sealed_frame) <= MAX_FRAME_BYTES
        ):
            return self._report("outbound_admission_rejected")
        self._pending_outbound = (epoch, message_id, counter, bytes(sealed_frame))
        self._attempts = 0
        self._queued_sends = 0
        self.state = "outbound_pending_ack"
        return self._report("sealed_once_pending_transmit")

    def transmit_attempt(
        self, *, exact_sealed_frame: bytes, transport_queued: bool
    ) -> dict[str, Any]:
        if self.state != "outbound_pending_ack" or self._pending_outbound is None:
            if transport_queued is True:
                return self._reconcile("out_of_order_transport_queue_uncertain")
            return self._report("transmit_not_pending")
        if type(exact_sealed_frame) is not bytes or not _is_exact_bool(transport_queued):
            if transport_queued is True:
                return self._reconcile("queued_unknown_sealed_bytes_uncertain")
            return self._report("transmit_evidence_rejected")
        if exact_sealed_frame != self._pending_outbound[3]:
            if transport_queued is True:
                return self._reconcile("queued_mismatched_sealed_bytes_uncertain")
            return self._report("sealed_bytes_mismatch_rejected")
        if self._attempts >= self.maximum_attempts:
            if transport_queued is True:
                return self._reconcile("queued_after_attempt_limit_uncertain")
            self._pending_outbound = None
            self.state = "ready"
            return self._report("attempts_exhausted")
        self._attempts += 1
        if transport_queued is not True:
            return self._report("transport_rejected_attempt_bounded")
        self._queued_sends += 1
        return self._report("queued_not_delivered")

    def acknowledge(
        self,
        *,
        epoch: int,
        authenticated: bool,
        encrypted: bool,
        integrity_exact: bool,
        source_destination_exact: bool,
        reverse_direction_exact: bool,
        correlation_exact: bool,
        replay_fresh: bool,
        deadline_valid: bool,
        semantic: str,
    ) -> dict[str, Any]:
        if self.state != "outbound_pending_ack" or self._pending_outbound is None:
            return self._report("ack_not_pending")
        if _is_exact_bool(deadline_valid) and deadline_valid is False:
            self._pending_outbound = None
            self.state = "ready"
            return self._report("ack_deadline_expired")
        bools = (
            authenticated,
            encrypted,
            integrity_exact,
            source_destination_exact,
            reverse_direction_exact,
            correlation_exact,
            replay_fresh,
            deadline_valid,
        )
        if (
            not all(_is_exact_bool(value) for value in bools)
            or not _is_exact_int(epoch)
            or type(semantic) is not str
        ):
            return self._report("ack_evidence_rejected")
        if (
            epoch != self.current_epoch
            or self._queued_sends < 1
            or semantic != "peer_device_durably_admitted"
            or not all(value is True for value in bools)
        ):
            return self._report("unexpected_ack_rejected")
        self._pending_outbound = None
        self.state = "ready"
        return self._report("delivery_confirmed", delivery_confirmed=True)

    def receive(
        self,
        *,
        epoch: int,
        message_id: int,
        counter: int,
        sealed_frame: bytes,
        structurally_valid: bool,
        group_exact: bool,
        destination_exact: bool,
        authenticated: bool,
        integrity_exact: bool,
        source_exact: bool,
        direction_exact: bool,
    ) -> dict[str, Any]:
        if self.state != "ready" or self.current_epoch is None:
            return self._report("inbound_not_available")
        bools = (
            structurally_valid,
            group_exact,
            destination_exact,
            authenticated,
            integrity_exact,
            source_exact,
            direction_exact,
        )
        if (
            not all(_is_exact_bool(value) for value in bools)
            or not _is_exact_int(epoch)
            or not _is_exact_int(message_id)
            or not _is_exact_int(counter)
            or type(sealed_frame) is not bytes
        ):
            return self._report("inbound_evidence_rejected")
        if (
            not 1 <= len(sealed_frame) <= MAX_FRAME_BYTES
            or structurally_valid is not True
        ):
            return self._report("malformed_rejected")
        if (
            epoch != self.current_epoch
            or group_exact is not True
            or destination_exact is not True
        ):
            return self._report("wrong_context_rejected")
        if (
            authenticated is not True
            or integrity_exact is not True
            or source_exact is not True
            or direction_exact is not True
        ):
            return self._report("unauthenticated_rejected")
        if (
            message_id <= 0
            or message_id > MAX_COUNTER
            or counter <= 0
            or counter > MAX_COUNTER
        ):
            return self._report("identifier_rejected")
        prior_by_message = self._durable_seen_by_message.get(message_id)
        prior_by_counter = self._durable_seen_by_counter.get(counter)
        if prior_by_message is not None or prior_by_counter is not None:
            if (
                prior_by_message == (counter, sealed_frame)
                and prior_by_counter == (message_id, sealed_frame)
            ):
                return self._report(
                    "exact_duplicate_suppressed",
                    protected_ack_required=True,
                )
            return self._report("conflicting_duplicate_rejected")
        self._pending_inbound = (message_id, counter, bytes(sealed_frame))
        self.state = "inbound_persistence_pending"
        return self._report("replay_persistence_required")

    def persist_inbound(self, result: str) -> dict[str, Any]:
        if self.state != "inbound_persistence_pending" or self._pending_inbound is None:
            if type(result) is str and result == "known_no_change":
                return self._report("inbound_persistence_not_pending")
            return self._reconcile("replay_persistence_out_of_order_uncertain")
        if type(result) is not str:
            return self._reconcile("replay_persistence_uncertain")
        if result == "known_no_change":
            self._pending_inbound = None
            self.state = "ready"
            return self._report("persistence_failed_no_plaintext")
        if result != "verified_exact":
            return self._reconcile("replay_persistence_uncertain")
        message_id, counter, sealed_frame = self._pending_inbound
        self._durable_seen_by_message[message_id] = (counter, sealed_frame)
        self._durable_seen_by_counter[counter] = (message_id, sealed_frame)
        self._pending_inbound = None
        self.state = "ready"
        return self._report(
            "plaintext_released_once",
            plaintext_released=True,
            protected_ack_required=True,
        )

    def restart(self) -> dict[str, Any]:
        interrupted = self._pending_outbound is not None
        self._pending_inbound = None
        self._pending_outbound = None
        self._attempts = 0
        self._queued_sends = 0
        self.state = "blocked"
        return self._report(
            "interrupted_delivery_unknown_no_retry"
            if interrupted
            else "restart_restore_required"
        )


def main(argv: list[str] | None = None) -> int:
    parser = SanitizedArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate-contract")
    validate.add_argument("--input", required=True, type=Path)
    try:
        args = parser.parse_args(argv)
        print(json.dumps(validate_contract(load_contract(args.input)), sort_keys=True))
        return 0
    except (AdmissionError, ValueError, RecursionError) as exc:
        error = (
            str(exc)
            if isinstance(exc, AdmissionError)
            else "contract JSON is malformed or exceeds the nesting limit"
        )
        report = {
            "schema": SCHEMA,
            "version": VERSION,
            "artifact_kind": "secure_lora_contract_admission",
            "contract_status": "CONTRACT-INVALID",
            "implementation_status": "NOT-IMPLEMENTED",
            "physical_acceptance_status": "NOT-EVALUATED",
            "execution_authority_granted": False,
            "score_credit_added": False,
            "error": error,
        }
        print(json.dumps(report, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
