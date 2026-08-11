#include "opentrail/map_selector_domain_activation.hpp"

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

bool coherent_source_read(
    const MapSelectorDomainProtectedSourceRead& read) {
    if (read.error != MapSelectorDomainProtectedSourceError::none) {
        return false;
    }
    if (read.state ==
        MapSelectorDomainProtectedSourceState::uninitialized) {
        return !map_selector_domain_id_nonzero(read.domain) &&
               read.selector_generation == 0;
    }
    if (read.state == MapSelectorDomainProtectedSourceState::ready) {
        return map_selector_domain_id_nonzero(read.domain);
    }
    return false;
}

bool exact_source(
    const MapSelectorDomainProtectedSourceRead& read,
    const MapSelectorDomainId& domain,
    std::uint64_t generation) {
    return coherent_source_read(read) &&
           read.state == MapSelectorDomainProtectedSourceState::ready &&
           read.domain == domain &&
           read.selector_generation == generation;
}

bool safe_map_unavailable(const MapActivationStatus& status) {
    const bool stopped =
        !status.running && status.state == MapActivationState::stopped;
    const bool mapless =
        status.running && status.state == MapActivationState::mapless;
    return (stopped || mapless) && !status.map_available &&
           status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none &&
           status.active_generation == 0 &&
           status.previous_generation == 0 &&
           status.staged_generation == 0;
}

bool exactly_empty_selector(
    const MapSelectorInspectionResult& inspection) {
    return inspection.error == MapSelectorStoreError::no_checkpoint &&
           inspection.slot_a == MapSelectorSlotState::empty &&
           inspection.slot_b == MapSelectorSlotState::empty &&
           !inspection.checkpoint_available;
}

bool pending_record_state(MapSelectorDomainRecordState state) {
    return state ==
               MapSelectorDomainRecordState::pending_first_baseline ||
           state ==
               MapSelectorDomainRecordState::pending_selector_reseed;
}

void copy_domain_inspection(
    MapSelectorDomainActivationResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
}

void copy_selector_inspection(
    MapSelectorDomainActivationResult& result,
    const MapSelectorInspectionResult& inspection) {
    result.selector_store_error = inspection.error;
    result.selector_slot_a = inspection.slot_a;
    result.selector_slot_b = inspection.slot_b;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorDomainActivationResult& result) {
    result.map_exposure_allowed = live_guard.status().map_available;
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    MapSelectorDomainActivationResult& result,
    MapActivationGuard& live_guard) {
    MapActivationGuard mapless{};
    result.containment_error = mapless.start(
        policy, {MapSelectorState::ambiguous, {}});
    if (result.containment_error == MapActivationError::none) {
        live_guard = mapless;
    } else {
        live_guard.stop();
    }
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
}

void finish_service(
    MapSelectorDomainActivationResult& result,
    MapSelectorDomainActivationReason reason,
    bool reconciliation,
    const MapActivationPolicy& policy,
    MapActivationGuard& live_guard) {
    result.reason = reason;
    result.reconciliation_required = reconciliation;
    result.state = reconciliation
                       ? MapSelectorDomainActivationState::
                             reconciliation_required
                       : MapSelectorDomainActivationState::service_required;
    if (reconciliation) {
        contain_map_exposure(policy, result, live_guard);
    } else {
        record_live_state(live_guard, result);
    }
}

MapSelectorDomainActivationReason selector_save_reason(
    const MapSelectorSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error == MapSelectorStoreError::verification_failure
                   ? MapSelectorDomainActivationReason::
                         selector_verification_failed
                   : MapSelectorDomainActivationReason::
                         selector_commit_uncertain;
    }
    if (save.error == MapSelectorStoreError::generation_exhausted) {
        return MapSelectorDomainActivationReason::
            selector_generation_exhausted;
    }
    if (save.error == MapSelectorStoreError::state_mismatch) {
        return MapSelectorDomainActivationReason::selector_changed;
    }
    if (save.error == MapSelectorStoreError::verification_failure) {
        return MapSelectorDomainActivationReason::
            selector_verification_failed;
    }
    return MapSelectorDomainActivationReason::selector_save_failed;
}

MapSelectorDomainActivationReason domain_save_reason(
    const MapSelectorDomainSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error ==
                       MapSelectorDomainStoreError::verification_failure
                   ? MapSelectorDomainActivationReason::
                         domain_verification_failed
                   : MapSelectorDomainActivationReason::
                         domain_commit_uncertain;
    }
    if (save.error ==
        MapSelectorDomainStoreError::generation_exhausted) {
        return MapSelectorDomainActivationReason::
            domain_generation_exhausted;
    }
    if (save.error == MapSelectorDomainStoreError::generation_mismatch ||
        save.error == MapSelectorDomainStoreError::transition_rejected ||
        save.error == MapSelectorDomainStoreError::generation_conflict ||
        save.error == MapSelectorDomainStoreError::invalid_state) {
        return MapSelectorDomainActivationReason::domain_changed;
    }
    if (save.error ==
        MapSelectorDomainStoreError::verification_failure) {
        return MapSelectorDomainActivationReason::
            domain_verification_failed;
    }
    return MapSelectorDomainActivationReason::domain_save_failed;
}

bool active_candidate(const MapActivationGuard& guard) {
    const auto status = guard.status();
    return status.running && status.state == MapActivationState::active &&
           status.map_available && status.active_slot != MapSlot::none &&
           status.active_generation != 0 &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none;
}

}  // namespace

MapSelectorDomainActivationCoordinator::
    MapSelectorDomainActivationCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainActivationResult
MapSelectorDomainActivationCoordinator::activate(
    MapActivationGuard& live_guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& baseline_package) {
    MapSelectorDomainActivationResult result{};
    const auto live_before = live_guard.status();
    record_live_state(live_guard, result);
    if (!safe_map_unavailable(live_before)) {
        result.reason = MapSelectorDomainActivationReason::
            map_unavailable_required;
        return result;
    }
    if (live_before.running && !live_guard.matches_policy(policy)) {
        result.reason = MapSelectorDomainActivationReason::invalid_policy;
        return result;
    }

    MapActivationGuard candidate_check{};
    result.candidate_error = candidate_check.start(
        policy, {MapSelectorState::valid, baseline_package});
    if (result.candidate_error != MapActivationError::none) {
        result.reason = result.candidate_error ==
                                MapActivationError::invalid_policy
                            ? MapSelectorDomainActivationReason::invalid_policy
                            : MapSelectorDomainActivationReason::
                                  candidate_rejected;
        return result;
    }
    if (!active_candidate(candidate_check)) {
        result.reason =
            MapSelectorDomainActivationReason::candidate_rejected;
        return result;
    }

    const auto domain_inspection = domain_store_.inspect();
    copy_domain_inspection(result, domain_inspection);
    if (domain_inspection.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::domain_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (domain_inspection.error != MapSelectorDomainStoreError::none ||
        !domain_inspection.record_available) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::domain_state_mismatch,
            false,
            policy,
            live_guard);
        return result;
    }

    const auto& domain_record = domain_inspection.record;
    const bool pending = pending_record_state(domain_record.state);
    const bool already_active =
        domain_record.state == MapSelectorDomainRecordState::active;
    if (!pending && !already_active) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::domain_state_mismatch,
            false,
            policy,
            live_guard);
        return result;
    }

    result.resumed_active_domain = already_active;
    result.domain_epoch = domain_record.domain_epoch;
    result.retired_selector_floor =
        domain_record.retired_selector_generation;
    result.domain_record_generation = domain_record.record_generation;
    if (pending) {
        if (domain_record.record_generation ==
                std::numeric_limits<std::uint64_t>::max() ||
            domain_record.retired_selector_generation ==
                std::numeric_limits<std::uint64_t>::max()) {
            finish_service(
                result,
                MapSelectorDomainActivationReason::
                    domain_generation_exhausted,
                true,
                policy,
                live_guard);
            return result;
        }
        result.selector_generation =
            domain_record.retired_selector_generation + 1;
    } else {
        result.selector_generation =
            domain_record.accepted_selector_generation;
        result.active_domain_verified = true;
    }

    const auto selector_inspection = selector_store_.inspect();
    copy_selector_inspection(result, selector_inspection);
    if (selector_inspection.error ==
        MapSelectorStoreError::storage_failure) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::
                selector_storage_unavailable,
            true,
            policy,
            live_guard);
        return result;
    }

    const bool selector_empty =
        exactly_empty_selector(selector_inspection);
    const bool selector_resumable =
        selector_inspection.error == MapSelectorStoreError::none &&
        selector_inspection.checkpoint_available &&
        selector_inspection.generation == result.selector_generation;
    if (already_active && !selector_resumable) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::selector_state_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if (pending && !selector_empty && !selector_resumable) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::selector_state_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }

    MapActivationGuard candidate{};
    if (selector_resumable) {
        result.selector_load = selector_store_.restore_at_or_above(
            candidate,
            policy,
            baseline_package,
            {},
            0,
            result.selector_generation);
        result.selector_store_error = result.selector_load.error;
        result.selector_slot_a = result.selector_load.slot_a;
        result.selector_slot_b = result.selector_load.slot_b;
        if (!result.selector_load.restored ||
            result.selector_load.error != MapSelectorStoreError::none ||
            result.selector_load.generation !=
                result.selector_generation ||
            !active_candidate(candidate)) {
            finish_service(
                result,
                MapSelectorDomainActivationReason::selector_restore_failed,
                true,
                policy,
                live_guard);
            return result;
        }
        result.resumed_selector = true;
        result.selector_persisted = true;
    } else {
        candidate = candidate_check;
    }

    auto source_before = protected_source_.read();
    source_before.error = sanitize_source_error(source_before.error);
    result.protected_source_error = source_before.error;
    result.protected_source_state_before = source_before.state;
    if (source_before.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::protected_source_unavailable,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!coherent_source_read(source_before)) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::protected_source_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.state !=
            MapSelectorDomainProtectedSourceState::ready ||
        source_before.domain != domain_record.current_domain) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::protected_source_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if ((selector_empty && source_before.selector_generation != 0) ||
        (selector_resumable &&
         source_before.selector_generation != 0 &&
         source_before.selector_generation !=
             result.selector_generation)) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::protected_source_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }

    if (selector_empty) {
        result.selector_save = selector_store_.save_if_empty(
            candidate, domain_record.retired_selector_generation);
        result.selector_store_error = result.selector_save.error;
        result.selector_slot_a = result.selector_save.slot_a;
        result.selector_slot_b = result.selector_save.slot_b;
        if (!result.selector_save.saved() ||
            result.selector_save.generation !=
                result.selector_generation) {
            finish_service(
                result,
                selector_save_reason(result.selector_save),
                true,
                policy,
                live_guard);
            return result;
        }
        result.selector_persisted = true;
    }

    if (source_before.selector_generation == 0) {
        result.protected_advance_called = true;
        result.protected_source_error = sanitize_source_error(
            protected_source_.advance_selector_generation(
                {domain_record.current_domain,
                 0,
                 result.selector_generation}));
        if (result.protected_source_error !=
            MapSelectorDomainProtectedSourceError::none) {
            finish_service(
                result,
                MapSelectorDomainActivationReason::
                    protected_advance_failed,
                true,
                policy,
                live_guard);
            return result;
        }
    }

    auto source_after = protected_source_.read();
    source_after.error = sanitize_source_error(source_after.error);
    result.protected_source_error = source_after.error;
    result.protected_source_state_after = source_after.state;
    if (source_after.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::protected_readback_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!exact_source(
            source_after,
            domain_record.current_domain,
            result.selector_generation)) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::
                protected_readback_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_source_verified = true;

    result.selector_verify = selector_store_.verify_current(
        candidate, result.selector_generation);
    result.selector_store_error = result.selector_verify.error;
    result.selector_slot_a = result.selector_verify.slot_a;
    result.selector_slot_b = result.selector_verify.slot_b;
    if (!result.selector_verify.exact_match ||
        result.selector_verify.error != MapSelectorStoreError::none ||
        result.selector_verify.generation != result.selector_generation) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_verified = true;

    if (pending) {
        auto active_record = domain_record;
        active_record.state = MapSelectorDomainRecordState::active;
        active_record.accepted_selector_generation =
            result.selector_generation;
        ++active_record.record_generation;
        result.domain_save = domain_store_.save(active_record);
        result.domain_store_error = result.domain_save.error;
        result.domain_slot_a = result.domain_save.slot_a;
        result.domain_slot_b = result.domain_save.slot_b;
        if (!result.domain_save.saved()) {
            finish_service(
                result,
                domain_save_reason(result.domain_save),
                true,
                policy,
                live_guard);
            return result;
        }
        result.active_domain_saved = true;
        result.active_domain_verified = true;
        result.domain_record_generation =
            active_record.record_generation;
    }

    auto source_final = protected_source_.read();
    source_final.error = sanitize_source_error(source_final.error);
    result.protected_source_error = source_final.error;
    result.protected_source_state_after = source_final.state;
    if (!exact_source(
            source_final,
            domain_record.current_domain,
            result.selector_generation)) {
        finish_service(
            result,
            MapSelectorDomainActivationReason::final_source_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    live_guard = candidate;
    result.live_guard_updated = true;
    record_live_state(live_guard, result);
    result.state = MapSelectorDomainActivationState::activated;
    result.reason = MapSelectorDomainActivationReason::none;
    result.reconciliation_required = false;
    return result;
}

}  // namespace opentrail::maps
