#include "opentrail/enrollment_fingerprint_review.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace opentrail::security_evaluation;
struct Port final : FingerprintReviewPort {
    FingerprintReviewSample value{};
    FingerprintReviewFrame frame{};
    std::function<void()> callback=[]{};
    bool display_ok=true, update_revision=true;
    Port(){value.context.boot.fill(7);value.context.generation=1;value.context.request=1;value.now_ms=100;}
    FingerprintReviewSample sample() override { callback(); return value; }
    bool show(const FingerprintReviewFrame& f) override { frame=f;if(update_revision)value.display_revision=f.revision;return display_ok; }
};
struct Fixture {
    RetainedEnrollmentIdentities pins{};
    std::array<unsigned char,64> a{},b{};
    Port port;
    std::optional<EnrollmentFingerprintReview> owner;
    InvitationRole role;
    explicit Fixture(InvitationRole r=InvitationRole::initiator):role(r){
        std::array<unsigned char,32> seed{};seed.fill(1);crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data());
        seed.fill(2);crypto_sign_seed_keypair(pins.responder.data(),b.data(),seed.data());
        owner.emplace(port,role==InvitationRole::initiator?pins.initiator:pins.responder,role,9);
    }
    ~Fixture(){sodium_memzero(a.data(),a.size());sodium_memzero(b.data(),b.size());}
    bool begin(){return owner->begin(role==InvitationRole::initiator?pins.responder:pins.initiator);}
    void ready(){assert(begin());assert(owner->show_peer());assert(owner->poll());port.value.now_ms=101;port.value.button_down=true;assert(owner->poll());port.value.now_ms=1101;port.value.button_down=false;assert(owner->poll());}
    EnrollmentIdentityProof proof(std::uint64_t issued=1101){
        IndependentInvitationFields f{};f.group=9;f.epoch=1;f.signer=pins.initiator;
        f.peer_a.fill(11);f.peer_b.fill(12);f.nonce.fill(13);f.boot_a=port.value.context.boot;f.boot_b=f.boot_a;
        f.issued_a_ms=issued;f.issued_b_ms=issued;f.window_a_ms=60000;f.window_b_ms=60000;
        EnrollmentIdentityProof p{};assert(encode_independent_invitation(f,p.invitation));
        crypto_sign_detached(p.invitation.signature.data(),nullptr,p.invitation.payload.data(),p.invitation.payload.size(),a.data());
        auto bytes=enrollment_identity_signing_bytes(pins,p.invitation);
        crypto_sign_detached(p.initiator_signature.data(),nullptr,bytes.data(),bytes.size(),a.data());
        crypto_sign_detached(p.responder_signature.data(),nullptr,bytes.data(),bytes.size(),b.data());return p;
    }
};
int main(){
    unsigned groups=0;
    for(auto role:{InvitationRole::initiator,InvitationRole::responder}){
        Fixture f(role);f.ready();const auto& key=role==InvitationRole::initiator?f.pins.responder:f.pins.initiator;
        constexpr char hex[]="0123456789ABCDEF";
        for(unsigned i=0;i<32;++i){assert(f.port.frame.digits[i/8][2*(i%8)]==hex[key[i]>>4]);assert(f.port.frame.digits[i/8][2*(i%8)+1]==hex[key[i]&15]);}
        for(auto row:f.port.frame.digits)assert(row[16]==0);
        assert(f.port.frame.peer_page && f.port.frame.local_role==role);
        std::optional<VerifiedIdentityBinding> out;assert(f.owner->bind(f.proof(),out));assert(out);
        const auto before=out->invitation();assert(!f.owner->bind(f.proof(),out));assert(out->invitation().payload==before.payload);++groups;
    }
    for(unsigned variant=0;variant<14;++variant){
        Fixture f;f.ready();auto p=f.proof();
        std::optional<VerifiedIdentityBinding> out;assert(EnrollmentIdentityVerifier(f.pins,f.pins.initiator,9).verify(p,out));const auto before=out->invitation();
        switch(variant){
        case 0:f.owner->cancel();break;
        case 1:++f.port.value.display_revision;break;
        case 2:++f.port.value.context.generation;break;
        case 3:++f.port.value.context.request;break;
        case 4:++f.port.value.context.boot[0];break;
        case 5:f.port.value.now_ms=1100;break;
        case 6:f.port.value.now_ms=120100;break;
        case 7:f.port.value.now_ms=61101;break;
        case 8:p=f.proof(1100);break;
        case 9:p=f.proof(1102);break;
        case 10:p.responder_signature[0]^=1;break;
        case 11:assert(f.owner->show_local());break;
        case 12:assert(f.owner->show_peer());break;
        case 13:assert(!f.owner->begin(f.pins.responder));break;
        }
        assert(!f.owner->bind(p,out));assert(out->invitation().payload==before.payload && out->invitation().signature==before.signature);++groups;
    }
    // Changes during the final delegated sample must suppress staged proof.
    for(unsigned variant=0;variant<7;++variant){
        Fixture f;f.ready();auto p=f.proof();unsigned count=0;f.port.callback=[&]{if(++count!=2)return;
            switch(variant){case 0:f.owner->cancel();break;case 1:assert(!f.owner->poll());break;
            case 2:++f.port.value.context.request;break;case 3:++f.port.value.display_revision;break;
            case 4:f.port.value.now_ms=120100;break;case 5:f.port.value.now_ms=1000;break;case 6:f.port.value.now_ms=61101;break;}};
        std::optional<VerifiedIdentityBinding> out;assert(!f.owner->bind(p,out) && !out);++groups;
    }
    for(unsigned variant=0;variant<6;++variant){
        Fixture f;
        if(variant==0)f.port.display_ok=false;
        if(variant==1)f.port.update_revision=false;
        if(variant==2)f.port.value.context.generation=0;
        if(variant==3)f.port.value.context.boot.fill(0);
        if(variant==4)f.port.value.now_ms=std::numeric_limits<std::uint64_t>::max();
        if(variant==5)f.port.callback=[&]{f.owner->cancel();};
        assert(!f.begin());++groups;
    }
    // Held-at-entry, short tap, own page and overlong/reset holds never confirm.
    for(unsigned variant=0;variant<4;++variant){
        Fixture f;assert(f.begin());if(variant!=2)assert(f.owner->show_peer());
        f.port.value.button_down=variant==0;if(variant!=0)assert(f.owner->poll());
        f.port.value.button_down=true;f.port.value.now_ms=101;assert(f.owner->poll());
        f.port.value.now_ms=variant==1?500:variant==3?4000:1101;f.port.value.button_down=false;assert(f.owner->poll());
        std::optional<VerifiedIdentityBinding> out;assert(!f.owner->bind(f.proof(),out) && !out);++groups;
    }
    std::cout<<"PASS "<<groups<<" enrollment fingerprint review groups (hardware port simulated)\n";
}
