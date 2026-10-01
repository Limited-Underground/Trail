#pragma once
struct ble_hs_cfg_t{int(*store_read_cb)(int);int(*store_write_cb)(int);int(*store_delete_cb)(int);};
extern ble_hs_cfg_t ble_hs_cfg;
#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS 3
#define MYNEWT_VAL_BLE_STORE_MAX_CCCDS 3
#define MYNEWT_VAL_BLE_STORE_MAX_CSFCS 3
#define MYNEWT_VAL_BLE_HOST_BASED_PRIVACY 0
#define MYNEWT_VAL_ENC_ADV_DATA 0
