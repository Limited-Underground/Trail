#!/usr/bin/env python3
"""Fail-closed checks for the OT-084 SECURE_VERSION review."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
PLAN_PATH = TARGET / "protected-root-secure-version-floor-plan.json"
DESCRIPTOR_PATH = TARGET / "protected-root-rollback-floor-descriptor-plan.json"
PROVIDER_PATH = TARGET / "protected-storage-provider-plan.json"
PROVISIONING_PATH = TARGET / "protected-storage-provisioning-plan.json"
TRANSITION_PATH = TARGET / "protected-storage-transition-plan.json"
INVENTORY_PATH = TARGET / "protected-root-inventory-plan.json"
HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_rollback_floor_secure_version_viability.hpp"
SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_rollback_floor_secure_version_viability.cpp"
TEST_HOST = ROOT / "tools" / "Test-Host.ps1"

EXPECTED_SOURCES = [
    ("components/bootloader/Kconfig.app_rollback", 3725,
     "718AE775CE4FEB58A5EB4A0BBAF85E91D4DB9DA1E636F5121FA430AFC2DA9D38"),
    ("components/esp_app_format/esp_app_desc.c", 5541,
     "6CB345372D4C5786BF023B4D076B62B63BC86CC084242B6B9984F49E2D86A45D"),
    ("components/efuse/src/esp_efuse_fields.c", 8879,
     "E7C04ACDF54CDA0EFF2F2AC7551D6B25CB782E62A2F221C0E4B31DDC37D57AB5"),
    ("components/bootloader_support/src/bootloader_utility.c", 55388,
     "A61F5FFF38616B9A6650C7675F149855B0E39B00D2E83651CC5C5ACAE67AE861"),
    ("components/app_update/esp_ota_ops.c", 52230,
     "AFE45475F68C5952BFAEA73E8DC200E963DF4B9036E67EC17418CC4EA2986290"),
    ("components/efuse/esp32s3/esp_efuse_table.csv", 26976,
     "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E"),
    ("docs/en/api-reference/system/ota.rst", 24209,
     "9DD27882AE44E1286DDC58967DF906523602A6381A6C39F21B788A5D9EB59303"),
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_exact_source_bound_review() -> None:
    plan = load(PLAN_PATH)
    require(plan["schema"] == "OTSVF0/v0" and plan["plan_id"] == "OT-084",
            "unexpected OT-084 identity")
    require(plan["status"] ==
            "OFFLINE-REVIEWED-SECURE-VERSION-NOT-ADMITTED",
            "OT-084 must remain an offline rejection")
    reviewed = plan["reviewed_sources"]
    require(reviewed["esp_idf_version"] == "6.0.2",
            "unexpected ESP-IDF version")
    require([(item["path"], item["byte_length"], item["sha256"])
             for item in reviewed["files"]] == EXPECTED_SOURCES,
            "pinned source identity drifted")


def test_secure_version_is_rejected_for_exact_reasons() -> None:
    primitive = load(PLAN_PATH)["reviewed_primitive"]
    require(primitive["field"] == {
        "block": "EFUSE_BLK0", "first_bit": 142,
        "bit_count": 16, "coding_scheme": "NONE",
    }, "SECURE_VERSION field identity drifted")
    require(primitive["maximum_advances"] == 16 and
            primitive["exhaustion"] == "PERMANENT" and
            primitive["native_role"] ==
            "APPLICATION-FIRMWARE-ANTI-ROLLBACK" and
            primitive["native_partition_requirement"] ==
            "OTA_0-PLUS-OTA_1-WITHOUT-FACTORY-OR-TEST" and
            primitive["current_layout_contains_factory"] is True and
            primitive["accepted_recovery_route_restores_factory"] is True and
            primitive["authorization_floor_must_be_independent_from_firmware_version"] is True,
            "SECURE_VERSION coupling facts drifted")
    for key in ("shared_firmware_version_budget_accepted",
                "sixteen_transition_budget_accepted",
                "factory_recovery_redesign_accepted",
                "trusted_firmware_policy_accepted"):
        require(primitive[key] is False, f"{key} must remain unaccepted")
    require(primitive["result"] == "REJECTED-COUPLED-NOT-INDEPENDENT",
            "SECURE_VERSION must remain rejected")


def test_external_class_is_future_only_and_unselected() -> None:
    plan = load(PLAN_PATH)
    external = plan["external_monotonic_hardware"]
    require(external["status"] == "FUTURE-HARDWARE-REVISION-OPTION-ONLY" and
            external["selected"] is False and
            external["part_selected"] is False and
            external["present_on_current_target"] is False and
            external["current_target_supported"] is False,
            "external hardware must not become a current-target claim")
    require(len(external["required_later_decisions"]) == 9,
            "external architecture gates must remain explicit")
    require(plan["current_selection"] == {
        "provider_class": None, "part": None, "descriptor": None,
        "capacity": None, "provider_selected": False,
        "provider_admitted": False, "provider_active": False,
    }, "no provider may be selected")


def test_no_execution_authority_or_private_data() -> None:
    plan = load(PLAN_PATH)
    require(plan["execution_surface"] == {
        "device_port": None, "commands": [], "attempts": 0,
    }, "OT-084 must have no execution surface")
    require(not any(plan["authorities"].values()),
            "OT-084 must grant no authority")
    require(plan["privacy"] == {
        "raw_device_data_present": False,
        "private_identity_or_path_present": False,
    }, "OT-084 must retain the privacy boundary")
    require(plan["fixed_public_results"] == [
        "REVIEWED-SECURE-VERSION-COUPLED-NOT-ADMITTED",
        "DENY-SECURE-VERSION-VIABILITY",
    ], "fixed public results drifted")


def test_parent_plans_stay_closed_and_linked() -> None:
    descriptor = load(DESCRIPTOR_PATH)
    provider = load(PROVIDER_PATH)
    provisioning = load(PROVISIONING_PATH)
    transition = load(TRANSITION_PATH)
    inventory = load(INVENTORY_PATH)
    for parent in (descriptor, provider, provisioning, transition, inventory):
        require(parent["secure_version_floor_plan"] == PLAN_PATH.name,
                "parent plan must reference OT-084")
    require(descriptor["current_selection"]["provider_class"] is None and
            provider["rollback_floor"]["provider_class"] is None and
            provisioning["rollback_floor"]["provider"] is None and
            transition["rollback_floor_requirements"]["provider_class"] is None and
            inventory["candidate_provider"]["provider_class"] is None,
            "parent plans must retain null providers")
    require(not any(descriptor["authorities"].values()) and
            not any(provider["authorities"].values()) and
            not any(provisioning["physical_authority"].values()) and
            not any(transition["authorities"].values()) and
            not any(inventory["authority"].values()),
            "parent plans must remain authority closed")


def test_pure_evaluator_and_host_registration() -> None:
    text = (HEADER.read_text(encoding="utf-8") + "\n" +
            SOURCE.read_text(encoding="utf-8")).lower()
    for forbidden in ("esp_efuse_write", "write_field_blob", "serial",
                      "subprocess", "fopen", "ofstream", "printf",
                      "esp_log", "nvs_flash", "hmac", "write_efuse"):
        require(forbidden not in text,
                f"SECURE_VERSION evaluator contains forbidden surface: {forbidden}")
    require("evaluatesecureversionfloorviability" in text and
            "sanitizesecureversionfloorviabilitydecision" in text,
            "pure evaluator or sanitizer missing")
    harness = TEST_HOST.read_text(encoding="utf-8")
    for expected in ("companion SECURE_VERSION rollback-floor viability",
                     "companion_rollback_floor_secure_version_viability.cpp",
                     "protected_root_secure_version_floor_plan_tests.py"):
        require(expected in harness, f"host registration missing {expected}")


def main() -> int:
    tests = (
        test_exact_source_bound_review,
        test_secure_version_is_rejected_for_exact_reasons,
        test_external_class_is_future_only_and_unselected,
        test_no_execution_authority_or_private_data,
        test_parent_plans_stay_closed_and_linked,
        test_pure_evaluator_and_host_registration,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"All {len(tests)} SECURE_VERSION floor-plan groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
