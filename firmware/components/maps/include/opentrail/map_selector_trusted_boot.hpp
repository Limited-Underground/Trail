#pragma once

#include <cstdint>

#include "opentrail/map_selector_boot.hpp"
#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {

enum class MapSelectorTrustedBootReason : std::uint8_t {
    none = 0,
    live_guard_not_clean,
    trusted_source_unavailable,
    trusted_source_changed,
    trusted_history_missing,
    selector_boot_failed,
    trusted_advance_failed,
};

struct MapSelectorTrustedBootResult {
    MapSelectorTrustedBootReason reason{
        MapSelectorTrustedBootReason::selector_boot_failed};
    MapSelectorBootResult selector{};
    MapSelectorTrustedGenerationResult trusted_before{};
    MapSelectorTrustedGenerationResult trusted_after{};
    std::uint64_t trusted_generation_before{0};
    std::uint64_t trusted_generation_after{0};
    bool live_guard_published{false};
    bool map_exposure_allowed{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool operational() const {
        return reason == MapSelectorTrustedBootReason::none &&
               live_guard_published && selector.operational();
    }
};

// Boot-only composition that derives the selector floor from the protected
// source and keeps restored selector state private until the source has been
// rechecked or advanced with exact readback. The target must provide exclusive
// ownership of the selector store and trusted-generation source for the call.
class MapSelectorTrustedBootCoordinator final {
public:
    MapSelectorTrustedBootCoordinator(
        MapSelectorBootCoordinator& selector_boot,
        MapSelectorTrustedGeneration& trusted_generation);

    [[nodiscard]] MapSelectorTrustedBootResult boot(
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected,
        const MapPackageEvidence& previous,
        std::uint64_t now_ms,
        MapActivationGuard& live_guard);

private:
    MapSelectorBootCoordinator& selector_boot_;
    MapSelectorTrustedGeneration& trusted_generation_;
};

}  // namespace opentrail::maps
