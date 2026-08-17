#include "companion_authorization_nvs_context.hpp"

#include <cstddef>
#include <cstdint>

#include "esp_partition.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr char kAuthorizationPartition[] = "ot_auth";
constexpr char kAuthorizationNamespace[] = "ot_owner";
constexpr std::uint32_t kAuthorizationOffset = 0x00F00000U;
constexpr std::uint32_t kAuthorizationSize = 0x00010000U;

void secure_zero(void* data, std::size_t size) {
    auto* current = static_cast<volatile std::uint8_t*>(data);
    while (size > 0) {
        *current = 0;
        ++current;
        --size;
    }
}

constexpr bool exact_security_configuration_selected() {
#if defined(CONFIG_NVS_ENCRYPTION) && CONFIG_NVS_ENCRYPTION && \
    defined(CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC) && \
    CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC && \
    defined(CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID) && \
    CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID >= 0 && \
    CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID <= 5
    return true;
#else
    return false;
#endif
}

class ScopedOperation final {
public:
    explicit ScopedOperation(bool& active) : active_(active) {
        active_ = true;
    }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

}  // namespace

EspIdfCompanionAuthorizationNvsContext::
    ~EspIdfCompanionAuthorizationNvsContext() {
    static_cast<void>(close());
}

void EspIdfCompanionAuthorizationNvsContext::observe_reentry() {
    reentry_observed_ = true;
}

CompanionAuthorizationNvsContextSnapshot
EspIdfCompanionAuthorizationNvsContext::snapshot() const {
    return {faulted_ ? CompanionAuthorizationNvsContextError::uncertain
                     : opened_
                           ? CompanionAuthorizationNvsContextError::none
                           : CompanionAuthorizationNvsContextError::not_ready,
            opened_ && !faulted_, faulted_};
}

CompanionAuthorizationNvsContextSnapshot
EspIdfCompanionAuthorizationNvsContext::fail_uncertain() {
    if (opened_) {
        backend_.reset();
        nvs_close(handle_);
        handle_ = 0;
        opened_ = false;
    }
    if (partition_initialized_) {
        static_cast<void>(nvs_flash_deinit_partition(kAuthorizationPartition));
        partition_initialized_ = false;
    }
    faulted_ = true;
    return {CompanionAuthorizationNvsContextError::uncertain, false, true};
}

CompanionAuthorizationNvsContextSnapshot
EspIdfCompanionAuthorizationNvsContext::open_existing() {
    if (operation_active_) {
        observe_reentry();
        return {CompanionAuthorizationNvsContextError::uncertain, false, true};
    }
    ScopedOperation operation(operation_active_);
    if (attempted_ || opened_ || faulted_) {
        return {faulted_ ? CompanionAuthorizationNvsContextError::uncertain
                         : CompanionAuthorizationNvsContextError::failed,
                opened_ && !faulted_, faulted_};
    }
    attempted_ = true;
    if (!exact_security_configuration_selected()) {
        return {CompanionAuthorizationNvsContextError::not_ready, false, false};
    }

    const auto* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
        kAuthorizationPartition);
    if (reentry_observed_) {
        return fail_uncertain();
    }
    if (partition == nullptr || !partition->encrypted || partition->readonly ||
        partition->address != kAuthorizationOffset ||
        partition->size != kAuthorizationSize) {
        return {CompanionAuthorizationNvsContextError::not_ready, false, false};
    }

    auto* scheme = nvs_flash_get_default_security_scheme();
    if (scheme == nullptr) {
        return {CompanionAuthorizationNvsContextError::not_ready, false, false};
    }
    nvs_sec_cfg_t security_configuration{};
    const auto read_security =
        nvs_flash_read_security_cfg_v2(scheme, &security_configuration);
    if (reentry_observed_) {
        secure_zero(&security_configuration, sizeof(security_configuration));
        return fail_uncertain();
    }
    if (read_security != ESP_OK) {
        secure_zero(&security_configuration, sizeof(security_configuration));
        return {CompanionAuthorizationNvsContextError::not_ready, false, false};
    }

    // Invoking secure initialization creates a cleanup obligation even when
    // ESP-IDF reports failure; the call may have acquired partial ownership.
    partition_initialized_ = true;
    const auto initialized = nvs_flash_secure_init_partition(
        kAuthorizationPartition, &security_configuration);
    secure_zero(&security_configuration, sizeof(security_configuration));
    if (reentry_observed_ || initialized != ESP_OK) {
        // Once secure initialization is invoked, failure cannot prove that no
        // target state or framework ownership changed.
        return fail_uncertain();
    }

    nvs_handle_t opened_handle = 0;
    const auto opened = nvs_open_from_partition(
        kAuthorizationPartition, kAuthorizationNamespace, NVS_READWRITE,
        &opened_handle);
    // A failing native call that nevertheless publishes a handle is
    // ambiguous. Retain it only long enough for fail_uncertain() to close it.
    handle_ = opened_handle;
    opened_ = opened_handle != 0;
    if (reentry_observed_ || opened != ESP_OK || opened_handle == 0) {
        return fail_uncertain();
    }

    backend_.emplace(handle_);
    return {CompanionAuthorizationNvsContextError::none, true, false};
}

CompanionAuthorizationNvsContextSnapshot
EspIdfCompanionAuthorizationNvsContext::close() {
    if (operation_active_) {
        observe_reentry();
        return {CompanionAuthorizationNvsContextError::uncertain, false, true};
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return {CompanionAuthorizationNvsContextError::uncertain, false, true};
    }
    if (!opened_) {
        return {CompanionAuthorizationNvsContextError::none, false, false};
    }

    backend_.reset();
    nvs_close(handle_);
    handle_ = 0;
    opened_ = false;
    const auto deinitialized =
        nvs_flash_deinit_partition(kAuthorizationPartition);
    partition_initialized_ = false;
    if (reentry_observed_ || deinitialized != ESP_OK) {
        faulted_ = true;
        return {CompanionAuthorizationNvsContextError::uncertain, false, true};
    }
    return {CompanionAuthorizationNvsContextError::none, false, false};
}

EspIdfCompanionAuthorizationNvsBackend*
EspIdfCompanionAuthorizationNvsContext::backend() {
    return opened_ && !faulted_ ? &backend_.value() : nullptr;
}

}  // namespace opentrail::target::heltec_v4_bench
