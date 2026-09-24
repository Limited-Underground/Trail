#pragma once
// Exact review IO data only. These types do not admit a peer or approve a review.
#include <array>
#include <cstdint>
#include "opentrail/enrollment_review_display.hpp"

namespace opentrail::security_evaluation {
enum class InvitationRole : std::uint8_t;
struct FingerprintReviewContext {
    std::array<std::uint8_t,16> boot{};
    std::uint64_t generation{}, request{};
    bool operator==(const FingerprintReviewContext& b) const {
        return boot == b.boot && generation == b.generation && request == b.request;
    }
};
struct FingerprintReviewSample {
    FingerprintReviewContext context{};
    std::uint64_t now_ms{}, display_revision{};
    bool button_down{};
};
enum class EnrollmentDisplayPurpose { identity_review, transcript_confirmation };
struct FingerprintReviewFrame {
    EnrollmentDisplayPurpose purpose{EnrollmentDisplayPurpose::identity_review};
    // Fixed candidate domain is displayed alongside all 64 hex digits; not an
    // accepted final fingerprint algorithm. Every line fits the 128px OLED.
    std::array<char,17> domain{'O','T','-','I','D','1',' ','E','D','2','5','5','1','9',0};
    std::array<std::array<char,17>,4> digits{};
    InvitationRole local_role{};
    bool peer_page{};
    std::uint64_t revision{};
    std::uint64_t group{};
};
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

} // namespace opentrail::security_evaluation
