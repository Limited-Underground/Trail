#include "opentrail/update_recovery_transition.hpp"

namespace opentrail::update {
namespace {

bool same_candidate(
    const VerifiedUpdateCandidate& first,
    const VerifiedUpdateCandidate& second) {
    return first.hardware_id == second.hardware_id &&
           first.version == second.version &&
           first.target_slot == second.target_slot &&
           first.image_bytes == second.image_bytes &&
           first.authenticity_verified == second.authenticity_verified &&
           first.integrity_verified == second.integrity_verified &&
           first.compatibility_verified == second.compatibility_verified &&
           first.rollback_image_verified == second.rollback_image_verified;
}

bool same_persistent_status(
    const UpdateGuardStatus& first,
    const UpdateGuardStatus& second) {
    return first.running == second.running &&
           first.state == second.state &&
           first.rollback_reason == second.rollback_reason &&
           same_candidate(first.candidate, second.candidate) &&
           first.trial_boots == second.trial_boots;
}

UpdateTransitionState transition_state(UpdatePersistenceState state) {
    switch (state) {
        case UpdatePersistenceState::committed:
            return UpdateTransitionState::committed;
        case UpdatePersistenceState::reboot_reconcile_required:
            return UpdateTransitionState::reboot_reconcile_required;
        case UpdatePersistenceState::safe_mode:
            return UpdateTransitionState::safe_mode;
        case UpdatePersistenceState::service_required:
        default:
            return UpdateTransitionState::service_required;
    }
}

}  // namespace

UpdateRecoveryTransitionCoordinator::UpdateRecoveryTransitionCoordinator(
    UpdateCheckpointStore& store,
    UpdateTrustedGenerationSource& trusted_generation)
    : save_(store, trusted_generation) {}

UpdateTransitionResult UpdateRecoveryTransitionCoordinator::report_health(
    UpdateBootGuard& live_guard,
    std::uint32_t boot_session_id,
    std::uint32_t passing_health_mask,
    std::uint64_t now_ms) {
    const auto before = live_guard.status();
    auto attempted = live_guard;
    const auto error = attempted.report_health(
        boot_session_id, passing_health_mask, now_ms);
    return finish(
        live_guard,
        attempted,
        before,
        UpdateTransitionOperation::report_health,
        error);
}

UpdateTransitionResult UpdateRecoveryTransitionCoordinator::tick(
    UpdateBootGuard& live_guard,
    std::uint64_t now_ms) {
    const auto before = live_guard.status();
    auto attempted = live_guard;
    const auto error = attempted.tick(now_ms);
    return finish(
        live_guard,
        attempted,
        before,
        UpdateTransitionOperation::tick,
        error);
}

UpdateTransitionResult UpdateRecoveryTransitionCoordinator::confirm(
    UpdateBootGuard& live_guard,
    std::uint32_t boot_session_id,
    std::uint64_t now_ms) {
    const auto before = live_guard.status();
    auto attempted = live_guard;
    const auto error = attempted.confirm(boot_session_id, now_ms);
    return finish(
        live_guard,
        attempted,
        before,
        UpdateTransitionOperation::confirm,
        error);
}

UpdateTransitionResult UpdateRecoveryTransitionCoordinator::request_rollback(
    UpdateBootGuard& live_guard,
    RollbackReason reason) {
    const auto before = live_guard.status();
    auto attempted = live_guard;
    const auto error = attempted.request_rollback(reason);
    return finish(
        live_guard,
        attempted,
        before,
        UpdateTransitionOperation::request_rollback,
        error);
}

UpdateTransitionResult UpdateRecoveryTransitionCoordinator::finish(
    UpdateBootGuard& live_guard,
    const UpdateBootGuard& attempted_guard,
    const UpdateGuardStatus& before,
    UpdateTransitionOperation operation,
    UpdateGuardError guard_error) {
    UpdateTransitionResult result{};
    result.operation = operation;
    result.guard_error = guard_error;
    result.before_state = before.state;
    const auto attempted = attempted_guard.status();
    result.attempted_state = attempted.state;
    result.persistence_required =
        !same_persistent_status(before, attempted);

    if (!result.persistence_required) {
        live_guard = attempted_guard;
        result.state = guard_error == UpdateGuardError::none
            ? UpdateTransitionState::applied_volatile
            : UpdateTransitionState::rejected;
        result.live_state = live_guard.status().state;
        return result;
    }

    result.persistence = save_.save(attempted_guard);
    result.state = transition_state(result.persistence.state);
    if (result.persistence.committed()) {
        live_guard = attempted_guard;
    } else {
        live_guard.stop();
        result.live_guard_stopped = true;
    }
    result.live_state = live_guard.status().state;
    return result;
}

}  // namespace opentrail::update
