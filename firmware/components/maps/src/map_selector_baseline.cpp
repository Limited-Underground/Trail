#include "opentrail/map_selector_baseline.hpp"

namespace opentrail::maps {
namespace {

bool clean_mapless(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::mapless &&
           status.reason == MapActivationReason::no_selector &&
           status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none && !status.map_available;
}

bool empty_store(const MapSelectorInspectionResult& inspection) {
    return inspection.error == MapSelectorStoreError::no_checkpoint &&
           inspection.slot_a == MapSelectorSlotState::empty &&
           inspection.slot_b == MapSelectorSlotState::empty &&
           !inspection.checkpoint_available;
}

bool needs_reconciliation(MapSelectorBaselineReason reason) {
    return reason == MapSelectorBaselineReason::trusted_history_present ||
           reason == MapSelectorBaselineReason::selector_not_empty ||
           reason == MapSelectorBaselineReason::generation_conflict ||
           reason == MapSelectorBaselineReason::selector_changed ||
           reason ==
               MapSelectorBaselineReason::checkpoint_commit_uncertain ||
           reason ==
               MapSelectorBaselineReason::checkpoint_verification_failed;
}

MapSelectorBaselineReason inspection_reason(
    const MapSelectorInspectionResult& inspection) {
    switch (inspection.error) {
        case MapSelectorStoreError::none:
            return MapSelectorBaselineReason::selector_not_empty;
        case MapSelectorStoreError::invalid_state:
            return MapSelectorBaselineReason::selector_invalid;
        case MapSelectorStoreError::storage_failure:
            return MapSelectorBaselineReason::storage_unavailable;
        case MapSelectorStoreError::generation_conflict:
            return MapSelectorBaselineReason::generation_conflict;
        default:
            return MapSelectorBaselineReason::selector_invalid;
    }
}

MapSelectorBaselineReason save_reason(const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorBaselineReason::
                         checkpoint_verification_failed
                   : MapSelectorBaselineReason::checkpoint_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::state_mismatch) {
        return MapSelectorBaselineReason::selector_changed;
    }
    if (save.error == MapSelectorStoreError::generation_conflict) {
        return MapSelectorBaselineReason::generation_conflict;
    }
    if (save.error == MapSelectorStoreError::invalid_state) {
        return MapSelectorBaselineReason::selector_invalid;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorBaselineReason::checkpoint_verification_failed;
    }
    return MapSelectorBaselineReason::checkpoint_save_failed;
}

void publish_mapless(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorBaselineReason reason) {
    const bool ambiguous = needs_reconciliation(reason);
    MapActivationGuard mapless{};
    const auto error = mapless.start(
        policy,
        {ambiguous ? MapSelectorState::ambiguous
                   : MapSelectorState::unreadable,
         {}});
    if (error == MapActivationError::none) {
        live_guard = mapless;
    } else {
        live_guard.stop();
    }
}

void finish_failure(
    MapSelectorBaselineResult& result,
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorBaselineReason reason) {
    result.reason = reason;
    result.reconciliation_required = needs_reconciliation(reason);
    publish_mapless(live_guard, policy, reason);
    result.state = result.reconciliation_required
                       ? MapSelectorBaselineState::reconciliation_required
                       : MapSelectorBaselineState::service_required;
    result.live_state = live_guard.status().state;
    result.live_guard_mapless =
        result.live_state == MapActivationState::mapless;
    result.map_exposure_allowed = false;
}

}  // namespace

MapSelectorBaselineCoordinator::MapSelectorBaselineCoordinator(
    MapSelectorStore& store)
    : store_(store) {}

MapSelectorBaselineResult MapSelectorBaselineCoordinator::establish(
    MapActivationGuard& live_guard,
    const MapSelectorBaselineContext& context,
    const MapPackageEvidence& baseline) {
    MapSelectorBaselineResult result{};
    const auto before = live_guard.status();
    result.before_state = before.state;
    result.live_state = before.state;
    result.baseline_slot = baseline.slot;
    result.baseline_package_generation = baseline.generation;
    result.map_exposure_allowed = before.map_available;
    result.live_guard_mapless =
        before.state == MapActivationState::mapless;

    if (!before.running) {
        result.reason = MapSelectorBaselineReason::live_guard_not_running;
        return result;
    }
    if (!clean_mapless(before)) {
        result.reason =
            MapSelectorBaselineReason::clean_mapless_guard_required;
        return result;
    }
    if (!live_guard.matches_policy(context.policy)) {
        result.reason = MapSelectorBaselineReason::invalid_policy;
        live_guard.stop();
        result.state = MapSelectorBaselineState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless = false;
        return result;
    }

    auto candidate_check = live_guard;
    result.candidate_error = candidate_check.stage(baseline);
    if (result.candidate_error != MapActivationError::none) {
        result.reason = MapSelectorBaselineReason::candidate_rejected;
        return result;
    }

    if (context.trusted_minimum_generation != 0) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            MapSelectorBaselineReason::trusted_history_present);
        return result;
    }

    result.inspection = store_.inspect();
    if (!empty_store(result.inspection)) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            inspection_reason(result.inspection));
        return result;
    }

    MapActivationGuard attempted{};
    result.candidate_error = attempted.start(
        context.policy, {MapSelectorState::valid, baseline});
    if (result.candidate_error != MapActivationError::none ||
        attempted.status().state != MapActivationState::active) {
        result.reason = MapSelectorBaselineReason::candidate_rejected;
        return result;
    }

    result.save = store_.save_if_empty(attempted);
    if (!result.save.saved()) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            save_reason(result.save));
        return result;
    }

    live_guard = attempted;
    result.state = MapSelectorBaselineState::committed;
    result.reason = MapSelectorBaselineReason::none;
    result.live_state = live_guard.status().state;
    result.active_record_generation = result.save.generation;
    result.map_exposure_allowed = live_guard.status().map_available;
    result.live_guard_mapless = false;
    return result;
}

}  // namespace opentrail::maps
