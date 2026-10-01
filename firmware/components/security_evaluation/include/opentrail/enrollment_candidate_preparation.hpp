#pragma once
// Host evaluation composition only. This in-memory value has no BLE/radio wire
// encoding and selects no production profile, capability, or persistent root.
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/enrollment_identity_store.hpp"
#include "opentrail/selected_enrollment_request_owner.hpp"

namespace opentrail::security_evaluation {
struct EvaluationEnrollmentCandidate {
    static constexpr std::uint32_t current_version = 1;
    static constexpr std::uint32_t ed25519_evaluation_profile = 1;
    std::uint32_t version{current_version};
    std::uint32_t profile{ed25519_evaluation_profile};
    InvitationKey public_identity{};
};

// Actual identity-owner export, not a provision/sign/authorize operation. The
// receiving side still treats all these fields as untrusted public input.
inline bool export_evaluation_enrollment_candidate(
    EnrollmentIdentityStore& identity, EvaluationEnrollmentCandidate& output) {
    EvaluationEnrollmentCandidate staged{};
    if (!identity.public_key(staged.public_identity) ||
        !invitation_detail::nonzero(staged.public_identity)) return false;
    output = staged;
    return true;
}

struct EnrollmentCandidateLocalObservation {
    companion::DeviceNameAuthority authority{};
    std::uint16_t connection_handle{0xffff};
    companion::SelectedEnrollmentRequest intent_request{};
    InvitationRole local_role{};
    std::uint64_t group{};
    FingerprintReviewContext review_context{};
    std::uint64_t monotonic_domain{};
};

// Trusted serialized application owner only, never a packet handler. It samples
// live protected authorization plus locally chosen role/group for the EXACT
// admitted request, and the owned boot/session allocation. Clock-domain tokens
// are composition identity, not peer-provided data. The request clock and device
// review clock MUST be the same local monotonic source. No production adapter is
// supplied here; the host test simulates this hardware/application boundary.
// observe() is a read-only terminal observation: it must not allocate generations,
// write identity storage, replace requests, or invoke enrollment operations.
class EnrollmentCandidateLocalAuthority {
public:
    virtual ~EnrollmentCandidateLocalAuthority() = default;
    virtual bool observe(EnrollmentCandidateLocalObservation&) = 0;
};

// All dependencies and this owner must outlive downstream preparation. One
// serialized application owns them; callbacks are checked for reentry, not races.
class EnrollmentCandidatePreparation final {
    class GuardedPort final : public FingerprintReviewPort {
    public:
        explicit GuardedPort(EnrollmentCandidatePreparation& owner) : owner_(owner) {}
        FingerprintReviewSample sample() override { return owner_.sample(); }
        bool show(const FingerprintReviewFrame& frame) override { return owner_.show(frame); }
    private:
        EnrollmentCandidatePreparation& owner_;
    };
public:
    EnrollmentCandidatePreparation(companion::SelectedEnrollmentRequestOwner& request,
        EnrollmentCandidateLocalAuthority& authority, EnrollmentIdentityStore& identity,
        FingerprintReviewPort& device_port, std::uint64_t device_clock_domain)
        : request_owner_(request), authority_(authority), identity_(identity),
          device_port_(device_port), clock_domain_(device_clock_domain), guarded_port_(*this) {}
    EnrollmentCandidatePreparation(const EnrollmentCandidatePreparation&) = delete;
    EnrollmentCandidatePreparation& operator=(const EnrollmentCandidatePreparation&) = delete;
    ~EnrollmentCandidatePreparation() { cancel(); }

    bool begin(const EvaluationEnrollmentCandidate& peer) {
        const auto candidate = peer;
        return operation([&] {
            if (attempted_) return false;
            attempted_ = true;
            if (!request_owner_.pending()) return false;
            request_ = request_owner_.request();
            captured_ = true;
            EnrollmentCandidateLocalObservation first{};
            if (!clock_domain_ || !authority_.observe(first) || failed_ ||
                !same_request(first.intent_request, request_) ||
                first.monotonic_domain != clock_domain_ || !first.group ||
                (first.local_role != InvitationRole::initiator &&
                 first.local_role != InvitationRole::responder) ||
                first.review_context.request != request_.delivery_token ||
                !first.review_context.generation ||
                !invitation_detail::nonzero(first.review_context.boot)) return false;
            local_ = first;
            if (!observe_authority(first)) return false;
            EvaluationEnrollmentCandidate own{};
            if (!export_evaluation_enrollment_candidate(identity_, own) || failed_ ||
                candidate.version != EvaluationEnrollmentCandidate::current_version ||
                candidate.profile != EvaluationEnrollmentCandidate::ed25519_evaluation_profile ||
                !invitation_detail::nonzero(candidate.public_identity) ||
                candidate.public_identity == own.public_identity) return false;
            local_key_ = own.public_identity;
            const auto sample = guarded_port_.sample();
            if (failed_ || !(sample.context == local_.review_context)) return false;
            review_.emplace(guarded_port_, local_key_, local_.local_role, local_.group);
            return review_->begin_until(candidate.public_identity, request_.deadline_ms) && !failed_;
        });
    }
    bool show_peer() { return operation([&] { return review_ && review_->show_peer(); }); }
    bool show_local() { return operation([&] { return review_ && review_->show_local(); }); }
    bool poll() { return operation([&] { return review_ && review_->poll(); }); }
    bool take_review(std::optional<ReviewedEnrollmentIdentity>& output) {
        std::optional<ReviewedEnrollmentIdentity> staged;
        const bool ok = operation([&] {
            if (!review_ || !review_->take_review(staged)) return false;
            const auto after = guarded_port_.sample();
            return !failed_ && after.context == local_.review_context && staged &&
                staged->deadline() == request_.deadline_ms &&
                after.display_revision == staged->display_revision();
        });
        if (ok) output = std::move(staged);
        return ok;
    }
    // Preserve the live request guard after the local receipt is consumed.
    FingerprintReviewPort& preparation_port() { return guarded_port_; }
    void cancel() { (void)refuse(); }
    bool failed() const { return failed_; }

private:
    static bool same_context(const companion::DeviceNameContext& a,
                             const companion::DeviceNameContext& b) {
        return a.device == b.device && a.runtime == b.runtime && a.owner == b.owner &&
            a.owner_generation == b.owner_generation &&
            a.transport_generation == b.transport_generation &&
            a.controller == b.controller && a.session_nonce == b.session_nonce;
    }
    static bool same_request(const companion::SelectedEnrollmentRequest& a,
                             const companion::SelectedEnrollmentRequest& b) {
        return same_context(a.authority, b.authority) &&
            a.connection_handle == b.connection_handle && a.delivery_token == b.delivery_token &&
            a.exchange_id == b.exchange_id && a.admitted_ms == b.admitted_ms &&
            a.deadline_ms == b.deadline_ms;
    }
    bool refuse() {
        failed_ = true;
        if (review_) review_->cancel();
        if (captured_) (void)request_owner_.cancel_exact(request_);
        return false;
    }
    bool observe_authority(const EnrollmentCandidateLocalObservation& o) {
        // Check exact ownership BEFORE observe(), which may cancel its current
        // request. An old review must never cancel a newer request after reuse.
        if (failed_ || !captured_ || !request_owner_.pending() ||
            !same_request(request_owner_.request(), request_) ||
            !same_request(o.intent_request, request_) ||
            o.monotonic_domain != clock_domain_ ||
            o.local_role != local_.local_role || o.group != local_.group ||
            !(o.review_context == local_.review_context) ||
            o.authority.now_ms < last_authority_ ||
            !request_owner_.observe(o.authority, o.connection_handle)) return false;
        last_authority_ = o.authority.now_ms;
        return true;
    }
    bool read(FingerprintReviewSample& result) {
        EnrollmentCandidateLocalObservation before{}, after{};
        if (!authority_.observe(before) || !observe_authority(before)) return false;
        const auto staged = device_port_.sample();
        InvitationKey key{};
        if (failed_ || !identity_.public_key(key) || failed_ || key != local_key_ ||
            !authority_.observe(after) || !observe_authority(after) || identity_.failed() ||
            !(staged.context == local_.review_context) ||
            staged.now_ms < before.authority.now_ms || staged.now_ms > after.authority.now_ms ||
            staged.now_ms < last_sample_ || staged.now_ms >= request_.deadline_ms) return false;
        last_sample_ = staged.now_ms;
        result = staged;
        return true;
    }
    FingerprintReviewSample sample() {
        if (port_busy_ || failed_) { refuse(); return {}; }
        port_busy_ = true;
        FingerprintReviewSample staged{};
        const bool ok = read(staged);
        if (!ok) refuse();
        port_busy_ = false;
        return failed_ ? FingerprintReviewSample{} : staged;
    }
    bool show(const FingerprintReviewFrame& frame) {
        if (port_busy_ || failed_) return refuse();
        port_busy_ = true;
        FingerprintReviewSample before{}, after{};
        const bool ok = read(before) && frame.local_role == local_.local_role &&
            frame.group == local_.group && device_port_.show(frame) && !failed_ &&
            read(after) && after.display_revision == frame.revision;
        if (!ok) refuse();
        port_busy_ = false;
        return ok && !failed_;
    }
    template<class Action> bool operation(Action action) {
        if (busy_ || port_busy_ || failed_) return refuse();
        busy_ = true;
        const bool ok = action();
        busy_ = false;
        if (!ok) refuse();
        return ok && !failed_;
    }
    companion::SelectedEnrollmentRequestOwner& request_owner_;
    EnrollmentCandidateLocalAuthority& authority_;
    EnrollmentIdentityStore& identity_;
    FingerprintReviewPort& device_port_;
    const std::uint64_t clock_domain_;
    GuardedPort guarded_port_;
    companion::SelectedEnrollmentRequest request_{};
    EnrollmentCandidateLocalObservation local_{};
    InvitationKey local_key_{};
    std::optional<EnrollmentFingerprintReview> review_;
    std::uint64_t last_authority_{}, last_sample_{};
    bool attempted_{}, captured_{}, failed_{}, busy_{}, port_busy_{};
};
} // namespace opentrail::security_evaluation
