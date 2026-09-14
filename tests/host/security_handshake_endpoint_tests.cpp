#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/evaluation_handshake_endpoint.hpp"
#include "fake_secure_random.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<EvaluationHandshakeEndpoint>);
static_assert(!std::is_move_constructible_v<EvaluationHandshakeEndpoint>);
struct Source final : ConfirmationAuthority {
    ConfirmationSample value{{1,1},100};
    std::function<void()> callback=[]{};
    ConfirmationSample sample() override { callback(); return value; }
};
struct Peer {
    Storage boot, role, tx, rx;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EvaluationHandshakeEndpoint endpoint;
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
    SignedInvitation signed_invite;
    Peer a{InvitationRole::initiator,signed_invite.fields.signer,0};
    Peer b{InvitationRole::responder,signed_invite.fields.signer,80};
    Pair(){
        b.source.value.context={5,8};
        CHECK(a.endpoint.prepare_identity()); CHECK(b.endpoint.prepare_identity());
        CHECK(&a.boot!=&b.boot && &a.role!=&b.role && &a.tx!=&b.tx && &a.rx!=&b.rx);
        signed_invite.fields.peer_a=a.endpoint.public_identity();
        signed_invite.fields.peer_b=b.endpoint.public_identity();
        CHECK(a.endpoint.boot_context()==b.endpoint.boot_context());
        signed_invite.fields.boot_context=a.endpoint.boot_context();signed_invite.sign();
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
static const ConfirmationOffer& offer(Peer& p){auto* v=p.endpoint.offer();CHECK(v);return *v;}
static void refused(Peer& p){CHECK(p.endpoint.state()==EndpointState::refused);CHECK(!p.endpoint.offer());CHECK(p.endpoint.secrets_cleared());}
static bool blank(const HandshakeFrame& f){
    if(f.version||f.step||f.payload_bytes)return false;
    for(auto b:f.payload)if(b)return false;
    return true;
}
int main(){
 unsigned groups=0;
 {
    Pair p;p.handshake();CHECK(p.a.endpoint.state()==EndpointState::review && p.b.endpoint.state()==EndpointState::review);
    const auto& a=offer(p.a);const auto& b=offer(p.b);
    CHECK(a.transcript()==b.transcript() && a.peer()==p.b.endpoint.public_identity() && b.peer()==p.a.endpoint.public_identity());
    CHECK(a.context()!=b.context());CHECK(p.a.traffic_mutations()==0 && p.b.traffic_mutations()==0);
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
    if(fault==1)p.b.source.value.now_ms=1000;
    if(fault==2)p.b.source.value.now_ms=99;
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
    Pair p;p.handshake();const ConfirmationOffer copy(offer(p.a));
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
    Source source;EvaluationHandshakeEndpoint replacement(random,p.a.boot,p.a.role,p.a.tx,p.a.rx,source,InvitationRole::initiator,p.signed_invite.fields.signer);
    CHECK(replacement.prepare_identity());
    if(refresh){
        // A new durable boot plus a freshly signed invitation is a new attempt,
        // not resurrection of the old invitation or automatic confirmation.
        p.signed_invite.fields.boot_context=replacement.boot_context();
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
    SignedInvitation s;Peer a(InvitationRole::initiator,s.fields.signer,0),b(InvitationRole::responder,s.fields.signer,80);
    // Only B has already booted once. Do not share A's boot authority to hide it.
    {InvitationBootAuthority prior(b.boot);CHECK(prior.start());}
    CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
    CHECK(a.endpoint.boot_context()!=b.endpoint.boot_context());
    s.fields.peer_a=a.endpoint.public_identity();s.fields.peer_b=b.endpoint.public_identity();s.fields.boot_context=a.endpoint.boot_context();s.sign();
    CHECK(a.endpoint.begin(s.invitation,b.endpoint.public_identity()));CHECK(!b.endpoint.begin(s.invitation,a.endpoint.public_identity()));refused(b);++groups;
 }
 {
    Storage store;security::test_support::FakeSecureRandomSource random;Source source;SignedInvitation s;
    EvaluationHandshakeEndpoint e(random,store,store,store,store,source,InvitationRole::initiator,s.fields.signer);
    CHECK(!e.prepare_identity());CHECK(store.mutations()==0 && e.secrets_cleared());++groups;
 }
 {
    SignedInvitation s;Peer p(InvitationRole::initiator,s.fields.signer,0);
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
 std::cout<<"PASS "<<groups<<" independent handshake endpoint groups\n";
}
