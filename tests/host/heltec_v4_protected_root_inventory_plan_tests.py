#!/usr/bin/env python3
"""Fail-closed checks for the OT-079 offline inventory contract."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
PLAN_PATH = TARGET / "protected-root-inventory-plan.json"
DESCRIPTOR_PLAN_PATH = TARGET / "protected-root-rollback-floor-descriptor-plan.json"
PROVIDER_PATH = TARGET / "protected-storage-provider-plan.json"
PROVISIONING_PATH = TARGET / "protected-storage-provisioning-plan.json"
TRANSITION_PATH = TARGET / "protected-storage-transition-plan.json"
RECOVERY_PATH = TARGET / "protected-storage-recovery-bundle-plan.json"
HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_protected_root_inventory.hpp"
SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_protected_root_inventory.cpp"
TEST_HOST = ROOT / "tools" / "Test-Host.ps1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_exact_offline_boundary() -> None:
    plan = load(PLAN_PATH)
    require(plan["schema"] == "OTPRI0/v0", "unexpected inventory schema")
    require(plan["plan_id"] == "OT-079" and
            plan["target"] == "heltec_v4_bench" and
            plan["device_family"] == "ESP32-S3",
            "inventory plan must bind the exact target class")
    require(plan["status"] ==
            "OFFLINE-PLAN-AND-VERIFIER-ACCEPTED-DEVICE-READ-AUTHORITY-ABSENT",
            "inventory plan must remain device-read closed")
    require(plan["public_result"] ==
            "OTPRI0/v0/OFFLINE-PLAN-VERIFIER-PASS",
            "unexpected fixed offline public result")
    require(plan["reader_route_plan"] ==
            "protected-root-inventory-reader-plan.json" and
            plan["reader_route_status"] ==
            "OFFLINE_METADATA_INTERFACE_ACCEPTED_EXECUTION_UNAUTHORIZED",
            "OT-080 reader route must be linked without execution authority")
    candidate = plan["candidate_provider"]
    require(plan["rollback_floor_descriptor_plan"] == DESCRIPTOR_PLAN_PATH.name,
            "inventory plan must reference the exact OT-083 descriptor review")
    require(candidate["provider_class"] is None and
            candidate["reviewed_candidate"] == {
                "provider_class": "ESP32S3_CUSTOM_USER_EFUSE_THERMOMETER",
                "result": "REJECTED-RS-CODING-UNIT-SUPPORTS-ONE-WRITE-NOT-REPEATED-ADVANCES",
            }, "custom USER_DATA candidate must be rejected")
    require(candidate["efuse_block"] is None and
            candidate["first_bit"] is None and
            candidate["capacity_bits"] is None and
            candidate["inventory_observed"] is False and
            candidate["field_selected"] is False and
            candidate["provider_admitted"] is False and
            candidate["provider_active"] is False,
            "offline plan must not invent a physical allocation")


def test_zero_authority_and_execution_surface() -> None:
    plan = load(PLAN_PATH)
    require(plan["authority"] and
            all(value is False for value in plan["authority"].values()),
            "every OT-079 authority must remain false")
    require(plan["execution_surface"] == {
        "port": None,
        "commands": [],
        "attempts": 0,
        "coarse_key_roster_leaf_present": True,
        "complete_inventory_reader_orchestrator_present": False,
    }, "offline plan must expose no device execution surface")
    initial = plan["initial_assertions"]
    require(all(value is False for value in initial.values()),
            "offline plan must claim no hardware or runtime evidence")


def test_complete_evidence_and_fixed_results() -> None:
    plan = load(PLAN_PATH)
    facts = set(plan["future_observation_envelope"]["required_facts"])
    for required in (
            "all_six_key_slots_purpose_provisioned_unused_and_protection_metadata",
            "configured_nvs_binding_and_conflict_state",
            "complete_floor_candidate_map_with_known_suitability_facts",
            "reader_close_cleanup_and_single_attempt_evidence"):
        require(required in facts, f"missing required fact: {required}")
    expected = plan["expected_security_state"]
    require(expected["secure_boot_enabled"] is False and
            expected["flash_encryption_enabled"] is False and
            expected["secure_download_enabled"] is False and
            expected["fresh_same_operation_observation_required_before_any_later_admission"] is True,
            "OT-079 must preserve the exact OT-077 security expectation")
    verifier = plan["offline_verifier_contract"]
    require(verifier["input_is_supplied_evidence_only"] is True and
            verifier["device_io_permitted"] is False and
            verifier["command_execution_permitted"] is False and
            verifier["logging_api_permitted"] is False and
            verifier["complete_unfavorable_inventory_is_reviewable"] is True and
            verifier["pass_result"] ==
            "PRIVATE-INVENTORY-CAPTURED-SELECTION-PENDING" and
            verifier["deny_result"] == "DENY-INVENTORY",
            "offline verifier contract is not exact")


def test_privacy_and_pure_source_surface() -> None:
    plan = load(PLAN_PATH)
    privacy = plan["privacy"]
    require(privacy["public_exact_device_identifiers_permitted"] is False and
            privacy["public_raw_inventory_permitted"] is False,
            "public inventory must remain sanitized")
    source = (HEADER.read_text(encoding="utf-8") + "\n" +
              SOURCE.read_text(encoding="utf-8")).lower()
    for forbidden in (
            "esp_efuse", "espefuse", "serialport", "createfile",
            "subprocess", "system(", "popen(", "fstream", "ofstream",
            "printf(", "std::cout", "std::cerr"):
        require(forbidden not in source,
                f"pure inventory evaluator contains forbidden surface: {forbidden}")
    require("evaluateprotectedrootinventory" in source and
            "sanitizeprotectedrootinventorydecision" in source,
            "expected pure evaluator and sanitizer are missing")


def test_parent_plans_remain_closed() -> None:
    for path in (PROVIDER_PATH, PROVISIONING_PATH, TRANSITION_PATH, RECOVERY_PATH):
        plan = load(path)
        require(plan["protected_root_inventory_plan"] ==
                "protected-root-inventory-plan.json",
                f"{path.name} must reference OT-079")
    for path in (PROVIDER_PATH, TRANSITION_PATH, RECOVERY_PATH):
        encoded = json.dumps(load(path), sort_keys=True).lower()
        require('"commands": []' in encoded,
                f"{path.name} must retain an empty command surface")
    provisioning = load(PROVISIONING_PATH)
    require(all(value is False for value in provisioning["physical_authority"].values()),
            "provisioning plan must retain zero physical authority")
    provider = load(PROVIDER_PATH)["protected_root_inventory"]
    require(provider == {
        "offline_plan_and_verifier_accepted": True,
        "device_inventory_observed": False,
        "exact_allocations_selected": False,
        "current_result": "DENY-DEVICE-INVENTORY-ABSENT",
    }, "provider plan must remain factually denied")


def test_host_registration() -> None:
    text = TEST_HOST.read_text(encoding="utf-8")
    require("companion protected-root inventory" in text and
            "companion_protected_root_inventory.cpp" in text and
            "companion_protected_root_inventory_tests.cpp" in text,
            "C++ inventory test is not registered")
    require("heltec_v4_protected_root_inventory_plan_tests.py" in text,
            "inventory-plan test is not registered")


def main() -> int:
    tests = (
        test_exact_offline_boundary,
        test_zero_authority_and_execution_surface,
        test_complete_evidence_and_fixed_results,
        test_privacy_and_pure_source_surface,
        test_parent_plans_remain_closed,
        test_host_registration,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"All {len(tests)} protected-root inventory plan groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
