#pragma once

#include <cstdint>

#include "opentrail/map_selector_reseed.hpp"
#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {

enum class MapSelectorTrustedReseedReason : std::uint8_t {
    none = 0,
    live_state_not_service_mapless,
    trusted_source_unavailable,
    reseed_failed,
    trusted_advance_failed,
    trusted_source_changed,
};

struct MapSelectorTrustedReseedContext {
    MapActivationPolicy policy{};
    std::uint64_t boot_session_id{0};
    std::uint64_t authorization_use_time_ms{0};
};

struct MapSelectorTrustedReseedResult {
    MapSelectorTrustedReseedReason reason{
        MapSelectorTrustedReseedReason::reseed_failed};
    MapSelectorReseedResult reseed{};
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
        return reason == MapSelectorTrustedReseedReason::none &&
               trusted_generation_satisfied;
    }

    [[nodiscard]] constexpr bool committed() const {
        return protected_ordering_satisfied() && reseed.committed();
    }
};

// Authorized service recovery that derives the reseed floor from the
// protected source. Selector reset/save happens against a private guard, and
// the recovered map is published only after exact protected advance/readback.
// The permit must be minted for the same policy, package, and protected value.
// The target must exclusively own selector storage and the protected source
// for each complete call.
class MapSelectorTrustedReseedCoordinator final {
public:
    MapSelectorTrustedReseedCoordinator(
        MapSelectorReseedCoordinator& reseed,
        MapSelectorTrustedGeneration& trusted_generation);

    [[nodiscard]] MapSelectorTrustedReseedResult reseed(
        MapActivationGuard& live_guard,
        const MapSelectorTrustedReseedContext& context,
        const MapPackageEvidence& baseline,
        MapSelectorReseedPermit& permit);

private:
    MapSelectorReseedCoordinator& reseed_;
    MapSelectorTrustedGeneration& trusted_generation_;
};

}  // namespace opentrail::maps
