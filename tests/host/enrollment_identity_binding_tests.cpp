#include "opentrail/enrollment_identity_binding.hpp"
#include <cassert>
#include <iostream>
#include <type_traits>
using namespace opentrail::security_evaluation;
using Secret = std::array<unsigned char,64>;
int main() {
    static_assert(!std::is_default_constructible_v<VerifiedIdentityBinding>);
    std::array<unsigned char,32> seed{};
    RetainedEnrollmentIdentities pins{}; InvitationKey inviter{}; Secret a{},b{},s{};
    seed.fill(1); crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data());
    seed.fill(2); crypto_sign_seed_keypair(pins.responder.data(),b.data(),seed.data());
    inviter=pins.initiator; s=a;
    IndependentInvitationFields fields{};
    fields.group=9; fields.epoch=1; fields.signer=inviter;
    fields.peer_a.fill(11); fields.peer_b.fill(12); fields.nonce.fill(13);
    fields.boot_a.fill(14); fields.boot_b.fill(15);
    fields.issued_a_ms=100; fields.issued_b_ms=200; fields.window_a_ms=50000; fields.window_b_ms=50000;
    auto sign = [&](IndependentInvitationFields f) {
        EnrollmentIdentityProof p{}; assert(encode_independent_invitation(f,p.invitation));
        crypto_sign_detached(p.invitation.signature.data(),nullptr,p.invitation.payload.data(),p.invitation.payload.size(),s.data());
        auto bytes=enrollment_identity_signing_bytes(pins,p.invitation);
        crypto_sign_detached(p.initiator_signature.data(),nullptr,bytes.data(),bytes.size(),a.data());
        crypto_sign_detached(p.responder_signature.data(),nullptr,bytes.data(),bytes.size(),b.data());
        return p;
    };
    auto proof=sign(fields); EnrollmentIdentityVerifier initial(pins,inviter,9);
    std::optional<VerifiedIdentityBinding> out;
    assert(initial.verify(proof,out)); const auto first=*out;
    auto reject=[&](const EnrollmentIdentityVerifier& v,EnrollmentIdentityProof p) {
        const auto before=out->invitation(); assert(!v.verify(p,out));
        assert(before.payload==out->invitation().payload && before.signature==out->invitation().signature);
    };
    // Every canonical field, including version, both roles, boots and windows,
    // is covered by real signatures. All 188 one-byte mutations must reject.
    for(std::size_t i=0;i<proof.invitation.payload.size();++i) {
        auto p=proof; p.invitation.payload[i]^=1; reject(initial,p);
    }
    for(unsigned i=0;i<64;++i) {
        auto p=proof; p.initiator_signature[i]^=1; reject(initial,p);
        p=proof; p.responder_signature[i]^=1; reject(initial,p);
        p=proof; p.invitation.signature[i]^=1; reject(initial,p);
    }
    auto swapped=pins; std::swap(swapped.initiator,swapped.responder);
    reject(EnrollmentIdentityVerifier(swapped,inviter,9),proof);
    auto bad=pins; bad.initiator.fill(0); reject(EnrollmentIdentityVerifier(bad,inviter,9),proof);
    bad=pins; bad.responder=bad.initiator; reject(EnrollmentIdentityVerifier(bad,inviter,9),proof);
    reject(EnrollmentIdentityVerifier(pins,inviter,10),proof);
    auto next=fields; next.epoch=2; next.peer_a.fill(21); next.peer_b.fill(22); next.nonce.fill(23);
    EnrollmentIdentityVerifier rekey(first);
    assert(rekey.verify(sign(next),out));
    reject(initial,sign(next)); reject(rekey,proof);
    auto altered=next; altered.epoch=3; reject(rekey,sign(altered));
    altered=next; altered.nonce=fields.nonce; reject(rekey,sign(altered));
    for(auto old : {fields.peer_a,fields.peer_b}) {
        altered=next; altered.peer_a=old; reject(rekey,sign(altered));
        altered=next; altered.peer_b=old; reject(rekey,sign(altered));
    }
    altered=next; altered.group++; reject(rekey,sign(altered));
    auto p=sign(next); std::swap(p.initiator_signature,p.responder_signature); reject(rekey,p);
    // Changed retained signers can produce valid signatures but cannot alter pins.
    seed.fill(3); crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data()); reject(rekey,sign(next));
    auto third=fields; third.signer=pins.initiator; s=a;
    pins=first.identities(); seed.fill(1); crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data());
    reject(EnrollmentIdentityVerifier(first.identities(),third.signer,9),sign(third));
    sodium_memzero(a.data(),a.size()); sodium_memzero(b.data(),b.size()); sodium_memzero(s.data(),s.size());
    std::cout << "PASS 1 enrollment identity binding groups: real signatures, 380 byte mutations, role/pin/schema failures, staged output and rekey transitions\n";
}
