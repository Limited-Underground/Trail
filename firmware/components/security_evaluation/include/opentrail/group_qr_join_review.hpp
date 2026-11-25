#pragma once

// F8 step 2d (D-170): the board-local review behind a QR group join's approve().
// Both boards show the same screens first pairing uses, on the trusted review
// port (one display and button owner):
// - fingerprints: this board's ID ("MY ID"), then the other board's ID
//   ("PEER ID"); hold the button 1 to 3 seconds on each page. The admin board
//   is the INVITER, the joiner board the MEMBER;
// - transcript: an 8-character code (0-9, A-F) from the join's transcript ("COMPARE CODE"),
//   the same on both boards when nobody is in the middle; hold to confirm.
// A press that starts before the page appears does not count. Holding longer
// than 3 seconds refuses the review; so do a changed display or context, and
// the join's own deadline. Approval covers only the exact review kind, side,
// peer and value that were shown.
// Host model: the port is the trusted application owner; nothing here reads
// GPIO, time or radio input directly.

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/group_qr_invite.hpp"

namespace opentrail::security_evaluation {

class GroupQrJoinReview {
public:
    using Review = identity::roster::qr::Review;
    using Side = identity::roster::qr::Side;
    enum class State : std::uint8_t { idle, showing, approved, refused };
    static constexpr std::uint64_t kHoldMs = 1000, kMaximumHoldMs = 3000;

    struct Request {
        Review review{Review::fingerprints};
        Side side{Side::joiner};
        identity::roster::PublicKey local{}, peer{};
        identity::roster::Digest value{};
        std::uint64_t group{}, deadline_ms{};
        bool operator==(const Request& b) const {
            return review == b.review && side == b.side && local == b.local && peer == b.peer &&
                   value == b.value && group == b.group && deadline_ms == b.deadline_ms;
        }
    };

    GroupQrJoinReview(EnrollmentSessionDevicePort& port, const FingerprintReviewContext& context, const Request& request)
        : port_(port), context_(context), request_(request),
          fingerprints_(port, request.local, role(request.side), request.group) {}
    GroupQrJoinReview(const GroupQrJoinReview&) = delete;
    GroupQrJoinReview& operator=(const GroupQrJoinReview&) = delete;
    ~GroupQrJoinReview() { close(); }

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const Request& request() const { return request_; }

    // Binds the review context and shows the first page.
    bool begin() {
        if (state_ != State::idle) return false;
        if (!valid(request_) || !port_.bind_context(context_)) return refuse();
        bound_ = true;
        state_ = State::showing;
        if (request_.review == Review::fingerprints)
            return fingerprints_.begin_until(request_.peer, request_.deadline_ms) || refuse();
        return show_code() || refuse();
    }

    // One step of the physical review. Call on each app-task poll.
    void poll() {
        if (state_ != State::showing) return;
        if (request_.review == Review::fingerprints) {
            EnrollmentGestureObservation seen{};
            if (!peer_page_) {
                if (!fingerprints_.poll_confirmation(true, seen)) { refuse(); return; }
                if (seen.confirmed) {
                    if (!fingerprints_.show_peer()) { refuse(); return; }
                    peer_page_ = true;
                }
                return;
            }
            if (!fingerprints_.poll_confirmation(false, seen)) { refuse(); return; }
            if (seen.confirmed) approve();
            return;
        }
        poll_code();
    }

    // True only for the exact request this board showed and confirmed.
    [[nodiscard]] bool approves(const Request& request) const {
        return state_ == State::approved && request == request_;
    }

    // Ends the review and gives the display back.
    void close() {
        if (state_ == State::showing) (void)refuse();
        if (bound_) {
            fingerprints_.cancel();
            (void)port_.release_context(context_);
            bound_ = false;
        }
    }

    // The 8 hex digits shown for a transcript value.
    static std::array<char, 8> code(const identity::roster::Digest& value) {
        constexpr char hex[] = "0123456789ABCDEF";
        std::array<char, 8> out{};
        for (unsigned i = 0; i < 4; ++i) {
            out[2 * i] = hex[value[i] >> 4];
            out[2 * i + 1] = hex[value[i] & 15];
        }
        return out;
    }

private:
    static InvitationRole role(Side side) {
        return side == Side::admin ? InvitationRole::initiator : InvitationRole::responder;
    }
    static bool nonzero(const identity::roster::PublicKey& value) {
        for (const auto b : value) if (b) return true;
        return false;
    }
    static bool valid(const Request& r) {
        return (r.side == Side::admin || r.side == Side::joiner) && nonzero(r.local) && nonzero(r.peer) &&
               r.local != r.peer && nonzero(r.value) && r.group != 0 && r.deadline_ms != 0 &&
               (r.review == Review::fingerprints || r.review == Review::transcript);
    }
    bool refuse() {
        state_ = State::refused;
        return false;
    }
    void approve() {
        // The page must still be the one shown, inside the join's deadline.
        const auto sample = port_.sample();
        if (!(sample.context == context_) || sample.now_ms >= request_.deadline_ms) { refuse(); return; }
        state_ = State::approved;
    }

    bool show_code() {
        auto sample = port_.sample();
        if (!(sample.context == context_) || sample.now_ms >= request_.deadline_ms ||
            sample.display_revision == std::numeric_limits<std::uint64_t>::max()) return false;
        FingerprintReviewFrame frame{};
        frame.domain = {};
        constexpr char label[] = "OT-CODE1";
        std::memcpy(frame.domain.data(), label, sizeof(label));
        frame.purpose = EnrollmentDisplayPurpose::transcript_confirmation;
        frame.local_role = role(request_.side);
        frame.peer_page = true;
        frame.revision = sample.display_revision + 1;
        frame.group = request_.group;
        const auto digits = code(request_.value);
        std::memcpy(frame.digits[0].data(), digits.data(), digits.size());
        revision_ = frame.revision;
        if (!port_.show(frame)) return false;
        sample = port_.sample();
        if (!(sample.context == context_) || sample.display_revision != revision_ ||
            sample.now_ms >= request_.deadline_ms) return false;
        last_ = sample.now_ms;
        released_ = !sample.button_down;  // a press held from before does not count
        return true;
    }

    void poll_code() {
        const auto sample = port_.sample();
        if (!(sample.context == context_) || sample.display_revision != revision_ || sample.now_ms < last_ ||
            sample.now_ms >= request_.deadline_ms) { refuse(); return; }
        last_ = sample.now_ms;
        if (!sample.button_down) {
            if (pressed_) {
                const auto held = sample.now_ms - pressed_at_;
                pressed_ = false;
                if (held >= kHoldMs && held <= kMaximumHoldMs) { approve(); return; }
            }
            released_ = true;
            return;
        }
        if (!released_) return;
        if (!pressed_) { pressed_ = true; pressed_at_ = sample.now_ms; return; }
        if (sample.now_ms - pressed_at_ > kMaximumHoldMs) refuse();
    }

    EnrollmentSessionDevicePort& port_;
    const FingerprintReviewContext context_;
    const Request request_;
    EnrollmentFingerprintReview fingerprints_;
    State state_{State::idle};
    std::uint64_t revision_{}, last_{}, pressed_at_{};
    bool bound_{}, peer_page_{}, released_{}, pressed_{};
};

// Fresh review ports from the trusted display and button owner, one per review
// (the board's review port is one-shot).
class GroupQrJoinReviewPorts {
public:
    virtual ~GroupQrJoinReviewPorts() = default;
    virtual EnrollmentSessionDevicePort* open() = 0;
    virtual void close() = 0;
};

// Turns the QR session's approve() questions into one physical review at a
// time. approve() never draws: it answers from the review already shown, or
// queues the question for poll(), which runs on the same app task between
// session operations.
class GroupQrJoinReviewDesk {
public:
    using Request = GroupQrJoinReview::Request;
    using State = GroupQrJoinReview::State;

    GroupQrJoinReviewDesk(GroupQrJoinReviewPorts& ports, const identity::roster::BootToken& boot)
        : ports_(ports), boot_(boot) {}
    GroupQrJoinReviewDesk(const GroupQrJoinReviewDesk&) = delete;
    GroupQrJoinReviewDesk& operator=(const GroupQrJoinReviewDesk&) = delete;
    ~GroupQrJoinReviewDesk() { reset(); }

    bool approve(const Request& request) {
        if (review_ && review_->request() == request) return review_->state() == State::approved;
        if (!review_ || review_->state() != State::showing) pending_ = request;
        return false;
    }

    void poll() {
        if (pending_ && (!review_ || review_->state() != State::showing)) {
            const auto request = *pending_;
            pending_.reset();
            end_review();
            auto* port = ports_.open();
            if (port == nullptr) { refused_ = true; return; }
            const FingerprintReviewContext context{
                boot_, ++generation_, request.review == identity::roster::qr::Review::fingerprints ? 1u : 2u};
            auto& review = review_.emplace(*port, context, request);
            if (!review.begin()) refused_ = true;
            return;
        }
        if (!review_) return;
        review_->poll();
        if (review_->state() == State::refused) refused_ = true;
    }

    // A review the person refused, or that could not be shown: the caller
    // cancels the join.
    [[nodiscard]] bool refused() const { return refused_; }
    [[nodiscard]] bool showing() const { return review_ && review_->state() == State::showing; }
    // The review on the screen now, if any.
    [[nodiscard]] std::optional<identity::roster::qr::Review> shown() const {
        if (!showing()) return std::nullopt;
        return review_->request().review;
    }

    void reset() {
        pending_.reset();
        end_review();
        refused_ = false;
    }

private:
    void end_review() {
        if (!review_) return;
        review_->close();
        review_.reset();
        ports_.close();
    }

    GroupQrJoinReviewPorts& ports_;
    const identity::roster::BootToken boot_;
    std::optional<GroupQrJoinReview> review_;
    std::optional<Request> pending_;
    std::uint64_t generation_{0};
    bool refused_{false};
};

}  // namespace opentrail::security_evaluation
