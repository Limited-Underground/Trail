#include "opentrail/update_recovery_boot.hpp"

namespace opentrail::update {
namespace {

bool exactly_empty(const UpdateCheckpointLoadResult& load) {
    return !load.restored &&
           load.error == UpdateCheckpointStoreError::no_checkpoint &&
           load.slot_a == UpdateCheckpointSlotState::empty &&
           load.slot_b == UpdateCheckpointSlotState::empty;
}

bool matches_baseline(
    const UpdateGuardPolicy& policy,
    const BootObservation& observation) {
    return observation.boot_session_id != 0 &&
           observation.version == policy.current_version &&
           observation.slot == policy.current_slot;
}

bool matches_candidate(
    const UpdateGuardStatus& status,
    const BootObservation& observation) {
    return observation.boot_session_id != 0 &&
           observation.version == status.candidate.version &&
           observation.slot == status.candidate.target_slot;
}

UpdateRecoveryBootReason load_failure_reason(
    const UpdateCheckpointLoadResult& load) {
    switch (load.error) {
        case UpdateCheckpointStoreError::generation_below_floor:
            return UpdateRecoveryBootReason::rollback_detected;
        case UpdateCheckpointStoreError::generation_conflict:
            return UpdateRecoveryBootReason::generation_conflict;
        case UpdateCheckpointStoreError::no_checkpoint:
            return UpdateRecoveryBootReason::recovery_missing;
        case UpdateCheckpointStoreError::storage_failure:
            return UpdateRecoveryBootReason::storage_unavailable;
        default:
            return UpdateRecoveryBootReason::checkpoint_rejected;
    }
}

bool load_requires_service(UpdateRecoveryBootReason reason) {
    return reason == UpdateRecoveryBootReason::storage_unavailable;
}

bool save_requires_safe_mode(const UpdateCheckpointSaveResult& save) {
    return save.error == UpdateCheckpointStoreError::invalid_state ||
           save.error == UpdateCheckpointStoreError::generation_conflict ||
           save.error == UpdateCheckpointStoreError::checkpoint_rejected;
}

}  // namespace

UpdateRecoveryBootCoordinator::UpdateRecoveryBootCoordinator(
    UpdateCheckpointStore& store,
    UpdateTrustedGenerationSource& trusted_generation)
    : store_(store), trusted_generation_(trusted_generation) {}

UpdateRecoveryBootResult UpdateRecoveryBootCoordinator::boot(
    const UpdateGuardPolicy& policy,
    const BootObservation& observation,
    UpdateBootGuard& live_guard) {
    UpdateRecoveryBootResult result{};
    if (live_guard.status().running) {
        result.state = UpdateRecoveryBootState::safe_mode;
        result.reason = UpdateRecoveryBootReason::live_guard_not_clean;
        result.guard_error = UpdateGuardError::invalid_state;
        return result;
    }

    UpdateBootGuard candidate{};
    result.guard_error = candidate.start(policy);
    if (result.guard_error != UpdateGuardError::none) {
        result.reason = UpdateRecoveryBootReason::invalid_policy;
        return result;
    }

    const auto trusted = trusted_generation_.read();
    result.trusted_error = trusted.error;
    result.trusted_generation = trusted.generation;
    if (trusted.error == UpdateTrustedGenerationError::not_initialized) {
        result.load = store_.restore(candidate);
        if (!exactly_empty(result.load)) {
            result.state =
                result.load.error ==
                        UpdateCheckpointStoreError::storage_failure
                    ? UpdateRecoveryBootState::service_required
                    : UpdateRecoveryBootState::safe_mode;
            result.reason =
                result.load.error ==
                        UpdateCheckpointStoreError::storage_failure
                    ? UpdateRecoveryBootReason::storage_unavailable
                    : UpdateRecoveryBootReason::baseline_state_conflict;
            return result;
        }
        if (!matches_baseline(policy, observation)) {
            result.state = UpdateRecoveryBootState::safe_mode;
            result.reason =
                UpdateRecoveryBootReason::boot_observation_rejected;
            return result;
        }
        live_guard = candidate;
        result.state = UpdateRecoveryBootState::baseline_ready;
        result.reason = UpdateRecoveryBootReason::clean_baseline;
        result.application_allowed = true;
        return result;
    }
    if (trusted.error != UpdateTrustedGenerationError::none) {
        result.reason = UpdateRecoveryBootReason::trusted_read_failed;
        return result;
    }
    if (trusted.generation == 0) {
        result.reason =
            UpdateRecoveryBootReason::trusted_generation_invalid;
        return result;
    }

    result.load = store_.restore_at_or_above(
        candidate, trusted.generation);
    result.active_generation = result.load.generation;
    result.repair_required = result.load.recovery_required;
    if (!result.load.restored) {
        result.reason = load_failure_reason(result.load);
        result.state = load_requires_service(result.reason)
            ? UpdateRecoveryBootState::service_required
            : UpdateRecoveryBootState::safe_mode;
        return result;
    }

    auto advance_trust = [&](std::uint64_t generation) {
        if (result.trusted_generation == generation) {
            return true;
        }
        result.trusted_error = trusted_generation_.advance_to(generation);
        if (result.trusted_error != UpdateTrustedGenerationError::none) {
            result.state = UpdateRecoveryBootState::service_required;
            result.reason =
                UpdateRecoveryBootReason::trusted_advance_failed;
            result.reconciliation_required = true;
            return false;
        }
        const auto verified = trusted_generation_.read();
        result.trusted_error = verified.error;
        result.trusted_generation = verified.generation;
        if (verified.error != UpdateTrustedGenerationError::none ||
            verified.generation != generation) {
            result.state = UpdateRecoveryBootState::service_required;
            result.reason =
                UpdateRecoveryBootReason::trusted_readback_failed;
            result.reconciliation_required = true;
            return false;
        }
        return true;
    };

    auto persist_candidate = [&]() {
        result.save = store_.save_next_after(
            candidate, result.trusted_generation);
        result.active_generation = result.save.generation;
        if (!result.save.saved()) {
            result.reconciliation_required = result.save.commit_uncertain;
            if (result.save.commit_uncertain) {
                result.state = UpdateRecoveryBootState::service_required;
                result.reason =
                    UpdateRecoveryBootReason::checkpoint_commit_uncertain;
            } else {
                result.state = save_requires_safe_mode(result.save)
                    ? UpdateRecoveryBootState::safe_mode
                    : UpdateRecoveryBootState::service_required;
                result.reason =
                    UpdateRecoveryBootReason::checkpoint_save_failed;
            }
            return false;
        }
        if (!advance_trust(result.save.generation)) {
            return false;
        }
        result.repair_required = false;
        return true;
    };

    const auto restored_state = candidate.status().state;
    if (restored_state == UpdateState::pending_reboot ||
        restored_state == UpdateState::trial) {
        result.guard_error = candidate.begin_boot(observation);
        if (result.guard_error == UpdateGuardError::none) {
            if (!persist_candidate()) {
                return result;
            }
            live_guard = candidate;
            result.state = UpdateRecoveryBootState::trial_ready;
            result.reason = UpdateRecoveryBootReason::none;
            result.application_allowed = true;
            result.confirmation_required = true;
            return result;
        }
        if (result.guard_error == UpdateGuardError::boot_mismatch ||
            result.guard_error == UpdateGuardError::boot_attempt_limit) {
            if (!persist_candidate()) {
                return result;
            }
            live_guard = candidate;
            result.state = UpdateRecoveryBootState::rollback_required;
            result.reason = result.guard_error == UpdateGuardError::boot_mismatch
                ? UpdateRecoveryBootReason::boot_mismatch
                : UpdateRecoveryBootReason::trial_boot_limit;
            result.reboot_to_baseline_required = true;
            return result;
        }
        result.state = UpdateRecoveryBootState::safe_mode;
        result.reason =
            UpdateRecoveryBootReason::boot_observation_rejected;
        return result;
    }

    if (restored_state == UpdateState::rollback_required) {
        result.guard_error = candidate.complete_rollback(observation);
        if (result.guard_error != UpdateGuardError::none) {
            result.state = UpdateRecoveryBootState::safe_mode;
            result.reason =
                UpdateRecoveryBootReason::rollback_observation_rejected;
            return result;
        }
        if (!persist_candidate()) {
            return result;
        }
        live_guard = candidate;
        result.state = UpdateRecoveryBootState::baseline_recovered;
        result.reason = UpdateRecoveryBootReason::none;
        result.application_allowed = true;
        result.checkpoint_cleanup_required = true;
        return result;
    }

    if (restored_state == UpdateState::confirmed) {
        if (!matches_candidate(candidate.status(), observation)) {
            result.state = UpdateRecoveryBootState::safe_mode;
            result.reason =
                UpdateRecoveryBootReason::boot_observation_rejected;
            return result;
        }
        if (!advance_trust(result.load.generation)) {
            return result;
        }
        live_guard = candidate;
        result.state =
            UpdateRecoveryBootState::confirmed_cleanup_required;
        result.reason = UpdateRecoveryBootReason::none;
        result.application_allowed = true;
        result.checkpoint_cleanup_required = true;
        return result;
    }

    if (restored_state == UpdateState::rolled_back) {
        if (!matches_baseline(policy, observation)) {
            result.state = UpdateRecoveryBootState::safe_mode;
            result.reason =
                UpdateRecoveryBootReason::boot_observation_rejected;
            return result;
        }
        if (!advance_trust(result.load.generation)) {
            return result;
        }
        live_guard = candidate;
        result.state = UpdateRecoveryBootState::baseline_recovered;
        result.reason = UpdateRecoveryBootReason::none;
        result.application_allowed = true;
        result.checkpoint_cleanup_required = true;
        return result;
    }

    result.state = UpdateRecoveryBootState::safe_mode;
    result.reason = UpdateRecoveryBootReason::checkpoint_rejected;
    return result;
}

}  // namespace opentrail::update
