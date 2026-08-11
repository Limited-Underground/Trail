#pragma once

#include <cstdint>
#include <type_traits>

#include "opentrail/update_recovery_transition.hpp"

namespace opentrail::update {

enum class UpdateRecoveryStatusOperation : std::uint8_t {
    boot = 0,
    save,
    transition,
};

enum class UpdateRecoveryOperatorState : std::uint8_t {
    service_required = 0,
    operational,
    trial_active,
    persistence_committed,
    transition_rejected,
    rollback_required,
    cleanup_required,
    safe_mode,
    reboot_reconcile_required,
};

enum class UpdateRecoveryOperatorReason : std::uint8_t {
    invalid_result = 0,
    none,
    clean_baseline,
    trial_confirmation_required,
    baseline_recovered,
    cleanup_required,
    invalid_configuration,
    live_state_invalid,
    trusted_state_unavailable,
    trusted_state_invalid,
    baseline_state_conflict,
    recovery_missing,
    storage_unavailable,
    rollback_detected,
    generation_conflict,
    generation_exhausted,
    checkpoint_rejected,
    boot_observation_rejected,
    rollback_observation_rejected,
    boot_mismatch,
    trial_boot_limit,
    trusted_reconciliation_required,
    commit_uncertain,
    trust_update_failed,
    transition_rejected,
    confirmation_committed,
    explicit_rollback,
    confirmation_timeout,
};

enum class UpdateRecoveryOperatorAction : std::uint8_t {
    service = 0,
    none,
    continue_operation,
    continue_trial,
    reboot_to_baseline,
    reboot_and_reconcile,
    cleanup_update_state,
};

// Fixed-shape, pointer-free target status. It deliberately contains no policy,
// hardware/candidate identity, address, key, raw checkpoint, or nested result.
struct UpdateRecoveryStatus {
    UpdateRecoveryStatusOperation operation{UpdateRecoveryStatusOperation::boot};
    UpdateRecoveryOperatorState state{
        UpdateRecoveryOperatorState::service_required};
    UpdateRecoveryOperatorReason reason{
        UpdateRecoveryOperatorReason::invalid_result};
    UpdateRecoveryOperatorAction action{
        UpdateRecoveryOperatorAction::service};
    std::uint64_t observed_generation{0};
    std::uint64_t trusted_generation{0};
    bool operation_succeeded{false};
    bool normal_operation_blocked{true};
    bool attention_required{true};
    bool reboot_required{false};
    bool confirmation_required{false};
    bool cleanup_required{false};
};

static_assert(std::is_trivially_copyable_v<UpdateRecoveryStatus>);
static_assert(
    sizeof(UpdateRecoveryStatus) <= 32,
    "Update recovery status must remain bounded for target diagnostics");

[[nodiscard]] UpdateRecoveryStatus make_update_recovery_status(
    const UpdateRecoveryBootResult& result);
[[nodiscard]] UpdateRecoveryStatus make_update_recovery_status(
    const UpdatePersistenceResult& result);
[[nodiscard]] UpdateRecoveryStatus make_update_recovery_status(
    const UpdateTransitionResult& result);

}  // namespace opentrail::update
