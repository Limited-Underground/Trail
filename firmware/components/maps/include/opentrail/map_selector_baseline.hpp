#pragma once

#include <cstdint>

#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorBaselineState : std::uint8_t {
    rejected = 0,
    committed,
    reconciliation_required,
    service_required,
};

enum class MapSelectorBaselineReason : std::uint8_t {
    none = 0,
    live_guard_not_running,
    clean_mapless_guard_required,
    invalid_policy,
    candidate_rejected,
    trusted_history_present,
    selector_not_empty,
    selector_invalid,
    storage_unavailable,
    generation_conflict,
    selector_changed,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    checkpoint_verification_failed,
};

struct MapSelectorBaselineContext {
    MapActivationPolicy policy{};
    std::uint64_t trusted_minimum_generation{0};
};

struct MapSelectorBaselineResult {
    MapSelectorBaselineState state{MapSelectorBaselineState::rejected};
    MapSelectorBaselineReason reason{
        MapSelectorBaselineReason::candidate_rejected};
    MapSelectorInspectionResult inspection{};
    MapSelectorSaveResult save{};
    MapActivationError candidate_error{MapActivationError::none};
    MapActivationState before_state{MapActivationState::stopped};
    MapActivationState live_state{MapActivationState::stopped};
    MapSlot baseline_slot{MapSlot::none};
    std::uint64_t baseline_package_generation{0};
    std::uint64_t active_record_generation{0};
    bool map_exposure_allowed{false};
    bool live_guard_mapless{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorBaselineState::committed && save.saved();
    }
};

// Establishes the first stable map only from a clean no-selector guard, empty
// selector store, zero trusted history, and fully evidenced package. The caller
// must provide explicit provisioning authority and exclusive store ownership.
// This boundary does not reset, reseed, authenticate, or stage package bytes.
class MapSelectorBaselineCoordinator {
public:
    explicit MapSelectorBaselineCoordinator(MapSelectorStore& store);

    [[nodiscard]] MapSelectorBaselineResult establish(
        MapActivationGuard& live_guard,
        const MapSelectorBaselineContext& context,
        const MapPackageEvidence& baseline);

private:
    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
