#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"

struct nvs_sec_scheme_t {
    std::uint32_t marker{0};
};

struct nvs_sec_cfg_t {
    std::array<std::uint8_t, 64> bytes{};
};

nvs_sec_scheme_t* nvs_flash_get_default_security_scheme();
esp_err_t nvs_flash_read_security_cfg_v2(nvs_sec_scheme_t* scheme,
                                         nvs_sec_cfg_t* configuration);
esp_err_t nvs_flash_secure_init_partition(
    const char* partition_label,
    nvs_sec_cfg_t* configuration);
esp_err_t nvs_flash_deinit_partition(const char* partition_label);
