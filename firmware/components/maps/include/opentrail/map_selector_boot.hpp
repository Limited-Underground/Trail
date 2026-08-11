#pragma once

#include <cstdint>

#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorBootState : std::uint8_t {
    service_required = 0,
    mapless_ready,
    active_ready,
    trial_ready,
    fallback_required,
};

enum class MapSelectorBootReason : std::uint8_t {
    none = 0,
    no_checkpoint,
    live_guard_not_clean,
    invalid_policy,
    storage_unavailable,
    selector_invalid,
    generation_conflict,
    rollback_detected,
    checkpoint_rejected,
    trial_boot_limit,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    checkpoint_verification_failed,
    generation_exhausted,
};

struct MapSelectorBootResult {
    MapSelectorBootState state{MapSelectorBootState::service_required};
    MapSelectorBootReason reason{MapSelectorBootReason::checkpoint_rejected};
    MapSelectorLoadResult load{};
    MapSelectorSaveResult save{};
    MapActivationError guard_error{MapActivationError::none};
    std::uint64_t loaded_generation{0};
    std::uint64_t active_generation{0};
    bool map_exposure_allowed{false};
    bool fallback_required{false};
    bool repair_required{false};
    bool reconciliation_required{false};
    bool live_guard_published{false};

    [[nodiscard]] constexpr bool operational() const {
        return live_guard_published &&
               state != MapSelectorBootState::service_required;
    }
};

class MapSelectorBootCoordinator {
public:
    explicit MapSelectorBootCoordinator(MapSelectorStore& store);

    // live_guard must be stopped. Restored state remains private until every
    // required checkpoint write has passed the store's exact readback check.
    // The optional floor is caller-owned and is not anti-rollback protection.
    [[nodiscard]] MapSelectorBootResult boot(
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected,
        const MapPackageEvidence& previous,
        std::uint64_t now_ms,
        std::uint64_t trusted_minimum_generation,
        MapActivationGuard& live_guard);

private:
    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
