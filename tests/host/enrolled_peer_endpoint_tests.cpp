#include <type_traits>
#include "security_peer_traffic_fixture.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
using namespace peer_traffic_test;
static_assert(!std::is_copy_constructible_v<EnrolledPeerEndpoint>);

namespace {
struct EnrolledPeer {
    CallbackStorage boot, role, tx, rx, activation;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    EnrolledPeer(CallbackStorage& membership, InvitationRole role_value,
                 const InvitationKey& signer, unsigned offset, std::uint64_t group = 17)
        : endpoint(random, boot, role, tx, rx, activation, membership,
                   source, role_value, signer, group) {
        std::array<unsigned char,64> bytes{};
        for (unsigned i=0; i<bytes.size(); ++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));
        random.set_state(security::EntropyState::ready);
        if (role_value == InvitationRole::responder) source.value={{5,8},70000};
    }
};
struct EnrolledPair {
    SignedIndependentInvitation invitation;
    EnrolledPeer a, b;
    EnrolledPair(CallbackStorage& ma, CallbackStorage& mb,
                 const IndependentInvitation* prior=nullptr, unsigned offset=0,
                 std::uint32_t epoch=1)
        : a(ma,InvitationRole::initiator,invitation.fields.signer,offset),
          b(mb,InvitationRole::responder,invitation.fields.signer,offset+80) {
        CHECK(a.endpoint.initialize(prior)); CHECK(b.endpoint.initialize(prior));
        CHECK(!a.endpoint.ready() && !b.endpoint.ready());
        CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
        invitation.fields.epoch=epoch;
        invitation.fields.nonce[0]=static_cast<unsigned char>(epoch);
        invitation.fields.peer_a=a.endpoint.public_identity();
        invitation.fields.peer_b=b.endpoint.public_identity();
        invitation.fields.boot_a=a.endpoint.boot_context();
        invitation.fields.boot_b=b.endpoint.boot_context();
        invitation.sign();
    }
    void begin() {
        CHECK(a.endpoint.begin(invitation.invitation));
        CHECK(b.endpoint.begin(invitation.invitation));
    }
    static void handshake_frame(EnrolledPeer& from,EnrolledPeer& to) {
        HandshakeFrame frame{}; CHECK(from.endpoint.next_handshake(frame));
        CHECK(to.endpoint.receive_handshake(frame));
    }
    void handshake() {
        begin(); handshake_frame(a,b); handshake_frame(b,a); handshake_frame(a,b);
    }
    void confirm() {
        const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
        const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
    }
    static void control(EnrolledPeer& from,EnrolledPeer& to) {
        EvaluationRecord record{}; CHECK(from.endpoint.next_control(record));
        CHECK(to.endpoint.receive_control(record));
    }
    void almost_ready() {
        handshake(); confirm(); control(a,b); control(b,a); control(a,b);
    }
    void activate() {
        almost_ready(); control(b,a);
        CHECK(a.endpoint.ready() && b.endpoint.ready());
    }
};
}

int main() {
    unsigned groups=0;
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb);
        p.handshake(); CHECK(ma.mutations()==0 && mb.mutations()==0);
        p.confirm(); CHECK(ma.mutations()==0 && mb.mutations()==0);
        EnrolledPair::control(p.a,p.b); EnrolledPair::control(p.b,p.a);
        CHECK(ma.mutations()==0 && mb.mutations()==0);
        EnrolledPair::control(p.a,p.b); EnrolledPair::control(p.b,p.a);
        CHECK(p.a.endpoint.ready() && p.b.endpoint.ready());
        CHECK(ma.mutations()>0 && mb.mutations()>0);
        for (std::uint8_t code=1;code<=8;++code) {
            EvaluationRecord wire{}; std::uint8_t got=0;
            CHECK(p.a.endpoint.send_status(code,wire));
            CHECK(p.b.endpoint.receive_status(wire,got) && got==code);
            CHECK(p.b.endpoint.send_status(code,wire));
            CHECK(p.a.endpoint.receive_status(wire,got) && got==code);
        }
        CHECK(p.a.endpoint.cancel() && p.b.endpoint.cancel());
        CHECK(p.a.endpoint.secrets_cleared() && p.b.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned invalid=0;invalid<3;++invalid) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb);
        if (invalid==0) p.invitation.fields.group++;
        if (invalid==1) {
            std::array<unsigned char,32> seed{}; seed.fill(91);
            CHECK(crypto_sign_seed_keypair(p.invitation.fields.signer.data(),
                p.invitation.secret.data(),seed.data())==0);
        }
        p.invitation.sign();
        if (invalid==2) p.invitation.invitation.signature[0]^=1;
        CHECK(!p.a.endpoint.begin(p.invitation.invitation));
        CHECK(ma.mutations()==0 && p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        CHECK(p.a.endpoint.cancel() && p.b.endpoint.cancel());
        EnrolledPeer reconstructed(ma,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(reconstructed.endpoint.initialize(&p.invitation.invitation));
        CHECK(!reconstructed.endpoint.ready());
        EvaluationRecord out{}; out.counter=77; const auto before=out;
        CHECK(!reconstructed.endpoint.send_status(1,out));
        CHECK(same_record(before,out) && reconstructed.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned invalid=0;invalid<3;++invalid) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        CHECK(p.a.endpoint.cancel() && p.b.endpoint.cancel());
        auto retained=p.invitation.invitation;
        if (invalid==1) retained.signature[0]^=1;
        if (invalid==2) {
            p.invitation.fields.nonce[0]++; p.invitation.sign(); retained=p.invitation.invitation;
        }
        EnrolledPeer reconstructed(ma,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(!reconstructed.endpoint.initialize(invalid==0?nullptr:&retained));
        CHECK(!reconstructed.endpoint.ready() && reconstructed.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair first(ma,mb); first.activate();
        CHECK(first.a.endpoint.cancel() && first.b.endpoint.cancel());
        EnrolledPair next(ma,mb,&first.invitation.invitation,150,2);
        CHECK(next.a.endpoint.public_identity()!=first.invitation.fields.peer_a);
        next.activate();
        EvaluationRecord wire{}; std::uint8_t got=0;
        CHECK(next.a.endpoint.send_status(8,wire)); CHECK(next.b.endpoint.receive_status(wire,got) && got==8);
        CHECK(next.a.endpoint.cancel() && next.b.endpoint.cancel());
        EnrolledPeer old(ma,InvitationRole::initiator,next.invitation.fields.signer,30);
        CHECK(!old.endpoint.initialize(&first.invitation.invitation)); ++groups;
    }
    for (unsigned invalid=0;invalid<2;++invalid) {
        CallbackStorage ma,mb; EnrolledPair first(ma,mb); first.activate();
        CHECK(first.a.endpoint.cancel() && first.b.endpoint.cancel());
        EnrolledPair next(ma,mb,&first.invitation.invitation,invalid==0?150:0,invalid==0?1:2);
        CHECK(!next.a.endpoint.begin(next.invitation.invitation));
        CHECK(next.a.endpoint.secrets_cleared()); ++groups;
    }
    for (bool reset : {false,true}) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        CHECK(reset?p.a.endpoint.prepare_reset():p.a.endpoint.revoke());
        CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared());
        EnrolledPeer next(ma,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(!next.endpoint.initialize(&p.invitation.invitation)); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.handshake();
        CHECK(p.a.endpoint.cancel()); CHECK(ma.mutations()==0);
        CHECK(!p.a.endpoint.begin(p.invitation.invitation));
        EnrolledPeer fresh(ma,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(fresh.endpoint.initialize()); ++groups;
    }
    for (auto fault : {Fault::read_error,Fault::short_read,Fault::corrupt_read,
                       Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after}) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.almost_ready();
        ma.arm(fault); EvaluationRecord final{}; CHECK(p.b.endpoint.next_control(final));
        CHECK(!p.a.endpoint.receive_control(final));
        CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned failure=0;failure<3;++failure) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.almost_ready();
        bool fired=false;
        ma.callback=[&](char op) {
            if (op!='w' || fired) return;
            fired=true;
            if (failure==0) p.a.source.value.now_ms=1000;
            if (failure==1) p.a.source.value.context.session_nonce++;
            if (failure==2) CHECK(!p.a.endpoint.cancel());
        };
        EvaluationRecord final{}; CHECK(p.b.endpoint.next_control(final));
        CHECK(!p.a.endpoint.receive_control(final));
        CHECK(fired && !p.a.endpoint.ready() && p.a.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned mismatch=0;mismatch<3;++mismatch) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        CHECK(p.a.endpoint.cancel());
        auto signer=p.invitation.fields.signer;
        if (mismatch==2) signer[0]^=1;
        EnrolledPeer next(ma,mismatch==0?InvitationRole::responder:InvitationRole::initiator,
                          signer,150,mismatch==1?18:17);
        CHECK(!next.endpoint.initialize(&p.invitation.invitation));
        CHECK(!next.endpoint.ready() && next.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        ma.inner.memory.corrupt_byte(domain,0,16,0x55);
        EvaluationRecord output{}; output.counter=73; output.ciphertext.fill(0xA5);
        const auto before=output; const auto writes=p.a.tx.mutations();
        CHECK(!p.a.endpoint.send_status(1,output));
        CHECK(same_record(output,before) && p.a.tx.mutations()==writes);
        CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        EvaluationRecord wire{}; CHECK(p.a.endpoint.send_status(3,wire));
        auto invalid=wire; invalid.ciphertext[0]^=1; std::uint8_t got=99;
        CHECK(!p.b.endpoint.receive_status(invalid,got) && got==99);
        CHECK(p.b.endpoint.ready());
        CHECK(p.b.endpoint.receive_status(wire,got) && got==3);
        got=99; CHECK(!p.b.endpoint.receive_status(wire,got) && got==99);
        CHECK(p.b.endpoint.ready()); ++groups;
    }
    {
        SignedIndependentInvitation invitation;
        CallbackStorage boot,role,tx,rx,activation;
        security::test_support::FakeSecureRandomSource random;
        Source source;
        EnrolledPeerEndpoint endpoint(random,boot,role,tx,rx,activation,activation,
            source,InvitationRole::initiator,invitation.fields.signer,17);
        CHECK(!endpoint.initialize() && endpoint.secrets_cleared());
        CHECK(activation.mutations()==0); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        const auto prior_tx=p.a.tx.mutations();
        bool post_seal_membership_observed=false, changed=false;
        // Trigger by observable ordering, not an internal sample/read count:
        // sealing has durably consumed a TX counter, then membership is checked,
        // then the external authority callback runs during final freshness polling.
        ma.callback=[&](char op) {
            if (op=='r' && p.a.tx.mutations()>prior_tx)
                post_seal_membership_observed=true;
        };
        p.a.source.callback=[&] {
            if (!post_seal_membership_observed || changed) return;
            changed=true;
            ma.inner.memory.corrupt_byte(domain,0,16,0x55);
        };
        EvaluationRecord output{}; output.counter=73; output.ciphertext.fill(0xA5);
        const auto before=output;
        CHECK(!p.a.endpoint.send_status(1,output));
        CHECK(changed && same_record(output,before));
        CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb; EnrolledPair prior(ma,mb); prior.activate();
        CHECK(prior.a.endpoint.cancel() && prior.b.endpoint.cancel());
        EnrolledPair next(ma,mb,&prior.invitation.invitation,80,2);
        CHECK(next.a.endpoint.public_identity()==prior.invitation.fields.peer_b);
        next.invitation.fields.peer_b=prior.invitation.fields.peer_a;
        next.invitation.sign();
        const auto mutations=ma.mutations();
        CHECK(!next.a.endpoint.begin(next.invitation.invitation));
        CHECK(ma.mutations()==mutations && next.a.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned failure=0;failure<3;++failure) {
        CallbackStorage ma,mb; EnrolledPair p(ma,mb); p.activate();
        const auto prior_tx=p.a.tx.mutations();
        bool post_seal_membership_observed=false, downstream_polled=false, changed=false;
        p.a.source.callback=[&] {
            if (post_seal_membership_observed) downstream_polled=true;
        };
        ma.callback=[&](char op) {
            if (op!='r' || p.a.tx.mutations()<=prior_tx) return;
            post_seal_membership_observed=true;
            if (!downstream_polled || changed) return;
            changed=true;
            if (failure==0) p.a.source.value.now_ms=1000;
            if (failure==1) p.a.source.value.context.session_nonce++;
            if (failure==2) p.a.random.set_state(security::EntropyState::not_ready);
        };
        EvaluationRecord output{}; output.counter=73; output.ciphertext.fill(0xA5);
        const auto before=output;
        CHECK(!p.a.endpoint.send_status(1,output));
        CHECK(changed && same_record(output,before));
        CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared()); ++groups;
    }
    std::cout << "PASS " << groups << " enrolled peer endpoint groups\n";
}
