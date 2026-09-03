#!/usr/bin/env python3
"""Pin NimBLE teardown and synchronous-stop ordering used by the target."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET_ADAPTER = (
    ROOT / "firmware" / "targets" / "heltec_v4_bench" / "main" /
    "companion_nimble_gatt.cpp"
)
TARGET_RUNTIME = (
    ROOT / "firmware" / "targets" / "heltec_v4_bench" / "main" /
    "companion_nimble_runtime.cpp"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start + len(signature))
    return source[start:end]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-path", required=True, type=Path)
    arguments = parser.parse_args()

    host_source = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "nimble" / "host" / "src"
    )
    gap_path = host_source / "ble_gap.c"
    gatts_path = host_source / "ble_gatts.c"
    gattc_path = host_source / "ble_gattc.c"
    gatt_service_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "nimble" / "host" / "services" / "gatt" / "src" /
        "ble_svc_gatt.c"
    )
    port_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "porting" / "nimble" / "src" / "nimble_port.c"
    )
    for path in (
        gap_path,
        gatts_path,
        gattc_path,
        gatt_service_path,
        port_path,
        TARGET_ADAPTER,
        TARGET_RUNTIME,
    ):
        require(path.is_file(), f"required pinned source is missing: {path.name}")

    gap = gap_path.read_text(encoding="utf-8")
    gatts = gatts_path.read_text(encoding="utf-8")
    gattc = gattc_path.read_text(encoding="utf-8")
    gatt_service = gatt_service_path.read_text(encoding="utf-8")
    adapter = TARGET_ADAPTER.read_text(encoding="utf-8")
    runtime = TARGET_RUNTIME.read_text(encoding="utf-8")
    port = port_path.read_text(encoding="utf-8")

    service_changed_definition = function_body(
        gatt_service,
        "ble_svc_gatt_changed(uint16_t start_handle, uint16_t end_handle)",
        "ble_svc_gatt_changed_handle(void)")
    require("BLE_GATT_CHR_F_INDICATE" in gatt_service and
            "ble_gatts_chr_updated(ble_svc_gatt_changed_val_handle);" in
            service_changed_definition,
            "pinned Service Changed must remain an indication-backed pending update")

    encryption_event = function_body(
        gap,
        "ble_gap_enc_event(uint16_t conn_handle, int status,",
        "ble_gap_identity_event")
    application_encryption_callback = encryption_event.index(
        "ble_gap_call_conn_event_cb(&event, conn_handle);")
    restore_bonded_cccds = encryption_event.index(
        "ble_gatts_bonding_restored(conn_handle);")
    require(application_encryption_callback < restore_bonded_cccds,
            "pinned NimBLE must restore bonded CCCDs after the app encryption callback")

    bonding_restored = function_body(
        gatts,
        "ble_gatts_bonding_restored(uint16_t conn_handle)",
        "ble_gatts_find_svc_entry_by_uuid")
    restored_changed = bonding_restored.index("if (cccd_value.value_changed)")
    restored_subscription = bonding_restored.index(
        "BLE_GAP_SUBSCRIBE_REASON_RESTORE")
    restored_indication = bonding_restored.index(
        "ble_gatts_indicate(conn_handle, cccd_value.chr_val_handle);")
    require(restored_changed < restored_subscription < restored_indication,
            "pinned NimBLE must expose the restored pending-indication race")

    require("clear_pending_cccd_value_changes()" in runtime,
            "Heltec startup lacks pending CCCD value-change sanitation")
    sanitizer = function_body(
        runtime,
        "clear_pending_cccd_value_changes()",
        "fail_security_configuration")
    require("ble_store_read_cccd(" in sanitizer and
            "cccd.value_changed = 0;" in sanitizer and
            "ble_store_write_cccd(&cccd)" in sanitizer,
            "Heltec startup must clear only pending CCCD value-change state")
    require("cccd.flags =" not in sanitizer and
            "ble_store_delete_cccd" not in sanitizer and
            "ble_store_clear" not in sanitizer,
            "pending-update sanitation must preserve subscriptions and bonds")
    configure_security = function_body(
        runtime,
        "bool configure_secure_connections_bonding() override",
        "bool register_protected_service() override")
    store_initialized = configure_security.index("ble_store_config_init();")
    pending_updates_cleared = configure_security.index(
        "clear_pending_cccd_value_changes()")
    store_exposed = configure_security.index(
        "g_factory_reset_bonds->set_store_access_ready(true);")
    require(store_initialized < pending_updates_cleared < store_exposed,
            "pending CCCD updates must clear after store init and before host exposure")

    broken = function_body(
        gap,
        "ble_gap_conn_broken(uint16_t conn_handle, int reason)",
        "ble_gap_update_to_l2cap")
    gatts_broken = broken.index("ble_gatts_connection_broken(conn_handle);")
    gattc_broken = broken.index("ble_gattc_connection_broken(conn_handle);")
    disconnect_event = broken.index("event.type = BLE_GAP_EVENT_DISCONNECT;")
    application_callback = broken.index("ble_gap_call_event_cb(")
    require(gatts_broken < gattc_broken < disconnect_event < application_callback,
            "pinned NimBLE must terminate indication procedures before app disconnect")

    gatts_disconnect = function_body(
        gatts,
        "ble_gatts_connection_broken(uint16_t conn_handle)",
        "ble_gatts_bonding_established")
    require("ble_gatts_indicate_fail_notconn(conn_handle);" in gatts_disconnect,
            "GATTS teardown must fail the active indication before disconnect")

    fail_not_connected = function_body(
        gattc,
        "ble_gatts_indicate_fail_notconn(uint16_t conn_handle)",
        "ble_gatts_indicate_custom")
    require("BLE_GATT_OP_INDICATE" in fail_not_connected and
            "BLE_HS_ENOTCONN" in fail_not_connected,
            "active indication teardown must produce an exact not-connected result")

    require("g_indication_port.pending_tuple()" in adapter and
            "g_adapter->status().pending" not in adapter,
            "NOTIFY_TX must use the immutable submission-era tuple")
    require("transport_generation_ = transport_generation" in adapter and
            "exchange_id_ = exchange_id" in adapter,
            "real indication port must retain generation and exchange")

    stop = function_body(port, "nimble_port_stop(void)", "nimble_port_run(void)")
    stop_request = stop.index("ble_hs_stop(&stop_listener")
    completion_wait = stop.index(
        "ble_npl_sem_pend(&ble_hs_stop_sem, BLE_NPL_TIME_FOREVER);")
    stop_event = stop.index("ble_npl_eventq_put(&g_eventq_dflt, &ble_hs_ev_stop);")
    event_wait = stop.index(
        "ble_npl_sem_pend(&ble_hs_stop_sem, BLE_NPL_TIME_FOREVER);",
        completion_wait + 1)
    require(stop_request < completion_wait < stop_event < event_wait,
            "pinned nimble_port_stop must synchronously await host completion and stop event")

    print("PASS: pinned NimBLE indication and synchronous-stop ordering")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
