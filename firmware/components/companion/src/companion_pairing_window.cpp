#include "opentrail/companion_pairing_window.hpp"

#include <array>
#include <cstddef>
#include <limits>

namespace opentrail::companion {
namespace {
constexpr std::uint32_t kUniformAcceptanceLimit = 4294000000U;

void secure_clear(void* data, std::size_t bytes) {
    auto* current = static_cast<volatile std::uint8_t*>(data);
    while (bytes-- != 0) *current++ = 0;
}

std::uint32_t decode_little_endian(
    const std::array<std::uint8_t, 4>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::array<char, 6> decimal_digits(std::uint32_t passkey) {
    std::array<char, 6> digits{};
    for (std::size_t index = digits.size(); index != 0; --index) {
        digits[index - 1] = static_cast<char>('0' + (passkey % 10U));
        passkey /= 10U;
    }
    return digits;
}

bool valid_candidate(CompanionPairingCandidate candidate) {
    return candidate.connection_handle != 0xFFFF &&
           candidate.transport_generation != 0;
}

bool valid_terminal(CompanionPairingAttemptTerminal terminal) {
    switch (terminal) {
        case CompanionPairingAttemptTerminal::secure_bond_complete:
        case CompanionPairingAttemptTerminal::pairing_failed:
        case CompanionPairingAttemptTerminal::cancelled:
            return true;
    }
    return false;
}
}  // namespace

CompanionPairingWindow::CompanionPairingWindow(
    security::SecureRandomSource& random,
    CompanionPairingPinDisplayPort& display)
    : random_(random), display_(display) {}

bool CompanionPairingWindow::enter_operation() {
    if (operation_active_) return false;
    operation_active_ = true;
    return true;
}
void CompanionPairingWindow::leave_operation() { operation_active_ = false; }

CompanionPairingWindowError CompanionPairingWindow::observe_time(
    std::uint64_t now_ms) {
    if (clock_initialized_ && now_ms < last_now_ms_) {
        return contain_fault(CompanionPairingWindowError::clock_rollback);
    }
    clock_initialized_ = true;
    last_now_ms_ = now_ms;
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError CompanionPairingWindow::sample_passkey(
    std::uint32_t& passkey) {
    if (random_.state() == security::EntropyState::not_ready)
        return CompanionPairingWindowError::entropy_not_ready;
    if (random_.state() != security::EntropyState::ready)
        return CompanionPairingWindowError::entropy_failed;
    std::array<std::uint8_t, 4> bytes{};
    for (std::uint8_t draw = 0;
         draw < kCompanionPairingMaximumRandomDraws; ++draw) {
        const auto filled = random_.fill(bytes.data(), bytes.size());
        if (!filled.ok() || filled.bytes_written != bytes.size()) {
            secure_clear(bytes.data(), bytes.size());
            return CompanionPairingWindowError::entropy_failed;
        }
        const auto sample = decode_little_endian(bytes);
        secure_clear(bytes.data(), bytes.size());
        if (sample < kUniformAcceptanceLimit) {
            passkey = sample % kCompanionPairingPasskeyCount;
            return CompanionPairingWindowError::none;
        }
    }
    return CompanionPairingWindowError::random_rejection_exhausted;
}

void CompanionPairingWindow::clear_private_state() {
    secure_clear(&passkey_, sizeof(passkey_));
    candidate_ = {};
    candidate_bound_ = false;
    candidate_bonded_ = false;
    deadline_ms_ = 0;
    status_.passkey_displayed = false;
    status_.attempt_consumed = false;
}

CompanionPairingWindowError
CompanionPairingWindow::require_candidate_cleanup(
    CompanionPairingWindowError result) {
    const bool needed_clear = status_.passkey_displayed;
    secure_clear(&passkey_, sizeof(passkey_));
    deadline_ms_ = 0;
    status_.passkey_displayed = false;
    status_.phase = CompanionPairingWindowPhase::candidate_cleanup_required;
    if (needed_clear && !display_.clear_pairing_pin()) {
        return CompanionPairingWindowError::display_clear_failed;
    }
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::close_window(
    CompanionPairingWindowError result) {
    const bool needed_clear = status_.passkey_displayed;
    clear_private_state();
    status_.phase = CompanionPairingWindowPhase::closed;
    if (needed_clear && !display_.clear_pairing_pin()) {
        status_.phase = CompanionPairingWindowPhase::faulted;
        return CompanionPairingWindowError::display_clear_failed;
    }
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::contain_fault(
    CompanionPairingWindowError result) {
    if (candidate_bonded_) {
        return require_candidate_cleanup(result);
    }
    const bool needed_clear = status_.passkey_displayed;
    clear_private_state();
    status_.phase = CompanionPairingWindowPhase::faulted;
    if (needed_clear && !display_.clear_pairing_pin())
        return CompanionPairingWindowError::display_clear_failed;
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::open_window(
    std::uint64_t now_ms, std::uint64_t physical_event,
    std::uint64_t hold_ms, bool released) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const auto observed = observe_time(now_ms);
    if (observed != CompanionPairingWindowError::none) {
        leave_operation(); return observed;
    }
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if (status_.phase != CompanionPairingWindowPhase::closed) {
        leave_operation();
        return CompanionPairingWindowError::window_not_available;
    }
    if (hold_ms < kCompanionPairingMinimumHoldMs || !released) {
        leave_operation();
        return CompanionPairingWindowError::physical_gesture_rejected;
    }
    if (physical_event == 0 || physical_event <= last_physical_event_) {
        leave_operation();
        return CompanionPairingWindowError::stale_physical_event;
    }
    if (now_ms > std::numeric_limits<std::uint64_t>::max() -
                     kCompanionPairingWindowMs) {
        const auto result = contain_fault(
            CompanionPairingWindowError::deadline_overflow);
        leave_operation(); return result;
    }

    last_physical_event_ = physical_event;
    std::uint32_t sampled_passkey = 0;
    const auto sampled = sample_passkey(sampled_passkey);
    if (sampled != CompanionPairingWindowError::none) {
        secure_clear(&sampled_passkey, sizeof(sampled_passkey));
        leave_operation(); return sampled;
    }
    auto digits = decimal_digits(sampled_passkey);
    if (!display_.show_pairing_pin(digits)) {
        secure_clear(digits.data(), digits.size());
        secure_clear(&sampled_passkey, sizeof(sampled_passkey));
        (void)display_.clear_pairing_pin();
        const auto result = contain_fault(
            CompanionPairingWindowError::display_failed);
        leave_operation(); return result;
    }
    secure_clear(digits.data(), digits.size());
    passkey_ = sampled_passkey;
    secure_clear(&sampled_passkey, sizeof(sampled_passkey));
    deadline_ms_ = now_ms + kCompanionPairingWindowMs;
    status_.phase = CompanionPairingWindowPhase::open;
    status_.passkey_displayed = true;
    status_.attempt_consumed = false;
    leave_operation();
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError
CompanionPairingWindow::open_unowned_boot_window(
    std::uint64_t now_ms, std::uint64_t boot_event) {
    if (unowned_boot_window_consumed_) {
        return CompanionPairingWindowError::attempt_already_consumed;
    }
    const auto result = open_window(now_ms, boot_event,
                                    kCompanionPairingMinimumHoldMs, true);
    if (result == CompanionPairingWindowError::none) {
        unowned_boot_window_consumed_ = true;
    }
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::service(
    std::uint64_t now_ms) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const auto observed = observe_time(now_ms);
    if (observed != CompanionPairingWindowError::none) {
        leave_operation(); return observed;
    }
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if ((status_.phase == CompanionPairingWindowPhase::open ||
         status_.phase == CompanionPairingWindowPhase::attempt_active) &&
        now_ms >= deadline_ms_) {
        const auto result = candidate_bonded_
            ? require_candidate_cleanup(
                  CompanionPairingWindowError::candidate_cleanup_required)
            : close_window(CompanionPairingWindowError::window_expired);
        leave_operation(); return result;
    }
    leave_operation();
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError CompanionPairingWindow::handle_passkey_action(
    std::uint64_t now_ms, CompanionPairingCandidate candidate,
    CompanionPairingPasskeyPort& passkey_port) {
    return handle_passkey_action_impl(
        now_ms, candidate, passkey_port, false);
}

CompanionPairingWindowError
CompanionPairingWindow::handle_passkey_action_deferred_cleanup(
    std::uint64_t now_ms, CompanionPairingCandidate candidate,
    CompanionPairingPasskeyPort& passkey_port) {
    return handle_passkey_action_impl(
        now_ms, candidate, passkey_port, true);
}

CompanionPairingWindowError
CompanionPairingWindow::handle_passkey_action_impl(
    std::uint64_t now_ms, CompanionPairingCandidate candidate,
    CompanionPairingPasskeyPort& passkey_port,
    bool defer_cleanup) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const auto observed = defer_cleanup
        ? (clock_initialized_ && now_ms < last_now_ms_
               ? CompanionPairingWindowError::clock_rollback
               : CompanionPairingWindowError::none)
        : observe_time(now_ms);
    if (observed != CompanionPairingWindowError::none) {
        leave_operation(); return observed;
    }
    if (defer_cleanup) {
        clock_initialized_ = true;
        last_now_ms_ = now_ms;
    }
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if (status_.phase == CompanionPairingWindowPhase::closed) {
        leave_operation(); return CompanionPairingWindowError::window_closed;
    }
    if (status_.phase ==
        CompanionPairingWindowPhase::candidate_cleanup_required) {
        leave_operation();
        return CompanionPairingWindowError::candidate_cleanup_required;
    }
    if ((status_.phase == CompanionPairingWindowPhase::open ||
         status_.phase == CompanionPairingWindowPhase::attempt_active) &&
        now_ms >= deadline_ms_) {
        const auto result = candidate_bonded_
            ? (defer_cleanup
                   ? CompanionPairingWindowError::candidate_cleanup_required
                   : require_candidate_cleanup(
                         CompanionPairingWindowError::
                             candidate_cleanup_required))
            : (defer_cleanup
                   ? CompanionPairingWindowError::window_expired
                   : close_window(CompanionPairingWindowError::window_expired));
        leave_operation(); return result;
    }
    if (!valid_candidate(candidate)) {
        leave_operation(); return CompanionPairingWindowError::invalid_argument;
    }
    if (status_.attempt_consumed ||
        status_.phase == CompanionPairingWindowPhase::attempt_active) {
        const auto result = candidate_bound_ && !(candidate == candidate_)
            ? CompanionPairingWindowError::candidate_mismatch
            : CompanionPairingWindowError::attempt_already_consumed;
        leave_operation(); return result;
    }
    candidate_ = candidate;
    candidate_bound_ = true;
    status_.attempt_consumed = true;
    status_.phase = CompanionPairingWindowPhase::attempt_active;
    if (!passkey_port.inject_display_passkey(candidate, passkey_)) {
        const auto result = defer_cleanup
            ? CompanionPairingWindowError::passkey_injection_failed
            : close_window(
                  CompanionPairingWindowError::passkey_injection_failed);
        leave_operation(); return result;
    }
    leave_operation();
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError
CompanionPairingWindow::reserve_secure_bond_terminal(
    std::uint64_t now_ms, CompanionPairingCandidate candidate) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    // This path runs in the synchronous NimBLE callback. Observe the clock and
    // mutate only private state; display cleanup belongs to the serialized
    // owner after durable owner publication has succeeded or been contained.
    if (clock_initialized_ && now_ms < last_now_ms_) {
        leave_operation();
        return CompanionPairingWindowError::clock_rollback;
    }
    clock_initialized_ = true;
    last_now_ms_ = now_ms;
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if (status_.phase == CompanionPairingWindowPhase::closed) {
        leave_operation(); return CompanionPairingWindowError::window_closed;
    }
    if (status_.phase ==
        CompanionPairingWindowPhase::candidate_cleanup_required) {
        leave_operation();
        return CompanionPairingWindowError::candidate_cleanup_required;
    }
    if (status_.phase != CompanionPairingWindowPhase::attempt_active ||
        !candidate_bound_) {
        leave_operation();
        return CompanionPairingWindowError::terminal_not_pending;
    }
    if (!valid_candidate(candidate) || !(candidate == candidate_)) {
        leave_operation();
        return CompanionPairingWindowError::candidate_mismatch;
    }

    // NimBLE has already created the exact candidate bond by this callback.
    // From this point, every non-success path must retain cleanup authority.
    candidate_bonded_ = true;
    secure_clear(&passkey_, sizeof(passkey_));
    if (now_ms >= deadline_ms_) {
        status_.phase =
            CompanionPairingWindowPhase::candidate_cleanup_required;
        leave_operation();
        return CompanionPairingWindowError::window_expired;
    }
    status_.phase =
        CompanionPairingWindowPhase::secure_bond_terminal_reserved;
    leave_operation();
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError CompanionPairingWindow::finish_attempt(
    std::uint64_t now_ms, CompanionPairingCandidate candidate,
    CompanionPairingAttemptTerminal terminal) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    if (status_.phase ==
        CompanionPairingWindowPhase::secure_bond_terminal_reserved) {
        if (!valid_candidate(candidate) || !(candidate == candidate_)) {
            leave_operation();
            return CompanionPairingWindowError::candidate_mismatch;
        }
        if (!valid_terminal(terminal)) {
            leave_operation();
            return CompanionPairingWindowError::invalid_argument;
        }
        const auto result =
            terminal == CompanionPairingAttemptTerminal::secure_bond_complete
                ? close_window(CompanionPairingWindowError::none)
                : require_candidate_cleanup(
                      CompanionPairingWindowError::candidate_cleanup_required);
        leave_operation();
        return result;
    }
    const auto observed = observe_time(now_ms);
    if (observed != CompanionPairingWindowError::none) {
        leave_operation(); return observed;
    }
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if ((status_.phase == CompanionPairingWindowPhase::open ||
         status_.phase == CompanionPairingWindowPhase::attempt_active) &&
        now_ms >= deadline_ms_) {
        const auto result = candidate_bonded_
            ? require_candidate_cleanup(
                  CompanionPairingWindowError::candidate_cleanup_required)
            : close_window(CompanionPairingWindowError::window_expired);
        leave_operation(); return result;
    }
    if (status_.phase ==
        CompanionPairingWindowPhase::candidate_cleanup_required) {
        leave_operation();
        return CompanionPairingWindowError::candidate_cleanup_required;
    }
    if (status_.phase != CompanionPairingWindowPhase::attempt_active ||
        !candidate_bound_) {
        leave_operation();
        return CompanionPairingWindowError::terminal_not_pending;
    }
    if (!valid_candidate(candidate) || !(candidate == candidate_)) {
        leave_operation();
        return CompanionPairingWindowError::candidate_mismatch;
    }
    if (!valid_terminal(terminal)) {
        leave_operation(); return CompanionPairingWindowError::invalid_argument;
    }
    const auto result = close_window(CompanionPairingWindowError::none);
    leave_operation();
    return result;
}

CompanionPairingWindowError
CompanionPairingWindow::complete_candidate_cleanup(
    CompanionPairingCandidate candidate) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if (status_.phase !=
            CompanionPairingWindowPhase::candidate_cleanup_required ||
        !candidate_bound_ || !candidate_bonded_) {
        leave_operation();
        return CompanionPairingWindowError::candidate_cleanup_not_pending;
    }
    if (!valid_candidate(candidate) || !(candidate == candidate_)) {
        leave_operation();
        return CompanionPairingWindowError::candidate_mismatch;
    }
    const auto result = close_window(CompanionPairingWindowError::none);
    leave_operation();
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::disconnect(
    std::uint64_t now_ms, CompanionPairingCandidate candidate) {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const auto observed = observe_time(now_ms);
    if (observed != CompanionPairingWindowError::none) {
        leave_operation(); return observed;
    }
    if (status_.phase == CompanionPairingWindowPhase::faulted) {
        leave_operation(); return CompanionPairingWindowError::faulted;
    }
    if (status_.phase == CompanionPairingWindowPhase::closed) {
        leave_operation(); return CompanionPairingWindowError::window_closed;
    }
    if (status_.phase ==
        CompanionPairingWindowPhase::candidate_cleanup_required) {
        leave_operation();
        return CompanionPairingWindowError::candidate_cleanup_required;
    }
    if (now_ms >= deadline_ms_) {
        const auto result = candidate_bonded_
            ? require_candidate_cleanup(
                  CompanionPairingWindowError::candidate_cleanup_required)
            : close_window(CompanionPairingWindowError::window_expired);
        leave_operation(); return result;
    }
    if (candidate_bound_ &&
        (!valid_candidate(candidate) || !(candidate == candidate_))) {
        leave_operation();
        return CompanionPairingWindowError::candidate_mismatch;
    }
    const auto result = candidate_bonded_
        ? require_candidate_cleanup(
              CompanionPairingWindowError::candidate_cleanup_required)
        : close_window(CompanionPairingWindowError::none);
    leave_operation();
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::restart() {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const bool cleanup_required = candidate_bonded_;
    const auto result = close_window(
        cleanup_required
            ? CompanionPairingWindowError::candidate_cleanup_required
            : CompanionPairingWindowError::none);
    clock_initialized_ = false;
    last_now_ms_ = 0;
    last_physical_event_ = 0;
    leave_operation();
    return result;
}

CompanionPairingWindowError CompanionPairingWindow::fault() {
    if (!enter_operation()) return CompanionPairingWindowError::reentrant_call;
    const auto result = contain_fault(CompanionPairingWindowError::faulted);
    leave_operation();
    return result;
}

CompanionPairingWindowStatus CompanionPairingWindow::status() const {
    return status_;
}

}  // namespace opentrail::companion
