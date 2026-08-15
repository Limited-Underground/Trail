#!/usr/bin/env python3
"""Pin the NimBLE teardown ordering OT-052 relies on for exact indications."""

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
    for path in (gap_path, gatts_path, gattc_path, TARGET_ADAPTER):
        require(path.is_file(), f"required pinned source is missing: {path.name}")

    gap = gap_path.read_text(encoding="utf-8")
    gatts = gatts_path.read_text(encoding="utf-8")
    gattc = gattc_path.read_text(encoding="utf-8")
    adapter = TARGET_ADAPTER.read_text(encoding="utf-8")

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

    print("PASS: pinned NimBLE indication teardown ordering")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
