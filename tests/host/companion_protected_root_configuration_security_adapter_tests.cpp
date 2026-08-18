#include "companion_protected_root_configuration_security_adapter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "esp_efuse.h"
#include "esp_efuse_table.h"

struct esp_efuse_desc_t {
    int tag;
};

namespace {

using opentrail::target::heltec_v4_bench::
    EspIdfProtectedRootConfigurationSecurityAdapter;
using opentrail::target::heltec_v4_bench::
    ProtectedRootConfigurationSecurityReadResult;
using opentrail::target::heltec_v4_bench::
    ProtectedRootConfigurationSecurityReadStatus;
using opentrail::target::heltec_v4_bench::ProtectedRootNvsProtectionMode;
using opentrail::target::heltec_v4_bench::
    normalize_protected_root_nvs_build_configuration;

int failures = 0;
bool secure_boot = false;
bool flash_encryption = false;
bool secure_download = false;
bool download_disabled = false;
int call_index = 0;
int reenter_at = -1;
EspIdfProtectedRootConfigurationSecurityAdapter* reentry_target = nullptr;
ProtectedRootConfigurationSecurityReadResult nested_reentry{};
std::vector<std::string> calls;

esp_efuse_desc_t secure_download_descriptor{1};
esp_efuse_desc_t download_disabled_descriptor{2};

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void maybe_reenter() {
    ++call_index;
    if (call_index == reenter_at && reentry_target != nullptr) {
        nested_reentry = reentry_target->read_once();
    }
}

void reset_fixture() {
    secure_boot = false;
    flash_encryption = false;
    secure_download = false;
    download_disabled = false;
    call_index = 0;
    reenter_at = -1;
    reentry_target = nullptr;
    nested_reentry = {};
    calls.clear();
}

bool is_default_denial(
    const ProtectedRootConfigurationSecurityReadResult& result) {
    return !result.complete && !result.nvs.observed &&
           !result.security.complete &&
           !result.nvs.configured_hmac_key_slot_known;
}

void test_nvs_build_configuration_normalization() {
    const auto disabled = normalize_protected_root_nvs_build_configuration(
        false, false, false, false, -1);
    expect(disabled.valid && disabled.observed &&
               !disabled.nvs_encryption_enabled &&
               disabled.protection_mode == ProtectedRootNvsProtectionMode::disabled &&
               !disabled.configured_hmac_key_slot_known,
           "disabled NVS configuration must normalize exactly");

    for (int slot = 0; slot < 6; ++slot) {
        const auto hmac = normalize_protected_root_nvs_build_configuration(
            true, true, false, false, slot);
        expect(hmac.valid && hmac.nvs_encryption_enabled &&
                   hmac.protection_mode == ProtectedRootNvsProtectionMode::hmac &&
                   hmac.configured_hmac_key_slot_known &&
                   hmac.configured_hmac_key_slot == static_cast<std::uint8_t>(slot),
               "each valid HMAC key slot must normalize exactly");
    }

    const auto flash = normalize_protected_root_nvs_build_configuration(
        true, false, true, false, -1);
    expect(flash.valid &&
               flash.protection_mode ==
                   ProtectedRootNvsProtectionMode::flash_encryption &&
               !flash.configured_hmac_key_slot_known,
           "flash-encryption protection must remain distinct");

    const auto external = normalize_protected_root_nvs_build_configuration(
        true, false, false, true, -1);
    expect(external.valid &&
               external.protection_mode == ProtectedRootNvsProtectionMode::external &&
               !external.configured_hmac_key_slot_known,
           "external protection must remain distinct");

    struct InvalidCase {
        bool encryption;
        bool hmac;
        bool flash;
        bool external;
        int slot;
    };
    const InvalidCase invalid_cases[] = {
        {false, true, false, false, 0},
        {false, false, true, false, -1},
        {false, false, false, true, -1},
        {false, false, false, false, 0},
        {true, false, false, false, -1},
        {true, true, true, false, 0},
        {true, true, false, true, 0},
        {true, false, true, true, -1},
        {true, true, false, false, -1},
        {true, true, false, false, 6},
        {true, false, true, false, 0},
        {true, false, false, true, 0},
    };
    for (const auto& test : invalid_cases) {
        const auto result = normalize_protected_root_nvs_build_configuration(
            test.encryption, test.hmac, test.flash, test.external, test.slot);
        expect(!result.valid && result.observed,
               "incoherent NVS build configuration must fail closed");
    }
}

void test_all_security_state_combinations() {
    for (unsigned mask = 0U; mask < 16U; ++mask) {
        reset_fixture();
        secure_boot = (mask & 1U) != 0U;
        flash_encryption = (mask & 2U) != 0U;
        secure_download = (mask & 4U) != 0U;
        download_disabled = (mask & 8U) != 0U;

        EspIdfProtectedRootConfigurationSecurityAdapter adapter;
        const auto result = adapter.read_once();
        expect(result.status ==
                   ProtectedRootConfigurationSecurityReadStatus::complete &&
                   result.complete && result.nvs.valid && result.nvs.observed &&
                   result.nvs.protection_mode == ProtectedRootNvsProtectionMode::hmac &&
                   result.nvs.configured_hmac_key_slot_known &&
                   result.nvs.configured_hmac_key_slot == 2U &&
                   result.security.complete,
               "complete normalized snapshot must be published atomically");
        expect(result.security.secure_boot_enabled == secure_boot &&
                   result.security.flash_encryption_enabled == flash_encryption &&
                   result.security.secure_download_enabled == secure_download &&
                   result.security.download_mode_disabled == download_disabled,
               "all security-state combinations must remain factual");
        expect(calls == std::vector<std::string>({
                            "secure_boot", "flash_encryption",
                            "secure_download", "download_disabled"}),
               "security metadata calls must use exact order");
    }
}

void test_reentry_and_all_or_none_publication() {
    for (int position = 1; position <= 4; ++position) {
        reset_fixture();
        EspIdfProtectedRootConfigurationSecurityAdapter adapter;
        reentry_target = &adapter;
        reenter_at = position;

        const auto result = adapter.read_once();
        expect(result.status == ProtectedRootConfigurationSecurityReadStatus::reentry &&
                   is_default_denial(result),
               "reentry must poison and suppress the complete snapshot");
        expect(nested_reentry.status ==
                   ProtectedRootConfigurationSecurityReadStatus::reentry &&
                   is_default_denial(nested_reentry),
               "nested reentry must receive only a default denial");
        expect(call_index == position,
               "reentry must stop before any later metadata call");

        const auto repeated = adapter.read_once();
        expect(repeated.status ==
                   ProtectedRootConfigurationSecurityReadStatus::already_attempted &&
                   is_default_denial(repeated) && call_index == position,
               "a poisoned source must never retry");
    }
}

void test_one_use_and_determinism() {
    reset_fixture();
    EspIdfProtectedRootConfigurationSecurityAdapter adapter;
    const auto first = adapter.read_once();
    const auto calls_after_first = calls.size();
    const auto second = adapter.read_once();
    expect(first.status == ProtectedRootConfigurationSecurityReadStatus::complete &&
               second.status ==
                   ProtectedRootConfigurationSecurityReadStatus::already_attempted &&
               is_default_denial(second) && calls.size() == calls_after_first,
           "a successful source must be one-use without retry");

    for (int iteration = 0; iteration < 100; ++iteration) {
        reset_fixture();
        secure_boot = true;
        download_disabled = true;
        EspIdfProtectedRootConfigurationSecurityAdapter fresh;
        const auto result = fresh.read_once();
        expect(result.complete && result.security.secure_boot_enabled &&
                   !result.security.flash_encryption_enabled &&
                   !result.security.secure_download_enabled &&
                   result.security.download_mode_disabled,
               "fresh sources must normalize deterministically");
    }
}

}  // namespace

const esp_efuse_desc_t* ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD[] = {
    &secure_download_descriptor, nullptr};
const esp_efuse_desc_t* ESP_EFUSE_DIS_DOWNLOAD_MODE[] = {
    &download_disabled_descriptor, nullptr};

bool esp_secure_boot_enabled(void) {
    calls.emplace_back("secure_boot");
    maybe_reenter();
    return secure_boot;
}

bool esp_efuse_is_flash_encryption_enabled(void) {
    calls.emplace_back("flash_encryption");
    maybe_reenter();
    return flash_encryption;
}

bool esp_efuse_read_field_bit(const esp_efuse_desc_t* field[]) {
    if (field == ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD) {
        calls.emplace_back("secure_download");
        maybe_reenter();
        return secure_download;
    }
    if (field == ESP_EFUSE_DIS_DOWNLOAD_MODE) {
        calls.emplace_back("download_disabled");
        maybe_reenter();
        return download_disabled;
    }
    calls.emplace_back("unknown_descriptor");
    maybe_reenter();
    return false;
}

int main() {
    test_nvs_build_configuration_normalization();
    test_all_security_state_combinations();
    test_reentry_and_all_or_none_publication();
    test_one_use_and_determinism();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "protected-root configuration/security adapter tests passed\n";
    return EXIT_SUCCESS;
}
