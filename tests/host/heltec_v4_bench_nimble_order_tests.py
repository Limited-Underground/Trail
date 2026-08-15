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
    port_path = (
        arguments.idf_path / "components" / "bt" / "host" / "nimble" /
        "nimble" / "porting" / "nimble" / "src" / "nimble_port.c"
    )
    for path in (gap_path, gatts_path, gattc_path, port_path, TARGET_ADAPTER):
        require(path.is_file(), f"required pinned source is missing: {path.name}")

    gap = gap_path.read_text(encoding="utf-8")
    gatts = gatts_path.read_text(encoding="utf-8")
    gattc = gattc_path.read_text(encoding="utf-8")
    adapter = TARGET_ADAPTER.read_text(encoding="utf-8")
    port = port_path.read_text(encoding="utf-8")

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
