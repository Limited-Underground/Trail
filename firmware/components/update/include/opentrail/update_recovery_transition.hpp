#pragma once

#include "opentrail/update_recovery_save.hpp"

namespace opentrail::update {

enum class UpdateTransitionOperation : std::uint8_t {
    report_health = 0,
    tick,
    confirm,
    request_rollback,
};

enum class UpdateTransitionState : std::uint8_t {
    rejected = 0,
    applied_volatile,
    committed,
    reboot_reconcile_required,
    safe_mode,
    service_required,
};

struct UpdateTransitionResult {
    UpdateTransitionOperation operation{UpdateTransitionOperation::tick};
    UpdateTransitionState state{UpdateTransitionState::rejected};
    UpdateGuardError guard_error{UpdateGuardError::invalid_state};
    UpdatePersistenceResult persistence{};
    UpdateState before_state{UpdateState::idle};
    UpdateState attempted_state{UpdateState::idle};
    UpdateState live_state{UpdateState::idle};
    bool persistence_required{false};
    bool live_guard_stopped{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == UpdateTransitionState::committed &&
               persistence.committed();
    }
};

// Owns trial-time lifecycle mutations that may create durable confirmation or
// rollback intent. Persistent changes are applied to a private copy and become
// live only after checkpoint and trusted-generation verification. A failed
// persistence attempt stops the live guard and requires boot reconciliation.
class UpdateRecoveryTransitionCoordinator {
public:
    UpdateRecoveryTransitionCoordinator(
        UpdateCheckpointStore& store,
        UpdateTrustedGenerationSource& trusted_generation);

    [[nodiscard]] UpdateTransitionResult report_health(
        UpdateBootGuard& live_guard,
        std::uint32_t boot_session_id,
        std::uint32_t passing_health_mask,
        std::uint64_t now_ms);
    [[nodiscard]] UpdateTransitionResult tick(
        UpdateBootGuard& live_guard,
        std::uint64_t now_ms);
    [[nodiscard]] UpdateTransitionResult confirm(
        UpdateBootGuard& live_guard,
        std::uint32_t boot_session_id,
        std::uint64_t now_ms);
    [[nodiscard]] UpdateTransitionResult request_rollback(
        UpdateBootGuard& live_guard,
        RollbackReason reason);

private:
    [[nodiscard]] UpdateTransitionResult finish(
        UpdateBootGuard& live_guard,
        const UpdateBootGuard& attempted_guard,
        const UpdateGuardStatus& before,
        UpdateTransitionOperation operation,
        UpdateGuardError guard_error);

    UpdateRecoverySaveCoordinator save_;
};

}  // namespace opentrail::update
