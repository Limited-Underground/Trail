#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/independent_invitation.hpp"
#include "noise_xk_libsodium.h"
#include <type_traits>
#include <functional>
#include "opentrail/independent_invitation_authority.hpp"
using namespace invitation_lifecycle_test;
static_assert(sizeof(IndependentInvitation)==252);
static_assert(!std::is_copy_constructible_v<IndependentInvitationGate>);
struct SignedV2 {
    IndependentInvitationFields f {
    };
    IndependentInvitation invitation {
    };
    std::array<unsigned char,64> secret {
    };
    SignedV2() {
        SignedInvitation old;
        f.group=old.fields.group;
        f.epoch=old.fields.epoch;
        f.signer=old.fields.signer;
        secret=old.secret;
        f.peer_a=old.fields.peer_a;
        f.peer_b=old.fields.peer_b;
        f.nonce.fill(3);
        f.boot_a.fill(4);
        f.boot_b.fill(9);
        f.issued_a_ms=100;
        f.window_a_ms=900;
        f.issued_b_ms=70000;
        f.window_b_ms=300;
        sign();
    }
    ~SignedV2() {
        sodium_memzero(secret.data(),secret.size());
    }
    void raw_sign() {
        CHECK(crypto_sign_detached(invitation.signature.data(),nullptr,invitation.payload.data(),invitation.payload.size(),secret.data())==0);
    }
    void sign() {
        CHECK(encode_independent_invitation(f,invitation));
        raw_sign();
    }
    bool open(IndependentInvitationGate& g,bool b=false) {
        return g.open(invitation,b?InvitationRole::responder:InvitationRole::initiator,f.signer,f.peer_a,f.peer_b,b?f.boot_b:f.boot_a,b?70000:100);
    }
};
static unsigned gate_tests() {
    unsigned groups=0;
    {
        SignedV2 s;
        const auto* p=s.invitation.payload.data();
        const std::array<unsigned char,8> magic {
            'O','T','E','I','N','V',0,2
        };
        CHECK(std::memcmp(p,magic.data(),8)==0);
        CHECK(p[8]==0&&p[15]==17&&p[19]==1);
        CHECK(std::memcmp(p+20,s.f.signer.data(),32)==0);
        CHECK(std::memcmp(p+52,s.f.peer_a.data(),32)==0&&std::memcmp(p+84,s.f.peer_b.data(),32)==0);
        CHECK(p[116]==3&&p[132]==4&&p[148]==0&&p[155]==100);
        CHECK(p[156]==0&&p[157]==0&&p[158]==3&&p[159]==0x84);
        CHECK(p[160]==9&&p[176]==0&&p[181]==1&&p[182]==0x11&&p[183]==0x70);
        CHECK(p[184]==0&&p[185]==0&&p[186]==1&&p[187]==0x2c);
        ++groups;
    }
    for(bool b: {
        false,true
    }) {
        SignedV2 s;
        auto& issued=b?s.f.issued_b_ms:s.f.issued_a_ms;
        auto& window=b?s.f.window_b_ms:s.f.window_a_ms;
        issued=UINT64_MAX-60000;
        window=60000;
        s.sign();
        IndependentInvitationGate g;
        CHECK(g.open(s.invitation,b?InvitationRole::responder:InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b,b?s.f.boot_b:s.f.boot_a,issued));
        CHECK(g.advance(UINT64_MAX-1));
        CHECK(!g.advance(UINT64_MAX));
        ++groups;
    }
    for(bool b: {
        false,true
    })for(unsigned fault=0; fault<3; ++fault) {
        SignedV2 s;
        auto* p=s.invitation.payload.data();
        const auto issued=b?176:148;
        const auto window=b?184:156;
        if(fault==0)invitation_detail::put(p+window,60001,4);
        if(fault==1)invitation_detail::put(p+window,0,4);
        if(fault==2)invitation_detail::put(p+issued,UINT64_MAX,8);
        s.raw_sign();
        IndependentInvitationGate g;
        CHECK(!s.open(g));
        ++groups;
    }
    {
        SignedV2 s;
        IndependentInvitationGate a,b;
        CHECK(s.open(a)&&s.open(b,true));
        CHECK(a.prologue()==b.prologue());
        CHECK(a.issued_ms()==100&&b.issued_ms()==70000&&a.deadline_ms()==1000&&b.deadline_ms()==70300);
        std::array<unsigned char,252> bytes {
        };
        std::memcpy(bytes.data(),s.invitation.payload.data(),188);
        std::memcpy(bytes.data()+188,s.invitation.signature.data(),64);
        InvitationKey expected {
        };
        CHECK(crypto_hash_sha256(expected.data(),bytes.data(),bytes.size())==0);
        CHECK(a.prologue()==expected);
        ++groups;
    }
    for(unsigned k=0; k<16; ++k) {
        SignedV2 s;
        auto old=s.invitation;
        if(k==0)s.f.group=0;
        if(k==1)s.f.epoch=0;
        if(k==2)s.f.signer= {
        };
        if(k==3)s.f.peer_a= {
        };
        if(k==4)s.f.peer_b= {
        };
        if(k==5)s.f.peer_b=s.f.peer_a;
        if(k==6)s.f.nonce= {
        };
        if(k==7)s.f.boot_a= {
        };
        if(k==8)s.f.boot_b= {
        };
        if(k==9)s.f.window_a_ms=0;
        if(k==10)s.f.window_b_ms=0;
        if(k==11)s.f.window_a_ms=60001;
        if(k==12)s.f.window_b_ms=60001;
        if(k==13)s.f.issued_a_ms=UINT64_MAX;
        if(k==14)s.f.issued_b_ms=UINT64_MAX;
        if(k==15) {
            s.f.issued_b_ms=UINT64_MAX-299;
            s.f.window_b_ms=300;
        }
        CHECK(!encode_independent_invitation(s.f,s.invitation));
        CHECK(s.invitation.payload==old.payload&&s.invitation.signature==old.signature);
        ++groups;
    }
    for(unsigned offset: {
        0U,7U,8U,16U,20U,52U,84U,116U,132U,148U,156U,160U,176U,184U
    }) {
        SignedV2 s;
        s.invitation.payload[offset]^=1;
        IndependentInvitationGate g;
        CHECK(!s.open(g));
        CHECK(g.failed()&&g.prologue()==InvitationKey {
        });
        CHECK(!s.open(g));
        ++groups;
    }
    {
        SignedV2 s;
        s.invitation.signature[63]^=1;
        IndependentInvitationGate g;
        CHECK(!s.open(g));
        ++groups;
    }
    for(unsigned k=0; k<6; ++k) {
        SignedV2 s;
        auto signer=s.f.signer,a=s.f.peer_a,b=s.f.peer_b;
        auto boot=s.f.boot_a;
        auto role=InvitationRole::initiator;
        if(k==0)signer[0]^=1;
        if(k==1)a[0]^=1;
        if(k==2)b[0]^=1;
        if(k==3)boot=s.f.boot_b;
        if(k==4)role=static_cast<InvitationRole>(99);
        if(k==5)role=InvitationRole::responder;
        IndependentInvitationGate g;
        CHECK(!g.open(s.invitation,role,signer,a,b,boot,100));
        ++groups;
    }
    for(bool b: {
        false,true
    })for(unsigned k=0; k<3; ++k) {
        SignedV2 s;
        IndependentInvitationGate g;
        auto issued=b?s.f.issued_b_ms:s.f.issued_a_ms;
        auto window=b?s.f.window_b_ms:s.f.window_a_ms;
        auto now=k==0?issued-1:k==1?issued+window:UINT64_MAX;
        CHECK(!g.open(s.invitation,b?InvitationRole::responder:InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b,b?s.f.boot_b:s.f.boot_a,now));
        ++groups;
    }
    for(unsigned k=0; k<4; ++k) {
        SignedV2 s;
        if(k==0)s.invitation.payload[7]=1;
        if(k==1)std::memset(s.invitation.payload.data()+8,0,8);
        if(k==2)std::memset(s.invitation.payload.data()+160,0,16);
        if(k==3)std::memset(s.invitation.payload.data()+184,0,4);
        s.raw_sign();
        IndependentInvitationGate g;
        CHECK(!s.open(g));
        ++groups;
    }
    {
        SignedV2 s;
        SignedInvitation old;
        old.sign();
        s.invitation= {
        };
        std::memcpy(s.invitation.payload.data(),old.invitation.payload.data(),old.invitation.payload.size());
        s.invitation.signature=old.invitation.signature;
        IndependentInvitationGate g;
        CHECK(!s.open(g));
        ++groups;
    }
    for(unsigned k=0; k<8; ++k) {
        SignedV2 s;
        IndependentInvitationGate g;
        InvitationKey t {
        };
        t.fill(3);
        CHECK(s.open(g));
        if(k==0) {
            CHECK(!s.open(g));
        }
        if(k==1) {
            CHECK(!g.confirm(t,101));
        }
        if(k==2) {
            CHECK(g.bind_transcript(t,101));
            auto wrong=t;
            wrong[0]^=1;
            CHECK(!g.confirm(wrong,102));
        }
        if(k==3) {
            CHECK(g.advance(500));
            CHECK(!g.advance(499));
        }
        if(k==4) {
            CHECK(!g.bind_transcript(t,1000));
        }
        if(k==5) {
            CHECK(g.bind_transcript(t,101));
            CHECK(!g.confirm(t,1000));
        }
        if(k==6) {
            CHECK(g.bind_transcript(t,101));
            CHECK(g.confirm(t,102));
            CHECK(!g.confirm(t,103));
        }
        if(k==7) {
            g.revoke();
        }
        CHECK(g.failed()&&!g.confirmed()&&!g.advance(101));
        ++groups;
    }
    return groups;
}
static ot_noise_xk_keypair key(unsigned seed) {
    ot_noise_xk_keypair k {
    };
    for(unsigned i=0; i<32; ++i)k.secret[i]=static_cast<unsigned char>(seed+i);
    CHECK(crypto_scalarmult_curve25519_base(k.public_key,k.secret)==0);
    return k;
}
static unsigned noise_tests() {
    unsigned groups=0;
    for(bool different: {
        false,true
    }) {
        SignedV2 s;
        Storage ba,bb,ra,rb;
        {
            InvitationBootAuthority old(bb);
            CHECK(old.start());
        }
        InvitationBootAuthority boot_a(ba),boot_b(bb);
        CHECK(boot_a.start()&&boot_b.start());
        CHECK(boot_a.context()!=boot_b.context());
        s.f.boot_a=boot_a.context();
        s.f.boot_b=boot_b.context();
        auto sa=key(1),sb=key(41),ea=key(81),eb=key(121);
        std::memcpy(s.f.peer_a.data(),sa.public_key,32);
        std::memcpy(s.f.peer_b.data(),sb.public_key,32);
        s.sign();
        IndependentInvitationGate ga,gb;
        CHECK(s.open(ga));
        const auto original=s.invitation;
        if(different) {
            s.f.nonce[0]^=1;
            s.sign();
        }
        CHECK(s.open(gb,true));
        IndependentRoleInvitationAuthority role_a(ra,boot_a,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b),role_b(rb,boot_b,InvitationRole::responder,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(role_a.consume(original,100)&&role_b.consume(s.invitation,70000));
        ot_noise_xk_state a {
        },b {
        };
        CHECK(ot_noise_xk_init_initiator(&a,&sa,&ea,sb.public_key,ga.prologue().data(),32)==0);
        CHECK(ot_noise_xk_init_responder(&b,&sb,&eb,gb.prologue().data(),32)==0);
        std::array<unsigned char,64> m {
        };
        std::size_t n=0;
        CHECK(ot_noise_xk_write_message(&a,m.data(),m.size(),&n)==0);
        if(different) {
            CHECK(ot_noise_xk_read_message(&b,m.data(),n)!=0);
        }
        else {
            CHECK(ot_noise_xk_read_message(&b,m.data(),n)==0);
            CHECK(ot_noise_xk_write_message(&b,m.data(),m.size(),&n)==0);
            CHECK(ot_noise_xk_read_message(&a,m.data(),n)==0);
            CHECK(ot_noise_xk_write_message(&a,m.data(),m.size(),&n)==0);
            CHECK(ot_noise_xk_read_message(&b,m.data(),n)==0);
            CHECK(std::memcmp(a.handshake_hash,b.handshake_hash,32)==0);
            CHECK(std::memcmp(a.remote_static_public,sb.public_key,32)==0);
            CHECK(std::memcmp(b.remote_static_public,sa.public_key,32)==0);
            InvitationKey t {
            };
            std::memcpy(t.data(),a.handshake_hash,32);
            CHECK(ga.bind_transcript(t,101)&&gb.bind_transcript(t,70001));
            CHECK(ga.confirm(t,102)&&!gb.confirmed());
            CHECK(gb.confirm(t,70002));
        }
        CHECK(role_a.cancel()&&role_b.cancel());
        ot_noise_xk_abort(&a);
        ot_noise_xk_abort(&b);
        sodium_memzero(&sa,sizeof sa);
        sodium_memzero(&sb,sizeof sb);
        sodium_memzero(&ea,sizeof ea);
        sodium_memzero(&eb,sizeof eb);
        ++groups;
    }
    return groups;
}
struct HookStorage final : persistence::PersistentStorage {
    Storage inner;
    std::function<void()> hook=[] {
    };
    bool firing=false;
    void fire() {
        if(!firing) {
            firing=true;
            hook();
            firing=false;
        }
    }
    persistence::StorageReadResult read_slot(Domain d,std::size_t i,persistence::MutableStorageByteView v)override {
        fire();
        return inner.read_slot(d,i,v);
    }
    Error erase_slot(Domain d,std::size_t i)override {
        fire();
        return inner.erase_slot(d,i);
    }
    Error write_slot(Domain d,std::size_t i,std::size_t o,persistence::StorageByteView v)override {
        fire();
        return inner.write_slot(d,i,o,v);
    }
    Error sync_slot(Domain d,std::size_t i)override {
        fire();
        return inner.sync_slot(d,i);
    }
};
static unsigned authority_tests() {
    unsigned groups=0;
    {
        SignedV2 s;
        Storage ba,bb,ra,rb;
        {
            InvitationBootAuthority previous(bb);
            CHECK(previous.start());
        }
        InvitationBootAuthority a(ba),b(bb);
        CHECK(a.start()&&b.start());
        CHECK(a.context()!=b.context());
        s.f.boot_a=a.context();
        s.f.boot_b=b.context();
        s.sign();
        IndependentRoleInvitationAuthority aa(ra,a,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b),ab(rb,b,InvitationRole::responder,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(aa.consume(s.invitation,100)&&ab.consume(s.invitation,70000));
        CHECK(aa.current()&&ab.current());
        CHECK(aa.cancel());
        CHECK(!aa.current()&&ab.current());
        CHECK(ab.cancel());
        ++groups;
    }
    {
        SignedV2 s;
        Storage bs,rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a=boot.context();
        s.sign();
        {
            IndependentRoleInvitationAuthority a(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
            CHECK(a.consume(s.invitation,100));
            CHECK(!a.consume(s.invitation,101));
        }
        IndependentRoleInvitationAuthority reconstructed(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(!reconstructed.consume(s.invitation,100));
        ++groups;
    }
    {
        SignedV2 s;
        Storage bs,rs;
        {
            InvitationBootAuthority old(bs);
            CHECK(old.start());
            s.f.boot_a=old.context();
            s.sign();
            IndependentRoleInvitationAuthority a(rs,old,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
            CHECK(a.consume(s.invitation,100));
            CHECK(a.cancel());
        }
        InvitationBootAuthority fresh(bs);
        CHECK(fresh.start());
        IndependentRoleInvitationAuthority old_attempt(rs,fresh,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(!old_attempt.consume(s.invitation,100));
        InvitationBootAuthority newer(bs);
        CHECK(newer.start());
        s.f.boot_a=newer.context();
        s.f.nonce[0]++;
        s.sign();
        IndependentRoleInvitationAuthority new_attempt(rs,newer,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(new_attempt.consume(s.invitation,100));
        CHECK(new_attempt.current()&&new_attempt.cancel());
        ++groups;
    }
    for(auto fault:faults) {
        SignedV2 s;
        Storage bs,rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a=boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        rs.arm(fault);
        CHECK(!a.consume(s.invitation,100));
        CHECK(a.failed()&&!a.current());
        rs.clear();
        CHECK(!a.consume(s.invitation,100));
        ++groups;
    }
    for(auto fault: {
        Fault::read_error,Fault::short_read,Fault::corrupt_read
    }) {
        SignedV2 s;
        Storage bs,rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a=boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(a.consume(s.invitation,100));
        rs.arm(fault);
        CHECK(!a.cancel());
        CHECK(!a.current());
        ++groups;
    }
    for(unsigned mode=0; mode<3; ++mode) {
        SignedV2 s;
        Storage bs;
        HookStorage rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a=boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        bool fired=false;
        rs.hook=[&] {
            if(!fired) {
                fired=true;
                if(mode==0)CHECK(!a.consume(s.invitation,100));
                if(mode==1)CHECK(!a.cancel());
                if(mode==2)CHECK(!a.current());
            }
        };
        CHECK(!a.consume(s.invitation,100));
        rs.hook=[] {
        };
        CHECK(fired&&a.failed()&&!a.current());
        ++groups;
    }
    {
        SignedV2 s;
        Storage bs;
        HookStorage rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a=boot.context();
        s.sign();
        const auto original=s.invitation;
        bool fired=false;
        rs.hook=[&] {
            if(!fired) {
                fired=true;
                s.invitation.payload.fill(0);
                s.invitation.signature.fill(0);
            }
        };
        IndependentRoleInvitationAuthority a(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(a.consume(s.invitation,100));
        rs.hook=[] {
        };
        CHECK(fired&&a.current());
        IndependentInvitationGate g;
        CHECK(g.open(original,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b,boot.context(),100));
        CHECK(a.cancel());
        ++groups;
    }
    // A v1 durable role record cannot be mistaken for the v2 authority schema.
    {
        SignedV2 s;
        Storage bs,rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        SignedInvitation old;
        old.fields.boot_context=boot.context();
        old.sign();
        RoleInvitationAuthority a(rs,boot,InvitationRole::initiator,old.fields.signer,old.fields.peer_a,old.fields.peer_b);
        CHECK(a.consume(old.invitation,100));
        CHECK(a.cancel());
        s.f.boot_a=boot.context();
        s.sign();
        IndependentRoleInvitationAuthority b(rs,boot,InvitationRole::initiator,s.f.signer,s.f.peer_a,s.f.peer_b);
        CHECK(!b.consume(s.invitation,100));
        ++groups;
    }

    for (auto fault : {Fault::write_after, Fault::sync_after}) {
        SignedV2 s;
        Storage bs, rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a = boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs, boot, InvitationRole::initiator,
                                             s.f.signer, s.f.peer_a, s.f.peer_b);
        rs.arm(fault, 2);
        CHECK(!a.consume(s.invitation, 100));
        CHECK(a.failed());
        rs.clear();
        IndependentRoleInvitationAuthority reconstructed(rs, boot, InvitationRole::initiator,
                                                         s.f.signer, s.f.peer_a, s.f.peer_b);
        CHECK(!reconstructed.consume(s.invitation, 100));
        ++groups;
    }
    {
        SignedV2 s;
        Storage bs;
        HookStorage rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a = boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs, boot, InvitationRole::initiator,
                                             s.f.signer, s.f.peer_a, s.f.peer_b);
        bool fired = false;
        rs.hook = [&] {
            // Trigger after a write has happened, rather than at the first read.
            if (!fired && rs.inner.count('w') > 0) {
                fired = true;
                CHECK(!a.cancel());
            }
        };
        CHECK(!a.consume(s.invitation, 100));
        rs.hook = [] {};
        CHECK(fired && a.failed() && !a.current());
        IndependentRoleInvitationAuthority reconstructed(rs, boot, InvitationRole::initiator,
                                                         s.f.signer, s.f.peer_a, s.f.peer_b);
        CHECK(!reconstructed.consume(s.invitation, 100));
        ++groups;
    }

    {
        SignedV2 s;
        Storage bs, rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a = boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs, boot, InvitationRole::initiator,
                                             s.f.signer, s.f.peer_a, s.f.peer_b);
        // Reads 1 and 2 inspect the prior slots; read 3 verifies the committed slot.
        rs.arm(Fault::read_error, 3);
        CHECK(!a.consume(s.invitation, 100));
        CHECK(rs.count('w') == 2 && a.failed());
        rs.clear();
        IndependentRoleInvitationAuthority reconstructed(rs, boot, InvitationRole::initiator,
                                                         s.f.signer, s.f.peer_a, s.f.peer_b);
        CHECK(!reconstructed.consume(s.invitation, 100));
        ++groups;
    }

    {
        SignedV2 s;
        Storage bs;
        HookStorage rs;
        InvitationBootAuthority boot(bs);
        CHECK(boot.start());
        s.f.boot_a = boot.context();
        s.sign();
        IndependentRoleInvitationAuthority a(rs, boot, InvitationRole::initiator,
                                             s.f.signer, s.f.peer_a, s.f.peer_b);
        CHECK(a.consume(s.invitation, 100));
        bool fired = false;
        rs.hook = [&] {
            if (!fired) { fired = true; CHECK(!a.cancel()); }
        };
        CHECK(!a.current());
        rs.hook = [] {};
        CHECK(fired && a.failed() && !a.current());
        ++groups;
    }
    return groups;
}
int main() {
    unsigned groups=gate_tests()+noise_tests()+authority_tests();
    std::cout<<"PASS "<<groups<<" independent invitation groups\n";
}
