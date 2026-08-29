#pragma once

#include <array>
#include <cstdint>

#include "opentrail/secure_random.hpp"

namespace opentrail::companion {

inline constexpr std::uint64_t kCompanionPairingMinimumHoldMs = 3000;
inline constexpr std::uint64_t kCompanionPairingWindowMs = 30000;
inline constexpr std::uint32_t kCompanionPairingPasskeyCount = 1000000;
inline constexpr std::uint8_t kCompanionPairingMaximumRandomDraws = 16;

enum class CompanionPairingPurpose : std::uint8_t { claim = 0, replacement };
enum class CompanionPairingWindowPhase : std::uint8_t {
    closed = 0, open, attempt_active, faulted,
};
enum class CompanionPairingWindowError : std::uint8_t {
    none = 0, invalid_argument, reentrant_call, faulted,
    window_not_available, physical_gesture_rejected, stale_physical_event,
    clock_rollback, deadline_overflow, entropy_not_ready, entropy_failed,
    random_rejection_exhausted, display_failed, display_clear_failed,
    window_closed, window_expired, attempt_already_consumed,
    candidate_mismatch, passkey_injection_failed, terminal_not_pending,
};
enum class CompanionPairingAttemptTerminal : std::uint8_t {
    secure_bond_complete = 0, pairing_failed, cancelled,
};

struct CompanionPairingCandidate {
    std::uint16_t connection_handle{0xFFFF};
    std::uint64_t transport_generation{0};
};
[[nodiscard]] constexpr bool operator==(
    const CompanionPairingCandidate& left,
    const CompanionPairingCandidate& right) {
    return left.connection_handle == right.connection_handle &&
           left.transport_generation == right.transport_generation;
}

struct CompanionPairingWindowStatus {
    CompanionPairingWindowPhase phase{CompanionPairingWindowPhase::closed};
    CompanionPairingPurpose purpose{CompanionPairingPurpose::claim};
    bool passkey_displayed{false};
    bool attempt_consumed{false};
};

// Digits are supplied only to the target-local display adapter. They must not
// be logged, persisted, returned through status(), or enter public evidence.
class CompanionPairingPinDisplayPort {
public:
    virtual ~CompanionPairingPinDisplayPort() = default;
    [[nodiscard]] virtual bool show_pairing_pin(
        const std::array<char, 6>& digits) = 0;
    [[nodiscard]] virtual bool clear_pairing_pin() = 0;
};

// The numeric passkey crosses this seam only for the exact active candidate's
// display-passkey action. Implementations inject it directly and never retain
// or log it.
class CompanionPairingPasskeyPort {
public:
    virtual ~CompanionPairingPasskeyPort() = default;
    [[nodiscard]] virtual bool inject_display_passkey(
        CompanionPairingCandidate candidate, std::uint32_t passkey) = 0;
};

// Pure externally serialized OTBP0 window/passkey owner. It creates no
// application authorization, durable owner, or bond-persistence claim.
class CompanionPairingWindow {
public:
    CompanionPairingWindow(security::SecureRandomSource& random,
                           CompanionPairingPinDisplayPort& display);
    [[nodiscard]] CompanionPairingWindowError open_window(
        std::uint64_t now_ms, std::uint64_t physical_event,
        std::uint64_t hold_ms, bool released,
        CompanionPairingPurpose purpose);
    [[nodiscard]] CompanionPairingWindowError service(std::uint64_t now_ms);
    [[nodiscard]] CompanionPairingWindowError handle_passkey_action(
        std::uint64_t now_ms, CompanionPairingCandidate candidate,
        CompanionPairingPasskeyPort& passkey_port);
    // Synchronous BLE security callbacks must answer immediately, but they
    // must never drive the OLED.  This variant leaves timeout, clock-fault,
    // and injection-failure cleanup to the serialized application owner.
    [[nodiscard]] CompanionPairingWindowError
    handle_passkey_action_deferred_cleanup(
        std::uint64_t now_ms, CompanionPairingCandidate candidate,
        CompanionPairingPasskeyPort& passkey_port);
    [[nodiscard]] CompanionPairingWindowError finish_attempt(
        std::uint64_t now_ms, CompanionPairingCandidate candidate,
        CompanionPairingAttemptTerminal terminal);
    [[nodiscard]] CompanionPairingWindowError disconnect(
        std::uint64_t now_ms, CompanionPairingCandidate candidate);
    [[nodiscard]] CompanionPairingWindowError restart();
    [[nodiscard]] CompanionPairingWindowError fault();
    [[nodiscard]] CompanionPairingWindowStatus status() const;

private:
    [[nodiscard]] bool enter_operation();
    void leave_operation();
    [[nodiscard]] CompanionPairingWindowError observe_time(std::uint64_t now_ms);
    [[nodiscard]] CompanionPairingWindowError sample_passkey(
        std::uint32_t& passkey);
    [[nodiscard]] CompanionPairingWindowError handle_passkey_action_impl(
        std::uint64_t now_ms, CompanionPairingCandidate candidate,
        CompanionPairingPasskeyPort& passkey_port,
        bool defer_cleanup);
    [[nodiscard]] CompanionPairingWindowError close_window(
        CompanionPairingWindowError result);
    [[nodiscard]] CompanionPairingWindowError contain_fault(
        CompanionPairingWindowError result);
    void clear_private_state();

    security::SecureRandomSource& random_;
    CompanionPairingPinDisplayPort& display_;
    CompanionPairingWindowStatus status_{};
    CompanionPairingCandidate candidate_{};
    std::uint32_t passkey_{0};
    std::uint64_t deadline_ms_{0};
    std::uint64_t last_now_ms_{0};
    std::uint64_t last_physical_event_{0};
    bool clock_initialized_{false};
    bool candidate_bound_{false};
    bool operation_active_{false};
};

}  // namespace opentrail::companion
