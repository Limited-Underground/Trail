#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_secure_random.hpp"
#include "opentrail/companion_pairing_window.hpp"

namespace {
using namespace opentrail::companion;
using opentrail::security::EntropyState;
using opentrail::security::test_support::FakeSecureRandomSource;

int failures = 0;
void expect(bool value, const char* expression, int line) {
    if (!value) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(value) expect((value), #value, __LINE__)

class Display final : public CompanionPairingPinDisplayPort {
public:
    bool show_pairing_pin(const std::array<char, 6>& value) override {
        ++shows;
        digits = value;
        if (on_show) on_show(context);
        return show_ok;
    }
    bool clear_pairing_pin() override {
        ++clears;
        digits.fill('\0');
        return clear_ok;
    }
    bool show_ok{true};
    bool clear_ok{true};
    std::size_t shows{0};
    std::size_t clears{0};
    std::array<char, 6> digits{};
    void (*on_show)(void*){nullptr};
    void* context{nullptr};
};

class PasskeyPort final : public CompanionPairingPasskeyPort {
public:
    bool inject_display_passkey(CompanionPairingCandidate value,
                                std::uint32_t secret) override {
        ++calls;
        candidate = value;
        observed = secret;
        return succeeds;
    }
    bool succeeds{true};
    std::size_t calls{0};
    CompanionPairingCandidate candidate{};
    std::uint32_t observed{0};
};

struct Fixture {
    Fixture() : window(random, display) {
        random.set_state(EntropyState::ready);
    }
    void word(std::uint32_t value) {
        const std::array<std::uint8_t, 4> bytes{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 24U)};
        EXPECT(random.load_bytes(bytes.data(), bytes.size()));
    }
    CompanionPairingWindowError open(
        std::uint64_t now = 0, std::uint64_t event = 1,
        std::uint64_t hold = 3000, bool released = true) {
        return window.open_window(now, event, hold, released);
    }
    FakeSecureRandomSource random;
    Display display;
    CompanionPairingWindow window;
};

constexpr CompanionPairingCandidate candidate{7, 11};
constexpr CompanionPairingCandidate other{8, 12};

void exact_gesture_and_zero_padding() {
    Fixture value;
    value.word(42);
    EXPECT(value.open(100) == CompanionPairingWindowError::none);
    EXPECT(value.display.digits ==
           (std::array<char, 6>{'0', '0', '0', '0', '4', '2'}));
    const auto status = value.window.status();
    EXPECT(status.phase == CompanionPairingWindowPhase::open);
    EXPECT(status.passkey_displayed && !status.attempt_consumed);
}

void unowned_boot_opens_one_fresh_sixty_second_window() {
    Fixture value;
    value.word(314159);
    EXPECT(value.window.open_unowned_boot_window(10, 1) ==
           CompanionPairingWindowError::none);
    EXPECT(value.display.digits ==
           (std::array<char, 6>{'3', '1', '4', '1', '5', '9'}));
    EXPECT(value.window.service(10 + kCompanionPairingWindowMs - 1) ==
           CompanionPairingWindowError::none);
    EXPECT(value.window.service(10 + kCompanionPairingWindowMs) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(value.display.clears == 1);

    value.word(271828);
    EXPECT(value.window.open_unowned_boot_window(
               10 + kCompanionPairingWindowMs + 1, 1) ==
           CompanionPairingWindowError::attempt_already_consumed);
    EXPECT(value.window.open_unowned_boot_window(
               10 + kCompanionPairingWindowMs + 1, 2) ==
           CompanionPairingWindowError::attempt_already_consumed);
    EXPECT(value.display.shows == 1);
}

void rejected_gestures_and_event_replay() {
    Fixture value;
    value.word(1);
    EXPECT(value.open(0, 1, 2999) ==
           CompanionPairingWindowError::physical_gesture_rejected);
    EXPECT(value.open(0, 1, 3000, false) ==
           CompanionPairingWindowError::physical_gesture_rejected);
    EXPECT(value.display.shows == 0);
    EXPECT(value.open() == CompanionPairingWindowError::none);
    EXPECT(value.window.disconnect(1, {}) == CompanionPairingWindowError::none);
    value.word(2);
    EXPECT(value.open(2, 1) ==
           CompanionPairingWindowError::stale_physical_event);
    EXPECT(value.display.shows == 1);
}

void unbiased_sampling_and_entropy_failure() {
    Fixture sampled;
    const std::array<std::uint8_t, 8> script{
        0xFF, 0xFF, 0xFF, 0xFF, 0x40, 0x42, 0x0F, 0x00};
    EXPECT(sampled.random.load_bytes(script.data(), script.size()));
    EXPECT(sampled.open() == CompanionPairingWindowError::none);
    EXPECT(sampled.random.fill_attempt_count() == 2);
    EXPECT(sampled.display.digits ==
           (std::array<char, 6>{'0', '0', '0', '0', '0', '0'}));

    Fixture exhausted;
    std::array<std::uint8_t, 64> rejected{};
    rejected.fill(0xFF);
    EXPECT(exhausted.random.load_bytes(rejected.data(), rejected.size()));
    EXPECT(exhausted.open() ==
           CompanionPairingWindowError::random_rejection_exhausted);
    EXPECT(exhausted.random.fill_attempt_count() == 16);
    EXPECT(exhausted.display.shows == 0);

    Fixture unavailable;
    unavailable.random.set_state(EntropyState::not_ready);
    EXPECT(unavailable.open() == CompanionPairingWindowError::entropy_not_ready);
    EXPECT(unavailable.window.status().phase ==
           CompanionPairingWindowPhase::closed);
    unavailable.random.set_state(EntropyState::failed);
    EXPECT(unavailable.open(1, 2) == CompanionPairingWindowError::entropy_failed);
}

void exact_deadline() {
    Fixture value;
    value.word(1);
    EXPECT(value.open(10) == CompanionPairingWindowError::none);
    EXPECT(value.window.service(60009) == CompanionPairingWindowError::none);
    EXPECT(value.window.status().phase == CompanionPairingWindowPhase::open);
    EXPECT(value.window.service(60010) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(value.window.status().phase == CompanionPairingWindowPhase::closed);
    EXPECT(!value.window.status().passkey_displayed);
    EXPECT(value.display.clears == 1);
}

void one_candidate_one_attempt() {
    Fixture value;
    value.word(123456);
    EXPECT(value.open() == CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(value.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(port.calls == 1 && port.candidate == candidate &&
           port.observed == 123456);
    EXPECT(value.window.status().phase ==
           CompanionPairingWindowPhase::attempt_active);
    EXPECT(value.window.status().attempt_consumed &&
           value.window.status().passkey_displayed);
    EXPECT(value.window.handle_passkey_action(2, candidate, port) ==
           CompanionPairingWindowError::attempt_already_consumed);
    EXPECT(value.window.handle_passkey_action(2, other, port) ==
           CompanionPairingWindowError::candidate_mismatch);
    EXPECT(port.calls == 1);
}

void injection_failure_consumes_window() {
    Fixture value;
    value.word(9);
    EXPECT(value.open() == CompanionPairingWindowError::none);
    PasskeyPort port;
    port.succeeds = false;
    EXPECT(value.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::passkey_injection_failed);
    EXPECT(value.window.status().phase == CompanionPairingWindowPhase::closed);
    EXPECT(!value.window.status().passkey_displayed);
    EXPECT(value.window.handle_passkey_action(2, candidate, port) ==
           CompanionPairingWindowError::window_closed);
}

void callback_safe_path_defers_all_display_cleanup() {
    Fixture expired;
    expired.word(20);
    EXPECT(expired.open(10) == CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(expired.window.handle_passkey_action_deferred_cleanup(
               60010, candidate, port) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(expired.display.clears == 0);
    EXPECT(expired.window.status().phase ==
           CompanionPairingWindowPhase::open);
    EXPECT(expired.window.service(60010) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(expired.display.clears == 1);

    Fixture injection;
    injection.word(21);
    EXPECT(injection.open() == CompanionPairingWindowError::none);
    port.succeeds = false;
    EXPECT(injection.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) ==
           CompanionPairingWindowError::passkey_injection_failed);
    EXPECT(injection.display.clears == 0);
    EXPECT(injection.window.status().phase ==
           CompanionPairingWindowPhase::attempt_active);
    EXPECT(injection.window.finish_attempt(
               2, candidate,
               CompanionPairingAttemptTerminal::pairing_failed) ==
           CompanionPairingWindowError::none);
    EXPECT(injection.display.clears == 1);

    Fixture rollback;
    rollback.word(22);
    EXPECT(rollback.open(100) == CompanionPairingWindowError::none);
    EXPECT(rollback.window.handle_passkey_action_deferred_cleanup(
               99, candidate, port) ==
           CompanionPairingWindowError::clock_rollback);
    EXPECT(rollback.display.clears == 0);
    EXPECT(rollback.window.service(99) ==
           CompanionPairingWindowError::clock_rollback);
    EXPECT(rollback.display.clears == 1);
}

void terminal_and_disconnect_binding() {
    Fixture claim;
    claim.word(9);
    EXPECT(claim.open() == CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(claim.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(claim.window.finish_attempt(
               2, candidate,
               CompanionPairingAttemptTerminal::secure_bond_complete) ==
           CompanionPairingWindowError::none);
    EXPECT(claim.window.status().phase == CompanionPairingWindowPhase::closed);

    Fixture disconnected;
    disconnected.word(11);
    EXPECT(disconnected.open() == CompanionPairingWindowError::none);
    EXPECT(disconnected.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(disconnected.window.disconnect(2, other) ==
           CompanionPairingWindowError::candidate_mismatch);
    EXPECT(disconnected.window.disconnect(2, candidate) ==
           CompanionPairingWindowError::none);
}

void bonded_candidate_interruptions_require_external_cleanup() {
    PasskeyPort port;

    Fixture aborted;
    aborted.word(26);
    EXPECT(aborted.open() == CompanionPairingWindowError::none);
    EXPECT(aborted.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(aborted.window.reserve_secure_bond_terminal(2, candidate) ==
           CompanionPairingWindowError::none);
    EXPECT(aborted.window.finish_attempt(
               2, candidate, CompanionPairingAttemptTerminal::pairing_failed) ==
           CompanionPairingWindowError::candidate_cleanup_required);
    EXPECT(aborted.window.status().phase ==
           CompanionPairingWindowPhase::candidate_cleanup_required);

    Fixture disconnected;
    disconnected.word(27);
    EXPECT(disconnected.open() == CompanionPairingWindowError::none);
    EXPECT(disconnected.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(disconnected.window.reserve_secure_bond_terminal(2, candidate) ==
           CompanionPairingWindowError::none);
    EXPECT(disconnected.window.disconnect(3, candidate) ==
           CompanionPairingWindowError::candidate_cleanup_required);
    EXPECT(disconnected.window.status().phase ==
           CompanionPairingWindowPhase::candidate_cleanup_required);

    Fixture restarted;
    restarted.word(28);
    EXPECT(restarted.open() == CompanionPairingWindowError::none);
    EXPECT(restarted.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(restarted.window.reserve_secure_bond_terminal(2, candidate) ==
           CompanionPairingWindowError::none);
    EXPECT(restarted.window.restart() ==
           CompanionPairingWindowError::candidate_cleanup_required);
    EXPECT(restarted.window.status().phase ==
           CompanionPairingWindowPhase::closed);
}

void candidate_cleanup_completion_requires_exact_bound_candidate() {
    Fixture open_only;
    open_only.word(32);
    EXPECT(open_only.open() ==
           CompanionPairingWindowError::none);
    EXPECT(open_only.window.complete_candidate_cleanup(candidate) ==
           CompanionPairingWindowError::candidate_cleanup_not_pending);
    EXPECT(open_only.window.status().phase ==
           CompanionPairingWindowPhase::open);

    Fixture value;
    value.word(33);
    EXPECT(value.open(100, 100) ==
           CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(value.window.handle_passkey_action_deferred_cleanup(
               101, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(value.window.reserve_secure_bond_terminal(102, candidate) ==
           CompanionPairingWindowError::none);
    EXPECT(value.window.finish_attempt(
               103, candidate, CompanionPairingAttemptTerminal::cancelled) ==
           CompanionPairingWindowError::candidate_cleanup_required);
    EXPECT(value.window.complete_candidate_cleanup(other) ==
           CompanionPairingWindowError::candidate_mismatch);
    EXPECT(value.window.status().phase ==
           CompanionPairingWindowPhase::candidate_cleanup_required);
    EXPECT(value.window.complete_candidate_cleanup(candidate) ==
           CompanionPairingWindowError::none);
    const auto status = value.window.status();
    EXPECT(status.phase == CompanionPairingWindowPhase::closed);
    EXPECT(!status.passkey_displayed && !status.attempt_consumed);
    EXPECT(value.display.clears == 1);
    EXPECT(value.window.complete_candidate_cleanup(candidate) ==
           CompanionPairingWindowError::candidate_cleanup_not_pending);
    EXPECT(value.window.restart() == CompanionPairingWindowError::none);
}

void pairing_failure_before_bond_closes_without_cleanup() {
    Fixture value;
    value.word(29);
    EXPECT(value.open() ==
           CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(value.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(value.window.finish_attempt(
               2, candidate,
               CompanionPairingAttemptTerminal::pairing_failed) ==
           CompanionPairingWindowError::none);
    EXPECT(value.window.status().phase == CompanionPairingWindowPhase::closed);
}

void exact_deadline_precedes_terminal_or_candidate_validation() {
    Fixture terminal;
    terminal.word(23);
    EXPECT(terminal.open() == CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(terminal.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::none);
    EXPECT(terminal.window.finish_attempt(
               60000, other,
               static_cast<CompanionPairingAttemptTerminal>(0xFF)) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(terminal.window.status().phase ==
           CompanionPairingWindowPhase::closed);
    EXPECT(terminal.display.clears == 1);

    Fixture disconnected;
    disconnected.word(24);
    EXPECT(disconnected.open() == CompanionPairingWindowError::none);
    EXPECT(disconnected.window.handle_passkey_action(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(disconnected.window.disconnect(60000, other) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(disconnected.window.status().phase ==
           CompanionPairingWindowPhase::closed);
    EXPECT(disconnected.display.clears == 1);
}

void secure_bond_terminal_reservation_enforces_exact_deadline() {
    Fixture admitted;
    admitted.word(25);
    EXPECT(admitted.open() == CompanionPairingWindowError::none);
    PasskeyPort port;
    EXPECT(admitted.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(admitted.window.reserve_secure_bond_terminal(
               59999, candidate) == CompanionPairingWindowError::none);
    EXPECT(admitted.window.status().phase ==
           CompanionPairingWindowPhase::secure_bond_terminal_reserved);
    EXPECT(admitted.display.clears == 0);
    // Owner-context service may observe a later clock before it drains the
    // terminal event. The already-admitted 59,999 ms terminal stays valid and
    // serialized completion owns the one OLED clear.
    EXPECT(admitted.window.service(70000) == CompanionPairingWindowError::none);
    EXPECT(admitted.window.finish_attempt(
               59999, candidate,
               CompanionPairingAttemptTerminal::secure_bond_complete) ==
           CompanionPairingWindowError::none);
    EXPECT(admitted.window.status().phase ==
           CompanionPairingWindowPhase::closed);
    EXPECT(admitted.display.clears == 1);

    Fixture expired;
    expired.word(26);
    EXPECT(expired.open() == CompanionPairingWindowError::none);
    EXPECT(expired.window.handle_passkey_action_deferred_cleanup(
               1, candidate, port) == CompanionPairingWindowError::none);
    EXPECT(expired.window.reserve_secure_bond_terminal(
               60000, candidate) ==
           CompanionPairingWindowError::window_expired);
    EXPECT(expired.window.status().phase ==
           CompanionPairingWindowPhase::candidate_cleanup_required);
    EXPECT(expired.window.status().passkey_displayed);
    EXPECT(expired.display.clears == 0);
    EXPECT(expired.window.finish_attempt(
               60000, candidate,
               CompanionPairingAttemptTerminal::secure_bond_complete) ==
           CompanionPairingWindowError::candidate_cleanup_required);
    EXPECT(expired.display.clears == 0);
}

void restart_clock_and_fault_closure() {
    Fixture restarted;
    restarted.word(12);
    EXPECT(restarted.open(100) == CompanionPairingWindowError::none);
    EXPECT(restarted.window.restart() == CompanionPairingWindowError::none);
    restarted.word(13);
    EXPECT(restarted.open(0) == CompanionPairingWindowError::none);

    Fixture rollback;
    rollback.word(14);
    EXPECT(rollback.open(100) == CompanionPairingWindowError::none);
    EXPECT(rollback.window.service(99) ==
           CompanionPairingWindowError::clock_rollback);
    EXPECT(rollback.window.status().phase == CompanionPairingWindowPhase::faulted);

    Fixture overflow;
    overflow.word(15);
    EXPECT(overflow.open(std::numeric_limits<std::uint64_t>::max() - 59999) ==
           CompanionPairingWindowError::deadline_overflow);
    EXPECT(overflow.window.status().phase == CompanionPairingWindowPhase::faulted);

    Fixture failed;
    failed.word(16);
    EXPECT(failed.open() == CompanionPairingWindowError::none);
    EXPECT(failed.window.fault() == CompanionPairingWindowError::faulted);
    EXPECT(!failed.window.status().passkey_displayed);
}

void display_failures_contain() {
    Fixture show;
    show.word(17);
    show.display.show_ok = false;
    EXPECT(show.open() == CompanionPairingWindowError::display_failed);
    EXPECT(show.window.status().phase == CompanionPairingWindowPhase::faulted);
    PasskeyPort port;
    EXPECT(show.window.handle_passkey_action(1, candidate, port) ==
           CompanionPairingWindowError::faulted);
    EXPECT(port.calls == 0);

    Fixture clear;
    clear.word(18);
    EXPECT(clear.open() == CompanionPairingWindowError::none);
    clear.display.clear_ok = false;
    EXPECT(clear.window.service(60000) ==
           CompanionPairingWindowError::display_clear_failed);
    EXPECT(clear.window.status().phase == CompanionPairingWindowPhase::faulted);
    EXPECT(!clear.window.status().passkey_displayed);
}

void reenter(void* context) {
    auto* window = static_cast<CompanionPairingWindow*>(context);
    EXPECT(window->open_window(1, 2, 3000, true) ==
           CompanionPairingWindowError::reentrant_call);
}
void reentrant_open_is_denied() {
    Fixture value;
    value.word(19);
    value.display.context = &value.window;
    value.display.on_show = reenter;
    EXPECT(value.open() == CompanionPairingWindowError::none);
    EXPECT(value.display.shows == 1);
    EXPECT(value.window.status().phase == CompanionPairingWindowPhase::open);
}
}  // namespace

int main() {
    exact_gesture_and_zero_padding();
    unowned_boot_opens_one_fresh_sixty_second_window();
    rejected_gestures_and_event_replay();
    unbiased_sampling_and_entropy_failure();
    exact_deadline();
    one_candidate_one_attempt();
    injection_failure_consumes_window();
    callback_safe_path_defers_all_display_cleanup();
    terminal_and_disconnect_binding();
    bonded_candidate_interruptions_require_external_cleanup();
    candidate_cleanup_completion_requires_exact_bound_candidate();
    pairing_failure_before_bond_closes_without_cleanup();
    exact_deadline_precedes_terminal_or_candidate_validation();
    secure_bond_terminal_reservation_enforces_exact_deadline();
    restart_clock_and_fault_closure();
    display_failures_contain();
    reentrant_open_is_denied();
    if (failures != 0) {
        std::cerr << failures << " pairing-window assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 17 companion pairing-window groups\n";
    return EXIT_SUCCESS;
}
