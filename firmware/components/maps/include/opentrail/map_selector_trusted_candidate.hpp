#pragma once

#include <cstdint>

#include "opentrail/map_selector_candidate.hpp"
#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {

enum class MapSelectorTrustedCandidateReason : std::uint8_t {
    none = 0,
    live_state_not_candidate_baseline,
    trusted_source_unavailable,
    trusted_source_changed,
    candidate_failed,
    trusted_advance_failed,
};

struct MapSelectorTrustedCandidateResult {
    MapSelectorTrustedCandidateReason reason{
        MapSelectorTrustedCandidateReason::candidate_failed};
    MapSelectorCandidateResult candidate{};
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
        return reason == MapSelectorTrustedCandidateReason::none &&
               trusted_generation_satisfied;
    }

    [[nodiscard]] constexpr bool committed() const {
        return protected_ordering_satisfied() && candidate.committed();
    }
};

// Replacement composition that derives candidate generation values from the
// protected source. Selector verification/save happens on a private guard, and
// the target must exclusively own both selector storage and protected source
// for each complete call.
class MapSelectorTrustedCandidateCoordinator final {
public:
    MapSelectorTrustedCandidateCoordinator(
        MapSelectorCandidateCoordinator& candidate,
        MapSelectorTrustedGeneration& trusted_generation);

    [[nodiscard]] MapSelectorTrustedCandidateResult activate(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& candidate,
        std::uint64_t now_ms);

private:
    MapSelectorCandidateCoordinator& candidate_;
    MapSelectorTrustedGeneration& trusted_generation_;
};

}  // namespace opentrail::maps
