#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "opentrail/product_enrollment_activation.hpp"

namespace opentrail::companion {

// Internal selected-identity observation, not an authentication proof or wire
// response. In particular it makes no radio-armed claim. The protected-channel
// exporter and real Packet transport must supply their own live admission.
struct CommittedEnrollmentQuery {
    security_evaluation::InvitationKey remote_identity{};
    std::uint64_t group{};
    std::uint32_t epoch{}, nonce{};
    std::uint8_t identity_domain{1}; // Frozen full Ed25519 identity slot.
};

struct CommittedEnrollmentObservation {
    security_evaluation::InvitationKey local_identity{}, remote_identity{};
    std::uint64_t group{}, device_generation{}, session_generation{}, membership_revision{};
    std::uint32_t epoch{}, nonce{}, remaining_ms{};
    std::uint8_t identity_domain{};
};

// The serialized enrollment owner and all its dependencies must outlive this
// producer. Construction grants nothing. Each call observes that exact live
// owner; no caller-provided admitted flag, generation, deadline or cached proof
// can substitute for its committed endpoint and durable stores.
class CommittedEnrollmentAuthority final {
public:
    explicit CommittedEnrollmentAuthority(
        security_evaluation::ProductEnrollmentActivation& owner) : owner_(owner) {}
    CommittedEnrollmentAuthority(const CommittedEnrollmentAuthority&) = delete;
    CommittedEnrollmentAuthority& operator=(const CommittedEnrollmentAuthority&) = delete;
    CommittedEnrollmentAuthority(CommittedEnrollmentAuthority&&) = delete;
    CommittedEnrollmentAuthority& operator=(CommittedEnrollmentAuthority&&) = delete;

    bool snapshot(const CommittedEnrollmentQuery& request,
                  CommittedEnrollmentObservation& output) {
        using namespace security_evaluation;
        const auto query = request; // Freeze caller input across callbacks.
        output = {};
        if (query.identity_domain != 1 || !query.group || !query.epoch ||
            !query.nonce || !invitation_detail::nonzero(query.remote_identity)) return false;

        CommittedEnrollmentObservation staged{};
        bool matched = false;
        const bool ok = owner_.operation([&] {
            // An early query refuses without consuming a still-pending workflow.
            if (!owner_.committed_) return true;
            if (!owner_.current() || !owner_.binding_ || !owner_.receipt_ ||
                !owner_.confirmation_) return false;
            const auto authenticated_context = owner_.confirmation_->context();
            EnrollmentCommitContext actual{};
            PeerMembership member{};
            const auto expected = owner_.receipt_->context();
            if (!owner_.journal_.read_public(actual) || !owner_.membership_.read(member) ||
                !same_commit(actual, expected) || member.state != MembershipState::active ||
                !member.generation || member.binding != actual.evidence_digest ||
                !actual.session_generation || actual.session_generation != owner_.generation_ ||
                actual.session_generation != owner_.context_.generation) return false;

            const bool initiator = owner_.role_ == InvitationRole::initiator;
            if (!initiator && owner_.role_ != InvitationRole::responder) return false;
            const auto local = initiator ? actual.identities.initiator : actual.identities.responder;
            const auto remote = initiator ? actual.identities.responder : actual.identities.initiator;
            if (!invitation_detail::nonzero(local) || !invitation_detail::nonzero(remote) ||
                local == remote) return false;
            if (query.remote_identity != remote || query.group != actual.group ||
                query.epoch != actual.epoch) return true; // Refusal discloses no facts.

            const auto fields = independent_invitation_detail::decode(owner_.binding_->invitation());
            const auto issued = initiator ? fields.issued_a_ms : fields.issued_b_ms;
            const auto window = initiator ? fields.window_a_ms : fields.window_b_ms;
            if (!window || issued > std::numeric_limits<std::uint64_t>::max() - window)
                return false;
            const auto deadline = issued + window;
            ConfirmationSample before{};
            if (!owner_.observed_clock(before) || owner_.reentered_ ||
                before.context != authenticated_context ||
                !valid_time(before.now_ms, issued, deadline) ||
                !before.context.transport_generation || !before.context.session_nonce ||
                (seen_ && before.now_ms < last_now_)) return false;

            staged.local_identity = local;
            staged.remote_identity = remote;
            staged.group = actual.group;
            staged.epoch = actual.epoch;
            staged.device_generation = actual.session_generation;
            staged.session_generation = actual.session_generation;
            staged.membership_revision = member.generation;
            staged.nonce = query.nonce;
            staged.identity_domain = 1;

            // Complete all original durable/endpoint ready checks after staging,
            // then copy their LAST validated sample without another callback.
            // No independent raw-clock reading can inflate this lifetime.
            if (!owner_.current()) return false;
            ConfirmationSample after{};
            if (!owner_.observed_clock(after) || owner_.reentered_ ||
                after.context != authenticated_context ||
                after.context != before.context ||
                after.now_ms < before.now_ms || !valid_time(after.now_ms, issued, deadline)) return false;
            staged.remaining_ms = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(deadline - after.now_ms, 30000));
            last_now_ = after.now_ms;
            seen_ = true;
            matched = true;
            return true;
        });
        // operation() rechecks allocator/identity and lifecycle after the action;
        // reentrancy or a post-staging fault never releases staged metadata.
        if (!ok || !matched) { output = {}; return false; }
        output = staged;
        return true;
    }

    // Use at command acceptance in the same serialized owner turn. It accepts
    // only the requested context and repeats live checks; an old observation
    // cannot be passed back to grant authority. No command is dispatched here.
    bool command_current(const CommittedEnrollmentQuery& query) {
        CommittedEnrollmentObservation ignored{};
        return snapshot(query, ignored);
    }

private:
    static bool valid_time(std::uint64_t now, std::uint64_t issued, std::uint64_t deadline) {
        return now != std::numeric_limits<std::uint64_t>::max() && now >= issued && now < deadline;
    }
    static bool same_commit(const security_evaluation::EnrollmentCommitContext& a,
                            const security_evaluation::EnrollmentCommitContext& b) {
        return a.identities.initiator == b.identities.initiator &&
            a.identities.responder == b.identities.responder && a.group == b.group &&
            a.epoch == b.epoch && a.session_generation == b.session_generation &&
            a.evidence_digest == b.evidence_digest && a.operation == b.operation;
    }
    security_evaluation::ProductEnrollmentActivation& owner_;
    std::uint64_t last_now_{};
    bool seen_{};
};

} // namespace opentrail::companion
