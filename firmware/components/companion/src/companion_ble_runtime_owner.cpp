#include "opentrail/companion_ble_runtime_owner.hpp"

#include <limits>

namespace opentrail::companion {

CompanionBleRuntimeOwner::CompanionBleRuntimeOwner(
    CompanionBleRuntimePort& port,
    CompanionBleRuntimePolicy policy)
    : port_(port), policy_(policy) {}

bool CompanionBleRuntimeOwner::valid_policy() const {
    return policy_.host_sync_timeout_ms != 0 &&
           policy_.restart_delay_ms != 0 &&
           policy_.max_restart_attempts != 0 &&
           policy_.max_restart_attempts <= 8 &&
           policy_.public_link_window_ms != 0 &&
           policy_.termination_ack_timeout_ms != 0;
}

bool CompanionBleRuntimeOwner::deadline_reached(
    std::uint64_t now_ms,
    std::uint64_t deadline_ms) const {
    return now_ms >= deadline_ms;
}

bool CompanionBleRuntimeOwner::add_deadline(
    std::uint64_t now_ms,
    std::uint64_t delay_ms,
    std::uint64_t& deadline) const {
    if (now_ms > std::numeric_limits<std::uint64_t>::max() - delay_ms) {
        return false;
    }
    deadline = now_ms + delay_ms;
    return true;
}

bool CompanionBleRuntimeOwner::enter_operation() {
    if (operation_active_) {
        reentry_observed_ = true;
        return false;
    }
    operation_active_ = true;
    reentry_observed_ = false;
    return true;
}

void CompanionBleRuntimeOwner::leave_operation() {
    operation_active_ = false;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::contain(
    CompanionBleRuntimeError error) {
    if (status_.phase == CompanionBleRuntimePhase::contained) {
        return CompanionBleRuntimeError::contained;
    }
    status_.phase = CompanionBleRuntimePhase::contained;
    status_.terminal_error = error;
    status_.connection_handle = kCompanionBleInvalidConnectionHandle;
    status_.restart_token = 0;
    status_.termination_pending = false;
    if (!port_.contain_stack()) {
        status_.terminal_error =
            CompanionBleRuntimeError::stack_shutdown_failed;
        return CompanionBleRuntimeError::stack_shutdown_failed;
    }
    return error;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::start(
    std::uint64_t now_ms,
    bool authorization_admission_denied) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::dormant) {
        leave_operation();
        return CompanionBleRuntimeError::already_started;
    }
    if (!valid_policy() || !authorization_admission_denied ||
        !add_deadline(now_ms, policy_.host_sync_timeout_ms,
                      host_sync_deadline_ms_)) {
        leave_operation();
        return CompanionBleRuntimeError::invalid_argument;
    }
    if (!port_.initialize_stack() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_ ? CompanionBleRuntimeError::reentrant_call
                              : CompanionBleRuntimeError::stack_init_failed);
        leave_operation();
        return result;
    }
    if (!port_.configure_secure_connections_bonding() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_
                ? CompanionBleRuntimeError::reentrant_call
                : CompanionBleRuntimeError::security_config_failed);
        leave_operation();
        return result;
    }
    if (!port_.register_protected_service() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_
                ? CompanionBleRuntimeError::reentrant_call
                : CompanionBleRuntimeError::service_registration_failed);
        leave_operation();
        return result;
    }
    // Stage callback admission before the platform may create its host task.
    // A real integration must additionally serialize that task against this
    // owner; staging prevents a queued sync callback from seeing dormant.
    status_.phase = CompanionBleRuntimePhase::waiting_for_host_sync;
    if (!port_.start_host_task() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_ ? CompanionBleRuntimeError::reentrant_call
                              : CompanionBleRuntimeError::host_task_start_failed);
        leave_operation();
        return result;
    }
    leave_operation();
    return CompanionBleRuntimeError::none;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::host_synced(
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::waiting_for_host_sync) {
        leave_operation();
        return CompanionBleRuntimeError::host_sync_out_of_order;
    }
    if (deadline_reached(now_ms, host_sync_deadline_ms_)) {
        const auto result = contain(CompanionBleRuntimeError::startup_timeout);
        leave_operation();
        return result;
    }
    if (!port_.configure_public_service_advertising() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_
                ? CompanionBleRuntimeError::reentrant_call
                : CompanionBleRuntimeError::advertising_config_failed);
        leave_operation();
        return result;
    }
    if (!port_.start_advertising() || reentry_observed_) {
        const auto result = contain(
            reentry_observed_
                ? CompanionBleRuntimeError::reentrant_call
                : CompanionBleRuntimeError::advertising_start_failed);
        leave_operation();
        return result;
    }
    status_.phase = CompanionBleRuntimePhase::advertising;
    leave_operation();
    return CompanionBleRuntimeError::none;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::connection_opened(
    std::uint16_t connection_handle,
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (connection_handle == kCompanionBleInvalidConnectionHandle) {
        leave_operation();
        return CompanionBleRuntimeError::invalid_argument;
    }
    if (status_.phase == CompanionBleRuntimePhase::connected) {
        const auto terminated = port_.terminate_connection(connection_handle);
        if (!terminated || reentry_observed_) {
            const auto result = contain(
                reentry_observed_
                    ? CompanionBleRuntimeError::reentrant_call
                    : CompanionBleRuntimeError::connection_termination_failed);
            leave_operation();
            return result;
        }
        leave_operation();
        return CompanionBleRuntimeError::connection_in_use;
    }
    if (status_.phase != CompanionBleRuntimePhase::advertising) {
        const auto terminated = port_.terminate_connection(connection_handle);
        if (!terminated || reentry_observed_) {
            const auto result = contain(
                reentry_observed_
                    ? CompanionBleRuntimeError::reentrant_call
                    : CompanionBleRuntimeError::connection_termination_failed);
            leave_operation();
            return result;
        }
        leave_operation();
        return CompanionBleRuntimeError::contained;
    }
    if (!add_deadline(now_ms, policy_.public_link_window_ms,
                      public_link_deadline_ms_)) {
        const auto result = contain(CompanionBleRuntimeError::invalid_argument);
        leave_operation();
        return result;
    }
    status_.phase = CompanionBleRuntimePhase::connected;
    status_.connection_handle = connection_handle;
    status_.restart_token = 0;
    status_.termination_pending = false;
    leave_operation();
    return CompanionBleRuntimeError::none;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::next_restart(
    std::uint64_t now_ms) {
    if (next_restart_token_ == 0 ||
        next_restart_token_ == std::numeric_limits<std::uint64_t>::max() ||
        !add_deadline(now_ms, policy_.restart_delay_ms, restart_deadline_ms_)) {
        return contain(CompanionBleRuntimeError::restart_exhausted);
    }
    status_.restart_token = next_restart_token_++;
    status_.phase = CompanionBleRuntimePhase::restart_wait;
    return CompanionBleRuntimeError::none;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::connection_closed(
    std::uint16_t connection_handle,
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::connected ||
        connection_handle != status_.connection_handle) {
        leave_operation();
        return CompanionBleRuntimeError::wrong_connection;
    }
    status_.connection_handle = kCompanionBleInvalidConnectionHandle;
    status_.termination_pending = false;
    status_.restart_attempts = 0;
    const auto result = next_restart(now_ms);
    leave_operation();
    return result;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::advertising_interrupted(
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::advertising) {
        leave_operation();
        return CompanionBleRuntimeError::stale_restart;
    }
    status_.restart_attempts = 0;
    const auto result = next_restart(now_ms);
    leave_operation();
    return result;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::service_restart(
    std::uint64_t expected_restart_token,
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::restart_wait ||
        expected_restart_token == 0 ||
        expected_restart_token != status_.restart_token) {
        leave_operation();
        return CompanionBleRuntimeError::stale_restart;
    }
    if (!deadline_reached(now_ms, restart_deadline_ms_)) {
        leave_operation();
        return CompanionBleRuntimeError::restart_not_due;
    }
    ++status_.restart_attempts;
    if (port_.start_advertising() && !reentry_observed_) {
        status_.phase = CompanionBleRuntimePhase::advertising;
        status_.restart_token = 0;
        status_.restart_attempts = 0;
        leave_operation();
        return CompanionBleRuntimeError::none;
    }
    if (reentry_observed_ ||
        status_.restart_attempts >= policy_.max_restart_attempts) {
        const auto result = contain(
            reentry_observed_ ? CompanionBleRuntimeError::reentrant_call
                              : CompanionBleRuntimeError::restart_exhausted);
        leave_operation();
        return result;
    }
    const auto result = next_restart(now_ms);
    leave_operation();
    return result;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::service_watchdog(
    std::uint64_t now_ms) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase == CompanionBleRuntimePhase::waiting_for_host_sync &&
        deadline_reached(now_ms, host_sync_deadline_ms_)) {
        const auto result = contain(CompanionBleRuntimeError::startup_timeout);
        leave_operation();
        return result;
    }
    if (status_.phase == CompanionBleRuntimePhase::connected &&
        !status_.termination_pending &&
        deadline_reached(now_ms, public_link_deadline_ms_)) {
        if (!add_deadline(now_ms, policy_.termination_ack_timeout_ms,
                          termination_ack_deadline_ms_)) {
            const auto result = contain(
                CompanionBleRuntimeError::connection_termination_failed);
            leave_operation();
            return result;
        }
        status_.termination_pending = true;
        const auto terminated =
            port_.terminate_connection(status_.connection_handle);
        if (!terminated || reentry_observed_) {
            const auto result = contain(
                reentry_observed_
                    ? CompanionBleRuntimeError::reentrant_call
                    : CompanionBleRuntimeError::connection_termination_failed);
            leave_operation();
            return result;
        }
    } else if (status_.phase == CompanionBleRuntimePhase::connected &&
               status_.termination_pending &&
               deadline_reached(now_ms, termination_ack_deadline_ms_)) {
        const auto result = contain(
            CompanionBleRuntimeError::connection_termination_failed);
        leave_operation();
        return result;
    }
    const auto result = status_.phase == CompanionBleRuntimePhase::contained
                            ? CompanionBleRuntimeError::contained
                            : CompanionBleRuntimeError::none;
    leave_operation();
    return result;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::host_reset() {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    const auto result = contain(CompanionBleRuntimeError::host_reset);
    leave_operation();
    return result;
}

CompanionBleRuntimeError CompanionBleRuntimeOwner::callback_overflow() {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    const auto result = contain(
        CompanionBleRuntimeError::callback_queue_overflow);
    leave_operation();
    return result;
}

CompanionBleRuntimeError
CompanionBleRuntimeOwner::connection_termination_failed(
    std::uint16_t connection_handle) {
    if (!enter_operation()) {
        return CompanionBleRuntimeError::reentrant_call;
    }
    if (status_.phase != CompanionBleRuntimePhase::connected ||
        connection_handle != status_.connection_handle) {
        leave_operation();
        return CompanionBleRuntimeError::wrong_connection;
    }
    const auto result = contain(
        CompanionBleRuntimeError::connection_termination_failed);
    leave_operation();
    return result;
}

CompanionBleRuntimeStatus CompanionBleRuntimeOwner::status() const {
    return status_;
}

}  // namespace opentrail::companion
