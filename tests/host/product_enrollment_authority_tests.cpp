#include "product_enrollment_fixture.hpp"
using namespace product_enrollment_test;

static CallbackStorage& committed_store(Node& n,unsigned kind) {
    return kind==0 ? n.evidence : kind==1 ? n.binding_proof : kind==2 ? n.membership : n.journal;
}
static void inactive(Node& n) {
    EvaluationRecord record{},saved=record;
    CHECK(!n.endpoint->send_status(1,record) && same_record(record,saved));
    CHECK(n.endpoint->secrets_cleared());
}
static void copy_public_store(const CallbackStorage& from,CallbackStorage& to) {
    for(unsigned d=0;d<5;++d) for(unsigned s=0;s<2;++s) {
        const auto domain=static_cast<Domain>(d);
        to.inner.memory.seed_slot(domain,s,from.inner.memory.slot_bytes(domain,s));
    }
}
int main() {
    unsigned groups=0;
    // Earlier storage callbacks must not publish stage3 after the active owner
    // is cancelled or reentered. Both first enrollment and rekey use real peers.
    for(bool rekey:{false,true}) for(unsigned kind=0;kind<4;++kind)
    for(char operation:{'w','s'}) for(bool cancel:{false,true}) {
        ProductPair p;
        if(rekey){p.activate();p.restart_and_compare();p.authorize(2);}
        p.controls();
        auto& storage=committed_store(p.a,kind);bool fired=false;
        storage.callback=[&](char actual) {
            if(!fired && actual==operation) {
                fired=true;
                if(cancel)p.a.preparation->cancel();
                else CHECK(!p.a.endpoint->ready());
            }
        };
        CHECK(!p.a.endpoint->commit_membership() && fired);
        storage.callback=[](char){};
        inactive(p.a);
        EnrollmentCommitCoordinator reconstructed(p.a.journal);
        if(reconstructed.initialize()) CHECK(reconstructed.state()==EnrollmentJournalState::reconcile);
        else CHECK(kind==3 && reconstructed.failed());
        ++groups;
    }
    // All four cloned stores hold byte-identical, legitimately committed data.
    // Their comparison can authenticate successfully, but cannot authorize the
    // product whose actual retained owners did not produce that comparison.
    for(bool responder:{false,true}) {
        ProductPair p;p.activate();
        restart_node(p.a,p.a.key,151);restart_node(p.b,p.a.key,211);
        auto& foreign=responder?p.b:p.a;
        CallbackStorage journal_storage,member_storage,evidence_storage,binding_storage;
        copy_public_store(foreign.journal,journal_storage);
        copy_public_store(foreign.membership,member_storage);
        copy_public_store(foreign.evidence,evidence_storage);
        copy_public_store(foreign.binding_proof,binding_storage);
        EnrollmentCommitCoordinator journal(journal_storage);
        PeerMembershipStore member(member_storage);
        EnrollmentEvidenceStore evidence(evidence_storage);
        EnrollmentBindingStore bindings(binding_storage);
        CHECK(journal.initialize() && member.initialize() && evidence.initialize() && bindings.initialize());
        foreign.comparison.emplace(foreign.random,journal,member,evidence,bindings,
            foreign.identity,foreign.allocator,foreign.port,foreign.role);
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        CHECK(p.a.comparison->begin(ac) && p.b.comparison->begin(bc));
        CHECK(p.a.comparison->sign(bc,ar) && p.b.comparison->sign(ac,br));
        CHECK(p.a.comparison->verify(br,p.a.compared) && p.b.comparison->verify(ar,p.b.compared));
        p.a.preparation.emplace(p.a.random,p.a.identity,p.a.allocator,p.a.port,std::move(*p.a.compared));
        p.b.preparation.emplace(p.b.random,p.b.identity,p.b.allocator,p.b.port,std::move(*p.b.compared));
        p.authorize(2);
        foreign.journal.inner.clear();
        CHECK(!foreign.endpoint->begin(*foreign.trusted));inactive(foreign);
        CHECK(foreign.journal.inner.count('w')==0);
        EnrollmentCommitCoordinator unchanged(foreign.journal);
        CHECK(unchanged.initialize() && unchanged.state()==EnrollmentJournalState::active_committed);
        EnrollmentCommitContext context{};
        CHECK(unchanged.read_public(context) && context.epoch==1);
        ++groups;
    }
    // Every direct storage alias among the nine constructor namespaces is
    // refused before any persistent initialization or entropy consumption.
    for(unsigned first=0;first<9;++first) for(unsigned second=first+1;second<9;++second) {
        std::array<CallbackStorage,9> stores;
        std::array<CallbackStorage*,9> selected{};
        for(unsigned i=0;i<9;++i)selected[i]=&stores[i];
        selected[second]=selected[first];
        security::test_support::FakeSecureRandomSource random;entropy(random,19);
        Source clock;Port port;InvitationKey signer{};signer.fill(1);
        ProductEnrollmentActivation endpoint(random,*selected[0],*selected[1],*selected[2],*selected[3],
            *selected[4],*selected[5],*selected[6],*selected[7],*selected[8],clock,port,InvitationRole::initiator,signer);
        CHECK(!endpoint.prepare_identity() && endpoint.secrets_cleared());
        for(const auto& storage:stores)CHECK(storage.inner.trace.empty());
        ++groups;
    }
    std::cout<<"PASS "<<groups<<" product enrollment authority groups\n";
}
