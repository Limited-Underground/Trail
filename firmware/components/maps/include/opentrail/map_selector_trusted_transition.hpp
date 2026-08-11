#pragma once

#include <cstdint>

#include "opentrail/map_selector_transition.hpp"
#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {

enum class MapSelectorTrustedTransitionReason : std::uint8_t {
    none = 0,
    live_state_not_transitionable,
    trusted_source_unavailable,
    trusted_source_changed,
    transition_failed,
    protected_history_retained,
    trusted_advance_failed,
};

struct MapSelectorTrustedTransitionResult {
    MapSelectorTrustedTransitionReason reason{
        MapSelectorTrustedTransitionReason::transition_failed};
    MapSelectorTransitionResult transition{};
    MapSelectorTrustedGenerationResult trusted_before{};
    MapSelectorTrustedGenerationResult trusted_after{};
    MapActivationError containment_error{MapActivationError::none};
    std::uint64_t trusted_generation_before{0};
    std::uint64_t trusted_generation_after{0};
    bool live_guard_updated{false};
    bool map_exposure_allowed{false};
    bool live_guard_mapless{false};
    bool reconciliation_required{false};
    bool trusted_generation_satisfied{false};

    [[nodiscard]] constexpr bool protected_ordering_satisfied() const {
        return reason == MapSelectorTrustedTransitionReason::none &&
               trusted_generation_satisfied;
    }
};

// Runtime composition that derives both transition generation values from the
// protected source. Selector verification/mutation happens on a private guard;
// the target must exclusively own the selector store and protected source for
// each complete call.
class MapSelectorTrustedTransitionCoordinator final {
public:
    MapSelectorTrustedTransitionCoordinator(
        MapSelectorTransitionCoordinator& transition,
        MapSelectorTrustedGeneration& trusted_generation);

    [[nodiscard]] MapSelectorTrustedTransitionResult report_trial_read(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        bool complete_read_succeeded,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorTrustedTransitionResult tick(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorTrustedTransitionResult complete_fallback(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& restored);
    [[nodiscard]] MapSelectorTrustedTransitionResult mark_previous_removed(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        MapSlot slot,
        std::uint64_t generation);

private:
    MapSelectorTransitionCoordinator& transition_;
    MapSelectorTrustedGeneration& trusted_generation_;
};

}  // namespace opentrail::maps
