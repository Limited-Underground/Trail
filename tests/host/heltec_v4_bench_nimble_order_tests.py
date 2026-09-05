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
    att_path = host_source / "ble_att.c"
    att_server_path = host_source / "ble_att_svr.c"
    gap_path = host_source / "ble_gap.c"
    gatts_path = host_source / "ble_gatts.c"
    gattc_path = host_source / "ble_gattc.c"
    gatt_service_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "nimble" / "host" / "services" / "gatt" / "src" /
        "ble_svc_gatt.c"
    )
    startup_path = host_source / "ble_hs_startup.c"
    privacy_path = host_source / "ble_hs_pvcy.c"
    store_config_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "nimble" / "host" / "store" / "config" / "src" /
        "ble_store_config.c"
    )
    store_nvs_path = store_config_path.with_name("ble_store_nvs.c")
    port_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "porting" / "nimble" / "src" / "nimble_port.c"
    )
    for path in (
        att_path,
        att_server_path,
        gap_path,
        gatts_path,
        gattc_path,
        gatt_service_path,
        startup_path,
        privacy_path,
        store_config_path,
        store_nvs_path,
        port_path,
        TARGET_ADAPTER,
        TARGET_RUNTIME,
    ):
        require(path.is_file(), f"required pinned source is missing: {path.name}")

    att = att_path.read_text(encoding="utf-8")
    att_server = att_server_path.read_text(encoding="utf-8")
    gap = gap_path.read_text(encoding="utf-8")
    gatts = gatts_path.read_text(encoding="utf-8")
    gattc = gattc_path.read_text(encoding="utf-8")
    gatt_service = gatt_service_path.read_text(encoding="utf-8")
    startup = startup_path.read_text(encoding="utf-8")
    privacy = privacy_path.read_text(encoding="utf-8")
    store_config = store_config_path.read_text(encoding="utf-8")
    store_nvs = store_nvs_path.read_text(encoding="utf-8")
    adapter = TARGET_ADAPTER.read_text(encoding="utf-8")
    runtime = TARGET_RUNTIME.read_text(encoding="utf-8")

    port = port_path.read_text(encoding="utf-8")

    gatt_client_dispatch = att[
        att.index("#if MYNEWT_VAL(BLE_GATTC)"):
        att.index("#if MYNEWT_VAL(BLE_GATTS)")
    ]
    require("{ BLE_ATT_OP_INDICATE_REQ,         ble_att_svr_rx_indicate }" in
            gatt_client_dispatch,
            "GATT-client support must register incoming indications")
    build_indication_response = function_body(
        att_server,
        "ble_att_svr_build_indicate_rsp(struct os_mbuf **rxom",
        "ble_att_svr_rx_indicate(uint16_t conn_handle")
    receive_indication = function_body(
        att_server,
        "ble_att_svr_rx_indicate(uint16_t conn_handle",
        "ble_att_svr_move_entries")
    require("BLE_ATT_OP_INDICATE_RSP" in build_indication_response and
            "ble_att_svr_build_indicate_rsp" in receive_indication and
            "ble_att_svr_tx_rsp" in receive_indication,
            "incoming indications must build and transmit a confirmation")

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
    require("ble_hs_cfg.store_gen_key_cb" not in runtime and
            "ble_hs_pvcy_set_default_irk();" in startup,
            "Heltec must retain the pinned default-IRK startup path")
    default_irk = function_body(
        privacy,
        "void ble_hs_pvcy_set_default_irk(void)",
        "ble_hs_pvcy_set_our_irk")
    require("#if MYNEWT_VAL(BLE_STATIC_TO_DYNAMIC)" in default_irk and
            "ble_store_config_init();" in default_irk,
            "pinned privacy startup must expose the second store initialization")
    store_init = function_body(
        store_config,
        "ble_store_config_init(void)",
        "ble_store_config_deinit(void)")
    require("ble_store_config_num_cccds = 0;" in store_init and
            "ble_store_config_conf_init();" in store_init,
            "second store initialization must reset RAM before restoring NVS")
    restore_sec = function_body(
        store_nvs,
        "ble_nvs_restore_sec_keys(void)",
        "ble_nvs_restore_peer_records(void)")
    require("populate_db_from_nvs(BLE_STORE_OBJ_TYPE_CCCD" in restore_sec,
            "pinned NVS restore must reload persisted CCCD records")
    persist_cccds = function_body(
        store_nvs,
        "int ble_store_config_persist_cccds(void)",
        "int ble_store_config_persist_csfcs(void)")
    require("nvs_count < ble_store_config_num_cccds" in persist_cccds and
            "nvs_count > ble_store_config_num_cccds" in persist_cccds and
            "return 0;" in persist_cccds,
            "pinned equal-count CCCD writes must retain the reload hazard")
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
    require("clear_pending_cccd_value_changes()" not in configure_security,
            "pre-host sanitation is invalidated by the pinned second store initialization")
    configure_advertising = function_body(
        runtime,
        "bool configure_public_service_advertising() override",
        "bool start_advertising() override")
    pending_updates_cleared = configure_advertising.index(
        "clear_pending_cccd_value_changes()")
    address_ready = configure_advertising.index("ble_hs_util_ensure_addr(1)")
    require(pending_updates_cleared < address_ready,
            "pending CCCD updates must clear after host sync and before advertising")

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
