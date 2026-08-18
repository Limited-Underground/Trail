#!/usr/bin/env python3
"""Fail-closed admission checks for the OT-078 offline provider plan."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
PLAN_PATH = TARGET / "protected-storage-provider-plan.json"
PROVISIONING_PATH = TARGET / "protected-storage-provisioning-plan.json"
TRANSITION_PATH = TARGET / "protected-storage-transition-plan.json"
KEY_HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_protected_key_provider_admission.hpp"
KEY_SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_protected_key_provider_admission.cpp"
FLOOR_HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_authorization_rollback_floor_provider.hpp"
FLOOR_SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_authorization_rollback_floor_provider.cpp"
VIABILITY_HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_rollback_floor_descriptor_viability.hpp"
VIABILITY_SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_rollback_floor_descriptor_viability.cpp"
DESCRIPTOR_PLAN_PATH = TARGET / "protected-root-rollback-floor-descriptor-plan.json"
TEST_HOST = ROOT / "tools" / "Test-Host.ps1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_exact_offline_selection() -> None:
    plan = load(PLAN_PATH)
    require(plan["schema"] == "OTPRP0/v0", "unexpected provider schema")
    require(plan["status"] ==
            "OFFLINE-KEY-PROVIDER-TYPES-SELECTED-ROLLBACK-PROVIDER-UNSELECTED",
            "provider plan must stay physical-admission closed")
    selection = plan["provider_type_selection"]
    require(selection == {
        "protected_key_roles_accepted_offline": True,
        "rollback_floor_provider_selected_offline": False,
        "physical_provider_admitted": False,
        "runtime_activation_admitted": False,
    }, "offline selection must grant no physical or runtime admission")

    roles = plan["protected_key_roles"]
    require(roles["provider_kind"] == "ESP32S3_HMAC_UP_EFUSE",
            "both key roles must use the accepted provider class")
    for name, expected_role in (
            ("nvs_encryption", "OT_AUTH_NVS_ENCRYPTION_ONLY"),
            ("bond_binding_prf", "PRIVATE_BOND_BINDING_PRF_ONLY")):
        role = roles[name]
        require(role["role"] == expected_role and
                role["purpose"] == "HMAC_UP" and
                role["key_block_id"] is None and
                role["provider_selected_offline"] is True and
                role["block_selected"] is False and
                role["provisioned"] is False and
                role["read_protection_observed"] is None and
                role["operational_self_test"] is None,
                f"{name} must be selected by type but physically unknown")
    require(roles["distinct_blocks_required"] is True and
            roles["factual_pair_admission_current_result"] == "DENY",
            "key pair must remain factually denied")


def test_floor_candidate_is_rejected_and_unallocated() -> None:
    floor = load(PLAN_PATH)["rollback_floor"]
    require(floor["provider_class"] is None and
            floor["selected_offline_conditionally"] is False and
            floor["descriptor_plan"] == DESCRIPTOR_PLAN_PATH.name,
            "rollback-floor provider must remain unselected")
    require(floor["reviewed_candidate"] == {
        "provider_class": "ESP32S3_CUSTOM_USER_EFUSE_THERMOMETER",
        "result": "REJECTED-RS-CODING-UNIT-SUPPORTS-ONE-WRITE-NOT-REPEATED-ADVANCES",
    }, "custom USER_DATA candidate must be explicitly rejected")
    require(floor["exact_efuse_block"] is None and
            floor["first_bit"] is None and
            floor["capacity_bits"] is None and
            floor["inventory_observed"] is False and
            floor["protection_state_observed"] is False and
            floor["provisioned"] is False and floor["active"] is False,
            "rejected floor must not invent a physical allocation")
    require(floor["advance_step"] == 1 and
            floor["post_advance_exact_reread_required"] is True and
            floor["exhaustion_result"] == "PERMANENT-DENY" and
            floor["decrement_wrap_reseed_erase_allowed"] is False and
            floor["production_anti_tamper_claimed"] is False and
            floor["physical_provider_admission_current_result"] == "DENY",
            "floor failure and claim boundaries must remain closed")


def test_no_execution_or_authority() -> None:
    plan = load(PLAN_PATH)
    execution = plan["execution_surface"]
    require(execution == {
        "device_port": None,
        "commands": [],
        "attempts": 0,
        "device_access_authorized": False,
    }, "provider plan must contain no executable device surface")
    require(not any(plan["authorities"].values()),
            "provider plan must grant no authority")
    require(plan["current_result"] == "DENY-PHYSICAL-ADMISSION",
            "current physical result must deny")


def test_parent_plans_remain_denied() -> None:
    provider = load(PLAN_PATH)
    provisioning = load(PROVISIONING_PATH)
    transition = load(TRANSITION_PATH)
    require(provisioning["provider_plan"] == PLAN_PATH.name and
            transition["provider_plan"] == PLAN_PATH.name,
            "both parent plans must reference the exact provider plan")
    require(provisioning["key_roles"]["provider_types_selected_offline"] is True and
            provisioning["key_roles"]["physical_pair_admitted"] is False and
            provisioning["rollback_floor"]["provider"] is None and
            provisioning["rollback_floor"]["exact_field_selected"] is False,
            "provisioning must distinguish type selection from admission")
    role_requirements = transition["protected_key_role_requirements"]
    require(role_requirements["provider_kind"] ==
            provider["protected_key_roles"]["provider_kind"] and
            role_requirements["nvs_encryption_hmac_key"]["key_block_id"] is None and
            role_requirements["bond_binding_prf_hmac_key"]["key_block_id"] is None and
            role_requirements["current_result"] == "DENY",
            "transition key-role admission must remain denied")
    require(transition["rollback_floor_requirements"]["exact_field_selected"] is False and
            transition["rollback_floor_requirements"]["provider_class"] is None and
            transition["rollback_floor_requirements"]["current_result"] == "DENY" and
            transition["promotion_admission"]["current_result"] == "DENY" and
            not any(transition["authorities"].values()),
            "transition and floor authorities must remain denied")


def test_pure_source_surface_and_registration() -> None:
    combined = "\n".join(path.read_text(encoding="utf-8") for path in
                         (KEY_HEADER, KEY_SOURCE, FLOOR_HEADER, FLOOR_SOURCE,
                          VIABILITY_HEADER, VIABILITY_SOURCE))
    forbidden = (
        "esp_efuse", "esp_hmac", "nvs_flash", "nvs_open", "SerialPort",
        "esptool", "subprocess", "CreateFile", "fopen", "ofstream",
        "printf", "ESP_LOG", "erase", "reset_device",
    )
    for token in forbidden:
        require(token not in combined,
                f"offline provider source contains forbidden surface: {token}")
    harness = TEST_HOST.read_text(encoding="utf-8")
    for expected in (
        "companion protected-key provider admission",
        "companion authorization rollback-floor provider",
        "companion_protected_key_provider_admission.cpp",
        "companion_authorization_rollback_floor_provider.cpp",
        "companion rollback-floor descriptor viability",
        "companion_rollback_floor_descriptor_viability.cpp",
    ):
        require(expected in harness, f"host matrix missing {expected}")


def main() -> int:
    tests = (
        test_exact_offline_selection,
        test_floor_candidate_is_rejected_and_unallocated,
        test_no_execution_or_authority,
        test_parent_plans_remain_denied,
        test_pure_source_surface_and_registration,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} protected-storage provider-plan groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
