#!/usr/bin/env python3
"""Fail-closed source admission for the experimental Heltec V4 target."""

from __future__ import annotations

import csv
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
CONTRACT = TARGET / "target-contract.json"
PHYSICAL_FLASH_PLAN = TARGET / "physical-flash-plan.json"
OLED_STARTUP_FLASH_PLAN = TARGET / "oled-startup-flash-plan.json"
PROTECTED_STORAGE_CANDIDATE_PARTITIONS = (
    TARGET / "protected-storage-partitions.candidate.csv")
PROTECTED_STORAGE_PROVISIONING_PLAN = TARGET / "protected-storage-provisioning-plan.json"
SOURCE = TARGET / "main" / "app_main.cpp"
SELF_CHECK = TARGET / "main" / "companion_boot_self_check.cpp"
NIMBLE_GATT = TARGET / "main" / "companion_nimble_gatt.cpp"
NIMBLE_RUNTIME = TARGET / "main" / "companion_nimble_runtime.cpp"
ANDROID_TERMINATION_POLICY = (
    ROOT / "android" / "app" / "src" / "debug" / "kotlin" / "io" /
    "github" / "nbjelanovic" / "otclient" /
    "PublicLinkAutomaticTerminationPolicy.kt")
ANDROID_TERMINATION_INSTRUMENTATION = (
    ROOT / "android" / "app" / "src" / "androidTest" / "kotlin" / "io" /
    "github" / "nbjelanovic" / "otclient" /
    "PublicLinkProbeInstrumentation.kt")
ANDROID_APP_BUILD = ROOT / "android" / "app" / "build.gradle.kts"
BLE_RUNTIME_OWNER = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_ble_runtime_owner.cpp"
)
GATT_SESSION = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_gatt_session.cpp"
)
GATT_AUTHORIZATION = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_gatt_authorization.cpp"
)
GATT_AUTHORIZATION_ADAPTER = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_gatt_authorization_adapter.cpp"
)
AUTHORIZATION_PERSISTENCE = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_authorization_persistence.cpp"
)
AUTHORIZATION_STORAGE = TARGET / "main" / "companion_authorization_storage.cpp"
AUTHORIZATION_NVS_BACKEND_HEADER = (
    TARGET / "main" / "companion_authorization_nvs_backend.hpp")
AUTHORIZATION_NVS_BACKEND = (
    TARGET / "main" / "companion_authorization_nvs_backend.cpp")
AUTHORIZATION_NVS_CONTEXT_HEADER = (
    TARGET / "main" / "companion_authorization_nvs_context.hpp")
AUTHORIZATION_NVS_CONTEXT = (
    TARGET / "main" / "companion_authorization_nvs_context.cpp")
PROTECTED_ROOT_KEY_ROSTER_HEADER = (
    TARGET / "main" / "companion_protected_root_key_roster_adapter.hpp")
PROTECTED_ROOT_KEY_ROSTER = (
    TARGET / "main" / "companion_protected_root_key_roster_adapter.cpp")
PROTECTED_ROOT_CONFIGURATION_SECURITY_HEADER = (
    TARGET / "main" /
    "companion_protected_root_configuration_security_adapter.hpp")
PROTECTED_ROOT_CONFIGURATION_SECURITY = (
    TARGET / "main" /
    "companion_protected_root_configuration_security_adapter.cpp")
MAIN_CMAKE = TARGET / "main" / "CMakeLists.txt"
COMPANION_SOURCES = (
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_protocol.cpp",
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_semantics.cpp",
    ROOT / "firmware" / "components" / "companion" / "src" /
    "companion_request_coordinator.cpp",
)
DEFAULTS = TARGET / "sdkconfig.defaults"
PARTITIONS = TARGET / "partitions.csv"
BUILD_SCRIPT = ROOT / "tools" / "Build-HeltecV4BenchTarget.ps1"
DISPLAY_OWNER_HEADER = TARGET / "main" / "heltec_startup_display.hpp"
DISPLAY_OWNER = TARGET / "main" / "heltec_startup_display.cpp"
BATTERY_HEADER = TARGET / "main" / "heltec_v4_battery.hpp"
BATTERY_SOURCE = TARGET / "main" / "heltec_v4_battery.cpp"
GNSS_HEADER = TARGET / "main" / "heltec_v4_gnss.hpp"
GNSS_SOURCE = TARGET / "main" / "heltec_v4_gnss.cpp"
PAIRING_INPUT_HEADER = TARGET / "main" / "heltec_v4_pairing_input.hpp"
PAIRING_INPUT_SOURCE = TARGET / "main" / "heltec_v4_pairing_input.cpp"
SECURE_RANDOM_HEADER = TARGET / "main" / "heltec_v4_secure_random.hpp"
SECURE_RANDOM_SOURCE = TARGET / "main" / "heltec_v4_secure_random.cpp"
DISPLAY_ADAPTER_HEADER = TARGET / "main" / "heltec_v4_oled.hpp"
DISPLAY_ADAPTER = TARGET / "main" / "heltec_v4_oled.cpp"
DISPLAY_LOGO = TARGET / "main" / "trail_startup_logo.hpp"
COMPACT_FOOTER_HEADER = (
    ROOT / "firmware" / "components" / "ui" / "include" / "opentrail" /
    "compact_status_footer.hpp")
COMPACT_FOOTER = (
    ROOT / "firmware" / "components" / "ui" / "src" /
    "compact_status_footer.cpp")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_contract() -> None:
    expected_files = {
        "CMakeLists.txt",
        "README.md",
        "main/CMakeLists.txt",
        "main/app_main.cpp",
        "main/companion_boot_self_check.cpp",
        "main/companion_boot_self_check.hpp",
        "main/companion_authorization_storage.cpp",
        "main/companion_authorization_storage.hpp",
        "main/companion_authorization_nvs_backend.cpp",
        "main/companion_authorization_nvs_backend.hpp",
        "main/companion_authorization_nvs_context.cpp",
        "main/companion_authorization_nvs_context.hpp",
        "main/companion_protected_root_key_roster_adapter.cpp",
        "main/companion_protected_root_key_roster_adapter.hpp",
        "main/companion_protected_root_configuration_security_adapter.cpp",
        "main/companion_protected_root_configuration_security_adapter.hpp",
        "main/companion_nimble_gatt.cpp",
        "main/companion_nimble_gatt.hpp",
        "main/companion_nimble_runtime.cpp",
        "main/companion_nimble_runtime.hpp",
        "partitions.csv",
        "main/heltec_startup_display.cpp",
        "main/heltec_startup_display.hpp",
        "main/heltec_v4_battery.cpp",
        "main/heltec_v4_battery.hpp",
        "main/heltec_v4_gnss.cpp",
        "main/heltec_v4_gnss.hpp",
        "main/heltec_v4_pairing_input.cpp",
        "main/heltec_v4_pairing_input.hpp",
        "main/heltec_v4_secure_random.cpp",
        "main/heltec_v4_secure_random.hpp",
        "main/heltec_v4_oled.cpp",
        "main/heltec_v4_oled.hpp",
        "main/trail_startup_logo.hpp",
        "oled-startup-flash-plan.json",
        "physical-flash-plan.json",
        "protected-storage-partitions.candidate.csv",
        "protected-storage-provider-plan.json",
        "protected-root-inventory-plan.json",
        "protected-root-inventory-reader-plan.json",
        "protected-root-configuration-security-plan.json",
        "protected-root-rollback-floor-descriptor-plan.json",
        "protected-root-secure-version-floor-plan.json",
        "protected-storage-provisioning-plan.json",
        "protected-storage-recovery-bundle-plan.json",
        "protected-storage-transition-read-plan.json",
        "protected-storage-transition-plan.json",
        "sdkconfig.defaults",
        "target-contract.json",
    }
    observed_files = {
        path.relative_to(TARGET).as_posix()
        for path in TARGET.rglob("*")
        if path.is_file()
    }
    require(observed_files == expected_files,
            "target file surface changed without admission review")

    document = json.loads(CONTRACT.read_text(encoding="utf-8"))
    require(document["schema"] == "OTTB0", "unexpected contract schema")
    require(document["schema_version"] == 0, "unexpected schema version")
    require(document["evidence_state"] == "experimented", "must remain experimental")
    require(document["framework"] == {
        "name": "esp-idf",
        "version": "v6.0.2",
        "target": "esp32s3",
    }, "framework must be exactly pinned")
    require(document["hardware"]["exact_received_revision"] is None,
            "received revision must not be invented")
    require(document["hardware"] == {
        "family_candidate": "Heltec WiFi LoRa 32 V4",
        "evidence_unit": "OT-DEV-001",
        "processor": {
            "observed_family": "esp32s3",
            "observed_revision": "v0.2",
            "family_profile_part": "ESP32-S3R2",
        },
        "flash": {
            "observed_bytes": 16777216,
            "observed_io_capability": "quad",
            "build_mode": "qio",
            "build_frequency_mhz": 80,
            "physical_frequency_verified": False,
        },
        "psram": {
            "observed_bytes": 2097152,
            "observed_voltage": "AP_3v3",
            "build_mode": "quad",
            "build_frequency_mhz": 80,
            "physical_interface_and_frequency_verified": False,
        },
        "exact_received_revision": None,
        "rf_variant": None,
        "display_candidate": {
            "controller": "SSD1315-compatible",
            "width": 128,
            "height": 64,
            "bits_per_pixel": 1,
            "i2c_address": "0x3c",
            "i2c_clock_hz": 400000,
            "sda_gpio": 17,
            "scl_gpio": 18,
            "reset_gpio": 21,
            "vext_control_gpio": 36,
            "vext_enable_level": 0,
            "exact_binding_physically_verified": True,
        },
        "supported": False,
    }, "hardware profile must match only recorded OT-DEV-001 evidence")
    require(document["hardware"]["supported"] is False,
            "candidate must not claim support")
    require(document["partition_layout"] == {
        "schema": "OTHP0",
        "schema_version": 0,
        "file": "partitions.csv",
        "flash_bytes": 16777216,
        "factory_slot_bytes": 5177344,
        "ota_slot_bytes": 5242880,
        "ota_slot_count": 2,
        "reserved_state_partition": "ot_state",
        "reserved_state_bytes": 1048576,
        "ends_at_flash_boundary": True,
        "updater_authority": False,
        "ota_authority": False,
        "storage_authority": False,
        "recovery_authority": False,
    }, "unexpected recovery partition contract")
    require(document["recovery"] == {
        "sacrificial_first_candidate": "OT-DEV-001",
        "sacrificial_first_unit": "OT-DEV-001",
        "physical_flash_plan": "physical-flash-plan.json",
        "oled_startup_flash_plan": "oled-startup-flash-plan.json",
        "meshcore_state_preservation_required": False,
        "meshcore_restore_route": "owner-operated official MeshCore web flasher",
        "prior_rom_entry_and_restore_recorded": True,
        "manual_rom_entry_exit_rehearsed": True,
        "meshcore_restore_executed": False,
        "physical_write_authorized": False,
        "authorized_physical_write_completed": True,
        "additional_physical_write_authorized": False,
        "public_region_verification_passed": True,
        "bounded_runtime_accepted": True,
        "oled_factory_app_write_completed": True,
        "public_link_factory_app_write_completed": True,
        "write_attempts": 5,
    }, "physical execution evidence must remain exact and further authority absent")


    capabilities = document["capabilities"]
    admitted = {
        "bounded_usb_heartbeat",
        "boot_companion_codec_self_check",
        "boot_companion_request_self_check",
        "boot_companion_gatt_session_self_check",
        "boot_companion_gatt_authorization_self_check",
        "boot_companion_gatt_authorization_adapter_self_check",
        "boot_companion_authorization_storage_self_check",
        "boot_companion_ble_runtime_owner_self_check",
        "nimble_gatt_definition_build_linked",
        "nimble_gatt_callback_adapter_build_linked",
        "companion_authorization_persistence_build_linked",
        "companion_authorization_storage_preflight_build_linked",
        "companion_authorization_storage_read_only_probe_build_linked",
        "companion_authorization_nvs_backend_build_compiled",
        "companion_authorization_nvs_context_build_compiled",
        "protected_root_key_roster_adapter_build_compiled",
        "protected_root_configuration_security_adapter_build_compiled",
        "nimble_runtime_owner_build_linked",
        "nimble_runtime_startup_coded",
        "companion_public_link_info_build_linked",
        "public_link_characteristic_runtime_coded",
        "bounded_public_link_window_host_tested",
        "public_link_connection_physically_observed",
        "public_link_info_physically_read",
        "public_link_automatic_termination_physically_observed",
        "public_link_compatible_advertiser_return_physically_observed",
        "private_service_advertising_coded",
        "evidence_bound_memory_profile_build_configured",
        "recovery_partition_layout_build_configured",
        "bounded_usb_heartbeat_physically_observed",
        "nimble_runtime_startup_physically_reached",
        "ble_service_advertising_physically_observed",
        "ble_pairing_window_host_tested",
        "ble_pairing_window_build_linked",
        "ble_pairing_window_secure_random_build_linked",
        "ble_pairing_input_gpio0_build_linked",
        "ble_pairing_input_physically_observed",
        "ble_pairing_six_digit_display_physically_observed",
        "ble_pairing_timeout_concealment_physically_observed",
        "ble_pairing_reset_concealment_physically_observed",
        "oled_startup_display_owner_host_tested",
        "oled_startup_display_build_linked",
        "oled_compact_status_footer_host_tested",
        "oled_compact_status_footer_build_linked",
        "oled_compact_status_footer_physically_observed",
        "oled_compact_status_footer_live_telemetry_bound",
        "oled_startup_logo_coded",
        "oled_ble_phase_status_coded",
        "oled_ble_link_status_physically_observed",
        "oled_startup_display_physically_observed",
    }
    require(capabilities["bounded_usb_heartbeat"] is True,
            "heartbeat must remain admitted")
    require(capabilities["boot_companion_codec_self_check"] is True,
            "boot companion-codec self-check must be admitted")
    require(capabilities["boot_companion_request_self_check"] is True,
            "boot companion-request self-check must be admitted")
    require(capabilities["boot_companion_gatt_session_self_check"] is True,
            "boot companion GATT-session self-check must be admitted")
    require(capabilities["boot_companion_gatt_authorization_self_check"] is True,
            "boot restricted authorization self-check must be admitted")
    require(capabilities["boot_companion_gatt_authorization_adapter_self_check"] is True,
            "boot callback-adapter self-check must be admitted")
    require(capabilities["boot_companion_authorization_storage_self_check"] is True,
            "boot authorization-storage self-check must be admitted")
    require(capabilities["boot_companion_ble_runtime_owner_self_check"] is True,
            "boot BLE-runtime-owner self-check must be admitted")
    require(capabilities["nimble_gatt_definition_build_linked"] is True,
            "NimBLE GATT definition build linkage must be admitted")
    require(capabilities["nimble_gatt_callback_adapter_build_linked"] is True,
            "NimBLE callback adapter build linkage must be admitted")
    require(capabilities["companion_authorization_persistence_build_linked"] is True,
            "authorization persistence build linkage must be admitted")
    require(capabilities["companion_authorization_storage_preflight_build_linked"] is True,
            "authorization storage preflight build linkage must be admitted")
    require(capabilities[
        "companion_authorization_storage_read_only_probe_build_linked"] is True,
            "read-only storage probe build linkage must be admitted")
    require(capabilities[
        "companion_authorization_nvs_backend_build_compiled"] is True,
            "dormant authorization NVS backend compilation must be admitted")
    require(capabilities[
        "companion_authorization_nvs_context_build_compiled"] is True,
            "dormant authorization NVS context compilation must be admitted")
    require(capabilities[
        "protected_root_key_roster_adapter_build_compiled"] is True,
            "dormant protected-root key-roster adapter must be build-compiled")
    require(capabilities[
        "protected_root_key_roster_adapter_runtime_injected"] is False and
            capabilities[
                "protected_root_key_roster_adapter_executed"] is False and
            capabilities[
                "protected_root_key_metadata_device_read_authorized"] is False and
            capabilities[
                "protected_root_key_roster_complete_inventory"] is False,
            "key-roster build evidence must not grant runtime, read, or inventory authority")
    require(capabilities[
        "protected_root_configuration_security_adapter_build_compiled"] is True,
            "configuration/security adapter must be build-compiled")
    require(capabilities[
        "protected_root_configuration_security_adapter_runtime_injected"] is False and
            capabilities[
                "protected_root_configuration_security_adapter_executed"] is False and
            capabilities[
                "protected_root_nvs_metadata_device_read_authorized"] is False and
            capabilities[
                "protected_root_security_state_device_read_authorized"] is False,
            "configuration/security build evidence must not grant runtime or read authority")
    require(capabilities["nimble_runtime_owner_build_linked"] is True and
            capabilities["nimble_runtime_startup_coded"] is True and
            capabilities["private_service_advertising_coded"] is True,
            "bounded NimBLE runtime code must be explicitly admitted")
    require(capabilities["companion_public_link_info_build_linked"] is True and
            capabilities["public_link_characteristic_runtime_coded"] is True and
            capabilities["bounded_public_link_window_host_tested"] is True and
            capabilities["public_link_connection_physically_observed"] is True and
            capabilities["public_link_info_physically_read"] is True and
            capabilities[
                "public_link_automatic_termination_physically_observed"] is True and
            capabilities[
                "public_link_compatible_advertiser_return_physically_observed"] is True,
            "accepted public link read and automatic lifecycle must be recorded")
    require(capabilities["evidence_bound_memory_profile_build_configured"] is True and
            capabilities["recovery_partition_layout_build_configured"] is True,
            "memory profile and recovery layout must be build-configured")
    require(capabilities["bounded_usb_heartbeat_physically_observed"] is True and
            capabilities["nimble_runtime_startup_physically_reached"] is True and
            capabilities["ble_service_advertising_physically_observed"] is True,
            "physical heartbeat, runtime start, and BLE advertisement must be admitted")
    require(capabilities["ble_pairing_window_host_tested"] is True and
            capabilities["ble_pairing_window_build_linked"] is True and
            capabilities["ble_pairing_window_secure_random_build_linked"] is True and
            capabilities["ble_pairing_input_gpio0_build_linked"] is True,
            "fresh local pairing-window logic, entropy, and GPIO0 input must be build-admitted")
    require(capabilities["ble_pairing_input_physically_observed"] is True and
            capabilities["ble_pairing_six_digit_display_physically_observed"] is True and
            capabilities["ble_pairing_timeout_concealment_physically_observed"] is True and
            capabilities["ble_pairing_reset_concealment_physically_observed"] is True and
            capabilities["ble_pairing_android_exchange_physically_observed"] is False and
            capabilities["ble_pairing_bond_ownership_physically_observed"] is False,
            "physical local-window evidence must not claim Android exchange or bond ownership")
    require(capabilities["oled_startup_display_owner_host_tested"] is True and
            capabilities["oled_startup_display_build_linked"] is True and
            capabilities["oled_startup_logo_coded"] is True and
            capabilities["oled_ble_phase_status_coded"] is True,
            "host-tested, build-linked OLED startup/status surface must be admitted")
    require(capabilities["oled_compact_status_footer_host_tested"] is True and
            capabilities["oled_compact_status_footer_build_linked"] is True and
            capabilities[
                "oled_compact_status_footer_physically_observed"] is True and
            capabilities[
                "oled_compact_status_footer_live_telemetry_bound"] is True and
            capabilities[
                "oled_compact_status_footer_radio_activity_bound"] is False,
            "compact footer must bind live battery/GNSS while remaining radio-unbound")

    require(capabilities["oled_startup_display_physically_observed"] is True and
            capabilities["oled_ble_link_status_physically_observed"] is True and
            capabilities["display"] is False,
            "bounded OLED observations must not claim general display support")

    for name, enabled in capabilities.items():
        if name not in admitted:
            require(enabled is False, f"capability must remain disabled: {name}")

    require(document["write_policy"] == {
        "build_only": False,
        "flashing_authorized": False,
        "physical_flash_completed": True,
        "additional_flashing_authorized": False,
    }, "completed physical write must not grant further authority")


def test_executed_oled_startup_flash_plan() -> None:
    document = json.loads(OLED_STARTUP_FLASH_PLAN.read_text(encoding="utf-8"))
    require(document["schema"] == "OTOD0", "unexpected OLED flash-plan schema")
    require(document["schema_version"] == 0,
            "unexpected OLED flash-plan version")
    require(document["status"] == "EXECUTED-POSTWRITE-VERIFIED-OLED-RUNTIME-AND-BLE-ADVERTISEMENT-ACCEPTED",
            "OLED plan must retain the exact accepted runtime and BLE execution state")
    require(document["purpose"] ==
            "One bounded OT-DEV-001 factory-app update for the Trail startup logo and truthful BLE phase status on the candidate Heltec V4 OLED",
            "OLED plan purpose must remain exactly bounded")
    require(document["recorded_on"] == "2026-08-16" and
            document["target_id"] == "heltec-v4-bench-candidate" and
            document["base_commit"] ==
            "04cb2795d0a15a3a5101eafbc2aad0d6ec015118",
            "OLED plan must retain its recorded target and base commit")

    require(document["authorization"] == {
        "owner_intent_recorded": True,
        "exact_physical_write_authorized": False,
        "selected_unit": "OT-DEV-001",
        "authorized_execution_consumed": True,
        "excluded_units": ["OT-DEV-002"],
        "factory_app_write_authorized": False,
        "full_chip_erase_authorized": False,
        "other_partition_write_authorized": False,
        "recovery_write_authorized": False,
        "attempt_limit": 1,
        "executed_attempts": 1,
        "additional_attempts_authorized": False,
    }, "executed OLED plan must consume authority and preserve Unit 2 isolation")
    require(document["installed_state_precondition"] == {
        "required_receipt": "OT-061",
        "required_runtime": "accepted OpenTrail boot self-check, USB heartbeat, and BLE service advertisement",
        "repository_record_has_no_intervening_flash": True,
        "stop_if_current_state_is_not_confirmed": True,
    }, "OLED execution must fail closed unless the accepted OT-061 state is confirmed")
    require(document["candidate_binding"] == {
        "display_controller": "SSD1315-compatible",
        "width": 128,
        "height": 64,
        "i2c_address": "0x3c",
        "sda_gpio": 17,
        "scl_gpio": 18,
        "reset_gpio": 21,
        "vext_control_gpio": 36,
        "vext_enable_level": 0,
        "exact_received_revision": None,
        "binding_physically_verified": True,
    }, "OLED plan must retain the accepted candidate display binding")
    require(document["build_evidence"] == {
        "framework": "ESP-IDF v6.0.2",
        "build_evidence_bytes": 4131,
        "build_evidence_sha256":
            "45E4A4359E8014EA75AF0C7D026F615523EC9A755CC04558A5EEC1D843B3D249",
        "two_builds_byte_identical": True,
        "logo_source_bytes": 7422,
        "logo_source_sha256":
            "F9394C0EC3B7D4855C3A4198E660D4BC93E0EEC98C143E691446736623204CAA",
        "host_display_groups_passed": 3,
        "target_admission_groups_passed": 6,
        "status_before_execution": "NOT-FLASHED",
    }, "OLED plan must retain the exact accepted build-only evidence")
    require(document["unchanged_profile_evidence"] == {
        "bootloader_bytes": 22528,
        "bootloader_sha256":
            "E5C6CDDD63E974220360B7F110727B3D7A8B425CF8C3CAE23497D8588A9E8B62",
        "partition_table_bytes": 3072,
        "partition_table_sha256":
            "84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB",
        "otadata_bytes": 8192,
        "otadata_sha256":
            "7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F",
        "partitions_csv_bytes": 452,
        "partitions_csv_sha256":
            "4F064C125AA641697E0539EAF9EDA9D1CDECAB46DD8FF387988B900F3EFE2389",
        "sdkconfig_bytes": 107030,
        "sdkconfig_sha256":
            "878CB11FE8EF47BDACF548BA4DF9EE671BE21E9FD27900F312A3F5CD3AD1023F",
    }, "bootloader, partition table, OTA data, and profile must remain unchanged")
    require(document["factory_app"] == {
        "file": "build/targets/heltec_v4_bench/opentrail_heltec_v4_bench.bin",
        "offset": "0x010000",
        "bytes": 470928,
        "sha256":
            "A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B",
        "logical_end_exclusive": "0x082f90",
        "automatic_sector_erase_start": "0x010000",
        "automatic_sector_erase_end_exclusive": "0x083000",
        "partition_capacity_bytes": 5177344,
    }, "OLED plan must bind one exact factory-app image at 0x010000")
    require(document["execution_policy"] == {
        "tool": "esptool",
        "version": "5.3.1",
        "chip": "esp32s3",
        "baud": 115200,
        "before": "no-reset",
        "after": "no-reset",
        "flash_mode": "dio",
        "flash_size": "16MB",
        "flash_frequency": "80m",
        "manual_rom_entry_required": True,
        "only_one_fresh_unambiguous_rom_port": True,
        "exact_input_hash_recheck_required": True,
        "erase_all": False,
        "write_offsets": ["0x010000"],
        "verify_flash_before_boot_required": True,
        "manual_reset_after_verified": True,
        "stop_on_mismatch_or_failure": True,
        "operator_retry_without_new_authorization": False,
        "esptool_config_file": "build/targets/heltec_v4_bench/esptool-one-attempt.cfg",
        "esptool_config_bytes": 79,
        "esptool_config_sha256":
            "66672EFECB18272FA72C18EBD9198DE684D3A3DE97D9421CDD0D1A3F50AB233D",
        "write_block_attempts": 1,
        "open_port_attempts": 1,
        "connect_attempts": 1,
    }, "OLED execution must remain one app-only attempt verified before reset")
    require(document["physical_acceptance"] == {
        "startup_logo_readable": True,
        "startup_logo_orientation_correct": True,
        "limited_underground_trail_identity_recognizable": True,
        "ble_advertising_status_observed": True,
        "boot_self_checks_passed": True,
        "usb_heartbeat_count_minimum": 2,
        "usb_heartbeat_observed": 4,
        "android_exact_service_candidate_count": 1,
        "ble_connection_attempted": False,
        "ble_pairing_attempted": False,
        "android_scan_no_selection_connection_or_pairing": True,
        "android_scan_identifier_retained": False,
        "lora_started": False,
        "gnss_started": False,
        "panic_observed": False,
        "runtime_failure_observed": False,
        "unit2_touched": False,
    }, "OLED acceptance must remain exact, bounded, and privacy-safe")
    require(document["completion_policy"] == {
        "authority_consumed_after_first_write_invocation": True,
        "no_standing_write_authority_after_execution": True,
        "support_claim_allowed": False,
        "exact_board_binding_claim_allowed_only_after_visual_acceptance": True,
        "recovery_remains_owner_operated": True,
    }, "OLED completion must consume authority and preserve unsupported status")
    require(document["execution_result"] == {
        "executed_on": "2026-08-17",
        "write_invocations": 1,
        "additional_write_invocations": 0,
        "factory_app_write": "PASS",
        "verify_flash": "PASS",
        "verification_invocations": 2,
        "verification_note": "The first read-only verification completed without retained raw output but its success wording was rejected by the wrapper; an offline esptool 5.3.1 source check identified the parser mismatch, and one corrected read-only verification then passed.",
        "manual_reset_performed": True,
        "owner_visual_acceptance":
            "PASS: recognizable Trail startup logo followed by BLE ADVERTISING",
        "runtime_observation_seconds": 16,
        "runtime_heartbeat_count": 4,
        "boot_self_check_observed": True,
        "panic_or_runtime_failure_observed": False,
        "raw_device_identifiers_retained": False,
        "execution_complete": True,
    }, "OLED execution receipt must remain exact and retain no raw identifiers")


def test_physical_flash_plan() -> None:
    document = json.loads(PHYSICAL_FLASH_PLAN.read_text(encoding="utf-8"))
    require(document["schema"] == "OTFP0", "unexpected physical flash-plan schema")
    require(document["schema_version"] == 0, "unexpected flash-plan version")
    require(document["status"] == "EXECUTED-POSTWRITE-VERIFIED-RUNTIME-AND-BLE-ADVERTISEMENT-ACCEPTED",
            "physical plan must retain the exact accepted execution state")
    require(document["target_id"] == "heltec-v4-bench-candidate",
            "physical plan must bind the exact target")
    require(document["selection"] == {
        "candidate_unit": "OT-DEV-001", "selected_unit": "OT-DEV-001",
        "excluded_units": ["OT-DEV-002"], "exact_received_revision": None,
        "rf_variant": None, "require_only_selected_unit_connected": True,
        "require_manual_rom_identity_check": True,
    }, "physical plan must isolate the sacrificial-first candidate")
    require(document["owner_decisions"] == {
        "recorded_on": "2026-08-16", "preserve_existing_meshcore_state": False,
        "private_flash_backup": False,
        "restore_route": "owner-operated official MeshCore web flasher",
    }, "MeshCore preservation choice must remain explicit and privacy-safe")
    require(document["write_policy"] == {
        "physical_write_authorized": False, "full_chip_erase_authorized": False,
        "authorized_execution_consumed": True, "executed_attempts": 1,
        "additional_attempts_authorized": False, "attempt_limit": 1,
        "require_exact_input_hash_recheck": True,
        "require_public_region_verification": True,
        "require_unchanged_second_unit": True,
    }, "completed plan must not grant additional physical write authority")
    image = document["open_trail_image"]
    require({key: image[key] for key in (
        "framework", "chip", "generated_before", "generated_after",
        "execution_before", "execution_after", "manual_reset_after_verified",
        "stub", "flash_mode",
        "flash_size", "flash_frequency", "full_chip_erase_before_write",
    )} == {
        "framework": "ESP-IDF v6.0.2", "chip": "esp32s3",
        "generated_before": "default-reset", "generated_after": "hard-reset",
        "execution_before": "no-reset", "execution_after": "no-reset",
        "manual_reset_after_verified": True, "stub": True,
        "flash_mode": "dio", "flash_size": "16MB", "flash_frequency": "80m",
        "full_chip_erase_before_write": True,
    }, "physical plan must mirror the pinned ESP-IDF flash settings")
    require([
        (entry["role"], entry["offset"], entry["file"], entry["bytes"], entry["sha256"])
        for entry in image["files"]
    ] == [
        ("bootloader", "0x000000", "bootloader/bootloader.bin", 22528, "E5C6CDDD63E974220360B7F110727B3D7A8B425CF8C3CAE23497D8588A9E8B62"),
        ("partition-table", "0x008000", "partition_table/partition-table.bin", 3072, "84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB"),
        ("otadata", "0x009000", "ota_data_initial.bin", 8192, "7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F"),
        ("factory-app", "0x010000", "opentrail_heltec_v4_bench.bin", 437552, "F0E81310C62CA0C17CA2531AF9B0D5BD5E6E115E1649F84C97514F72D51D6A3A"),
    ], "physical plan must mirror the exact pinned file/offset/hash manifest")
    require(document["first_boot_acceptance"] == {
        "bounded_runtime_and_ble_advertisement_only": True, "boot_self_checks_required": True,
        "lora_enabled": False, "ble_service_advertising_expected": True,
        "gnss_enabled": False, "display_enabled": False,
        "gpio_sensitive_behavior_enabled": False,
        "protected_storage_expected": "DENIED", "supported_hardware_claim": False,
        "result": "PASS",
    }, "first physical boot must remain narrowly bounded")
    require(document["execution_result"] == {
        "date": "2026-08-16", "esptool_version": "5.3.1",
        "manual_rom_entry_exit_rehearsed": True,
        "full_chip_erase": "PASS", "write": "PASS",
        "public_region_verify": "PASS", "manual_reset_boot": "PASS",
        "ble_service_advertising_observed": True,
        "android_compatible_candidate_count": 1,
        "ble_connection_attempted": False, "ble_pairing_attempted": False,
        "android_api_level": 33,
        "android_apk_sha256": "9CE206EEEAE2B13FC5C1092CEF41C226607FD3A9905A5797D4EBE31F3DC7F01C",
        "heartbeat_period_ms": 5000,
        "heartbeat_count_observed_minimum": 2,
        "self_check_failure_observed": False,
        "runtime_failure_observed": False, "panic_observed": False,
        "unit2_touched": False,
    }, "physical execution receipt must remain exact and privacy-safe")

    require(document["recovery_reference"] == {
        "execution_authorized": False, "owner_operated": True,
        "source": "https://github.com/meshcore-dev/MeshCore/releases/tag/companion-v1.16.0",
        "asset": "heltec_v4_companion_radio_usb-v1.16.0-07a3ca9-merged.bin",
        "bytes": 724976,
        "sha256": "91A7AE8AABBF6DCDEE41880719FCAD849327C24CEC84CD23AFFAB1EF38CAA334",
        "offset": "0x000000", "erase_before_write": True,
    }, "recovery reference must stay exact and separately unauthorized")

def test_recovery_partition_layout() -> None:
    lines = PARTITIONS.read_text(encoding="utf-8").splitlines()
    require(lines[:4] == [
        "# OpenTrail Heltec V4 bench recovery layout OTHP0/v0.",
        "# Build-only layout: no updater, OTA, storage, or recovery authority is implemented.",
        "# The application-owned 0x40 partition type avoids ESP-IDF-reserved data subtypes.",
        "# Name, Type, SubType, Offset, Size, Flags",
    ], "partition layout must retain its exact version and denied-authority boundary")
    rows = list(csv.reader(line for line in lines if line and not line.startswith("#")))
    require(rows == [
        ["otadata", "data", "ota", "0x9000", "0x2000", ""],
        ["factory", "app", "factory", "0x10000", "0x4f0000", ""],
        ["ota_0", "app", "ota_0", "0x500000", "0x500000", ""],
        ["ota_1", "app", "ota_1", "0xa00000", "0x500000", ""],
        ["ot_state", "0x40", "0x00", "0xf00000", "0x100000", ""],
    ], "unexpected recovery partition rows")
    regions = [(int(row[3], 0), int(row[4], 0), row[0]) for row in rows]
    for (offset, size, name), (next_offset, _, _) in zip(regions, regions[1:]):
        require(offset + size <= next_offset, f"partition overlap after {name}")
    require(regions[-1][0] + regions[-1][1] <= 16777216,
            "partition layout exceeds the recorded 16 MB flash")
    app_sizes = {name: size for _, size, name in regions if name in {"factory", "ota_0", "ota_1"}}
    require(app_sizes == {"factory": 5177344, "ota_0": 5242880, "ota_1": 5242880},
            "factory and both equal OTA-capable slots must retain exact sizes")
    require(regions[-1][0] + regions[-1][1] == 16777216,
            "ot_state must end exactly at the 16 MB flash boundary")


def test_protected_storage_candidate_plan() -> None:
    plan = json.loads(PROTECTED_STORAGE_PROVISIONING_PLAN.read_text(
        encoding="utf-8"))
    require(plan["schema"] == "OTPSP0/v0",
            "unexpected protected-storage plan schema")
    require(plan["status"] == "DESIGN-ONLY-NOT-ACTIVE-NOT-AUTHORIZED" and
            plan["as_of"] == "2026-08-18" and
            plan["target"] == "heltec_v4_bench",
            "candidate plan must remain dated, target-bound, and inactive")
    require(plan["active_target_configuration_changed"] is False,
            "candidate plan must not claim an active target change")
    require(plan["candidate_partition_table"] ==
            "protected-storage-partitions.candidate.csv",
            "candidate plan must bind the exact candidate table")
    require(plan["provider_plan"] == "protected-storage-provider-plan.json",
            "candidate plan must bind the offline provider contract")
    require(plan["candidate_layout"] == {
        "flash_size_bytes": 16777216,
        "authorization_partition": {
            "label": "ot_auth", "type": "data", "subtype": "nvs",
            "offset": 15728640, "size_bytes": 65536,
            "encrypted_flag": True,
        },
        "remaining_state_partition": {
            "label": "ot_state", "type": "0x40", "subtype": "0x00",
            "offset": 15794176, "size_bytes": 983040,
        },
    }, "candidate partition layout must remain exact")
    require(plan["key_roles"] == {
        "provider_kind": "ESP32S3_HMAC_UP_EFUSE",
        "nvs_encryption_hmac_key_id": None,
        "bond_binding_prf_hmac_key_id": None,
        "provider_types_selected_offline": True,
        "physical_pair_admitted": False,
        "distinct_keys_required": True,
        "efuse_provisioning_authorized": False,
    }, "candidate plan must select only the provider type")
    require(plan["rollback_floor"] == {
        "provider": None,
        "provider_class_selected_offline_conditionally": False,
        "reviewed_custom_user_efuse_candidate":
            "REJECTED-RS-CODING-UNIT-SUPPORTS-ONE-WRITE-NOT-REPEATED-ADVANCES",
        "reviewed_secure_version_candidate":
            "REJECTED-FIRMWARE-COUPLED-NOT-INDEPENDENT",
        "external_monotonic_hardware": "NOT-SELECTED-OR-PRESENT",
        "exact_field_selected": False,
        "independent_from_authorization_partition_required": True,
        "provisioning_authorized": False,
    }, "candidate plan must keep the rejected floor unselected")
    require(all(value is False for value in plan["runtime"].values()),
            "candidate plan must enable no protected runtime capability")
    require(all(value is False for value in
                plan["physical_authority"].values()),
            "candidate plan must grant no physical authority")

    with PROTECTED_STORAGE_CANDIDATE_PARTITIONS.open(
            newline="", encoding="utf-8") as handle:
        rows = [row for row in csv.reader(handle)
                if row and not row[0].lstrip().startswith("#")]
    require(rows == [
        ["otadata", "data", "ota", "0x9000", "0x2000", ""],
        ["factory", "app", "factory", "0x10000", "0x4f0000", ""],
        ["ota_0", "app", "ota_0", "0x500000", "0x500000", ""],
        ["ota_1", "app", "ota_1", "0xa00000", "0x500000", ""],
        ["ot_auth", "data", "nvs", "0xf00000", "0x10000", "encrypted"],
        ["ot_state", "0x40", "0x00", "0xf10000", "0x0f0000", ""],
    ], "candidate protected-storage partition table must remain exact")
    active_partitions = PARTITIONS.read_text(encoding="utf-8")
    defaults = DEFAULTS.read_text(encoding="utf-8")
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    require("ot_auth" not in active_partitions and
            "CONFIG_NVS_ENCRYPTION=y" not in defaults and
            contract["capabilities"]["storage"] is False and
            contract["partition_layout"]["storage_authority"] is False,
            "active target storage and authority must remain unchanged")


def test_authorization_nvs_backend_surface() -> None:
    header = AUTHORIZATION_NVS_BACKEND_HEADER.read_text(encoding="utf-8")
    source = AUTHORIZATION_NVS_BACKEND.read_text(encoding="utf-8")

    for required in (
        "EspIdfCompanionAuthorizationNvsBackend",
        "CompanionAuthorizationProtectedKvBackend",
        "explicit EspIdfCompanionAuthorizationNvsBackend(nvs_handle_t handle)",
        "read_blob", "write_blob", "commit", "nvs_handle_t handle_{0}",
    ):
        require(required in header,
                f"missing dormant authorization NVS backend API: {required}")

    for required in (
        "kCompanionAuthorizationProtectedPartitionLabel",
        "kCompanionAuthorizationProtectedNamespace",
        "kCompanionAuthorizationProtectedSlotAKey",
        "kCompanionAuthorizationProtectedSlotBKey",
        "handle_ == 0",
        "capacity != kCompanionAuthorizationDurableRecordBytes",
        "size != kCompanionAuthorizationDurableRecordBytes",
        "nvs_get_blob(handle_, key, nullptr, &required_size)",
        "nvs_get_blob(handle_, key, output, &read_size)",
        "nvs_set_blob(handle_, key, data, size)",
        "nvs_commit(handle_)",
        "CompanionAuthorizationProtectedKvBackendError::uncertain",
    ):
        require(required in source,
                f"missing fail-closed authorization NVS binding: {required}")
    require(source.count("nvs_get_blob(") == 2,
            "NVS backend must use only size-query and exact-read blob calls")
    require(source.count("nvs_set_blob(") == 1 and
            source.count("nvs_commit(") == 1,
            "NVS backend must expose one exact set and commit path")

    combined_backend = header + "\n" + source
    forbidden_native_surface = (
        "nvs_open(", "nvs_open_from_partition(", "nvs_close(",
        "nvs_flash_init", "nvs_flash_secure_init", "nvs_flash_generate_keys",
        "nvs_erase_", "esp_efuse", "esp_hmac", "ESP_LOG", "printf(",
        "puts(", "std::cout", "Serial.",
    )
    for token in forbidden_native_surface:
        require(token not in combined_backend,
                f"dormant NVS backend gained forbidden authority: {token}")

    public_class = header.split(
        "class EspIdfCompanionAuthorizationNvsBackend", 1)[1]
    require("erase" not in public_class and "reset" not in public_class,
            "public NVS backend class must expose no erase/reset operation")

    runtime_sources = "\n".join((
        SOURCE.read_text(encoding="utf-8"),
        SELF_CHECK.read_text(encoding="utf-8"),
        NIMBLE_GATT.read_text(encoding="utf-8"),
        NIMBLE_RUNTIME.read_text(encoding="utf-8"),
        AUTHORIZATION_STORAGE.read_text(encoding="utf-8"),
    ))
    require("EspIdfCompanionAuthorizationNvsBackend" not in runtime_sources,
            "dormant NVS backend must not be instantiated by target runtime")
    require("companion_authorization_nvs_backend.hpp" not in runtime_sources,
            "target runtime must not include dormant NVS backend")
    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    require("companion_authorization_nvs_backend.cpp" in cmake and
            "companion/src/companion_authorization_protected_kv_media.cpp" in cmake and
            "nvs_flash" in cmake,
            "inactive NVS backend and protected-KV media must be build-compiled")

    context_header = AUTHORIZATION_NVS_CONTEXT_HEADER.read_text(encoding="utf-8")
    context_source = AUTHORIZATION_NVS_CONTEXT.read_text(encoding="utf-8")
    for required in (
        "EspIdfCompanionAuthorizationNvsContext",
        "open_existing", "close", "backend", "snapshot",
        "kAuthorizationPartition[] = \"ot_auth\"",
        "kAuthorizationNamespace[] = \"ot_owner\"",
        "kAuthorizationOffset = 0x00F00000U",
        "kAuthorizationSize = 0x00010000U",
        "NVS_READWRITE", "exact_security_configuration_selected",
        "nvs_flash_get_default_security_scheme",
        "nvs_flash_read_security_cfg_v2",
        "nvs_flash_secure_init_partition",
        "nvs_open_from_partition",
        "secure_zero(&security_configuration",
        "return fail_uncertain()",
    ):
        require(required in context_header + "\n" + context_source,
                f"missing fail-closed authorization NVS context: {required}")
    for forbidden in (
        "nvs_flash_init(", "nvs_flash_generate_keys", "nvs_erase_",
        "esp_efuse", "esp_hmac", "provision", "ESP_LOG", "printf(",
        "puts(", "std::cout", "Serial.",
    ):
        require(forbidden not in context_source,
                f"inactive NVS context gained forbidden authority: {forbidden}")
    public_context = context_header.split(
        "class EspIdfCompanionAuthorizationNvsContext", 1)[1]
    require("erase" not in public_context and "reset" not in public_context and
            "retry" not in public_context,
            "public NVS context must expose no erase/reset/retry authority")
    configured_gate = context_source.index(
        "if (!exact_security_configuration_selected())")
    partition_find = context_source.index("esp_partition_find_first(")
    security_read = context_source.index("nvs_flash_read_security_cfg_v2(")
    secure_init = context_source.index("nvs_flash_secure_init_partition(")
    zero_after_init = context_source.index(
        "secure_zero(&security_configuration", secure_init)
    namespace_open = context_source.index("nvs_open_from_partition(")
    require(configured_gate < partition_find < security_read < secure_init <
            zero_after_init < namespace_open,
            "context must gate, read existing config, zero it, then open exactly")
    require("EspIdfCompanionAuthorizationNvsContext" not in runtime_sources and
            "companion_authorization_nvs_context.hpp" not in runtime_sources,
            "inactive NVS context must not enter target runtime composition")


def test_protected_root_key_roster_adapter_surface() -> None:
    header = PROTECTED_ROOT_KEY_ROSTER_HEADER.read_text(encoding="utf-8")
    source = PROTECTED_ROOT_KEY_ROSTER.read_text(encoding="utf-8")
    combined = header + "\n" + source

    for required in (
        "EspIdfProtectedRootKeyRosterAdapter",
        "ProtectedRootKeyRosterReadResult",
        "kProtectedRootKeyRosterSlotCount = 6U",
        "EFUSE_BLK_KEY0", "EFUSE_BLK_KEY1", "EFUSE_BLK_KEY2",
        "EFUSE_BLK_KEY3", "EFUSE_BLK_KEY4", "EFUSE_BLK_KEY5",
        "attempted_", "active_", "poisoned_", "proven_unused",
        "if (!normalize_purpose(raw_purpose, slot.purpose))",
        "proven_unused &&",
    ):
        require(required in combined,
                f"missing fail-closed key-roster boundary: {required}")

    allowed_apis = {
        "esp_efuse_get_key_purpose",
        "esp_efuse_get_key_dis_read",
        "esp_efuse_get_key_dis_write",
        "esp_efuse_get_keypurpose_dis_write",
        "esp_efuse_key_block_unused",
    }
    called_apis = set(re.findall(
        r"\b(esp_efuse_[a-z0-9_]+)\s*\(", source))
    require(called_apis == allowed_apis,
            "adapter must call exactly the five admitted decoded APIs")
    for api in allowed_apis:
        require(source.count(f"{api}(") == 1,
                f"adapter source must contain exactly one call site: {api}")

    purpose_gate = source.index(
        "if (!normalize_purpose(raw_purpose, slot.purpose))")
    first_following_read = source.index("esp_efuse_get_key_dis_read(")
    require(purpose_gate < first_following_read,
            "invalid purpose must deny before any other slot metadata read")

    for forbidden in (
        "esp_efuse_get_key(", "esp_efuse_read_block(",
        "esp_efuse_read_field_blob(", "esp_efuse_write",
        "esp_efuse_set_", "esp_efuse_batch", "esp_hmac",
        "nvs_", "ESP_LOG", "printf(", "puts(", "std::cout",
        "std::cerr", "fstream", "filesystem", "subprocess", "serial",
        "uart", "usb", "provisioned{", "reserved{",
    ):
        require(forbidden not in combined,
                f"key-roster adapter gained forbidden surface: {forbidden}")
    require("companion_protected_root_inventory" not in combined,
            "coarse roster must not publish complete inventory evidence")

    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    linked_source_tokens = re.findall(r'"([^"\n]+\.cpp)"', cmake)
    require(len(linked_source_tokens) == 29,
            "non-injection gate must cover the exact 29-source target build")
    other_linked_sources = []
    for token in linked_source_tokens:
        if token == "companion_protected_root_key_roster_adapter.cpp":
            continue
        if token.startswith("${OPENTRAIL_COMPONENT_ROOT}/"):
            suffix = token.removeprefix("${OPENTRAIL_COMPONENT_ROOT}/")
            path = ROOT / "firmware" / "components" / Path(suffix)
        else:
            path = TARGET / "main" / token
        require(path.is_file(), f"linked source is missing: {token}")
        other_linked_sources.append(path)
    require(len(other_linked_sources) == 28,
            "non-injection gate must scan every other linked source")
    runtime_sources = "\n".join(
        path.read_text(encoding="utf-8") for path in other_linked_sources)
    require("EspIdfProtectedRootKeyRosterAdapter" not in runtime_sources and
            "companion_protected_root_key_roster_adapter.hpp" not in
            runtime_sources,
            "build-only key-roster adapter must have no runtime call path")


def test_protected_root_configuration_security_adapter_surface() -> None:
    header = PROTECTED_ROOT_CONFIGURATION_SECURITY_HEADER.read_text(
        encoding="utf-8")
    source = PROTECTED_ROOT_CONFIGURATION_SECURITY.read_text(encoding="utf-8")
    combined = header + "\n" + source

    for required in (
        "EspIdfProtectedRootConfigurationSecurityAdapter",
        "ProtectedRootConfigurationSecurityReadResult",
        "normalize_protected_root_nvs_build_configuration",
        "CONFIG_NVS_ENCRYPTION",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC",
        "CONFIG_NVS_SEC_KEY_PROTECT_NONE",
        "CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID",
        "esp_secure_boot_enabled()",
        "esp_efuse_is_flash_encryption_enabled()",
        "esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD)",
        "esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)",
        "attempted_", "active_", "poisoned_",
    ):
        require(required in combined,
                f"missing configuration/security boundary: {required}")

    require(source.count("esp_secure_boot_enabled()") == 1 and
            source.count("esp_efuse_is_flash_encryption_enabled()") == 1 and
            source.count("esp_efuse_read_field_bit(") == 2,
            "configuration/security source must retain four exact call sites")
    secure_boot = source.index("esp_secure_boot_enabled()")
    flash_encryption = source.index(
        "esp_efuse_is_flash_encryption_enabled()")
    secure_download = source.index(
        "esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD)")
    download_disabled = source.index(
        "esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)")
    require(secure_boot < flash_encryption < secure_download < download_disabled,
            "configuration/security metadata call order drifted")

    for forbidden in (
        "nvs_flash_read_security_cfg", "nvs_flash_generate_keys",
        "esp_hmac", "esp_efuse_get_key", "esp_efuse_read_block",
        "esp_efuse_read_field_blob", "esp_efuse_write",
        "esp_efuse_set_", "esp_efuse_batch", "ESP_LOG", "printf(",
        "puts(", "std::cout", "std::cerr", "fstream", "filesystem",
        "subprocess", "SerialPort", "serial_", "uart_", "usb_",
        "reset", "erase",
    ):
        require(forbidden not in combined,
                f"configuration/security adapter gained forbidden surface: {forbidden}")

    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    linked_source_tokens = re.findall(r'"([^"\n]+\.cpp)"', cmake)
    require(len(linked_source_tokens) == 29,
            "configuration/security non-injection gate must cover 26 sources")
    other_linked_sources = []
    for token in linked_source_tokens:
        if token == "companion_protected_root_configuration_security_adapter.cpp":
            continue
        if token.startswith("${OPENTRAIL_COMPONENT_ROOT}/"):
            suffix = token.removeprefix("${OPENTRAIL_COMPONENT_ROOT}/")
            path = ROOT / "firmware" / "components" / Path(suffix)
        else:
            path = TARGET / "main" / token
        require(path.is_file(), f"linked source is missing: {token}")
        other_linked_sources.append(path)
    require(len(other_linked_sources) == 28,
            "configuration/security gate must scan every other linked source")
    runtime_sources = "\n".join(
        path.read_text(encoding="utf-8") for path in other_linked_sources)
    require("EspIdfProtectedRootConfigurationSecurityAdapter" not in
            runtime_sources and
            "companion_protected_root_configuration_security_adapter.hpp" not in
            runtime_sources,
            "build-only configuration/security adapter gained a runtime path")


def test_display_surface() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    owner_header = DISPLAY_OWNER_HEADER.read_text(encoding="utf-8")
    owner = DISPLAY_OWNER.read_text(encoding="utf-8")
    battery_header = BATTERY_HEADER.read_text(encoding="utf-8")
    battery_source = BATTERY_SOURCE.read_text(encoding="utf-8")
    gnss_header = GNSS_HEADER.read_text(encoding="utf-8")
    gnss_source = GNSS_SOURCE.read_text(encoding="utf-8")
    adapter_header = DISPLAY_ADAPTER_HEADER.read_text(encoding="utf-8")
    adapter = DISPLAY_ADAPTER.read_text(encoding="utf-8")
    logo = DISPLAY_LOGO.read_text(encoding="utf-8")
    footer_header = COMPACT_FOOTER_HEADER.read_text(encoding="utf-8")
    footer = COMPACT_FOOTER.read_text(encoding="utf-8")

    exact_constants = (
        "kHeltecV4OledSdaGpio = 17",
        "kHeltecV4OledSclGpio = 18",
        "kHeltecV4OledResetGpio = 21",
        "kHeltecV4VextControlGpio = 36",
        "kHeltecV4VextEnableLevel = 0",
        "kHeltecV4OledAddress = 0x3C",
        "kHeltecV4OledClockHz = 400000",
    )
    for constant in exact_constants:
        require(constant in adapter_header,
                f"missing exact Heltec OLED candidate binding: {constant}")
    for required in (
        "kDisplayWidth = 128", "kDisplayHeight = 64",
        "esp_lcd_new_panel_ssd1306", "vendor_config.height = kDisplayHeight",
        "panel_config.bits_per_pixel = 1", "esp_lcd_panel_reset",
        "esp_lcd_panel_init", "esp_lcd_panel_mirror(panel_, true, true)",
        "esp_lcd_panel_disp_on_off(panel_, true)",
        "esp_lcd_panel_draw_bitmap",
    ):
        require(required in adapter,
                f"missing bounded SSD1315-compatible OLED adapter gate: {required}")
    require("kHeltecV4VextEnableLevel" in adapter and
            "gpio_set_level" in adapter,
            "documented Vext LOW-on candidate must be explicitly driven")
    require("record_failure" in adapter and
            "display unavailable step=%s code=%d" in adapter,
            "OLED adapter failures must become privacy-safe unavailability")

    require("display failure is latched unavailable" in owner_header and
            "never controls BLE or heartbeat" in owner_header,
            "display owner must document its fail-independent boundary")
    require("status_.available = false" in owner and
            "if (!started_ || !status_.available)" in owner,
            "display owner must latch initialization/render failure unavailable")
    require("start_companion_nimble_runtime" not in owner and
            "service_companion_nimble_runtime" not in owner,
            "display owner must not own BLE runtime authority")
    phase_frames = {
        "advertising": "StartupDisplayFrame::ble_advertising",
        "connected": "StartupDisplayFrame::ble_connected",
        "restart_wait": "StartupDisplayFrame::ble_retrying",
        "contained": "StartupDisplayFrame::ble_error",
    }
    for phase, frame in phase_frames.items():
        pattern = rf"CompanionBleRuntimePhase::{phase}.*?return {re.escape(frame)};"
        require(re.search(pattern, owner, re.DOTALL) is not None,
                f"typed BLE runtime phase lacks display mapping: {phase}")
    require('return "BLE ADVERTISING"' in owner,
            "advertising text must be available only through the typed frame")

    footer_codes = {
        "ble_starting": "BleCode::starting",
        "ble_advertising": "BleCode::advertising",
        "ble_connected": "BleCode::connected",
        "ble_retrying": "BleCode::retrying",
        "ble_error": "BleCode::error",
    }
    for frame, code in footer_codes.items():
        require(re.search(
            rf"StartupDisplayFrame::{frame}.*?snapshot\.ble = {code};",
            owner, re.DOTALL) is not None,
            f"typed BLE frame lacks compact footer code: {frame}")
    require("Snapshot snapshot{}" in owner and
            "snapshot.battery_percent = status.battery_percent" in owner and
            "snapshot.gps_satellites = status.gps_satellites" in owner and
            "status.freshness" in owner and
            "ActivityOwner activity{" in owner,
            "target footer must consume caller-owned live metrics and freshness")
    require("case StartupDisplayFrame::logo:" in owner and
            "case StartupDisplayFrame::self_check_failed:" in owner and
            "page = {};" in owner and "return false;" in owner,
            "logo and SELF CHECK FAIL must bypass the compact BLE footer")
    require("view.footer.columns.begin()" in adapter and
            "kStatusPage = 7" in adapter and
            "kDisplayWidth = 128" in adapter and
            "view.has_footer" in adapter and
            "view.footer.columns.end()" in adapter and
            "pixels.begin() + kStatusPage * kDisplayWidth" in adapter,
            "OLED adapter must copy the exact supplied 128-column page into page 7")
    require("draw_status_text(pixels, startup_display_text(view.frame))" in adapter and
            'return "SELF CHECK FAIL"' in owner,
            "logo/self-check fallback path must remain explicit")
    for required in (
        'kPairingLabel[] = "PAIR"',
        "kPairingDigitsScale = 2",
        "view.digits.data()",
        "view.digits.size()",
        "(kDisplayWidth - pixel_width) / 2",
        "pixels.fill(0)",
        'record_failure("pairing-draw", result)',
    ):
        require(required in adapter,
                f"pairing OLED page is missing: {required}")
    for digit in range(10):
        require(f"case '{digit}':" in adapter,
                f"pairing OLED font is missing digit {digit}")
    require("PairingPinDisplayPortAdapter final" in owner_header and
            "CompanionPairingPinDisplayPort" in owner_header and
            "pairing_view.digits.fill('\\0')" in owner and
            "pairing_pin_visible_" in owner_header and
            "port_.render(view_)" in owner and
            "port_.conceal()" in owner and
            "bool HeltecV4Oled::conceal()" in adapter and
            "esp_lcd_panel_disp_on_off(panel_, false)" in adapter and
            "1 - kHeltecV4VextEnableLevel" in adapter,
            "target pairing display must remain transient and restore normal view")
    require(re.search(r"ESP_LOG[^\n]*(digits|pin|passkey)", adapter,
                      re.IGNORECASE) is None,
            "pairing PIN must not enter OLED logs")
    require("kWidth = 128" in footer_header and
            "render_text(page, 1U, fields.battery)" in footer and
            "render_text(page, 51U, fields.gps)" in footer and
            "render_text(page, 89U, fields.ble)" in footer,
            "target must reuse the accepted compact footer geometry")
    for forbidden_binding in (
        "battery_metric_from_power", "GpsProvider", "RadioTransport",
    ):
        require(forbidden_binding not in "\n".join(
                    (source, owner_header, owner, adapter_header, adapter)),
                f"target gained an unadmitted telemetry binding: {forbidden_binding}")

    for required in (
        "GPIO_NUM_37", "ADC_CHANNEL_0", "kDividerNumerator = 490",
        "adc_cali_create_scheme_curve_fitting", "DividerGuard",
        "gpio_set_level(kAdcControl, 1) == ESP_OK",
    ):
        require(required in battery_source,
                f"missing audited Heltec V4 battery binding: {required}")
    require("voltage-derived and is not fuel-gauge accuracy" in battery_header and
            "kEmptyMillivolts = 3300" in battery_header and
            "kFullMillivolts = 4200" in battery_header,
            "battery percentage must remain explicitly approximate and bounded")

    for required in (
        "kHeltecV4GnssEnableGpio = 34",
        "kHeltecV4GnssResetGpio = 42",
        "kHeltecV4GnssRxGpio = 39",
        "kHeltecV4GnssTxGpio = 38",
        "kHeltecV4GnssBaud = 9'600",
        "kHeltecV4GnssEnableLevel = 0",
        "kHeltecV4GnssInactiveLevel = 1",
        "kHeltecV4GnssResetAssertedLevel = 0",
        "kHeltecV4GnssResetReleasedLevel = 1",
    ):
        require(required in gnss_header,
                f"missing audited Heltec V4 GNSS binding: {required}")
    require("uart_driver_install" in gnss_source and
            "uart_read_bytes" in gnss_source and
            "observer_.ingest" in gnss_source and
            "contain_failure" in gnss_source and
            "uart_driver_delete" in gnss_source,
            "GNSS adapter must remain a bounded UART satellite observer")
    for forbidden in ("latitude", "longitude", "altitude", "ESP_LOG", "printf("):
        require(forbidden not in gnss_source,
                f"GNSS adapter must not retain or emit private data: {forbidden}")

    for required in (
        "kBatterySamplePeriodMs = 30'000",
        "kBatteryFreshForMs = 60'000",
        "kGnssFreshForMs = 5'000",
        "g_gnss.service(elapsed_ms)",
        "opentrail::heltec_v4::battery_read()",
        "GnssSatelliteState::valid",
        "ObservationState::invalid",
        "show_compact_status",
    ):
        require(required in source,
                f"application lacks live compact-status binding: {required}")

    display_start = source.index("observe_display_result(g_startup_display.start())")
    self_check_start = source.index("if (!run_companion_codec_self_check() ||")
    pass_log = source.index("companion boot self-check PASS")
    runtime_start = source.index("start_companion_nimble_runtime(")
    runtime_service = source.index("service_companion_nimble_runtime(elapsed_ms)")
    typed_status = source.index("const auto runtime_status =")
    require(display_start < self_check_start < pass_log < runtime_start <
            runtime_service < typed_status,
            "logo, self-check, runtime, and typed status order changed")
    require("kMinimumLogoPeriodMs = 1200" in source and
            "render_now_ms - boot_started_at_ms >= kMinimumLogoPeriodMs" in source,
            "startup logo must remain visible before typed BLE status replaces it")
    require("startup display unavailable; runtime continues" in source,
            "display failure must explicitly preserve runtime progress")

    require("kTrailStartupLogoWidth = 128" in logo and
            "kTrailStartupLogoHeight = 64" in logo and
            "static_assert(kTrailStartupLogoSsd1306.size() == 1024)" in logo,
            "startup logo must remain an exact 128x64 one-bit asset")
    logo_array = re.search(
        r"kTrailStartupLogoSsd1306\s*=\s*\{(.*?)\};", logo, re.DOTALL)
    require(logo_array is not None, "startup logo byte array is missing")
    logo_bytes = [
        int(value, 16)
        for value in re.findall(r"0x([0-9A-Fa-f]{2})", logo_array.group(1))
    ]
    require(len(logo_bytes) == 1024, "startup logo must contain exactly 1024 bytes")
    require(any(value != 0 for value in logo_bytes) and
            any(value != 0xFF for value in logo_bytes),
            "startup logo must be neither blank nor all-on")
    require("LIMITED UNDERGROUND" in logo and "TRAIL" in logo,
            "startup asset must retain the accepted Trail identity description")
    require(hashlib.sha256(bytes(logo_bytes)).hexdigest() ==
            "7ca7bfa0f7b96742dd459237a8b24dda0cd5ddea8ae0cdb60ed68dbaabea04e6",
            "startup logo bitmap changed without explicit visual admission")


def test_application_surface() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    self_check = SELF_CHECK.read_text(encoding="utf-8")

    nimble_gatt = NIMBLE_GATT.read_text(encoding="utf-8")
    nimble_runtime = NIMBLE_RUNTIME.read_text(encoding="utf-8")
    runtime_owner = BLE_RUNTIME_OWNER.read_text(encoding="utf-8")
    require("run_companion_codec_self_check" in source,
            "missing deterministic boot codec self-check")
    require("kExpectedInfo" in source and
            "kExpectedAction" in source and
            "kExpectedFragment" in source,
            "self-check must compare exact OTB0, OTA0, and OTC0 vectors")
    require("companion boot self-check PASS" in source,
            "missing fixed combined PASS record")
    require("companion boot self-check FAIL" in source,
            "missing fixed combined FAIL record")
    require("run_companion_request_coordinator_self_check" in source,
            "missing coordinator self-check call")
    require("run_companion_gatt_session_self_check" in source,
            "missing GATT-session lifecycle self-check call")
    require("run_companion_gatt_authorization_self_check" in source,
            "missing restricted authorization lifecycle self-check call")
    require("run_companion_gatt_authorization_adapter_self_check" in source,
            "missing callback-adapter self-check call")
    require("run_companion_authorization_storage_self_check" in source,
            "missing authorization-storage self-check call")
    require("run_companion_ble_runtime_owner_self_check" in source,
            "missing BLE-runtime-owner self-check call")
    require("companion_nimble_gatt_definition_self_check" in source,
            "missing NimBLE GATT definition self-check call")
    require("start_companion_nimble_runtime" in source and
            "service_companion_nimble_runtime" in source,
            "application must own the bounded NimBLE runtime")
    require("FixedSnapshotAuthority" in self_check and
            "FixedActionAuthority" in self_check,
            "coordinator self-check must use fixed injected authorities")
    for vector in (
        "kActionRequest", "kActionResponse",
        "kSnapshotRequest", "kSnapshotResponse",
    ):
        require(vector in self_check,
                f"coordinator self-check missing exact vector: {vector}")
    require("replayed_cached_response" in self_check,
            "coordinator self-check must prove cached duplicate replay")
    require("observation.prepare_calls != 1" in self_check and
            "observation.commit_calls != 1" in self_check and
            "observation.applied_calls != 1" in self_check,
            "coordinator self-check must reject duplicate authority reapply")
    require("contain_self_check_failure" in source and
            "vTaskSuspend(nullptr)" in source,
            "codec failure must suspend before heartbeat")
    require("companion runtime started" in source and
            "companion runtime FAIL" in source,
            "missing fixed runtime startup/containment records")
    require("heartbeat elapsed_ms=%llu" in source,
            "missing privacy-safe heartbeat")
    require("esp_timer_get_time" in source,
            "heartbeat must use boot-local monotonic time")
    self_check_call = source.index("if (!run_companion_codec_self_check() ||")
    coordinator_call = source.index(
        "run_companion_request_coordinator_self_check()")
    gatt_session_call = source.index(
        "run_companion_gatt_session_self_check()")
    gatt_authorization_call = source.index(
        "run_companion_gatt_authorization_self_check()")
    gatt_adapter_call = source.index(
        "run_companion_gatt_authorization_adapter_self_check()")
    authorization_storage_call = source.index(
        "run_companion_authorization_storage_self_check()")
    runtime_owner_check_call = source.index(
        "run_companion_ble_runtime_owner_self_check()")
    gatt_definition_call = source.index(
        "companion_nimble_gatt_definition_self_check()")
    pass_log = source.index("companion boot self-check PASS")
    runtime_start = source.index("start_companion_nimble_runtime(")
    startup_log = source.index("companion runtime started")
    heartbeat_log = source.index("heartbeat elapsed_ms=%llu")
    require(self_check_call < coordinator_call < gatt_session_call < gatt_authorization_call < gatt_adapter_call < authorization_storage_call < runtime_owner_check_call < gatt_definition_call < pass_log < runtime_start < startup_log < heartbeat_log,
            "all self-checks must gate runtime startup and heartbeat")
    require("FixedBleRuntimePort" in self_check and
            "CompanionBleRuntimeOwner owner" in self_check and
            "authorization_claims_closed" in self_check and
            "normal_commands_closed" in self_check,
            "boot runtime-owner self-check must remain deterministic and denied")

    for vector in (
        "kAuthorizationProtocolInfo", "kAuthorizationClaimStart",
        "kAuthorizationPending", "kAuthorizationAccepted",
    ):
        require(vector in self_check,
                f"authorization self-check missing exact vector: {vector}")
    require("observation.authorization_calls != 0" in self_check and
            "observation.authorization_calls != 1" in self_check,
            "authorization check must prove no early and exactly one authority call")
    require("CompanionGattAuthorizationCallbackAdapter adapter" in self_check and
            "port.response_is(kAuthorizationPending)" in self_check and
            "port.response_is(kAuthorizationAccepted)" in self_check and
            "port.response_is(kActionResponse)" in self_check,
            "callback-adapter self-check must prove exact provisional promotion and normal response")

    gatt_authorization = GATT_AUTHORIZATION.read_text(encoding="utf-8")
    for required in (
        "kCompanionAuthorizationMinimumAttMtu",
        "kCompanionAuthorizationPendingDeliveryToken",
        "kCompanionAuthorizationTerminalDeliveryToken",
        "indication_sink_.reserve",
        "authority_.apply_claim",
        "CompanionGattAuthorizationPhase::awaiting_authority",
        "transport_generation",
        "ScopedOperation operation(operation_active_)",
    ):
        require(required in gatt_authorization,
                f"missing restricted authorization lifecycle gate: {required}")
    require(gatt_authorization.index("indication_sink_.reserve") <
            gatt_authorization.index("authority_.apply_claim"),
            "terminal capacity reservation must precede authorization mutation")

    gatt_session = GATT_SESSION.read_text(encoding="utf-8")
    gatt_adapter = GATT_AUTHORIZATION_ADAPTER.read_text(encoding="utf-8")
    authorization_persistence = AUTHORIZATION_PERSISTENCE.read_text(
        encoding="utf-8")
    authorization_storage = AUTHORIZATION_STORAGE.read_text(encoding="utf-8")
    for required in (
        "nimble_port_init", "register_companion_nimble_gatt_service",
        "xTaskCreatePinnedToCore", "ble_gap_adv_start",
        "BLE_HS_IO_DISPLAY_ONLY", "sm_bonding = 1", "sm_mitm = 1",
        "sm_sc = 1", "StaticQueue_t", "std::atomic<bool>",
        "nvs_encryption_not_configured", "connection_termination_failed",
        "ble_gap_terminate", "15000", "2000",
        "observe_host_run_exit", "host_started_ && !host_run_exited_",
    ):
        require(required in nimble_runtime,
                f"missing bounded NimBLE runtime gate: {required}")
    sdkconfig_defaults = (TARGET / "sdkconfig.defaults").read_text(
        encoding="utf-8")
    require("CONFIG_BT_NIMBLE_MAX_BONDS=1" in sdkconfig_defaults and
            "CONFIG_LOG_MAXIMUM_LEVEL=3" in sdkconfig_defaults,
            "pairing build must retain one transient bond and compile out DEBUG passkey logs")
    require("fields.name" not in nimble_runtime and
            "fields.mfg_data" not in nimble_runtime and
            "ble_hs_id_copy_addr" not in nimble_runtime and
            "ESP_LOG" not in nimble_runtime,
            "advertising/runtime must not expose or log identity")
    require(nimble_runtime.index("companion_authorization_storage_preflight") <
            nimble_runtime.index("g_runtime_owner.start"),
            "denied protected-storage preflight must precede host start")
    exited_service = nimble_runtime[
        nimble_runtime.index("g_host_exited.exchange"):
        nimble_runtime.index("g_event_overflow.exchange")
    ]
    require(exited_service.index("observe_host_run_exit") <
            exited_service.index("g_runtime_owner.host_reset"),
            "confirmed host exit must suppress stop before containment")
    contain_port = nimble_runtime[
        nimble_runtime.index("bool contain_stack() override"):
        nimble_runtime.index("void observe_host_run_exit()")
    ]
    require(contain_port.index("host_started_ && !host_run_exited_") <
            contain_port.index("vTaskDelete(host_task_)") <
            contain_port.index("nimble_port_deinit()"),
            "both normal and already-exited cleanup must delete before deinit")
    connect_case = nimble_runtime[
        nimble_runtime.index("case BLE_GAP_EVENT_CONNECT"):
        nimble_runtime.index("case BLE_GAP_EVENT_DISCONNECT")
    ]
    require("RuntimeEventKind::connection_opened" in connect_case and
            "security_initiation_accepted" in connect_case and
            "pairing_open" in connect_case,
            "public link must remain queued while an open PIN window initiates security")
    for required in (
        "BLE_GAP_EVENT_PASSKEY_ACTION", "BLE_SM_IOACT_DISP",
        "ble_sm_inject_io", "BLE_GAP_EVENT_ENC_CHANGE",
        "description.sec_state.encrypted", "description.sec_state.authenticated",
        "description.sec_state.bonded", "BLE_SM_PAIR_KEY_SZ_MAX",
        "CompanionPairingWindow", "xSemaphoreCreateMutexStatic",
    ):
        require(required in nimble_runtime,
                f"missing dynamic pairing runtime gate: {required}")
    for required in (
        "BLE_HS_EALREADY", "BLE_GAP_EVENT_TERM_FAILURE",
        "handle_passkey_action_deferred_cleanup",
        "exact_active_attempt", "terminate_connection_or_contain",
    ):
        require(required in nimble_runtime,
                f"missing fail-closed pairing integration gate: {required}")
    enc_case = nimble_runtime[
        nimble_runtime.index("case BLE_GAP_EVENT_ENC_CHANGE"):
        nimble_runtime.index("default:")
    ]
    require("companion_nimble_gatt_gap_event" in enc_case and
            "if (exact_active_attempt)" in enc_case,
            "encryption changes must update GATT security and only close the exact attempt")
    reset_case = nimble_runtime[
        nimble_runtime.index("case RuntimeEventKind::host_reset"):
        nimble_runtime.index("case RuntimeEventKind::advertising_interrupted")
    ]
    require("kCompanionBleInvalidConnectionHandle" in reset_case and
            "++g_pairing_transport_generation" in reset_case and
            "g_pairing_window->restart()" in reset_case,
            "host reset must invalidate stale transport state and clear the PIN")
    require("static_passkey" not in nimble_runtime.lower() and
            re.search(r"ESP_LOG[^\n]*(pin|passkey)", nimble_runtime,
                      re.IGNORECASE) is None,
            "runtime must not use or log a static/dynamic PIN")
    for required in (
        "operation_active_", "reentry_observed_", "callback_overflow",
        "max_restart_attempts", "restart_token", "contain_stack",
        "connection_termination_failed", "public_link_window_ms",
        "termination_ack_timeout_ms", "termination_pending",
    ):
        require(required in runtime_owner,
                f"missing serialized runtime-owner invariant: {required}")
    for required in (
        "kCompanionMaxResponseRecordBytes",
        "indication_sink_.reserve",
        "coordinator_.service",
        "indication_sink_.submit_reserved",
        "blocked_until_disconnect_",
        "pending_delivery_token_",
        "ScopedOperation operation(operation_active_)",
    ):
        require(required in gatt_session,
                f"missing fixed GATT lifecycle gate: {required}")
    require(gatt_session.index("indication_sink_.reserve") <
            gatt_session.index("coordinator_.service") <
            gatt_session.index("indication_sink_.submit_reserved"),
            "response reservation must precede coordinator mutation and submission")

    for required in (
        "BLE_GATT_SVC_TYPE_PRIMARY",
        "BLE_GATT_CHR_F_READ_ENC",
        "BLE_GATT_CHR_F_READ_AUTHEN",
        "BLE_GATT_CHR_F_READ_AUTHOR",
        "BLE_GATT_CHR_F_WRITE_ENC",
        "BLE_GATT_CHR_F_WRITE_AUTHEN",
        "BLE_GATT_CHR_F_WRITE_AUTHOR",
        "BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC",
        "BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN",
        "BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHOR",
        "ble_gap_conn_find",
        "ble_gatts_count_cfg",
        "ble_gatts_add_svcs",
        "register_companion_nimble_gatt_service",
        "ensure_exact_registered_handles",
        "BLE_GAP_EVENT_CONNECT",
        "BLE_GAP_EVENT_DISCONNECT",
        "BLE_GAP_EVENT_ENC_CHANGE",
        "BLE_GAP_EVENT_MTU",
        "BLE_GAP_EVENT_SUBSCRIBE",
        "BLE_GAP_EVENT_NOTIFY_TX",
        "BLE_GAP_EVENT_AUTHORIZE",
        "g_indication_port.pending_tuple()",
        "kPublicLinkInfoFlags", "BLE_GATT_CHR_F_READ",
        "encode_companion_public_link_info",
    ):
        require(required in nimble_gatt,
                f"missing fail-closed NimBLE GATT surface: {required}")
    command_access = nimble_gatt[
        nimble_gatt.rindex("int command_access(std::uint16_t connection_handle,"):
        nimble_gatt.rindex("int stream_access(std::uint16_t,")
    ]
    require("g_adapter->service_command" in command_access and
            "refresh_security(connection_handle)" in command_access and
            "BLE_ATT_ERR_INSUFFICIENT_AUTHOR" in command_access,
            "Command callback must refresh security and route only through the fail-closed adapter")
    public_access = nimble_gatt[
        nimble_gatt.rindex("int public_link_info_access("):
        nimble_gatt.rindex("int command_access(std::uint16_t connection_handle,")
    ]
    require("encode_companion_public_link_info" in public_access and
            "BLE_GATT_ACCESS_OP_READ_CHR" in public_access and
            "refresh_security" not in public_access and
            "g_adapter" not in public_access and
            "service_command" not in public_access,
            "public link-info callback must expose only the fixed read")
    public_flags = nimble_gatt[
        nimble_gatt.index("constexpr ble_gatt_chr_flags kPublicLinkInfoFlags"):
        nimble_gatt.index("CompanionGattAuthorizationCallbackAdapter*")]
    require("BLE_GATT_CHR_F_READ" in public_flags and
            "WRITE" not in public_flags and "AUTHOR" not in public_flags,
            "public link-info characteristic must be read-only and unprivileged")
    require("g_stream_handle + 1" not in nimble_gatt and
            "ble_gatts_find_dsc" in nimble_gatt and
            "BLE_GATT_DSC_CLT_CFG_UUID16" in nimble_gatt and
            "cccd == 0 || cccd == stream" in nimble_gatt,
            "Stream CCCD handle must never be inferred from its value handle")
    require("g_adapter->status().pending" not in nimble_gatt and
            "g_indication_port.pending_tuple()" in nimble_gatt,
            "NOTIFY_TX must consume the immutable submission-era tuple")
    for required in (
        "transport_generation_",
        "binding_authority_.resolve",
        "refresh_security_impl",
        "indication_port_.bind_exchange",
        "exact_pending(expected)",
        "ScopedOperation operation(operation_active_)",
    ):
        require(required in gatt_adapter,
                f"missing callback-adapter fail-closed gate: {required}")
    for required in (
        "CONFIG_NVS_ENCRYPTION",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC",
        "protected_nvs_initialized_and_verified",
        "kPrivateBondStoreImplemented = false",
        "kSeparateBindingPrfKeyProvisioned = false",
        "kAtomicRecordAndFloorBackendImplemented = false",
        "kIndependentRollbackFloorImplemented = false",
        "nvs_encryption_not_configured",
    ):
        require(required in authorization_storage,
                f"missing closed authorization-storage preflight: {required}")
    for required in (
        "esp_partition_find_first", "esp_efuse_get_key_purpose",
        "esp_efuse_get_key_dis_read", "esp_hmac_calculate",
        "secure_zero(message.data(), message.size())",
        "secure_zero(output.data(), output.size())",
    ):
        require(required in authorization_storage,
                f"missing read-only authorization-storage probe: {required}")
    for forbidden in (
        "nvs_flash_init", "nvs_flash_secure_init", "nvs_flash_generate_keys",
        "nvs_open", "nvs_set_", "nvs_commit", "nvs_erase",
        "esp_efuse_write", "esp_efuse_batch_write",
    ):
        require(forbidden not in authorization_storage,
                f"read-only storage probe must not mutate target state: {forbidden}")
    require("secure_zero(message.data(), message.size())" in
            authorization_persistence and
            "secure_zero(derived.data(), derived.size())" in
            authorization_persistence,
            "private bond reference and PRF scratch must be explicitly wiped")
    command_flags = nimble_gatt[
        nimble_gatt.index("constexpr ble_gatt_chr_flags kCommandFlags"):
        nimble_gatt.index("constexpr ble_gatt_chr_flags kStreamFlags")
    ]
    require("BLE_GATT_CHR_F_WRITE" in command_flags and
            "BLE_GATT_CHR_F_WRITE_NO_RSP" not in command_flags,
            "Command must remain Write With Response only")
    for uuid_tail in (
        "0x00, 0x2A, 0x0F, 0x5E",
        "0x01, 0x2A, 0x0F, 0x5E",
        "0x02, 0x2A, 0x0F, 0x5E",
        "0x03, 0x2A, 0x0F, 0x5E",
        "0x04, 0x2A, 0x0F, 0x5E",
    ):
        require(uuid_tail in nimble_gatt,
                f"missing exact v0 UUID tail: {uuid_tail}")

    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    for required in (
        "companion/src/companion_protocol.cpp",
        "companion/src/companion_semantics.cpp",
        "companion/src/companion_request_coordinator.cpp",
        "companion/src/companion_gatt_session.cpp",
        "companion/src/companion_authorization_wire.cpp",
        "companion/src/companion_gatt_authorization.cpp",
        "companion/src/companion_gatt_authorization_adapter.cpp",
        "companion/src/companion_authorization.cpp",
        "companion/src/companion_authorization_persistence.cpp",
        "companion/src/companion_authorization_protected_kv_media.cpp",
        "companion_authorization_nvs_backend.cpp",
        "companion_authorization_nvs_context.cpp",
        "companion_protected_root_key_roster_adapter.cpp",
        "companion_protected_root_configuration_security_adapter.cpp",
        "companion/src/companion_ble_runtime_owner.cpp",
        "companion/src/companion_public_link_info.cpp",
        "companion_authorization_storage.cpp",
        "companion_boot_self_check.cpp",
        "companion_nimble_gatt.cpp",
        "companion_nimble_runtime.cpp",
        "companion/include",
        "heltec_startup_display.cpp",
        "heltec_v4_battery.cpp",
        "heltec_v4_gnss.cpp",
        "heltec_v4_oled.cpp",
        "protocol/include",
        "ui/src/compact_status_footer.cpp",
        "ui/include",
        "radio/include",
    ):
        require(required in cmake,
                f"target must link accepted companion surface: {required}")
    require(cmake.count('.cpp"') == 29,
            "target source set must remain thirteen target, twelve companion, and one UI source")
    require("REQUIRES" in cmake and all(
        dependency in cmake for dependency in (
            "bt", "bootloader_support", "efuse", "esp_partition", "esp_security",
            "esp_adc", "esp_driver_gpio", "esp_driver_i2c", "esp_driver_uart",
            "esp_lcd", "nvs_flash")),
            "target must declare pinned Bluetooth and security dependencies")

    linked_source = self_check + "\n" + nimble_gatt + "\n" + gatt_session + "\n" + gatt_authorization + "\n" + gatt_adapter + "\n" + authorization_persistence + "\n" + authorization_storage + "\n" + "\n".join(
        path.read_text(encoding="utf-8") for path in COMPANION_SOURCES)
    forbidden_initializers = (
        "esp_ble_", "esp_bt_controller", "nimble_port_init",
        "esp_nimble_hci_init", "ble_gap_adv_start",
        "ble_gap_ext_adv_start",
        "esp_wifi_init", "nvs_flash_init", "gpio_config",
        "spi_bus_initialize", "uart_driver_install", "radiolib",
        "sx126", "gnss_init", "gps_init", "gnss.begin", "gps.begin",
    )
    for token in forbidden_initializers:
        require(token not in linked_source.lower(),
                f"linked codec source contains forbidden initializer: {token}")

    forbidden = (
        "WiFi", "RadioLib",
        "SX126", "LoRa", "NVS", "nvs_", "SPIFFS", "FATFS",
        "OTA", "efuse", "MAC", "secret", "private_key", "identity",
    )
    for token in forbidden:
        require(token.lower() not in source.lower(),
                f"forbidden application capability token: {token}")


def test_build_only_tooling() -> None:
    defaults = DEFAULTS.read_text(encoding="utf-8")
    require("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" in defaults,
            "USB Serial/JTAG console must be explicit")
    competing_console_options = (
        "CONFIG_ESP_CONSOLE_UART_DEFAULT=y",
        "CONFIG_ESP_CONSOLE_UART_CUSTOM=y",
        "CONFIG_ESP_CONSOLE_USB_CDC=y",
        "CONFIG_ESP_CONSOLE_NONE=y",
        "CONFIG_ESP_CONSOLE_UART_NONE=y",
    )
    for option in competing_console_options:
        require(option not in defaults,
                f"competing or deprecated console selection present: {option}")
    require("CONFIG_APP_COMPILE_TIME_DATE=n" in defaults,
            "compile time must not be embedded")
    required_profile = (
        "CONFIG_ESPTOOLPY_OCT_FLASH=n",
        "CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=n",
        "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y",
        "CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y",
        "CONFIG_ESPTOOLPY_FLASHFREQ_80M=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        "CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=n",
        "CONFIG_PARTITION_TABLE_CUSTOM=y",
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
        "CONFIG_SPIRAM=y",
        "CONFIG_SPIRAM_MODE_QUAD=y",
        "CONFIG_SPIRAM_TYPE_ESPPSRAM16=y",
        "CONFIG_SPIRAM_SPEED_80M=y",
        "CONFIG_SPIRAM_BOOT_HW_INIT=y",
        "CONFIG_SPIRAM_BOOT_INIT=y",
        "CONFIG_SPIRAM_IGNORE_NOTFOUND=n",
        "CONFIG_SPIRAM_MEMTEST=y",
        "CONFIG_SPIRAM_USE_CAPS_ALLOC=y",
    )
    forbidden_profile = (
        "CONFIG_ESPTOOLPY_FLASHMODE_DIO=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y",
        "CONFIG_PARTITION_TABLE_SINGLE_APP=y",
        "CONFIG_SPIRAM_MODE_OCT=y",
        "CONFIG_SPIRAM_IGNORE_NOTFOUND=y",
    )
    for option in required_profile:
        require(option in defaults, f"missing exact OT-DEV-001 profile selection: {option}")
    for option in forbidden_profile:
        require(option not in defaults, f"generic or unsafe profile selection present: {option}")
    required_nimble = (
        "CONFIG_BT_ENABLED=y",
        "CONFIG_BT_CONTROLLER_ENABLED=y",
        "CONFIG_BT_NIMBLE_ENABLED=y",
        "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y",
        "CONFIG_BT_NIMBLE_GATT_SERVER=y",
        "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
        "CONFIG_BT_NIMBLE_SECURITY_ENABLE=y",
        "CONFIG_BT_NIMBLE_SM_SC=y",
        "CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION=y",
        "CONFIG_BT_NIMBLE_SM_LVL=3",
        "CONFIG_BT_NIMBLE_SM_SC_ONLY=1",
    )
    forbidden_nimble = (
        "CONFIG_BT_NIMBLE_ROLE_CENTRAL=y",
        "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y",
        "CONFIG_BT_NIMBLE_ROLE_OBSERVER=y",
        "CONFIG_BT_NIMBLE_GATT_CLIENT=y",
        "CONFIG_BT_NIMBLE_SM_LEGACY=y",
        "CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS=y",
        "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
    )
    for option in required_nimble:
        require(option in defaults,
                f"missing required NimBLE security selection: {option}")
    for option in forbidden_nimble:
        require(option not in defaults,
                f"forbidden NimBLE selection enabled: {option}")

    script = BUILD_SCRIPT.read_text(encoding="utf-8")
    require("ESP-IDF v6.0.2" in script, "build script must pin ESP-IDF")
    for exact_toolchain_evidence in (
        "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40",
        "xtensa-esp32s3-elf-gcc",
        "15.2.0",
        "esp-15.2.0_20251204",
        "20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5",
        "cmake version 4.0.3",
        "392ab4d6c3c91543fd297ed6c7e7354bf62edcd26fdf2706ad8613ad620cc45e",
        "1.12.1",
        "68865c3276d449d746cea5065fdec2baf755d7813e161ab04205b0907b2629b8",
        "Python 3.14.6",
        "199ce15a9f0d4f9522edba59338e4879d28cf61f88e377b8164bcb716275ed22",
        "framework_tracked_source_clean = $true",
        "compiler_sha256 = $requiredCompilerSha256",
    ):
        require(exact_toolchain_evidence in script,
                f"OT-093 exact toolchain evidence missing: {exact_toolchain_evidence}")
    require("[ValidateSet('standard', 'ot093-a', 'ot093-b')]" in script and
            "OT-093 requires an absent, independent build directory" in script and
            "$env:CCACHE_DISABLE = '1'" in script,
            "OT-093 must use two absent independent build roots with shared cache disabled")
    require("CONFIG_APP_REPRODUCIBLE_BUILD=y" in script and
            "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6" in script and
            "reproducible_build_paths_normalized = $true" in script and
            "future_ot005_candidate_builds_require_same_baseline_config = $true" in script,
            "OT-093 must bind path-normalized defaults for this and future candidate builds")
    require("diff --quiet $acceptedFirmwareBaseCommit -- firmware/components firmware/targets/heltec_v4_bench" in script and
            "$requiredFirmwareInputManifestFileCount = 307" in script and
            "$requiredFirmwareInputManifestSha256 = '6738195a7da53eb3d03c4a47552f6c0b6489559a2d81c0ba068489fe9faf7bc3'" in script and
            "ls-files --stage" in script and
            "ls-files --others --exclude-standard" in script and
            "status --porcelain --untracked-files=all" in script and
            "firmware_input_manifest_kind = 'git-index-stage-zero-v1'" in script and
            "working_tree_manifest_kind = 'sha256-raw-bytes-path-v1'" in script and
            "$requiredFirmwareWorkingTreeManifestSha256 = '3837dbce866a3fc7cef76fd374bf242bb0125c042e8de15273a9e44bafff3324'" in script and
            "$requiredGitCoreAutocrlf = 'true'" in script and
            "firmware_input_manifest_sha256 = $sourceManifestSha256" in script,
            "OT-093 must bind a clean firmware scope and deterministic input manifest")
    require("$requiredOt093ProjectVersion = 'ot093-precrypto-v0'" in script and
            'PROJECT_VER=$requiredOt093ProjectVersion' in script and
            "project_description.json" in script and
            "application image does not embed the stable OT-093 project version" in script,
            "OT-093 must freeze and verify a Git-state-independent embedded project version")
    require('$env:PYTHONPYCACHEPREFIX = $ot093PythonCacheRoot' in script and
            'ot093-python-cache-$BuildProfile' in script and
            "$env:PYTHONNOUSERSITE = '1'" in script and
            "independent_python_cache = $true" in script,
            "OT-093 Python imports must use a fresh per-profile cache and no user site")
    for artifact_role in (
        "'application'", "'application_elf'", "'linker_map'", "'bootloader'",
        "'partition_table'", "'generated_sdkconfig'", "'partition_csv'",
    ):
        require(f"Role = {artifact_role}" in script,
                f"OT-093 receipt is missing ordered artifact role {artifact_role}")
    require("applicationHeadroomBytes = $smallestAppSlotBytes - $applicationImageBytes" in script and
            "headroom_bytes = $applicationHeadroomBytes" in script,
            "OT-093 must prove the exact application-slot headroom equation")
    require("NO-OT005-CANDIDATE-OR-SECURE-LORA-ADAPTER-IMPORTED-OR-EXECUTED" in script,
            "OT-093 must state the candidate-specific pre-selection claim")
    require(script.count("ot005_pre_selection_baseline") == 1 and
            "$evidence['ot005_pre_selection_baseline']" in script,
            "standard evidence must omit the OT-093-only frozen baseline receipt")
    require("BUILD-RUN-CAPTURED; OTCBL0-RECONCILIATION-PENDING; OTCB0-EXECUTION-BLOCKED" in script and
            "BUILD-BASELINE-FROZEN" not in script,
            "one helper run must not claim aggregate two-run baseline acceptance")
    for denied_claim in (
        "ot005_candidate_imported = $false",
        "secure_lora_adapter_imported = $false",
        "secure_lora_adapter_executed = $false",
        "suite_selected = $false",
        "handshake_implemented = $false",
        "packet_v1_wire_selected = $false",
        "radio_enabled = $false",
        "hardware_or_device_accessed = $false",
        "key_or_entropy_operation = $false",
        "score_credit_added = $false",
    ):
        require(denied_claim in script,
                f"OT-093 build receipt is missing denied authority: {denied_claim}")
    for forbidden_link_token in (
        "'libsodium'", "'sodium_'", "'monocypher'", "'noise_xk'",
        "'secure_lora'", "'otsl0'", "'otcb0'",
    ):
        require(forbidden_link_token in script,
                f"OT-093 link-map guard is missing {forbidden_link_token}")
    require(re.search(r"set-target\s+esp32s3", script, re.IGNORECASE) is not None,
            "build script must select ESP32-S3")
    require("if ($requiresTargetSelection)" in script,
            "target selection must be conditional for incremental rebuilds")
    require("CONFIG_IDF_TARGET=\"esp32s3\"" in script,
            "incremental admission must check the exact generated target")
    require("preserving incremental build state" in script,
            "accepted incremental path must remain explicit")
    require(re.search(r"(?m)^\s*build(?:\s+2>&1\))?\s*$", script) is not None,
            "build script must invoke the build action")
    require(re.search(r"(?m)^\s*size\s*$", script) is not None,
            "build script must run image-size analysis")
    require('SDKCONFIG=$sdkconfigPath' in script,
            "generated sdkconfig must stay below the ignored build root")
    require("status = 'NOT-FLASHED'" in script,
            "build evidence must deny physical write evidence")
    for suffix in (".bin'", ".elf'", ".map'", "partition-table.bin'"):
        require(suffix in script, f"missing artifact evidence surface: {suffix}")
    require("console_primary = 'USB_SERIAL_JTAG'" in script,
            "build evidence must record the inspected primary console")
    require("framework_log_surface = 'UNREVIEWED-RUNTIME'" in script,
            "build evidence must preserve the runtime log-review gap")
    require("$requiredGeneratedProfileSelections" in script and
            "$requiredDisabledGeneratedProfileSelections" in script and
            'CONFIG_ESPTOOLPY_FLASHMODE="dio"' in script and
            'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"' in script and
            'CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"' in script,
            "build helper must reject or regenerate a stale generated profile")
    require("observed_flash_size_bytes = 16777216" in script and
            "observed_psram_size_bytes = 2097152" in script and
            "partition_layout = 'OTHP0/v0'" in script,
            "build evidence must record the exact memory/layout profile")
    require("image_header_flash_mode = 'DIO-bootstrap'" in script and
            "image_header_flash_size = '16MB'" in script and
            "image_header_flash_frequency = '80MHz'" in script,
            "build helper must verify and record the exact image header")
    require("partition_binary_verified = $partitionBinaryVerified" in script and
            "factory_slot_bytes = 5177344" in script and
            "ota_slot_bytes = 5242880" in script,
            "build helper must verify the recovery partition binary")
    require("companion_codec_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime self-check evidence")
    require("companion_request_coordinator_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime coordinator evidence")
    require("companion_gatt_session_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime GATT-session evidence")
    require("companion_gatt_authorization_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime authorization evidence")
    require("companion_gatt_authorization_adapter_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime callback-adapter evidence")
    require("companion_authorization_storage_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime storage self-check evidence")
    require("companion_ble_runtime_owner_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime-owner self-check evidence")
    require("companion_authorization_persistence = 'BUILD-LINKED-PROTECTED-BACKEND-NOT-INJECTED'" in script and
            "companion_authorization_storage_preflight = 'DENIED-NVS-ENCRYPTION-NOT-CONFIGURED'" in script,
            "build evidence must preserve closed protected-storage admission")
    require("companion_authorization_nvs_backend = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'" in script,
            "build evidence must distinguish compilation from runtime injection")
    require("companion_authorization_nvs_context = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'" in script,
            "build evidence must keep the context owner outside runtime")
    require("protected_root_key_roster_adapter = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'" in script and
            "protected_root_key_roster_execution = 'NOT-AUTHORIZED-NOT-RUN'" in script,
            "build evidence must keep the key-roster adapter inactive")
    require("$protectedRootRosterHeaderPath" in script and
            "$protectedRootRosterSourcePath" in script and
            "protected_root_key_roster_build_evidence = $protectedRootRosterBuildEvidence" in script,
            "build receipt must bind the exact adapter header and source")
    for required_receipt_field in (
        "name = Split-Path -Leaf $protectedRootRosterHeaderPath",
        "name = Split-Path -Leaf $protectedRootRosterSourcePath",
        "name = $protectedRootRosterObject.Name",
        "sha256 = (Get-FileHash -LiteralPath $protectedRootRosterObject.FullName",
    ):
        require(required_receipt_field in script,
                f"build receipt is missing roster revision evidence: {required_receipt_field}")
    require("protected_root_configuration_security_adapter = 'BUILD-COMPILED-NOT-RUNTIME-INJECTED'" in script and
            "protected_root_configuration_security_execution = 'NOT-AUTHORIZED-NOT-RUN'" in script,
            "build evidence must keep the configuration/security adapter inactive")
    require("$configurationSecurityHeaderPath" in script and
            "$configurationSecuritySourcePath" in script and
            "protected_root_configuration_security_build_evidence = $configurationSecurityBuildEvidence" in script,
            "build receipt must bind the exact configuration/security sources")
    for required_receipt_field in (
        "name = Split-Path -Leaf $configurationSecurityHeaderPath",
        "name = Split-Path -Leaf $configurationSecuritySourcePath",
        "name = $configurationSecurityObject.Name",
        "sha256 = (Get-FileHash -LiteralPath $configurationSecurityObject.FullName",
    ):
        require(required_receipt_field in script,
                f"build receipt is missing configuration/security evidence: {required_receipt_field}")
    for expected_hash in (
        "CD6C5462CB1B2ADFE7735915810461EDE96ECF0B830A0761E4CAF2E6CB982C73",
        "66A12FA28B11642385C54A249CEC8EBEE139BF7A5BF562B9D5AFF29A3B8CF3F4",
        "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E",
        "4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7",
        "B5299EE67627C912C5E7A0E4A908D1678FD0D2F12D5AFD7A58D849FC1BADAA30",
    ):
        require(expected_hash in script,
                f"build helper must pin OT-080 metadata source: {expected_hash}")
    for expected_hash in (
        "2589E0573A1F32C3CBF69D07AB1CF591A5A55935FBDCD76E12A59DA1DACA8B3D",
        "4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7",
        "66A12FA28B11642385C54A249CEC8EBEE139BF7A5BF562B9D5AFF29A3B8CF3F4",
        "E7C04ACDF54CDA0EFF2F2AC7551D6B25CB782E62A2F221C0E4B31DDC37D57AB5",
        "B83AE97309A1AF3A7AE114C30033FDC88AB55842B0CF54ABBF64717C3AE9B8F7",
        "DA8BA0B51CEA533541E139D88F612ABECA447ACEB73F822A70C1A7A4D43E3234",
        "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E",
        "28C92CF756E98CDBDC31FBBE0A4C7C23E0415E620CB51323D06B66939B33EEFB",
        "B73B8946370A4815391F90067C2A760466C60B68E67EB69FA725C14668430FDA",
        "ED0199B6407A1E920C9FC6169FB6E0EBA97241D01320FC52255E2AB16E1BDB06",
        "60C5EA67B4B957DEDF74477C3B618BE1C9B311099974EFD373E1500A58D181F9",
    ):
        require(expected_hash in script,
                f"build helper must pin OT-082 configuration/security source: {expected_hash}")
    require("companion_public_link_info = 'BUILD-LINKED-PUBLIC-READ-NOT-RUN'" in script and
            "bounded_public_link_window = 'HOST-TESTED-BUILD-LINKED-NOT-RUN'" in script and
            "public_link_hardware_observation = 'NOT-RUN'" in script,
            "build evidence must keep the public link at build-only status")
    require("$publicLinkInfoHeaderPath" in script and
            "$publicLinkInfoSourcePath" in script and
            "companion_public_link_info_build_evidence = $publicLinkInfoBuildEvidence" in script,
            "build receipt must bind the exact public link-info sources")
    for required_receipt_field in (
        "name = Split-Path -Leaf $publicLinkInfoHeaderPath",
        "name = Split-Path -Leaf $publicLinkInfoSourcePath",
        "name = $publicLinkInfoObject.Name",
        "sha256 = (Get-FileHash -LiteralPath $publicLinkInfoObject.FullName",
    ):
        require(required_receipt_field in script,
                f"build receipt is missing public link revision evidence: {required_receipt_field}")
    require("companion_public_link_info.cpp.obj" in script,
            "build helper must verify the public link-info object in the link map")
    require("companion_nimble_gatt = 'BUILD-LINKED-RUNTIME-PATH-NOT-RUN'" in script and
            "companion_nimble_runtime = 'CODED-BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny GATT/runtime execution evidence")
    require("heltec_v4_bench_nimble_order_tests.py" in script and
            "--idf-path $env:IDF_PATH" in script,
            "build helper must enforce pinned NimBLE disconnect ordering")
    require("oled_startup_display = 'CODED-BUILD-LINKED-NOT-RUN'" in script and
            "oled_controller_candidate = 'SSD1315-COMPATIBLE-128X64-NOT-PHYSICALLY-VERIFIED'" in script and
            "oled_ble_phase_status = 'HOST-TESTED-BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must preserve the OLED execution and binding gap")
    require("oled_logo_source_sha256 = (Get-FileHash" in script and
            "$logoPath" in script,
            "build evidence must hash the exact admitted startup logo source")
    require("nimble_controller = 'CODED-BUILD-LINKED-NOT-RUN'" in script and
            "advertising = 'CODED-PRIVATE-SERVICE-ONLY-NOT-RUN'" in script and
            "application_authorization = 'NOT-INJECTED'" in script,
            "build evidence must preserve the closed runtime boundary")
    require("companion_protocol.cpp.obj" in script and
            "companion_semantics.cpp.obj" in script and
            "companion_request_coordinator.cpp.obj" in script and
            "companion_gatt_session.cpp.obj" in script and
            "companion_authorization_wire.cpp.obj" in script and
            "companion_gatt_authorization.cpp.obj" in script and
            "companion_gatt_authorization_adapter.cpp.obj" in script and
            "companion_authorization.cpp.obj" in script and
            "companion_authorization_persistence.cpp.obj" in script and
            "companion_boot_self_check.cpp.obj" in script and
            "companion_nimble_gatt.cpp.obj" in script and
            "companion_authorization_storage.cpp.obj" in script,
            "build helper must verify all companion/self-check objects in the link map")
    require("companion_nimble_runtime.cpp.obj" in script and
            "companion_ble_runtime_owner.cpp.obj" in script,
            "build helper must verify runtime objects in the link map")
    require("companion_authorization_nvs_backend.cpp.obj" in script and
            "companion_authorization_protected_kv_media.cpp.obj" in script and
            "companion_authorization_nvs_context.cpp.obj" in script,
            "build helper must verify all inactive storage objects were compiled")
    require("companion_protected_root_key_roster_adapter.cpp.obj" in script,
            "build helper must verify the inactive key-roster adapter object")
    require("companion_protected_root_configuration_security_adapter.cpp.obj" in script,
            "build helper must verify the inactive configuration/security object")
    require("protected_nvs = 'NOT-INITIALIZED-NOT-VERIFIED'" in script and
            "private_bond_store = 'NOT-IMPLEMENTED'" in script and
            "binding_prf_key = 'NOT-PROVISIONED-NOT-VERIFIED'" in script and
            "rollback_floor = 'NOT-IMPLEMENTED'" in script,
            "build evidence must preserve exact security-provisioning gaps")
    require("heltec_startup_display.cpp.obj" in script and
            "heltec_v4_oled.cpp.obj" in script,
            "build helper must verify OLED owner and adapter objects in the link map")

    require("Generated sdkconfig did not select USB Serial/JTAG" in script,
            "build helper must inspect the generated console selection")

    forbidden_commands = (
        r"\bidf\.py\s+flash\b",
        r"\bwrite_flash\b",
        r"\besptool(?:\.py)?\b",
        r"\berase_flash\b",
        r"\b--port\b",
        r"\bCOM\d+\b",
    )
    combined = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (BUILD_SCRIPT, TARGET / "CMakeLists.txt",
                     TARGET / "main" / "CMakeLists.txt")
    )
    for pattern in forbidden_commands:
        require(re.search(pattern, combined, re.IGNORECASE) is None,
                f"device-write surface present: {pattern}")

    destructive_idf_actions = (
        "flash", "app-flash", "app_flash", "bootloader-flash",
        "bootloader_flash", "partition-table-flash", "partition_table_flash",
        "encrypted-flash", "encrypted_flash", "erase-flash", "erase_flash",
        "erase-otadata", "erase_otadata",
    )
    for action in destructive_idf_actions:
        require(re.search(
            rf"(?m)^\s*{re.escape(action)}\s*$", script,
            re.IGNORECASE) is None,
            f"destructive ESP-IDF action present: {action}")


def test_automatic_termination_acceptance_surface() -> None:
    runtime = NIMBLE_RUNTIME.read_text(encoding="utf-8")
    policy = ANDROID_TERMINATION_POLICY.read_text(encoding="utf-8")
    instrumentation = ANDROID_TERMINATION_INSTRUMENTATION.read_text(
        encoding="utf-8")
    android_build = ANDROID_APP_BUILD.read_text(encoding="utf-8")

    require(
        re.search(
            r"kRuntimePolicy\s*\{\s*10000\s*,\s*1000\s*,\s*3\s*,"
            r"\s*15000\s*,\s*2000\s*\}",
            runtime,
        ) is not None,
        "target automatic termination policy must remain exactly 15 seconds",
    )
    require("TARGET_WINDOW_MILLIS = 15_000L" in policy,
            "Android acceptance must pin the exact target window")
    require(
        "TARGET_WINDOW_MILLIS - EARLY_CALLBACK_ALLOWANCE_MILLIS" in policy and
        "TARGET_WINDOW_MILLIS + LATE_CALLBACK_ALLOWANCE_MILLIS" in policy and
        "MAX_ACCEPTED_MILLIS + WAIT_MARGIN_MILLIS" in policy,
        "Android timing bounds must derive from the pinned target window",
    )
    for required in (
        "SystemClock.elapsedRealtime()",
        "AtomicLong(0L)",
        "stage.compareAndSet(Stage.HOLDING, Stage.DISCONNECTED)",
        "status == BluetoothGatt.GATT_SUCCESS",
        "PublicLinkAutomaticTerminationPolicy.acceptsElapsed(elapsedMillis)",
        "initial.size == 1",
        "setServiceUuid(ParcelUuid(SERVICE_UUID))",
        "postTermination.size == 1",
        "OT085B_PUBLIC_READ=",
        "OT085B_AUTOMATIC_TERMINATION=",
        "OT085B_COMPATIBLE_ADVERTISER_RETURNED=",
        "OT085B_PHONE_ACCEPTANCE=",
    ):
        require(required in instrumentation,
                f"missing bounded automatic-termination gate: {required}")
    for forbidden in (
        "gatt.disconnect(",
        ".requestDisconnect(",
        "BluetoothDevice.EXTRA_DEVICE",
        "result.device.address",
        "result.device.name",
        "selectedDevice in scan(",
    ):
        require(forbidden not in instrumentation,
                f"phone-driven or identity-bearing harness surface: {forbidden}")
    require(
        '"io.github.nbjelanovic.otclient.PublicLinkProbeInstrumentation"' in
        android_build and
        "androidx.test.runner.AndroidJUnitRunner" not in android_build,
        "the built test APK must register the bounded OT-085B instrumentation",
    )


def test_pairing_input_surface() -> None:
    header = PAIRING_INPUT_HEADER.read_text(encoding="utf-8")
    source = PAIRING_INPUT_SOURCE.read_text(encoding="utf-8")

    for required in (
        "kHeltecV4PairingButtonGpio = 0",
        "kHeltecV4PairingButtonPressedLevel = 0",
        "kHeltecV4PairingButtonDebounceMs = 40",
        "kHeltecV4PairingButtonHoldMs = 3'000",
        "long_press_released",
        "void reset(bool raw_pressed, std::uint64_t now_ms)",
        "PairingInputEvent observe(",
        "PairingInputEvent poll(std::uint64_t now_ms)",
    ):
        require(required in header,
                f"pairing input header is missing: {required}")

    for required in (
        "GPIO_NUM_0",
        "GPIO_MODE_INPUT",
        "GPIO_PULLUP_ENABLE",
        "GPIO_PULLDOWN_DISABLE",
        "GPIO_INTR_DISABLE",
        "now_ms < last_observed_ms_",
        "reset(raw_pressed, now_ms)",
        "now_ms - raw_since_ms_ < kHeltecV4PairingButtonDebounceMs",
        "held_ms < kHeltecV4PairingButtonHoldMs",
        "PairingInputEvent::long_press_released",
    ):
        require(required in source,
                f"pairing input source is missing: {required}")

    combined = header + "\n" + source
    for forbidden in (
        "gpio_isr_handler_add",
        "gpio_install_isr_service",
        "xTaskCreate",
        "vTaskDelay",
        "esp_timer_get_time",
    ):
        require(forbidden not in combined,
                f"pairing input must remain caller-polled: {forbidden}")

    app_main = SOURCE.read_text(encoding="utf-8")
    main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    require("heltec_v4_pairing_input.hpp" in app_main and
            "g_pairing_input.poll(elapsed_ms)" in app_main and
            "PairingInputEvent::long_press_released" in app_main,
            "pairing input must be polled by the single app_main owner")
    require('"heltec_v4_pairing_input.cpp"' in main_cmake,
            "pairing input must be admitted into the target")


def test_secure_random_surface() -> None:
    header = SECURE_RANDOM_HEADER.read_text(encoding="utf-8")
    source = SECURE_RANDOM_SOURCE.read_text(encoding="utf-8")
    for required in (
        "final : public security::SecureRandomSource",
        "set_entropy_state", "state() const override", "fill(",
        "kMaximumSecureRandomRequestBytes", "esp_fill_random",
    ):
        require(required in header + "\n" + source,
                f"secure random adapter is missing: {required}")
    for forbidden in ("ESP_LOG", "printf", "snprintf", "%06"):
        require(forbidden not in header + "\n" + source,
                f"secure random adapter must retain/log no PIN: {forbidden}")
    app_main = SOURCE.read_text(encoding="utf-8")
    main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    require("HeltecV4SecureRandom g_pairing_random" in app_main and
            "set_entropy_state" in app_main,
            "app owner must gate target entropy on synchronized NimBLE state")
    require('"heltec_v4_secure_random.cpp"' in main_cmake and
            '"${OPENTRAIL_COMPONENT_ROOT}/security/include"' in main_cmake and
            "companion_pairing_window.cpp" in main_cmake,
            "target build must admit RNG and shared pairing component")


def main() -> int:
    tests = (test_contract, test_executed_oled_startup_flash_plan,
             test_physical_flash_plan, test_recovery_partition_layout,
             test_protected_storage_candidate_plan,
             test_authorization_nvs_backend_surface,
             test_protected_root_key_roster_adapter_surface,
             test_protected_root_configuration_security_adapter_surface,
             test_display_surface,
             test_application_surface, test_build_only_tooling,
             test_automatic_termination_acceptance_surface,
             test_pairing_input_surface, test_secure_random_surface)
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} Heltec V4 bench target admission groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
