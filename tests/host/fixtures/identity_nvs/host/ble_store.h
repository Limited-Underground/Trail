#pragma once
union ble_store_value {int unused;};
constexpr int BLE_STORE_OBJ_TYPE_OUR_SEC=1,BLE_STORE_OBJ_TYPE_PEER_SEC=2,BLE_STORE_OBJ_TYPE_CCCD=3,BLE_STORE_OBJ_TYPE_CSFC=4,BLE_STORE_OBJ_TYPE_PEER_ADDR=5;
int ble_store_iterate(int,int(*)(int,ble_store_value*,void*),void*);int ble_store_clear();
