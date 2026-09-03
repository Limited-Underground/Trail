#include <cstdlib>
#include <iostream>

#include "opentrail/companion_factory_reset_gesture.hpp"

namespace {

using opentrail::companion::CompanionFactoryResetGesture;
using opentrail::companion::CompanionFactoryResetGestureEvent;
using opentrail::companion::CompanionFactoryResetGesturePhase;
using opentrail::companion::kCompanionFactoryResetConfirmationWindowMs;
using opentrail::companion::kCompanionFactoryResetDebounceMs;
using opentrail::companion::kCompanionFactoryResetHoldMs;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

CompanionFactoryResetGestureEvent settle(
    CompanionFactoryResetGesture& gesture,
    bool pressed,
    std::uint64_t transition_at_ms) {
    require(gesture.observe(pressed, transition_at_ms) ==
                CompanionFactoryResetGestureEvent::none,
            "raw transition emits no event before debounce");
    return gesture.observe(
        pressed, transition_at_ms + kCompanionFactoryResetDebounceMs);
}

void arm_released(CompanionFactoryResetGesture& gesture) {
    gesture.reset(false, 0);
    require(gesture.observe(false, kCompanionFactoryResetDebounceMs) ==
                CompanionFactoryResetGestureEvent::none,
            "stable boot release emits no event");
    require(gesture.status().phase == CompanionFactoryResetGesturePhase::idle,
            "stable boot release arms the gesture");
}

std::uint64_t show_prompt_and_release(
    CompanionFactoryResetGesture& gesture,
    std::uint64_t press_at_ms) {
    require(settle(gesture, true, press_at_ms) ==
                CompanionFactoryResetGestureEvent::none,
            "initial press settles without an event");
    require(gesture.observe(true, press_at_ms +
                                      kCompanionFactoryResetHoldMs) ==
                CompanionFactoryResetGestureEvent::prompt_requested,
            "ten-second hold requests the prompt");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::prompt_while_held &&
                gesture.status().prompt_visible,
            "prompt remains active while held");
    const auto release_at_ms =
        press_at_ms + kCompanionFactoryResetHoldMs + 100;
    require(settle(gesture, false, release_at_ms) ==
                CompanionFactoryResetGestureEvent::none,
            "release arms confirmation without duplicating the prompt");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::confirmation_ready,
            "release opens confirmation window");
    return release_at_ms;
}

void test_boot_held_requires_a_stable_release() {
    CompanionFactoryResetGesture gesture;
    gesture.reset(true, 0);
    require(gesture.observe(true, kCompanionFactoryResetDebounceMs) ==
                CompanionFactoryResetGestureEvent::none,
            "boot-held input remains inert");
    require(gesture.observe(true, 30'000) ==
                CompanionFactoryResetGestureEvent::none,
            "boot-held duration cannot request a prompt");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::awaiting_initial_release,
            "boot-held input remains disarmed");
    require(settle(gesture, false, 30'100) ==
                CompanionFactoryResetGestureEvent::none,
            "first stable release emits no event");
    require(gesture.status().phase == CompanionFactoryResetGesturePhase::idle,
            "first stable release arms future input");
}

void test_threshold_boundaries_and_prompt_deduplication() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    constexpr std::uint64_t press_at = 100;
    require(settle(gesture, true, press_at) ==
                CompanionFactoryResetGestureEvent::none,
            "short-hold press settles");
    require(settle(gesture, false,
                   press_at + kCompanionFactoryResetHoldMs - 1) ==
                CompanionFactoryResetGestureEvent::none,
            "release one millisecond early is ignored");
    require(gesture.status().phase == CompanionFactoryResetGesturePhase::idle &&
                !gesture.status().prompt_visible,
            "early release leaves no reset state");

    constexpr std::uint64_t exact_press_at = 20'200;
    require(settle(gesture, true, exact_press_at) ==
                CompanionFactoryResetGestureEvent::none,
            "exact-threshold press settles");
    require(gesture.observe(true,
                            exact_press_at + kCompanionFactoryResetHoldMs) ==
                CompanionFactoryResetGestureEvent::prompt_requested,
            "exact threshold requests prompt");
    require(gesture.observe(true,
                            exact_press_at + kCompanionFactoryResetHoldMs +
                                5'000) ==
                CompanionFactoryResetGestureEvent::none,
            "continued hold cannot duplicate prompt");
}

void test_sparse_poll_release_at_threshold_still_prompts() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    constexpr std::uint64_t press_at = 100;
    require(settle(gesture, true, press_at) ==
                CompanionFactoryResetGestureEvent::none,
            "sparse-poll press settles");
    require(settle(gesture, false,
                   press_at + kCompanionFactoryResetHoldMs) ==
                CompanionFactoryResetGestureEvent::prompt_requested,
            "threshold release both prompts and arms confirmation");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::confirmation_ready &&
                gesture.status().prompt_visible,
            "threshold release enters visible confirmation");
}

void test_one_short_press_requests_commit_once() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    const auto confirmation_at = show_prompt_and_release(gesture, 100);
    require(settle(gesture, true, confirmation_at + 100) ==
                CompanionFactoryResetGestureEvent::none,
            "confirmation press produces no early commit");
    require(settle(gesture, false, confirmation_at + 200) ==
                CompanionFactoryResetGestureEvent::commit_requested,
            "confirmation release requests commit");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::commit_requested &&
                !gesture.status().prompt_visible,
            "commit request is terminal and clears prompt state");
    require(gesture.observe(false, confirmation_at + 1'000) ==
                CompanionFactoryResetGestureEvent::none &&
                gesture.observe(true, confirmation_at + 2'000) ==
                    CompanionFactoryResetGestureEvent::none &&
                gesture.observe(false, confirmation_at + 3'000) ==
                    CompanionFactoryResetGestureEvent::none,
            "terminal state emits no duplicate commit request");
}

void test_confirmation_deadline_is_inclusive() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    const auto confirmation_at = show_prompt_and_release(gesture, 100);
    require(settle(gesture, true, confirmation_at + 9'000) ==
                CompanionFactoryResetGestureEvent::none,
            "timely confirmation press settles");
    const auto release_at =
        confirmation_at + kCompanionFactoryResetConfirmationWindowMs;
    require(gesture.observe(false, release_at) ==
                CompanionFactoryResetGestureEvent::none,
            "deadline release awaits debounce");
    require(gesture.observe(false,
                            release_at + kCompanionFactoryResetDebounceMs) ==
                CompanionFactoryResetGestureEvent::commit_requested,
            "release exactly at deadline requests commit");
}

void test_known_noncommit_can_rearm_exactly_once() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    const auto confirmation_at = show_prompt_and_release(gesture, 100);
    require(settle(gesture, true, confirmation_at + 100) ==
                CompanionFactoryResetGestureEvent::none,
            "rearm case confirmation press");
    require(settle(gesture, false, confirmation_at + 200) ==
                CompanionFactoryResetGestureEvent::commit_requested,
            "rearm case commit request");
    require(gesture.rearm_after_noncommit(false, confirmation_at + 300),
            "known noncommit rearms terminal request");
    require(!gesture.rearm_after_noncommit(false, confirmation_at + 301),
            "rearm cannot repeat outside terminal request");
    require(gesture.observe(false, confirmation_at + 340) ==
                CompanionFactoryResetGestureEvent::none &&
                gesture.status().phase == CompanionFactoryResetGesturePhase::idle,
            "rearm requires and accepts stable release");
}

void test_timeout_and_late_confirmation_cancel_once() {
    CompanionFactoryResetGesture timeout;
    arm_released(timeout);
    const auto confirmation_at = show_prompt_and_release(timeout, 100);
    require(timeout.observe(
                false,
                confirmation_at + kCompanionFactoryResetConfirmationWindowMs) ==
                CompanionFactoryResetGestureEvent::none,
            "exact deadline remains open");
    require(timeout.observe(
                false,
                confirmation_at + kCompanionFactoryResetConfirmationWindowMs +
                    1) ==
                CompanionFactoryResetGestureEvent::prompt_cancelled,
            "first millisecond after deadline cancels prompt");
    require(timeout.observe(
                false,
                confirmation_at + kCompanionFactoryResetConfirmationWindowMs +
                    2) == CompanionFactoryResetGestureEvent::none,
            "timeout cancellation is not repeated");

    CompanionFactoryResetGesture late;
    arm_released(late);
    const auto late_confirmation_at = show_prompt_and_release(late, 100);
    require(settle(late, true, late_confirmation_at + 9'000) ==
                CompanionFactoryResetGestureEvent::none,
            "late-case confirmation press settles");
    require(late.observe(
                false,
                late_confirmation_at +
                    kCompanionFactoryResetConfirmationWindowMs + 1) ==
                CompanionFactoryResetGestureEvent::prompt_cancelled,
            "release after deadline cancels without commit");
    require(late.status().phase ==
                CompanionFactoryResetGesturePhase::awaiting_initial_release,
            "late held input must release before rearming");
}

void test_bounce_and_repeated_samples_are_inert() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    require(gesture.observe(true, 100) ==
                CompanionFactoryResetGestureEvent::none &&
                gesture.observe(false, 120) ==
                    CompanionFactoryResetGestureEvent::none &&
                gesture.observe(true, 140) ==
                    CompanionFactoryResetGestureEvent::none &&
                gesture.observe(true, 179) ==
                    CompanionFactoryResetGestureEvent::none,
            "sub-debounce bounce emits nothing");
    require(gesture.observe(true, 180) ==
                CompanionFactoryResetGestureEvent::none,
            "post-bounce press settles once");
    require(gesture.observe(true, 180) ==
                CompanionFactoryResetGestureEvent::none,
            "duplicate timestamp and level remain inert");
    require(gesture.observe(true, 140 + kCompanionFactoryResetHoldMs) ==
                CompanionFactoryResetGestureEvent::prompt_requested,
            "hold begins only at final raw transition");
    require(gesture.observe(true, 140 + kCompanionFactoryResetHoldMs) ==
                CompanionFactoryResetGestureEvent::none,
            "duplicate threshold sample cannot duplicate prompt");
}

void test_clock_regression_fails_closed() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    require(settle(gesture, true, 100) ==
                CompanionFactoryResetGestureEvent::none,
            "clock-regression hold settles");
    require(gesture.observe(true, 10'100) ==
                CompanionFactoryResetGestureEvent::prompt_requested,
            "clock-regression setup shows prompt");
    require(gesture.observe(true, 10'099) ==
                CompanionFactoryResetGestureEvent::prompt_cancelled,
            "clock regression cancels visible prompt");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::awaiting_initial_release,
            "clock regression requires a new stable release");
    require(gesture.observe(false, 10'100) ==
                CompanionFactoryResetGestureEvent::none &&
                gesture.observe(false,
                                10'100 + kCompanionFactoryResetDebounceMs) ==
                    CompanionFactoryResetGestureEvent::none,
            "post-regression release only rearms");
    require(gesture.status().phase == CompanionFactoryResetGesturePhase::idle,
            "post-regression release returns to idle");
}

void test_explicit_cancel_never_requests_commit() {
    CompanionFactoryResetGesture gesture;
    arm_released(gesture);
    const auto confirmation_at = show_prompt_and_release(gesture, 100);
    require(gesture.cancel(false, confirmation_at + 100) ==
                CompanionFactoryResetGestureEvent::prompt_cancelled,
            "explicit cancellation clears visible prompt");
    require(gesture.cancel(false, confirmation_at + 200) ==
                CompanionFactoryResetGestureEvent::none,
            "duplicate cancellation is inert");
    require(gesture.status().phase ==
                CompanionFactoryResetGesturePhase::awaiting_initial_release,
            "explicit cancellation requires stable rearm");
}

}  // namespace

int main() {
    test_boot_held_requires_a_stable_release();
    test_threshold_boundaries_and_prompt_deduplication();
    test_sparse_poll_release_at_threshold_still_prompts();
    test_one_short_press_requests_commit_once();
    test_known_noncommit_can_rearm_exactly_once();
    test_confirmation_deadline_is_inclusive();
    test_timeout_and_late_confirmation_cancel_once();
    test_bounce_and_repeated_samples_are_inert();
    test_clock_regression_fails_closed();
    test_explicit_cancel_never_requests_commit();
    std::cout << "10 companion factory-reset gesture groups passed.\n";
    return 0;
}
