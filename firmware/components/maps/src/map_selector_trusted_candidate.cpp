#include "opentrail/map_selector_trusted_candidate.hpp"

namespace opentrail::maps {
namespace {

bool candidate_baseline(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::active;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorTrustedCandidateResult& result) {
    const auto status = live_guard.status();
    result.map_exposure_allowed = status.map_available;
    result.live_guard_mapless =
        status.running && status.state == MapActivationState::mapless;
}

void publish_candidate(
    const MapActivationGuard& candidate,
    MapSelectorTrustedCandidateResult& result,
    MapActivationGuard& live_guard) {
    live_guard = candidate;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorTrustedCandidateResult& result,
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

bool candidate_failed(const MapSelectorCandidateResult& candidate) {
    return candidate.state == MapSelectorCandidateState::service_required ||
           candidate.state ==
               MapSelectorCandidateState::reconciliation_required;
}

}  // namespace

MapSelectorTrustedCandidateCoordinator::
    MapSelectorTrustedCandidateCoordinator(
        MapSelectorCandidateCoordinator& candidate,
        MapSelectorTrustedGeneration& trusted_generation)
    : candidate_(candidate),
      trusted_generation_(trusted_generation) {}

MapSelectorTrustedCandidateResult
MapSelectorTrustedCandidateCoordinator::activate(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& candidate_package,
    std::uint64_t now_ms) {
    MapSelectorTrustedCandidateResult result{};
    if (!candidate_baseline(live_guard.status())) {
        MapActivationGuard private_guard = live_guard;
        const MapSelectorCandidateContext empty_context{policy, 0, 0};
        result.candidate = candidate_.activate(
            private_guard,
            empty_context,
            candidate_package,
            now_ms);
        result.reason = MapSelectorTrustedCandidateReason::
            live_state_not_candidate_baseline;
        record_live_state(live_guard, result);
        return result;
    }

    result.trusted_before = trusted_generation_.inspect();
    result.trusted_generation_before =
        result.trusted_before.observed_generation;
    if (!result.trusted_before.usable()) {
        result.reason = MapSelectorTrustedCandidateReason::
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

    MapActivationGuard private_guard = live_guard;
    const MapSelectorCandidateContext context{
        policy,
        result.trusted_generation_before,
        result.trusted_generation_before};
    result.candidate = candidate_.activate(
        private_guard, context, candidate_package, now_ms);

    if (candidate_failed(result.candidate)) {
        result.reason =
            MapSelectorTrustedCandidateReason::candidate_failed;
        result.reconciliation_required =
            result.candidate.reconciliation_required;
        publish_candidate(private_guard, result, live_guard);
        return result;
    }

    if (result.candidate.state == MapSelectorCandidateState::rejected) {
        result.trusted_after = trusted_generation_.inspect();
        if (!result.trusted_after.usable()) {
            result.reason = MapSelectorTrustedCandidateReason::
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
        result.trusted_generation_after =
            result.trusted_after.observed_generation;
        if (result.trusted_generation_after !=
            result.trusted_generation_before) {
            result.reason = MapSelectorTrustedCandidateReason::
                trusted_source_changed;
            result.reconciliation_required = true;
            contain_map_exposure(policy, true, result, live_guard);
            return result;
        }

        result.reason = MapSelectorTrustedCandidateReason::none;
        result.trusted_generation_satisfied = true;
        record_live_state(live_guard, result);
        return result;
    }

    if (!result.candidate.committed()) {
        result.reason = MapSelectorTrustedCandidateReason::candidate_failed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    const auto target_generation =
        result.candidate.active_record_generation;
    if (target_generation <= result.trusted_generation_before) {
        result.reason = MapSelectorTrustedCandidateReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    result.trusted_after = trusted_generation_.advance_exact(
        result.trusted_generation_before, target_generation);
    if (!result.trusted_after.usable()) {
        result.reason = MapSelectorTrustedCandidateReason::
            trusted_advance_failed;
        // Selector persistence already completed. Any inability to confirm the
        // matching protected value now requires fresh-boot reconciliation,
        // including a pre-write conflict reported by the source.
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    result.trusted_generation_after =
        result.trusted_after.observed_generation;
    if (result.trusted_generation_after != target_generation) {
        result.reason = MapSelectorTrustedCandidateReason::
            trusted_source_changed;
        result.reconciliation_required = true;
        contain_map_exposure(policy, true, result, live_guard);
        return result;
    }

    publish_candidate(private_guard, result, live_guard);
    result.reason = MapSelectorTrustedCandidateReason::none;
    result.trusted_generation_satisfied = true;
    return result;
}

}  // namespace opentrail::maps
