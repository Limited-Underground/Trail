#pragma once
// Candidate pre-invitation proof only. The serialized product owner supplies
// locally reviewed pins and fresh allocator/entropy contexts, and rechecks their
// currentness before using the result. No invitation or traffic authority here.
#include "opentrail/enrollment_identity_binding.hpp"
#include "opentrail/enrollment_identity_store.hpp"

namespace opentrail::security_evaluation {
struct EnrollmentPossessionContext {
    InvitationToken boot{};
    std::uint64_t generation{}, request{};
    bool operator==(const EnrollmentPossessionContext& b) const {
        return boot == b.boot && generation == b.generation && request == b.request;
    }
};
struct EnrollmentPossessionStatement {
    std::uint64_t group{};
    EnrollmentPossessionContext initiator_context{}, responder_context{};
    std::array<std::uint8_t,32> initiator_challenge{}, responder_challenge{};
    bool operator==(const EnrollmentPossessionStatement& b) const {
        return group == b.group && initiator_context == b.initiator_context &&
            responder_context == b.responder_context && initiator_challenge == b.initiator_challenge &&
            responder_challenge == b.responder_challenge;
    }
};
using EnrollmentPossessionBytes = std::array<std::uint8_t,218>;
inline EnrollmentPossessionBytes enrollment_possession_bytes(
    const RetainedEnrollmentIdentities& pins, const EnrollmentPossessionStatement& statement) {
    EnrollmentPossessionBytes bytes{};
    constexpr std::array<std::uint8_t,16> domain{'O','T','-','P','O','S','S','E','S','S','I','O','N',0,0,1};
    std::size_t at=0;
    const auto append=[&](const auto& value) { for(auto byte:value) bytes[at++]=byte; };
    const auto integer=[&](std::uint64_t value) { for(unsigned i=0;i<8;++i) bytes[at++]=static_cast<std::uint8_t>(value>>(i*8)); };
    const auto context=[&](const EnrollmentPossessionContext& value) { append(value.boot);integer(value.generation);integer(value.request); };
    append(domain);bytes[at++]=1;append(pins.initiator);bytes[at++]=2;append(pins.responder);
    integer(statement.group);context(statement.initiator_context);context(statement.responder_context);
    append(statement.initiator_challenge);append(statement.responder_challenge);
    return bytes;
}
struct EnrollmentPossessionProof {
    EnrollmentPossessionStatement statement{};
    std::array<std::uint8_t,64> initiator_signature{}, responder_signature{};
};
class EnrollmentPossessionAttempt;
class VerifiedEnrollmentPossession final {
public:
    const RetainedEnrollmentIdentities& identities() const { return pins_; }
    const EnrollmentPossessionStatement& statement() const { return statement_; }
private:
    friend class EnrollmentPossessionAttempt;
    VerifiedEnrollmentPossession(RetainedEnrollmentIdentities pins,EnrollmentPossessionStatement statement)
        :pins_(pins),statement_(statement){}
    RetainedEnrollmentIdentities pins_;
    EnrollmentPossessionStatement statement_;
};
class EnrollmentPossessionAttempt final {
public:
    EnrollmentPossessionAttempt(RetainedEnrollmentIdentities pins,EnrollmentPossessionStatement expected)
        :pins_(pins),expected_(expected){}
    EnrollmentPossessionAttempt(const EnrollmentPossessionAttempt&)=delete;
    EnrollmentPossessionAttempt& operator=(const EnrollmentPossessionAttempt&)=delete;
    // Only the locally owned identity store can sign. Output is transactional.
    bool sign(InvitationRole role,EnrollmentIdentityStore& identity,std::array<std::uint8_t,64>& output) {
        if (spent_ || busy_ || !valid()) return fail();
        if (role!=InvitationRole::initiator && role!=InvitationRole::responder) return fail();
        const bool a=role==InvitationRole::initiator;
        if (a ? signed_a_ : signed_b_) return fail();
        busy_=true;
        InvitationKey key{};std::array<std::uint8_t,64> staged{};
        const auto bytes=enrollment_possession_bytes(pins_,expected_);
        const bool ok=identity.public_key(key) && key==(a ? pins_.initiator : pins_.responder) &&
            identity.sign(bytes.data(),bytes.size(),staged);
        busy_=false;
        if (!ok || spent_) return fail();
        (a ? signed_a_ : signed_b_)=true;output=staged;return true;
    }
    // Every verification attempt consumes this owner, including malformed proof.
    // Reconstructing owners with old inputs cannot replace durable allocation.
    bool verify(const EnrollmentPossessionProof& proof,std::optional<VerifiedEnrollmentPossession>& output) {
        if (spent_ || busy_) return fail();
        spent_=true;
        const auto candidate=proof;
        if (!valid() || !(candidate.statement==expected_)) return false;
        const auto bytes=enrollment_possession_bytes(pins_,candidate.statement);
        if (crypto_sign_verify_detached(candidate.initiator_signature.data(),bytes.data(),bytes.size(),pins_.initiator.data())!=0 ||
            crypto_sign_verify_detached(candidate.responder_signature.data(),bytes.data(),bytes.size(),pins_.responder.data())!=0) return false;
        output=VerifiedEnrollmentPossession(pins_,expected_);return true;
    }
    void cancel(){spent_=true;}
private:
    bool fail(){spent_=true;return false;}
    bool valid() const {
        const auto context=[](const EnrollmentPossessionContext& c){return invitation_detail::nonzero(c.boot) && c.generation!=0 && c.request!=0;};
        return invitation_detail::nonzero(pins_.initiator) && invitation_detail::nonzero(pins_.responder) && pins_.initiator!=pins_.responder &&
            expected_.group!=0 && context(expected_.initiator_context) && context(expected_.responder_context) &&
            invitation_detail::nonzero(expected_.initiator_challenge) && invitation_detail::nonzero(expected_.responder_challenge) &&
            expected_.initiator_challenge!=expected_.responder_challenge;
    }
    const RetainedEnrollmentIdentities pins_;
    const EnrollmentPossessionStatement expected_;
    bool spent_{},busy_{},signed_a_{},signed_b_{};
};
}
