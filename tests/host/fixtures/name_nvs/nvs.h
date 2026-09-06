#pragma once
#include <cstddef>
#include <cstdint>
using nvs_handle_t = std::uint32_t;
using esp_err_t = int;
inline constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NVS_NOT_FOUND=1,
    ESP_ERR_NVS_TYPE_MISMATCH=2, NVS_READONLY=0, NVS_READWRITE=1;
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_used_entry_count(nvs_handle_t, std::size_t*);
