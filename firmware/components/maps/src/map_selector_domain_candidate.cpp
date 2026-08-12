#include "opentrail/map_selector_domain_candidate.hpp"

#include <limits>

namespace opentrail::maps {
namespace {

bool known_source_error(MapSelectorDomainProtectedSourceError error) {
    switch (error) {
        case MapSelectorDomainProtectedSourceError::none:
        case MapSelectorDomainProtectedSourceError::not_ready:
        case MapSelectorDomainProtectedSourceError::io_failure:
        case MapSelectorDomainProtectedSourceError::invalid_state:
        case MapSelectorDomainProtectedSourceError::rejected:
        case MapSelectorDomainProtectedSourceError::conflict:
            return true;
    }
    return false;
}

MapSelectorDomainProtectedSourceError sanitize_source_error(
    MapSelectorDomainProtectedSourceError error) {
    return known_source_error(error)
               ? error
               : MapSelectorDomainProtectedSourceError::invalid_state;
}

bool exact_source(
    const MapSelectorDomainProtectedSourceRead& read,
    const MapSelectorDomainId& domain,
    std::uint64_t generation) {
    return read.error == MapSelectorDomainProtectedSourceError::none &&
           read.state == MapSelectorDomainProtectedSourceState::ready &&
           map_selector_domain_id_nonzero(read.domain) &&
           read.domain == domain &&
           read.selector_generation == generation;
}

bool exact_record(
    const MapSelectorDomainRecord& left,
    const MapSelectorDomainRecord& right) {
    return left.version == right.version && left.state == right.state &&
           left.origin == right.origin &&
           left.current_domain == right.current_domain &&
           left.retired_domain == right.retired_domain &&
           left.retired_selector_generation ==
               right.retired_selector_generation &&
           left.accepted_selector_generation ==
               right.accepted_selector_generation &&
           left.domain_epoch == right.domain_epoch &&
           left.record_generation == right.record_generation;
}

bool stable_live_baseline(const MapActivationStatus& status) {
    return status.running && status.state == MapActivationState::active &&
           status.map_available && status.active_slot != MapSlot::none &&
           status.active_generation != 0 &&
           status.previous_slot == MapSlot::none &&
           status.previous_generation == 0 &&
           status.staged_slot == MapSlot::none &&
           status.staged_generation == 0;
}

bool trial_candidate(const MapActivationGuard& guard) {
    const auto status = guard.status();
    return status.running && status.state == MapActivationState::trial &&
           status.map_available && status.active_slot != MapSlot::none &&
           status.active_generation != 0 &&
           status.previous_slot != MapSlot::none &&
           status.previous_generation != 0 &&
           status.active_slot != status.previous_slot &&
           status.staged_slot == MapSlot::none &&
           status.staged_generation == 0;
}

void copy_domain_inspection(
    MapSelectorDomainCandidateResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
    result.domain_repair_required =
        result.domain_repair_required || inspection.recovery_required;
}

void copy_selector_verify(
    MapSelectorDomainCandidateResult& result,
    const MapSelectorVerifyResult& verify) {
    result.selector_repair_required =
        result.selector_repair_required || verify.recovery_required;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorDomainCandidateResult& result) {
    result.map_exposure_allowed = live_guard.status().map_available;
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorDomainCandidateResult& result,
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

void finish_unavailable(
    MapSelectorDomainCandidateResult& result,
    MapSelectorDomainCandidateReason reason,
    bool reconciliation,
    const MapActivationPolicy& policy,
    MapActivationGuard& live_guard) {
    result.reason = reason;
    result.reconciliation_required = reconciliation;
    result.state = reconciliation
                       ? MapSelectorDomainCandidateState::
                             reconciliation_required
                       : MapSelectorDomainCandidateState::service_required;
    contain_map_exposure(
        policy, reconciliation, result, live_guard);
}

MapSelectorDomainCandidateReason domain_save_reason(
    const MapSelectorDomainSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error ==
                       MapSelectorDomainStoreError::verification_failure
                   ? MapSelectorDomainCandidateReason::
                         domain_verification_failed
                   : MapSelectorDomainCandidateReason::
                         domain_commit_uncertain;
    }
    if (save.error ==
        MapSelectorDomainStoreError::verification_failure) {
        return MapSelectorDomainCandidateReason::
            domain_verification_failed;
    }
    return MapSelectorDomainCandidateReason::domain_save_failed;
}

bool exact_selector(
    const MapSelectorVerifyResult& verify,
    std::uint64_t generation) {
    return verify.error == MapSelectorStoreError::none &&
           verify.exact_match && verify.generation == generation;
}

}  // namespace

MapSelectorDomainCandidateCoordinator::
    MapSelectorDomainCandidateCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainCandidateResult
MapSelectorDomainCandidateCoordinator::activate(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& candidate_package,
    std::uint64_t now_ms) {
    MapSelectorDomainCandidateResult result{};
    record_live_state(live_guard, result);
    if (!stable_live_baseline(live_guard.status())) {
        result.reason =
            MapSelectorDomainCandidateReason::live_baseline_required;
        return result;
    }
    if (!live_guard.matches_policy(policy)) {
        result.reason = MapSelectorDomainCandidateReason::invalid_policy;
        return result;
    }

    const auto domain_before = domain_store_.inspect();
    copy_domain_inspection(result, domain_before);
    if (domain_before.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                domain_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (domain_before.error != MapSelectorDomainStoreError::none ||
        !domain_before.record_available ||
        domain_before.record.state !=
            MapSelectorDomainRecordState::active) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::domain_not_active,
            domain_before.record_available ||
                domain_before.error !=
                    MapSelectorDomainStoreError::no_record,
            policy,
            live_guard);
        return result;
    }

    const auto domain_record = domain_before.record;
    result.prior_selector_generation =
        domain_record.accepted_selector_generation;
    result.domain_record_generation = domain_record.record_generation;
    result.domain_epoch = domain_record.domain_epoch;
    if (result.prior_selector_generation ==
            std::numeric_limits<std::uint64_t>::max() ||
        result.domain_record_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                domain_generation_exhausted,
            false,
            policy,
            live_guard);
        return result;
    }

    auto source_before = protected_source_.read();
    source_before.error = sanitize_source_error(source_before.error);
    result.protected_source_error = source_before.error;
    result.protected_source_state_before = source_before.state;
    if (source_before.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                protected_source_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (source_before.state !=
            MapSelectorDomainProtectedSourceState::ready ||
        !map_selector_domain_id_nonzero(source_before.domain)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::protected_source_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.domain != domain_record.current_domain) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::protected_domain_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.selector_generation !=
        result.prior_selector_generation) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                protected_generation_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }

    MapActivationGuard private_guard = live_guard;
    MapSelectorCandidateCoordinator candidate_coordinator{
        selector_store_};
    result.candidate = candidate_coordinator.activate(
        private_guard,
        {policy,
         result.prior_selector_generation,
         result.prior_selector_generation},
        candidate_package,
        now_ms);
    result.selector_repair_required =
        result.candidate.repair_required;

    if (result.candidate.state == MapSelectorCandidateState::rejected) {
        const auto domain_after_rejection = domain_store_.inspect();
        copy_domain_inspection(result, domain_after_rejection);
        if (domain_after_rejection.error ==
            MapSelectorDomainStoreError::storage_failure) {
            finish_unavailable(
                result,
                MapSelectorDomainCandidateReason::
                    domain_storage_unavailable,
                false,
                policy,
                live_guard);
            return result;
        }
        if (domain_after_rejection.error !=
                MapSelectorDomainStoreError::none ||
            !domain_after_rejection.record_available ||
            !exact_record(
                domain_record, domain_after_rejection.record)) {
            finish_unavailable(
                result,
                MapSelectorDomainCandidateReason::domain_changed,
                true,
                policy,
                live_guard);
            return result;
        }
        result.domain_generation_verified = true;

        result.selector_final = selector_store_.verify_current(
            live_guard, result.prior_selector_generation);
        copy_selector_verify(result, result.selector_final);
        if (!exact_selector(
                result.selector_final,
                result.prior_selector_generation)) {
            finish_unavailable(
                result,
                MapSelectorDomainCandidateReason::final_selector_changed,
                true,
                policy,
                live_guard);
            return result;
        }
        result.selector_verified = true;

        auto source_after_rejection = protected_source_.read();
        source_after_rejection.error = sanitize_source_error(
            source_after_rejection.error);
        result.protected_source_error = source_after_rejection.error;
        result.protected_source_state_after = source_after_rejection.state;
        if (!exact_source(
                source_after_rejection,
                domain_record.current_domain,
                result.prior_selector_generation)) {
            finish_unavailable(
                result,
                MapSelectorDomainCandidateReason::final_source_changed,
                true,
                policy,
                live_guard);
            return result;
        }
        result.protected_source_verified = true;
        result.state = MapSelectorDomainCandidateState::rejected;
        result.reason =
            MapSelectorDomainCandidateReason::candidate_rejected;
        record_live_state(live_guard, result);
        return result;
    }

    if (!result.candidate.committed() ||
        !trial_candidate(private_guard)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::candidate_failed,
            result.candidate.reconciliation_required,
            policy,
            live_guard);
        return result;
    }

    result.trial_selector_generation =
        result.candidate.active_record_generation;
    if (result.trial_selector_generation !=
        result.prior_selector_generation + 1) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_persisted = true;

    const auto domain_before_advance = domain_store_.inspect();
    copy_domain_inspection(result, domain_before_advance);
    if (domain_before_advance.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                domain_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (domain_before_advance.error !=
            MapSelectorDomainStoreError::none ||
        !domain_before_advance.record_available ||
        !exact_record(domain_record, domain_before_advance.record)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    result.selector_before_protected =
        selector_store_.verify_current(
            private_guard, result.trial_selector_generation);
    copy_selector_verify(result, result.selector_before_protected);
    if (!exact_selector(
            result.selector_before_protected,
            result.trial_selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    result.protected_advance_called = true;
    result.protected_source_error = sanitize_source_error(
        protected_source_.advance_selector_generation(
            {domain_record.current_domain,
             result.prior_selector_generation,
             result.trial_selector_generation}));
    if (result.protected_source_error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::protected_advance_failed,
            true,
            policy,
            live_guard);
        return result;
    }

    auto source_after = protected_source_.read();
    source_after.error = sanitize_source_error(source_after.error);
    result.protected_source_error = source_after.error;
    result.protected_source_state_after = source_after.state;
    if (source_after.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::protected_readback_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!exact_source(
            source_after,
            domain_record.current_domain,
            result.trial_selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::
                protected_readback_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_source_verified = true;

    result.selector_after_protected =
        selector_store_.verify_current(
            private_guard, result.trial_selector_generation);
    copy_selector_verify(result, result.selector_after_protected);
    if (!exact_selector(
            result.selector_after_protected,
            result.trial_selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    auto updated_domain = domain_record;
    updated_domain.accepted_selector_generation =
        result.trial_selector_generation;
    ++updated_domain.record_generation;
    result.domain_save = domain_store_.save(updated_domain);
    result.domain_store_error = result.domain_save.error;
    result.domain_slot_a = result.domain_save.slot_a;
    result.domain_slot_b = result.domain_save.slot_b;
    result.domain_repair_required =
        result.domain_repair_required || result.domain_save.repaired_peer;
    if (!result.domain_save.saved()) {
        finish_unavailable(
            result,
            domain_save_reason(result.domain_save),
            true,
            policy,
            live_guard);
        return result;
    }
    result.domain_generation_saved = true;
    result.domain_record_generation = updated_domain.record_generation;

    const auto domain_final = domain_store_.inspect();
    copy_domain_inspection(result, domain_final);
    if (domain_final.error != MapSelectorDomainStoreError::none ||
        !domain_final.record_available ||
        !exact_record(updated_domain, domain_final.record)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::final_domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.domain_generation_verified = true;

    result.selector_final = selector_store_.verify_current(
        private_guard, result.trial_selector_generation);
    copy_selector_verify(result, result.selector_final);
    if (!exact_selector(
            result.selector_final,
            result.trial_selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::final_selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_verified = true;

    auto source_final = protected_source_.read();
    source_final.error = sanitize_source_error(source_final.error);
    result.protected_source_error = source_final.error;
    result.protected_source_state_after = source_final.state;
    if (!exact_source(
            source_final,
            domain_record.current_domain,
            result.trial_selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainCandidateReason::final_source_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    live_guard = private_guard;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
    result.state = MapSelectorDomainCandidateState::trial_ready;
    result.reason = MapSelectorDomainCandidateReason::none;
    result.reconciliation_required = false;
    return result;
}

}  // namespace opentrail::maps
