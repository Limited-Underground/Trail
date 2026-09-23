#pragma once
// OT-0238b candidate owner; hardware port/provisioning integration is not yet wired.
// Serialized owner polls a trusted device port. No command/caller confirm Boolean.
#include "opentrail/enrollment_identity_binding.hpp"

namespace opentrail::security_evaluation {
struct FingerprintReviewContext {
    InvitationToken boot{};
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
struct FingerprintReviewFrame {
    // Fixed candidate domain is displayed alongside all 64 hex digits; not an
    // accepted final fingerprint algorithm. Every line fits the 128px OLED.
    std::array<char,17> domain{'O','T','-','I','D','1',' ','E','D','2','5','5','1','9',0};
    std::array<std::array<char,17>,4> digits{};
    InvitationRole local_role{};
    bool peer_page{};
    std::uint64_t revision{};
};
class FingerprintReviewPort {
public:
    virtual ~FingerprintReviewPort() = default;
    virtual FingerprintReviewSample sample() = 0;
    // Must draw all lines/role/purpose and atomically own the display revision.
    // A real target adapter, not a packet handler, must implement this boundary.
    virtual bool show(const FingerprintReviewFrame&) = 0;
};
class EnrollmentFingerprintReview final {
public:
    EnrollmentFingerprintReview(FingerprintReviewPort& port, InvitationKey local,
                                InvitationRole role, std::uint64_t group)
        : port_(port), local_(local), role_(role), group_(group) {}
    EnrollmentFingerprintReview(const EnrollmentFingerprintReview&) = delete;
    EnrollmentFingerprintReview& operator=(const EnrollmentFingerprintReview&) = delete;
    bool begin(const InvitationKey& untrusted_peer) {
        if (busy_ || begun_ || spent_) return stop();
        Guard guard(*this);
        if (!invitation_detail::nonzero(local_) || !invitation_detail::nonzero(untrusted_peer) ||
            local_ == untrusted_peer || group_ == 0 ||
            (role_ != InvitationRole::initiator && role_ != InvitationRole::responder)) return stop();
        peer_ = untrusted_peer;
        const auto sample = port_.sample();
        if (spent_ || sample.context.generation == 0 || sample.context.request == 0 ||
            !invitation_detail::nonzero(sample.context.boot) ||
            sample.now_ms > std::numeric_limits<std::uint64_t>::max() - review_window_ms) return stop();
        context_ = sample.context; last_ = sample.now_ms; deadline_ = last_ + review_window_ms;
        begun_ = true;
        return render(false); // Own identity must be inspectable before peer review.
    }
    bool show_peer() { return page(true); }
    bool show_local() { return page(false); }
    bool poll() {
        if (busy_) return stop();
        Guard guard(*this);
        FingerprintReviewSample sample{};
        if (!observe(sample)) return stop();
        if (!peer_page_ || confirmed_) return true;
        if (!sample.button_down) {
            if (pressed_) {
                const auto held = sample.now_ms - pressed_at_;
                pressed_ = false;
                if (held >= hold_ms && held <= maximum_hold_ms) {
                    confirmed_ = true; confirmed_at_ = sample.now_ms;
                }
            }
            released_ = true;
        } else if (released_ && !pressed_) {
            pressed_ = true; pressed_at_ = sample.now_ms;
        } else if (pressed_ && sample.now_ms - pressed_at_ > maximum_hold_ms) return stop();
        return true;
    }
    // Consumes only this owner's local physical review. It does not prove the
    // required pre-invitation possession exchange or durable membership. The
    // product coordinator must supply those gates before creating an invitation.
    bool bind(const EnrollmentIdentityProof& proof, std::optional<VerifiedIdentityBinding>& output) {
        if (busy_) return stop();
        Guard guard(*this);
        FingerprintReviewSample sample{};
        if (!confirmed_ || !peer_page_ || !observe(sample)) return stop();
        spent_ = true; // Attempt consumed even on signature failure.
        const auto candidate = proof;
        const auto fields = independent_invitation_detail::decode(candidate.invitation);
        const bool a = role_ == InvitationRole::initiator;
        const auto issued = a ? fields.issued_a_ms : fields.issued_b_ms;
        const auto window = a ? fields.window_a_ms : fields.window_b_ms;
        if ((a ? fields.boot_a : fields.boot_b) != context_.boot || issued < confirmed_at_ ||
            issued > sample.now_ms || sample.now_ms - issued >= window) return false;
        const RetainedEnrollmentIdentities pins = a ? RetainedEnrollmentIdentities{local_,peer_}
                                                   : RetainedEnrollmentIdentities{peer_,local_};
        std::optional<VerifiedIdentityBinding> staged;
        if (!EnrollmentIdentityVerifier(pins,pins.initiator,group_).verify(candidate,staged)) return false;
        // Check context, display and deadline AGAIN after delegated signing work.
        const auto after = port_.sample();
        if (!(after.context == context_) || after.display_revision != revision_ ||
            after.now_ms < sample.now_ms || after.now_ms >= deadline_ || after.now_ms < issued ||
            after.now_ms - issued >= window || cancelled_ || !confirmed_) return false;
        output = staged;
        return true;
    }
    void cancel() { cancelled_ = true; (void)stop(); }
private:
    struct Guard { EnrollmentFingerprintReview& owner; explicit Guard(EnrollmentFingerprintReview& o):owner(o){o.busy_=true;} ~Guard(){owner.busy_=false;} };
    bool stop() { spent_=true; confirmed_=false; return false; }
    bool observe(FingerprintReviewSample& sample) {
        if (!begun_ || spent_ || cancelled_) return false;
        sample=port_.sample();
        if (spent_ || !(sample.context == context_) || sample.display_revision != revision_ ||
            sample.now_ms < last_ || sample.now_ms >= deadline_) return false;
        last_=sample.now_ms;
        return true;
    }
    bool page(bool peer) {
        if (busy_) return stop();
        Guard guard(*this); FingerprintReviewSample sample{};
        if (!observe(sample)) return stop();
        return render(peer);
    }
    bool render(bool peer) {
        if (revision_ == std::numeric_limits<std::uint64_t>::max()) return stop();
        ++revision_; peer_page_=peer; confirmed_=false; released_=false; pressed_=false;
        FingerprintReviewFrame frame{}; frame.revision=revision_; frame.peer_page=peer; frame.local_role=role_;
        constexpr char hex[]="0123456789ABCDEF";
        const auto& key=peer ? peer_ : local_;
        for (std::size_t i=0;i<key.size();++i) {
            frame.digits[i/8][2*(i%8)]=hex[key[i]>>4];
            frame.digits[i/8][2*(i%8)+1]=hex[key[i]&15];
        }
        if (!port_.show(frame) || spent_) return stop();
        FingerprintReviewSample sample{};
        return observe(sample) || stop();
    }
    static constexpr std::uint64_t review_window_ms=120000, hold_ms=1000, maximum_hold_ms=3000;
    FingerprintReviewPort& port_;
    const InvitationKey local_; const InvitationRole role_; const std::uint64_t group_;
    InvitationKey peer_{}; FingerprintReviewContext context_{};
    std::uint64_t deadline_{},last_{},revision_{},pressed_at_{},confirmed_at_{};
    bool begun_{},spent_{},cancelled_{},busy_{},peer_page_{},released_{},pressed_{},confirmed_{};
};
}
