#pragma once

#include <cstdint>
#include <optional>

#include "companion_authorization_nvs_backend.hpp"

namespace opentrail::target::heltec_v4_bench {

enum class CompanionAuthorizationNvsContextError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    uncertain,
};

struct CompanionAuthorizationNvsContextSnapshot {
    CompanionAuthorizationNvsContextError error{
        CompanionAuthorizationNvsContextError::not_ready};
    bool opened{false};
    bool faulted{false};
};

// Inactive owner for one exact, already provisioned encrypted NVS context.
// It can consume existing security configuration only; it never generates,
// provisions, erases, repairs, retries, or selects an eFuse key.
class EspIdfCompanionAuthorizationNvsContext final {
public:
    EspIdfCompanionAuthorizationNvsContext() = default;
    ~EspIdfCompanionAuthorizationNvsContext();
    EspIdfCompanionAuthorizationNvsContext(
        const EspIdfCompanionAuthorizationNvsContext&) = delete;
    EspIdfCompanionAuthorizationNvsContext& operator=(
        const EspIdfCompanionAuthorizationNvsContext&) = delete;

    [[nodiscard]] CompanionAuthorizationNvsContextSnapshot open_existing();
    [[nodiscard]] CompanionAuthorizationNvsContextSnapshot close();

    [[nodiscard]] EspIdfCompanionAuthorizationNvsBackend* backend();
    [[nodiscard]] CompanionAuthorizationNvsContextSnapshot snapshot() const;

private:
    CompanionAuthorizationNvsContextSnapshot fail_uncertain();
    void observe_reentry();

    bool operation_active_{false};
    bool reentry_observed_{false};
    bool attempted_{false};
    bool partition_initialized_{false};
    bool opened_{false};
    bool faulted_{false};
    nvs_handle_t handle_{0};
    std::optional<EspIdfCompanionAuthorizationNvsBackend> backend_{};
};

}  // namespace opentrail::target::heltec_v4_bench
