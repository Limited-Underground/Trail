#include "opentrail/map_selector_trusted_boot.hpp"

namespace opentrail::maps {
namespace {

void publish_candidate(
    MapActivationGuard& candidate,
    bool map_exposure_allowed,
    MapSelectorTrustedBootResult& result,
    MapActivationGuard& live_guard) {
    live_guard = candidate;
    result.live_guard_published = true;
    result.map_exposure_allowed = map_exposure_allowed;
    result.selector.live_guard_published = true;
    result.selector.map_exposure_allowed = map_exposure_allowed;
}

}  // namespace

MapSelectorTrustedBootCoordinator::MapSelectorTrustedBootCoordinator(
    MapSelectorBootCoordinator& selector_boot,
    MapSelectorTrustedGeneration& trusted_generation)
    : selector_boot_(selector_boot),
      trusted_generation_(trusted_generation) {}

MapSelectorTrustedBootResult MapSelectorTrustedBootCoordinator::boot(
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected,
    const MapPackageEvidence& previous,
    std::uint64_t now_ms,
    MapActivationGuard& live_guard) {
    MapSelectorTrustedBootResult result{};
    if (live_guard.status().running) {
        result.reason = MapSelectorTrustedBootReason::live_guard_not_clean;
        result.selector.reason =
            MapSelectorBootReason::live_guard_not_clean;
        result.selector.guard_error = MapActivationError::invalid_state;
        return result;
    }

    result.trusted_before = trusted_generation_.inspect();
    result.trusted_generation_before =
        result.trusted_before.observed_generation;
    if (!result.trusted_before.usable()) {
        result.reason =
            MapSelectorTrustedBootReason::trusted_source_unavailable;
        result.reconciliation_required =
            result.trusted_before.reconciliation_required;
        return result;
    }

    MapActivationGuard candidate{};
    result.selector = selector_boot_.boot(
        policy,
        selected,
        previous,
        now_ms,
        result.trusted_generation_before,
        candidate);
    const bool candidate_guard_published =
        result.selector.live_guard_published;
    const bool candidate_operational = result.selector.operational();
    const bool candidate_map_exposure_allowed =
        result.selector.map_exposure_allowed;
    result.selector.live_guard_published = false;
    result.selector.map_exposure_allowed = false;

    if (!candidate_operational) {
        const bool trusted_history_without_selector =
            result.trusted_generation_before != 0 &&
            result.selector.load.error ==
                MapSelectorStoreError::generation_below_floor &&
            result.selector.load.slot_a == MapSelectorSlotState::empty &&
            result.selector.load.slot_b == MapSelectorSlotState::empty;
        if (trusted_history_without_selector) {
            result.reason =
                MapSelectorTrustedBootReason::trusted_history_missing;
            result.reconciliation_required = true;
            return result;
        }
        result.reason = MapSelectorTrustedBootReason::selector_boot_failed;
        result.reconciliation_required =
            result.selector.reconciliation_required;
        if (candidate_guard_published) {
            publish_candidate(candidate, false, result, live_guard);
        }
        return result;
    }

    if (result.selector.state == MapSelectorBootState::mapless_ready &&
        result.trusted_generation_before != 0) {
        result.reason =
            MapSelectorTrustedBootReason::trusted_history_missing;
        result.reconciliation_required = true;
        return result;
    }

    const std::uint64_t selector_generation =
        result.selector.active_generation;
    if (selector_generation < result.trusted_generation_before) {
        result.reason = MapSelectorTrustedBootReason::trusted_source_changed;
        result.reconciliation_required = true;
        return result;
    }

    if (selector_generation > result.trusted_generation_before) {
        result.trusted_after = trusted_generation_.advance_exact(
            result.trusted_generation_before, selector_generation);
        if (!result.trusted_after.usable()) {
            result.reason =
                MapSelectorTrustedBootReason::trusted_advance_failed;
            result.reconciliation_required =
                result.trusted_after.reconciliation_required;
            return result;
        }
    } else {
        result.trusted_after = trusted_generation_.inspect();
        if (!result.trusted_after.usable()) {
            result.reason =
                MapSelectorTrustedBootReason::trusted_source_unavailable;
            result.reconciliation_required =
                result.trusted_after.reconciliation_required;
            return result;
        }
    }

    result.trusted_generation_after =
        result.trusted_after.observed_generation;
    if (result.trusted_generation_after != selector_generation) {
        result.reason = MapSelectorTrustedBootReason::trusted_source_changed;
        result.reconciliation_required = true;
        return result;
    }

    publish_candidate(
        candidate,
        candidate_map_exposure_allowed,
        result,
        live_guard);
    result.reason = MapSelectorTrustedBootReason::none;
    return result;
}

}  // namespace opentrail::maps
