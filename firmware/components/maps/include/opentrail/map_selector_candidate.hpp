#pragma once

#include <cstdint>

#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorCandidateState : std::uint8_t {
    rejected = 0,
    committed,
    reconciliation_required,
    service_required,
};

enum class MapSelectorCandidateReason : std::uint8_t {
    none = 0,
    live_guard_not_running,
    stable_baseline_required,
    invalid_policy,
    current_checkpoint_missing,
    selector_invalid,
    storage_unavailable,
    generation_conflict,
    rollback_detected,
    current_generation_mismatch,
    live_checkpoint_mismatch,
    candidate_rejected,
    selector_commit_rejected,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    checkpoint_verification_failed,
    generation_exhausted,
};

struct MapSelectorCandidateContext {
    MapActivationPolicy policy{};
    std::uint64_t current_generation{0};
    std::uint64_t trusted_minimum_generation{0};
};

struct MapSelectorCandidateResult {
    MapSelectorCandidateState state{MapSelectorCandidateState::rejected};
    MapSelectorCandidateReason reason{
        MapSelectorCandidateReason::candidate_rejected};
    MapSelectorVerifyResult verification{};
    MapSelectorSaveResult save{};
    MapActivationError stage_error{MapActivationError::none};
    MapActivationError selector_error{MapActivationError::none};
    MapActivationState before_state{MapActivationState::stopped};
    MapActivationState live_state{MapActivationState::stopped};
    MapSlot candidate_slot{MapSlot::none};
    std::uint64_t candidate_generation{0};
    std::uint64_t prior_record_generation{0};
    std::uint64_t active_record_generation{0};
    bool map_exposure_allowed{false};
    bool live_guard_mapless{false};
    bool repair_required{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorCandidateState::committed &&
               save.saved();
    }
};

// Coordinates replacement of a stable active map with an already staged and
// fully evidenced alternate-slot package. The caller must provide exclusive
// store ownership for activate(). First-map baseline creation remains separate
// because restart-safe trials require a prior-good package.
class MapSelectorCandidateCoordinator {
public:
    explicit MapSelectorCandidateCoordinator(MapSelectorStore& store);

    [[nodiscard]] MapSelectorCandidateResult activate(
        MapActivationGuard& live_guard,
        const MapSelectorCandidateContext& context,
        const MapPackageEvidence& candidate,
        std::uint64_t now_ms);

private:
    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
