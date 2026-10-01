#pragma once
// Host composition of the candidate session with the real serialized input and
// leased display owners. No production caller or request authority is supplied.
#include <type_traits>
#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/enrollment_review_device_port.hpp"

namespace opentrail::security_evaluation {
// DeviceIo is the trusted application owner, not a peer/packet adapter. It must
// implement EnrollmentReviewDeviceIo and exact bind_admitted_context /
// release_admitted_context, plus non-consuming service_review_tick. Device and
// fresh request authority must use the same local monotonic source/domain. A
// refused bind must leave no newly owned context. The same app task must drain
// queued poll() events through its existing reset dispatcher before the next
// enrollment operation. Sampling never consumes that event. Dependencies outlive
// this adapter and its one session.
// Reconstruct a fresh adapter after session close for rekey/recovery. The inner
// review port remains one-shot and is never silently recycled after release.
template<class DeviceIo>
class EnrollmentSessionReviewDevicePort final : public EnrollmentSessionDevicePort {
    static_assert(std::is_base_of_v<EnrollmentReviewDeviceIo,DeviceIo>);
public:
    explicit EnrollmentSessionReviewDevicePort(DeviceIo& io):io_(io),review_(io) {}
    EnrollmentSessionReviewDevicePort(const EnrollmentSessionReviewDevicePort&)=delete;
    EnrollmentSessionReviewDevicePort& operator=(const EnrollmentSessionReviewDevicePort&)=delete;
    ~EnrollmentSessionReviewDevicePort() override {
        busy_=true;failed_=true;cleanup();busy_=false;
    }

    bool bind_context(const FingerprintReviewContext& input) override {
        const auto context=input;
        if (!enter()) return false;
        bool ok=!attempted_ && !failed_ && context.generation && context.request &&
            invitation_detail::nonzero(context.boot);
        if (ok) {
            attempted_=true;context_=context;
            admitted_=io_.bind_admitted_context(context_);
            ok=admitted_ && !failed_ && review_.acquire() && !failed_;
            if (ok) {
                FingerprintReviewSample sample{};
                ok=read(sample) && sample.display_revision==0;
            }
            bound_=ok;
        }
        return finish(ok);
    }

    FingerprintReviewSample sample() override {
        if (!enter()) return {};
        FingerprintReviewSample output{};
        bool ok=bound_ && !failed_;
        if (ok) ok=read(output);
        return finish(ok) ? output : FingerprintReviewSample{};
    }

    bool show(const FingerprintReviewFrame& input) override {
        const auto frame=input;
        if (!enter()) return false;
        return finish(bound_ && !failed_ && io_.service_review_tick() && !failed_ &&
            review_.show(frame) && !failed_);
    }

    bool release_context(const FingerprintReviewContext& input) override {
        const auto context=input;
        if (!enter()) return false;
        // An old or unrelated context cannot tear down this adapter's owner.
        if (!attempted_ || !(context==context_)) {busy_=false;return false;}
        failed_=true;cleanup();
        const bool ok=cleanup_ok_ && !reentered_;
        busy_=false;return ok;
    }

private:
    bool read(FingerprintReviewSample& output) {
        if (!io_.service_review_tick() || failed_) return false;
        output=review_.sample();
        return !failed_ && output.context==context_;
    }
    bool enter() {
        if (busy_) {failed_=true;reentered_=true;return false;}
        busy_=true;return true;
    }
    bool finish(bool ok) {
        if (!ok) failed_=true;
        if (failed_) cleanup();
        busy_=false;return ok && !failed_;
    }
    void cleanup() {
        if (cleanup_attempted_) return;
        cleanup_attempted_=true;bound_=false;
        // Keep the adapter busy through delegated conceal/restore and context
        // release; reentry is refused and cannot recursively draw or clean up.
        review_.cancel();
        const bool released=!admitted_ || io_.release_admitted_context(context_);
        cleanup_ok_=review_.cleanup_verified() && released && !reentered_;
    }
    DeviceIo& io_;EnrollmentReviewDevicePort review_;
    FingerprintReviewContext context_{};
    bool attempted_{},admitted_{},bound_{},failed_{},busy_{},reentered_{};
    bool cleanup_attempted_{},cleanup_ok_{};
};
} // namespace opentrail::security_evaluation
