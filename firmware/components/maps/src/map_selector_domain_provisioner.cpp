#include "opentrail/map_selector_domain_provisioner.hpp"

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

bool exact_prepared_source(
    const MapSelectorDomainProtectedSourceRead& read,
    const MapSelectorDomainAuthorizationBinding& binding) {
    return coherent_source_read(read) &&
           read.state == MapSelectorDomainProtectedSourceState::ready &&
           read.domain == binding.proposed_domain &&
           read.selector_generation == 0;
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

bool selector_matches_binding(
    const MapSelectorInspectionResult& inspection,
    const MapSelectorDomainAuthorizationBinding& binding) {
    if (binding.media_state ==
        MapSelectorDomainMediaState::verified_empty) {
        return binding.reviewed_selector_generation == 0 &&
               exactly_empty_selector(inspection);
    }
    if (binding.media_state ==
        MapSelectorDomainMediaState::retained_quarantined) {
        return binding.reviewed_selector_generation != 0 &&
               inspection.error == MapSelectorStoreError::none &&
               inspection.checkpoint_available &&
               inspection.generation ==
                   binding.reviewed_selector_generation;
    }
    return false;
}

bool matching_pending_record(
    const MapSelectorDomainRecord& record,
    MapSelectorDomainAuthorizationScope scope,
    const MapSelectorDomainAuthorizationBinding& binding) {
    if (record.state ==
            MapSelectorDomainRecordState::pending_first_baseline &&
        scope == MapSelectorDomainAuthorizationScope::
                     commission_new_device_domain) {
        return record.origin == MapSelectorDomainRecordOrigin::
                                    fresh_device_commissioning &&
               record.current_domain == binding.proposed_domain &&
               !map_selector_domain_id_nonzero(record.retired_domain) &&
               record.retired_selector_generation == 0 &&
               record.accepted_selector_generation == 0 &&
               record.domain_epoch == 1;
    }
    if (record.state ==
            MapSelectorDomainRecordState::pending_selector_reseed &&
        scope == MapSelectorDomainAuthorizationScope::
                     replace_same_device_domain) {
        const bool floor_matches =
            record.retired_selector_generation != 0 &&
            (binding.media_state ==
                     MapSelectorDomainMediaState::verified_empty ||
             record.retired_selector_generation >=
                 binding.reviewed_selector_generation);
        return record.origin == MapSelectorDomainRecordOrigin::
                                    same_device_replacement &&
               record.current_domain == binding.proposed_domain &&
               record.retired_domain == binding.retired_domain &&
               record.accepted_selector_generation == 0 &&
               record.domain_epoch >= 2 && floor_matches;
    }
    return false;
}

void copy_domain_inspection(
    MapSelectorDomainProvisionResult& result,
    const MapSelectorDomainInspectionResult& inspection) {
    result.domain_store_error = inspection.error;
    result.domain_slot_a = inspection.slot_a;
    result.domain_slot_b = inspection.slot_b;
}

void copy_selector_inspection(
    MapSelectorDomainProvisionResult& result,
    const MapSelectorInspectionResult& inspection) {
    result.selector_store_error = inspection.error;
    result.selector_slot_a = inspection.slot_a;
    result.selector_slot_b = inspection.slot_b;
    result.observed_selector_generation =
        inspection.checkpoint_available ? inspection.generation : 0;
}

void finish_service(
    MapSelectorDomainProvisionResult& result,
    MapSelectorDomainProvisionReason reason) {
    result.reason = reason;
    result.reconciliation_required = result.pending_record_persisted;
    result.state = result.reconciliation_required
                       ? MapSelectorDomainProvisionState::
                             reconciliation_required
                       : MapSelectorDomainProvisionState::service_required;
    result.map_exposure_allowed = false;
}

MapSelectorDomainProvisionReason domain_save_reason(
    const MapSelectorDomainSaveResult& save) {
    if (save.commit_uncertain) {
        return save.error ==
                       MapSelectorDomainStoreError::verification_failure
                   ? MapSelectorDomainProvisionReason::
                         domain_verification_failed
                   : MapSelectorDomainProvisionReason::
                         domain_commit_uncertain;
    }
    if (save.error ==
        MapSelectorDomainStoreError::generation_exhausted) {
        return MapSelectorDomainProvisionReason::domain_generation_exhausted;
    }
    if (save.error == MapSelectorDomainStoreError::generation_mismatch ||
        save.error == MapSelectorDomainStoreError::transition_rejected ||
        save.error == MapSelectorDomainStoreError::generation_conflict ||
        save.error == MapSelectorDomainStoreError::invalid_state) {
        return MapSelectorDomainProvisionReason::domain_changed;
    }
    if (save.error == MapSelectorDomainStoreError::verification_failure) {
        return MapSelectorDomainProvisionReason::domain_verification_failed;
    }
    return MapSelectorDomainProvisionReason::domain_save_failed;
}

MapSelectorDomainProvisionReason permit_reason(
    MapSelectorDomainPermitUse use) {
    switch (use) {
        case MapSelectorDomainPermitUse::already_consumed:
            return MapSelectorDomainProvisionReason::
                authorization_already_consumed;
        case MapSelectorDomainPermitUse::binding_mismatch:
            return MapSelectorDomainProvisionReason::
                authorization_binding_mismatch;
        case MapSelectorDomainPermitUse::boot_session_mismatch:
            return MapSelectorDomainProvisionReason::
                authorization_boot_session_mismatch;
        case MapSelectorDomainPermitUse::not_yet_valid:
            return MapSelectorDomainProvisionReason::
                authorization_not_yet_valid;
        case MapSelectorDomainPermitUse::expired:
            return MapSelectorDomainProvisionReason::authorization_expired;
        case MapSelectorDomainPermitUse::unavailable:
        case MapSelectorDomainPermitUse::none:
            return MapSelectorDomainProvisionReason::authorization_required;
    }
    return MapSelectorDomainProvisionReason::authorization_required;
}

}  // namespace

MapSelectorDomainProvisioner::MapSelectorDomainProvisioner(
    MapSelectorDomainStore& domain_store,
    MapSelectorStore& selector_store,
    MapSelectorDomainProtectedSource& protected_source)
    : domain_store_(domain_store),
      selector_store_(selector_store),
      protected_source_(protected_source) {}

MapSelectorDomainProvisionResult MapSelectorDomainProvisioner::provision(
    MapActivationGuard& live_guard,
    const MapSelectorDomainProvisionContext& context,
    const MapSelectorDomainAuthorizationBinding& binding,
    MapSelectorDomainAuthorizationPermit& permit) {
    MapSelectorDomainProvisionResult result{};
    const auto permit_use = permit.consume(
        binding,
        context.boot_session_id,
        context.authorization_use_time_ms);
    result.authorization_consumed =
        permit_use != MapSelectorDomainPermitUse::unavailable &&
        permit_use != MapSelectorDomainPermitUse::already_consumed;
    if (permit_use != MapSelectorDomainPermitUse::none) {
        result.reason = permit_reason(permit_use);
        return result;
    }

    result.scope = permit.scope_;
    if (result.scope !=
            MapSelectorDomainAuthorizationScope::
                replace_same_device_domain &&
        result.scope !=
            MapSelectorDomainAuthorizationScope::
                commission_new_device_domain) {
        result.reason =
            MapSelectorDomainProvisionReason::authorization_scope_invalid;
        return result;
    }

    const auto before = live_guard.status();
    result.map_exposure_allowed = before.map_available;
    if (!safe_map_unavailable(before)) {
        result.reason =
            MapSelectorDomainProvisionReason::map_unavailable_required;
        return result;
    }
    live_guard.stop();
    result.map_exposure_allowed = false;

    const auto domain_inspection = domain_store_.inspect();
    copy_domain_inspection(result, domain_inspection);
    MapSelectorDomainRecord pending{};
    bool pending_needs_save = false;

    if (domain_inspection.error ==
        MapSelectorDomainStoreError::storage_failure) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::domain_storage_unavailable);
        return result;
    }
    if (domain_inspection.error == MapSelectorDomainStoreError::no_record) {
        if (result.scope !=
            MapSelectorDomainAuthorizationScope::
                commission_new_device_domain) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::domain_state_mismatch);
            return result;
        }
        pending = {
            kMapSelectorDomainRecordVersion,
            MapSelectorDomainRecordState::pending_first_baseline,
            MapSelectorDomainRecordOrigin::fresh_device_commissioning,
            binding.proposed_domain,
            {},
            0,
            0,
            1,
            1};
        pending_needs_save = true;
    } else if (domain_inspection.error !=
                   MapSelectorDomainStoreError::none ||
               !domain_inspection.record_available) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::domain_state_mismatch);
        return result;
    } else if (matching_pending_record(
                   domain_inspection.record, result.scope, binding)) {
        pending = domain_inspection.record;
        result.pending_record_persisted = true;
        result.resumed_pending_record = true;
    } else if (
        result.scope ==
            MapSelectorDomainAuthorizationScope::
                replace_same_device_domain &&
        domain_inspection.record.state ==
            MapSelectorDomainRecordState::active &&
        domain_inspection.record.current_domain == binding.retired_domain) {
        const auto& current = domain_inspection.record;
        if (current.record_generation ==
                std::numeric_limits<std::uint64_t>::max() ||
            current.domain_epoch ==
                std::numeric_limits<std::uint64_t>::max()) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::
                    domain_generation_exhausted);
            return result;
        }
        if (binding.media_state ==
                MapSelectorDomainMediaState::retained_quarantined &&
            binding.reviewed_selector_generation <
                current.accepted_selector_generation) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::domain_state_mismatch);
            return result;
        }
        pending = {
            kMapSelectorDomainRecordVersion,
            MapSelectorDomainRecordState::pending_selector_reseed,
            MapSelectorDomainRecordOrigin::same_device_replacement,
            binding.proposed_domain,
            binding.retired_domain,
            std::max(
                current.accepted_selector_generation,
                binding.reviewed_selector_generation),
            0,
            current.domain_epoch + 1,
            current.record_generation + 1};
        pending_needs_save = true;
    } else {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::domain_state_mismatch);
        return result;
    }

    result.domain_record_generation = pending.record_generation;
    result.domain_epoch = pending.domain_epoch;
    result.retired_selector_floor =
        pending.retired_selector_generation;

    const auto selector_before = selector_store_.inspect();
    copy_selector_inspection(result, selector_before);
    if (selector_before.error == MapSelectorStoreError::storage_failure) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::selector_storage_unavailable);
        return result;
    }
    if (!selector_matches_binding(selector_before, binding)) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::selector_state_mismatch);
        return result;
    }

    auto source_before = protected_source_.read();
    source_before.error = sanitize_source_error(source_before.error);
    result.protected_source_error = source_before.error;
    result.protected_source_state_before = source_before.state;
    if (source_before.error !=
        MapSelectorDomainProtectedSourceError::none) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::protected_source_unavailable);
        return result;
    }
    if (!coherent_source_read(source_before)) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::protected_source_invalid);
        return result;
    }
    const bool source_already_prepared =
        exact_prepared_source(source_before, binding);
    if (source_before.state !=
            MapSelectorDomainProtectedSourceState::uninitialized &&
        !(source_already_prepared && result.resumed_pending_record)) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::protected_source_conflict);
        return result;
    }

    if (pending_needs_save) {
        const auto saved = domain_store_.save(pending);
        result.domain_store_error = saved.error;
        result.domain_slot_a = saved.slot_a;
        result.domain_slot_b = saved.slot_b;
        if (!saved.saved()) {
            result.pending_commit_uncertain = saved.commit_uncertain;
            result.reason = domain_save_reason(saved);
            const bool write_attempted =
                saved.written_slot != MapSelectorDomainSource::none;
            result.reconciliation_required =
                saved.commit_uncertain || write_attempted;
            result.state = result.reconciliation_required
                               ? MapSelectorDomainProvisionState::
                                     reconciliation_required
                               : MapSelectorDomainProvisionState::
                                     service_required;
            return result;
        }
        result.pending_record_persisted = true;
    }

    if (binding.media_state ==
        MapSelectorDomainMediaState::retained_quarantined) {
        result.selector_clear_attempted = true;
        result.selector_reset = selector_store_.reset_and_verify_empty();
        result.selector_store_error = result.selector_reset.error;
        result.selector_slot_a = result.selector_reset.slot_a;
        result.selector_slot_b = result.selector_reset.slot_b;
        if (!result.selector_reset.cleared()) {
            finish_service(
                result,
                result.selector_reset.error ==
                        MapSelectorStoreError::verification_failure
                    ? MapSelectorDomainProvisionReason::
                          selector_clear_unverified
                    : MapSelectorDomainProvisionReason::
                          selector_clear_failed);
            return result;
        }
        result.selector_empty_verified = true;
    } else {
        const auto selector_after = selector_store_.inspect();
        copy_selector_inspection(result, selector_after);
        if (selector_after.error == MapSelectorStoreError::storage_failure) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::
                    selector_storage_unavailable);
            return result;
        }
        if (!exactly_empty_selector(selector_after)) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::selector_changed);
            return result;
        }
        result.selector_empty_verified = true;
    }

    if (!source_already_prepared) {
        result.protected_source_called = true;
        result.protected_source_error = sanitize_source_error(
            protected_source_.establish_fresh_domain(
                {result.scope,
                 binding.retired_domain,
                 binding.proposed_domain}));
        if (result.protected_source_error !=
            MapSelectorDomainProtectedSourceError::none) {
            finish_service(
                result,
                MapSelectorDomainProvisionReason::protected_establish_failed);
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
            MapSelectorDomainProvisionReason::protected_readback_failed);
        return result;
    }
    if (!exact_prepared_source(source_after, binding)) {
        finish_service(
            result,
            MapSelectorDomainProvisionReason::protected_readback_mismatch);
        return result;
    }

    result.state = MapSelectorDomainProvisionState::prepared;
    result.reason = MapSelectorDomainProvisionReason::none;
    result.protected_source_verified = true;
    result.reconciliation_required = false;
    result.map_exposure_allowed = false;
    return result;
}

}  // namespace opentrail::maps
