#pragma once

#include <cstdint>

#include "opentrail/map_selector_boot.hpp"
#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainTrialBootState : std::uint8_t {
    rejected = 0,
    active_ready,
    trial_ready,
    fallback_required,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainTrialBootReason : std::uint8_t {
    none = 0,
    live_guard_not_clean,
    invalid_policy,
    domain_storage_unavailable,
    domain_not_active,
    domain_generation_exhausted,
    protected_source_unavailable,
    protected_source_invalid,
    protected_domain_mismatch,
    protected_generation_mismatch,
    selector_storage_unavailable,
    selector_state_mismatch,
    selector_generation_gap,
    selector_boot_failed,
    selector_boot_unexpected_state,
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
// fixed-shape recovery status, not a public log or persistent record.
struct MapSelectorDomainTrialBootResult {
    MapSelectorDomainTrialBootState state{
        MapSelectorDomainTrialBootState::rejected};
    MapSelectorDomainTrialBootReason reason{
        MapSelectorDomainTrialBootReason::selector_boot_failed};
    MapSelectorBootResult selector_boot{};
    MapSelectorInspectionResult selector_inspection{};
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
    std::uint64_t accepted_generation_before{0};
    std::uint64_t protected_generation_before{0};
    std::uint64_t selector_generation_before{0};
    std::uint64_t selector_generation_after{0};
    std::uint64_t domain_record_generation{0};
    std::uint64_t domain_epoch{0};
    bool domain_repair_required{false};
    bool selector_repair_required{false};
    bool selector_booted{false};
    bool protected_advance_called{false};
    bool protected_source_verified{false};
    bool domain_generation_saved{false};
    bool domain_generation_verified{false};
    bool selector_verified{false};
    bool live_guard_published{false};
    bool map_exposure_allowed{false};
    bool fallback_required{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool completed() const {
        return (state == MapSelectorDomainTrialBootState::active_ready ||
                state == MapSelectorDomainTrialBootState::trial_ready ||
                state ==
                    MapSelectorDomainTrialBootState::fallback_required) &&
               selector_booted && protected_source_verified &&
               domain_generation_verified && selector_verified &&
               live_guard_published && !reconciliation_required;
    }
};

// Restart-safe composition for an active domain whose selector contains a
// trial, fallback-required, or active runtime-transition checkpoint. It
// accepts only the committed baseline or the single-generation gaps that
// candidate entry, this boot, or a domain-aware runtime transition can leave
// after interruption. Selector persistence precedes protected-source advance,
// which precedes the OTMD accepted-generation advance. Nothing is published
// until all three boundaries reconcile exactly.
class MapSelectorDomainTrialBootCoordinator final {
public:
    MapSelectorDomainTrialBootCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainTrialBootResult boot(
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected_package,
        const MapPackageEvidence& previous_package,
        std::uint64_t now_ms,
        MapActivationGuard& live_guard);

private:
    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
