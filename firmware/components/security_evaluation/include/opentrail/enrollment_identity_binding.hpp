#pragma once
// OT-0238b host candidate only: signatures bind locally provisioned persistent
// identities to fresh session invitations. Pin provisioning, durable consumption,
// activation, clocks and product crypto selection remain separate gates.
#include "opentrail/independent_invitation.hpp"
#include <optional>

namespace opentrail::security_evaluation {
struct RetainedEnrollmentIdentities { InvitationKey initiator{}, responder{}; };
struct EnrollmentIdentityProof {
    IndependentInvitation invitation{};
    std::array<std::uint8_t, 64> initiator_signature{}, responder_signature{};
};
using EnrollmentIdentitySigningBytes = std::array<std::uint8_t, 16 + 64 + kIndependentInvitationPayloadBytes>;
// Ordered identities and the entire existing canonical invitation are signed by
// BOTH retained signers. No keys supplied by the proof select verification roots.
inline EnrollmentIdentitySigningBytes enrollment_identity_signing_bytes(
    const RetainedEnrollmentIdentities& identities, const IndependentInvitation& invitation) {
    EnrollmentIdentitySigningBytes bytes{};
    constexpr std::array<std::uint8_t,16> domain{'O','T','-','E','N','R','O','L','L','-','B','I','N','D',0,1};
    std::memcpy(bytes.data(), domain.data(), domain.size());
    std::memcpy(bytes.data()+16, identities.initiator.data(),32);
    std::memcpy(bytes.data()+48, identities.responder.data(),32);
    std::memcpy(bytes.data()+80, invitation.payload.data(), invitation.payload.size());
    return bytes;
}
class EnrollmentIdentityVerifier;
class VerifiedIdentityBinding final {
public:
    const IndependentInvitation& invitation() const { return invitation_; }
    const RetainedEnrollmentIdentities& identities() const { return identities_; }
private:
    friend class EnrollmentIdentityVerifier;
    VerifiedIdentityBinding(RetainedEnrollmentIdentities identities, IndependentInvitation invitation)
        : identities_(identities), invitation_(invitation) {}
    RetainedEnrollmentIdentities identities_;
    IndependentInvitation invitation_;
};
class EnrollmentIdentityVerifier final {
public:
    // Initial enrollment is always epoch 1. Supplied pins MUST originate in the
    // unresolved trusted local provisioning path, never an untrusted packet.
    EnrollmentIdentityVerifier(RetainedEnrollmentIdentities pins, InvitationKey inviter, std::uint64_t group)
        : pins_(pins), inviter_(inviter), group_(group) {}
    // A prior cryptographically verified context is required, not a caller bool.
    // Caller separately proves it is the CURRENT durably activated membership.
    // Reuse checks cover this immediate predecessor only. Older history, rollback
    // protection and once-only acceptance require the durable product owner.
    explicit EnrollmentIdentityVerifier(const VerifiedIdentityBinding& prior)
        : pins_(prior.identities_), inviter_(independent_invitation_detail::decode(prior.invitation_).signer),
          group_(independent_invitation_detail::decode(prior.invitation_).group), prior_(prior) {}
    bool verify(const EnrollmentIdentityProof& proof, std::optional<VerifiedIdentityBinding>& output) const {
        const auto candidate = proof;
        const auto fields = independent_invitation_detail::decode(candidate.invitation);
        IndependentInvitation canonical{};
        if (!invitation_detail::nonzero(pins_.initiator) || !invitation_detail::nonzero(pins_.responder) ||
            pins_.initiator == pins_.responder || !invitation_detail::nonzero(inviter_) || group_ == 0 || inviter_ != pins_.initiator ||
            !encode_independent_invitation(fields, canonical) || canonical.payload != candidate.invitation.payload ||
            fields.group != group_ || fields.signer != inviter_) return false;
        if (prior_) {
            const auto previous = independent_invitation_detail::decode(prior_->invitation_);
            if (previous.epoch == std::numeric_limits<std::uint32_t>::max() || fields.epoch != previous.epoch + 1 ||
                fields.nonce == previous.nonce || fields.peer_a == previous.peer_a || fields.peer_a == previous.peer_b ||
                fields.peer_b == previous.peer_a || fields.peer_b == previous.peer_b) return false;
        } else if (fields.epoch != 1) return false;
        const auto bytes = enrollment_identity_signing_bytes(pins_, candidate.invitation);
        if (crypto_sign_verify_detached(candidate.invitation.signature.data(), candidate.invitation.payload.data(),
                candidate.invitation.payload.size(), inviter_.data()) != 0 ||
            crypto_sign_verify_detached(candidate.initiator_signature.data(), bytes.data(), bytes.size(), pins_.initiator.data()) != 0 ||
            crypto_sign_verify_detached(candidate.responder_signature.data(), bytes.data(), bytes.size(), pins_.responder.data()) != 0) return false;
        output = VerifiedIdentityBinding(pins_, candidate.invitation);
        return true;
    }
private:
    const RetainedEnrollmentIdentities pins_;
    const InvitationKey inviter_;
    const std::uint64_t group_;
    const std::optional<VerifiedIdentityBinding> prior_{};
};
} // namespace opentrail::security_evaluation
