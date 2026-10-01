#pragma once
struct ble_hs_cfg_t {int(*store_read_cb)(int);int(*store_write_cb)(int);int(*store_delete_cb)(int);};
extern ble_hs_cfg_t ble_hs_cfg;
#define MYNEWT_VAL(x) 0
