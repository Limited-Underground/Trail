#pragma once
#include "heltec_startup_display.hpp"
#include "heltec_v4_factory_reset_input.hpp"
#include "opentrail/companion_factory_reset_gesture.hpp"
#include "opentrail/enrollment_review_io.hpp"

namespace opentrail::target::heltec_v4_bench {
// App-task only. Owns every BOOT sample and the existing reset recognizer.
// observe() never reads GPIO/time. Each service tick reads them once; a completed
// review draw explicitly services a fresh post-frame tick before publishing it.
class HeltecEnrollmentInputArbiter final : public security_evaluation::EnrollmentReviewDeviceIo {
public:
    explicit HeltecEnrollmentInputArbiter(StartupDisplayOwner& display) : display_(display) {}
    HeltecEnrollmentInputArbiter(const HeltecEnrollmentInputArbiter&) = delete;
    HeltecEnrollmentInputArbiter& operator=(const HeltecEnrollmentInputArbiter&) = delete;
    [[nodiscard]] bool initialize();
    [[nodiscard]] companion::CompanionFactoryResetGestureEvent poll();
    [[nodiscard]] companion::CompanionFactoryResetGestureEvent cancel(std::uint64_t generation);
    [[nodiscard]] bool rearm_after_noncommit(std::uint64_t generation);
    [[nodiscard]] std::uint64_t generation() const { return sample_valid_ ? generation_ : 0; }
    [[nodiscard]] companion::CompanionFactoryResetGestureStatus status() const { return gesture_.status(); }

    // A future trusted product owner must supply an already-admitted exact
    // context. This method has NO production caller and admits no peer/Boolean.
    [[nodiscard]] bool bind_admitted_context(const security_evaluation::FingerprintReviewContext&);
    bool observe(security_evaluation::EnrollmentDeviceObservation&) override;
    bool acquire(std::uint64_t& lease) override;
    bool render(std::uint64_t lease, const security_evaluation::FingerprintReviewFrame&,
                const security_evaluation::EnrollmentReviewLayout&) override;
    bool release(std::uint64_t lease) override;
private:
    using Event = companion::CompanionFactoryResetGestureEvent;
    using Phase = companion::CompanionFactoryResetGesturePhase;
    bool enter();
    bool finish(bool result);
    bool service_tick(bool initial=false);
    void handle_event(Event);
    void retire_review();
    void invalidate_sample();
    bool reset_pending() const;
    bool idle_for_review() const;
    bool token_current(std::uint64_t) const;
    StartupDisplayOwner& display_;
    HeltecV4FactoryResetInput input_{};
    companion::CompanionFactoryResetGesture gesture_{};
    security_evaluation::FingerprintReviewContext context_{},last_context_{};
    std::uint64_t now_ms_{},generation_{},consumed_generation_{},lease_{},revision_{},preempted_lease_{};
    Event pending_event_{Event::none};
    bool attempted_{},initialized_{},sample_valid_{},button_down_{},clock_seen_{};
    bool context_bound_{},busy_{},reentry_{},faulted_{},fresh_release_sample_{},preemption_verified_{};
};
} // namespace opentrail::target::heltec_v4_bench
