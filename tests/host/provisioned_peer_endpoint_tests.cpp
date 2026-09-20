#include "security_peer_traffic_fixture.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
using namespace peer_traffic_test;

namespace {
// Explicit fresh session backing per fixture instance; retained membership and
// enrollment are supplied separately. This is not same-bank restart rekey.
struct ProvisionedPeer {
    CallbackStorage boot,role,tx,rx,activation;
    EnrollmentEvidenceStore evidence;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    ProvisionedPeer(CallbackStorage& membership,CallbackStorage& backing,
        InvitationRole role_value,const InvitationKey& signer,unsigned offset,std::uint64_t group=17)
        : evidence(backing), endpoint(random,boot,role,tx,rx,activation,membership,
            source,role_value,signer,group,&evidence) {
        std::array<unsigned char,64> bytes{};
        for(unsigned i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));
        random.set_state(security::EntropyState::ready);
        if(role_value==InvitationRole::responder) source.value={{5,8},70000};
    }
};
struct ProvisionedPair {
    SignedIndependentInvitation invitation;
    ProvisionedPeer a,b;
    ProvisionedPair(CallbackStorage& ma,CallbackStorage& mb,CallbackStorage& ea,CallbackStorage& eb,
        unsigned offset=0,std::uint32_t epoch=1)
        : a(ma,ea,InvitationRole::initiator,invitation.fields.signer,offset),
          b(mb,eb,InvitationRole::responder,invitation.fields.signer,offset+80) {
        CHECK(a.endpoint.initialize() && b.endpoint.initialize());
        CHECK(!a.endpoint.ready() && !b.endpoint.ready());
        CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
        invitation.fields.epoch=epoch; invitation.fields.nonce[0]=static_cast<unsigned char>(epoch);
        invitation.fields.peer_a=a.endpoint.public_identity(); invitation.fields.peer_b=b.endpoint.public_identity();
        invitation.fields.boot_a=a.endpoint.boot_context(); invitation.fields.boot_b=b.endpoint.boot_context();
        invitation.sign();
    }
    void begin() { CHECK(a.endpoint.begin(invitation.invitation)); CHECK(b.endpoint.begin(invitation.invitation)); }
    static void frame(ProvisionedPeer& from,ProvisionedPeer& to) {
        HandshakeFrame wire{}; CHECK(from.endpoint.next_handshake(wire)); CHECK(to.endpoint.receive_handshake(wire));
    }
    static void control(ProvisionedPeer& from,ProvisionedPeer& to) {
        EvaluationRecord wire{}; CHECK(from.endpoint.next_control(wire)); CHECK(to.endpoint.receive_control(wire));
    }
    void activate() {
        begin(); frame(a,b); frame(b,a); frame(a,b);
        const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
        const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
        control(a,b); control(b,a); control(a,b); control(b,a);
        CHECK(a.endpoint.ready() && b.endpoint.ready());
    }
};
struct BankBackend final : EvaluationStorageBackend {
    std::array<CallbackStorage,7> stores;
    persistence::StorageReadResult read(EvaluationNamespace n,Domain d,std::size_t slot,
        persistence::MutableStorageByteView out) override { return stores[static_cast<unsigned>(n)].read_slot(d,slot,out); }
    Error erase(EvaluationNamespace n,Domain d,std::size_t slot) override { return stores[static_cast<unsigned>(n)].erase_slot(d,slot); }
    Error write(EvaluationNamespace n,Domain d,std::size_t slot,std::size_t offset,
        persistence::StorageByteView in) override { return stores[static_cast<unsigned>(n)].write_slot(d,slot,offset,in); }
    Error sync(EvaluationNamespace n,Domain d,std::size_t slot) override { return stores[static_cast<unsigned>(n)].sync_slot(d,slot); }
};
struct BankPeer {
    EvaluationStorageBank bank;
    EnrollmentEvidenceStore evidence;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    BankPeer(BankBackend& backend,InvitationRole role,const InvitationKey& signer,unsigned offset)
        : bank(backend), evidence(*bank.get(EvaluationNamespace::enrollment)),
          endpoint(random,bank,evidence,source,role,signer,17) {
        std::array<unsigned char,64> bytes{};
        for(unsigned i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size())); random.set_state(security::EntropyState::ready);
        if(role==InvitationRole::responder) source.value={{5,8},70000};
    }
};
bool same_invitation(const IndependentInvitation& a,const IndependentInvitation& b) {
    return a.payload==b.payload && a.signature==b.signature;
}
void no_resume(CallbackStorage& membership,CallbackStorage& backing,const InvitationKey& signer) {
    ProvisionedPeer fresh(membership,backing,InvitationRole::initiator,signer,150);
    (void)fresh.endpoint.initialize();
    CHECK(!fresh.endpoint.ready());
    EvaluationRecord output{}; output.counter=97; const auto before=output;
    CHECK(!fresh.endpoint.send_status(1,output)); CHECK(same_record(before,output));
    CHECK(fresh.endpoint.secrets_cleared());
}
}
int main() {
    unsigned groups=0;
    {
        CallbackStorage membership,evidence; SignedIndependentInvitation invitation;
        ProvisionedPeer peer(membership,evidence,InvitationRole::initiator,invitation.fields.signer,0);
        CHECK(!peer.endpoint.prepare_identity()); CHECK(peer.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage shared; SignedIndependentInvitation invitation;
        ProvisionedPeer peer(shared,shared,InvitationRole::initiator,invitation.fields.signer,0);
        CHECK(!peer.endpoint.initialize()); CHECK(shared.inner.count('w')==0); ++groups;
    }
    {
        SignedIndependentInvitation invitation; BankBackend ba,bb;
        {
            BankPeer a(ba,InvitationRole::initiator,invitation.fields.signer,0);
            BankPeer b(bb,InvitationRole::responder,invitation.fields.signer,80);
            CHECK(a.endpoint.initialize() && b.endpoint.initialize());
            CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
            invitation.fields.peer_a=a.endpoint.public_identity(); invitation.fields.peer_b=b.endpoint.public_identity();
            invitation.fields.boot_a=a.endpoint.boot_context(); invitation.fields.boot_b=b.endpoint.boot_context(); invitation.sign();
            CHECK(a.endpoint.begin(invitation.invitation) && b.endpoint.begin(invitation.invitation));
            auto frame=[](BankPeer& from,BankPeer& to) {
                HandshakeFrame out{}; CHECK(from.endpoint.next_handshake(out)); CHECK(to.endpoint.receive_handshake(out));
            };
            frame(a,b);frame(b,a);frame(a,b);
            const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
            const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
            auto control=[](BankPeer& from,BankPeer& to) {
                EvaluationRecord out{}; CHECK(from.endpoint.next_control(out)); CHECK(to.endpoint.receive_control(out));
            };
            control(a,b);control(b,a);control(a,b);control(b,a);
            EvaluationRecord out{}; std::uint8_t got=0;
            CHECK(a.endpoint.send_status(5,out)); CHECK(b.endpoint.receive_status(out,got) && got==5);
            CHECK(a.endpoint.cancel() && b.endpoint.cancel());
        }
        BankPeer fresh(ba,InvitationRole::initiator,invitation.fields.signer,150);
        CHECK(fresh.endpoint.initialize()); CHECK(!fresh.endpoint.ready());
        BankPeer other(bb,InvitationRole::responder,invitation.fields.signer,230);
        CHECK(other.endpoint.initialize());
        CHECK(fresh.endpoint.prepare_identity() && other.endpoint.prepare_identity());
        invitation.fields.epoch=2; invitation.fields.nonce[0]=9;
        invitation.fields.peer_a=fresh.endpoint.public_identity(); invitation.fields.peer_b=other.endpoint.public_identity();
        invitation.fields.boot_a=fresh.endpoint.boot_context(); invitation.fields.boot_b=other.endpoint.boot_context(); invitation.sign();
        CHECK(fresh.endpoint.begin(invitation.invitation) && other.endpoint.begin(invitation.invitation));
        HandshakeFrame frame{};
        CHECK(fresh.endpoint.next_handshake(frame)); CHECK(other.endpoint.receive_handshake(frame));
        CHECK(other.endpoint.next_handshake(frame)); CHECK(fresh.endpoint.receive_handshake(frame));
        CHECK(fresh.endpoint.next_handshake(frame)); CHECK(other.endpoint.receive_handshake(frame));
        const auto* offer=fresh.endpoint.offer(); CHECK(offer);
        CHECK(!fresh.endpoint.confirm(*offer));
        CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,ea; SignedIndependentInvitation invitation;
        ProvisionedPeer p(ma,ea,InvitationRole::initiator,invitation.fields.signer,0);
        CHECK(p.evidence.initialize()); CHECK(!p.endpoint.initialize());
        ea.inner.trace.clear(); CHECK(!p.endpoint.prepare_reset());
        CHECK(ea.inner.count('w')==0 && ea.inner.count('e')==0 && ea.inner.count('s')==0); ++groups;
    }
    {
        SignedIndependentInvitation invitation; BankBackend a,b;
        EvaluationStorageBank bank(a), foreign(b);
        EnrollmentEvidenceStore evidence(*foreign.get(EvaluationNamespace::enrollment));
        security::test_support::FakeSecureRandomSource random; Source source;
        EnrolledPeerEndpoint endpoint(random,bank,evidence,source,InvitationRole::initiator,invitation.fields.signer,17);
        CHECK(!endpoint.initialize()); CHECK(endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        IndependentInvitation retained{}; CHECK(p.a.evidence.read(retained));
        CHECK(same_invitation(retained,p.invitation.invitation));
        EvaluationRecord wire{}; std::uint8_t got=0;
        CHECK(p.a.endpoint.send_status(8,wire)); CHECK(p.b.endpoint.receive_status(wire,got) && got==8);
        CHECK(p.a.endpoint.cancel() && p.b.endpoint.cancel());
        ProvisionedPeer fresh(ma,ea,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(fresh.endpoint.initialize()); CHECK(!fresh.endpoint.ready()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair first(ma,mb,ea,eb); first.activate();
        CHECK(first.a.endpoint.cancel() && first.b.endpoint.cancel());
        ProvisionedPair second(ma,mb,ea,eb,150,2); second.activate();
        IndependentInvitation retained{}; CHECK(second.a.evidence.read(retained));
        CHECK(same_invitation(retained,second.invitation.invitation));
        CHECK(!same_invitation(retained,first.invitation.invitation));
        EvaluationRecord wire{}; std::uint8_t got=0;
        CHECK(second.a.endpoint.send_status(4,wire)); CHECK(second.b.endpoint.receive_status(wire,got) && got==4);
        ++groups;
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb);
        if(invalid==0) p.invitation.fields.group++;
        if(invalid==1) p.invitation.fields.peer_a[0]^=1;
        if(invalid==2) p.invitation.fields.boot_a[0]^=1;
        if(invalid==3) p.invitation.fields.issued_a_ms++;
        p.invitation.sign(); if(invalid==4) p.invitation.invitation.signature[0]^=1;
        ea.inner.trace.clear(); CHECK(!p.a.endpoint.begin(p.invitation.invitation));
        CHECK(ea.inner.count('w')==0 && ea.inner.count('e')==0);
        CHECK(p.a.endpoint.secrets_cleared()); ++groups;
    }
    for(unsigned invalid=0;invalid<4;++invalid) {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        CHECK(p.a.endpoint.cancel());
        auto signer=p.invitation.fields.signer; if(invalid==0) signer[0]^=1;
        ProvisionedPeer fresh(ma,ea,invalid==2?InvitationRole::responder:InvitationRole::initiator,
            signer,150,invalid==1?18:17);
        CHECK(!fresh.endpoint.initialize(invalid==3?&p.invitation.invitation:nullptr));
        CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.begin();
        CHECK(p.a.endpoint.cancel());
        ProvisionedPeer fresh(ma,ea,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(!fresh.endpoint.initialize()); CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        CHECK(p.a.endpoint.cancel()); ea.inner.memory.corrupt_byte(domain,0,0,0x55);
        ProvisionedPeer fresh(ma,ea,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(!fresh.endpoint.initialize()); CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    for(bool reset : {false,true}) {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        CHECK(reset?p.a.endpoint.prepare_reset():p.a.endpoint.revoke());
        ProvisionedPeer fresh(ma,ea,InvitationRole::initiator,p.invitation.fields.signer,150);
        CHECK(!fresh.endpoint.initialize()); CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    for(bool reset : {false,true}) {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair baseline(ma,mb,ea,eb);
        if(reset) baseline.activate();
        ea.inner.trace.clear();
        CHECK(reset?baseline.a.endpoint.prepare_reset():baseline.a.endpoint.begin(baseline.invitation.invitation));
        const auto trace=ea.inner.trace;
        for(auto fault : faults) {
            unsigned count=0; for(char op:trace) count+=op==operation(fault);
            for(unsigned nth=1;nth<=count;++nth) {
                CallbackStorage m1,m2,e1,e2; ProvisionedPair p(m1,m2,e1,e2);
                if(reset) p.activate();
                e1.arm(fault,nth);
                CHECK(!(reset?p.a.endpoint.prepare_reset():p.a.endpoint.begin(p.invitation.invitation)));
                CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared());
                e1.inner.clear(); no_resume(m1,e1,p.invitation.fields.signer); ++groups;
            }
        }
    }
    for(unsigned trigger=0;trigger<2;++trigger) {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        bool fired=false;
        auto mutation=[&](char op) { if(!fired && op==(trigger==0?'r':'w')) {
            fired=true; ea.inner.memory.corrupt_byte(domain,0,0,0x55);
        }};
        if(trigger==0) ma.callback=mutation; else p.a.tx.callback=mutation;
        EvaluationRecord wire{}; wire.counter=87; const auto before=wire;
        CHECK(!p.a.endpoint.send_status(1,wire)); CHECK(fired && same_record(wire,before));
        CHECK(p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair first(ma,mb,ea,eb); first.activate();
        CHECK(first.a.endpoint.cancel() && first.b.endpoint.cancel());
        ProvisionedPair second(ma,mb,ea,eb,150,2); second.begin();
        CHECK(second.a.endpoint.cancel());
        ProvisionedPeer fresh(ma,ea,InvitationRole::initiator,first.invitation.fields.signer,70);
        CHECK(!fresh.endpoint.initialize()); CHECK(fresh.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma,mb,ea,eb; ProvisionedPair baseline(ma,mb,ea,eb); baseline.activate();
        ma.inner.trace.clear(); CHECK(baseline.a.endpoint.prepare_reset()); const auto trace=ma.inner.trace;
        for(auto fault : faults) {
            unsigned count=0; for(char op:trace) count+=op==operation(fault);
            for(unsigned nth=1;nth<=count;++nth) {
                CallbackStorage m1,m2,e1,e2; ProvisionedPair p(m1,m2,e1,e2); p.activate();
                m1.arm(fault,nth); CHECK(!p.a.endpoint.prepare_reset());
                CHECK(!p.a.endpoint.ready() && p.a.endpoint.secrets_cleared());
                m1.inner.clear();
                ProvisionedPeer fresh(m1,e1,InvitationRole::initiator,p.invitation.fields.signer,150);
                CHECK(!fresh.endpoint.initialize()); CHECK(fresh.endpoint.secrets_cleared()); ++groups;
            }
        }
    }
    // Every evidence read may naturally advance authority state. Even the final
    // read before publication must be followed by read-only authority checks.
    for(unsigned fault=0;fault<3;++fault) {
        CallbackStorage m0,m1,e0,e1; ProvisionedPair baseline(m0,m1,e0,e1); baseline.activate();
        e0.inner.trace.clear(); EvaluationRecord valid{}; CHECK(baseline.a.endpoint.send_status(1,valid));
        const auto reads=e0.inner.count('r'); CHECK(reads>0);
        CallbackStorage ma,mb,ea,eb; ProvisionedPair p(ma,mb,ea,eb); p.activate();
        unsigned seen=0;
        ea.callback=[&](char op) { if(op!='r' || ++seen!=reads) return;
            if(fault==0) p.a.source.value.now_ms=1000;
            if(fault==1) ++p.a.source.value.context.session_nonce;
            if(fault==2) p.a.random.set_state(security::EntropyState::not_ready);
        };
        EvaluationRecord out{}; out.counter=74; const auto before=out;
        CHECK(!p.a.endpoint.send_status(2,out)); CHECK(seen==reads && same_record(out,before));
        CHECK(p.a.endpoint.secrets_cleared()); ++groups;
    }
    std::cout << "PASS " << groups << " provisioned peer endpoint groups\n";
}

