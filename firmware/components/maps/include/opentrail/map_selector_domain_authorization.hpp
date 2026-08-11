#pragma once

#include <cstdint>

#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_reset_policy.hpp"

namespace opentrail::maps {

inline constexpr std::uint64_t
    kMapSelectorDomainMaximumAuthorizationLifetimeMs = 300'000;

enum class MapSelectorDomainAuthorizationScope : std::uint8_t {
    none = 0,
    replace_same_device_domain,
    commission_new_device_domain,
};

enum class MapSelectorDomainServiceTransport : std::uint8_t {
    unknown = 0,
    local_usb,
    authenticated_local_wireless,
    remote_radio,
};

enum class MapSelectorDomainMediaState : std::uint8_t {
    unknown = 0,
    verified_empty,
    retained_quarantined,
};

enum class MapSelectorDomainAuthorizationBackendState : std::uint8_t {
    denied = 0,
    authorized,
    not_ready,
    failed,
};

enum class MapSelectorDomainAuthorizationError : std::uint8_t {
    none = 0,
    invalid_policy,
    invalid_request,
    invalid_binding,
    invalid_route,
    backend_not_ready,
    backend_failed,
    denied,
    handle_mismatch,
    scope_mismatch,
    transport_not_local_usb,
    boot_session_mismatch,
    binding_mismatch,
    intent_incomplete,
    local_confirmation_missing,
    invalid_time_window,
    not_yet_valid,
    expired,
    lifetime_exceeded,
};

// These confirmations are reviewed by the target-owned authenticated service
// backend. They are intent evidence, not credentials or physical proof.
struct MapSelectorDomainAuthorizationEvidence {
    bool explicit_operator_confirmation{false};
    bool physical_access_confirmed{false};
    bool map_unavailability_acknowledged{false};
    bool protected_history_retirement_acknowledged{false};
    bool retained_selector_import_forbidden{false};
    bool fresh_domain_confirmed{false};

    [[nodiscard]] constexpr bool complete() const {
        return explicit_operator_confirmation && physical_access_confirmed &&
               map_unavailability_acknowledged &&
               protected_history_retirement_acknowledged &&
               retained_selector_import_forbidden && fresh_domain_confirmed;
    }
};

struct MapSelectorDomainAuthorizationBinding {
    MapSelectorResetRequest request{MapSelectorResetRequest::unknown};
    MapSelectorLifecycleState lifecycle_state{
        MapSelectorLifecycleState::unknown};
    MapSelectorDomainMediaState media_state{
        MapSelectorDomainMediaState::unknown};
    std::uint64_t reviewed_selector_generation{0};
    MapSelectorDomainId retired_domain{};
    MapSelectorDomainId proposed_domain{};
};

struct MapSelectorDomainAuthorizationPolicy {
    std::uint64_t maximum_grant_lifetime_ms{0};
};

struct MapSelectorDomainAuthorizationRequest {
    std::uint64_t authorization_handle{0};
    std::uint64_t boot_session_id{0};
    std::uint64_t now_ms{0};
};

// The concrete backend owns credentials, administrator policy, physical-
// presence verification, challenges, audit, and durable replay state.
struct MapSelectorDomainAuthorizationGrant {
    MapSelectorDomainAuthorizationBackendState state{
        MapSelectorDomainAuthorizationBackendState::denied};
    MapSelectorDomainAuthorizationScope scope{
        MapSelectorDomainAuthorizationScope::none};
    MapSelectorDomainServiceTransport transport{
        MapSelectorDomainServiceTransport::unknown};
    std::uint64_t authorization_handle{0};
    std::uint64_t boot_session_id{0};
    std::uint64_t issued_at_ms{0};
    std::uint64_t expires_at_ms{0};
    std::uint32_t local_confirmation_revision{0};
    MapSelectorDomainAuthorizationBinding binding{};
    MapSelectorDomainAuthorizationEvidence acknowledgements{};
};

class MapSelectorDomainAuthorizationBackend {
public:
    virtual ~MapSelectorDomainAuthorizationBackend() = default;

    // An authorized handle must be atomically consumed before it is returned
    // as authorized. A replay must not be authorized again.
    [[nodiscard]] virtual MapSelectorDomainAuthorizationGrant
    verify_and_consume(
        std::uint64_t authorization_handle,
        const MapSelectorDomainAuthorizationBinding& expected_binding) = 0;
};

class MapSelectorDomainAuthorizer;
class MapSelectorDomainProvisioner;

enum class MapSelectorDomainPermitUse : std::uint8_t {
    none = 0,
    unavailable,
    already_consumed,
    binding_mismatch,
    boot_session_mismatch,
    not_yet_valid,
    expired,
};

// Boot-local, non-copyable authority for one domain-provisioning operation.
// Only MapSelectorDomainProvisioner can consume it after authorization.
class MapSelectorDomainAuthorizationPermit {
public:
    MapSelectorDomainAuthorizationPermit() = default;
    MapSelectorDomainAuthorizationPermit(
        const MapSelectorDomainAuthorizationPermit&) = delete;
    MapSelectorDomainAuthorizationPermit& operator=(
        const MapSelectorDomainAuthorizationPermit&) = delete;
    MapSelectorDomainAuthorizationPermit(
        MapSelectorDomainAuthorizationPermit&& other) noexcept;
    MapSelectorDomainAuthorizationPermit& operator=(
        MapSelectorDomainAuthorizationPermit&& other) noexcept;

    [[nodiscard]] constexpr bool available() const {
        return granted_ && !consumed_;
    }

private:
    friend class MapSelectorDomainAuthorizer;
    friend class MapSelectorDomainProvisioner;

    explicit MapSelectorDomainAuthorizationPermit(
        MapSelectorDomainAuthorizationScope scope,
        const MapSelectorDomainAuthorizationBinding& binding,
        std::uint64_t boot_session_id,
        std::uint64_t issued_at_ms,
        std::uint64_t expires_at_ms);
    [[nodiscard]] MapSelectorDomainPermitUse consume(
        const MapSelectorDomainAuthorizationBinding& expected_binding,
        std::uint64_t boot_session_id,
        std::uint64_t now_ms);
    void invalidate();

    MapSelectorDomainAuthorizationScope scope_{
        MapSelectorDomainAuthorizationScope::none};
    MapSelectorDomainAuthorizationBinding binding_{};
    std::uint64_t boot_session_id_{0};
    std::uint64_t issued_at_ms_{0};
    std::uint64_t expires_at_ms_{0};
    bool granted_{false};
    bool consumed_{false};
};

struct MapSelectorDomainAuthorizationResult {
    MapSelectorDomainAuthorizationError error{
        MapSelectorDomainAuthorizationError::invalid_request};
    MapSelectorDomainAuthorizationBackendState backend_state{
        MapSelectorDomainAuthorizationBackendState::denied};
    MapSelectorDomainAuthorizationScope required_scope{
        MapSelectorDomainAuthorizationScope::none};
    bool backend_called{false};

    [[nodiscard]] constexpr bool authorized() const {
        return error == MapSelectorDomainAuthorizationError::none;
    }
};

class MapSelectorDomainAuthorizer final {
public:
    explicit MapSelectorDomainAuthorizer(
        MapSelectorDomainAuthorizationBackend& backend);

    [[nodiscard]] MapSelectorDomainAuthorizationResult authorize(
        const MapSelectorDomainAuthorizationPolicy& policy,
        const MapSelectorDomainAuthorizationRequest& request,
        const MapSelectorDomainAuthorizationBinding& binding,
        MapSelectorDomainAuthorizationPermit& output_permit);

private:
    MapSelectorDomainAuthorizationBackend& backend_;
};

}  // namespace opentrail::maps
