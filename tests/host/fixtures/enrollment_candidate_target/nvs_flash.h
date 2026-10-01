#pragma once
#include "nvs.h"
esp_err_t nvs_flash_init();
esp_err_t nvs_flash_init_partition(const char*);
esp_err_t nvs_flash_erase();
esp_err_t nvs_flash_erase_partition(const char*);
