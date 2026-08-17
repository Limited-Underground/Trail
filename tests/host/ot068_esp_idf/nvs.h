#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

using nvs_handle_t = std::uint32_t;

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char* key,
                       void* output,
                       std::size_t* length);
esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char* key,
                       const void* data,
                       std::size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
