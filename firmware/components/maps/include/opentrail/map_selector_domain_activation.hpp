#pragma once

#include <cstdint>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainActivationState : std::uint8_t {
    rejected = 0,
    activated,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainActivationReason : std::uint8_t {
    none = 0,
    map_unavailable_required,
    invalid_policy,
    candidate_rejected,
    domain_storage_unavailable,
    domain_state_mismatch,
    domain_generation_exhausted,
    selector_storage_unavailable,
    selector_state_mismatch,
    selector_generation_exhausted,
    selector_restore_failed,
    selector_save_failed,
    selector_commit_uncertain,
    selector_verification_failed,
    selector_changed,
    protected_source_unavailable,
    protected_source_invalid,
    protected_source_mismatch,
    protected_advance_failed,
    protected_readback_failed,
    protected_readback_mismatch,
    domain_changed,
    domain_save_failed,
    domain_commit_uncertain,
    domain_verification_failed,
    final_source_changed,
};

// Domain identifiers are deliberately absent. This is coarse status plumbing,
// not a public log or durable record.
struct MapSelectorDomainActivationResult {
    MapSelectorDomainActivationState state{
        MapSelectorDomainActivationState::rejected};
    MapSelectorDomainActivationReason reason{
        MapSelectorDomainActivationReason::domain_state_mismatch};
    MapSelectorDomainStoreError domain_store_error{
        MapSelectorDomainStoreError::none};
    MapSelectorDomainSlotState domain_slot_a{
        MapSelectorDomainSlotState::empty};
    MapSelectorDomainSlotState domain_slot_b{
        MapSelectorDomainSlotState::empty};
    MapSelectorStoreError selector_store_error{MapSelectorStoreError::none};
    MapSelectorSlotState selector_slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState selector_slot_b{MapSelectorSlotState::empty};
    MapSelectorLoadResult selector_load{};
    MapSelectorSaveResult selector_save{};
    MapSelectorVerifyResult selector_verify{};
    MapSelectorDomainSaveResult domain_save{};
    MapSelectorDomainProtectedSourceError protected_source_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceState protected_source_state_before{
        MapSelectorDomainProtectedSourceState::unknown};
    MapSelectorDomainProtectedSourceState protected_source_state_after{
        MapSelectorDomainProtectedSourceState::unknown};
    MapActivationError candidate_error{MapActivationError::none};
    MapActivationError containment_error{MapActivationError::none};
    std::uint64_t selector_generation{0};
    std::uint64_t domain_record_generation{0};
    std::uint64_t domain_epoch{0};
    std::uint64_t retired_selector_floor{0};
    bool resumed_active_domain{false};
    bool resumed_selector{false};
    bool selector_persisted{false};
    bool selector_verified{false};
    bool protected_advance_called{false};
    bool protected_source_verified{false};
    bool active_domain_saved{false};
    bool active_domain_verified{false};
    bool live_guard_updated{false};
    bool map_exposure_allowed{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool activated() const {
        return state == MapSelectorDomainActivationState::activated &&
               selector_persisted && selector_verified &&
               protected_source_verified && active_domain_verified &&
               live_guard_updated && map_exposure_allowed;
    }
};

// Completes an already authorized OTMD pending lifecycle. The caller must own
// the live guard, both stores, the protected source, and physical package-slot
// state exclusively for the complete call. The fixed durable order is selector
// save, exact domain-bound protected-generation advance, active OTMD save, then
// live map publication. Matching completed steps can be resumed after restart.
class MapSelectorDomainActivationCoordinator final {
public:
    MapSelectorDomainActivationCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainActivationResult activate(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& baseline_package);

private:
    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
