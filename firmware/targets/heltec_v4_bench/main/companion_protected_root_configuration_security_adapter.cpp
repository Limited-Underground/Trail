#include "companion_protected_root_configuration_security_adapter.hpp"

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_secure_boot.h"
#include "sdkconfig.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr bool build_nvs_encryption_enabled() noexcept {
#if defined(CONFIG_NVS_ENCRYPTION) && CONFIG_NVS_ENCRYPTION
    return true;
#else
    return false;
#endif
}

constexpr bool build_hmac_protection_selected() noexcept {
#if defined(CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC) && \
    CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC
    return true;
#else
    return false;
#endif
}

constexpr bool build_flash_encryption_protection_selected() noexcept {
#if defined(CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC) && \
    CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC
    return true;
#else
    return false;
#endif
}

constexpr bool build_external_protection_selected() noexcept {
#if defined(CONFIG_NVS_SEC_KEY_PROTECT_NONE) && CONFIG_NVS_SEC_KEY_PROTECT_NONE
    return true;
#else
    return false;
#endif
}

constexpr int build_hmac_key_slot() noexcept {
#if defined(CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID)
    return CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID;
#else
    return -1;
#endif
}

}  // namespace

ProtectedRootNvsBuildConfiguration normalize_protected_root_nvs_build_configuration(
    bool nvs_encryption_enabled,
    bool hmac_protection_selected,
    bool flash_encryption_protection_selected,
    bool external_protection_selected,
    int configured_hmac_key_slot) noexcept {
    ProtectedRootNvsBuildConfiguration result{};
    result.observed = true;
    result.nvs_encryption_enabled = nvs_encryption_enabled;

    const unsigned selected_modes =
        static_cast<unsigned>(hmac_protection_selected) +
        static_cast<unsigned>(flash_encryption_protection_selected) +
        static_cast<unsigned>(external_protection_selected);

    if (!nvs_encryption_enabled) {
        if (selected_modes != 0U || configured_hmac_key_slot != -1) {
            return result;
        }
        result.valid = true;
        result.protection_mode = ProtectedRootNvsProtectionMode::disabled;
        return result;
    }

    if (selected_modes != 1U) {
        return result;
    }

    if (hmac_protection_selected) {
        if (configured_hmac_key_slot < 0 || configured_hmac_key_slot > 5) {
            return result;
        }
        result.valid = true;
        result.protection_mode = ProtectedRootNvsProtectionMode::hmac;
        result.configured_hmac_key_slot_known = true;
        result.configured_hmac_key_slot =
            static_cast<std::uint8_t>(configured_hmac_key_slot);
        return result;
    }

    if (configured_hmac_key_slot != -1) {
        return result;
    }

    result.valid = true;
    result.protection_mode = flash_encryption_protection_selected
                                 ? ProtectedRootNvsProtectionMode::flash_encryption
                                 : ProtectedRootNvsProtectionMode::external;
    return result;
}

ProtectedRootConfigurationSecurityReadResult
EspIdfProtectedRootConfigurationSecurityAdapter::read_once() noexcept {
    ProtectedRootConfigurationSecurityReadResult denied{};
    if (active_) {
        poisoned_ = true;
        denied.status = ProtectedRootConfigurationSecurityReadStatus::reentry;
        return denied;
    }
    if (attempted_ || poisoned_) {
        denied.status =
            ProtectedRootConfigurationSecurityReadStatus::already_attempted;
        return denied;
    }

    attempted_ = true;
    active_ = true;

    const auto nvs = normalize_protected_root_nvs_build_configuration(
        build_nvs_encryption_enabled(), build_hmac_protection_selected(),
        build_flash_encryption_protection_selected(),
        build_external_protection_selected(), build_hmac_key_slot());
    if (!nvs.valid) {
        active_ = false;
        return denied;
    }

    ProtectedRootSecurityStateMetadata security{};
    security.secure_boot_enabled = esp_secure_boot_enabled();
    if (poisoned_) {
        active_ = false;
        denied.status = ProtectedRootConfigurationSecurityReadStatus::reentry;
        return denied;
    }
    security.flash_encryption_enabled =
        esp_efuse_is_flash_encryption_enabled();
    if (poisoned_) {
        active_ = false;
        denied.status = ProtectedRootConfigurationSecurityReadStatus::reentry;
        return denied;
    }
    security.secure_download_enabled =
        esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD);
    if (poisoned_) {
        active_ = false;
        denied.status = ProtectedRootConfigurationSecurityReadStatus::reentry;
        return denied;
    }
    security.download_mode_disabled =
        esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE);
    if (poisoned_) {
        active_ = false;
        denied.status = ProtectedRootConfigurationSecurityReadStatus::reentry;
        return denied;
    }
    security.complete = true;

    active_ = false;
    ProtectedRootConfigurationSecurityReadResult accepted{};
    accepted.status = ProtectedRootConfigurationSecurityReadStatus::complete;
    accepted.complete = true;
    accepted.nvs = nvs;
    accepted.security = security;
    return accepted;
}

}  // namespace opentrail::target::heltec_v4_bench
