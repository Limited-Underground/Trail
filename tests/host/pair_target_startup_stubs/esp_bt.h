#pragma once
#ifndef ESP_OK
#define ESP_OK 0
#endif
#define ESP_BT_MODE_BLE 1
struct esp_bt_controller_config_t {int mode;};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {1}
enum esp_bt_controller_status_t {ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED,ESP_BT_CONTROLLER_STATUS_ENABLED};
extern "C" {int esp_bt_controller_init(esp_bt_controller_config_t*);int esp_bt_controller_enable(int);int esp_bt_controller_disable();int esp_bt_controller_deinit();esp_bt_controller_status_t esp_bt_controller_get_status();}
