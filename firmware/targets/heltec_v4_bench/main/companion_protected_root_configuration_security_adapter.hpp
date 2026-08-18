#pragma once

#include <cstdint>

namespace opentrail::target::heltec_v4_bench {

enum class ProtectedRootNvsProtectionMode : std::uint8_t {
    disabled = 0,
    hmac = 1,
    flash_encryption = 2,
    external = 3,
};

enum class ProtectedRootConfigurationSecurityReadStatus : std::uint8_t {
    denied = 0,
    complete = 1,
    reentry = 2,
    already_attempted = 3,
};

struct ProtectedRootNvsBuildConfiguration {
    bool valid{false};
    bool observed{false};
    bool nvs_encryption_enabled{false};
    ProtectedRootNvsProtectionMode protection_mode{
        ProtectedRootNvsProtectionMode::disabled};
    bool configured_hmac_key_slot_known{false};
    std::uint8_t configured_hmac_key_slot{0U};
};

struct ProtectedRootSecurityStateMetadata {
    bool complete{false};
    bool secure_boot_enabled{false};
    bool flash_encryption_enabled{false};
    bool secure_download_enabled{false};
    bool download_mode_disabled{false};
};

struct ProtectedRootConfigurationSecurityReadResult {
    ProtectedRootConfigurationSecurityReadStatus status{
        ProtectedRootConfigurationSecurityReadStatus::denied};
    bool complete{false};
    ProtectedRootNvsBuildConfiguration nvs{};
    ProtectedRootSecurityStateMetadata security{};
};

// Pure normalization of the default build configuration only. It does not
// prove a runtime-selected NVS scheme or a physical eFuse allocation.
ProtectedRootNvsBuildConfiguration normalize_protected_root_nvs_build_configuration(
    bool nvs_encryption_enabled,
    bool hmac_protection_selected,
    bool flash_encryption_protection_selected,
    bool external_protection_selected,
    int configured_hmac_key_slot) noexcept;

// One-use, target-local source for bounded normalized configuration/security
// metadata. It has no serializer, transport, runtime registration, or authority
// to execute on a device. A complete result is inventory input only.
class EspIdfProtectedRootConfigurationSecurityAdapter final {
public:
    EspIdfProtectedRootConfigurationSecurityAdapter() = default;
    EspIdfProtectedRootConfigurationSecurityAdapter(
        const EspIdfProtectedRootConfigurationSecurityAdapter&) = delete;
    EspIdfProtectedRootConfigurationSecurityAdapter& operator=(
        const EspIdfProtectedRootConfigurationSecurityAdapter&) = delete;

    ProtectedRootConfigurationSecurityReadResult read_once() noexcept;

private:
    bool attempted_{false};
    bool active_{false};
    bool poisoned_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
