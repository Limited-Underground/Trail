#include "opentrail/companion_factory_reset_gesture.hpp"

namespace opentrail::companion {

void CompanionFactoryResetGesture::reset(bool raw_pressed,
                                          std::uint64_t now_ms) {
    phase_ = CompanionFactoryResetGesturePhase::awaiting_initial_release;
    raw_pressed_ = raw_pressed;
    stable_pressed_ = false;
    stable_known_ = false;
    prompt_visible_ = false;
    raw_since_ms_ = now_ms;
    press_started_ms_ = 0;
    confirmation_started_ms_ = 0;
    last_observed_ms_ = now_ms;
}

CompanionFactoryResetGestureEvent
CompanionFactoryResetGesture::cancel_sequence() {
    const bool prompt_was_visible = prompt_visible_;
    prompt_visible_ = false;
    press_started_ms_ = 0;
    confirmation_started_ms_ = 0;
    phase_ = raw_pressed_ || (stable_known_ && stable_pressed_)
                 ? CompanionFactoryResetGesturePhase::awaiting_initial_release
                 : CompanionFactoryResetGesturePhase::idle;
    return prompt_was_visible
               ? CompanionFactoryResetGestureEvent::prompt_cancelled
               : CompanionFactoryResetGestureEvent::none;
}

CompanionFactoryResetGestureEvent
CompanionFactoryResetGesture::apply_stable_transition(
    bool pressed,
    std::uint64_t transition_at_ms) {
    if (pressed) {
        if (phase_ == CompanionFactoryResetGesturePhase::idle) {
            phase_ = CompanionFactoryResetGesturePhase::hold_in_progress;
            press_started_ms_ = transition_at_ms;
        } else if (phase_ ==
                   CompanionFactoryResetGesturePhase::confirmation_ready) {
            if (transition_at_ms - confirmation_started_ms_ <=
                kCompanionFactoryResetConfirmationWindowMs) {
                phase_ = CompanionFactoryResetGesturePhase::
                    confirmation_press_in_progress;
            } else {
                return cancel_sequence();
            }
        }
        return CompanionFactoryResetGestureEvent::none;
    }

    switch (phase_) {
        case CompanionFactoryResetGesturePhase::awaiting_initial_release:
            phase_ = CompanionFactoryResetGesturePhase::idle;
            return CompanionFactoryResetGestureEvent::none;
        case CompanionFactoryResetGesturePhase::hold_in_progress: {
            const auto held_ms = transition_at_ms - press_started_ms_;
            press_started_ms_ = 0;
            if (held_ms < kCompanionFactoryResetHoldMs) {
                phase_ = CompanionFactoryResetGesturePhase::idle;
                return CompanionFactoryResetGestureEvent::none;
            }
            prompt_visible_ = true;
            confirmation_started_ms_ = transition_at_ms;
            phase_ = CompanionFactoryResetGesturePhase::confirmation_ready;
            return CompanionFactoryResetGestureEvent::prompt_requested;
        }
        case CompanionFactoryResetGesturePhase::prompt_while_held:
            confirmation_started_ms_ = transition_at_ms;
            phase_ = CompanionFactoryResetGesturePhase::confirmation_ready;
            return CompanionFactoryResetGestureEvent::none;
        case CompanionFactoryResetGesturePhase::
            confirmation_press_in_progress:
            if (transition_at_ms - confirmation_started_ms_ <=
                kCompanionFactoryResetConfirmationWindowMs) {
                prompt_visible_ = false;
                phase_ =
                    CompanionFactoryResetGesturePhase::commit_requested;
                return CompanionFactoryResetGestureEvent::commit_requested;
            }
            return cancel_sequence();
        case CompanionFactoryResetGesturePhase::idle:
        case CompanionFactoryResetGesturePhase::confirmation_ready:
        case CompanionFactoryResetGesturePhase::commit_requested:
            return CompanionFactoryResetGestureEvent::none;
    }
    return CompanionFactoryResetGestureEvent::none;
}

CompanionFactoryResetGestureEvent CompanionFactoryResetGesture::observe(
    bool raw_pressed,
    std::uint64_t now_ms) {
    if (phase_ == CompanionFactoryResetGesturePhase::commit_requested) {
        return CompanionFactoryResetGestureEvent::none;
    }

    if (now_ms < last_observed_ms_) {
        const bool prompt_was_visible = prompt_visible_;
        reset(raw_pressed, now_ms);
        return prompt_was_visible
                   ? CompanionFactoryResetGestureEvent::prompt_cancelled
                   : CompanionFactoryResetGestureEvent::none;
    }
    last_observed_ms_ = now_ms;

    if (raw_pressed != raw_pressed_) {
        raw_pressed_ = raw_pressed;
        raw_since_ms_ = now_ms;
    }

    if ((!stable_known_ || stable_pressed_ != raw_pressed_) &&
        now_ms - raw_since_ms_ >= kCompanionFactoryResetDebounceMs) {
        stable_known_ = true;
        stable_pressed_ = raw_pressed_;
        const auto transition_event =
            apply_stable_transition(stable_pressed_, raw_since_ms_);
        if (transition_event != CompanionFactoryResetGestureEvent::none) {
            return transition_event;
        }
    }

    if (phase_ == CompanionFactoryResetGesturePhase::hold_in_progress &&
        raw_pressed_ && stable_pressed_ &&
        now_ms - press_started_ms_ >= kCompanionFactoryResetHoldMs) {
        prompt_visible_ = true;
        phase_ = CompanionFactoryResetGesturePhase::prompt_while_held;
        return CompanionFactoryResetGestureEvent::prompt_requested;
    }

    if ((phase_ == CompanionFactoryResetGesturePhase::confirmation_ready ||
         phase_ == CompanionFactoryResetGesturePhase::
                       confirmation_press_in_progress) &&
        now_ms - confirmation_started_ms_ >
            kCompanionFactoryResetConfirmationWindowMs) {
        // A raw release at or before the deadline is allowed to finish its
        // debounce interval before the exact transition time is judged.
        const bool timely_release_pending =
            phase_ == CompanionFactoryResetGesturePhase::
                          confirmation_press_in_progress &&
            stable_pressed_ && !raw_pressed_ &&
            raw_since_ms_ - confirmation_started_ms_ <=
                kCompanionFactoryResetConfirmationWindowMs;
        if (!timely_release_pending) {
            return cancel_sequence();
        }
    }

    return CompanionFactoryResetGestureEvent::none;
}

CompanionFactoryResetGestureEvent CompanionFactoryResetGesture::cancel(
    bool raw_pressed,
    std::uint64_t now_ms) {
    if (phase_ == CompanionFactoryResetGesturePhase::commit_requested) {
        return CompanionFactoryResetGestureEvent::none;
    }
    const bool prompt_was_visible = prompt_visible_;
    reset(raw_pressed, now_ms);
    return prompt_was_visible
               ? CompanionFactoryResetGestureEvent::prompt_cancelled
               : CompanionFactoryResetGestureEvent::none;
}

bool CompanionFactoryResetGesture::rearm_after_noncommit(
    bool raw_pressed,
    std::uint64_t now_ms) {
    if (phase_ != CompanionFactoryResetGesturePhase::commit_requested) {
        return false;
    }
    reset(raw_pressed, now_ms);
    return true;
}

}  // namespace opentrail::companion
