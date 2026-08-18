#pragma once

struct esp_efuse_desc_t;

bool esp_efuse_is_flash_encryption_enabled(void);
bool esp_efuse_read_field_bit(const esp_efuse_desc_t* field[]);
