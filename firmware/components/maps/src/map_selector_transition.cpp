#include "opentrail/map_selector_transition.hpp"

#include "opentrail/map_selector_checkpoint.hpp"

namespace opentrail::maps {
namespace {

bool policy_matches(
    const MapSelectorCheckpoint& checkpoint,
    const MapActivationPolicy& policy) {
    return checkpoint.maximum_package_bytes ==
               policy.maximum_package_bytes &&
           checkpoint.trial_deadline_ms == policy.trial_deadline_ms &&
           checkpoint.required_healthy_reads ==
               policy.required_healthy_reads &&
           checkpoint.maximum_trial_boots == policy.maximum_trial_boots;
}

bool persistable_state(MapActivationState state) {
    return state == MapActivationState::active ||
           state == MapActivationState::trial ||
           state == MapActivationState::fallback_required;
}

bool same_persistent_status(
    const MapActivationStatus& first,
    const MapActivationStatus& second) {
    return first.running == second.running &&
           first.state == second.state &&
           first.active_slot == second.active_slot &&
           first.previous_slot == second.previous_slot &&
           first.active_generation == second.active_generation &&
           first.previous_generation == second.previous_generation &&
           first.trial_boots == second.trial_boots;
}

MapSelectorTransitionReason verification_failure_reason(
    MapSelectorStoreError error) {
    switch (error) {
        case MapSelectorStoreError::no_checkpoint:
            return MapSelectorTransitionReason::current_checkpoint_missing;
        case MapSelectorStoreError::invalid_state:
            return MapSelectorTransitionReason::selector_invalid;
        case MapSelectorStoreError::generation_conflict:
            return MapSelectorTransitionReason::generation_conflict;
        case MapSelectorStoreError::generation_below_floor:
            return MapSelectorTransitionReason::rollback_detected;
        case MapSelectorStoreError::state_mismatch:
        case MapSelectorStoreError::checkpoint_rejected:
            return MapSelectorTransitionReason::live_checkpoint_mismatch;
        case MapSelectorStoreError::storage_failure:
            return MapSelectorTransitionReason::storage_unavailable;
        default:
            return MapSelectorTransitionReason::live_checkpoint_mismatch;
    }
}

MapSelectorTransitionReason save_failure_reason(
    const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorTransitionReason::checkpoint_verification_failed
                   : MapSelectorTransitionReason::checkpoint_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::generation_conflict) {
        return MapSelectorTransitionReason::generation_conflict;
    }
    if (save.error == MapSelectorStoreError::generation_exhausted) {
        return MapSelectorTransitionReason::generation_exhausted;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorTransitionReason::checkpoint_verification_failed;
    }
    return MapSelectorTransitionReason::checkpoint_save_failed;
}

bool reason_requires_reconciliation(MapSelectorTransitionReason reason) {
    return reason == MapSelectorTransitionReason::generation_conflict ||
           reason == MapSelectorTransitionReason::rollback_detected ||
           reason ==
               MapSelectorTransitionReason::current_generation_mismatch ||
           reason == MapSelectorTransitionReason::live_checkpoint_mismatch ||
           reason ==
               MapSelectorTransitionReason::checkpoint_commit_uncertain ||
           reason ==
               MapSelectorTransitionReason::checkpoint_verification_failed ||
           reason == MapSelectorTransitionReason::checkpoint_clear_failed ||
           reason == MapSelectorTransitionReason::
                         checkpoint_clear_verification_failed;
}

MapSelectorState failure_selector_state(
    MapSelectorTransitionReason reason) {
    return reason_requires_reconciliation(reason)
               ? MapSelectorState::ambiguous
               : MapSelectorState::unreadable;
}

MapActivationError publish_mapless(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorTransitionReason reason) {
    MapActivationGuard mapless{};
    const auto error = mapless.start(
        policy, {failure_selector_state(reason), {}});
    if (error == MapActivationError::none) {
        live_guard = mapless;
    } else {
        live_guard.stop();
    }
    return error;
}

}  // namespace

MapSelectorTransitionCoordinator::MapSelectorTransitionCoordinator(
    MapSelectorStore& store)
    : store_(store) {}

MapSelectorTransitionResult
MapSelectorTransitionCoordinator::report_trial_read(
    MapActivationGuard& live_guard,
    const MapSelectorTransitionContext& context,
    bool complete_read_succeeded,
    std::uint64_t now_ms) {
    const auto before = live_guard.status();
    MapSelectorVerifyResult verification{};
    if (before.running && persistable_state(before.state)) {
        verification = store_.verify_current(
            live_guard, context.trusted_minimum_generation);
    }
    auto attempted = live_guard;
    const auto error = before.running && persistable_state(before.state)
                           ? attempted.report_trial_read(
                                 complete_read_succeeded, now_ms)
                           : MapActivationError::invalid_state;
    return finish(
        live_guard,
        attempted,
        context,
        MapSelectorTransitionOperation::report_trial_read,
        error,
        before,
        verification);
}

MapSelectorTransitionResult MapSelectorTransitionCoordinator::tick(
    MapActivationGuard& live_guard,
    const MapSelectorTransitionContext& context,
    std::uint64_t now_ms) {
    const auto before = live_guard.status();
    MapSelectorVerifyResult verification{};
    if (before.running && persistable_state(before.state)) {
        verification = store_.verify_current(
            live_guard, context.trusted_minimum_generation);
    }
    auto attempted = live_guard;
    const auto error = before.running && persistable_state(before.state)
                           ? attempted.tick(now_ms)
                           : MapActivationError::invalid_state;
    return finish(
        live_guard,
        attempted,
        context,
        MapSelectorTransitionOperation::tick,
        error,
        before,
        verification);
}

MapSelectorTransitionResult
MapSelectorTransitionCoordinator::complete_fallback(
    MapActivationGuard& live_guard,
    const MapSelectorTransitionContext& context,
    const MapPackageEvidence& restored) {
    const auto before = live_guard.status();
    MapSelectorVerifyResult verification{};
    if (before.running && persistable_state(before.state)) {
        verification = store_.verify_current(
            live_guard, context.trusted_minimum_generation);
    }
    auto attempted = live_guard;
    const auto error = before.running && persistable_state(before.state)
                           ? attempted.complete_fallback(restored)
                           : MapActivationError::invalid_state;
    return finish(
        live_guard,
        attempted,
        context,
        MapSelectorTransitionOperation::complete_fallback,
        error,
        before,
        verification);
}

MapSelectorTransitionResult
MapSelectorTransitionCoordinator::mark_previous_removed(
    MapActivationGuard& live_guard,
    const MapSelectorTransitionContext& context,
    MapSlot slot,
    std::uint64_t generation) {
    const auto before = live_guard.status();
    MapSelectorVerifyResult verification{};
    if (before.running && persistable_state(before.state)) {
        verification = store_.verify_current(
            live_guard, context.trusted_minimum_generation);
    }
    auto attempted = live_guard;
    const auto error = before.running && persistable_state(before.state)
                           ? attempted.mark_previous_removed(slot, generation)
                           : MapActivationError::invalid_state;
    return finish(
        live_guard,
        attempted,
        context,
        MapSelectorTransitionOperation::mark_previous_removed,
        error,
        before,
        verification);
}

MapSelectorTransitionResult MapSelectorTransitionCoordinator::finish(
    MapActivationGuard& live_guard,
    const MapActivationGuard& attempted_guard,
    const MapSelectorTransitionContext& context,
    MapSelectorTransitionOperation operation,
    MapActivationError guard_error,
    const MapActivationStatus& before,
    const MapSelectorVerifyResult& verification) {
    MapSelectorTransitionResult result{};
    result.operation = operation;
    result.guard_error = guard_error;
    result.before_state = before.state;
    result.attempted_state = attempted_guard.status().state;
    result.live_state = before.state;
    result.prior_generation = context.current_generation;
    result.active_generation = context.current_generation;
    result.verification = verification;
    result.repair_required = verification.recovery_required;

    if (!before.running) {
        result.reason =
            MapSelectorTransitionReason::live_guard_not_running;
        return result;
    }
    if (!persistable_state(before.state)) {
        result.reason =
            MapSelectorTransitionReason::live_state_not_persistable;
        return result;
    }
    if (context.current_generation == 0) {
        result.reason =
            MapSelectorTransitionReason::current_generation_mismatch;
        result.reconciliation_required = true;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorTransitionState::
            reconciliation_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        return result;
    }

    MapSelectorCheckpoint live_checkpoint{};
    const auto exported = live_guard.export_checkpoint(
        context.current_generation, live_checkpoint);
    if (exported != MapActivationError::none ||
        !policy_matches(live_checkpoint, context.policy)) {
        result.reason = MapSelectorTransitionReason::invalid_policy;
        result.guard_error = exported != MapActivationError::none
                                 ? exported
                                 : MapActivationError::checkpoint_mismatch;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorTransitionState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        return result;
    }

    if (!verification.exact_match ||
        verification.error != MapSelectorStoreError::none) {
        result.reason = verification_failure_reason(verification.error);
        result.reconciliation_required =
            reason_requires_reconciliation(result.reason);
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = result.reconciliation_required
                           ? MapSelectorTransitionState::
                                 reconciliation_required
                           : MapSelectorTransitionState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        return result;
    }
    if (verification.generation != context.current_generation) {
        result.reason =
            MapSelectorTransitionReason::current_generation_mismatch;
        result.reconciliation_required = true;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorTransitionState::
            reconciliation_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        return result;
    }

    const auto attempted = attempted_guard.status();
    result.persistence_required =
        !same_persistent_status(before, attempted);
    if (!result.persistence_required) {
        live_guard = attempted_guard;
        result.state = guard_error == MapActivationError::none
                           ? MapSelectorTransitionState::applied_volatile
                           : MapSelectorTransitionState::rejected;
        result.reason = guard_error == MapActivationError::none
                            ? MapSelectorTransitionReason::none
                            : MapSelectorTransitionReason::guard_rejected;
        result.live_state = live_guard.status().state;
        result.map_exposure_allowed = live_guard.status().map_available;
        return result;
    }

    if (attempted.state == MapActivationState::mapless) {
        result.clear_error = store_.reset();
        result.clear_inspection = store_.inspect();
        const bool exactly_empty =
            result.clear_error == MapSelectorStoreError::none &&
            result.clear_inspection.error ==
                MapSelectorStoreError::no_checkpoint &&
            result.clear_inspection.slot_a ==
                MapSelectorSlotState::empty &&
            result.clear_inspection.slot_b ==
                MapSelectorSlotState::empty;
        live_guard = attempted_guard;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless = true;
        result.active_generation = 0;
        if (exactly_empty) {
            result.state =
                MapSelectorTransitionState::mapless_committed;
            result.reason = MapSelectorTransitionReason::none;
            result.checkpoint_cleared = true;
        } else {
            result.state = MapSelectorTransitionState::
                reconciliation_required;
            result.reason = result.clear_error !=
                                    MapSelectorStoreError::none
                                ? MapSelectorTransitionReason::
                                      checkpoint_clear_failed
                                : MapSelectorTransitionReason::
                                      checkpoint_clear_verification_failed;
            result.reconciliation_required = true;
            result.repair_required = true;
        }
        return result;
    }

    result.save = store_.save_next_after(
        attempted_guard, context.trusted_minimum_generation);
    result.repair_required =
        result.repair_required || result.save.repaired_peer;
    if (result.save.saved()) {
        live_guard = attempted_guard;
        result.state = MapSelectorTransitionState::committed;
        result.reason = MapSelectorTransitionReason::none;
        result.live_state = live_guard.status().state;
        result.active_generation = result.save.generation;
        result.map_exposure_allowed = live_guard.status().map_available;
        return result;
    }

    result.reason = save_failure_reason(result.save);
    result.reconciliation_required =
        reason_requires_reconciliation(result.reason);
    publish_mapless(live_guard, context.policy, result.reason);
    result.state = result.reconciliation_required
                       ? MapSelectorTransitionState::
                             reconciliation_required
                       : MapSelectorTransitionState::service_required;
    result.live_state = live_guard.status().state;
    result.live_guard_mapless =
        result.live_state == MapActivationState::mapless;
    result.map_exposure_allowed = false;
    result.repair_required = true;
    return result;
}

}  // namespace opentrail::maps
