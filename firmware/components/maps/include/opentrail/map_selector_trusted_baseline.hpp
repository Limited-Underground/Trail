#pragma once

#include <cstdint>

#include "opentrail/map_selector_baseline.hpp"
#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {

enum class MapSelectorTrustedBaselineReason : std::uint8_t {
    none = 0,
    live_state_not_clean_baseline,
    trusted_source_unavailable,
    trusted_history_present,
    baseline_failed,
    trusted_advance_failed,
    trusted_source_changed,
};

struct MapSelectorTrustedBaselineResult {
    MapSelectorTrustedBaselineReason reason{
        MapSelectorTrustedBaselineReason::baseline_failed};
    MapSelectorBaselineResult baseline{};
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
        return reason == MapSelectorTrustedBaselineReason::none &&
               trusted_generation_satisfied;
    }

    [[nodiscard]] constexpr bool committed() const {
        return protected_ordering_satisfied() && baseline.committed();
    }
};

// First-use composition that accepts only zero protected history, keeps the
// generation-1 selector baseline private, and publishes it only after an exact
// protected advance/readback. The target must exclusively own selector storage
// and the protected source for each complete call.
class MapSelectorTrustedBaselineCoordinator final {
public:
    MapSelectorTrustedBaselineCoordinator(
        MapSelectorBaselineCoordinator& baseline,
        MapSelectorTrustedGeneration& trusted_generation);

    [[nodiscard]] MapSelectorTrustedBaselineResult establish(
        MapActivationGuard& live_guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& baseline_package);

private:
    MapSelectorBaselineCoordinator& baseline_;
    MapSelectorTrustedGeneration& trusted_generation_;
};

}  // namespace opentrail::maps
