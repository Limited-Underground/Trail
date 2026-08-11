#pragma once

#include <cstdint>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_domain_authorization.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainProtectedSourceState : std::uint8_t {
    unknown = 0,
    uninitialized,
    ready,
};

enum class MapSelectorDomainProtectedSourceError : std::uint8_t {
    none = 0,
    not_ready,
    io_failure,
    invalid_state,
    rejected,
    conflict,
};

struct MapSelectorDomainProtectedSourceRead {
    MapSelectorDomainProtectedSourceError error{
        MapSelectorDomainProtectedSourceError::io_failure};
    MapSelectorDomainProtectedSourceState state{
        MapSelectorDomainProtectedSourceState::unknown};
    MapSelectorDomainId domain{};
    std::uint64_t selector_generation{0};
};

struct MapSelectorDomainProtectedEstablishRequest {
    MapSelectorDomainAuthorizationScope scope{
        MapSelectorDomainAuthorizationScope::none};
    MapSelectorDomainId retired_domain{};
    MapSelectorDomainId proposed_domain{};
};

struct MapSelectorDomainProtectedAdvanceRequest {
    MapSelectorDomainId expected_domain{};
    std::uint64_t expected_selector_generation{0};
    std::uint64_t proposed_selector_generation{0};
};

// A target implementation may establish only an independently uninitialized
// source. It must not erase, reset, or rebind an initialized source through
// this interface. Generation advance must atomically compare the exact domain
// and generation before increasing it. Any reported mutation failure is
// commit-uncertain.
class MapSelectorDomainProtectedSource {
public:
    virtual ~MapSelectorDomainProtectedSource() = default;

    [[nodiscard]] virtual MapSelectorDomainProtectedSourceRead read() = 0;
    [[nodiscard]] virtual MapSelectorDomainProtectedSourceError
    establish_fresh_domain(
        const MapSelectorDomainProtectedEstablishRequest& request) = 0;
    [[nodiscard]] virtual MapSelectorDomainProtectedSourceError
    advance_selector_generation(
        const MapSelectorDomainProtectedAdvanceRequest& request) = 0;
};

enum class MapSelectorDomainProvisionState : std::uint8_t {
    rejected = 0,
    prepared,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainProvisionReason : std::uint8_t {
    none = 0,
    authorization_required,
    authorization_already_consumed,
    authorization_binding_mismatch,
    authorization_boot_session_mismatch,
    authorization_not_yet_valid,
    authorization_expired,
    authorization_scope_invalid,
    map_unavailable_required,
    protected_source_unavailable,
    protected_source_invalid,
    protected_source_conflict,
    domain_storage_unavailable,
    domain_state_mismatch,
    domain_generation_exhausted,
    domain_changed,
    domain_save_failed,
    domain_commit_uncertain,
    domain_verification_failed,
    selector_storage_unavailable,
    selector_state_mismatch,
    selector_changed,
    selector_clear_failed,
    selector_clear_unverified,
    protected_establish_failed,
    protected_readback_failed,
    protected_readback_mismatch,
};

struct MapSelectorDomainProvisionContext {
    std::uint64_t boot_session_id{0};
    std::uint64_t authorization_use_time_ms{0};
};

// This result intentionally omits current, retired, and proposed domain bytes.
// It is suitable for coarse status plumbing but is not a public log format.
struct MapSelectorDomainProvisionResult {
    MapSelectorDomainProvisionState state{
        MapSelectorDomainProvisionState::rejected};
    MapSelectorDomainProvisionReason reason{
        MapSelectorDomainProvisionReason::authorization_required};
    MapSelectorDomainAuthorizationScope scope{
        MapSelectorDomainAuthorizationScope::none};
    MapSelectorDomainStoreError domain_store_error{
        MapSelectorDomainStoreError::none};
    MapSelectorDomainSlotState domain_slot_a{
        MapSelectorDomainSlotState::empty};
    MapSelectorDomainSlotState domain_slot_b{
        MapSelectorDomainSlotState::empty};
    MapSelectorStoreError selector_store_error{MapSelectorStoreError::none};
    MapSelectorSlotState selector_slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState selector_slot_b{MapSelectorSlotState::empty};
    MapSelectorResetResult selector_reset{};
    MapSelectorDomainProtectedSourceError protected_source_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceState protected_source_state_before{
        MapSelectorDomainProtectedSourceState::unknown};
    MapSelectorDomainProtectedSourceState protected_source_state_after{
        MapSelectorDomainProtectedSourceState::unknown};
    std::uint64_t observed_selector_generation{0};
    std::uint64_t domain_record_generation{0};
    std::uint64_t domain_epoch{0};
    std::uint64_t retired_selector_floor{0};
    bool authorization_consumed{false};
    bool map_exposure_allowed{false};
    bool pending_record_persisted{false};
    bool pending_commit_uncertain{false};
    bool resumed_pending_record{false};
    bool selector_clear_attempted{false};
    bool selector_empty_verified{false};
    bool protected_source_called{false};
    bool protected_source_verified{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool prepared() const {
        return state == MapSelectorDomainProvisionState::prepared &&
               pending_record_persisted && selector_empty_verified &&
               protected_source_verified && !map_exposure_allowed;
    }
};

// The caller must hold exclusive ownership of the live map guard and all three
// injected persistence boundaries for the entire call. The permit is consumed
// before any I/O. A pending OTMD record is durably verified before selector
// clear or protected-source establishment.
class MapSelectorDomainProvisioner final {
public:
    MapSelectorDomainProvisioner(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainProvisionResult provision(
        MapActivationGuard& live_guard,
        const MapSelectorDomainProvisionContext& context,
        const MapSelectorDomainAuthorizationBinding& binding,
        MapSelectorDomainAuthorizationPermit& permit);

private:
    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
