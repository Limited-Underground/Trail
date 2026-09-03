#pragma once

#include <cstdint>

namespace opentrail::companion {

inline constexpr std::uint64_t kCompanionFactoryResetDebounceMs = 40;
inline constexpr std::uint64_t kCompanionFactoryResetHoldMs = 10'000;
inline constexpr std::uint64_t
    kCompanionFactoryResetConfirmationWindowMs = 10'000;

enum class CompanionFactoryResetGesturePhase : std::uint8_t {
    awaiting_initial_release = 0,
    idle,
    hold_in_progress,
    prompt_while_held,
    confirmation_ready,
    confirmation_press_in_progress,
    commit_requested,
};

// These are intent-only events. This component has no storage, BLE, display,
// reboot, or erase authority.
enum class CompanionFactoryResetGestureEvent : std::uint8_t {
    none = 0,
    prompt_requested,
    prompt_cancelled,
    commit_requested,
};

struct CompanionFactoryResetGestureStatus {
    CompanionFactoryResetGesturePhase phase{
        CompanionFactoryResetGesturePhase::awaiting_initial_release};
    bool prompt_visible{false};
};

// Allocation-free recognizer for the destructive physical-reset gesture.
// The target converts electrical polarity into raw_pressed and supplies a
// checked monotonic millisecond clock. Debouncing is performed here so every
// target observes identical threshold and confirmation semantics.
//
// After commit_requested is emitted, the recognizer is terminal unless the
// target proves that the durable executor made no commit and explicitly calls
// rearm_after_noncommit().
class CompanionFactoryResetGesture {
public:
    void reset(bool raw_pressed, std::uint64_t now_ms);

    [[nodiscard]] CompanionFactoryResetGestureEvent observe(
        bool raw_pressed,
        std::uint64_t now_ms);

    // Cancels any pre-commit sequence. A visible prompt produces exactly one
    // prompt_cancelled event. A committed request cannot be cancelled here.
    [[nodiscard]] CompanionFactoryResetGestureEvent cancel(
        bool raw_pressed,
        std::uint64_t now_ms);

    // Re-arms only after commit_requested when the durable executor returned a
    // known noncommit/invalid result and the target remains contained.
    [[nodiscard]] bool rearm_after_noncommit(bool raw_pressed,
                                             std::uint64_t now_ms);

    [[nodiscard]] CompanionFactoryResetGestureStatus status() const {
        return {phase_, prompt_visible_};
    }

private:
    [[nodiscard]] CompanionFactoryResetGestureEvent cancel_sequence();
    [[nodiscard]] CompanionFactoryResetGestureEvent apply_stable_transition(
        bool pressed,
        std::uint64_t transition_at_ms);

    CompanionFactoryResetGesturePhase phase_{
        CompanionFactoryResetGesturePhase::awaiting_initial_release};
    bool raw_pressed_{false};
    bool stable_pressed_{false};
    bool stable_known_{false};
    bool prompt_visible_{false};
    std::uint64_t raw_since_ms_{0};
    std::uint64_t press_started_ms_{0};
    std::uint64_t confirmation_started_ms_{0};
    std::uint64_t last_observed_ms_{0};
};

}  // namespace opentrail::companion
