#pragma once

#include <cstdint>

#include "opentrail/companion_authorization_persistence.hpp"

namespace opentrail::target::heltec_v4_bench {

enum class CompanionAuthorizationStorageProbeError : std::uint8_t {
    none = 0,
    nvs_encryption_not_configured,
    protected_nvs_partition_missing,
    nvs_hmac_key_protection_not_configured,
    nvs_hmac_key_id_not_selected,
    nvs_hmac_key_purpose_mismatch,
    nvs_hmac_key_not_read_protected,
    nvs_hmac_key_unusable,
};

struct CompanionAuthorizationStorageProbeConfiguration {
    bool nvs_encryption_configured{false};
    bool nvs_hmac_key_protection_configured{false};
    int nvs_hmac_key_id{-1};
};

struct CompanionAuthorizationStorageProbeSnapshot {
    CompanionAuthorizationStorageProbeError error{
        CompanionAuthorizationStorageProbeError::
            nvs_encryption_not_configured};
    bool protected_nvs_partition_present{false};
    bool nvs_hmac_key_id_selected{false};
    bool nvs_hmac_key_purpose_verified{false};
    bool nvs_hmac_key_read_protected{false};
    bool nvs_hmac_key_operational{false};

    [[nodiscard]] constexpr bool accepted() const {
        return error == CompanionAuthorizationStorageProbeError::none;
    }
};

// Read-only target seam. Implementations may inspect only coarse partition and
// key-admission facts. They must not initialize or open NVS, expose key output,
// program eFuses, or mutate persistent state.
class CompanionAuthorizationStorageReadOnlyProbePort {
public:
    virtual ~CompanionAuthorizationStorageReadOnlyProbePort() = default;

    [[nodiscard]] virtual bool protected_nvs_partition_present() = 0;
    [[nodiscard]] virtual bool hmac_key_has_expected_purpose(
        std::uint8_t key_id) = 0;
    [[nodiscard]] virtual bool hmac_key_is_read_protected(
        std::uint8_t key_id) = 0;
    [[nodiscard]] virtual bool hmac_key_is_operational(
        std::uint8_t key_id) = 0;
};

[[nodiscard]] CompanionAuthorizationStorageProbeSnapshot
probe_companion_authorization_storage(
    const CompanionAuthorizationStorageProbeConfiguration& configuration,
    CompanionAuthorizationStorageReadOnlyProbePort& port);

// Runtime probe for the pinned target configuration. It performs no NVS
// initialization/open/write and no eFuse programming.
[[nodiscard]] CompanionAuthorizationStorageProbeSnapshot
companion_authorization_storage_probe();

[[nodiscard]] opentrail::companion::
    CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence();

[[nodiscard]] opentrail::companion::
    CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence(
    const CompanionAuthorizationStorageProbeConfiguration& configuration,
    CompanionAuthorizationStorageReadOnlyProbePort& port);

[[nodiscard]] opentrail::companion::
    CompanionAuthorizationTargetAdmissionError
companion_authorization_storage_preflight();

// Deterministic in-memory reboot check plus exact closed-target preflight. It
// starts no controller, service, advertiser, NVS backend, or peripheral.
[[nodiscard]] bool run_companion_authorization_storage_self_check();

}  // namespace opentrail::target::heltec_v4_bench
