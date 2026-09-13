#pragma once
enum esp_bt_controller_status_t { ESP_BT_CONTROLLER_STATUS_IDLE, ESP_BT_CONTROLLER_STATUS_INITED, ESP_BT_CONTROLLER_STATUS_ENABLED };
esp_bt_controller_status_t esp_bt_controller_get_status();
// No controller init/enable/disable/deinit API is available to this proof.
