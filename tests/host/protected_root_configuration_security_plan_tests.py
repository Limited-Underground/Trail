#!/usr/bin/env python3

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
PLAN = TARGET / "protected-root-configuration-security-plan.json"
READER_PLAN = TARGET / "protected-root-inventory-reader-plan.json"
INVENTORY_PLAN = TARGET / "protected-root-inventory-plan.json"
CONTRACT = TARGET / "target-contract.json"
HEADER = TARGET / "main" / "companion_protected_root_configuration_security_adapter.hpp"
SOURCE = TARGET / "main" / "companion_protected_root_configuration_security_adapter.cpp"
HOST_TEST = ROOT / "tests" / "host" / "companion_protected_root_configuration_security_adapter_tests.cpp"
DECISION = ROOT / "docs" / "decisions" / "0025-build-only-protected-root-configuration-security-adapter.md"
EVIDENCE = ROOT / "tests" / "hardware" / "OT-082-2026-08-18.md"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_identity_and_implementation(plan: dict) -> None:
    require(plan["schema"] == "OTPRCS0/v0", "schema drift")
    require(plan["increment"] == "OT-082", "increment drift")
    require(plan["target"] == "heltec_v4_bench" and
            plan["device_family"] == "ESP32-S3", "target drift")
    require(plan["status"] == "BUILD-COMPILED-NOT-RUNTIME-INJECTED",
            "status must remain build-only")
    implementation = plan["implementation"]
    require(implementation["header"] ==
            "main/companion_protected_root_configuration_security_adapter.hpp" and
            implementation["source"] ==
            "main/companion_protected_root_configuration_security_adapter.cpp" and
            implementation["host_test"] ==
            "tests/host/companion_protected_root_configuration_security_adapter_tests.cpp",
            "implementation paths drift")
    require(implementation["esp_idf_version"] == "6.0.2" and
            implementation["build_compiled"] is True and
            implementation["runtime_injected"] is False and
            implementation["executed"] is False and
            implementation["one_use"] is True and
            implementation["all_or_none"] is True and
            implementation["reentry_poisoned"] is True,
            "implementation boundary drift")
    for path in (HEADER, SOURCE, HOST_TEST, DECISION, EVIDENCE):
        require(path.is_file(), f"missing OT-082 evidence path: {path}")


def test_default_nvs_configuration(plan: dict) -> None:
    nvs = plan["default_build_nvs_configuration"]
    require(nvs["scope"] == "DEFAULT-BUILD-CONFIGURATION-ONLY",
            "NVS scope must not imply runtime state")
    require(nvs["symbols"] == [
        "CONFIG_NVS_ENCRYPTION",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC",
        "CONFIG_NVS_SEC_KEY_PROTECT_NONE",
        "CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID",
    ], "NVS symbol set drift")
    require(nvs["normalized_modes"] == [
        "disabled", "hmac", "flash_encryption", "external"
    ], "NVS normalized modes drift")
    current = nvs["current_target_build"]
    require(current == {
        "nvs_encryption_enabled": False,
        "protection_mode": "disabled",
        "configured_hmac_key_slot_known": False,
        "configured_hmac_key_slot": None,
    }, "current target NVS build configuration drift")
    require(nvs["runtime_scheme_override_observed"] is False and
            nvs["configured_binding_conflict_resolved"] is False and
            nvs["complete_inventory_output"] is False,
            "build configuration must not claim complete NVS evidence")


def test_security_source(plan: dict) -> None:
    security = plan["security_state_source"]
    require(security["exact_order"] == [
        "esp_secure_boot_enabled",
        "esp_efuse_is_flash_encryption_enabled",
        "esp_efuse_read_field_bit:ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD",
        "esp_efuse_read_field_bit:ESP_EFUSE_DIS_DOWNLOAD_MODE",
    ], "security call order drift")
    require(security["normalized_fields"] == [
        "secure_boot_enabled", "flash_encryption_enabled",
        "secure_download_enabled", "download_mode_disabled"
    ], "security normalized field drift")
    require(all(value is None for value in
                security["current_device_observation"].values()),
            "build-only plan must retain no device security observation")
    require(security[
        "expected_recovery_compatible_values_are_not_observations"] is True,
            "expected values must remain distinct from observations")


def test_source_pins_and_forbidden_surface(plan: dict) -> None:
    pins = plan["source_pins"]
    require(len(pins) == 11, "exact pinned source count drift")
    require(len({pin["path"] for pin in pins}) == 11 and
            all(len(pin["sha256"]) == 64 for pin in pins),
            "source pins must be unique exact SHA-256 values")
    forbidden = set(plan["forbidden_surfaces"])
    for token in (
            "nvs_flash_read_security_cfg", "nvs_flash_read_security_cfg_v2",
            "nvs_flash_generate_keys", "nvs_flash_generate_keys_v2",
            "esp_hmac_calculate", "esp_efuse_get_key",
            "esp_efuse_read_block", "esp_efuse_read_field_blob",
            "esp_efuse_write", "esp_efuse_batch_write", "erase", "reset",
            "logging", "serialization", "transport"):
        require(token in forbidden, f"missing forbidden surface: {token}")


def test_execution_and_result_boundary(plan: dict) -> None:
    execution = plan["execution_surface"]
    require(execution["commands"] == [] and execution["attempts"] == 0,
            "execution surface must be empty")
    require(execution["selected_unit"] is None and execution["port"] is None and
            execution["operation_id"] is None and
            execution["evidence_set_id"] is None,
            "private execution binding must remain absent")
    authority_values = [value for key, value in execution.items()
                        if key.endswith("_authorized")]
    require(authority_values and all(value is False for value in authority_values),
            "all OT-082 authorities must remain false")
    result = plan["current_result"]
    require(result["adapter_build_evidence_accepted"] is True and
            result["device_metadata_observed"] is False and
            result["configured_nvs_conflict_known"] is False and
            result["security_context_complete"] is False and
            result["inventory_admissible"] is False and
            result["provider_admitted"] is False and
            result["score_change_supported"] is False,
            "build evidence must not become inventory or authority")


def test_parent_plan_and_contract_coherence(plan: dict) -> None:
    reader = load(READER_PLAN)
    inventory = load(INVENTORY_PLAN)
    contract = load(CONTRACT)
    capabilities = contract["capabilities"]
    plan_ref = "protected-root-configuration-security-plan.json"
    require(reader["configuration_security_plan"] == plan_ref and
            inventory["configuration_security_plan"] == plan_ref,
            "parent plans must reference OT-082")
    require(reader["accepted_target_side_route"][
                "configuration_security_build_evidence"]["increment"] ==
            "OT-082" and
            inventory["configuration_security_adapter_status"] ==
            "BUILD-COMPILED-NOT-RUNTIME-INJECTED" and
            inventory["configuration_security_adapter_executed"] is False,
            "parent plans must preserve exact OT-082 build-only evidence")
    require(reader["complete_inventory_reader_orchestrator_present"] is False and
            inventory["candidate_provider"]["inventory_observed"] is False,
            "OT-082 must not create a complete reader or inventory")
    require(capabilities[
        "protected_root_configuration_security_adapter_build_compiled"] is True and
            capabilities[
                "protected_root_configuration_security_adapter_runtime_injected"] is False and
            capabilities[
                "protected_root_configuration_security_adapter_executed"] is False and
            capabilities[
                "protected_root_nvs_metadata_device_read_authorized"] is False and
            capabilities[
                "protected_root_security_state_device_read_authorized"] is False,
            "target contract must preserve build-only authority boundary")
    require(plan["next_gate"].startswith(
        "Accept the exact build and host/static evidence"), "next gate drift")


def main() -> None:
    plan = load(PLAN)
    tests = (
        test_identity_and_implementation,
        test_default_nvs_configuration,
        test_security_source,
        test_source_pins_and_forbidden_surface,
        test_execution_and_result_boundary,
        test_parent_plan_and_contract_coherence,
    )
    for test in tests:
        test(plan)
    print(f"PASS: {len(tests)} protected-root configuration/security plan groups")


if __name__ == "__main__":
    main()
