#pragma once

#include "opentrail/update_recovery_boot.hpp"

namespace opentrail::update {

enum class UpdatePersistenceState : std::uint8_t {
    service_required = 0,
    committed,
    reboot_reconcile_required,
    safe_mode,
};

enum class UpdatePersistenceReason : std::uint8_t {
    none = 0,
    live_guard_not_running,
    trusted_read_failed,
    trusted_generation_invalid,
    recovery_missing,
    rollback_detected,
    trusted_reconciliation_required,
    generation_conflict,
    generation_exhausted,
    storage_failure,
    checkpoint_rejected,
    commit_uncertain,
    trusted_advance_failed,
    trusted_readback_failed,
};

struct UpdatePersistenceResult {
    UpdatePersistenceState state{
        UpdatePersistenceState::service_required};
    UpdatePersistenceReason reason{
        UpdatePersistenceReason::trusted_read_failed};
    UpdateTrustedGenerationError trusted_error{
        UpdateTrustedGenerationError::none};
    UpdateCheckpointInspectionResult inspection{};
    UpdateCheckpointSaveResult save{};
    std::uint64_t prior_trusted_generation{0};
    std::uint64_t observed_trusted_readback{0};
    std::uint64_t committed_generation{0};

    [[nodiscard]] constexpr bool committed() const {
        return state == UpdatePersistenceState::committed &&
               reason == UpdatePersistenceReason::none;
    }
};

class UpdateRecoverySaveCoordinator {
public:
    UpdateRecoverySaveCoordinator(
        UpdateCheckpointStore& store,
        UpdateTrustedGenerationSource& trusted_generation);

    // Normal-operation persistence only. The local and trusted generations
    // must agree exactly before the next checkpoint is written. Any reboot-
    // reconcile result must not be retried in the same boot.
    [[nodiscard]] UpdatePersistenceResult save(
        const UpdateBootGuard& guard);

private:
    UpdateCheckpointStore& store_;
    UpdateTrustedGenerationSource& trusted_generation_;
};

}  // namespace opentrail::update
