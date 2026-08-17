#pragma once

using esp_err_t = int;

inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
inline constexpr esp_err_t ESP_ERR_NVS_INVALID_HANDLE = 0x1107;
inline constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110C;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_INITIALIZED = 0x110D;
