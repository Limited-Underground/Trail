#include "opentrail/update_recovery_save.hpp"

namespace opentrail::update {
namespace {

UpdatePersistenceReason save_failure_reason(
    const UpdateCheckpointSaveResult& save) {
    if (save.commit_uncertain) {
        return UpdatePersistenceReason::commit_uncertain;
    }
    switch (save.error) {
        case UpdateCheckpointStoreError::generation_conflict:
            return UpdatePersistenceReason::generation_conflict;
        case UpdateCheckpointStoreError::generation_exhausted:
            return UpdatePersistenceReason::generation_exhausted;
        case UpdateCheckpointStoreError::checkpoint_rejected:
        case UpdateCheckpointStoreError::invalid_state:
            return UpdatePersistenceReason::checkpoint_rejected;
        default:
            return UpdatePersistenceReason::storage_failure;
    }
}

bool safe_mode_reason(UpdatePersistenceReason reason) {
    return reason == UpdatePersistenceReason::recovery_missing ||
           reason == UpdatePersistenceReason::rollback_detected ||
           reason == UpdatePersistenceReason::generation_conflict ||
           reason == UpdatePersistenceReason::checkpoint_rejected;
}

}  // namespace

UpdateRecoverySaveCoordinator::UpdateRecoverySaveCoordinator(
    UpdateCheckpointStore& store,
    UpdateTrustedGenerationSource& trusted_generation)
    : store_(store), trusted_generation_(trusted_generation) {}

UpdatePersistenceResult UpdateRecoverySaveCoordinator::save(
    const UpdateBootGuard& guard) {
    UpdatePersistenceResult result{};
    if (!guard.status().running) {
        result.state = UpdatePersistenceState::safe_mode;
        result.reason = UpdatePersistenceReason::live_guard_not_running;
        return result;
    }

    const auto trusted = trusted_generation_.read();
    result.trusted_error = trusted.error;
    result.prior_trusted_generation = trusted.generation;
    if (trusted.error != UpdateTrustedGenerationError::none) {
        result.reason = UpdatePersistenceReason::trusted_read_failed;
        return result;
    }
    if (trusted.generation == 0) {
        result.reason =
            UpdatePersistenceReason::trusted_generation_invalid;
        return result;
    }

    result.inspection = store_.inspect();
    if (!result.inspection.checkpoint_available) {
        switch (result.inspection.error) {
            case UpdateCheckpointStoreError::generation_conflict:
                result.reason = UpdatePersistenceReason::generation_conflict;
                break;
            case UpdateCheckpointStoreError::storage_failure:
                result.reason = UpdatePersistenceReason::storage_failure;
                break;
            case UpdateCheckpointStoreError::invalid_state:
                result.reason = UpdatePersistenceReason::checkpoint_rejected;
                break;
            default:
                result.reason = UpdatePersistenceReason::recovery_missing;
                break;
        }
        result.state = safe_mode_reason(result.reason)
            ? UpdatePersistenceState::safe_mode
            : UpdatePersistenceState::service_required;
        return result;
    }
    if (result.inspection.error ==
        UpdateCheckpointStoreError::storage_failure) {
        result.reason = UpdatePersistenceReason::storage_failure;
        return result;
    }
    if (result.inspection.generation < trusted.generation) {
        result.state = UpdatePersistenceState::safe_mode;
        result.reason = UpdatePersistenceReason::rollback_detected;
        return result;
    }
    if (result.inspection.generation > trusted.generation) {
        result.state = UpdatePersistenceState::reboot_reconcile_required;
        result.reason =
            UpdatePersistenceReason::trusted_reconciliation_required;
        return result;
    }

    result.save = store_.save_next_after(guard, trusted.generation);
    result.committed_generation = result.save.generation;
    if (!result.save.saved()) {
        result.reason = save_failure_reason(result.save);
        result.state = result.save.commit_uncertain
            ? UpdatePersistenceState::reboot_reconcile_required
            : safe_mode_reason(result.reason)
                ? UpdatePersistenceState::safe_mode
                : UpdatePersistenceState::service_required;
        return result;
    }

    result.trusted_error =
        trusted_generation_.advance_to(result.save.generation);
    if (result.trusted_error != UpdateTrustedGenerationError::none) {
        result.state = UpdatePersistenceState::reboot_reconcile_required;
        result.reason = UpdatePersistenceReason::trusted_advance_failed;
        return result;
    }
    const auto verified = trusted_generation_.read();
    result.trusted_error = verified.error;
    result.observed_trusted_readback = verified.generation;
    if (verified.error != UpdateTrustedGenerationError::none ||
        verified.generation != result.save.generation) {
        result.state = UpdatePersistenceState::reboot_reconcile_required;
        result.reason = UpdatePersistenceReason::trusted_readback_failed;
        return result;
    }

    result.state = UpdatePersistenceState::committed;
    result.reason = UpdatePersistenceReason::none;
    return result;
}

}  // namespace opentrail::update
