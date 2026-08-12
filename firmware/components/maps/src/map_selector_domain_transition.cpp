#include "opentrail/map_selector_domain_transition.hpp"

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

bool transitionable_state(const MapActivationStatus& status) {
    return status.running &&
           (status.state == MapActivationState::active ||
            status.state == MapActivationState::trial ||
            status.state == MapActivationState::fallback_required);
}

bool exact_selector(
    const MapSelectorVerifyResult& verify,
    std::uint64_t generation) {
    return verify.error == MapSelectorStoreError::none &&
           verify.exact_match && verify.generation == generation;
}

void copy_domain_inspection(
    MapSelectorDomainTransitionResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
    result.domain_repair_required =
        result.domain_repair_required || inspection.recovery_required;
}

void copy_selector_verify(
    MapSelectorDomainTransitionResult& result,
    const MapSelectorVerifyResult& verify) {
    result.selector_repair_required =
        result.selector_repair_required || verify.recovery_required;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorDomainTransitionResult& result) {
    result.map_exposure_allowed = live_guard.status().map_available;
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorDomainTransitionResult& result,
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
    MapSelectorDomainTransitionResult& result,
    MapSelectorDomainTransitionReason reason,
    bool reconciliation,
    const MapActivationPolicy& policy,
    MapActivationGuard& live_guard) {
    result.reason = reason;
    result.reconciliation_required = reconciliation;
    result.state = reconciliation
                       ? MapSelectorDomainTransitionState::
                             reconciliation_required
                       : MapSelectorDomainTransitionState::service_required;
    contain_map_exposure(policy, reconciliation, result, live_guard);
}

MapSelectorDomainTransitionReason domain_save_reason(
    const MapSelectorDomainSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error ==
                       MapSelectorDomainStoreError::verification_failure
                   ? MapSelectorDomainTransitionReason::
                         domain_verification_failed
                   : MapSelectorDomainTransitionReason::
                         domain_commit_uncertain;
    }
    if (save.error ==
        MapSelectorDomainStoreError::verification_failure) {
        return MapSelectorDomainTransitionReason::
            domain_verification_failed;
    }
    return MapSelectorDomainTransitionReason::domain_save_failed;
}

}  // namespace

MapSelectorDomainTransitionCoordinator::
    MapSelectorDomainTransitionCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainTransitionResult
MapSelectorDomainTransitionCoordinator::report_trial_read(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    bool complete_read_succeeded,
    std::uint64_t now_ms) {
    return run(
        MapSelectorTransitionOperation::report_trial_read,
        live_guard,
        policy,
        complete_read_succeeded,
        now_ms,
        nullptr,
        MapSlot::none,
        0);
}

MapSelectorDomainTransitionResult
MapSelectorDomainTransitionCoordinator::tick(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    std::uint64_t now_ms) {
    return run(
        MapSelectorTransitionOperation::tick,
        live_guard,
        policy,
        false,
        now_ms,
        nullptr,
        MapSlot::none,
        0);
}

MapSelectorDomainTransitionResult
MapSelectorDomainTransitionCoordinator::complete_fallback(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& restored) {
    return run(
        MapSelectorTransitionOperation::complete_fallback,
        live_guard,
        policy,
        false,
        0,
        &restored,
        MapSlot::none,
        0);
}

MapSelectorDomainTransitionResult
MapSelectorDomainTransitionCoordinator::mark_previous_removed(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    MapSlot slot,
    std::uint64_t generation) {
    return run(
        MapSelectorTransitionOperation::mark_previous_removed,
        live_guard,
        policy,
        false,
        0,
        nullptr,
        slot,
        generation);
}

MapSelectorDomainTransitionResult
MapSelectorDomainTransitionCoordinator::run(
    MapSelectorTransitionOperation operation,
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    bool complete_read_succeeded,
    std::uint64_t now_ms,
    const MapPackageEvidence* restored,
    MapSlot removed_slot,
    std::uint64_t removed_generation) {
    MapSelectorDomainTransitionResult result{};
    result.operation = operation;
    record_live_state(live_guard, result);
    if (!transitionable_state(live_guard.status())) {
        result.reason = MapSelectorDomainTransitionReason::
            live_state_not_transitionable;
        return result;
    }
    if (!live_guard.matches_policy(policy)) {
        result.reason = MapSelectorDomainTransitionReason::invalid_policy;
        return result;
    }

    const auto domain_before = domain_store_.inspect();
    copy_domain_inspection(result, domain_before);
    if (domain_before.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
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
            MapSelectorDomainTransitionReason::domain_not_active,
            domain_before.record_available ||
                domain_before.error !=
                    MapSelectorDomainStoreError::no_record,
            policy,
            live_guard);
        return result;
    }

    const auto domain_record = domain_before.record;
    result.selector_generation_before =
        domain_record.accepted_selector_generation;
    result.domain_record_generation = domain_record.record_generation;
    result.domain_epoch = domain_record.domain_epoch;
    if (result.selector_generation_before == 0 ||
        result.selector_generation_before ==
            std::numeric_limits<std::uint64_t>::max() ||
        result.domain_record_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
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
            MapSelectorDomainTransitionReason::
                protected_source_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (source_before.state !=
            MapSelectorDomainProtectedSourceState::ready ||
        !map_selector_domain_id_nonzero(source_before.domain) ||
        source_before.selector_generation == 0) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::protected_source_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.domain != domain_record.current_domain) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::protected_domain_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.selector_generation !=
        result.selector_generation_before) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
                protected_generation_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }

    MapActivationGuard private_guard = live_guard;
    MapSelectorTransitionCoordinator transition{selector_store_};
    const MapSelectorTransitionContext context{
        policy,
        result.selector_generation_before,
        result.selector_generation_before};
    switch (operation) {
        case MapSelectorTransitionOperation::report_trial_read:
            result.transition = transition.report_trial_read(
                private_guard,
                context,
                complete_read_succeeded,
                now_ms);
            break;
        case MapSelectorTransitionOperation::tick:
            result.transition = transition.tick(
                private_guard, context, now_ms);
            break;
        case MapSelectorTransitionOperation::complete_fallback:
            if (restored == nullptr) {
                result.reason = MapSelectorDomainTransitionReason::
                    transition_rejected;
                return result;
            }
            result.transition = transition.complete_fallback(
                private_guard, context, *restored);
            break;
        case MapSelectorTransitionOperation::mark_previous_removed:
            result.transition = transition.mark_previous_removed(
                private_guard,
                context,
                removed_slot,
                removed_generation);
            break;
    }
    result.selector_repair_required = result.transition.repair_required;
    result.selector_generation_after =
        result.transition.active_generation;

    if (result.transition.state ==
            MapSelectorTransitionState::service_required ||
        result.transition.state ==
            MapSelectorTransitionState::reconciliation_required) {
        live_guard = private_guard;
        result.live_guard_updated = true;
        result.reconciliation_required =
            result.transition.reconciliation_required;
        result.state = result.reconciliation_required
                           ? MapSelectorDomainTransitionState::
                                 reconciliation_required
                           : MapSelectorDomainTransitionState::
                                 service_required;
        result.reason = MapSelectorDomainTransitionReason::
            transition_failed;
        record_live_state(live_guard, result);
        return result;
    }

    if (result.transition.state ==
        MapSelectorTransitionState::mapless_committed) {
        live_guard = private_guard;
        result.live_guard_updated = true;
        result.state = MapSelectorDomainTransitionState::service_required;
        result.reason = MapSelectorDomainTransitionReason::
            protected_history_retained;
        result.reconciliation_required = true;
        record_live_state(live_guard, result);
        return result;
    }

    const bool persistent = result.transition.committed();
    const bool volatile_applied =
        result.transition.state ==
        MapSelectorTransitionState::applied_volatile;
    const bool safely_rejected =
        result.transition.state == MapSelectorTransitionState::rejected;
    if (!persistent && !volatile_applied && !safely_rejected) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::transition_failed,
            result.transition.reconciliation_required,
            policy,
            live_guard);
        return result;
    }

    const auto target_generation =
        persistent ? result.transition.active_generation
                   : result.selector_generation_before;
    result.selector_generation_after = target_generation;
    if ((persistent &&
         target_generation != result.selector_generation_before + 1) ||
        (!persistent &&
         target_generation != result.selector_generation_before)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
                transition_generation_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_persisted = persistent;

    const auto domain_before_advance = domain_store_.inspect();
    copy_domain_inspection(result, domain_before_advance);
    if (domain_before_advance.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
                domain_storage_unavailable,
            persistent,
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
            MapSelectorDomainTransitionReason::domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    result.selector_before_protected = selector_store_.verify_current(
        private_guard, target_generation);
    copy_selector_verify(result, result.selector_before_protected);
    if (!exact_selector(
            result.selector_before_protected, target_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    if (safely_rejected || volatile_applied) {
        auto source_final = protected_source_.read();
        source_final.error = sanitize_source_error(source_final.error);
        result.protected_source_error = source_final.error;
        result.protected_source_state_after = source_final.state;
        if (!exact_source(
                source_final,
                domain_record.current_domain,
                target_generation)) {
            finish_unavailable(
                result,
                MapSelectorDomainTransitionReason::final_source_changed,
                true,
                policy,
                live_guard);
            return result;
        }
        result.protected_source_verified = true;
        result.domain_generation_verified = true;
        result.selector_verified = true;
        if (volatile_applied) {
            live_guard = private_guard;
            result.live_guard_updated = true;
            result.state =
                MapSelectorDomainTransitionState::applied_volatile;
            result.reason = MapSelectorDomainTransitionReason::none;
        } else {
            result.state = MapSelectorDomainTransitionState::rejected;
            result.reason = MapSelectorDomainTransitionReason::
                transition_rejected;
        }
        record_live_state(live_guard, result);
        return result;
    }

    result.protected_advance_called = true;
    result.protected_source_error = sanitize_source_error(
        protected_source_.advance_selector_generation(
            {domain_record.current_domain,
             result.selector_generation_before,
             target_generation}));
    if (result.protected_source_error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::protected_advance_failed,
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
            MapSelectorDomainTransitionReason::protected_readback_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!exact_source(
            source_after,
            domain_record.current_domain,
            target_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::
                protected_readback_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_source_verified = true;

    result.selector_after_protected = selector_store_.verify_current(
        private_guard, target_generation);
    copy_selector_verify(result, result.selector_after_protected);
    if (!exact_selector(
            result.selector_after_protected, target_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    auto updated_domain = domain_record;
    updated_domain.accepted_selector_generation = target_generation;
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
            MapSelectorDomainTransitionReason::final_domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.domain_generation_verified = true;

    result.selector_final = selector_store_.verify_current(
        private_guard, target_generation);
    copy_selector_verify(result, result.selector_final);
    if (!exact_selector(result.selector_final, target_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::final_selector_changed,
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
            target_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainTransitionReason::final_source_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    live_guard = private_guard;
    result.live_guard_updated = true;
    result.state = MapSelectorDomainTransitionState::committed;
    result.reason = MapSelectorDomainTransitionReason::none;
    result.reconciliation_required = false;
    record_live_state(live_guard, result);
    return result;
}

}  // namespace opentrail::maps
