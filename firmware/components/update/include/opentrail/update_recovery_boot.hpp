#pragma once

#include <cstdint>

#include "opentrail/update_checkpoint_store.hpp"

namespace opentrail::update {

enum class UpdateTrustedGenerationError : std::uint8_t {
    none = 0,
    not_initialized,
    io_failure,
    invalid_state,
    rejected,
};

struct UpdateTrustedGenerationRead {
    UpdateTrustedGenerationError error{
        UpdateTrustedGenerationError::io_failure};
    std::uint64_t generation{0};
};

class UpdateTrustedGenerationSource {
public:
    virtual ~UpdateTrustedGenerationSource() = default;

    [[nodiscard]] virtual UpdateTrustedGenerationRead read() = 0;
    // Success must persist a value greater than or equal to the prior value.
    // The coordinator requires exact readback before exposing live state.
    [[nodiscard]] virtual UpdateTrustedGenerationError advance_to(
        std::uint64_t generation) = 0;
};

enum class UpdateRecoveryBootState : std::uint8_t {
    service_required = 0,
    baseline_ready,
    trial_ready,
    rollback_required,
    baseline_recovered,
    confirmed_cleanup_required,
    safe_mode,
};

enum class UpdateRecoveryBootReason : std::uint8_t {
    none = 0,
    clean_baseline,
    invalid_policy,
    live_guard_not_clean,
    trusted_read_failed,
    trusted_generation_invalid,
    baseline_state_conflict,
    recovery_missing,
    storage_unavailable,
    rollback_detected,
    generation_conflict,
    checkpoint_rejected,
    boot_observation_rejected,
    boot_mismatch,
    trial_boot_limit,
    rollback_observation_rejected,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    trusted_advance_failed,
    trusted_readback_failed,
};

struct UpdateRecoveryBootResult {
    UpdateRecoveryBootState state{
        UpdateRecoveryBootState::service_required};
    UpdateRecoveryBootReason reason{
        UpdateRecoveryBootReason::trusted_read_failed};
    UpdateTrustedGenerationError trusted_error{
        UpdateTrustedGenerationError::none};
    UpdateCheckpointLoadResult load{};
    UpdateCheckpointSaveResult save{};
    UpdateGuardError guard_error{UpdateGuardError::none};
    std::uint64_t trusted_generation{0};
    std::uint64_t active_generation{0};
    bool application_allowed{false};
    bool confirmation_required{false};
    bool reboot_to_baseline_required{false};
    bool checkpoint_cleanup_required{false};
    bool repair_required{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool operational() const {
        return application_allowed &&
               (state == UpdateRecoveryBootState::baseline_ready ||
                state == UpdateRecoveryBootState::trial_ready ||
                state == UpdateRecoveryBootState::baseline_recovered ||
                state ==
                    UpdateRecoveryBootState::confirmed_cleanup_required);
    }
};

class UpdateRecoveryBootCoordinator {
public:
    UpdateRecoveryBootCoordinator(
        UpdateCheckpointStore& store,
        UpdateTrustedGenerationSource& trusted_generation);

    // The output guard must not already be running. The coordinator starts and
    // mutates only a private candidate until all required persistence and
    // trusted-generation readback steps have completed.
    [[nodiscard]] UpdateRecoveryBootResult boot(
        const UpdateGuardPolicy& policy,
        const BootObservation& observation,
        UpdateBootGuard& live_guard);

private:
    UpdateCheckpointStore& store_;
    UpdateTrustedGenerationSource& trusted_generation_;
};

}  // namespace opentrail::update
