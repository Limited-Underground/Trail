#include "opentrail/update_boot_guard.hpp"

namespace opentrail::update {
namespace {

bool known_slot(ImageSlot slot) {
    return slot == ImageSlot::slot_a || slot == ImageSlot::slot_b;
}

bool valid_health_mask(std::uint32_t mask) {
    return mask != 0 && (mask & ~kAllTrialHealthBits) == 0;
}

bool known_rollback_reason(RollbackReason reason) {
    return reason == RollbackReason::boot_mismatch ||
           reason == RollbackReason::confirmation_timeout ||
           reason == RollbackReason::boot_attempt_limit ||
           reason == RollbackReason::explicit_health_failure;
}

}  // namespace

UpdateGuardError UpdateBootGuard::start(const UpdateGuardPolicy& policy) {
    if (status_.running) {
        return UpdateGuardError::invalid_state;
    }
    if (policy.hardware_id == 0 || policy.current_version == 0 ||
        !known_slot(policy.current_slot) ||
        !valid_health_mask(policy.required_health_mask) ||
        policy.minimum_stable_ms == 0 ||
        policy.confirmation_deadline_ms <= policy.minimum_stable_ms ||
        policy.maximum_trial_boots == 0 ||
        policy.maximum_image_bytes == 0) {
        return UpdateGuardError::invalid_configuration;
    }
    policy_ = policy;
    status_ = {};
    status_.running = true;
    status_.state = UpdateState::idle;
    return UpdateGuardError::none;
}

void UpdateBootGuard::stop() {
    status_.running = false;
}

UpdateGuardError UpdateBootGuard::stage(
    const VerifiedUpdateCandidate& candidate) {
    if (!status_.running || status_.state != UpdateState::idle) {
        return UpdateGuardError::invalid_state;
    }
    if (candidate.hardware_id != policy_.hardware_id ||
        candidate.version <= policy_.current_version ||
        !known_slot(candidate.target_slot) ||
        candidate.target_slot == policy_.current_slot ||
        candidate.image_bytes == 0 ||
        candidate.image_bytes > policy_.maximum_image_bytes) {
        return UpdateGuardError::invalid_candidate;
    }
    if (!candidate.authenticity_verified ||
        !candidate.integrity_verified ||
        !candidate.compatibility_verified ||
        !candidate.rollback_image_verified) {
        return UpdateGuardError::verification_required;
    }
    status_.candidate = candidate;
    status_.state = UpdateState::staged;
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::mark_written(
    const CandidateWriteEvidence& evidence) {
    if (!status_.running || status_.state != UpdateState::staged) {
        return UpdateGuardError::invalid_state;
    }
    if (evidence.version != status_.candidate.version ||
        evidence.slot != status_.candidate.target_slot) {
        return UpdateGuardError::invalid_candidate;
    }
    if (!evidence.full_readback_verified ||
        !evidence.boot_selection_persisted) {
        return UpdateGuardError::verification_required;
    }
    status_.state = UpdateState::pending_reboot;
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::cancel_staged() {
    if (!status_.running || status_.state != UpdateState::staged) {
        return UpdateGuardError::invalid_state;
    }
    status_.candidate = {};
    status_.state = UpdateState::idle;
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::begin_boot(
    const BootObservation& observation) {
    if (!status_.running ||
        (status_.state != UpdateState::pending_reboot &&
         status_.state != UpdateState::trial)) {
        return UpdateGuardError::invalid_state;
    }
    if (observation.boot_session_id == 0 ||
        !known_slot(observation.slot)) {
        return UpdateGuardError::invalid_candidate;
    }
    if (status_.state == UpdateState::trial &&
        observation.boot_session_id == status_.trial_boot_session_id) {
        return UpdateGuardError::invalid_state;
    }
    if (observation.version != status_.candidate.version ||
        observation.slot != status_.candidate.target_slot) {
        require_rollback(RollbackReason::boot_mismatch);
        return UpdateGuardError::boot_mismatch;
    }
    if (status_.trial_boots >= policy_.maximum_trial_boots) {
        require_rollback(RollbackReason::boot_attempt_limit);
        return UpdateGuardError::boot_attempt_limit;
    }

    ++status_.trial_boots;
    status_.state = UpdateState::trial;
    status_.trial_boot_session_id = observation.boot_session_id;
    status_.trial_started_ms = observation.monotonic_ms;
    status_.last_monotonic_ms = observation.monotonic_ms;
    status_.observed_health_mask = 0;
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::report_health(
    std::uint32_t boot_session_id,
    std::uint32_t passing_health_mask,
    std::uint64_t now_ms) {
    if (!status_.running || status_.state != UpdateState::trial ||
        boot_session_id != status_.trial_boot_session_id) {
        return UpdateGuardError::invalid_state;
    }
    if (passing_health_mask == 0 ||
        (passing_health_mask & ~kAllTrialHealthBits) != 0) {
        return UpdateGuardError::invalid_candidate;
    }
    const auto clock = advance_clock(now_ms);
    if (clock != UpdateGuardError::none) {
        return clock;
    }
    status_.observed_health_mask |= passing_health_mask;
    return tick(now_ms);
}

UpdateGuardError UpdateBootGuard::tick(std::uint64_t now_ms) {
    if (!status_.running || status_.state != UpdateState::trial) {
        return UpdateGuardError::invalid_state;
    }
    const auto clock = advance_clock(now_ms);
    if (clock != UpdateGuardError::none) {
        return clock;
    }
    if (now_ms - status_.trial_started_ms >=
        policy_.confirmation_deadline_ms) {
        require_rollback(RollbackReason::confirmation_timeout);
        return UpdateGuardError::confirmation_timeout;
    }
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::confirm(
    std::uint32_t boot_session_id,
    std::uint64_t now_ms) {
    if (!status_.running || status_.state != UpdateState::trial ||
        boot_session_id != status_.trial_boot_session_id) {
        return UpdateGuardError::invalid_state;
    }
    const auto time = tick(now_ms);
    if (time != UpdateGuardError::none) {
        return time;
    }
    if (now_ms - status_.trial_started_ms < policy_.minimum_stable_ms) {
        return UpdateGuardError::insufficient_stable_time;
    }
    if ((status_.observed_health_mask & policy_.required_health_mask) !=
        policy_.required_health_mask) {
        return UpdateGuardError::insufficient_health;
    }
    status_.state = UpdateState::confirmed;
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::request_rollback(
    RollbackReason reason) {
    if (!status_.running ||
        (status_.state != UpdateState::pending_reboot &&
         status_.state != UpdateState::trial) ||
        !known_rollback_reason(reason)) {
        return UpdateGuardError::invalid_state;
    }
    require_rollback(reason);
    return UpdateGuardError::none;
}

UpdateGuardError UpdateBootGuard::complete_rollback(
    const BootObservation& observation) {
    if (!status_.running ||
        status_.state != UpdateState::rollback_required) {
        return UpdateGuardError::invalid_state;
    }
    if (observation.boot_session_id == 0 ||
        observation.version != policy_.current_version ||
        observation.slot != policy_.current_slot) {
        return UpdateGuardError::rollback_mismatch;
    }
    status_.state = UpdateState::rolled_back;
    status_.trial_boot_session_id = observation.boot_session_id;
    status_.last_monotonic_ms = observation.monotonic_ms;
    return UpdateGuardError::none;
}

UpdateGuardStatus UpdateBootGuard::status() const {
    return status_;
}

UpdateGuardError UpdateBootGuard::advance_clock(std::uint64_t now_ms) {
    if (now_ms < status_.last_monotonic_ms) {
        return UpdateGuardError::clock_regression;
    }
    status_.last_monotonic_ms = now_ms;
    return UpdateGuardError::none;
}

void UpdateBootGuard::require_rollback(RollbackReason reason) {
    status_.state = UpdateState::rollback_required;
    status_.rollback_reason = reason;
}

}  // namespace opentrail::update


