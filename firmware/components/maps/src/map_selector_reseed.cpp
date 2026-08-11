#include "opentrail/map_selector_reseed.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::maps {
namespace {

bool service_mapless(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::mapless &&
           !status.map_available && status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none;
}

bool exactly_empty(const MapSelectorInspectionResult& inspection) {
    return inspection.error == MapSelectorStoreError::no_checkpoint &&
           inspection.slot_a == MapSelectorSlotState::empty &&
           inspection.slot_b == MapSelectorSlotState::empty &&
           !inspection.checkpoint_available;
}

bool needs_reconciliation(MapSelectorReseedReason reason) {
    return reason == MapSelectorReseedReason::selector_reset_failed ||
           reason == MapSelectorReseedReason::selector_reset_unverified ||
           reason == MapSelectorReseedReason::selector_changed ||
           reason ==
               MapSelectorReseedReason::checkpoint_commit_uncertain ||
           reason ==
               MapSelectorReseedReason::checkpoint_verification_failed;
}

MapSelectorReseedReason save_reason(const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorReseedReason::
                         checkpoint_verification_failed
                   : MapSelectorReseedReason::checkpoint_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::state_mismatch) {
        return MapSelectorReseedReason::selector_changed;
    }
    if (save.error == MapSelectorStoreError::generation_exhausted) {
        return MapSelectorReseedReason::generation_exhausted;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorReseedReason::checkpoint_verification_failed;
    }
    return MapSelectorReseedReason::checkpoint_save_failed;
}

void publish_mapless(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorReseedReason reason) {
    MapActivationGuard mapless{};
    const auto selector_state = needs_reconciliation(reason)
                                    ? MapSelectorState::ambiguous
                                    : MapSelectorState::unreadable;
    if (mapless.start(policy, {selector_state, {}}) ==
        MapActivationError::none) {
        live_guard = mapless;
    } else {
        live_guard.stop();
    }
}

void finish_failure(
    MapSelectorReseedResult& result,
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorReseedReason reason) {
    result.reason = reason;
    result.reconciliation_required = needs_reconciliation(reason);
    publish_mapless(live_guard, policy, reason);
    result.state = result.reconciliation_required
                       ? MapSelectorReseedState::reconciliation_required
                       : MapSelectorReseedState::service_required;
    const auto status = live_guard.status();
    result.live_state = status.state;
    result.live_guard_mapless = status.state == MapActivationState::mapless;
    result.map_exposure_allowed = false;
}

}  // namespace

MapSelectorReseedCoordinator::MapSelectorReseedCoordinator(
    MapSelectorStore& store)
    : store_(store) {}

MapSelectorReseedResult MapSelectorReseedCoordinator::reseed(
    MapActivationGuard& live_guard,
    const MapSelectorReseedContext& context,
    const MapPackageEvidence& baseline) {
    MapSelectorReseedResult result{};
    const auto before = live_guard.status();
    result.before_state = before.state;
    result.live_state = before.state;
    result.baseline_slot = baseline.slot;
    result.baseline_package_generation = baseline.generation;
    result.map_exposure_allowed = before.map_available;
    result.live_guard_mapless = before.state == MapActivationState::mapless;

    if (!context.authorization.complete()) {
        return result;
    }
    if (!before.running) {
        result.reason = MapSelectorReseedReason::live_guard_not_running;
        return result;
    }
    if (!service_mapless(before)) {
        result.reason = MapSelectorReseedReason::mapless_service_required;
        return result;
    }
    if (!live_guard.matches_policy(context.policy)) {
        result.reason = MapSelectorReseedReason::invalid_policy;
        live_guard.stop();
        result.state = MapSelectorReseedState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless = false;
        return result;
    }

    auto candidate_check = live_guard;
    result.candidate_error = candidate_check.stage(baseline);
    if (result.candidate_error != MapActivationError::none) {
        result.reason = MapSelectorReseedReason::candidate_rejected;
        return result;
    }

    result.inspection = store_.inspect();
    if (result.inspection.error == MapSelectorStoreError::storage_failure) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            MapSelectorReseedReason::storage_unavailable);
        return result;
    }
    if (exactly_empty(result.inspection) &&
        context.trusted_minimum_generation == 0) {
        result.reason = MapSelectorReseedReason::first_use_requires_baseline;
        return result;
    }

    result.prior_observed_record_generation =
        result.inspection.checkpoint_available
            ? result.inspection.generation
            : 0;
    result.generation_base = std::max(
        result.prior_observed_record_generation,
        context.trusted_minimum_generation);
    if (result.generation_base ==
        std::numeric_limits<std::uint64_t>::max()) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            MapSelectorReseedReason::generation_exhausted);
        return result;
    }

    MapActivationGuard attempted{};
    result.candidate_error = attempted.start(
        context.policy, {MapSelectorState::valid, baseline});
    if (result.candidate_error != MapActivationError::none ||
        attempted.status().state != MapActivationState::active) {
        result.reason = MapSelectorReseedReason::candidate_rejected;
        return result;
    }

    result.reset = store_.reset_and_verify_empty();
    result.selector_clear_verified = result.reset.cleared();
    if (!result.reset.cleared()) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            result.reset.error == MapSelectorStoreError::verification_failure
                ? MapSelectorReseedReason::selector_reset_unverified
                : MapSelectorReseedReason::selector_reset_failed);
        return result;
    }

    result.save = store_.save_if_empty(attempted, result.generation_base);
    if (!result.save.saved()) {
        finish_failure(
            result,
            live_guard,
            context.policy,
            save_reason(result.save));
        return result;
    }

    live_guard = attempted;
    result.state = MapSelectorReseedState::committed;
    result.reason = MapSelectorReseedReason::none;
    result.live_state = live_guard.status().state;
    result.active_record_generation = result.save.generation;
    result.map_exposure_allowed = live_guard.status().map_available;
    result.live_guard_mapless = false;
    return result;
}

}  // namespace opentrail::maps
