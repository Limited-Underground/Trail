#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/independent_handshake_endpoint.hpp"
#include "fake_secure_random.hpp"
#include "security_independent_session_fixture.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<IndependentHandshakeEndpoint>);
static_assert(!std::is_move_constructible_v<IndependentHandshakeEndpoint>);
struct SignedIndependentInvitation {
    IndependentInvitationFields fields{};
    IndependentInvitation invitation{};
    std::array<unsigned char,64> secret{};
    SignedIndependentInvitation() {
        SignedInvitation old;
        fields.group=old.fields.group; fields.epoch=old.fields.epoch;
        fields.signer=old.fields.signer; secret=old.secret;
        fields.peer_a=old.fields.peer_a; fields.peer_b=old.fields.peer_b;
        fields.nonce.fill(3); fields.boot_a.fill(4); fields.boot_b.fill(9);
        fields.issued_a_ms=100; fields.window_a_ms=900;
        fields.issued_b_ms=70000; fields.window_b_ms=300;
    }
    ~SignedIndependentInvitation(){sodium_memzero(secret.data(),secret.size());}
    void sign(){CHECK(encode_independent_invitation(fields,invitation));
        CHECK(crypto_sign_detached(invitation.signature.data(),nullptr,invitation.payload.data(),invitation.payload.size(),secret.data())==0);}
};
struct Source final : ConfirmationAuthority {
    ConfirmationSample value{{1,1},100};
    std::function<void()> callback=[]{};
    ConfirmationSample sample() override { callback(); return value; }
};
struct Peer {
    Storage boot, role, tx, rx;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    IndependentHandshakeEndpoint endpoint;
    Peer(InvitationRole r,const InvitationKey& signer,unsigned offset)
      : endpoint(random,boot,role,tx,rx,source,r,signer) {
        std::array<unsigned char,64> bytes{};
        for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));
        random.set_state(security::EntropyState::ready);
    }
    unsigned traffic_mutations()const{return tx.mutations()+rx.mutations();}
};
struct Pair {
    SignedIndependentInvitation signed_invite;
    Peer a{InvitationRole::initiator,signed_invite.fields.signer,0};
    Peer b{InvitationRole::responder,signed_invite.fields.signer,80};
    Pair(){
        b.source.value.context={5,8};
        b.source.value.now_ms=70000;
        {InvitationBootAuthority previous(b.boot); CHECK(previous.start());}
        CHECK(a.endpoint.prepare_identity()); CHECK(b.endpoint.prepare_identity());
        CHECK(&a.boot!=&b.boot && &a.role!=&b.role && &a.tx!=&b.tx && &a.rx!=&b.rx);
        signed_invite.fields.peer_a=a.endpoint.public_identity();
        signed_invite.fields.peer_b=b.endpoint.public_identity();
        CHECK(a.endpoint.boot_context()!=b.endpoint.boot_context());
        signed_invite.fields.boot_a=a.endpoint.boot_context();
        signed_invite.fields.boot_b=b.endpoint.boot_context();signed_invite.sign();
    }
    void begin(){
        CHECK(a.endpoint.begin(signed_invite.invitation,b.endpoint.public_identity()));
        CHECK(b.endpoint.begin(signed_invite.invitation,a.endpoint.public_identity()));
    }
    // A bounded in-memory transport carries only value frames between endpoints.
    // Neither endpoint can call the other endpoint or access its state/storage.
    HandshakeFrame deliver(Peer& from,Peer& to,unsigned step){
        HandshakeFrame frame{};CHECK(from.endpoint.next_send(frame));
        CHECK(frame.version==1 && frame.step==step && frame.payload_bytes>0 && frame.payload_bytes<=128);
        CHECK(to.endpoint.receive(frame));return frame;
    }
    void handshake(){begin();deliver(a,b,1);deliver(b,a,2);deliver(a,b,3);}
};
static const IndependentConfirmationOffer& offer(Peer& p){auto* v=p.endpoint.offer();CHECK(v);return *v;}
static void refused(Peer& p){CHECK(p.endpoint.state()==EndpointState::refused);CHECK(!p.endpoint.offer());CHECK(p.endpoint.secrets_cleared());}
static bool blank(const HandshakeFrame& f){
    if(f.version||f.step||f.payload_bytes)return false;
    for(auto b:f.payload)if(b)return false;
    return true;
}
int main(){
 unsigned groups=opentrail_independent_session_test::independent_session_traffic_tests();
 {
    Pair p;p.handshake();CHECK(p.a.endpoint.state()==EndpointState::review && p.b.endpoint.state()==EndpointState::review);
    const auto& a=offer(p.a);const auto& b=offer(p.b);
    CHECK(a.transcript()==b.transcript() && a.peer()==p.b.endpoint.public_identity() && b.peer()==p.a.endpoint.public_identity());
    CHECK(a.context()!=b.context());CHECK(a.deadline_ms()==1000 && b.deadline_ms()==70300);CHECK(p.a.traffic_mutations()==0 && p.b.traffic_mutations()==0);
    CHECK(p.a.endpoint.confirm(a));CHECK(p.a.endpoint.state()==EndpointState::local_confirmed);
    CHECK(p.b.endpoint.state()==EndpointState::review && p.b.traffic_mutations()==0);
    CHECK(p.b.endpoint.confirm(b));CHECK(p.b.endpoint.state()==EndpointState::local_confirmed);
    CHECK(p.a.traffic_mutations()>0 && p.b.traffic_mutations()>0);
    CHECK(p.a.endpoint.close() && p.b.endpoint.close());CHECK(p.a.endpoint.secrets_cleared() && p.b.endpoint.secrets_cleared());++groups;
 }
 for(unsigned fault=0;fault<6;++fault){
    Pair p;p.begin();HandshakeFrame f{};CHECK(p.a.endpoint.next_send(f));
    if(fault==0)f.version=2;
    if(fault==1)f.step=2;
    if(fault==2)f.payload_bytes=129;
    if(fault==3)f.payload_bytes=0;
    if(fault==4)f.payload[0]^=1;
    if(fault==5)f.payload.back()=1;
    CHECK(!p.b.endpoint.receive(f));refused(p.b);CHECK(p.b.traffic_mutations()==0);++groups;
 }
 {
    Pair p;p.begin();auto first=p.deliver(p.a,p.b,1);CHECK(!p.b.endpoint.receive(first));refused(p.b);++groups;
 }
 {
    Pair p;p.begin();HandshakeFrame out{};out.payload.fill(99);out.step=9;
    CHECK(!p.b.endpoint.next_send(out));CHECK(blank(out));refused(p.b);++groups;
 }
 {
    Pair p;p.begin();HandshakeFrame out{};CHECK(p.a.endpoint.next_send(out));out.payload.fill(88);
    CHECK(!p.a.endpoint.next_send(out));CHECK(blank(out));refused(p.a);++groups;
 }
 {
    Pair p;p.begin();unsigned samples=0;
    // Expire at the endpoint's final sample, after the owner's write completed.
    p.a.source.callback=[&]{if(++samples==4)p.a.source.value.now_ms=1000;};
    HandshakeFrame out{};out.payload.fill(77);CHECK(!p.a.endpoint.next_send(out));
    p.a.source.callback=[]{};CHECK(samples==4 && blank(out));refused(p.a);++groups;
 }
 for(unsigned fault=0;fault<4;++fault){
    Pair p;p.begin();HandshakeFrame f{};CHECK(p.a.endpoint.next_send(f));
    if(fault==0)p.b.source.value.context={};
    if(fault==1)p.b.source.value.now_ms=70300;
    if(fault==2)p.b.source.value.now_ms=69999;
    if(fault==3)p.b.source.value.context.session_nonce++;
    CHECK(!p.b.endpoint.receive(f));refused(p.b);++groups;
 }
 for(unsigned fault=0;fault<3;++fault){
    Pair p;
    auto peer=p.b.endpoint.public_identity();
    if(fault==0)p.signed_invite.invitation.signature[0]^=1;
    if(fault==1)peer[0]^=1;
    if(fault==2)p.signed_invite.invitation.payload[4]^=1;
    CHECK(!p.a.endpoint.begin(p.signed_invite.invitation,peer));refused(p.a);CHECK(p.a.traffic_mutations()==0);++groups;
 }
 {
    Pair p;p.handshake();const IndependentConfirmationOffer copy(offer(p.a));
    CHECK(!p.a.endpoint.confirm(copy));refused(p.a);CHECK(p.a.traffic_mutations()==0);++groups;
 }
 {
    Pair p;p.handshake();CHECK(!p.a.endpoint.confirm(offer(p.b)));refused(p.a);++groups;
 }
 for(unsigned refresh=0;refresh<2;++refresh){
    Pair p;p.handshake();CHECK(p.a.endpoint.cancel(offer(p.a)));CHECK(p.a.endpoint.secrets_cleared());
    CHECK(!p.a.endpoint.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    // Reconstruct with retained local ledgers; an old invitation cannot revive.
    security::test_support::FakeSecureRandomSource random;std::array<unsigned char,64> bytes{};
    for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i);
    CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);
    Source source;IndependentHandshakeEndpoint replacement(random,p.a.boot,p.a.role,p.a.tx,p.a.rx,source,InvitationRole::initiator,p.signed_invite.fields.signer);
    CHECK(replacement.prepare_identity());
    if(refresh){
        // A new durable boot plus a freshly signed invitation is a new attempt,
        // not resurrection of the old invitation or automatic confirmation.
        p.signed_invite.fields.boot_a=replacement.boot_context();
        p.signed_invite.fields.peer_a=replacement.public_identity();
        p.signed_invite.fields.nonce[0]++;p.signed_invite.sign();
    }
    if(refresh){
        CHECK(replacement.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
        CHECK(replacement.state()==EndpointState::handshake);CHECK(replacement.close());
    }else CHECK(!replacement.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    CHECK(replacement.secrets_cleared());CHECK(p.a.traffic_mutations()==0);++groups;
 }
 {
    Pair p;p.begin();CHECK(p.a.endpoint.close());HandshakeFrame f{};f.payload.fill(1);
    CHECK(!p.a.endpoint.next_send(f) && blank(f));CHECK(p.a.endpoint.secrets_cleared());++groups;
 }
 {
    Pair p;p.handshake();p.a.source.value.context={};CHECK(!p.a.endpoint.offer());refused(p.a);++groups;
 }
 {
    Pair p;bool called=false;p.a.source.callback=[&]{if(!called){called=true;CHECK(!p.a.endpoint.close());}};
    CHECK(!p.a.endpoint.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    p.a.source.callback=[]{};CHECK(called);refused(p.a);++groups;
 }
 {
    SignedIndependentInvitation s;Peer a(InvitationRole::initiator,s.fields.signer,0),b(InvitationRole::responder,s.fields.signer,80);
    // Only B has already booted once; a signer binding A's token to B is refused.
    b.source.value.now_ms=70000;
    {InvitationBootAuthority prior(b.boot);CHECK(prior.start());}
    CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
    CHECK(a.endpoint.boot_context()!=b.endpoint.boot_context());
    s.fields.peer_a=a.endpoint.public_identity();s.fields.peer_b=b.endpoint.public_identity();s.fields.boot_a=a.endpoint.boot_context();s.fields.boot_b=a.endpoint.boot_context();s.sign();
    CHECK(a.endpoint.begin(s.invitation,b.endpoint.public_identity()));CHECK(!b.endpoint.begin(s.invitation,a.endpoint.public_identity()));refused(b);++groups;
 }
 {
    Storage store;security::test_support::FakeSecureRandomSource random;Source source;SignedIndependentInvitation s;
    IndependentHandshakeEndpoint e(random,store,store,store,store,source,InvitationRole::initiator,s.fields.signer);
    CHECK(!e.prepare_identity());CHECK(store.mutations()==0 && e.secrets_cleared());++groups;
 }
 {
    SignedIndependentInvitation s;Peer p(InvitationRole::initiator,s.fields.signer,0);
    CHECK(p.endpoint.prepare_identity());CHECK(p.endpoint.close());CHECK(p.endpoint.secrets_cleared());++groups;
 }
 for(auto fault:faults){
    Pair p;p.a.role.arm(fault);
    CHECK(!p.a.endpoint.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    refused(p.a);CHECK(p.a.traffic_mutations()==0);++groups;
 }
 {
    Pair p;p.handshake();CHECK(p.a.endpoint.confirm(offer(p.a)));
    p.a.rx.arm(Fault::write_before);
    CHECK(!p.a.endpoint.close());CHECK(p.a.endpoint.secrets_cleared());++groups;
 }
 {
    Pair p;p.handshake();
    CHECK(p.b.endpoint.confirm(offer(p.b)));
    CHECK(p.a.endpoint.state()==EndpointState::review && p.a.traffic_mutations()==0);
    CHECK(p.a.endpoint.confirm(offer(p.a)));
    CHECK(p.a.endpoint.close() && p.b.endpoint.close());++groups;
 }
 {
    Pair p;p.handshake();p.a.source.value.now_ms=1000;
    CHECK(!p.a.endpoint.offer());refused(p.a);
    CHECK(p.b.endpoint.state()==EndpointState::review);
    CHECK(p.b.endpoint.confirm(offer(p.b)));CHECK(p.b.endpoint.close());++groups;
 }
 for(bool b:{false,true}){
    Pair p;
    if(b)p.signed_invite.fields.boot_b=p.a.endpoint.boot_context();
    else p.signed_invite.fields.boot_a=p.b.endpoint.boot_context();
    p.signed_invite.sign();auto& peer=b?p.b:p.a;auto& other=b?p.a:p.b;
    CHECK(!peer.endpoint.begin(p.signed_invite.invitation,other.endpoint.public_identity()));
    refused(peer);CHECK(peer.traffic_mutations()==0);++groups;
 }
 {
    Pair p;
    CHECK(p.a.endpoint.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    p.signed_invite.fields.window_a_ms--;
    p.signed_invite.sign();
    CHECK(p.b.endpoint.begin(p.signed_invite.invitation,p.a.endpoint.public_identity()));
    HandshakeFrame frame{};CHECK(p.a.endpoint.next_send(frame));
    CHECK(!p.b.endpoint.receive(frame));refused(p.b);++groups;
 }
 {
    Pair p;p.begin();p.deliver(p.a,p.b,1);unsigned samples=0;
    // B expires after its write completes, using B's signed clock domain.
    p.b.source.callback=[&]{if(++samples==4)p.b.source.value.now_ms=70300;};
    HandshakeFrame out{};out.payload.fill(77);
    CHECK(!p.b.endpoint.next_send(out));p.b.source.callback=[]{};
    CHECK(samples==4 && blank(out));refused(p.b);++groups;
 }
 {
    Pair p;const auto original=p.signed_invite.invitation;bool changed=false;
    p.a.source.callback=[&]{if(!changed){changed=true;p.signed_invite.invitation={};}};
    CHECK(p.a.endpoint.begin(p.signed_invite.invitation,p.b.endpoint.public_identity()));
    p.a.source.callback=[]{};CHECK(changed);
    CHECK(p.b.endpoint.begin(original,p.a.endpoint.public_identity()));
    p.deliver(p.a,p.b,1);p.deliver(p.b,p.a,2);p.deliver(p.a,p.b,3);
    CHECK(offer(p.a).transcript()==offer(p.b).transcript());
    CHECK(p.a.endpoint.close() && p.b.endpoint.close());++groups;
 }
 {
    Pair p;p.begin();HandshakeFrame frame{};CHECK(p.a.endpoint.next_send(frame));
    bool changed=false;p.b.source.callback=[&]{if(!changed){changed=true;frame={};}};
    CHECK(p.b.endpoint.receive(frame));p.b.source.callback=[]{};CHECK(changed);
    p.deliver(p.b,p.a,2);p.deliver(p.a,p.b,3);
    CHECK(offer(p.a).transcript()==offer(p.b).transcript());
    CHECK(p.a.endpoint.close() && p.b.endpoint.close());++groups;
 }
 for(bool reenter:{false,true}){
    Pair p;p.handshake();const auto& expected=offer(p.b);unsigned samples=0;
    unsigned rx_before=0;
    p.b.source.callback=[&]{if(++samples==4){
        rx_before=p.b.rx.mutations();CHECK(rx_before>0);
        if(reenter)CHECK(!p.b.endpoint.close());else p.b.source.value.now_ms=70300;
    }};
    CHECK(!p.b.endpoint.confirm(expected));p.b.source.callback=[]{};
    CHECK(samples==4 && p.b.rx.mutations()>rx_before);refused(p.b);
    security_eval::EvaluationReplayStore::Context context{};
    const auto& retained=p.b.rx.memory.slot_bytes(domain,0);
    std::memcpy(context.data(),retained.data()+12,context.size());
    const auto mutations=p.b.rx.mutations();security_eval::EvaluationReplayStore reopened(p.b.rx);
    CHECK(reopened.start(context,false)==security_eval::ReplayError::retired);
    CHECK(p.b.rx.mutations()==mutations);++groups;
 }
 std::cout<<"PASS "<<groups<<" independent provisioned endpoint groups\n";
}
