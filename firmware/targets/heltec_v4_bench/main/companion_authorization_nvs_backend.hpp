#pragma once

#include "nvs.h"
#include "opentrail/companion_authorization_protected_kv_media.hpp"

namespace opentrail::target::heltec_v4_bench {

// ESP-IDF NVS binding for the exact OT-067 key/value backend. The handle must
// already belong to an admitted, securely initialized `ot_auth` / `ot_owner`
// context. This adapter never opens or initializes NVS and never owns, closes,
// erases, resets, retries, provisions, or logs the supplied handle or data.
class EspIdfCompanionAuthorizationNvsBackend final
    : public companion::CompanionAuthorizationProtectedKvBackend {
public:
    explicit EspIdfCompanionAuthorizationNvsBackend(nvs_handle_t handle);

    [[nodiscard]] companion::CompanionAuthorizationProtectedKvBackendError
    read_blob(const char* partition_label,
              const char* namespace_name,
              const char* key,
              std::uint8_t* output,
              std::size_t capacity,
              std::size_t& actual_size) override;

    [[nodiscard]] companion::CompanionAuthorizationProtectedKvBackendError
    write_blob(const char* partition_label,
               const char* namespace_name,
               const char* key,
               const std::uint8_t* data,
               std::size_t size) override;

    [[nodiscard]] companion::CompanionAuthorizationProtectedKvBackendError
    commit(const char* partition_label,
           const char* namespace_name) override;

private:
    nvs_handle_t handle_{0};
};

}  // namespace opentrail::target::heltec_v4_bench
