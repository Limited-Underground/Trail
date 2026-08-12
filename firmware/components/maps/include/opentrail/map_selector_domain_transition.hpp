#pragma once

#include <cstdint>

#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_transition.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainTransitionState : std::uint8_t {
    rejected = 0,
    applied_volatile,
    committed,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainTransitionReason : std::uint8_t {
    none = 0,
    live_state_not_transitionable,
    invalid_policy,
    domain_storage_unavailable,
    domain_not_active,
    domain_generation_exhausted,
    protected_source_unavailable,
    protected_source_invalid,
    protected_domain_mismatch,
    protected_generation_mismatch,
    transition_rejected,
    transition_failed,
    protected_history_retained,
    transition_generation_invalid,
    domain_changed,
    selector_changed,
    protected_advance_failed,
    protected_readback_failed,
    protected_readback_mismatch,
    domain_save_failed,
    domain_commit_uncertain,
    domain_verification_failed,
    final_domain_changed,
    final_selector_changed,
    final_source_changed,
};

// Domain identifiers and package details are deliberately absent. This is
// fixed-shape lifecycle evidence, not a public log or persistent record.
struct MapSelectorDomainTransitionResult {
    MapSelectorTransitionOperation operation{
        MapSelectorTransitionOperation::tick};
    MapSelectorDomainTransitionState state{
        MapSelectorDomainTransitionState::rejected};
    MapSelectorDomainTransitionReason reason{
        MapSelectorDomainTransitionReason::transition_failed};
    MapSelectorTransitionResult transition{};
    MapSelectorVerifyResult selector_before_protected{};
    MapSelectorVerifyResult selector_after_protected{};
    MapSelectorVerifyResult selector_final{};
    MapSelectorDomainSaveResult domain_save{};
    MapSelectorDomainStoreError domain_store_error{
        MapSelectorDomainStoreError::none};
    MapSelectorDomainSlotState domain_slot_a{
        MapSelectorDomainSlotState::empty};
    MapSelectorDomainSlotState domain_slot_b{
        MapSelectorDomainSlotState::empty};
    MapSelectorDomainProtectedSourceError protected_source_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceState protected_source_state_before{
        MapSelectorDomainProtectedSourceState::unknown};
    MapSelectorDomainProtectedSourceState protected_source_state_after{
        MapSelectorDomainProtectedSourceState::unknown};
    MapActivationError containment_error{MapActivationError::none};
    std::uint64_t selector_generation_before{0};
    std::uint64_t selector_generation_after{0};
    std::uint64_t domain_record_generation{0};
    std::uint64_t domain_epoch{0};
    bool domain_repair_required{false};
    bool selector_repair_required{false};
    bool selector_persisted{false};
    bool protected_advance_called{false};
    bool protected_source_verified{false};
    bool domain_generation_saved{false};
    bool domain_generation_verified{false};
    bool selector_verified{false};
    bool live_guard_updated{false};
    bool map_exposure_allowed{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool completed() const {
        return (state ==
                    MapSelectorDomainTransitionState::applied_volatile ||
                state == MapSelectorDomainTransitionState::committed) &&
               reason == MapSelectorDomainTransitionReason::none &&
               protected_source_verified && domain_generation_verified &&
               selector_verified && live_guard_updated &&
               !reconciliation_required;
    }
};

// Domain-aware runtime lifecycle composition. The target must exclusively own
// the live guard, selector store, domain store, protected source, and physical
// package slots for the complete call. Persistent selector mutation precedes
// protected-source advance, which precedes OTMD accepted-generation advance.
class MapSelectorDomainTransitionCoordinator final {
public:
    MapSelectorDomainTransitionCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainTransitionResult report_trial_read(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        bool complete_read_succeeded,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorDomainTransitionResult tick(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorDomainTransitionResult complete_fallback(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& restored);
    [[nodiscard]] MapSelectorDomainTransitionResult mark_previous_removed(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        MapSlot slot,
        std::uint64_t generation);

private:
    [[nodiscard]] MapSelectorDomainTransitionResult run(
        MapSelectorTransitionOperation operation,
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        bool complete_read_succeeded,
        std::uint64_t now_ms,
        const MapPackageEvidence* restored,
        MapSlot removed_slot,
        std::uint64_t removed_generation);

    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
