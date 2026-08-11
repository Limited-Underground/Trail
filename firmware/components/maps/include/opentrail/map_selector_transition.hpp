#pragma once

#include <cstdint>

#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorTransitionOperation : std::uint8_t {
    report_trial_read = 0,
    tick,
    complete_fallback,
    mark_previous_removed,
};

enum class MapSelectorTransitionState : std::uint8_t {
    rejected = 0,
    applied_volatile,
    committed,
    mapless_committed,
    reconciliation_required,
    service_required,
};

enum class MapSelectorTransitionReason : std::uint8_t {
    none = 0,
    live_guard_not_running,
    live_state_not_persistable,
    invalid_policy,
    current_checkpoint_missing,
    selector_invalid,
    storage_unavailable,
    generation_conflict,
    rollback_detected,
    current_generation_mismatch,
    live_checkpoint_mismatch,
    guard_rejected,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    checkpoint_verification_failed,
    generation_exhausted,
    checkpoint_clear_failed,
    checkpoint_clear_verification_failed,
};

struct MapSelectorTransitionContext {
    MapActivationPolicy policy{};
    std::uint64_t current_generation{0};
    std::uint64_t trusted_minimum_generation{0};
};

struct MapSelectorTransitionResult {
    MapSelectorTransitionOperation operation{
        MapSelectorTransitionOperation::tick};
    MapSelectorTransitionState state{
        MapSelectorTransitionState::rejected};
    MapSelectorTransitionReason reason{
        MapSelectorTransitionReason::guard_rejected};
    MapSelectorVerifyResult verification{};
    MapSelectorSaveResult save{};
    MapSelectorStoreError clear_error{MapSelectorStoreError::none};
    MapSelectorInspectionResult clear_inspection{};
    MapActivationError guard_error{MapActivationError::none};
    MapActivationState before_state{MapActivationState::stopped};
    MapActivationState attempted_state{MapActivationState::stopped};
    MapActivationState live_state{MapActivationState::stopped};
    std::uint64_t prior_generation{0};
    std::uint64_t active_generation{0};
    bool persistence_required{false};
    bool checkpoint_cleared{false};
    bool map_exposure_allowed{false};
    bool live_guard_mapless{false};
    bool repair_required{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorTransitionState::committed &&
               save.saved();
    }
};

// Applies lifecycle changes to a private guard copy. The live guard is replaced
// only after exact current-state verification and any required save/clear has
// been verified. This component owns no package, filesystem, renderer, radio,
// or physical-storage operation.
class MapSelectorTransitionCoordinator {
public:
    explicit MapSelectorTransitionCoordinator(MapSelectorStore& store);

    [[nodiscard]] MapSelectorTransitionResult report_trial_read(
        MapActivationGuard& live_guard,
        const MapSelectorTransitionContext& context,
        bool complete_read_succeeded,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorTransitionResult tick(
        MapActivationGuard& live_guard,
        const MapSelectorTransitionContext& context,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorTransitionResult complete_fallback(
        MapActivationGuard& live_guard,
        const MapSelectorTransitionContext& context,
        const MapPackageEvidence& restored);
    [[nodiscard]] MapSelectorTransitionResult mark_previous_removed(
        MapActivationGuard& live_guard,
        const MapSelectorTransitionContext& context,
        MapSlot slot,
        std::uint64_t generation);

private:
    [[nodiscard]] MapSelectorTransitionResult finish(
        MapActivationGuard& live_guard,
        const MapActivationGuard& attempted_guard,
        const MapSelectorTransitionContext& context,
        MapSelectorTransitionOperation operation,
        MapActivationError guard_error,
        const MapActivationStatus& before,
        const MapSelectorVerifyResult& verification);

    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
