#include "opentrail/map_selector_trusted_reseed.hpp"

namespace opentrail::maps {
namespace {

bool service_mapless(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::mapless &&
           !status.map_available && status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorTrustedReseedResult& result) {
    const auto status = live_guard.status();
    result.map_exposure_allowed = status.map_available;
    result.live_guard_mapless =
        status.running && status.state == MapActivationState::mapless;
}

void publish_candidate(
    const MapActivationGuard& candidate,
    MapSelectorTrustedReseedResult& result,
    MapActivationGuard& live_guard) {
    live_guard = candidate;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorTrustedReseedResult& result,
    MapActivationGuard& live_guard) {
    MapActivationGuard mapless{};
    result.containment_error = mapless.start(
        policy,
        {ambiguous ? MapSelectorState::ambiguous
                   : MapSelectorState::unreadable,
         {}});
    if (result.containment_error == MapActivationError::none) {
        live_guard = mapless;
    } else {
        live_guard.stop();
    }
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

bool reseed_failed(const MapSelectorReseedResult& reseed) {
    return reseed.state == MapSelectorReseedState::service_required ||
           reseed.state ==
               MapSelectorReseedState::reconciliation_required;
}

}  // namespace

MapSelectorTrustedReseedCoordinator::
    MapSelectorTrustedReseedCoordinator(
        MapSelectorReseedCoordinator& reseed,
        MapSelectorTrustedGeneration& trusted_generation)
    : reseed_(reseed),
      trusted_generation_(trusted_generation) {}

MapSelectorTrustedReseedResult
MapSelectorTrustedReseedCoordinator::reseed(
    MapActivationGuard& live_guard,
    const MapSelectorTrustedReseedContext& context,
    const MapPackageEvidence& baseline,
    MapSelectorReseedPermit& permit) {
    MapSelectorTrustedReseedResult result{};
    if (!service_mapless(live_guard.status())) {
        result.reason = MapSelectorTrustedReseedReason::
            live_state_not_service_mapless;
        record_live_state(live_guard, result);
        return result;
    }

    result.trusted_before = trusted_generation_.inspect();
    result.trusted_generation_before =
        result.trusted_before.observed_generation;
    if (!result.trusted_before.usable()) {
        result.reason = MapSelectorTrustedReseedReason::
            trusted_source_unavailable;
        result.reconciliation_required =
            result.trusted_before.reconciliation_required;
        if (result.reconciliation_required) {
            contain_map_exposure(
                context.policy, true, result, live_guard);
        } else {
            record_live_state(live_guard, result);
        }
        return result;
    }

    MapActivationGuard private_guard = live_guard;
    const MapSelectorReseedContext reseed_context{
        context.policy,
        result.trusted_generation_before,
        context.boot_session_id,
        context.authorization_use_time_ms};
    result.reseed = reseed_.reseed(
        private_guard, reseed_context, baseline, permit);

    if (reseed_failed(result.reseed)) {
        result.reason = MapSelectorTrustedReseedReason::reseed_failed;
        result.reconciliation_required =
            result.reseed.reconciliation_required;
        publish_candidate(private_guard, result, live_guard);
        return result;
    }

    if (result.reseed.state == MapSelectorReseedState::rejected) {
        result.reason = MapSelectorTrustedReseedReason::reseed_failed;
        record_live_state(live_guard, result);
        return result;
    }

    if (!result.reseed.committed()) {
        result.reason = MapSelectorTrustedReseedReason::reseed_failed;
        result.reconciliation_required = true;
        contain_map_exposure(context.policy, true, result, live_guard);
        return result;
    }

    const auto target_generation =
        result.reseed.active_record_generation;
    if (target_generation <= result.trusted_generation_before) {
        result.reason = MapSelectorTrustedReseedReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(context.policy, true, result, live_guard);
        return result;
    }

    result.trusted_after = trusted_generation_.advance_exact(
        result.trusted_generation_before, target_generation);
    if (!result.trusted_after.usable()) {
        result.reason = MapSelectorTrustedReseedReason::
            trusted_advance_failed;
        // Selector reset and replacement are already durable. Every failure
        // to confirm matching protected history now requires fresh boot.
        result.reconciliation_required = true;
        contain_map_exposure(context.policy, true, result, live_guard);
        return result;
    }

    result.trusted_generation_after =
        result.trusted_after.observed_generation;
    if (result.trusted_generation_after != target_generation) {
        result.reason = MapSelectorTrustedReseedReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(context.policy, true, result, live_guard);
        return result;
    }

    publish_candidate(private_guard, result, live_guard);
    result.reason = MapSelectorTrustedReseedReason::none;
    result.trusted_generation_satisfied = true;
    return result;
}

}  // namespace opentrail::maps
