#pragma once

#include <cstdint>

#include "opentrail/map_selector_candidate.hpp"
#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainCandidateState : std::uint8_t {
    rejected = 0,
    trial_ready,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainCandidateReason : std::uint8_t {
    none = 0,
    live_baseline_required,
    invalid_policy,
    domain_storage_unavailable,
    domain_not_active,
    domain_generation_exhausted,
    protected_source_unavailable,
    protected_source_invalid,
    protected_domain_mismatch,
    protected_generation_mismatch,
    candidate_rejected,
    candidate_failed,
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
// coarse fixed-shape status evidence, not a public log or durable record.
struct MapSelectorDomainCandidateResult {
    MapSelectorDomainCandidateState state{
        MapSelectorDomainCandidateState::rejected};
    MapSelectorDomainCandidateReason reason{
        MapSelectorDomainCandidateReason::candidate_failed};
    MapSelectorCandidateResult candidate{};
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
    std::uint64_t prior_selector_generation{0};
    std::uint64_t trial_selector_generation{0};
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

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorDomainCandidateState::trial_ready &&
               candidate.committed() && selector_persisted &&
               protected_source_verified && domain_generation_saved &&
               domain_generation_verified && selector_verified &&
               live_guard_updated && map_exposure_allowed &&
               !reconciliation_required;
    }
};

// Domain-aware entry from one stable active map into a privately persisted
// trial. The target must exclusively own the live guard, both stores,
// protected source, and both physical package slots for the complete call.
// Trial boot, promotion, fallback, cleanup, and interrupted-call recovery are
// separate boundaries.
class MapSelectorDomainCandidateCoordinator final {
public:
    MapSelectorDomainCandidateCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainCandidateResult activate(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& candidate_package,
        std::uint64_t now_ms);

private:
    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
