#include "opentrail/enrollment_possession_proof.hpp"
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include <type_traits>
using namespace invitation_lifecycle_test;
struct Random final : security::SecureRandomSource {
    unsigned value;
    explicit Random(unsigned v):value(v){}
    security::EntropyState state() const override{return security::EntropyState::ready;}
    security::RandomFillResult fill(std::uint8_t* out,std::size_t size) override {
        for(std::size_t i=0;i<size;++i)out[i]=static_cast<std::uint8_t>(value+i);
        return {security::RandomFillError::none,size};
    }
};
struct PossessionFixture {
    Storage sa,sb;Random ra{1},rb{41};EnrollmentIdentityStore a{sa,ra},b{sb,rb};
    RetainedEnrollmentIdentities pins{};EnrollmentPossessionStatement statement{};
    PossessionFixture(){
        CHECK(a.initialize() && b.initialize());CHECK(a.public_key(pins.initiator) && b.public_key(pins.responder));
        statement.group=9;statement.initiator_context.boot.fill(1);statement.responder_context.boot.fill(2);
        statement.initiator_context.generation=3;statement.responder_context.generation=4;
        statement.initiator_context.request=5;statement.responder_context.request=6;
        statement.initiator_challenge.fill(7);statement.responder_challenge.fill(8);
    }
    EnrollmentPossessionProof proof(){
        EnrollmentPossessionProof p{};p.statement=statement;
        EnrollmentPossessionAttempt attempt(pins,statement);
        CHECK(attempt.sign(InvitationRole::initiator,a,p.initiator_signature));
        CHECK(attempt.sign(InvitationRole::responder,b,p.responder_signature));return p;
    }
};
int main(){
    static_assert(!std::is_default_constructible_v<VerifiedEnrollmentPossession>);
    static_assert(!std::is_copy_constructible_v<EnrollmentPossessionAttempt>);
    PossessionFixture f;auto proof=f.proof();std::optional<VerifiedEnrollmentPossession> output;
    EnrollmentPossessionAttempt first(f.pins,f.statement);CHECK(first.verify(proof,output));
    CHECK(output->statement()==f.statement);CHECK(!first.verify(proof,output));
    const auto reject=[&](EnrollmentPossessionProof bad,EnrollmentPossessionStatement expected,RetainedEnrollmentIdentities pins){
        EnrollmentPossessionAttempt attempt(pins,expected);CHECK(!attempt.verify(bad,output));
        CHECK(output->statement()==f.statement && output->identities().initiator==f.pins.initiator);
        CHECK(!attempt.verify(proof,output)); // Even failed verification consumes attempt.
    };
    for(unsigned i=0;i<64;++i){auto bad=proof;bad.initiator_signature[i]^=1;reject(bad,f.statement,f.pins);
        bad=proof;bad.responder_signature[i]^=1;reject(bad,f.statement,f.pins);}
    // Even valid signatures over any changed canonical byte (including domain
    // and explicit role tags) do not authenticate the expected transcript.
    const auto canonical=enrollment_possession_bytes(f.pins,f.statement);
    for(std::size_t i=0;i<canonical.size();++i){auto altered=canonical;altered[i]^=1;auto changed=proof;
        CHECK(f.a.sign(altered.data(),altered.size(),changed.initiator_signature));
        CHECK(f.b.sign(altered.data(),altered.size(),changed.responder_signature));reject(changed,f.statement,f.pins);}
    auto bad=proof;std::swap(bad.initiator_signature,bad.responder_signature);reject(bad,f.statement,f.pins);
    auto pins=f.pins;std::swap(pins.initiator,pins.responder);reject(proof,f.statement,pins);
    pins=f.pins;pins.responder=pins.initiator;reject(proof,f.statement,pins);
    pins=f.pins;pins.initiator.fill(0);reject(proof,f.statement,pins);
    // Fresh current owner rejects every old context/challenge, even though both
    // signatures on the old transcript remain valid.
    for(unsigned field=0;field<9;++field){auto s=f.statement;
        switch(field){case 0:++s.group;break;case 1:s.initiator_context.boot[0]^=1;break;
        case 2:s.responder_context.boot[0]^=1;break;case 3:++s.initiator_context.generation;break;
        case 4:++s.responder_context.generation;break;case 5:++s.initiator_context.request;break;
        case 6:++s.responder_context.request;break;case 7:s.initiator_challenge[0]^=1;break;
        default:s.responder_challenge[0]^=1;break;}
        reject(proof,s,f.pins);bad=proof;bad.statement=s;reject(bad,s,f.pins);
    }
    auto s=f.statement;s.initiator_challenge.fill(0);reject(proof,s,f.pins);
    s=f.statement;s.responder_challenge=s.initiator_challenge;reject(proof,s,f.pins);
    s=f.statement;s.initiator_context.request=0;reject(proof,s,f.pins);
    s=f.statement;s.responder_context.generation=0;reject(proof,s,f.pins);
    EnrollmentPossessionAttempt cancelled(f.pins,f.statement);cancelled.cancel();CHECK(!cancelled.verify(proof,output));
    std::array<std::uint8_t,64> signature{};signature.fill(91);const auto unchanged=signature;
    EnrollmentPossessionAttempt wrong(f.pins,f.statement);CHECK(!wrong.sign(InvitationRole::initiator,f.b,signature));CHECK(signature==unchanged);
    EnrollmentPossessionAttempt repeated(f.pins,f.statement);CHECK(repeated.sign(InvitationRole::initiator,f.a,signature));
    const auto signed_once=signature;CHECK(!repeated.sign(InvitationRole::initiator,f.a,signature));CHECK(signature==signed_once);
    CHECK(!repeated.verify(proof,output));
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read}){
        PossessionFixture broken;broken.sa.arm(fault);EnrollmentPossessionAttempt attempt(broken.pins,broken.statement);
        signature=unchanged;CHECK(!attempt.sign(InvitationRole::initiator,broken.a,signature));CHECK(signature==unchanged);
    }
    std::cout<<"PASS enrollment possession: real stored signatures, roles, freshness, consuming attempts, storage faults, transactional output\n";
}
