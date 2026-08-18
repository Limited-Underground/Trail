#!/usr/bin/env python3
"""Fail-closed checks for the OT-083 rollback-floor viability review."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
PLAN_PATH = TARGET / "protected-root-rollback-floor-descriptor-plan.json"
PROVIDER_PATH = TARGET / "protected-storage-provider-plan.json"
PROVISIONING_PATH = TARGET / "protected-storage-provisioning-plan.json"
TRANSITION_PATH = TARGET / "protected-storage-transition-plan.json"
HEADER = ROOT / "firmware" / "components" / "companion" / "include" / "opentrail" / "companion_rollback_floor_descriptor_viability.hpp"
SOURCE = ROOT / "firmware" / "components" / "companion" / "src" / "companion_rollback_floor_descriptor_viability.cpp"
TEST_HOST = ROOT / "tools" / "Test-Host.ps1"

EXPECTED_SOURCES = [
    ("components/efuse/esp32s3/esp_efuse_table.csv", 26976,
     "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E"),
    ("components/efuse/esp32s3/include/esp_efuse_chip.h", 3689,
     "B5299EE67627C912C5E7A0E4A908D1678FD0D2F12D5AFD7A58D849FC1BADAA30"),
    ("components/efuse/esp32s3/esp_efuse_utility.c", 10672,
     "87EF1EA4E0B17AFEF9AB8E8939E67649A934C4CD3531FAFD07D343139B1B8E64"),
    ("components/efuse/include/esp_efuse.h", 35691,
     "4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7"),
    ("docs/en/api-reference/system/efuse.rst", 34742,
     "477703495E87597CC55DB87C78DB71399199187789624C9397F8BBF53002E9E2"),
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_exact_reviewed_source_identity() -> None:
    plan = load(PLAN_PATH)
    require(plan["schema"] == "OTRFD0/v0" and plan["plan_id"] == "OT-083",
            "unexpected OT-083 plan identity")
    require(plan["status"] ==
            "OFFLINE-VIABILITY-REVIEWED-NO-PROVIDER-SELECTED",
            "OT-083 must remain an offline rejection")
    sources = plan["reviewed_sources"]
    require(sources["esp_idf_version"] == "6.0.2",
            "unexpected ESP-IDF version")
    require([(item["path"], item["byte_length"], item["sha256"])
             for item in sources["files"]] == EXPECTED_SOURCES,
            "pinned source identity drifted")


def test_candidate_is_incompatible() -> None:
    candidate = load(PLAN_PATH)["reviewed_candidate"]
    require(candidate == {
        "provider_class": "ESP32S3_CUSTOM_USER_EFUSE_THERMOMETER",
        "user_data": {"block_class": "EFUSE_BLK3", "first_bit": 0,
                      "bit_count": 256},
        "mac_custom_overlap": {"first_bit": 200, "bit_count": 48},
        "coding_scheme": "REED_SOLOMON",
        "coding_unit_write_behavior": "SINGLE_WRITE_ONLY",
        "required_behavior": "REPEATED_INDEPENDENT_ONE_BIT_ADVANCES",
        "result": "REJECTED-INCOMPATIBLE-WITH-REQUIRED-ADVANCE-SEMANTICS",
    }, "custom USER_DATA incompatibility facts drifted")


def test_no_selection_execution_or_authority() -> None:
    plan = load(PLAN_PATH)
    require(plan["current_selection"] == {
        "provider_class": None, "efuse_block": None, "first_bit": None,
        "capacity_bits": None, "descriptor_selected": False,
        "provider_admitted": False, "provider_active": False,
    }, "no rollback-floor provider may be selected")
    require(plan["execution_surface"] == {
        "device_port": None, "commands": [], "attempts": 0,
    }, "OT-083 must have no execution surface")
    require(not any(plan["authorities"].values()),
            "OT-083 must grant no authority")
    require(plan["privacy"] == {
        "raw_device_data_present": False,
        "private_identity_or_path_present": False,
    }, "OT-083 must retain the privacy boundary")


def test_parent_plans_are_closed() -> None:
    provider = load(PROVIDER_PATH)
    provisioning = load(PROVISIONING_PATH)
    transition = load(TRANSITION_PATH)
    for parent in (provider, provisioning, transition):
        require(parent["rollback_floor_descriptor_plan"] == PLAN_PATH.name,
                "parent plan must reference OT-083")
    require(provider["rollback_floor"]["provider_class"] is None and
            provisioning["rollback_floor"]["provider"] is None and
            transition["rollback_floor_requirements"]["provider_class"] is None,
            "parent plans must keep the floor provider unselected")
    require(not any(provider["authorities"].values()) and
            not any(provisioning["physical_authority"].values()) and
            not any(transition["authorities"].values()),
            "parent plans must remain authority closed")


def test_pure_source_and_fixed_results() -> None:
    plan = load(PLAN_PATH)
    require(plan["fixed_public_results"] == [
        "REVIEWED-NO-VIABLE-CUSTOM-THERMOMETER",
        "DENY-DESCRIPTOR-VIABILITY",
    ], "public results must be fixed")
    text = (HEADER.read_text(encoding="utf-8") + "\n" +
            SOURCE.read_text(encoding="utf-8")).lower()
    for forbidden in ("esp_efuse", "serial", "subprocess", "fopen",
                      "ofstream", "printf", "esp_log", "nvs_flash",
                      "hmac", "write_efuse", "burn"):
        require(forbidden not in text,
                f"viability evaluator contains forbidden surface: {forbidden}")
    require("evaluaterollbackfloordescriptorviability" in text and
            "sanitizerollbackfloordescriptorviabilitydecision" in text,
            "pure viability evaluator or sanitizer missing")


def test_host_registration() -> None:
    text = TEST_HOST.read_text(encoding="utf-8")
    require("companion rollback-floor descriptor viability" in text and
            "companion_rollback_floor_descriptor_viability.cpp" in text and
            PLAN_PATH.with_name(
                "protected-root-rollback-floor-descriptor-plan.json").name in
            PLAN_PATH.name and
            "protected_root_rollback_floor_descriptor_plan_tests.py" in text,
            "OT-083 host registration is incomplete")


def main() -> int:
    tests = (
        test_exact_reviewed_source_identity,
        test_candidate_is_incompatible,
        test_no_selection_execution_or_authority,
        test_parent_plans_are_closed,
        test_pure_source_and_fixed_results,
        test_host_registration,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"All {len(tests)} rollback-floor descriptor plan groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
