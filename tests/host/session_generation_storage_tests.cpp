#include "security_peer_traffic_fixture.hpp"
#include "opentrail/session_generation_storage.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
using namespace peer_traffic_test;
namespace {
struct Backend final : EvaluationGenerationBackend {
    std::array<std::array<CallbackStorage,7>,5> stores;
    unsigned reads=0, fail_read=0;
    std::function<void()> callback=[]{};
    CallbackStorage& at(std::uint64_t g,EvaluationNamespace n) { CHECK(g<5); return stores[g][static_cast<unsigned>(n)]; }
    persistence::StorageReadResult read(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s,persistence::MutableStorageByteView out) override {
        callback(); if(++reads==fail_read) return {Error::io_failure,0}; return at(g,n).read_slot(d,s,out);
    }
    Error erase(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s) override { return at(g,n).erase_slot(d,s); }
    Error write(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s,std::size_t o,persistence::StorageByteView in) override { return at(g,n).write_slot(d,s,o,in); }
    Error sync(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s) override { return at(g,n).sync_slot(d,s); }
};
struct Allocation {
    GenerationLedgerStorage ledger;
    SessionGenerationAllocator allocator;
    std::uint64_t generation=0;
    Allocation(Backend& b):ledger(b),allocator(ledger,b,4) { CHECK(allocator.initialize()); CHECK(allocator.allocate(generation)); }
};
struct PeerInstance {
    Allocation allocation;
    GenerationEvaluationBackend mapped;
    EvaluationStorageBank bank;
    EnrollmentEvidenceStore evidence;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    PeerInstance(Backend& b,InvitationRole r,const InvitationKey& signer,unsigned offset)
        :allocation(b),mapped(allocation.allocator,b,allocation.generation),bank(mapped),
        evidence(*bank.get(EvaluationNamespace::enrollment)),endpoint(random,bank,evidence,source,r,signer,17) {
        std::array<unsigned char,64> bytes{};
        for(unsigned i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size())); random.set_state(security::EntropyState::ready);
        if(r==InvitationRole::responder) source.value={{5,8},70000};
    }
};
void activate(PeerInstance& a,PeerInstance& b,SignedIndependentInvitation& invitation,unsigned epoch) {
    CHECK(a.endpoint.initialize() && b.endpoint.initialize());
    CHECK(!a.endpoint.ready() && !b.endpoint.ready());
    CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
    invitation.fields.epoch=epoch; invitation.fields.nonce[0]=static_cast<unsigned char>(epoch);
    invitation.fields.peer_a=a.endpoint.public_identity(); invitation.fields.peer_b=b.endpoint.public_identity();
    invitation.fields.boot_a=a.endpoint.boot_context(); invitation.fields.boot_b=b.endpoint.boot_context(); invitation.sign();
    CHECK(a.endpoint.begin(invitation.invitation) && b.endpoint.begin(invitation.invitation));
    auto frame=[](PeerInstance& from,PeerInstance& to){ HandshakeFrame f{}; CHECK(from.endpoint.next_handshake(f)); CHECK(to.endpoint.receive_handshake(f)); };
    frame(a,b); frame(b,a); frame(a,b);
    const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
    const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
    auto control=[](PeerInstance& from,PeerInstance& to){ EvaluationRecord r{}; CHECK(from.endpoint.next_control(r)); CHECK(to.endpoint.receive_control(r)); };
    control(a,b); control(b,a); control(a,b); control(b,a);
    CHECK(a.endpoint.ready() && b.endpoint.ready());
}
}
int main() {
    unsigned groups=0;
    {
        Backend b; Allocation first(b); CHECK(first.generation==1);
        GenerationEvaluationBackend old(first.allocator,b,1); EvaluationStorageBank oldbank(old);
        authority_detail::Bytes bytes{}; bytes.fill(42);
        CHECK(oldbank.get(EvaluationNamespace::transmit)->write_slot(domain,0,0,{bytes.data(),bytes.size()})==Error::none);
        GenerationLedgerStorage ledger(b); SessionGenerationAllocator next(ledger,b,4);
        CHECK(next.initialize()); CHECK(!next.current(1));
        std::uint64_t generation=99; CHECK(next.allocate(generation) && generation==2);
        const auto before=bytes;
        CHECK(!oldbank.get(EvaluationNamespace::transmit)->read_slot(domain,0,{bytes.data(),bytes.size()}).read()); CHECK(bytes==before);
        CHECK(b.at(1,EvaluationNamespace::transmit).inner.memory.slot_bytes(domain,0)==before);
        CHECK(next.current(2)); ++groups;
    }
    {
        Backend b; Allocation a(b); std::uint64_t g=0;
        CHECK(a.allocator.allocate(g) && g==2); CHECK(!a.allocator.current(1)); CHECK(a.allocator.current(2));
        CHECK(a.allocator.allocate(g) && g==3); CHECK(a.allocator.allocate(g) && g==4);
        g=97; CHECK(!a.allocator.allocate(g) && g==97); ++groups;
    }
    {
        Backend b; std::uint8_t byte=1;
        CHECK(b.at(1,EvaluationNamespace::receive).write_slot(domain,1,0,{&byte,1})==Error::none);
        GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4);
        CHECK(a.initialize()); std::uint64_t g=97; CHECK(!a.allocate(g) && g==97);
        Allocation next(b); CHECK(next.generation==2); ++groups;
    }
    {
        Backend b; Allocation first(b);
        GenerationEvaluationBackend mapped(first.allocator,b,1); EvaluationStorageBank bank(mapped);
        GenerationLedgerStorage ledger(b); SessionGenerationAllocator replacement(ledger,b,4); CHECK(replacement.initialize());
        bool once=false;
        b.at(0,EvaluationNamespace::membership).callback=[&](char op){
            if(op=='r' && !once) { once=true; std::uint64_t g=0; CHECK(replacement.allocate(g) && g==2); }
        };
        authority_detail::Bytes out{}; out.fill(97); const auto before=out;
        CHECK(!bank.get(EvaluationNamespace::membership)->read_slot(domain,0,{out.data(),out.size()}).read());
        CHECK(out==before && replacement.current(2)); ++groups;
    }
    {
        Backend b; Allocation a(b); GenerationEvaluationBackend mapped(a.allocator,b,1); EvaluationStorageBank bank(mapped);
        bool once=false; authority_detail::Bytes nested{};
        b.at(1,EvaluationNamespace::boot).callback=[&](char op){if(op=='r'&&!once){once=true;CHECK(!bank.get(EvaluationNamespace::boot)->read_slot(domain,0,{nested.data(),nested.size()}).read());}};
        authority_detail::Bytes out{};out.fill(97);const auto before=out;
        CHECK(!bank.get(EvaluationNamespace::boot)->read_slot(domain,0,{out.data(),out.size()}).read()); CHECK(out==before);++groups;
    }
    {
        Backend b;GenerationLedgerStorage ledger(b);SessionGenerationAllocator a(ledger,b,0);CHECK(!a.initialize());std::uint64_t g=97;CHECK(!a.allocate(g)&&g==97);++groups;
    }
    // Every destination read fault consumes the committed generation; a fresh
    // allocator can only allocate the next one, never resume the failed attempt.
    for(unsigned nth=1;nth<=50;++nth) {
        Backend b; GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4);
        CHECK(a.initialize());
        const unsigned n=(nth-1)/10, local=(nth-1)%10+1;
        b.stores[1][n].arm(Fault::read_error,local);
        std::uint64_t g=97; CHECK(!a.allocate(g) && g==97);
        b.stores[1][n].inner.clear(); Allocation next(b); CHECK(next.generation==2); ++groups;
    }
    for(unsigned location=0;location<50;++location) {
        Backend b; const unsigned n=location/10,d=(location%10)/2,slot=location%2;
        const std::uint8_t byte=0;
        CHECK(b.stores[1][n].write_slot(static_cast<Domain>(d),slot,0,{&byte,1})==Error::none);
        GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4); CHECK(a.initialize());
        std::uint64_t g=97; CHECK(!a.allocate(g) && g==97); Allocation next(b); CHECK(next.generation==2); ++groups;
    }
    // Damage in either durable slot must never authorize an existing generation.
    for(unsigned offset=0;offset<128;++offset) {
        Backend b; Allocation first(b); std::uint64_t g=0; CHECK(first.allocator.allocate(g));
        auto& memory=b.at(0,EvaluationNamespace::boot).inner.memory;
        auto bytes=memory.slot_bytes(domain,offset/64); bytes[offset%64]^=1;
        CHECK(memory.erase_slot(domain,offset/64)==Error::none);
        CHECK(memory.write_slot(domain,offset/64,0,{bytes.data(),bytes.size()})==Error::none);
        GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4);
        CHECK(!a.initialize()); g=97; CHECK(!a.allocate(g) && g==97); ++groups;
    }
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read,Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after}) {
        Backend reference; GenerationLedgerStorage rl(reference); SessionGenerationAllocator ra(rl,reference,4); CHECK(ra.initialize());
        auto& ls=reference.at(0,EvaluationNamespace::boot).inner; ls.clear(); std::uint64_t value=0; CHECK(ra.allocate(value));
        const char kind=(fault==Fault::read_error||fault==Fault::short_read||fault==Fault::corrupt_read)?'r':fault==Fault::sync_after?'s':'w';
        for(unsigned nth=1;nth<=ls.count(kind);++nth) {
            Backend b; GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4); CHECK(a.initialize());
            b.at(0,EvaluationNamespace::boot).arm(fault,nth); value=97; CHECK(!a.allocate(value) && value==97);
            b.at(0,EvaluationNamespace::boot).inner.clear(); SessionGenerationAllocator next(ledger,b,4);
            if(next.initialize()) { CHECK(!next.current(1)); CHECK(next.allocate(value)); CHECK(value==1 || value==2); }
            ++groups;
        }
    }
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read,Fault::erase_before,Fault::erase_after,Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after}) {
        Backend reference; Allocation setup(reference); auto& ls=reference.at(0,EvaluationNamespace::boot).inner; ls.clear();
        std::uint64_t value=0; CHECK(setup.allocator.allocate(value));
        const char kind=(fault==Fault::read_error||fault==Fault::short_read||fault==Fault::corrupt_read)?'r':(fault==Fault::erase_before||fault==Fault::erase_after)?'e':fault==Fault::sync_after?'s':'w';
        const unsigned calls=ls.count(kind);
        for(unsigned nth=1;nth<=calls;++nth) {
            Backend b; Allocation first(b); b.at(0,EvaluationNamespace::boot).arm(fault,nth);
            value=97; CHECK(!first.allocator.allocate(value) && value==97);
            b.at(0,EvaluationNamespace::boot).inner.clear();
            GenerationLedgerStorage ledger(b); SessionGenerationAllocator next(ledger,b,4);
            if(next.initialize()) { CHECK(!next.current(1) && !next.current(2)); CHECK(next.allocate(value)); CHECK(value>=2); }
            ++groups;
        }
    }
    {
        Backend b; GenerationLedgerStorage ledger(b); SessionGenerationAllocator a(ledger,b,4); CHECK(a.initialize());
        bool once=false; b.callback=[&]{ if(!once) { once=true; std::uint64_t value=8; CHECK(!a.allocate(value)); CHECK(value==8); } };
        std::uint64_t g=97; CHECK(!a.allocate(g) && g==97); ++groups;
    }
    {
        Backend ba,bb; SignedIndependentInvitation invitation;
        EvaluationRecord oldwire{}; InvitationKey firstidentity{};
        {
            PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,0),b(bb,InvitationRole::responder,invitation.fields.signer,80);
            activate(a,b,invitation,1); firstidentity=a.endpoint.public_identity();
            CHECK(a.endpoint.send_status(8,oldwire)); std::uint8_t received=0;
            CHECK(b.endpoint.receive_status(oldwire,received) && received==8);
            CHECK(a.endpoint.cancel() && b.endpoint.cancel()); ++groups;
        }
        {
            PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,150),b(bb,InvitationRole::responder,invitation.fields.signer,230);
            CHECK(a.allocation.generation==2 && b.allocation.generation==2);
            activate(a,b,invitation,2); CHECK(a.endpoint.public_identity()!=firstidentity);
            std::uint8_t received=97; CHECK(!b.endpoint.receive_status(oldwire,received) && received==97);
            for(unsigned i=1;i<=8;++i) {
                EvaluationRecord wire{}; CHECK(a.endpoint.send_status(static_cast<std::uint8_t>(i),wire));
                CHECK(b.endpoint.receive_status(wire,received) && received==i);
                CHECK(b.endpoint.send_status(static_cast<std::uint8_t>(i),wire)); CHECK(a.endpoint.receive_status(wire,received) && received==i);
            }
            CHECK(a.endpoint.prepare_reset() && b.endpoint.prepare_reset()); ++groups;
        }
        {
            PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,31);
            CHECK(!a.endpoint.initialize()); CHECK(a.endpoint.secrets_cleared()); ++groups;
        }
    }
    std::printf("PASS %u session generation storage groups\n",groups);
}
