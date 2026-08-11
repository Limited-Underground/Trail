#include "opentrail/map_selector_boot.hpp"

namespace opentrail::maps {
namespace {

MapSelectorBootReason load_failure_reason(
    const MapSelectorLoadResult& load) {
    if (load.guard_error == MapActivationError::invalid_policy) {
        return MapSelectorBootReason::invalid_policy;
    }
    switch (load.error) {
        case MapSelectorStoreError::no_checkpoint:
            return MapSelectorBootReason::no_checkpoint;
        case MapSelectorStoreError::invalid_state:
            return MapSelectorBootReason::selector_invalid;
        case MapSelectorStoreError::generation_conflict:
            return MapSelectorBootReason::generation_conflict;
        case MapSelectorStoreError::generation_below_floor:
            return MapSelectorBootReason::rollback_detected;
        case MapSelectorStoreError::storage_failure:
            return MapSelectorBootReason::storage_unavailable;
        default:
            return MapSelectorBootReason::checkpoint_rejected;
    }
}

MapSelectorBootReason save_failure_reason(
    const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorBootReason::checkpoint_verification_failed
                   : MapSelectorBootReason::checkpoint_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::generation_conflict) {
        return MapSelectorBootReason::generation_conflict;
    }
    if (save.error == MapSelectorStoreError::generation_exhausted) {
        return MapSelectorBootReason::generation_exhausted;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorBootReason::checkpoint_verification_failed;
    }
    return MapSelectorBootReason::checkpoint_save_failed;
}

MapSelectorState degraded_selector_state(MapSelectorBootReason reason) {
    return reason == MapSelectorBootReason::generation_conflict ||
                   reason == MapSelectorBootReason::rollback_detected
               ? MapSelectorState::ambiguous
               : MapSelectorState::unreadable;
}

MapActivationError publish_mapless(
    const MapActivationPolicy& policy,
    MapSelectorState selector_state,
    MapActivationGuard& live_guard) {
    MapActivationGuard mapless{};
    const auto started = mapless.start(policy, {selector_state, {}});
    if (started == MapActivationError::none) {
        live_guard = mapless;
    }
    return started;
}

void publish_service_mapless(
    const MapActivationPolicy& policy,
    MapSelectorBootReason reason,
    MapSelectorBootResult& result,
    MapActivationGuard& live_guard) {
    const auto published = publish_mapless(
        policy, degraded_selector_state(reason), live_guard);
    if (published == MapActivationError::none) {
        result.live_guard_published = true;
        return;
    }
    result.guard_error = published;
    result.reason = MapSelectorBootReason::invalid_policy;
}

}  // namespace

MapSelectorBootCoordinator::MapSelectorBootCoordinator(
    MapSelectorStore& store)
    : store_(store) {}

MapSelectorBootResult MapSelectorBootCoordinator::boot(
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected,
    const MapPackageEvidence& previous,
    std::uint64_t now_ms,
    std::uint64_t trusted_minimum_generation,
    MapActivationGuard& live_guard) {
    MapSelectorBootResult result{};
    if (live_guard.status().running) {
        result.reason = MapSelectorBootReason::live_guard_not_clean;
        result.guard_error = MapActivationError::invalid_state;
        return result;
    }

    MapActivationGuard candidate{};
    result.load = store_.restore_at_or_above(
        candidate,
        policy,
        selected,
        previous,
        now_ms,
        trusted_minimum_generation);
    result.guard_error = result.load.guard_error;
    result.loaded_generation = result.load.generation;
    result.repair_required = result.load.recovery_required;

    const bool boot_limit_transition =
        result.load.guard_error ==
            MapActivationError::trial_boot_limit_reached &&
        candidate.status().running &&
        candidate.status().state ==
            MapActivationState::fallback_required;

    if (!result.load.restored && !boot_limit_transition) {
        result.reason = load_failure_reason(result.load);
        if (result.reason == MapSelectorBootReason::no_checkpoint) {
            const auto published = publish_mapless(
                policy, MapSelectorState::missing, live_guard);
            if (published != MapActivationError::none) {
                result.reason = MapSelectorBootReason::invalid_policy;
                result.guard_error = published;
                return result;
            }
            result.state = MapSelectorBootState::mapless_ready;
            result.live_guard_published = true;
            return result;
        }
        publish_service_mapless(
            policy, result.reason, result, live_guard);
        result.reconciliation_required =
            result.reason == MapSelectorBootReason::generation_conflict ||
            result.reason == MapSelectorBootReason::rollback_detected;
        return result;
    }

    const auto candidate_status = candidate.status();
    const bool persistence_required =
        candidate_status.state == MapActivationState::trial ||
        boot_limit_transition;
    if (persistence_required) {
        result.save = store_.save_after_exact(
            candidate,
            result.load.generation,
            trusted_minimum_generation);
        result.repair_required =
            result.repair_required || result.save.repaired_peer;
        if (!result.save.saved()) {
            result.reason = save_failure_reason(result.save);
            result.reconciliation_required = result.save.commit_uncertain;
            result.repair_required = true;
            publish_service_mapless(
                policy, result.reason, result, live_guard);
            return result;
        }
        result.active_generation = result.save.generation;
    } else {
        result.active_generation = result.load.generation;
    }

    live_guard = candidate;
    result.live_guard_published = true;
    switch (candidate_status.state) {
        case MapActivationState::active:
            result.state = MapSelectorBootState::active_ready;
            result.reason = MapSelectorBootReason::none;
            result.map_exposure_allowed = true;
            break;
        case MapActivationState::trial:
            result.state = MapSelectorBootState::trial_ready;
            result.reason = MapSelectorBootReason::none;
            result.map_exposure_allowed = true;
            break;
        case MapActivationState::fallback_required:
            result.state = MapSelectorBootState::fallback_required;
            result.reason = boot_limit_transition
                                ? MapSelectorBootReason::trial_boot_limit
                                : MapSelectorBootReason::none;
            result.fallback_required = true;
            break;
        default:
            live_guard.stop();
            result.state = MapSelectorBootState::service_required;
            result.reason = MapSelectorBootReason::checkpoint_rejected;
            result.map_exposure_allowed = false;
            result.live_guard_published = false;
            break;
    }
    return result;
}

}  // namespace opentrail::maps
