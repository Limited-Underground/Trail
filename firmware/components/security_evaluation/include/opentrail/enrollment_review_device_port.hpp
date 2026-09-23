#pragma once
// Host candidate adapter, not a deployed GPIO/display driver or trust root.
#include "opentrail/enrollment_review_layout.hpp"

namespace opentrail::security_evaluation {
struct EnrollmentDeviceObservation {
    FingerprintReviewContext context{};
    std::uint64_t now_ms{}, lease{}, display_revision{};
    bool button_down{}, reset_pending{};
};

// Implement only in the trusted, serialized application owner. One arbiter owns
// ALL GPIO sampling and display writers. A reset preempts enrollment, invalidates
// its lease/revision, and cannot reuse its held gesture. Packet handlers must not
// implement this interface. Acquiring a lease performs no enrollment approval.
class EnrollmentReviewDeviceIo {
public:
    virtual ~EnrollmentReviewDeviceIo() = default;
    virtual bool observe(EnrollmentDeviceObservation&) = 0;
    // Must return a fresh nonzero exclusive lease, with display revision zero.
    virtual bool acquire(std::uint64_t& lease) = 0;
    // Render every canonical layout row without clipping. Publish the supplied
    // revision only after the complete frame is visible under the same lease.
    virtual bool render(std::uint64_t lease, const FingerprintReviewFrame&,
                        const EnrollmentReviewLayout&) = 0;
    // Remove this overlay (or observe that reset already preempted it). Keep
    // reset input inhibited until a fresh stable release is observed. A failed
    // conceal/restore must not report successful release.
    virtual bool release(std::uint64_t lease) = 0;
};

class EnrollmentReviewDevicePort final : public FingerprintReviewPort {
public:
    explicit EnrollmentReviewDevicePort(EnrollmentReviewDeviceIo& io) : io_(io) {}
    EnrollmentReviewDevicePort(const EnrollmentReviewDevicePort&) = delete;
    EnrollmentReviewDevicePort& operator=(const EnrollmentReviewDevicePort&) = delete;
    ~EnrollmentReviewDevicePort() override { cancel(); }

    bool acquire() {
        if (busy_) return poison(true);
        busy_=true;
        bool ok=false;
        if (!attempted_ && !failed_) {
            attempted_=true;
            const bool acquired=io_.acquire(lease_);
            EnrollmentDeviceObservation o{};
            if (acquired && lease_ && !failed_ && io_.observe(o) && !failed_ &&
                o.lease==lease_ && !o.reset_pending && o.display_revision==0 &&
                valid_context(o.context)) {
                context_=o.context; last_=o.now_ms; active_=true;
                restart_button(o); ok=true;
            }
        }
        if (!ok) poison();
        finish(); return ok && !failed_;
    }

    FingerprintReviewSample sample() override {
        if (busy_) { poison(true); return {}; }
        busy_=true;
        EnrollmentDeviceObservation o{}; FingerprintReviewSample result{};
        if (read(o)) {
            debounce(o);
            result={context_,o.now_ms,revision_,stable_button_};
        } else poison();
        finish(); return failed_ ? FingerprintReviewSample{} : result;
    }

    bool show(const FingerprintReviewFrame& frame) override {
        if (busy_) return poison(true);
        busy_=true;
        EnrollmentDeviceObservation before{}, after{};
        EnrollmentReviewLayout layout{};
        bool ok=read(before) && revision_!=std::numeric_limits<std::uint64_t>::max() &&
            frame.revision==revision_+1 && enrollment_review_layout(frame,layout);
        if (ok) {
            ok=io_.render(lease_,frame,layout) && !failed_;
            if (ok) {
                revision_=frame.revision;
                ok=read(after);
                if (ok) restart_button(after);
            }
        }
        if (!ok) poison();
        finish(); return ok && !failed_;
    }

    void cancel() {
        poison(busy_);
        if (!busy_) { busy_=true; finish(); }
    }

    // Handoff evidence for the single input arbiter, never enrollment authority.
    // No target reset recognizer is connected by this host adapter.
    bool reset_handoff_ready() {
        if (busy_) return poison(true);
        if (!release_verified_ || handoff_failed_) return false;
        busy_=true;
        EnrollmentDeviceObservation o{};
        bool ready=false;
        if (!io_.observe(o) || handoff_failed_ || o.lease==lease_ || o.now_ms<last_) {
            handoff_failed_=true;
        } else {
            last_=o.now_ms;
            if (o.button_down) release_seen_=false;
            else if (!release_seen_) { release_seen_=true; release_since_=o.now_ms; }
            else ready=o.now_ms-release_since_>=debounce_ms;
        }
        busy_=false;
        return ready && !handoff_failed_;
    }

private:
    static constexpr std::uint64_t debounce_ms=20;
    static bool valid_context(const FingerprintReviewContext& c) {
        return c.generation && c.request && invitation_detail::nonzero(c.boot);
    }
    bool poison(bool reentry=false) {
        failed_=true; active_=false;
        if(reentry && release_attempted_) handoff_failed_=true;
        return false;
    }
    bool read(EnrollmentDeviceObservation& o) {
        if (!active_ || failed_ || !io_.observe(o) || failed_ ||
            !(o.context==context_) || o.lease!=lease_ || o.reset_pending ||
            o.display_revision!=revision_ || o.now_ms<last_) return false;
        last_=o.now_ms; return true;
    }
    void restart_button(const EnrollmentDeviceObservation& o) {
        raw_button_=o.button_down; raw_since_=o.now_ms;
        stable_button_=true; // Require a fresh stable release on every page.
    }
    void debounce(const EnrollmentDeviceObservation& o) {
        if(o.button_down!=raw_button_) {raw_button_=o.button_down;raw_since_=o.now_ms;}
        if(o.now_ms-raw_since_>=debounce_ms) stable_button_=raw_button_;
    }
    void finish() {
        if(failed_ && lease_ && !release_attempted_) {
            release_attempted_=true;
            EnrollmentDeviceObservation o{};
            if(io_.release(lease_) && !handoff_failed_ && io_.observe(o) &&
                !handoff_failed_ && o.lease!=lease_ && o.now_ms>=last_) {
                release_verified_=true; last_=o.now_ms;
                release_seen_=false; // A later stable release, not an old sample.
            } else handoff_failed_=true;
        }
        busy_=false;
    }
    EnrollmentReviewDeviceIo& io_;
    FingerprintReviewContext context_{};
    std::uint64_t lease_{},revision_{},last_{},raw_since_{},release_since_{};
    bool attempted_{},active_{},failed_{},busy_{},raw_button_{},stable_button_{true};
    bool release_attempted_{},release_verified_{},handoff_failed_{},release_seen_{};
};
} // namespace opentrail::security_evaluation
