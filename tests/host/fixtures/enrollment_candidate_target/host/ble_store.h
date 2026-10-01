#pragma once
union ble_store_value{int unused;};
constexpr int BLE_STORE_OBJ_TYPE_OUR_SEC=1,BLE_STORE_OBJ_TYPE_PEER_SEC=2,BLE_STORE_OBJ_TYPE_CCCD=3,
    BLE_STORE_OBJ_TYPE_CSFC=4,BLE_STORE_OBJ_TYPE_PEER_ADDR=5;
int ble_store_iterate(int,int(*)(int,ble_store_value*,void*),void*);int ble_store_clear();

// Host SDK representation only; production sizeof is checked by target builds.
struct ble_store_value_sec{unsigned char bytes[64];};
struct ble_store_value_cccd{unsigned char bytes[16];};
struct ble_store_value_csfc{unsigned char bytes[16];};
struct ble_store_value_rpa_rec{unsigned char bytes[32];};
struct ble_store_value_local_irk{unsigned char bytes[16];};
