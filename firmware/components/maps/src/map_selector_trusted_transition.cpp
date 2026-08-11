#include "opentrail/map_selector_trusted_transition.hpp"

namespace opentrail::maps {
namespace {

bool transitionable_state(const MapActivationStatus& status) {
    return status.running &&
           (status.state == MapActivationState::active ||
            status.state == MapActivationState::trial ||
            status.state == MapActivationState::fallback_required);
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorTrustedTransitionResult& result) {
    const auto status = live_guard.status();
    result.map_exposure_allowed = status.map_available;
    result.live_guard_mapless =
        status.running && status.state == MapActivationState::mapless;
}

void publish_candidate(
    const MapActivationGuard& candidate,
    MapSelectorTrustedTransitionResult& result,
    MapActivationGuard& live_guard) {
    live_guard = candidate;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorTrustedTransitionResult& result,
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

bool transition_failed(const MapSelectorTransitionResult& transition) {
    return transition.state == MapSelectorTransitionState::service_required ||
           transition.state ==
               MapSelectorTransitionState::reconciliation_required;
}

template <typename TransitionCall>
MapSelectorTrustedTransitionResult run_transition(
    MapSelectorTransitionCoordinator& transition,
    MapSelectorTrustedGeneration& trusted_generation,
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    TransitionCall call) {
    MapSelectorTrustedTransitionResult result{};
    if (!transitionable_state(live_guard.status())) {
        MapActivationGuard candidate = live_guard;
        const MapSelectorTransitionContext empty_context{policy, 0, 0};
        result.transition = call(
            transition, candidate, empty_context);
        result.reason = MapSelectorTrustedTransitionReason::
            live_state_not_transitionable;
        record_live_state(live_guard, result);
        return result;
    }

    result.trusted_before = trusted_generation.inspect();
    result.trusted_generation_before =
        result.trusted_before.observed_generation;
    if (!result.trusted_before.usable()) {
        result.reason = MapSelectorTrustedTransitionReason::
            trusted_source_unavailable;
        result.reconciliation_required =
            result.trusted_before.reconciliation_required;
        contain_map_exposure(
            policy,
            result.reconciliation_required,
            result,
            live_guard);
        return result;
    }

    MapActivationGuard candidate = live_guard;
    const MapSelectorTransitionContext context{
        policy,
        result.trusted_generation_before,
        result.trusted_generation_before};
    result.transition = call(transition, candidate, context);

    if (transition_failed(result.transition)) {
        result.reason =
            MapSelectorTrustedTransitionReason::transition_failed;
        result.reconciliation_required =
            result.transition.reconciliation_required;
        publish_candidate(candidate, result, live_guard);
        return result;
    }

    if (result.transition.state ==
        MapSelectorTransitionState::mapless_committed) {
        result.reason = MapSelectorTrustedTransitionReason::
            protected_history_retained;
        result.reconciliation_required = true;
        publish_candidate(candidate, result, live_guard);
        return result;
    }

    const auto target_generation = result.transition.active_generation;
    if (target_generation < result.trusted_generation_before) {
        result.reason = MapSelectorTrustedTransitionReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    if (target_generation > result.trusted_generation_before) {
        result.trusted_after = trusted_generation.advance_exact(
            result.trusted_generation_before, target_generation);
        if (!result.trusted_after.usable()) {
            result.reason = MapSelectorTrustedTransitionReason::
                trusted_advance_failed;
            result.reconciliation_required =
                result.trusted_after.reconciliation_required;
            contain_map_exposure(policy, true, result, live_guard);
            return result;
        }
    } else {
        result.trusted_after = trusted_generation.inspect();
        if (!result.trusted_after.usable()) {
            result.reason = MapSelectorTrustedTransitionReason::
                trusted_source_unavailable;
            result.reconciliation_required =
                result.trusted_after.reconciliation_required;
            contain_map_exposure(
                policy,
                result.reconciliation_required,
                result,
                live_guard);
            return result;
        }
    }

    result.trusted_generation_after =
        result.trusted_after.observed_generation;
    if (result.trusted_generation_after != target_generation) {
        result.reason = MapSelectorTrustedTransitionReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    publish_candidate(candidate, result, live_guard);
    result.reason = MapSelectorTrustedTransitionReason::none;
    result.trusted_generation_satisfied = true;
    return result;
}

}  // namespace

MapSelectorTrustedTransitionCoordinator::
    MapSelectorTrustedTransitionCoordinator(
        MapSelectorTransitionCoordinator& transition,
        MapSelectorTrustedGeneration& trusted_generation)
    : transition_(transition),
      trusted_generation_(trusted_generation) {}

MapSelectorTrustedTransitionResult
MapSelectorTrustedTransitionCoordinator::report_trial_read(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    bool complete_read_succeeded,
    std::uint64_t now_ms) {
    return run_transition(
        transition_,
        trusted_generation_,
        live_guard,
        policy,
        [complete_read_succeeded, now_ms](
            MapSelectorTransitionCoordinator& transition,
            MapActivationGuard& candidate,
            const MapSelectorTransitionContext& context) {
            return transition.report_trial_read(
                candidate,
                context,
                complete_read_succeeded,
                now_ms);
        });
}

MapSelectorTrustedTransitionResult
MapSelectorTrustedTransitionCoordinator::tick(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    std::uint64_t now_ms) {
    return run_transition(
        transition_,
        trusted_generation_,
        live_guard,
        policy,
        [now_ms](
            MapSelectorTransitionCoordinator& transition,
            MapActivationGuard& candidate,
            const MapSelectorTransitionContext& context) {
            return transition.tick(candidate, context, now_ms);
        });
}

MapSelectorTrustedTransitionResult
MapSelectorTrustedTransitionCoordinator::complete_fallback(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& restored) {
    return run_transition(
        transition_,
        trusted_generation_,
        live_guard,
        policy,
        [&restored](
            MapSelectorTransitionCoordinator& transition,
            MapActivationGuard& candidate,
            const MapSelectorTransitionContext& context) {
            return transition.complete_fallback(
                candidate, context, restored);
        });
}

MapSelectorTrustedTransitionResult
MapSelectorTrustedTransitionCoordinator::mark_previous_removed(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSlot slot,
    std::uint64_t generation) {
    return run_transition(
        transition_,
        trusted_generation_,
        live_guard,
        policy,
        [slot, generation](
            MapSelectorTransitionCoordinator& transition,
            MapActivationGuard& candidate,
            const MapSelectorTransitionContext& context) {
            return transition.mark_previous_removed(
                candidate, context, slot, generation);
        });
}

}  // namespace opentrail::maps
