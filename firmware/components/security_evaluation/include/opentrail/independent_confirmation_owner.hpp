#pragma once
// OT229 additive independent-clock confirmation owner. One serialized caller;
// exact local decisions expose no traffic, peer confirmation or membership.
// Shared Ready/time/outcome types remain defined by the frozen owner header.
#include "opentrail/evaluation_confirmation_owner.hpp"
#include "opentrail/independent_authorized_policy_session.hpp"

namespace opentrail::security_evaluation {
class IndependentConfirmationOwner;
class IndependentConfirmationOffer final {
public:
    IndependentConfirmationOffer(const IndependentConfirmationOffer&) = default;
    IndependentConfirmationOffer& operator=(const IndependentConfirmationOffer&) = delete;
    const ConfirmationContext& context() const { return context_; }
    InvitationRole role() const { return role_; }
    std::uint64_t group() const { return group_; }
    std::uint32_t epoch() const { return epoch_; }
    const InvitationToken& nonce() const { return nonce_; }
    const InvitationKey& peer() const { return peer_; }
    const InvitationKey& transcript() const { return transcript_; }
    std::uint64_t deadline_ms() const { return deadline_; }
private:
    friend class IndependentConfirmationOwner;
    IndependentConfirmationOffer(ConfirmationContext context, InvitationRole role, const InvitationFields& fields,
                      const InvitationKey& transcript)
        : context_(context), role_(role), group_(fields.group), epoch_(fields.epoch), nonce_(fields.nonce),
          peer_(role == InvitationRole::initiator ? fields.peer_b : fields.peer_a),
          transcript_(transcript), deadline_(fields.deadline_ms) {}
    const ConfirmationContext context_;
    const InvitationRole role_;
    const std::uint64_t group_;
    const std::uint32_t epoch_;
    const InvitationToken nonce_;
    const InvitationKey peer_, transcript_;
    const std::uint64_t deadline_;
};

class IndependentConfirmationOwner final {
public:
    IndependentConfirmationOwner(IndependentAuthorizedPolicySession& session, IndependentRoleInvitationAuthority& role,
                                ConfirmationAuthority& authority)
        : session_(session), role_(role), authority_(authority) {}
    ~IndependentConfirmationOwner() { (void)close(); }
    IndependentConfirmationOwner(const IndependentConfirmationOwner&) = delete;
    IndependentConfirmationOwner& operator=(const IndependentConfirmationOwner&) = delete;

    bool begin(const IndependentInvitation& invitation) {
        // Snapshot caller-owned signed bytes before any delegated operation.
        const IndependentInvitation candidate = invitation;
        return operation([&] {
            if (outcome_ != ConfirmationOutcome::empty) return refuse();
            if (!session_.bind_authority(role_)) return refuse();
            if (!observe(true)) return refuse();
            const auto local = role_.role() == InvitationRole::initiator ? role_.peer_a() : role_.peer_b();
            if (!authority_detail::role_valid(role_.role()) || session_.public_identity() != local ||
                !session_.begin(candidate, last_now_) || !role_.current()) return refuse();
            // The real session has validated both v2 windows and their sums.
            // Presentation and subsequent observations use this role's clock only.
            const auto decoded = independent_invitation_detail::decode(candidate);
            const bool initiator = role_.role() == InvitationRole::initiator;
            fields_.group = decoded.group;
            fields_.epoch = decoded.epoch;
            fields_.peer_a = role_.peer_a();
            fields_.peer_b = role_.peer_b();
            fields_.nonce = decoded.nonce;
            fields_.issued_ms = initiator ? decoded.issued_a_ms : decoded.issued_b_ms;
            fields_.deadline_ms = fields_.issued_ms +
                (initiator ? decoded.window_a_ms : decoded.window_b_ms);
            begun_ = true;
            if (!observe()) return refuse();
            outcome_ = ConfirmationOutcome::handshake;
            return true;
        });
    }
    bool write(unsigned char* output, std::size_t capacity, std::size_t& size) {
        size = 0;
        return operation([&] {
            if (outcome_ != ConfirmationOutcome::handshake || !observe()) return refuse();
            if (!session_.write(output, capacity, size, last_now_) || !observe()) {
                // A late completion must not publish usable handshake bytes.
                if (output != nullptr && size <= capacity) sodium_memzero(output, size);
                size = 0;
                return refuse();
            }
            return true;
        });
    }
    bool read(const unsigned char* input, std::size_t size) {
        return operation([&] {
            if (outcome_ != ConfirmationOutcome::handshake || !observe() ||
                !session_.read(input, size, last_now_) || !observe()) return refuse();
            return true;
        });
    }
    bool finish() {
        return operation([&] {
            if (outcome_ != ConfirmationOutcome::handshake || !observe() ||
                !session_.finish(last_now_) || !observe()) return refuse();
            offer_.emplace(IndependentConfirmationOffer(context_, role_.role(), fields_, session_.transcript()));
            outcome_ = ConfirmationOutcome::review;
            return true;
        });
    }
    const IndependentConfirmationOffer* offer() {
        const IndependentConfirmationOffer* result = nullptr;
        (void)operation([&] {
            if (outcome_ != ConfirmationOutcome::review) return false;
            if (!observe()) return refuse();
            result = &*offer_;
            return true;
        });
        return outcome_ == ConfirmationOutcome::review && !revoked_ ? result : nullptr;
    }
    bool confirm(const IndependentConfirmationOffer& expected) {
        return operation([&] {
            if (!exact(expected) || !observe()) return refuse();
            // Spend the local request before durable confirmation. No caller
            // Boolean or copied descriptor can authorize the real core call.
            spent_ = true;
            if (!session_.confirm(offer_->transcript(), last_now_) || !observe()) return refuse();
            outcome_ = ConfirmationOutcome::local_confirmed;
            return true;
        });
    }
    bool cancel(const IndependentConfirmationOffer& expected) {
        return operation([&] {
            if (!exact(expected) || !observe()) return refuse();
            spent_ = true;
            const bool cancelled = cleanup();
            if (!cancelled || !observe(false, false)) return refuse();
            outcome_ = ConfirmationOutcome::cancelled;
            return true;
        });
    }
    // Explicit abort also cleans an already locally confirmed session: the
    // frozen wrapper retires active RX before wiping. Check this return value
    // in the consuming target; the destructor is only a best-effort fallback.
    bool close() {
        if (busy_) { revoked_ = true; return false; }
        if (cleanup_started_) return cleanup_ok_;
        busy_ = true;
        spent_ = true;
        const bool result = cleanup();
        outcome_ = result && !revoked_ ? ConfirmationOutcome::cancelled : ConfirmationOutcome::refused;
        busy_ = false;
        return result && !revoked_;
    }
    ConfirmationOutcome outcome() const { return outcome_; }
private:
    template<class Action> bool operation(Action action) {
        if (busy_) { revoked_ = true; return false; }
        if (outcome_ == ConfirmationOutcome::local_confirmed || outcome_ == ConfirmationOutcome::cancelled ||
            outcome_ == ConfirmationOutcome::refused) return false;
        busy_ = true;
        const bool result = action();
        if (revoked_) (void)refuse();
        busy_ = false;
        return result && !revoked_ && outcome_ != ConfirmationOutcome::refused;
    }
    bool observe(bool initial = false, bool require_role = true) {
        // Durable readback may be slow. Take the authority/time sample last so
        // readback cannot outlive the sample used to publish the result.
        if (begun_ && require_role && !role_.current()) return false;
        const auto sample = authority_.sample();
        if (revoked_ || sample.context.transport_generation == 0 || sample.context.session_nonce == 0 ||
            sample.now_ms == std::numeric_limits<std::uint64_t>::max() ||
            (clock_seen_ && sample.now_ms < last_now_)) return false;
        if (initial) context_ = sample.context;
        else if (sample.context != context_) return false;
        if (begun_ && (sample.now_ms < fields_.issued_ms || sample.now_ms >= fields_.deadline_ms)) return false;
        last_now_ = sample.now_ms; clock_seen_ = true;
        return true;
    }
    bool exact(const IndependentConfirmationOffer& expected) const {
        return outcome_ == ConfirmationOutcome::review && !spent_ && offer_ && &expected == &*offer_;
    }
    bool cleanup() {
        if (cleanup_started_) return cleanup_ok_;
        cleanup_started_ = true;
        cleanup_ok_ = session_.cancel() && session_.secrets_cleared();
        return cleanup_ok_;
    }
    bool refuse() {
        spent_ = true;
        outcome_ = ConfirmationOutcome::refused;
        (void)cleanup();
        return false;
    }
    IndependentAuthorizedPolicySession& session_;
    IndependentRoleInvitationAuthority& role_;
    ConfirmationAuthority& authority_;
    ConfirmationContext context_{};
    InvitationFields fields_{};
    std::optional<IndependentConfirmationOffer> offer_;
    ConfirmationOutcome outcome_{ConfirmationOutcome::empty};
    std::uint64_t last_now_{0};
    bool clock_seen_{false}, begun_{false}, spent_{false}, busy_{false}, revoked_{false};
    bool cleanup_started_{false}, cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
