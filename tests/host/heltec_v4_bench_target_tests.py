#!/usr/bin/env python3
"""Fail-closed source admission for the build-only Heltec V4 candidate."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
CONTRACT = TARGET / "target-contract.json"
SOURCE = TARGET / "main" / "app_main.cpp"
SELF_CHECK = TARGET / "main" / "companion_boot_self_check.cpp"
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
BUILD_SCRIPT = ROOT / "tools" / "Build-HeltecV4BenchTarget.ps1"


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
    require(document["evidence_state"] == "candidate", "must remain candidate")
    require(document["framework"] == {
        "name": "esp-idf",
        "version": "v6.0.2",
        "target": "esp32s3",
    }, "framework must be exactly pinned")
    require(document["hardware"]["exact_received_revision"] is None,
            "received revision must not be invented")
    require(document["hardware"]["supported"] is False,
            "candidate must not claim support")

    capabilities = document["capabilities"]
    admitted = {
        "bounded_usb_heartbeat",
        "boot_companion_codec_self_check",
        "boot_companion_request_self_check",
    }
    require(capabilities["bounded_usb_heartbeat"] is True,
            "heartbeat must remain admitted")
    require(capabilities["boot_companion_codec_self_check"] is True,
            "boot companion-codec self-check must be admitted")
    require(capabilities["boot_companion_request_self_check"] is True,
            "boot companion-request self-check must be admitted")
    for name, enabled in capabilities.items():
        if name not in admitted:
            require(enabled is False, f"capability must remain disabled: {name}")

    require(document["write_policy"] == {
        "build_only": True,
        "flashing_authorized": False,
    }, "write policy must remain build-only")


def test_application_surface() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    self_check = SELF_CHECK.read_text(encoding="utf-8")
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
    require("build-only bench candidate started" in source,
            "missing bounded startup record")
    require("heartbeat elapsed_ms=%llu" in source,
            "missing privacy-safe heartbeat")
    require("esp_timer_get_time" in source,
            "heartbeat must use boot-local monotonic time")
    self_check_call = source.index("if (!run_companion_codec_self_check() ||")
    coordinator_call = source.index(
        "run_companion_request_coordinator_self_check()")
    pass_log = source.index("companion boot self-check PASS")
    startup_log = source.index("build-only bench candidate started")
    heartbeat_log = source.index("heartbeat elapsed_ms=%llu")
    require(self_check_call < coordinator_call < pass_log < startup_log < heartbeat_log,
            "both self-checks must gate startup and heartbeat")

    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    for required in (
        "companion/src/companion_protocol.cpp",
        "companion/src/companion_semantics.cpp",
        "companion/src/companion_request_coordinator.cpp",
        "companion_boot_self_check.cpp",
        "companion/include",
        "protocol/include",
        "radio/include",
    ):
        require(required in cmake,
                f"target must link accepted companion surface: {required}")
    require(cmake.count('.cpp"') == 5,
            "target source set must remain app, self-check, and three accepted companion files")

    linked_source = self_check + "\n" + "\n".join(
        path.read_text(encoding="utf-8") for path in COMPANION_SOURCES)
    forbidden_initializers = (
        "esp_ble_", "esp_bt_controller", "nimble_port_init",
        "esp_wifi_init", "nvs_flash_init", "gpio_config",
        "spi_bus_initialize", "uart_driver_install", "radiolib",
        "sx126", "gnss_init", "gps_init", "gnss.begin", "gps.begin",
    )
    for token in forbidden_initializers:
        require(token not in linked_source.lower(),
                f"linked codec source contains forbidden initializer: {token}")

    forbidden = (
        "WiFi", "Bluetooth", "NimBLE", "esp_ble", "esp_bt", "RadioLib",
        "SX126", "LoRa", "GNSS", "GPS", "NVS", "nvs_", "SPIFFS", "FATFS",
        "OTA", "gpio_", "efuse", "MAC", "secret", "private_key", "identity",
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

    script = BUILD_SCRIPT.read_text(encoding="utf-8")
    require("ESP-IDF v6.0.2" in script, "build script must pin ESP-IDF")
    require(re.search(r"set-target\s+esp32s3", script, re.IGNORECASE) is not None,
            "build script must select ESP32-S3")
    require("if ($requiresTargetSelection)" in script,
            "target selection must be conditional for incremental rebuilds")
    require("CONFIG_IDF_TARGET=\"esp32s3\"" in script,
            "incremental admission must check the exact generated target")
    require("preserving incremental build state" in script,
            "accepted incremental path must remain explicit")
    require(re.search(r"(?m)^\s*build\s*$", script) is not None,
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
    require("companion_codec_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime self-check evidence")
    require("companion_request_coordinator_self_check = 'BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny runtime coordinator evidence")
    require("companion_protocol.cpp.obj" in script and
            "companion_semantics.cpp.obj" in script and
            "companion_request_coordinator.cpp.obj" in script and
            "companion_boot_self_check.cpp.obj" in script,
            "build helper must verify all companion/self-check objects in the link map")
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


def main() -> int:
    tests = (test_contract, test_application_surface, test_build_only_tooling)
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} Heltec V4 bench target admission groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
