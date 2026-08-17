#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

using nvs_handle_t = std::uint32_t;
inline constexpr int NVS_READWRITE = 1;

esp_err_t nvs_open_from_partition(const char* partition_label,
                                  const char* namespace_name,
                                  int open_mode,
                                  nvs_handle_t* output_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char* key,
                       void* output,
                       std::size_t* length);
esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char* key,
                       const void* data,
                       std::size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
