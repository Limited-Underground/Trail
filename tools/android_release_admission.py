#!/usr/bin/env python3
"""Validate a public, non-executing Android operational-release plan."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTAR0"
VERSION = 0
ARTIFACT_KIND = "android_operational_release_plan"
PLAN_ID = "OT-086-ANDROID-OPERATIONAL-RELEASE-V0"
BLOCKED_STATUS = "accepted_execution_blocked"
READY_STATUS = "ready_for_separate_execution_authorization"
BLOCKED_REPORT = "PLAN-ACCEPTED-EXECUTION-BLOCKED"
READY_REPORT = "PLAN-ACCEPTED-READY-FOR-SEPARATE-AUTHORIZATION"
RELEASE_GATE_STATUS = "NOT-EVALUATED"
MAX_PLAN_BYTES = 64 * 1024
MAX_JSON_DEPTH = 64

APPLICATION_ID = "io.github.nbjelanovic.otclient"
CURRENT_VERSION_CODE = 1
CURRENT_VERSION_NAME = "1.0.0"
MIN_SDK = 26
BLUETOOTH_MIN_SDK = 31
TARGET_SDK = 35
MINIMUM_PHYSICAL_API_LEVEL = 31
REQUIRED_PHONE_ROLES = ["phone-a", "phone-b"]
REQUIRED_PHYSICAL_PHONES = 2
PRIVATE_SIDELOAD_SCOPE = "private-sideload-v1-pilot"
FIRST_RELEASE_UPGRADE_MODE = "first-release-not-applicable"
OPERATIONAL_POLICY_ID = "OT-088-ANDROID-PRIVATE-PILOT-OPERATIONAL-POLICY-V0"
PRIVACY_DATA_SAFETY_POLICY = {
    "policy_id": "OT-088-PRIVACY-DATA-SAFETY-V0",
    "network_or_server_collection": "none",
    "third_party_sharing": "none",
    "account_required": False,
    "companion_location_use": "ephemeral_local_display_only",
    "persistent_product_storage": "prohibited",
    "production_sensitive_logging": "prohibited",
    "sensitive_notification_content": "prohibited",
    "backup_and_device_transfer": "excluded",
    "uninstall_data_removal_verification": "required",
    "public_evidence": "aggregate_redacted_only",
    "play_data_safety": "not_applicable_private_sideload",
}
ROLLBACK_POLICY = {
    "policy_id": "OT-088-ROLLBACK-V0",
    "prior_supported_release": "none_first_release",
    "in_place_upgrade": "not_applicable_first_release",
    "downgrade": "prohibited",
    "automatic_rollback": "none",
    "recovery_route": "disconnect_stop_uninstall_verify_removal",
    "failure_fallback": "no_app_fail_closed",
    "data_restore": "prohibited",
    "failed_candidate_replacement": "new_evidence_set_required",
    "reinstall_authority": "separate_owner_authorization_required",
}
SUPPORT_POLICY = {
    "policy_id": "OT-088-SUPPORT-V0",
    "scope": "two_owner_approved_private_pilot_phones_only",
    "starts": "after_complete_release_acceptance",
    "supported_version": "exact_accepted_candidate_only",
    "supported_platforms": "exact_approved_physical_matrix_only",
    "installation_source": "owner_controlled_hash_and_signer_verified_apk",
    "known_limitations": "required",
    "service_level": "best_effort_no_sla",
    "failure_path": "stop_use_disconnect_uninstall_and_report",
    "support_channel": "owner_provided_private_pilot_channel",
    "security_reports": "repository_security_policy",
    "ends": "owner_revocation_or_superseding_accepted_release",
    "safety_boundary": "supplemental_not_guaranteed_rescue",
}

AUTHORED_PERMISSIONS = [
    "android.permission.BLUETOOTH_CONNECT",
    "android.permission.BLUETOOTH_SCAN",
    "android.permission.FOREGROUND_SERVICE",
    "android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE",
    "android.permission.POST_NOTIFICATIONS",
]
GENERATED_PERMISSIONS = [
    "io.github.nbjelanovic.otclient.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION",
]
FORBIDDEN_PERMISSIONS = [
    "android.permission.ACCESS_COARSE_LOCATION",
    "android.permission.ACCESS_FINE_LOCATION",
    "android.permission.INTERNET",
    "android.permission.MANAGE_EXTERNAL_STORAGE",
    "android.permission.READ_EXTERNAL_STORAGE",
    "android.permission.RECEIVE_BOOT_COMPLETED",
    "android.permission.REQUEST_INSTALL_PACKAGES",
    "android.permission.WAKE_LOCK",
    "android.permission.WRITE_EXTERNAL_STORAGE",
]
FORBIDDEN_RELEASE_COMPONENTS = [
    "io.github.nbjelanovic.otclient.PublicLinkAutomaticTerminationPolicy",
    "io.github.nbjelanovic.otclient.PublicLinkProbeInstrumentation",
]
REQUIRED_ACCEPTANCE_CHECKS = [
    "release_variant_build_and_lint",
    "artifact_identity_and_hash_manifest",
    "release_signature_and_certificate_pin",
    "permission_and_component_allowlist",
    "ot085_instrumentation_and_debug_helpers_absent",
    "clean_install_and_cold_launch",
    "first_release_uninstall_reinstall_and_state_boundary",
    "uninstall_and_data_removal",
    "foreground_service_notification_granted",
    "foreground_service_notification_denied",
    "background_reopen_and_process_restart",
    "device_reboot_no_auto_start",
    "screen_reader_and_accessibility_review",
    "backup_and_device_transfer_excluded",
    "privacy_data_safety_and_support",
    "rollback_and_recovery",
    "public_evidence_redaction",
]

TOP_LEVEL_KEYS = {
    "schema",
    "version",
    "artifact_kind",
    "plan_id",
    "plan_status",
    "application",
    "release_identity",
    "distribution",
    "upgrade_mode",
    "signing",
    "artifact_policy",
    "supported_platforms",
    "operational_policy",
    "required_acceptance_checks",
    "privacy",
    "execution_authority",
    "blockers",
}
FORBIDDEN_KEYS = {
    "access_token",
    "account_email",
    "coordinates",
    "device_serial",
    "key_password",
    "keystore_password",
    "local_path",
    "mac_address",
    "pairing_pin",
    "password",
    "private_key",
    "refresh_token",
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
HEX64 = re.compile(r"^[0-9a-f]{64}$")
VERSION_NAME = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


class AdmissionError(ValueError):
    pass


def _reject_duplicate_object_pairs(
    pairs: list[tuple[str, Any]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AdmissionError("plan JSON contains duplicate fields")
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
            raise AdmissionError("plan JSON exceeds the nesting limit")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    missing = sorted(expected - set(value))
    extra = sorted(set(value) - expected)
    if missing or extra:
        raise AdmissionError(f"{path} keys differ from the canonical shape")


def _boolean(value: Any, path: str) -> bool:
    if not isinstance(value, bool):
        raise AdmissionError(f"{path} must be a Boolean")
    return value


def _integer(value: Any, path: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise AdmissionError(f"{path} must be an integer >= {minimum}")
    return value


def _string(value: Any, path: str, *, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value):
        suffix = "possibly empty" if allow_empty else "nonempty"
        raise AdmissionError(f"{path} must be a {suffix} string")
    return value


def _scan_public(value: Any, path: str = "plan") -> None:
    if isinstance(value, dict):
        for key in value:
            if not isinstance(key, str):
                raise AdmissionError(f"{path} contains a noncanonical field name")
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


def _fixed_boolean_object(
    value: Any, expected: dict[str, bool], path: str
) -> dict[str, bool]:
    result = _object(value, path)
    _exact_keys(result, set(expected), path)
    for key, required in expected.items():
        actual = _boolean(result[key], f"{path}.{key}")
        if actual is not required:
            raise AdmissionError(f"{path}.{key} must be {str(required).lower()}")
    return result


def _fixed_policy_object(
    value: Any, expected: dict[str, Any], path: str
) -> dict[str, Any]:
    result = _object(value, path)
    _exact_keys(result, set(expected) | {"owner_approved"}, path)
    for key, required in expected.items():
        actual = result[key]
        if isinstance(required, bool):
            if _boolean(actual, f"{path}.{key}") is not required:
                raise AdmissionError(f"{path}.{key} differs from the canonical policy")
        elif isinstance(required, str):
            if _string(actual, f"{path}.{key}") != required:
                raise AdmissionError(f"{path}.{key} differs from the canonical policy")
        else:
            raise AssertionError("unsupported canonical policy value")
    _boolean(result["owner_approved"], f"{path}.owner_approved")
    return result


def canonical_sha256(value: dict[str, Any]) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _derived_blockers(plan: dict[str, Any]) -> list[str]:
    blockers: list[str] = []
    application = plan["application"]
    identity = plan["release_identity"]
    signing = plan["signing"]
    platforms = plan["supported_platforms"]
    operational = plan["operational_policy"]

    if (
        application["candidate_version_code"] < 1
        or not application["candidate_version_name"]
    ):
        blockers.append("release_version_not_frozen")
    if not application["production_variant_configured"]:
        blockers.append("production_variant_not_configured")
    if (
        identity["clearance"] != "professionally_cleared"
        or not identity["owner_approved_for_candidate"]
    ):
        blockers.append("release_identity_not_approved")
    if (
        signing["custody_model"] == "unselected"
        or not signing["certificate_sha256"]
        or not signing["custody_approved"]
    ):
        blockers.append("signer_and_custody_not_approved")
    if not platforms["matrix_approved"]:
        blockers.append("physical_acceptance_matrix_not_approved")
    if not operational["privacy_data_safety"]["owner_approved"]:
        blockers.append("privacy_data_safety_not_approved")
    if not operational["support"]["owner_approved"]:
        blockers.append("support_policy_not_approved")
    if not operational["rollback"]["owner_approved"]:
        blockers.append("rollback_policy_not_approved")
    return sorted(blockers)


def validate_plan(plan: dict[str, Any]) -> dict[str, Any]:
    _scan_public(plan)
    _exact_keys(plan, TOP_LEVEL_KEYS, "plan")
    if plan["schema"] != SCHEMA or plan["version"] != VERSION:
        raise AdmissionError("plan schema/version mismatch")
    if plan["artifact_kind"] != ARTIFACT_KIND or plan["plan_id"] != PLAN_ID:
        raise AdmissionError("plan artifact_kind/plan_id mismatch")
    plan_status = _string(plan["plan_status"], "plan.plan_status")
    if plan_status not in {BLOCKED_STATUS, READY_STATUS}:
        raise AdmissionError("plan.plan_status is not canonical")

    application = _object(plan["application"], "plan.application")
    _exact_keys(
        application,
        {
            "application_id",
            "current_version_code",
            "current_version_name",
            "candidate_version_code",
            "candidate_version_name",
            "min_sdk",
            "bluetooth_min_sdk",
            "target_sdk",
            "production_variant_configured",
        },
        "plan.application",
    )
    fixed_application = {
        "application_id": APPLICATION_ID,
        "current_version_code": CURRENT_VERSION_CODE,
        "current_version_name": CURRENT_VERSION_NAME,
        "min_sdk": MIN_SDK,
        "bluetooth_min_sdk": BLUETOOTH_MIN_SDK,
        "target_sdk": TARGET_SDK,
    }
    for key, expected in fixed_application.items():
        if application[key] != expected:
            raise AdmissionError(
                f"plan.application.{key} must equal the accepted baseline"
            )
    candidate_code = _integer(
        application["candidate_version_code"],
        "plan.application.candidate_version_code",
    )
    candidate_name = _string(
        application["candidate_version_name"],
        "plan.application.candidate_version_name",
        allow_empty=True,
    )
    _boolean(
        application["production_variant_configured"],
        "plan.application.production_variant_configured",
    )
    if bool(candidate_code) != bool(candidate_name):
        raise AdmissionError(
            "candidate version code and name must be absent or present together"
        )
    if candidate_name:
        if not VERSION_NAME.fullmatch(candidate_name):
            raise AdmissionError(
                "candidate version name must be stable X.Y.Z text"
            )
        if (
            candidate_code != CURRENT_VERSION_CODE
            or candidate_name != CURRENT_VERSION_NAME
        ):
            raise AdmissionError(
                "candidate version must equal the accepted source baseline"
            )

    identity = _object(plan["release_identity"], "plan.release_identity")
    _exact_keys(
        identity,
        {"public_name", "clearance", "owner_approved_for_candidate"},
        "plan.release_identity",
    )
    public_name = _string(
        identity["public_name"], "plan.release_identity.public_name"
    )
    if len(public_name) > 80:
        raise AdmissionError("plan.release_identity.public_name is too long")
    if identity["clearance"] not in {
        "pending_professional_clearance",
        "professionally_cleared",
    }:
        raise AdmissionError("plan.release_identity.clearance is not canonical")
    _boolean(
        identity["owner_approved_for_candidate"],
        "plan.release_identity.owner_approved_for_candidate",
    )

    distribution = _object(plan["distribution"], "plan.distribution")
    _exact_keys(
        distribution,
        {
            "scope",
            "artifact_format",
            "owner_approved",
            "external_account_authority_included",
        },
        "plan.distribution",
    )
    if (
        distribution["scope"] != PRIVATE_SIDELOAD_SCOPE
        or distribution["artifact_format"] != "apk"
        or distribution["owner_approved"] is not True
    ):
        raise AdmissionError(
            "distribution must remain the approved private-sideload-v1-pilot APK scope"
        )
    if _boolean(
        distribution["external_account_authority_included"],
        "plan.distribution.external_account_authority_included",
    ):
        raise AdmissionError("the public plan cannot include external account authority")

    if plan["upgrade_mode"] != FIRST_RELEASE_UPGRADE_MODE:
        raise AdmissionError(
            "plan.upgrade_mode must identify in-place upgrade as not applicable "
            "for the first supported release"
        )

    signing = _object(plan["signing"], "plan.signing")
    _exact_keys(
        signing,
        {
            "custody_model",
            "certificate_sha256",
            "custody_approved",
            "sensitive_material_included",
        },
        "plan.signing",
    )
    if signing["custody_model"] not in {"unselected", "owner_offline"}:
        raise AdmissionError(
            "plan.signing.custody_model is not valid for private sideload"
        )
    certificate = _string(
        signing["certificate_sha256"],
        "plan.signing.certificate_sha256",
        allow_empty=True,
    )
    if certificate and not HEX64.fullmatch(certificate):
        raise AdmissionError(
            "plan.signing.certificate_sha256 must be lowercase SHA-256"
        )
    _boolean(signing["custody_approved"], "plan.signing.custody_approved")
    if _boolean(
        signing["sensitive_material_included"],
        "plan.signing.sensitive_material_included",
    ):
        raise AdmissionError("the public plan cannot include signing material")

    artifact = _object(plan["artifact_policy"], "plan.artifact_policy")
    _exact_keys(
        artifact,
        {
            "require_signed_release",
            "require_non_debuggable",
            "require_test_components_absent",
            "require_backup_excluded",
            "require_hash_manifest",
            "require_sbom",
            "authored_permissions",
            "generated_permissions",
            "forbidden_permissions",
            "forbidden_release_components",
        },
        "plan.artifact_policy",
    )
    for key in (
        "require_signed_release",
        "require_non_debuggable",
        "require_test_components_absent",
        "require_backup_excluded",
        "require_hash_manifest",
        "require_sbom",
    ):
        if not _boolean(artifact[key], f"plan.artifact_policy.{key}"):
            raise AdmissionError(f"plan.artifact_policy.{key} must be true")
    if artifact["authored_permissions"] != AUTHORED_PERMISSIONS:
        raise AdmissionError(
            "plan.artifact_policy.authored_permissions differs from the canonical list"
        )
    if artifact["generated_permissions"] != GENERATED_PERMISSIONS:
        raise AdmissionError(
            "plan.artifact_policy.generated_permissions differs from the canonical list"
        )
    if artifact["forbidden_permissions"] != FORBIDDEN_PERMISSIONS:
        raise AdmissionError(
            "plan.artifact_policy.forbidden_permissions differs from the canonical list"
        )
    if artifact["forbidden_release_components"] != FORBIDDEN_RELEASE_COMPONENTS:
        raise AdmissionError(
            "plan.artifact_policy.forbidden_release_components differs from the canonical list"
        )

    platforms = _object(plan["supported_platforms"], "plan.supported_platforms")
    _exact_keys(
        platforms,
        {
            "minimum_api_level",
            "required_phone_roles",
            "required_physical_phones",
            "matrix_approved",
        },
        "plan.supported_platforms",
    )
    if (
        _integer(
            platforms["minimum_api_level"],
            "plan.supported_platforms.minimum_api_level",
            minimum=1,
        )
        != MINIMUM_PHYSICAL_API_LEVEL
    ):
        raise AdmissionError(
            "plan.supported_platforms.minimum_api_level differs from the canonical matrix"
        )
    if platforms["required_phone_roles"] != REQUIRED_PHONE_ROLES:
        raise AdmissionError(
            "plan.supported_platforms.required_phone_roles differs from the canonical matrix"
        )
    if (
        _integer(
            platforms["required_physical_phones"],
            "plan.supported_platforms.required_physical_phones",
            minimum=1,
        )
        != REQUIRED_PHYSICAL_PHONES
    ):
        raise AdmissionError("required physical phones must be exactly two")
    _boolean(
        platforms["matrix_approved"], "plan.supported_platforms.matrix_approved"
    )

    operational = _object(plan["operational_policy"], "plan.operational_policy")
    _exact_keys(
        operational,
        {
            "policy_id",
            "candidate_binding",
            "privacy_data_safety",
            "rollback",
            "support",
        },
        "plan.operational_policy",
    )
    if operational["policy_id"] != OPERATIONAL_POLICY_ID:
        raise AdmissionError("plan.operational_policy.policy_id is not canonical")
    binding = _object(
        operational["candidate_binding"],
        "plan.operational_policy.candidate_binding",
    )
    _exact_keys(
        binding,
        {"application_id", "version_code", "version_name", "distribution_scope"},
        "plan.operational_policy.candidate_binding",
    )
    if (
        _string(
            binding["application_id"],
            "plan.operational_policy.candidate_binding.application_id",
        )
        != application["application_id"]
        or _integer(
            binding["version_code"],
            "plan.operational_policy.candidate_binding.version_code",
            minimum=1,
        )
        != candidate_code
        or _string(
            binding["version_name"],
            "plan.operational_policy.candidate_binding.version_name",
        )
        != candidate_name
        or _string(
            binding["distribution_scope"],
            "plan.operational_policy.candidate_binding.distribution_scope",
        )
        != distribution["scope"]
    ):
        raise AdmissionError(
            "plan.operational_policy.candidate_binding differs from the candidate"
        )
    _fixed_policy_object(
        operational["privacy_data_safety"],
        PRIVACY_DATA_SAFETY_POLICY,
        "plan.operational_policy.privacy_data_safety",
    )
    _fixed_policy_object(
        operational["rollback"],
        ROLLBACK_POLICY,
        "plan.operational_policy.rollback",
    )
    _fixed_policy_object(
        operational["support"],
        SUPPORT_POLICY,
        "plan.operational_policy.support",
    )

    if plan["required_acceptance_checks"] != REQUIRED_ACCEPTANCE_CHECKS:
        raise AdmissionError(
            "plan.required_acceptance_checks differs from the canonical ordered list"
        )

    _fixed_boolean_object(
        plan["privacy"],
        {
            "aggregate_only": True,
            "account_identifiers_included": False,
            "device_identifiers_included": False,
            "local_paths_included": False,
            "precise_locations_included": False,
            "raw_logs_public": False,
            "signing_material_included": False,
            "store_credentials_included": False,
        },
        "plan.privacy",
    )
    _fixed_boolean_object(
        plan["execution_authority"],
        {
            "device_install": False,
            "hardware_access": False,
            "signing_key_access": False,
            "signing_key_generation": False,
            "store_account_access": False,
            "store_upload": False,
        },
        "plan.execution_authority",
    )

    blockers = plan["blockers"]
    if not isinstance(blockers, list) or any(
        not isinstance(item, str) for item in blockers
    ):
        raise AdmissionError("plan.blockers must be a list of strings")
    derived = _derived_blockers(plan)
    if blockers != derived:
        raise AdmissionError(
            "plan.blockers must exactly match the sorted derived blocker set"
        )
    expected_status = BLOCKED_STATUS if blockers else READY_STATUS
    if plan_status != expected_status:
        raise AdmissionError(
            "plan.plan_status does not match its derived blocker state"
        )

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "android_operational_release_plan_admission",
        "plan_id": PLAN_ID,
        "plan_sha256": canonical_sha256(plan),
        "plan_status": BLOCKED_REPORT if blockers else READY_REPORT,
        "release_gate_status": RELEASE_GATE_STATUS,
        "execution_authority_granted": False,
        "blockers": blockers,
    }


def load_plan(path: Path) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
    except OSError as exc:
        raise AdmissionError("plan JSON could not be read") from exc
    if len(encoded) > MAX_PLAN_BYTES:
        raise AdmissionError("plan JSON exceeds the 65536-byte limit")
    try:
        text = encoded.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise AdmissionError("plan JSON is not valid UTF-8") from exc
    try:
        value = json.loads(text, object_pairs_hook=_reject_duplicate_object_pairs)
        _reject_excessive_nesting(value)
    except AdmissionError as exc:
        raise AdmissionError("plan JSON is malformed or contains duplicate fields") from exc
    except (ValueError, RecursionError) as exc:
        raise AdmissionError("plan JSON is malformed or contains duplicate fields") from exc
    return _object(value, "plan")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate-plan")
    validate.add_argument("--input", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(validate_plan(load_plan(args.input)), sort_keys=True))
        return 0
    except (AdmissionError, ValueError, RecursionError) as exc:
        error = (
            str(exc)
            if isinstance(exc, AdmissionError)
            else "plan JSON is malformed or exceeds the nesting limit"
        )
        report = {
            "schema": SCHEMA,
            "version": VERSION,
            "artifact_kind": "android_operational_release_plan_admission",
            "plan_status": "PLAN-INVALID",
            "release_gate_status": RELEASE_GATE_STATUS,
            "execution_authority_granted": False,
            "error": error,
        }
        print(json.dumps(report, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
