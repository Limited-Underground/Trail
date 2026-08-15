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
NIMBLE_GATT = TARGET / "main" / "companion_nimble_gatt.cpp"
NIMBLE_RUNTIME = TARGET / "main" / "companion_nimble_runtime.cpp"
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
        "main/companion_authorization_storage.cpp",
        "main/companion_authorization_storage.hpp",
        "main/companion_nimble_gatt.cpp",
        "main/companion_nimble_gatt.hpp",
        "main/companion_nimble_runtime.cpp",
        "main/companion_nimble_runtime.hpp",
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
        "boot_companion_gatt_session_self_check",
        "boot_companion_gatt_authorization_self_check",
        "boot_companion_gatt_authorization_adapter_self_check",
        "boot_companion_authorization_storage_self_check",
        "boot_companion_ble_runtime_owner_self_check",
        "nimble_gatt_definition_build_linked",
        "nimble_gatt_callback_adapter_build_linked",
        "companion_authorization_persistence_build_linked",
        "companion_authorization_storage_preflight_build_linked",
        "nimble_runtime_owner_build_linked",
        "nimble_runtime_startup_coded",
        "private_service_advertising_coded",
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
    require(capabilities["nimble_runtime_owner_build_linked"] is True and
            capabilities["nimble_runtime_startup_coded"] is True and
            capabilities["private_service_advertising_coded"] is True,
            "bounded NimBLE runtime code must be explicitly admitted")
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
    runtime_start = source.index("start_companion_nimble_runtime(started_at_ms)")
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
        "BLE_HS_IO_NO_INPUT_OUTPUT", "sm_bonding = 1", "sm_mitm = 1",
        "sm_sc = 1", "StaticQueue_t", "std::atomic<bool>",
        "nvs_encryption_not_configured", "BLE_HS_ENOTCONN",
        "connection_termination_failed", "ble_gap_terminate",
        "observe_host_run_exit", "host_started_ && !host_run_exited_",
    ):
        require(required in nimble_runtime,
                f"missing bounded NimBLE runtime gate: {required}")
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
    require("ble_gap_terminate" in connect_case and
            "termination == BLE_HS_ENOTCONN" in connect_case and
            "RuntimeEventKind::connection_termination_failed" in connect_case,
            "denied-storage connect must close or contain on terminate failure")
    for required in (
        "operation_active_", "reentry_observed_", "callback_overflow",
        "max_restart_attempts", "restart_token", "contain_stack",
        "connection_termination_failed",
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
    for forbidden in (
        "nvs_flash_init", "nvs_open", "nvs_set_", "nvs_commit",
        "esp_hmac_calculate", "esp_efuse_write", "esp_efuse_batch_write",
    ):
        require(forbidden not in authorization_storage,
                f"dormant storage preflight must not access target state: {forbidden}")
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
        "companion/src/companion_ble_runtime_owner.cpp",
        "companion_authorization_storage.cpp",
        "companion_boot_self_check.cpp",
        "companion_nimble_gatt.cpp",
        "companion_nimble_runtime.cpp",
        "companion/include",
        "protocol/include",
        "radio/include",
    ):
        require(required in cmake,
                f"target must link accepted companion surface: {required}")
    require(cmake.count('.cpp"') == 15,
            "target source set must remain five target and ten accepted companion sources")
    require("REQUIRES" in cmake and "bt" in cmake,
            "target must declare the pinned ESP-IDF Bluetooth dependency")

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
    require("companion_nimble_gatt = 'BUILD-LINKED-RUNTIME-PATH-NOT-RUN'" in script and
            "companion_nimble_runtime = 'CODED-BUILD-LINKED-NOT-RUN'" in script,
            "build evidence must deny GATT/runtime execution evidence")
    require("heltec_v4_bench_nimble_order_tests.py" in script and
            "--idf-path $env:IDF_PATH" in script,
            "build helper must enforce pinned NimBLE disconnect ordering")
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
    require("protected_nvs = 'NOT-INITIALIZED-NOT-VERIFIED'" in script and
            "private_bond_store = 'NOT-IMPLEMENTED'" in script and
            "binding_prf_key = 'NOT-PROVISIONED-NOT-VERIFIED'" in script and
            "rollback_floor = 'NOT-IMPLEMENTED'" in script,
            "build evidence must preserve exact security-provisioning gaps")
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
