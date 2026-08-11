#pragma once

#include <cstdint>

#include "opentrail/map_activation_guard.hpp"

namespace opentrail::maps {

inline constexpr std::uint64_t
    kMapSelectorReseedMaximumAuthorizationLifetimeMs = 300'000;

enum class MapSelectorReseedAuthorizationScope : std::uint8_t {
    none = 0,
    selector_reseed,
};

enum class MapSelectorReseedServiceTransport : std::uint8_t {
    unknown = 0,
    local_usb,
    authenticated_local_wireless,
    remote_radio,
};

enum class MapSelectorReseedAuthorizationBackendState : std::uint8_t {
    denied = 0,
    authorized,
    not_ready,
    failed,
};

enum class MapSelectorReseedAuthorizationError : std::uint8_t {
    none = 0,
    invalid_policy,
    invalid_request,
    invalid_binding,
    backend_not_ready,
    backend_failed,
    denied,
    handle_mismatch,
    scope_mismatch,
    transport_not_local,
    boot_session_mismatch,
    binding_mismatch,
    intent_incomplete,
    local_confirmation_missing,
    invalid_time_window,
    not_yet_valid,
    expired,
    lifetime_exceeded,
};

// These five confirmations are reviewed inside the authenticated service
// backend. They remain intent evidence, not credentials or identity proof.
struct MapSelectorReseedAuthorizationEvidence {
    bool explicit_operator_confirmation{false};
    bool map_unavailability_acknowledged{false};
    bool selector_only_scope_confirmed{false};
    bool package_retention_confirmed{false};
    bool trusted_generation_reviewed{false};

    [[nodiscard]] constexpr bool complete() const {
        return explicit_operator_confirmation &&
               map_unavailability_acknowledged &&
               selector_only_scope_confirmed &&
               package_retention_confirmed &&
               trusted_generation_reviewed;
    }
};

struct MapSelectorReseedAuthorizationBinding {
    MapActivationPolicy policy{};
    std::uint64_t trusted_minimum_generation{0};
    MapPackageEvidence baseline{};
};

struct MapSelectorReseedAuthorizationPolicy {
    std::uint64_t maximum_grant_lifetime_ms{0};
};

struct MapSelectorReseedAuthorizationRequest {
    std::uint64_t authorization_handle{0};
    std::uint64_t boot_session_id{0};
    std::uint64_t now_ms{0};
};

// A concrete target verifier owns credentials, administrator policy,
// challenges, audit, and replay state. The common component receives only this
// redacted, exact-operation grant after verify_and_consume().
struct MapSelectorReseedAuthorizationGrant {
    MapSelectorReseedAuthorizationBackendState state{
        MapSelectorReseedAuthorizationBackendState::denied};
    MapSelectorReseedAuthorizationScope scope{
        MapSelectorReseedAuthorizationScope::none};
    MapSelectorReseedServiceTransport transport{
        MapSelectorReseedServiceTransport::unknown};
    std::uint64_t authorization_handle{0};
    std::uint64_t boot_session_id{0};
    std::uint64_t issued_at_ms{0};
    std::uint64_t expires_at_ms{0};
    std::uint32_t local_confirmation_revision{0};
    MapSelectorReseedAuthorizationBinding binding{};
    MapSelectorReseedAuthorizationEvidence acknowledgements{};
};

class MapSelectorReseedAuthorizationBackend {
public:
    virtual ~MapSelectorReseedAuthorizationBackend() = default;

    // An authorized handle must be consumed atomically before returning it as
    // authorized. A replay of that handle must not return authorized again.
    [[nodiscard]] virtual MapSelectorReseedAuthorizationGrant
    verify_and_consume(
        std::uint64_t authorization_handle,
        const MapSelectorReseedAuthorizationBinding& expected_binding) = 0;
};

enum class MapSelectorReseedPermitUse : std::uint8_t {
    none = 0,
    unavailable,
    already_consumed,
    binding_mismatch,
    boot_session_mismatch,
    not_yet_valid,
    expired,
};

class MapSelectorReseedCoordinator;
class MapSelectorReseedAuthorizer;

// A permit is boot-local, non-copyable, exact-operation-bound, and single-use.
// It contains no credential, operator identity, device identifier, or proof.
class MapSelectorReseedPermit {
public:
    MapSelectorReseedPermit() = default;
    MapSelectorReseedPermit(const MapSelectorReseedPermit&) = delete;
    MapSelectorReseedPermit& operator=(const MapSelectorReseedPermit&) = delete;
    MapSelectorReseedPermit(MapSelectorReseedPermit&& other) noexcept;
    MapSelectorReseedPermit& operator=(
        MapSelectorReseedPermit&& other) noexcept;

    [[nodiscard]] constexpr bool available() const {
        return granted_ && !consumed_;
    }

private:
    friend class MapSelectorReseedAuthorizer;
    friend class MapSelectorReseedCoordinator;

    explicit MapSelectorReseedPermit(
        const MapSelectorReseedAuthorizationBinding& binding,
        std::uint64_t boot_session_id,
        std::uint64_t issued_at_ms,
        std::uint64_t expires_at_ms);
    [[nodiscard]] MapSelectorReseedPermitUse consume(
        const MapSelectorReseedAuthorizationBinding& expected_binding,
        std::uint64_t boot_session_id,
        std::uint64_t now_ms);
    void invalidate();

    MapSelectorReseedAuthorizationBinding binding_{};
    std::uint64_t boot_session_id_{0};
    std::uint64_t issued_at_ms_{0};
    std::uint64_t expires_at_ms_{0};
    bool granted_{false};
    bool consumed_{false};
};

struct MapSelectorReseedAuthorizationResult {
    MapSelectorReseedAuthorizationError error{
        MapSelectorReseedAuthorizationError::invalid_request};
    MapSelectorReseedAuthorizationBackendState backend_state{
        MapSelectorReseedAuthorizationBackendState::denied};
    bool backend_called{false};

    [[nodiscard]] constexpr bool authorized() const {
        return error == MapSelectorReseedAuthorizationError::none;
    }
};

class MapSelectorReseedAuthorizer {
public:
    explicit MapSelectorReseedAuthorizer(
        MapSelectorReseedAuthorizationBackend& backend);

    [[nodiscard]] MapSelectorReseedAuthorizationResult authorize(
        const MapSelectorReseedAuthorizationPolicy& policy,
        const MapSelectorReseedAuthorizationRequest& request,
        const MapSelectorReseedAuthorizationBinding& binding,
        MapSelectorReseedPermit& output_permit);

private:
    MapSelectorReseedAuthorizationBackend& backend_;
};

}  // namespace opentrail::maps
