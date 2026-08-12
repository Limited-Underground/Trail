#include "opentrail/map_selector_domain_boot.hpp"

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

bool clean_stopped(const MapActivationStatus& status) {
    return !status.running && status.state == MapActivationState::stopped &&
           !status.map_available && status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none &&
           status.active_generation == 0 &&
           status.previous_generation == 0 &&
           status.staged_generation == 0;
}

bool stable_active_candidate(
    const MapActivationGuard& guard,
    const MapPackageEvidence& selected) {
    const auto status = guard.status();
    return status.running && status.state == MapActivationState::active &&
           status.map_available && status.active_slot == selected.slot &&
           status.active_generation == selected.generation &&
           status.previous_slot == MapSlot::none &&
           status.previous_generation == 0 &&
           status.staged_slot == MapSlot::none &&
           status.staged_generation == 0;
}

void copy_domain_inspection(
    MapSelectorDomainBootResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
    result.domain_repair_required =
        result.domain_repair_required || inspection.recovery_required;
}

void copy_selector_load(
    MapSelectorDomainBootResult& result,
    const MapSelectorLoadResult& load) {
    result.selector_store_error = load.error;
    result.selector_slot_a = load.slot_a;
    result.selector_slot_b = load.slot_b;
    result.selector_repair_required =
        result.selector_repair_required || load.recovery_required;
}

void copy_selector_verify(
    MapSelectorDomainBootResult& result,
    const MapSelectorVerifyResult& verify) {
    result.selector_store_error = verify.error;
    result.selector_slot_a = verify.slot_a;
    result.selector_slot_b = verify.slot_b;
    result.selector_repair_required =
        result.selector_repair_required || verify.recovery_required;
}

void publish_mapless(
    const MapActivationPolicy& policy,
    MapSelectorDomainBootResult& result,
    MapActivationGuard& live_guard) {
    MapActivationGuard mapless{};
    result.containment_error = mapless.start(
        policy, {MapSelectorState::ambiguous, {}});
    if (result.containment_error == MapActivationError::none) {
        live_guard = mapless;
        result.live_guard_published = true;
    } else {
        live_guard.stop();
    }
    result.map_exposure_allowed = live_guard.status().map_available;
}

void finish_unavailable(
    MapSelectorDomainBootResult& result,
    MapSelectorDomainBootReason reason,
    bool reconciliation,
    const MapActivationPolicy& policy,
    MapActivationGuard& live_guard) {
    result.reason = reason;
    result.reconciliation_required = reconciliation;
    result.state = reconciliation
                       ? MapSelectorDomainBootState::reconciliation_required
                       : MapSelectorDomainBootState::service_required;
    publish_mapless(policy, result, live_guard);
}

}  // namespace

MapSelectorDomainBootCoordinator::MapSelectorDomainBootCoordinator(
    MapSelectorDomainStore& domain_store,
    MapSelectorStore& selector_store,
    MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainBootResult MapSelectorDomainBootCoordinator::boot(
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected_package,
    MapActivationGuard& live_guard) {
    MapSelectorDomainBootResult result{};
    result.map_exposure_allowed = live_guard.status().map_available;
    if (!clean_stopped(live_guard.status())) {
        result.reason = MapSelectorDomainBootReason::live_guard_not_clean;
        return result;
    }

    MapActivationGuard package_check{};
    result.candidate_error = package_check.start(
        policy, {MapSelectorState::valid, selected_package});
    if (result.candidate_error != MapActivationError::none) {
        if (result.candidate_error == MapActivationError::invalid_policy) {
            result.reason = MapSelectorDomainBootReason::invalid_policy;
            return result;
        }
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::package_rejected,
            false,
            policy,
            live_guard);
        return result;
    }
    if (!stable_active_candidate(package_check, selected_package)) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::package_rejected,
            false,
            policy,
            live_guard);
        return result;
    }

    const auto domain_before = domain_store_.inspect();
    copy_domain_inspection(result, domain_before);
    if (domain_before.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::domain_storage_unavailable,
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
            MapSelectorDomainBootReason::domain_not_active,
            domain_before.record_available ||
                domain_before.error !=
                    MapSelectorDomainStoreError::no_record,
            policy,
            live_guard);
        return result;
    }

    const auto domain_record = domain_before.record;
    result.selector_generation =
        domain_record.accepted_selector_generation;
    result.domain_record_generation = domain_record.record_generation;
    result.domain_epoch = domain_record.domain_epoch;

    auto source_before = protected_source_.read();
    source_before.error = sanitize_source_error(source_before.error);
    result.protected_source_error = source_before.error;
    result.protected_source_state_before = source_before.state;
    if (source_before.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::protected_source_unavailable,
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
            MapSelectorDomainBootReason::protected_source_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.domain != domain_record.current_domain) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::protected_domain_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.selector_generation !=
        result.selector_generation) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::protected_generation_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }

    MapActivationGuard candidate{};
    result.selector_load = selector_store_.restore_at_or_above(
        candidate,
        policy,
        selected_package,
        {},
        0,
        result.selector_generation);
    copy_selector_load(result, result.selector_load);
    if (result.selector_load.error ==
        MapSelectorStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (!result.selector_load.restored ||
        result.selector_load.error != MapSelectorStoreError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_restore_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    if (result.selector_load.generation != result.selector_generation) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_generation_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!stable_active_candidate(candidate, selected_package)) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_not_stable,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_restored = true;

    const auto domain_after = domain_store_.inspect();
    copy_domain_inspection(result, domain_after);
    if (domain_after.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::domain_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (domain_after.error != MapSelectorDomainStoreError::none ||
        !domain_after.record_available ||
        !exact_record(domain_record, domain_after.record)) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.domain_verified = true;

    result.selector_verify = selector_store_.verify_current(
        candidate, result.selector_generation);
    copy_selector_verify(result, result.selector_verify);
    if (result.selector_verify.error ==
        MapSelectorStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (!result.selector_verify.exact_match ||
        result.selector_verify.error != MapSelectorStoreError::none ||
        result.selector_verify.generation != result.selector_generation) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::selector_verification_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_verified = true;

    auto source_after = protected_source_.read();
    source_after.error = sanitize_source_error(source_after.error);
    result.protected_source_error = source_after.error;
    result.protected_source_state_after = source_after.state;
    if (source_after.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::protected_source_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (!exact_source(
            source_after,
            domain_record.current_domain,
            result.selector_generation)) {
        finish_unavailable(
            result,
            MapSelectorDomainBootReason::final_source_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_source_verified = true;

    live_guard = candidate;
    result.live_guard_published = true;
    result.map_exposure_allowed = live_guard.status().map_available;
    result.state = MapSelectorDomainBootState::active_ready;
    result.reason = MapSelectorDomainBootReason::none;
    result.reconciliation_required = false;
    return result;
}

}  // namespace opentrail::maps
