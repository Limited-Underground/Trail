#include "heltec_enrollment_input_arbiter.hpp"
#include <limits>
#include "esp_timer.h"

namespace opentrail::target::heltec_v4_bench {
namespace se = security_evaluation;
bool HeltecEnrollmentInputArbiter::enter() {
    if (busy_) { reentry_=true; return false; }
    if (faulted_) return false;
    busy_=true;
    return true;
}
bool HeltecEnrollmentInputArbiter::finish(bool result) {
    if (reentry_) {
        faulted_=true; sample_valid_=false;
        retire_review(); // One deferred cleanup; reentry never recursively draws.
        result=false;
    }
    busy_=false;
    return result && !faulted_;
}
bool HeltecEnrollmentInputArbiter::reset_pending() const {
    return gesture_.status().prompt_visible || gesture_.status().phase==Phase::commit_requested;
}
bool HeltecEnrollmentInputArbiter::idle_for_review() const {
    return initialized_ && sample_valid_ && !fresh_release_sample_ &&
        gesture_.status().phase==Phase::idle && !reset_pending();
}
bool HeltecEnrollmentInputArbiter::token_current(std::uint64_t token) const {
    return initialized_ && sample_valid_ && token && token==generation_ && token!=consumed_generation_;
}
void HeltecEnrollmentInputArbiter::retire_review() {
    const auto old=lease_;
    lease_=revision_=0; context_bound_=false; context_={};
    if (old) (void)display_.release_enrollment_review(old);
}
void HeltecEnrollmentInputArbiter::invalidate_sample() {
    sample_valid_=false;
    retire_review();
    // No unknown/stale sample can advance a gesture. A new valid tick must
    // establish a fresh release; a terminal commit cannot be cancelled here.
    const auto event=gesture_.cancel(true,now_ms_);
    fresh_release_sample_=true;
    handle_event(event);
}
void HeltecEnrollmentInputArbiter::handle_event(Event event) {
    if (event==Event::none) return;
    if (event==Event::prompt_requested || event==Event::commit_requested) {
        if (lease_) {preempted_lease_=lease_;preemption_verified_=false;}
        lease_=revision_=0; context_bound_=false; context_={};
        // Invalidate the real display lease before exposing any cached sample.
        // This is idempotent when the application consumes the same event.
        const bool reset_drawn=display_.show_factory_reset_confirmation();
        preemption_verified_=reset_drawn && display_.status().available;
        if (!reset_drawn && event==Event::prompt_requested) {
            event=gesture_.cancel(button_down_,now_ms_);
            fresh_release_sample_=true;
        }
    } else if (event==Event::prompt_cancelled) {
        (void)display_.clear_factory_reset_confirmation();
    }
    pending_event_=event; // A post-render event survives until application poll.
}
bool HeltecEnrollmentInputArbiter::service_tick(bool initial) {
    const auto raw_us=esp_timer_get_time();
    bool pressed=true;
    const bool sampled=input_.sample(pressed);
    if (!sampled || raw_us<0 ||
        (clock_seen_ && static_cast<std::uint64_t>(raw_us/1000)<now_ms_) ||
        generation_==std::numeric_limits<std::uint64_t>::max()) {
        if (generation_==std::numeric_limits<std::uint64_t>::max()) faulted_=true;
        invalidate_sample();
        return false;
    }
    now_ms_=static_cast<std::uint64_t>(raw_us/1000);
    clock_seen_=true; button_down_=pressed; ++generation_; sample_valid_=true;
    if (initial) gesture_.reset(button_down_,now_ms_);
    else if (fresh_release_sample_) {
        // Begin release debounce from THIS post-handoff sample, never from a
        // release observed before cancellation or a slow restore finished.
        handle_event(gesture_.cancel(button_down_,now_ms_));
        fresh_release_sample_=false;
    } else handle_event(gesture_.observe(button_down_,now_ms_));
    return sample_valid_ && !faulted_ && !reentry_;
}
bool HeltecEnrollmentInputArbiter::initialize() {
    if (!enter()) return false;
    if (attempted_) return finish(initialized_ && sample_valid_);
    attempted_=true;
    initialized_=input_.initialize();
    const bool ok=initialized_ && service_tick(true);
    return finish(ok);
}
HeltecEnrollmentInputArbiter::Event HeltecEnrollmentInputArbiter::poll() {
    if (!enter()) return Event::none;
    if (initialized_) (void)service_tick();
    const auto event=pending_event_;pending_event_=Event::none;
    return finish(initialized_) ? event : Event::none;
}
HeltecEnrollmentInputArbiter::Event HeltecEnrollmentInputArbiter::cancel(std::uint64_t token) {
    if (!enter()) return Event::none;
    Event event=Event::none;
    if (token_current(token) && gesture_.status().phase!=Phase::commit_requested) {
        consumed_generation_=token;
        retire_review();
        event=gesture_.cancel(button_down_,now_ms_);
        fresh_release_sample_=true;
        handle_event(event);
        pending_event_=Event::none; // This caller consumes the cancellation now.
    }
    return finish(true) ? event : Event::none;
}
bool HeltecEnrollmentInputArbiter::rearm_after_noncommit(std::uint64_t token) {
    if (!enter()) return false;
    bool ok=token_current(token) && gesture_.rearm_after_noncommit(button_down_,now_ms_);
    if (ok) {
        consumed_generation_=token;fresh_release_sample_=true;
        retire_review();preempted_lease_=0;pending_event_=Event::none;
    }
    return finish(ok);
}
bool HeltecEnrollmentInputArbiter::bind_admitted_context(const se::FingerprintReviewContext& context) {
    if (!enter()) return false;
    bool nonzero=false;for(auto byte:context.boot) nonzero=nonzero || byte!=0;
    const bool ok=idle_for_review() && !lease_ && !context_bound_ &&
        nonzero && context.generation && context.request && !(context==last_context_);
    if (ok) { context_=last_context_=context;context_bound_=true;preempted_lease_=0; }
    return finish(ok);
}
bool HeltecEnrollmentInputArbiter::observe(se::EnrollmentDeviceObservation& output) {
    if (!enter()) return false;
    const auto shown=display_.enrollment_review_status();
    const bool ok=initialized_ && sample_valid_ &&
        shown.lease==lease_ && shown.revision==revision_;
    if (ok) output={context_,now_ms_,lease_,revision_,button_down_,reset_pending()};
    return finish(ok);
}
bool HeltecEnrollmentInputArbiter::acquire(std::uint64_t& lease) {
    lease=0;
    if (!enter()) return false;
    const bool ok=idle_for_review() && context_bound_ && !lease_ &&
        display_.acquire_enrollment_review(lease_);
    if (ok) {lease=lease_;revision_=0;}
    return finish(ok);
}
bool HeltecEnrollmentInputArbiter::render(std::uint64_t lease,
    const se::FingerprintReviewFrame& frame,const se::EnrollmentReviewLayout& layout) {
    if (!enter()) return false;
    const auto context=context_;
    bool ok=sample_valid_ && context_bound_ && lease && lease==lease_ && !reset_pending() &&
        display_.render_enrollment_review(lease,frame.revision,layout);
    if (ok && !reentry_) {
        // A synchronous draw can take time and the button can change during it.
        // Observe AFTER visibility, process reset first, then expose its revision.
        ok=service_tick() && context_bound_ && context_==context && lease_==lease && !reset_pending();
        const auto shown=display_.enrollment_review_status();
        ok=ok && shown.lease==lease && shown.revision==frame.revision;
        if (ok) revision_=frame.revision;
    }
    if (!ok && lease_==lease) {retire_review();fresh_release_sample_=true;}
    return finish(ok);
}
bool HeltecEnrollmentInputArbiter::release(std::uint64_t lease) {
    if (!enter()) return false;
    bool ok=false;
    if (lease && lease==preempted_lease_ && !lease_ && preemption_verified_ && display_.status().available &&
        display_.enrollment_review_status().lease!=lease) {
        // Exact reset-preempted cleanup is acknowledged without repainting or
        // cancelling reset. No generic stale lease receives this acknowledgement.
        preempted_lease_=0;ok=true;
    } else if (sample_valid_ && lease && lease==lease_) {
        lease_=revision_=0;context_bound_=false;context_={};
        ok=display_.release_enrollment_review(lease);
        (void)gesture_.cancel(button_down_,now_ms_);
        fresh_release_sample_=true;
    }
    return finish(ok);
}
} // namespace opentrail::target::heltec_v4_bench
