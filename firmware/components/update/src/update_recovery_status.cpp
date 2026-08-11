#include "opentrail/update_recovery_status.hpp"

namespace opentrail::update {
namespace {

UpdateRecoveryOperatorReason map_store_error(
    UpdateCheckpointStoreError error) {
    switch (error) {
        case UpdateCheckpointStoreError::generation_conflict:
            return UpdateRecoveryOperatorReason::generation_conflict;
        case UpdateCheckpointStoreError::generation_exhausted:
            return UpdateRecoveryOperatorReason::generation_exhausted;
        case UpdateCheckpointStoreError::checkpoint_rejected:
        case UpdateCheckpointStoreError::invalid_state:
            return UpdateRecoveryOperatorReason::checkpoint_rejected;
        case UpdateCheckpointStoreError::storage_failure:
            return UpdateRecoveryOperatorReason::storage_unavailable;
        default:
            return UpdateRecoveryOperatorReason::invalid_result;
    }
}

UpdateRecoveryOperatorReason map_boot_reason(
    const UpdateRecoveryBootResult& result) {
    switch (result.reason) {
        case UpdateRecoveryBootReason::none:
            return UpdateRecoveryOperatorReason::none;
        case UpdateRecoveryBootReason::clean_baseline:
            return UpdateRecoveryOperatorReason::clean_baseline;
        case UpdateRecoveryBootReason::invalid_policy:
            return UpdateRecoveryOperatorReason::invalid_configuration;
        case UpdateRecoveryBootReason::live_guard_not_clean:
            return UpdateRecoveryOperatorReason::live_state_invalid;
        case UpdateRecoveryBootReason::trusted_read_failed:
            return UpdateRecoveryOperatorReason::trusted_state_unavailable;
        case UpdateRecoveryBootReason::trusted_generation_invalid:
            return UpdateRecoveryOperatorReason::trusted_state_invalid;
        case UpdateRecoveryBootReason::baseline_state_conflict:
            return UpdateRecoveryOperatorReason::baseline_state_conflict;
        case UpdateRecoveryBootReason::recovery_missing:
            return UpdateRecoveryOperatorReason::recovery_missing;
        case UpdateRecoveryBootReason::storage_unavailable:
            return UpdateRecoveryOperatorReason::storage_unavailable;
        case UpdateRecoveryBootReason::rollback_detected:
            return UpdateRecoveryOperatorReason::rollback_detected;
        case UpdateRecoveryBootReason::generation_conflict:
            return UpdateRecoveryOperatorReason::generation_conflict;
        case UpdateRecoveryBootReason::checkpoint_rejected:
            return UpdateRecoveryOperatorReason::checkpoint_rejected;
        case UpdateRecoveryBootReason::boot_observation_rejected:
            return UpdateRecoveryOperatorReason::boot_observation_rejected;
        case UpdateRecoveryBootReason::boot_mismatch:
            return UpdateRecoveryOperatorReason::boot_mismatch;
        case UpdateRecoveryBootReason::trial_boot_limit:
            return UpdateRecoveryOperatorReason::trial_boot_limit;
        case UpdateRecoveryBootReason::rollback_observation_rejected:
            return UpdateRecoveryOperatorReason::rollback_observation_rejected;
        case UpdateRecoveryBootReason::checkpoint_save_failed:
            return map_store_error(result.save.error);
        case UpdateRecoveryBootReason::checkpoint_commit_uncertain:
            return UpdateRecoveryOperatorReason::commit_uncertain;
        case UpdateRecoveryBootReason::trusted_advance_failed:
        case UpdateRecoveryBootReason::trusted_readback_failed:
            return UpdateRecoveryOperatorReason::trust_update_failed;
    }
    return UpdateRecoveryOperatorReason::invalid_result;
}

UpdateRecoveryOperatorReason map_persistence_reason(
    UpdatePersistenceReason reason) {
    switch (reason) {
        case UpdatePersistenceReason::none:
            return UpdateRecoveryOperatorReason::none;
        case UpdatePersistenceReason::live_guard_not_running:
            return UpdateRecoveryOperatorReason::live_state_invalid;
        case UpdatePersistenceReason::trusted_read_failed:
            return UpdateRecoveryOperatorReason::trusted_state_unavailable;
        case UpdatePersistenceReason::trusted_generation_invalid:
            return UpdateRecoveryOperatorReason::trusted_state_invalid;
        case UpdatePersistenceReason::recovery_missing:
            return UpdateRecoveryOperatorReason::recovery_missing;
        case UpdatePersistenceReason::rollback_detected:
            return UpdateRecoveryOperatorReason::rollback_detected;
        case UpdatePersistenceReason::trusted_reconciliation_required:
            return UpdateRecoveryOperatorReason::
                trusted_reconciliation_required;
        case UpdatePersistenceReason::generation_conflict:
            return UpdateRecoveryOperatorReason::generation_conflict;
        case UpdatePersistenceReason::generation_exhausted:
            return UpdateRecoveryOperatorReason::generation_exhausted;
        case UpdatePersistenceReason::storage_failure:
            return UpdateRecoveryOperatorReason::storage_unavailable;
        case UpdatePersistenceReason::checkpoint_rejected:
            return UpdateRecoveryOperatorReason::checkpoint_rejected;
        case UpdatePersistenceReason::commit_uncertain:
            return UpdateRecoveryOperatorReason::commit_uncertain;
        case UpdatePersistenceReason::trusted_advance_failed:
        case UpdatePersistenceReason::trusted_readback_failed:
            return UpdateRecoveryOperatorReason::trust_update_failed;
    }
    return UpdateRecoveryOperatorReason::invalid_result;
}

bool exact_generation_pair(
    std::uint64_t observed,
    std::uint64_t trusted) {
    return observed != 0 && observed == trusted;
}

bool boot_reconciliation_reason(UpdateRecoveryBootReason reason) {
    return reason ==
               UpdateRecoveryBootReason::checkpoint_commit_uncertain ||
           reason == UpdateRecoveryBootReason::trusted_advance_failed ||
           reason == UpdateRecoveryBootReason::trusted_readback_failed;
}

bool boot_safe_mode_reason(const UpdateRecoveryBootResult& result) {
    switch (result.reason) {
        case UpdateRecoveryBootReason::live_guard_not_clean:
        case UpdateRecoveryBootReason::baseline_state_conflict:
        case UpdateRecoveryBootReason::recovery_missing:
        case UpdateRecoveryBootReason::rollback_detected:
        case UpdateRecoveryBootReason::generation_conflict:
        case UpdateRecoveryBootReason::checkpoint_rejected:
        case UpdateRecoveryBootReason::boot_observation_rejected:
        case UpdateRecoveryBootReason::rollback_observation_rejected:
            return true;
        case UpdateRecoveryBootReason::checkpoint_save_failed:
            return result.save.error ==
                       UpdateCheckpointStoreError::invalid_state ||
                   result.save.error ==
                       UpdateCheckpointStoreError::generation_conflict ||
                   result.save.error ==
                       UpdateCheckpointStoreError::checkpoint_rejected;
        default:
            return false;
    }
}

bool boot_service_reason(const UpdateRecoveryBootResult& result) {
    if (result.reason == UpdateRecoveryBootReason::checkpoint_save_failed) {
        return result.save.error ==
                   UpdateCheckpointStoreError::generation_exhausted ||
               result.save.error ==
                   UpdateCheckpointStoreError::storage_failure ||
               result.save.error ==
                   UpdateCheckpointStoreError::verification_failure;
    }
    return result.reason == UpdateRecoveryBootReason::invalid_policy ||
           result.reason == UpdateRecoveryBootReason::trusted_read_failed ||
           result.reason ==
               UpdateRecoveryBootReason::trusted_generation_invalid ||
           result.reason == UpdateRecoveryBootReason::storage_unavailable ||
           boot_reconciliation_reason(result.reason);
}

bool boot_service_evidence(const UpdateRecoveryBootResult& result) {
    switch (result.reason) {
        case UpdateRecoveryBootReason::invalid_policy:
            return result.guard_error != UpdateGuardError::none;
        case UpdateRecoveryBootReason::trusted_read_failed:
            return result.trusted_error !=
                   UpdateTrustedGenerationError::none;
        case UpdateRecoveryBootReason::trusted_generation_invalid:
            return result.trusted_error ==
                       UpdateTrustedGenerationError::none &&
                   result.trusted_generation == 0;
        case UpdateRecoveryBootReason::storage_unavailable:
            return result.load.error ==
                   UpdateCheckpointStoreError::storage_failure;
        case UpdateRecoveryBootReason::checkpoint_save_failed:
            return map_store_error(result.save.error) !=
                   UpdateRecoveryOperatorReason::invalid_result;
        case UpdateRecoveryBootReason::checkpoint_commit_uncertain:
            return result.save.commit_uncertain;
        case UpdateRecoveryBootReason::trusted_advance_failed:
            return result.trusted_error !=
                   UpdateTrustedGenerationError::none;
        case UpdateRecoveryBootReason::trusted_readback_failed:
            return result.trusted_error !=
                       UpdateTrustedGenerationError::none ||
                   result.active_generation != result.trusted_generation;
        default:
            return false;
    }
}

bool persistence_reboot_reason(UpdatePersistenceReason reason) {
    return reason ==
               UpdatePersistenceReason::trusted_reconciliation_required ||
           reason == UpdatePersistenceReason::commit_uncertain ||
           reason == UpdatePersistenceReason::trusted_advance_failed ||
           reason == UpdatePersistenceReason::trusted_readback_failed;
}

bool persistence_safe_reason(UpdatePersistenceReason reason) {
    return reason == UpdatePersistenceReason::live_guard_not_running ||
           reason == UpdatePersistenceReason::recovery_missing ||
           reason == UpdatePersistenceReason::rollback_detected ||
           reason == UpdatePersistenceReason::generation_conflict ||
           reason == UpdatePersistenceReason::checkpoint_rejected;
}

bool persistence_service_reason(UpdatePersistenceReason reason) {
    return reason == UpdatePersistenceReason::trusted_read_failed ||
           reason == UpdatePersistenceReason::trusted_generation_invalid ||
           reason == UpdatePersistenceReason::generation_exhausted ||
           reason == UpdatePersistenceReason::storage_failure;
}

bool boot_result_is_coherent(const UpdateRecoveryBootResult& result) {
    const auto mapped = map_boot_reason(result);
    switch (result.state) {
        case UpdateRecoveryBootState::baseline_ready:
            return result.reason == UpdateRecoveryBootReason::clean_baseline &&
                   result.application_allowed &&
                   !result.confirmation_required &&
                   !result.reboot_to_baseline_required &&
                   !result.checkpoint_cleanup_required &&
                   result.active_generation == 0 &&
                   result.trusted_generation == 0;
        case UpdateRecoveryBootState::trial_ready:
            return result.reason == UpdateRecoveryBootReason::none &&
                   result.operational() && result.confirmation_required &&
                   !result.reboot_to_baseline_required &&
                   !result.checkpoint_cleanup_required &&
                   exact_generation_pair(
                       result.active_generation,
                       result.trusted_generation);
        case UpdateRecoveryBootState::rollback_required:
            return !result.application_allowed &&
                   result.reboot_to_baseline_required &&
                   (result.reason == UpdateRecoveryBootReason::boot_mismatch ||
                    result.reason ==
                        UpdateRecoveryBootReason::trial_boot_limit) &&
                   exact_generation_pair(
                       result.active_generation,
                       result.trusted_generation);
        case UpdateRecoveryBootState::baseline_recovered:
        case UpdateRecoveryBootState::confirmed_cleanup_required:
            return result.reason == UpdateRecoveryBootReason::none &&
                   result.operational() &&
                   result.checkpoint_cleanup_required &&
                   !result.confirmation_required &&
                   !result.reboot_to_baseline_required &&
                   exact_generation_pair(
                       result.active_generation,
                       result.trusted_generation);
        case UpdateRecoveryBootState::safe_mode:
            return !result.application_allowed &&
                   boot_safe_mode_reason(result) &&
                   mapped != UpdateRecoveryOperatorReason::invalid_result &&
                   !result.reconciliation_required;
        case UpdateRecoveryBootState::service_required:
            return !result.application_allowed &&
                   boot_service_reason(result) &&
                   boot_service_evidence(result) &&
                   mapped != UpdateRecoveryOperatorReason::invalid_result &&
                   result.reconciliation_required ==
                       boot_reconciliation_reason(result.reason);
    }
    return false;
}

bool persistence_result_is_coherent(
    const UpdatePersistenceResult& result) {
    const auto mapped = map_persistence_reason(result.reason);
    switch (result.state) {
        case UpdatePersistenceState::committed:
            return result.committed() && result.save.saved() &&
                   result.trusted_error ==
                       UpdateTrustedGenerationError::none &&
                   result.inspection.checkpoint_available &&
                   result.inspection.generation ==
                       result.prior_trusted_generation &&
                   result.save.generation == result.committed_generation &&
                   exact_generation_pair(
                       result.committed_generation,
                       result.observed_trusted_readback);
        case UpdatePersistenceState::reboot_reconcile_required:
            return persistence_reboot_reason(result.reason) &&
                   mapped != UpdateRecoveryOperatorReason::invalid_result;
        case UpdatePersistenceState::safe_mode:
            return persistence_safe_reason(result.reason) &&
                   mapped != UpdateRecoveryOperatorReason::invalid_result;
        case UpdatePersistenceState::service_required:
            return persistence_service_reason(result.reason) &&
                   mapped != UpdateRecoveryOperatorReason::invalid_result;
    }
    return false;
}

bool durable_transition_attempt_is_coherent(
    const UpdateTransitionResult& result) {
    if (result.attempted_state == UpdateState::confirmed) {
        return result.operation == UpdateTransitionOperation::confirm &&
               result.guard_error == UpdateGuardError::none &&
               result.before_state == UpdateState::trial;
    }
    if (result.attempted_state != UpdateState::rollback_required) {
        return false;
    }
    if (result.operation == UpdateTransitionOperation::request_rollback) {
        return result.guard_error == UpdateGuardError::none &&
               (result.before_state == UpdateState::pending_reboot ||
                result.before_state == UpdateState::trial);
    }
    return result.guard_error == UpdateGuardError::confirmation_timeout &&
           result.before_state == UpdateState::trial &&
           (result.operation == UpdateTransitionOperation::report_health ||
            result.operation == UpdateTransitionOperation::tick ||
            result.operation == UpdateTransitionOperation::confirm);
}

bool transition_result_is_coherent(const UpdateTransitionResult& result) {
    switch (result.state) {
        case UpdateTransitionState::rejected:
            return result.guard_error != UpdateGuardError::none &&
                   result.guard_error !=
                       UpdateGuardError::confirmation_timeout &&
                   !result.persistence_required &&
                   !result.live_guard_stopped &&
                   result.before_state == result.attempted_state &&
                   result.attempted_state == result.live_state;
        case UpdateTransitionState::applied_volatile:
            return result.guard_error == UpdateGuardError::none &&
                   (result.operation ==
                        UpdateTransitionOperation::report_health ||
                    result.operation == UpdateTransitionOperation::tick) &&
                   !result.persistence_required &&
                   !result.live_guard_stopped &&
                   result.before_state == result.attempted_state &&
                   result.attempted_state == result.live_state;
        case UpdateTransitionState::committed:
            return result.persistence_required &&
                   !result.live_guard_stopped &&
                   result.persistence.committed() &&
                   persistence_result_is_coherent(result.persistence) &&
                   durable_transition_attempt_is_coherent(result) &&
                   result.attempted_state == result.live_state &&
                   result.before_state != result.live_state;
        case UpdateTransitionState::reboot_reconcile_required:
            return result.persistence_required &&
                   result.live_guard_stopped &&
                   durable_transition_attempt_is_coherent(result) &&
                   result.live_state == result.before_state &&
                   result.persistence.state ==
                       UpdatePersistenceState::reboot_reconcile_required &&
                   persistence_result_is_coherent(result.persistence);
        case UpdateTransitionState::safe_mode:
            return result.persistence_required &&
                   result.live_guard_stopped &&
                   durable_transition_attempt_is_coherent(result) &&
                   result.live_state == result.before_state &&
                   result.persistence.state ==
                       UpdatePersistenceState::safe_mode &&
                   persistence_result_is_coherent(result.persistence);
        case UpdateTransitionState::service_required:
            return result.persistence_required &&
                   result.live_guard_stopped &&
                   durable_transition_attempt_is_coherent(result) &&
                   result.live_state == result.before_state &&
                   result.persistence.state ==
                       UpdatePersistenceState::service_required &&
                   persistence_result_is_coherent(result.persistence);
    }
    return false;
}

void set_generation_evidence(
    UpdateRecoveryStatus& status,
    const UpdatePersistenceResult& result) {
    status.observed_generation = result.committed()
        ? result.committed_generation
        : result.inspection.checkpoint_available
            ? result.inspection.generation
            : 0;
    status.trusted_generation = result.observed_trusted_readback != 0
        ? result.observed_trusted_readback
        : result.prior_trusted_generation;
}

UpdateRecoveryStatus invalid_status(UpdateRecoveryStatusOperation operation) {
    UpdateRecoveryStatus status{};
    status.operation = operation;
    return status;
}

}  // namespace

UpdateRecoveryStatus make_update_recovery_status(
    const UpdateRecoveryBootResult& result) {
    if (!boot_result_is_coherent(result)) {
        return invalid_status(UpdateRecoveryStatusOperation::boot);
    }

    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::boot;
    status.reason = map_boot_reason(result);
    status.observed_generation = result.active_generation;
    status.trusted_generation = result.trusted_generation;

    switch (result.state) {
        case UpdateRecoveryBootState::baseline_ready:
            status.state = UpdateRecoveryOperatorState::operational;
            status.action = UpdateRecoveryOperatorAction::continue_operation;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.attention_required = false;
            break;
        case UpdateRecoveryBootState::trial_ready:
            status.state = UpdateRecoveryOperatorState::trial_active;
            status.reason =
                UpdateRecoveryOperatorReason::trial_confirmation_required;
            status.action = UpdateRecoveryOperatorAction::continue_trial;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.attention_required = false;
            status.confirmation_required = true;
            break;
        case UpdateRecoveryBootState::rollback_required:
            status.state = UpdateRecoveryOperatorState::rollback_required;
            status.action = UpdateRecoveryOperatorAction::reboot_to_baseline;
            status.operation_succeeded = true;
            status.reboot_required = true;
            break;
        case UpdateRecoveryBootState::baseline_recovered:
            status.state = UpdateRecoveryOperatorState::cleanup_required;
            status.reason = UpdateRecoveryOperatorReason::baseline_recovered;
            status.action =
                UpdateRecoveryOperatorAction::cleanup_update_state;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.cleanup_required = true;
            break;
        case UpdateRecoveryBootState::confirmed_cleanup_required:
            status.state = UpdateRecoveryOperatorState::cleanup_required;
            status.reason = UpdateRecoveryOperatorReason::cleanup_required;
            status.action =
                UpdateRecoveryOperatorAction::cleanup_update_state;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.cleanup_required = true;
            break;
        case UpdateRecoveryBootState::safe_mode:
            status.state = UpdateRecoveryOperatorState::safe_mode;
            status.action = UpdateRecoveryOperatorAction::service;
            break;
        case UpdateRecoveryBootState::service_required:
            if (result.reconciliation_required) {
                status.state =
                    UpdateRecoveryOperatorState::reboot_reconcile_required;
                status.action =
                    UpdateRecoveryOperatorAction::reboot_and_reconcile;
                status.reboot_required = true;
            }
            break;
    }
    return status;
}

UpdateRecoveryStatus make_update_recovery_status(
    const UpdatePersistenceResult& result) {
    if (!persistence_result_is_coherent(result)) {
        return invalid_status(UpdateRecoveryStatusOperation::save);
    }

    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::save;
    status.reason = map_persistence_reason(result.reason);
    set_generation_evidence(status, result);

    switch (result.state) {
        case UpdatePersistenceState::committed:
            status.state =
                UpdateRecoveryOperatorState::persistence_committed;
            status.action = UpdateRecoveryOperatorAction::none;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.attention_required = false;
            break;
        case UpdatePersistenceState::reboot_reconcile_required:
            status.state =
                UpdateRecoveryOperatorState::reboot_reconcile_required;
            status.action =
                UpdateRecoveryOperatorAction::reboot_and_reconcile;
            status.reboot_required = true;
            break;
        case UpdatePersistenceState::safe_mode:
            status.state = UpdateRecoveryOperatorState::safe_mode;
            status.action = UpdateRecoveryOperatorAction::service;
            break;
        case UpdatePersistenceState::service_required:
            break;
    }
    return status;
}

UpdateRecoveryStatus make_update_recovery_status(
    const UpdateTransitionResult& result) {
    if (!transition_result_is_coherent(result)) {
        return invalid_status(UpdateRecoveryStatusOperation::transition);
    }

    UpdateRecoveryStatus status{};
    status.operation = UpdateRecoveryStatusOperation::transition;
    set_generation_evidence(status, result.persistence);

    switch (result.state) {
        case UpdateTransitionState::rejected:
            status.state = UpdateRecoveryOperatorState::transition_rejected;
            status.reason = UpdateRecoveryOperatorReason::transition_rejected;
            status.action = UpdateRecoveryOperatorAction::none;
            status.normal_operation_blocked = false;
            break;
        case UpdateTransitionState::applied_volatile:
            status.state = UpdateRecoveryOperatorState::trial_active;
            status.reason =
                UpdateRecoveryOperatorReason::trial_confirmation_required;
            status.action = UpdateRecoveryOperatorAction::continue_trial;
            status.operation_succeeded = true;
            status.normal_operation_blocked = false;
            status.attention_required = false;
            status.confirmation_required = true;
            break;
        case UpdateTransitionState::committed:
            status.operation_succeeded = true;
            if (result.live_state == UpdateState::confirmed) {
                status.state = UpdateRecoveryOperatorState::operational;
                status.reason =
                    UpdateRecoveryOperatorReason::confirmation_committed;
                status.action =
                    UpdateRecoveryOperatorAction::continue_operation;
                status.normal_operation_blocked = false;
                status.attention_required = false;
            } else {
                status.state = UpdateRecoveryOperatorState::rollback_required;
                status.reason = result.guard_error ==
                        UpdateGuardError::confirmation_timeout
                    ? UpdateRecoveryOperatorReason::confirmation_timeout
                    : UpdateRecoveryOperatorReason::explicit_rollback;
                status.action =
                    UpdateRecoveryOperatorAction::reboot_to_baseline;
                status.reboot_required = true;
            }
            break;
        case UpdateTransitionState::reboot_reconcile_required:
            status.state =
                UpdateRecoveryOperatorState::reboot_reconcile_required;
            status.reason =
                map_persistence_reason(result.persistence.reason);
            status.action =
                UpdateRecoveryOperatorAction::reboot_and_reconcile;
            status.reboot_required = true;
            break;
        case UpdateTransitionState::safe_mode:
            status.state = UpdateRecoveryOperatorState::safe_mode;
            status.reason =
                map_persistence_reason(result.persistence.reason);
            status.action = UpdateRecoveryOperatorAction::service;
            break;
        case UpdateTransitionState::service_required:
            status.reason =
                map_persistence_reason(result.persistence.reason);
            break;
    }
    return status;
}

}  // namespace opentrail::update
