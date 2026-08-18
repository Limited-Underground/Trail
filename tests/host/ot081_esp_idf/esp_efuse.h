#pragma once

#include <cstdint>

extern "C" {

typedef enum {
    EFUSE_BLK_KEY0 = 4,
    EFUSE_BLK_KEY1 = 5,
    EFUSE_BLK_KEY2 = 6,
    EFUSE_BLK_KEY3 = 7,
    EFUSE_BLK_KEY4 = 8,
    EFUSE_BLK_KEY5 = 9,
    EFUSE_BLK_KEY_MAX = 10,
} esp_efuse_block_t;

typedef enum {
    ESP_EFUSE_KEY_PURPOSE_USER = 0,
    ESP_EFUSE_KEY_PURPOSE_RESERVED = 1,
    ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1 = 2,
    ESP_EFUSE_KEY_PURPOSE_HMAC_UP = 8,
    ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST0 = 9,
    ESP_EFUSE_KEY_PURPOSE_MAX = 16,
} esp_efuse_purpose_t;

esp_efuse_purpose_t esp_efuse_get_key_purpose(esp_efuse_block_t block);
bool esp_efuse_get_key_dis_read(esp_efuse_block_t block);
bool esp_efuse_get_key_dis_write(esp_efuse_block_t block);
bool esp_efuse_get_keypurpose_dis_write(esp_efuse_block_t block);
bool esp_efuse_key_block_unused(esp_efuse_block_t block);

}  // extern "C"
