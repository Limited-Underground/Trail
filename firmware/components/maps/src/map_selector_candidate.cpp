#include "opentrail/map_selector_candidate.hpp"

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

MapSelectorCandidateReason verification_reason(
    MapSelectorStoreError error) {
    switch (error) {
        case MapSelectorStoreError::no_checkpoint:
            return MapSelectorCandidateReason::current_checkpoint_missing;
        case MapSelectorStoreError::invalid_state:
            return MapSelectorCandidateReason::selector_invalid;
        case MapSelectorStoreError::generation_conflict:
            return MapSelectorCandidateReason::generation_conflict;
        case MapSelectorStoreError::generation_below_floor:
            return MapSelectorCandidateReason::rollback_detected;
        case MapSelectorStoreError::state_mismatch:
        case MapSelectorStoreError::checkpoint_rejected:
            return MapSelectorCandidateReason::live_checkpoint_mismatch;
        case MapSelectorStoreError::storage_failure:
            return MapSelectorCandidateReason::storage_unavailable;
        default:
            return MapSelectorCandidateReason::live_checkpoint_mismatch;
    }
}

MapSelectorCandidateReason save_reason(
    const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorCandidateReason::checkpoint_verification_failed
                   : MapSelectorCandidateReason::checkpoint_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::generation_conflict) {
        return MapSelectorCandidateReason::generation_conflict;
    }
    if (save.error == MapSelectorStoreError::state_mismatch) {
        return MapSelectorCandidateReason::live_checkpoint_mismatch;
    }
    if (save.error == MapSelectorStoreError::generation_exhausted) {
        return MapSelectorCandidateReason::generation_exhausted;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorCandidateReason::checkpoint_verification_failed;
    }
    return MapSelectorCandidateReason::checkpoint_save_failed;
}

bool needs_reconciliation(MapSelectorCandidateReason reason) {
    return reason == MapSelectorCandidateReason::generation_conflict ||
           reason == MapSelectorCandidateReason::rollback_detected ||
           reason ==
               MapSelectorCandidateReason::current_generation_mismatch ||
           reason == MapSelectorCandidateReason::live_checkpoint_mismatch ||
           reason ==
               MapSelectorCandidateReason::checkpoint_commit_uncertain ||
           reason ==
               MapSelectorCandidateReason::checkpoint_verification_failed;
}

MapActivationError publish_mapless(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSelectorCandidateReason reason) {
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
    return error;
}

}  // namespace

MapSelectorCandidateCoordinator::MapSelectorCandidateCoordinator(
    MapSelectorStore& store)
    : store_(store) {}

MapSelectorCandidateResult MapSelectorCandidateCoordinator::activate(
    MapActivationGuard& live_guard,
    const MapSelectorCandidateContext& context,
    const MapPackageEvidence& candidate,
    std::uint64_t now_ms) {
    MapSelectorCandidateResult result{};
    const auto before = live_guard.status();
    result.before_state = before.state;
    result.live_state = before.state;
    result.candidate_slot = candidate.slot;
    result.candidate_generation = candidate.generation;
    result.prior_record_generation = context.current_generation;
    result.active_record_generation = context.current_generation;
    result.map_exposure_allowed = before.map_available;

    if (!before.running) {
        result.reason = MapSelectorCandidateReason::live_guard_not_running;
        return result;
    }
    if (before.state != MapActivationState::active) {
        result.reason = MapSelectorCandidateReason::stable_baseline_required;
        return result;
    }
    if (context.current_generation == 0) {
        result.reason =
            MapSelectorCandidateReason::current_generation_mismatch;
        result.reconciliation_required = true;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorCandidateState::reconciliation_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        result.map_exposure_allowed = false;
        return result;
    }

    MapSelectorCheckpoint live_checkpoint{};
    const auto exported = live_guard.export_checkpoint(
        context.current_generation, live_checkpoint);
    if (exported != MapActivationError::none ||
        !policy_matches(live_checkpoint, context.policy)) {
        result.reason = MapSelectorCandidateReason::invalid_policy;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorCandidateState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        result.map_exposure_allowed = false;
        return result;
    }

    result.verification = store_.verify_current(
        live_guard, context.trusted_minimum_generation);
    result.repair_required = result.verification.recovery_required;
    if (!result.verification.exact_match ||
        result.verification.error != MapSelectorStoreError::none) {
        result.reason = verification_reason(result.verification.error);
        result.reconciliation_required = needs_reconciliation(result.reason);
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = result.reconciliation_required
                           ? MapSelectorCandidateState::
                                 reconciliation_required
                           : MapSelectorCandidateState::service_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        result.map_exposure_allowed = false;
        return result;
    }
    if (result.verification.generation != context.current_generation) {
        result.reason =
            MapSelectorCandidateReason::current_generation_mismatch;
        result.reconciliation_required = true;
        publish_mapless(live_guard, context.policy, result.reason);
        result.state = MapSelectorCandidateState::reconciliation_required;
        result.live_state = live_guard.status().state;
        result.live_guard_mapless =
            result.live_state == MapActivationState::mapless;
        result.map_exposure_allowed = false;
        return result;
    }

    auto attempted = live_guard;
    result.stage_error = attempted.stage(candidate);
    if (result.stage_error != MapActivationError::none) {
        result.reason = MapSelectorCandidateReason::candidate_rejected;
        return result;
    }
    result.selector_error = attempted.mark_selector_committed(
        candidate.slot, candidate.generation, now_ms);
    if (result.selector_error != MapActivationError::none) {
        result.reason =
            MapSelectorCandidateReason::selector_commit_rejected;
        return result;
    }

    result.save = store_.save_after_exact(
        attempted,
        context.current_generation,
        context.trusted_minimum_generation);
    result.repair_required =
        result.repair_required || result.save.repaired_peer;
    if (result.save.saved()) {
        live_guard = attempted;
        result.state = MapSelectorCandidateState::committed;
        result.reason = MapSelectorCandidateReason::none;
        result.live_state = live_guard.status().state;
        result.active_record_generation = result.save.generation;
        result.map_exposure_allowed = live_guard.status().map_available;
        return result;
    }

    result.reason = save_reason(result.save);
    result.reconciliation_required = needs_reconciliation(result.reason);
    publish_mapless(live_guard, context.policy, result.reason);
    result.state = result.reconciliation_required
                       ? MapSelectorCandidateState::reconciliation_required
                       : MapSelectorCandidateState::service_required;
    result.live_state = live_guard.status().state;
    result.live_guard_mapless =
        result.live_state == MapActivationState::mapless;
    result.map_exposure_allowed = false;
    result.repair_required = true;
    return result;
}

}  // namespace opentrail::maps
