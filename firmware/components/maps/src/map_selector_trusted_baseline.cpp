#include "opentrail/map_selector_trusted_baseline.hpp"

namespace opentrail::maps {
namespace {

bool clean_mapless_baseline(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::mapless &&
           status.reason == MapActivationReason::no_selector &&
           status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none && !status.map_available;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorTrustedBaselineResult& result) {
    const auto status = live_guard.status();
    result.map_exposure_allowed = status.map_available;
    result.live_guard_mapless =
        status.running && status.state == MapActivationState::mapless;
}

void publish_candidate(
    const MapActivationGuard& candidate,
    MapSelectorTrustedBaselineResult& result,
    MapActivationGuard& live_guard) {
    live_guard = candidate;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorTrustedBaselineResult& result,
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

bool baseline_failed(const MapSelectorBaselineResult& baseline) {
    return baseline.state == MapSelectorBaselineState::service_required ||
           baseline.state ==
               MapSelectorBaselineState::reconciliation_required;
}

}  // namespace

MapSelectorTrustedBaselineCoordinator::
    MapSelectorTrustedBaselineCoordinator(
        MapSelectorBaselineCoordinator& baseline,
        MapSelectorTrustedGeneration& trusted_generation)
    : baseline_(baseline),
      trusted_generation_(trusted_generation) {}

MapSelectorTrustedBaselineResult
MapSelectorTrustedBaselineCoordinator::establish(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& baseline_package) {
    MapSelectorTrustedBaselineResult result{};
    if (!clean_mapless_baseline(live_guard.status())) {
        MapActivationGuard private_guard = live_guard;
        const MapSelectorBaselineContext empty_context{policy, 0};
        result.baseline = baseline_.establish(
            private_guard, empty_context, baseline_package);
        result.reason = MapSelectorTrustedBaselineReason::
            live_state_not_clean_baseline;
        record_live_state(live_guard, result);
        return result;
    }

    result.trusted_before = trusted_generation_.inspect();
    result.trusted_generation_before =
        result.trusted_before.observed_generation;
    if (!result.trusted_before.usable()) {
        result.reason = MapSelectorTrustedBaselineReason::
            trusted_source_unavailable;
        result.reconciliation_required =
            result.trusted_before.reconciliation_required;
        if (result.reconciliation_required) {
            contain_map_exposure(policy, true, result, live_guard);
        } else {
            record_live_state(live_guard, result);
        }
        return result;
    }

    if (result.trusted_generation_before != 0) {
        result.reason = MapSelectorTrustedBaselineReason::
            trusted_history_present;
        result.baseline.reason =
            MapSelectorBaselineReason::trusted_history_present;
        result.baseline.state =
            MapSelectorBaselineState::reconciliation_required;
        result.baseline.reconciliation_required = true;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    MapActivationGuard private_guard = live_guard;
    const MapSelectorBaselineContext context{policy, 0};
    result.baseline = baseline_.establish(
        private_guard, context, baseline_package);

    if (baseline_failed(result.baseline)) {
        result.reason = MapSelectorTrustedBaselineReason::baseline_failed;
        result.reconciliation_required =
            result.baseline.reconciliation_required;
        publish_candidate(private_guard, result, live_guard);
        return result;
    }

    if (result.baseline.state == MapSelectorBaselineState::rejected) {
        result.reason = MapSelectorTrustedBaselineReason::baseline_failed;
        record_live_state(live_guard, result);
        return result;
    }

    if (!result.baseline.committed()) {
        result.reason = MapSelectorTrustedBaselineReason::baseline_failed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    const auto target_generation =
        result.baseline.active_record_generation;
    if (target_generation <= result.trusted_generation_before) {
        result.reason = MapSelectorTrustedBaselineReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    result.trusted_after = trusted_generation_.advance_exact(
        result.trusted_generation_before, target_generation);
    if (!result.trusted_after.usable()) {
        result.reason = MapSelectorTrustedBaselineReason::
            trusted_advance_failed;
        // Selector generation 1 already exists. Every failure to confirm the
        // matching protected value now requires fresh-boot reconciliation.
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    result.trusted_generation_after =
        result.trusted_after.observed_generation;
    if (result.trusted_generation_after != target_generation) {
        result.reason = MapSelectorTrustedBaselineReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    publish_candidate(private_guard, result, live_guard);
    result.reason = MapSelectorTrustedBaselineReason::none;
    result.trusted_generation_satisfied = true;
    return result;
}

}  // namespace opentrail::maps
