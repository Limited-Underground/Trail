#include "opentrail/map_selector_domain_trial_boot.hpp"

#include <algorithm>
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

bool clean_stopped(const MapActivationStatus& status) {
    return !status.running && status.state == MapActivationState::stopped &&
           !status.map_available && status.active_slot == MapSlot::none &&
           status.previous_slot == MapSlot::none &&
           status.staged_slot == MapSlot::none &&
           status.active_generation == 0 &&
           status.previous_generation == 0 &&
           status.staged_generation == 0;
}

bool exact_selector(
    const MapSelectorVerifyResult& verify,
    std::uint64_t generation) {
    return verify.error == MapSelectorStoreError::none &&
           verify.exact_match && verify.generation == generation;
}

bool expected_private_state(
    const MapSelectorBootResult& boot,
    const MapActivationGuard& guard) {
    const auto status = guard.status();
    if (!boot.operational() || !status.running ||
        status.staged_slot != MapSlot::none ||
        status.staged_generation != 0) {
        return false;
    }
    if (boot.state == MapSelectorBootState::active_ready) {
        return status.state == MapActivationState::active &&
               status.map_available &&
               status.active_slot != MapSlot::none &&
               status.active_generation != 0;
    }
    if (boot.state == MapSelectorBootState::trial_ready) {
        return status.state == MapActivationState::trial &&
               status.map_available && status.active_slot != MapSlot::none &&
               status.previous_slot != MapSlot::none &&
               status.active_slot != status.previous_slot;
    }
    if (boot.state == MapSelectorBootState::fallback_required) {
        return status.state == MapActivationState::fallback_required &&
               !status.map_available &&
               status.unavailable_notice_required &&
               status.active_slot != MapSlot::none &&
               status.previous_slot != MapSlot::none &&
               status.active_slot != status.previous_slot;
    }
    return false;
}

void copy_domain_inspection(
    MapSelectorDomainTrialBootResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
    result.domain_repair_required =
        result.domain_repair_required || inspection.recovery_required;
}

void copy_selector_inspection(
    MapSelectorDomainTrialBootResult& result,
    const MapSelectorInspectionResult& inspection) {
    result.selector_repair_required =
        result.selector_repair_required || inspection.recovery_required;
}

void copy_selector_verify(
    MapSelectorDomainTrialBootResult& result,
    const MapSelectorVerifyResult& verify) {
    result.selector_repair_required =
        result.selector_repair_required || verify.recovery_required;
}

void record_live_state(
    const MapActivationGuard& live_guard,
    MapSelectorDomainTrialBootResult& result) {
    result.map_exposure_allowed = live_guard.status().map_available;
}

void contain_map_exposure(
    const MapActivationPolicy& policy,
    bool ambiguous,
    MapSelectorDomainTrialBootResult& result,
    MapActivationGuard& live_guard) {
    MapActivationGuard mapless{};
    result.containment_error = mapless.start(
        policy,
        {ambiguous ? MapSelectorState::ambiguous
                   : MapSelectorState::unreadable,
         {}});
    if (result.containment_error == MapActivationError::none) {
        live_guard = mapless;
        result.live_guard_published = true;
    } else {
        live_guard.stop();
    }
    record_live_state(live_guard, result);
}

void finish_unavailable(
    MapSelectorDomainTrialBootResult& result,
    MapSelectorDomainTrialBootReason reason,
    bool reconciliation,
    const MapActivationPolicy& policy,
    MapActivationGuard& live_guard) {
    result.reason = reason;
    result.reconciliation_required = reconciliation;
    result.state = reconciliation
                       ? MapSelectorDomainTrialBootState::
                             reconciliation_required
                       : MapSelectorDomainTrialBootState::service_required;
    contain_map_exposure(
        policy, reconciliation, result, live_guard);
}

MapSelectorDomainTrialBootReason domain_save_reason(
    const MapSelectorDomainSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error ==
                       MapSelectorDomainStoreError::verification_failure
                   ? MapSelectorDomainTrialBootReason::
                         domain_verification_failed
                   : MapSelectorDomainTrialBootReason::
                         domain_commit_uncertain;
    }
    if (save.error ==
        MapSelectorDomainStoreError::verification_failure) {
        return MapSelectorDomainTrialBootReason::
            domain_verification_failed;
    }
    return MapSelectorDomainTrialBootReason::domain_save_failed;
}

}  // namespace

MapSelectorDomainTrialBootCoordinator::
    MapSelectorDomainTrialBootCoordinator(
        MapSelectorDomainStore& domain_store,
        MapSelectorStore& selector_store,
        MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainTrialBootResult
MapSelectorDomainTrialBootCoordinator::boot(
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected_package,
    const MapPackageEvidence& previous_package,
    std::uint64_t now_ms,
    MapActivationGuard& live_guard) {
    MapSelectorDomainTrialBootResult result{};
    record_live_state(live_guard, result);
    if (!clean_stopped(live_guard.status())) {
        result.reason =
            MapSelectorDomainTrialBootReason::live_guard_not_clean;
        return result;
    }

    MapActivationGuard policy_check{};
    if (policy_check.start(
            policy, {MapSelectorState::missing, {}}) !=
        MapActivationError::none) {
        result.reason = MapSelectorDomainTrialBootReason::invalid_policy;
        return result;
    }

    const auto domain_before = domain_store_.inspect();
    copy_domain_inspection(result, domain_before);
    if (domain_before.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
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
            MapSelectorDomainTrialBootReason::domain_not_active,
            domain_before.record_available ||
                domain_before.error !=
                    MapSelectorDomainStoreError::no_record,
            policy,
            live_guard);
        return result;
    }

    const auto domain_record = domain_before.record;
    result.accepted_generation_before =
        domain_record.accepted_selector_generation;
    result.domain_record_generation = domain_record.record_generation;
    result.domain_epoch = domain_record.domain_epoch;
    if (result.accepted_generation_before ==
            std::numeric_limits<std::uint64_t>::max() ||
        result.domain_record_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
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
            MapSelectorDomainTrialBootReason::
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
            MapSelectorDomainTrialBootReason::protected_source_invalid,
            true,
            policy,
            live_guard);
        return result;
    }
    if (source_before.domain != domain_record.current_domain) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
                protected_domain_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_generation_before =
        source_before.selector_generation;

    result.selector_inspection = selector_store_.inspect();
    copy_selector_inspection(result, result.selector_inspection);
    if (result.selector_inspection.error ==
        MapSelectorStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
                selector_storage_unavailable,
            false,
            policy,
            live_guard);
        return result;
    }
    if (result.selector_inspection.error !=
            MapSelectorStoreError::none ||
        !result.selector_inspection.checkpoint_available) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::selector_state_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.selector_generation_before =
        result.selector_inspection.generation;

    const auto accepted = result.accepted_generation_before;
    const auto protected_generation =
        result.protected_generation_before;
    const auto selector = result.selector_generation_before;
    const bool exact_baseline =
        selector == accepted && protected_generation == accepted;
    const bool recoverable_gap =
        selector == accepted + 1 &&
        (protected_generation == accepted ||
         protected_generation == selector);
    if (!exact_baseline && !recoverable_gap) {
        const bool protected_relation_invalid =
            protected_generation < accepted ||
            protected_generation > selector;
        finish_unavailable(
            result,
            protected_relation_invalid
                ? MapSelectorDomainTrialBootReason::
                      protected_generation_mismatch
                : MapSelectorDomainTrialBootReason::
                      selector_generation_gap,
            true,
            policy,
            live_guard);
        return result;
    }

    MapActivationGuard private_guard{};
    MapSelectorBootCoordinator selector_boot{selector_store_};
    result.selector_boot = selector_boot.boot(
        policy,
        selected_package,
        previous_package,
        now_ms,
        std::max(accepted, protected_generation),
        private_guard);
    result.selector_repair_required =
        result.selector_repair_required ||
        result.selector_boot.repair_required;
    result.selector_generation_after =
        result.selector_boot.active_generation;
    if (!expected_private_state(result.selector_boot, private_guard)) {
        const bool storage_failure =
            result.selector_boot.load.error ==
                MapSelectorStoreError::storage_failure ||
            (result.selector_boot.load.restored &&
             result.selector_boot.save.error ==
                 MapSelectorStoreError::storage_failure);
        finish_unavailable(
            result,
            storage_failure
                ? MapSelectorDomainTrialBootReason::
                      selector_storage_unavailable
                : (result.selector_boot.operational()
                       ? MapSelectorDomainTrialBootReason::
                             selector_boot_unexpected_state
                       : MapSelectorDomainTrialBootReason::
                             selector_boot_failed),
            result.selector_boot.reconciliation_required ||
                result.selector_boot.save.commit_uncertain,
            policy,
            live_guard);
        return result;
    }
    result.selector_booted = true;
    if (result.selector_generation_after < selector ||
        result.selector_generation_after == 0) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    const auto domain_before_advance = domain_store_.inspect();
    copy_domain_inspection(result, domain_before_advance);
    if (domain_before_advance.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
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
            MapSelectorDomainTrialBootReason::domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    result.selector_before_protected = selector_store_.verify_current(
        private_guard, result.selector_generation_after);
    copy_selector_verify(result, result.selector_before_protected);
    if (!exact_selector(
            result.selector_before_protected,
            result.selector_generation_after)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    if (protected_generation < result.selector_generation_after) {
        result.protected_advance_called = true;
        result.protected_source_error = sanitize_source_error(
            protected_source_.advance_selector_generation(
                {domain_record.current_domain,
                 protected_generation,
                 result.selector_generation_after}));
        if (result.protected_source_error !=
            MapSelectorDomainProtectedSourceError::none) {
            finish_unavailable(
                result,
                MapSelectorDomainTrialBootReason::
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
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
                protected_readback_failed,
            true,
            policy,
            live_guard);
        return result;
    }
    if (!exact_source(
            source_after,
            domain_record.current_domain,
            result.selector_generation_after)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::
                protected_readback_mismatch,
            true,
            policy,
            live_guard);
        return result;
    }
    result.protected_source_verified = true;

    result.selector_after_protected =
        selector_store_.verify_current(
            private_guard, result.selector_generation_after);
    copy_selector_verify(result, result.selector_after_protected);
    if (!exact_selector(
            result.selector_after_protected,
            result.selector_generation_after)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::selector_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    auto expected_domain = domain_record;
    if (accepted < result.selector_generation_after) {
        expected_domain.accepted_selector_generation =
            result.selector_generation_after;
        ++expected_domain.record_generation;
        result.domain_save = domain_store_.save(expected_domain);
        result.domain_store_error = result.domain_save.error;
        result.domain_slot_a = result.domain_save.slot_a;
        result.domain_slot_b = result.domain_save.slot_b;
        result.domain_repair_required =
            result.domain_repair_required ||
            result.domain_save.repaired_peer;
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
        result.domain_record_generation =
            expected_domain.record_generation;
    }

    const auto domain_final = domain_store_.inspect();
    copy_domain_inspection(result, domain_final);
    if (domain_final.error != MapSelectorDomainStoreError::none ||
        !domain_final.record_available ||
        !exact_record(expected_domain, domain_final.record)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::final_domain_changed,
            true,
            policy,
            live_guard);
        return result;
    }
    result.domain_generation_verified = true;

    result.selector_final = selector_store_.verify_current(
        private_guard, result.selector_generation_after);
    copy_selector_verify(result, result.selector_final);
    if (!exact_selector(
            result.selector_final,
            result.selector_generation_after)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::final_selector_changed,
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
            result.selector_generation_after)) {
        finish_unavailable(
            result,
            MapSelectorDomainTrialBootReason::final_source_changed,
            true,
            policy,
            live_guard);
        return result;
    }

    live_guard = private_guard;
    result.live_guard_published = true;
    record_live_state(live_guard, result);
    result.fallback_required =
        result.selector_boot.state ==
        MapSelectorBootState::fallback_required;
    if (result.fallback_required) {
        result.state =
            MapSelectorDomainTrialBootState::fallback_required;
    } else if (result.selector_boot.state ==
               MapSelectorBootState::active_ready) {
        result.state = MapSelectorDomainTrialBootState::active_ready;
    } else {
        result.state = MapSelectorDomainTrialBootState::trial_ready;
    }
    result.reason = MapSelectorDomainTrialBootReason::none;
    result.reconciliation_required = false;
    return result;
}

}  // namespace opentrail::maps
