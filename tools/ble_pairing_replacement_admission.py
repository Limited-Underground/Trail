#!/usr/bin/env python3
"""Validate and exercise the host-only OpenTrail BLE pairing contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTBP0"
VERSION = 0
ARTIFACT_KIND = "ble_pairing_replacement_contract"
CONTRACT_ID = "OT-090-BLE-PAIRING-REPLACEMENT-CONTRACT-V0"
WORK_ITEM = "OT-090"
AUTHORITY_DECISION = "0034"
GOVERNING_SCOPE_DECISION = "0033"
CONTRACT_STATUS = "host_frozen_not_implemented"
PUBLIC_SUMMARY = (
    "Practical V1 BLE pairing and phone replacement are frozen and "
    "host-tested; target implementation and physical acceptance remain open."
)
CANONICAL_CONTRACT_SHA256 = "92bbc290115d87f0534f020f94b0cb57cf3246a36a2a82eb97e5cdd115b6076d"
MAX_CONTRACT_BYTES = 64 * 1024
MAX_JSON_DEPTH = 64
PAIRING_WINDOW_MS = 30000
MINIMUM_PHYSICAL_HOLD_MS = 3000
REQUIRED_KEY_BYTES = 16
MAX_MONOTONIC_MS = (1 << 64) - 1

STATES = [
    "boot_reconcile",
    "closed_unowned",
    "closed_owned",
    "controller_active",
    "claim_window_open",
    "replacement_window_open",
    "replacement_confirmation_pending",
    "commit_in_progress",
    "candidate_cleanup_required",
    "reconcile_required",
    "faulted",
]

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
    "timing",
    "physical_presence",
    "pin",
    "ble_security",
    "android_os_pairing",
    "target_pairing_adapter",
    "states",
    "initial_claim",
    "reconnect",
    "replacement",
    "candidate_cleanup",
    "restart_and_reconciliation",
    "storage",
    "failure_states",
    "privacy",
    "execution_authority",
    "claims",
    "open_followup_gates",
}
FORBIDDEN_KEYS = {
    "access_token",
    "bond_key",
    "bond_reference",
    "controller_binding",
    "device_id",
    "device_serial",
    "key_password",
    "keystore_password",
    "local_path",
    "mac_address",
    "pairing_pin",
    "password",
    "phone_identifier",
    "physical_event_token",
    "private_key",
    "refresh_token",
    "session_challenge",
    "store_password",
    "store_token",
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
    if not isinstance(value, dict):
        raise AdmissionError(f"{path} must be an object")
    return value


def _reject_excessive_nesting(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        item, depth = pending.pop()
        if depth > MAX_JSON_DEPTH:
            raise AdmissionError("contract JSON exceeds the nesting limit")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)


def _scan_public(value: Any, path: str = "contract") -> None:
    if isinstance(value, dict):
        for key in value:
            if not isinstance(key, str):
                raise AdmissionError(f"{path} contains a noncanonical field name")
            if key in FORBIDDEN_KEYS or any(pattern.search(key) for pattern in PRIVATE_TEXT):
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
    if contract["schema"] != SCHEMA or contract["version"] != VERSION:
        raise AdmissionError("contract schema/version mismatch")
    if (
        contract["artifact_kind"] != ARTIFACT_KIND
        or contract["contract_id"] != CONTRACT_ID
        or contract["work_item"] != WORK_ITEM
    ):
        raise AdmissionError("contract identity mismatch")
    if contract["authority_decision"] != AUTHORITY_DECISION:
        raise AdmissionError("contract authority decision mismatch")
    if contract["governing_scope_decision"] != GOVERNING_SCOPE_DECISION:
        raise AdmissionError("contract governing scope decision mismatch")
    if contract["contract_status"] != CONTRACT_STATUS:
        raise AdmissionError("contract status mismatch")
    if contract["public_summary"] != PUBLIC_SUMMARY:
        raise AdmissionError("contract public summary mismatch")

    timing = _object(contract["timing"], "contract.timing")
    if timing != {
        "clock": "boot_local_monotonic_milliseconds",
        "pairing_window_ms": PAIRING_WINDOW_MS,
        "expiry_rule": "elapsed_greater_than_or_equal_to_window_closes",
        "replacement_confirmation_deadline": "original_pairing_window_deadline",
        "deadline_extension_allowed": False,
        "restart_restores_window": False,
    }:
        raise AdmissionError("pairing timing differs from the frozen policy")
    physical = _object(contract["physical_presence"], "contract.physical_presence")
    if physical != {
        "input": "designated_local_input",
        "target_gpio_or_button_selected": False,
        "open_window_gesture": {
            "minimum_hold_ms": MINIMUM_PHYSICAL_HOLD_MS,
            "release_required": True,
            "one_window_per_completed_gesture": True,
        },
        "replacement_confirmation_gesture": {
            "minimum_hold_ms": MINIMUM_PHYSICAL_HOLD_MS,
            "release_required": True,
            "one_confirmation_per_completed_gesture": True,
        },
        "confirmation_allowed_only_after_exact_candidate_bond": True,
        "event_token": "boot_local_strictly_increasing_nonzero",
        "stale_or_replayed_event": "deny_without_state_upgrade",
        "deadline_extension_allowed": False,
    }:
        raise AdmissionError(
            "physical-presence gesture differs from the frozen policy"
        )


    pin = _object(contract["pin"], "contract.pin")
    if (
        pin["source"] != "secure_random_only"
        or pin["conversion"] != "unbiased_uniform_rejection_sampling"
        or pin["range_min"] != 0
        or pin["range_max"] != 999999
        or pin["display_format"] != "six_zero_padded_decimal_digits"
        or pin["display_location"] != "local_heltec_only"
        or pin["generated_once_per_window"] is not True
        or pin["new_window_requires_new_sample"] is not True
        or pin["numeric_nonrepetition_guaranteed"] is not False
        or pin["static_or_debug_value_allowed"] is not False
    ):
        raise AdmissionError("PIN policy differs from the frozen policy")

    security = _object(contract["ble_security"], "contract.ble_security")
    required_true = (
        "le_secure_connections_required",
        "secure_connections_only",
        "mitm_authenticated_passkey_required",
        "bonding_required",
    )
    if not all(security[field] is True for field in required_true):
        raise AdmissionError("BLE Secure Connections policy was weakened")
    if (
        security["required_key_bytes"] != REQUIRED_KEY_BYTES
        or security["legacy_pairing_allowed"] is not False
        or security["just_works_allowed"] is not False
        or security["static_or_debug_passkey_allowed"] is not False
        or security["bond_state_alone_is_application_authorization"] is not False
        or security["pairing_attempts_per_window"] != 1
    ):
        raise AdmissionError("BLE pairing admission differs from the frozen policy")
    android = _object(contract["android_os_pairing"], "contract.android_os_pairing")
    if android != {
        "pin_entry_surface": "android_system_pairing_ui_only",
        "application_receives_pin": False,
        "application_persists_pin": False,
        "application_logs_pin": False,
        "bond_callback_requires_active_candidate_and_transport_generation": True,
        "bond_state_alone_is_application_authorization": False,
        "cancel_permission_loss_service_stop_or_disconnect": "close_without_automatic_retry",
        "automatic_pairing_retry": False,
    }:
        raise AdmissionError("Android OS pairing boundary differs from policy")
    target = _object(contract["target_pairing_adapter"], "contract.target_pairing_adapter")
    if target != {
        "dynamic_passkey_callback_required": True,
        "static_or_debug_passkey_allowed": False,
        "io_capability": "display_only",
        "passkey_display": "local_display_during_active_window_only",
        "unresolved_passkey_action": "fail_closed",
    }:
        raise AdmissionError("target passkey adapter boundary differs from policy")
    cleanup = _object(contract["candidate_cleanup"], "contract.candidate_cleanup")
    if (
        cleanup["verified_absence_required"] is not True
        or cleanup["verified_absence_result"]
        != "return_to_prior_durable_owner_or_unowned"
        or cleanup["uncertain_or_failed_result"]
        != "reconcile_required_no_controller"
        or cleanup["automatic_authorization_retry"] is not False
    ):
        raise AdmissionError("candidate cleanup boundary differs from policy")


    if contract["states"] != STATES:
        raise AdmissionError("state set/order differs from the frozen policy")
    initial = _object(contract["initial_claim"], "contract.initial_claim")
    if (
        initial["prior_application_authorization_required"] is not False
        or initial["physical_window_precedes_pairing"] is not True
        or initial["private_candidate_binding_required"] is not True
        or initial["application_authority_created_before_commit"] is not False
        or initial["application_authority_created_after_exact_commit_readback"]
        is not True
    ):
        raise AdmissionError("initial claim order differs from the frozen policy")

    replacement = _object(contract["replacement"], "contract.replacement")
    replacement_true = (
        "existing_owner_required",
        "old_durable_owner_retained_until_confirmed_commit",
        "new_exact_secure_bond_required",
        "same_private_bond_rejected",
        "second_deliberate_local_confirmation_required",
        "confirmation_bound_to_same_boot_window_and_candidate",
        "confirmation_must_precede_original_deadline",
        "candidate_owner_commit_and_exact_readback_precede_old_bond_removal",
        "old_application_authorization_invalid_after_candidate_commit",
        "old_bond_removal_and_absence_verification_required",
    )
    if not all(replacement[field] is True for field in replacement_true):
        raise AdmissionError("replacement policy was weakened")
    if replacement["new_controller_published_before_cleanup_verification"] is not False:
        raise AdmissionError("replacement publishes a controller before cleanup")

    storage = _object(contract["storage"], "contract.storage")
    if (
        storage["model"] != "ordinary_application_protected_storage"
        or storage["schema_must_be_distinct_from_historical_floor_based_records"]
        is not True
        or storage["exact_commit_readback_required"] is not True
        or storage["independent_monotonic_floor_required"] is not False
        or storage["secure_element_required"] is not False
        or storage["rollback_proof_against_physical_firmware_access"] is not False
    ):
        raise AdmissionError("storage or physical-flash boundary changed")

    if any(contract["execution_authority"].values()):
        raise AdmissionError("contract grants execution authority")
    if any(contract["claims"].values()):
        raise AdmissionError("contract makes an implementation or acceptance claim")


def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    try:
        _scan_public(contract)
        _require_exact_contract_semantics(contract)
    except AdmissionError:
        raise
    except (KeyError, TypeError, AttributeError) as exc:
        raise AdmissionError(
            "contract structure or field types differ from the frozen policy"
        ) from exc
    digest = canonical_sha256(contract)
    if digest != CANONICAL_CONTRACT_SHA256:
        raise AdmissionError("contract differs from the canonical frozen policy")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "ble_pairing_replacement_contract_admission",
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
        encoded = path.read_bytes()
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


class PairingReplacementModel:
    """Pure categorical reference model; it never accepts or exposes secrets."""

    def __init__(self, window_ms: int = PAIRING_WINDOW_MS) -> None:
        if not _is_exact_int(window_ms) or window_ms != PAIRING_WINDOW_MS:
            raise ValueError("window duration is not the frozen policy")
        self.window_ms = window_ms
        self.state = "boot_reconcile"
        self.owner_present = False
        self.controller_active = False
        self.pin_displayed = False
        self.pin_samples = 0
        self.last_now_ms: int | None = None
        self.last_physical_event = 0
        self.window_opened_ms: int | None = None
        self.window_deadline_ms: int | None = None
        self.pairing_attempted = False
        self.purpose: str | None = None
        self.candidate_bound = False
        self.candidate_owner_committed = False
        self.bond_roster_reconciliation_required = True

    def _report(self, outcome: str) -> dict[str, Any]:
        return {
            "outcome": outcome,
            "state": self.state,
            "owner_present": self.owner_present,
            "controller_active": self.controller_active,
            "pairing_window_open": self.state
            in {"claim_window_open", "replacement_window_open"},
            "confirmation_pending": self.state
            == "replacement_confirmation_pending",
            "reconciliation_required": self.state == "reconcile_required",
            "bond_roster_reconciliation_required":
            self.bond_roster_reconciliation_required,
            "pin_displayed": self.pin_displayed,
        }

    def _clear_transient(self) -> None:
        self.pin_displayed = False
        self.window_opened_ms = None
        self.window_deadline_ms = None
        self.pairing_attempted = False
        self.purpose = None
        self.candidate_bound = False
        self.candidate_owner_committed = False

    def _observe_time(self, now_ms: int) -> bool:
        if (
            not _is_exact_int(now_ms)
            or now_ms < 0
            or now_ms > MAX_MONOTONIC_MS
        ):
            self.controller_active = False
            self._clear_transient()
            self.state = "faulted"
            return False
        if self.last_now_ms is not None and now_ms < self.last_now_ms:
            self.controller_active = False
            self._clear_transient()
            self.state = "faulted"
            return False
        self.last_now_ms = now_ms
        return True

    def _window_expired(self, now_ms: int) -> bool:
        return (
            self.window_deadline_ms is not None
            and now_ms >= self.window_deadline_ms
        )

    def _close_to_durable_owner(self) -> None:
        self.controller_active = False
        self._clear_transient()
        self.state = "closed_owned" if self.owner_present else "closed_unowned"

    def _require_candidate_cleanup(self, outcome: str) -> dict[str, Any]:
        self.controller_active = False
        self.pin_displayed = False
        self.bond_roster_reconciliation_required = True
        self.state = "candidate_cleanup_required"
        return self._report(outcome)

    def _require_reconciliation(self, outcome: str) -> dict[str, Any]:
        self.controller_active = False
        self.pin_displayed = False
        self.bond_roster_reconciliation_required = True
        self.state = "reconcile_required"
        return self._report(outcome)

    def restore(
        self,
        *,
        owner_present: bool,
        exact_state: bool,
        bond_roster_exact: bool,
        orphan_candidate_present: bool,
    ) -> dict[str, Any]:
        if self.state not in {"boot_reconcile", "reconcile_required"}:
            return self._report("invalid_transition")
        if not all(
            _is_exact_bool(value)
            for value in (
                owner_present,
                exact_state,
                bond_roster_exact,
                orphan_candidate_present,
            )
        ):
            return self._require_reconciliation("restore_evidence_rejected")
        self.controller_active = False
        self._clear_transient()
        if not exact_state or not bond_roster_exact or orphan_candidate_present:
            return self._require_reconciliation("reconciliation_required")
        self.owner_present = owner_present
        self.bond_roster_reconciliation_required = False
        self.state = "closed_owned" if owner_present else "closed_unowned"
        return self._report("restored")

    def open_window(
        self,
        *,
        now_ms: int,
        physical_event: int,
        hold_ms: int,
        released: bool,
        secure_random_ready: bool,
        secure_random_sample_succeeded: bool,
    ) -> dict[str, Any]:
        if not self._observe_time(now_ms):
            return self._report("clock_or_argument_rejected")
        if self.state not in {
            "closed_unowned",
            "closed_owned",
            "controller_active",
        }:
            return self._report("window_not_available")
        if now_ms > MAX_MONOTONIC_MS - self.window_ms:
            self.controller_active = False
            self._clear_transient()
            self.state = "faulted"
            return self._report("deadline_overflow_faulted")
        if not all(
            _is_exact_bool(value)
            for value in (
                released,
                secure_random_ready,
                secure_random_sample_succeeded,
            )
        ):
            return self._report("window_evidence_rejected")
        if (
            not _is_exact_int(hold_ms)
            or hold_ms < MINIMUM_PHYSICAL_HOLD_MS
            or hold_ms > MAX_MONOTONIC_MS
            or released is not True
        ):
            return self._report("physical_gesture_rejected")
        if (
            not _is_exact_int(physical_event)
            or physical_event <= self.last_physical_event
            or physical_event > MAX_MONOTONIC_MS
        ):
            return self._report("stale_physical_event")
        self.last_physical_event = physical_event
        if not secure_random_ready or not secure_random_sample_succeeded:
            self._close_to_durable_owner()
            return self._report("secure_random_unavailable")
        self.controller_active = False
        self.window_opened_ms = now_ms
        self.window_deadline_ms = now_ms + self.window_ms
        self.pairing_attempted = False
        self.candidate_bound = False
        self.candidate_owner_committed = False
        self.pin_samples += 1
        self.pin_displayed = True
        self.purpose = "replace" if self.owner_present else "claim"
        self.state = (
            "replacement_window_open"
            if self.owner_present
            else "claim_window_open"
        )
        return self._report("window_opened")

    def tick(self, now_ms: int) -> dict[str, Any]:
        if not self._observe_time(now_ms):
            return self._report("clock_or_argument_rejected")
        if self._window_expired(now_ms):
            if self.state == "replacement_confirmation_pending":
                return self._require_candidate_cleanup(
                    "window_expired_cleanup_required"
                )
            if self.state in {"claim_window_open", "replacement_window_open"}:
                self._close_to_durable_owner()
                return self._report("window_expired")
        return self._report("no_change")

    def pairing_result(
        self,
        *,
        now_ms: int,
        secure_connections: bool,
        mitm_authenticated: bool,
        bonded: bool,
        key_bytes: int,
        candidate_binding_exact: bool,
        same_owner: bool = False,
    ) -> dict[str, Any]:
        if not self._observe_time(now_ms):
            return self._report("clock_or_argument_rejected")
        if self.state not in {"claim_window_open", "replacement_window_open"}:
            return self._report("window_closed")
        if (
            not all(
                _is_exact_bool(value)
                for value in (
                    secure_connections,
                    mitm_authenticated,
                    bonded,
                    candidate_binding_exact,
                    same_owner,
                )
            )
            or not _is_exact_int(key_bytes)
        ):
            return self._require_reconciliation("pairing_evidence_rejected")
        if self._window_expired(now_ms):
            if bonded is True:
                if candidate_binding_exact is not True:
                    return self._require_reconciliation(
                        "expired_candidate_binding_ambiguous"
                    )
                self.candidate_bound = True
                return self._require_candidate_cleanup(
                    "window_expired_cleanup_required"
                )
            self._close_to_durable_owner()
            return self._report("window_expired")
        if self.pairing_attempted:
            return self._report("attempt_already_consumed")
        self.pairing_attempted = True
        self.pin_displayed = False
        if bonded is True and candidate_binding_exact is not True:
            return self._require_reconciliation("candidate_binding_ambiguous")
        if not (
            secure_connections is True
            and mitm_authenticated is True
            and bonded is True
            and key_bytes == REQUIRED_KEY_BYTES
            and candidate_binding_exact is True
        ):
            if bonded is True:
                self.candidate_bound = True
                return self._require_candidate_cleanup("pairing_cleanup_required")
            self._close_to_durable_owner()
            return self._report("pairing_security_rejected_no_bond")
        if self.purpose == "replace" and same_owner is True:
            self._close_to_durable_owner()
            return self._report("same_owner_replacement_rejected")
        self.candidate_bound = True
        if self.purpose == "claim":
            self.state = "commit_in_progress"
            return self._report("claim_commit_required")
        self.state = "replacement_confirmation_pending"
        return self._report("replacement_confirmation_required")

    def confirm_replacement(
        self,
        *,
        now_ms: int,
        physical_event: int,
        hold_ms: int,
        released: bool,
    ) -> dict[str, Any]:
        if not self._observe_time(now_ms):
            return self._report("clock_or_argument_rejected")
        if self.state != "replacement_confirmation_pending":
            return self._report("confirmation_not_pending")
        if self._window_expired(now_ms):
            return self._require_candidate_cleanup("confirmation_expired_cleanup_required")
        if not _is_exact_bool(released):
            return self._report("confirmation_evidence_rejected")
        if (
            not _is_exact_int(hold_ms)
            or hold_ms < MINIMUM_PHYSICAL_HOLD_MS
            or hold_ms > MAX_MONOTONIC_MS
            or released is not True
        ):
            return self._report("confirmation_gesture_rejected")
        if (
            not _is_exact_int(physical_event)
            or physical_event <= self.last_physical_event
            or physical_event > MAX_MONOTONIC_MS
        ):
            return self._report("stale_confirmation")
        self.last_physical_event = physical_event
        self.state = "commit_in_progress"
        return self._report("replacement_commit_required")

    def commit_result(self, result: str) -> dict[str, Any]:
        if self.state != "commit_in_progress" or self.purpose not in {
            "claim",
            "replace",
        }:
            return self._report("commit_not_pending")
        if result == "known_no_change":
            return self._require_candidate_cleanup("known_no_change_cleanup_required")
        if result != "verified_exact":
            return self._require_reconciliation("commit_uncertain")
        self.owner_present = True

        if self.purpose == "claim":
            self.controller_active = True
            self.bond_roster_reconciliation_required = False
            self._clear_transient()
            self.state = "controller_active"
            return self._report("accepted")
        self.candidate_owner_committed = True
        self.controller_active = False
        self.bond_roster_reconciliation_required = True
        return self._report("old_bond_cleanup_required")

    def candidate_cleanup_result(self, *, verified_absent: bool) -> dict[str, Any]:
        if self.state != "candidate_cleanup_required" or not self.candidate_bound:
            return self._report("candidate_cleanup_not_pending")
        if not _is_exact_bool(verified_absent):
            return self._report("candidate_cleanup_evidence_rejected")
        if verified_absent is not True:
            return self._require_reconciliation("candidate_cleanup_uncertain")
        self.bond_roster_reconciliation_required = False
        self._close_to_durable_owner()
        return self._report("candidate_cleanup_verified")

    def old_bond_cleanup_result(self, *, verified_absent: bool) -> dict[str, Any]:
        if (
            self.state != "commit_in_progress"
            or self.purpose != "replace"
            or not self.candidate_owner_committed
        ):
            return self._report("cleanup_not_pending")
        if not _is_exact_bool(verified_absent):
            return self._report("cleanup_evidence_rejected")
        if verified_absent is not True:
            return self._require_reconciliation("cleanup_uncertain")
        self.controller_active = True
        self.bond_roster_reconciliation_required = False
        self._clear_transient()
        self.state = "controller_active"
        return self._report("replaced")

    def reconnect(
        self,
        *,
        now_ms: int,
        exact_owner_binding: bool,
        secure_connections: bool,
        mitm_authenticated: bool,
        bonded: bool,
        key_bytes: int,
        fresh_session: bool,
    ) -> dict[str, Any]:
        if not self._observe_time(now_ms):
            return self._report("clock_or_argument_rejected")
        if self.state == "controller_active":
            return self._report("controller_in_use")
        if self.state != "closed_owned" or not self.owner_present:
            return self._report("owner_not_available")
        if (
            not all(
                _is_exact_bool(value)
                for value in (
                    exact_owner_binding,
                    secure_connections,
                    mitm_authenticated,
                    bonded,
                    fresh_session,
                )
            )
            or not _is_exact_int(key_bytes)
        ):
            return self._report("reconnect_evidence_rejected")
        if not (
            exact_owner_binding is True
            and secure_connections is True
            and mitm_authenticated is True
            and bonded is True
            and key_bytes == REQUIRED_KEY_BYTES
            and fresh_session is True
        ):
            return self._report("reconnect_rejected")
        self.controller_active = True
        self.state = "controller_active"
        return self._report("authorized_reconnect")

    def disconnect(self) -> dict[str, Any]:
        if self.state == "controller_active":
            self.controller_active = False
            self.state = "closed_owned"
            return self._report("released")
        if self.state in {"claim_window_open", "replacement_window_open"}:
            self._close_to_durable_owner()
            return self._report("interrupted_closed")
        if self.state == "replacement_confirmation_pending":
            return self._require_candidate_cleanup("disconnect_cleanup_required")
        if self.state == "commit_in_progress":
            return self._require_reconciliation("disconnect_commit_uncertain")
        if self.state == "candidate_cleanup_required":
            return self._report("candidate_cleanup_still_required")
        return self._report("no_active_connection")

    def restart(self) -> dict[str, Any]:
        self.controller_active = False
        self.bond_roster_reconciliation_required = True
        self._clear_transient()
        self.last_now_ms = None
        self.last_physical_event = 0
        self.state = "boot_reconcile"
        return self._report("restart_reconcile_required")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate-contract")
    validate.add_argument("--input", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
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
            "artifact_kind": "ble_pairing_replacement_contract_admission",
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
