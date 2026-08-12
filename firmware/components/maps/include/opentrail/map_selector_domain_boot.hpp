#pragma once

#include <cstdint>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorDomainBootState : std::uint8_t {
    rejected = 0,
    active_ready,
    service_required,
    reconciliation_required,
};

enum class MapSelectorDomainBootReason : std::uint8_t {
    none = 0,
    live_guard_not_clean,
    invalid_policy,
    package_rejected,
    domain_storage_unavailable,
    domain_not_active,
    domain_changed,
    protected_source_unavailable,
    protected_source_invalid,
    protected_domain_mismatch,
    protected_generation_mismatch,
    selector_storage_unavailable,
    selector_restore_failed,
    selector_generation_mismatch,
    selector_not_stable,
    selector_verification_failed,
    final_source_changed,
};

// Domain identifiers and package details are deliberately absent. This is a
// fixed-shape coarse boot result, not a public log or persistent record.
struct MapSelectorDomainBootResult {
    MapSelectorDomainBootState state{MapSelectorDomainBootState::rejected};
    MapSelectorDomainBootReason reason{
        MapSelectorDomainBootReason::domain_not_active};
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
    MapSelectorVerifyResult selector_verify{};
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
    bool domain_repair_required{false};
    bool selector_repair_required{false};
    bool domain_verified{false};
    bool protected_source_verified{false};
    bool selector_restored{false};
    bool selector_verified{false};
    bool live_guard_published{false};
    bool map_exposure_allowed{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool operational() const {
        return state == MapSelectorDomainBootState::active_ready &&
               domain_verified && protected_source_verified &&
               selector_restored && selector_verified &&
               live_guard_published && map_exposure_allowed &&
               !reconciliation_required;
    }
};

// Read-only boot composition for the stable active-domain baseline. The caller
// must exclusively own the live guard, both stores, protected source, and
// physical package-slot state for the complete call. Candidate, trial,
// fallback, cleanup, and any generation mutation are outside this boundary.
class MapSelectorDomainBootCoordinator final {
public:
    MapSelectorDomainBootCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source);

    [[nodiscard]] MapSelectorDomainBootResult boot(
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected_package,
        MapActivationGuard& live_guard);

private:
    MapSelectorDomainStore& domain_store_;
    MapSelectorStore& selector_store_;
    MapSelectorDomainProtectedSource& protected_source_;
};

}  // namespace opentrail::maps
