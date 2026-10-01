#define OPENTRAIL_SESSION_FIXTURE_ONLY
#include "enrollment_candidate_session_tests.cpp"

namespace recovery_test {
struct PublicSnapshot {
    decltype(bytes(std::declval<const CallbackStorage&>())) identity,journal,binding,member,evidence;
    bool operator==(const PublicSnapshot& other) const {
        return identity==other.identity && journal==other.journal && binding==other.binding &&
            member==other.member && evidence==other.evidence;
    }
};
PublicSnapshot public_bytes(FreshNode& n) {
    return {bytes(n.identity),bytes(n.journal),bytes(n.binding),
        bytes(n.backend.at(0,EvaluationNamespace::membership)),bytes(n.backend.at(0,EvaluationNamespace::enrollment))};
}
void target_before_marker(FreshNode& n) {
    const auto before=mutations(n.backend.at(0,EvaluationNamespace::membership));bool fired=false;
    n.journal.callback=[&](char op){if(op=='r' && !fired && mutations(n.backend.at(0,EvaluationNamespace::membership))>before) {
        fired=true;CHECK(n.request.cancel_exact(n.request.request()));
    }};
    CHECK(!n.owner->commit());n.journal.callback=[](char){};
    CHECK(fired && n.journal_state()==EnrollmentJournalState::reconcile);
    CHECK(retained_member(n).state==MembershipState::active);
}
struct RecoveryPair : FreshPair {
    EnrollmentRecoveryChallenge ac{},bc{};EnrollmentRecoveryResponse ar{},br{};
    explicit RecoveryPair(bool previous=false):FreshPair(true) {
        if(previous){activate();restart_retained();}
        controls();
    }
    void one_sided(bool reverse=false) {CHECK((reverse?b:a).owner->commit());reconstruct_retained();}
    void start() {CHECK(a.owner->start_recovery(a.role,17));CHECK(b.owner->start_recovery(b.role,17));}
    EnrollmentIdentityProof archive() {
        std::optional<EnrollmentIdentityProof> aa,bb;
        CHECK(a.owner->export_recovery_archive(aa));CHECK(b.owner->export_recovery_archive(bb));
        CHECK(aa || bb);return aa?*aa:*bb;
    }
    void supply() {
        std::optional<EnrollmentIdentityProof> aa,bb;
        CHECK(a.owner->export_recovery_archive(aa));CHECK(b.owner->export_recovery_archive(bb));CHECK(aa || bb);
        if(!aa)CHECK(a.owner->accept_recovery_archive(*bb));
        if(!bb)CHECK(b.owner->accept_recovery_archive(*aa));
    }
    void compare() {
        CHECK(a.owner->begin_recovery_comparison(ac));CHECK(b.owner->begin_recovery_comparison(bc));
        CHECK(a.owner->sign_recovery_challenge(bc,ar));CHECK(b.owner->sign_recovery_challenge(ac,br));
    }
    void finish(bool reverse=false) {
        if(reverse){CHECK(b.owner->finish_recovery_comparison(ar));CHECK(a.owner->finish_recovery_comparison(br));}
        else {CHECK(a.owner->finish_recovery_comparison(br));CHECK(b.owner->finish_recovery_comparison(ar));}
        CHECK(a.journal_state()==EnrollmentJournalState::active_committed);
        CHECK(b.journal_state()==EnrollmentJournalState::active_committed);
    }
    void statuses() {
        for(unsigned i=1;i<=4;++i)for(auto* from:{&a,&b}) {
            auto& to=from==&a?b:a;EvaluationRecord record{};std::uint8_t value=0;
            CHECK(from->owner->send_status(static_cast<std::uint8_t>(i),record));
            CHECK(to.owner->receive_status(record,value));CHECK(value==i);
        }
    }
};
}
using namespace recovery_test;
#ifndef OPENTRAIL_RECOVERY_FIXTURE_ONLY
int main() {
    unsigned groups=0;
    // Actual first join and rekey, both interruption directions and finish orders.
    for(bool previous:{false,true})for(bool reverse:{false,true}) {
        RecoveryPair p(previous);p.one_sided(reverse);
        const auto ai=bytes(p.a.identity),bi=bytes(p.b.identity);
        const auto ad=p.a.request.request().deadline_ms,bd=p.b.request.request().deadline_ms;
        p.start();p.supply();p.compare();p.finish(reverse);
        CHECK(p.a.request.request().deadline_ms==ad && p.b.request.request().deadline_ms==bd);
        CHECK(bytes(p.a.identity)==ai && bytes(p.b.identity)==bi);
        p.activate();p.statuses();CHECK(p.epoch==(previous?3U:2U));
        CHECK(p.a.owner->close() && p.b.owner->close());
        CHECK(p.a.owner->secrets_cleared() && p.b.owner->secrets_cleared());++groups;
    }
    // Both actual stage-2 journals with complete target public metadata.
    for(bool previous:{false,true}) {
        RecoveryPair p(previous);target_before_marker(p.a);target_before_marker(p.b);
        p.reconstruct_retained();p.start();p.supply();p.compare();p.finish();p.activate();p.statuses();++groups;
    }
    {
        RecoveryPair p;CHECK(p.a.owner->commit() && p.b.owner->commit());p.reconstruct_retained();
        const auto a=public_bytes(p.a),b=public_bytes(p.b);
        p.start();p.supply();p.compare();p.finish();CHECK(public_bytes(p.a)==a && public_bytes(p.b)==b);
        p.activate();p.statuses();++groups;
    }
    // Local completion may be asymmetric; a new authenticated comparison retries
    // the supported coherent state rather than assuming simultaneous peer commit.
    {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();
        CHECK(p.b.owner->finish_recovery_comparison(p.ar));CHECK(p.a.owner->close());CHECK(p.b.owner->close());
        p.a.restart(157);p.b.restart(233);p.start();p.supply();p.compare();p.finish();p.activate();p.statuses();++groups;
    }
    {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();p.finish();
        CHECK(!p.a.owner->cancel() && !p.b.owner->cancel());
        const auto a=public_bytes(p.a),b=public_bytes(p.b);
        p.a.restart(167);p.b.restart(247);
        CHECK(p.a.owner->start_rekey(p.a.role,17) && p.b.owner->start_rekey(p.b.role,17));
        CHECK(public_bytes(p.a)==a && public_bytes(p.b)==b);
        p.activate();p.statuses();++groups;
    }
    {
        RecoveryPair p;CHECK(p.a.owner->commit());EvaluationRecord old{};CHECK(p.a.owner->send_status(1,old));
        p.reconstruct_retained();p.start();p.supply();p.compare();p.finish();p.activate();p.statuses();
        std::uint8_t out=71;CHECK(!p.b.owner->receive_status(old,out));CHECK(out==71 && p.b.owner->secrets_cleared());++groups;
    }
    // No archive on either side: retain exact public records without authority.
    {
        RecoveryPair p;p.reconstruct_retained();p.start();std::optional<EnrollmentIdentityProof> a,b;
        CHECK(p.a.owner->export_recovery_archive(a) && !a);CHECK(p.b.owner->export_recovery_archive(b) && !b);
        const auto before=public_bytes(p.a);EnrollmentRecoveryChallenge out{};out.version=79;
        CHECK(!p.a.owner->begin_recovery_comparison(out));CHECK(out.version==79 && public_bytes(p.a)==before);++groups;
    }
    // A signed archive is never permission to substitute any identity or fields.
    for(unsigned variant=0;variant<7;++variant) {
        RecoveryPair p;p.one_sided();p.start();auto proof=p.archive();
        if(variant==0)proof.initiator_signature[0]^=1;
        if(variant==1)proof.responder_signature[0]^=1;
        if(variant==2)proof.invitation.signature[0]^=1;
        if(variant==3)proof.invitation.payload[0]^=1;
        if(variant==4)proof.invitation.payload[40]^=1;
        if(variant==5)proof.invitation.payload[176]^=1;
        if(variant==6)std::swap(proof.initiator_signature,proof.responder_signature);
        const auto before=public_bytes(p.b);CHECK(!p.b.owner->accept_recovery_archive(proof));
        CHECK(public_bytes(p.b)==before && p.b.owner->secrets_cleared());++groups;
    }
    // Public reconciliation is not traffic permission; the actual new protocol is mandatory.
    for(bool completed:{false,true}) {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();if(completed)p.finish();
        const auto before=public_bytes(p.b);EvaluationRecord out{};out.group=765;
        CHECK(!p.b.owner->send_status(1,out));CHECK(out.group==765 && public_bytes(p.b)==before);++groups;
    }
    for(unsigned variant=0;variant<8;++variant) {
        RecoveryPair p;p.one_sided();p.start();p.supply();
        CHECK(p.a.owner->begin_recovery_comparison(p.ac));CHECK(p.b.owner->begin_recovery_comparison(p.bc));auto peer=p.bc;
        if(variant==0)++peer.version;
        if(variant==1)peer.role=p.a.role;
        if(variant==2)++peer.state.record.group;
        if(variant==3)peer.state.record.operation[0]^=1;
        if(variant==4)peer.state.stage=1;
        if(variant==5)peer.context.request=0;
        if(variant==6)peer.context.generation=peer.state.record.session_generation;
        if(variant==7)peer.challenge=p.ac.challenge;
        const auto before=public_bytes(p.a);EnrollmentRecoveryResponse out{};out.signature.fill(67);const auto unchanged=out.signature;
        CHECK(!p.a.owner->sign_recovery_challenge(peer,out));CHECK(out.signature==unchanged && public_bytes(p.a)==before);++groups;
    }
    for(unsigned variant=0;variant<4;++variant) {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();auto peer=p.ar;
        if(variant==0)peer.signature[0]^=1;
        if(variant==1)peer=p.br;
        if(variant==2)peer.responder.context.request++;
        if(variant==3)peer.initiator.state.member.generation++;
        const auto before=public_bytes(p.b);CHECK(!p.b.owner->finish_recovery_comparison(peer));
        CHECK(public_bytes(p.b)==before);++groups;
    }
    // Actual authorization/deadline changes at the final comparison cannot commit.
    for(unsigned variant=0;variant<5;++variant) {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();const auto before=public_bytes(p.b);
        if(variant==0)CHECK(p.b.request.cancel_exact(p.b.request.request()));
        if(variant==1)p.b.replace_request();
        if(variant==2)p.b.authority.value.phase=DeviceNamePhase::disconnected;
        if(variant==3)p.b.authority.value.now_ms+=60000;
        if(variant==4)--p.b.authority.value.now_ms;
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));CHECK(public_bytes(p.b)==before);
        if(variant==1)CHECK(p.b.request.pending());
        ++groups;
    }
    // Starting recovery late cannot extend the original admitted request.
    {
        RecoveryPair p;p.one_sided();p.a.authority.value.now_ms+=70000;p.b.authority.value.now_ms+=70000;
        p.start();p.supply();p.compare();const auto before=public_bytes(p.b);
        const auto recovery_start=p.b.authority.value.now_ms;
        p.b.authority.value.now_ms=p.b.request.request().deadline_ms;
        CHECK(p.b.authority.value.now_ms<recovery_start+60000);
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));CHECK(public_bytes(p.b)==before);++groups;
    }
    // PREPARED is not ACTIVATION_POSSIBLE and grants no recovery mutation.
    {
        FreshPair p(true);p.confirmation();p.reconstruct_retained();const auto before=public_bytes(p.a);
        const auto ledger=mutations(p.a.backend.at(0,EvaluationNamespace::boot)),boot=mutations(p.a.boot);
        CHECK(!p.a.owner->start_recovery(p.a.role,17));CHECK(public_bytes(p.a)==before);
        CHECK(mutations(p.a.backend.at(0,EvaluationNamespace::boot))==ledger && mutations(p.a.boot)==boot);++groups;
    }
    {
        RecoveryPair p(true);p.one_sided();std::array<std::uint8_t,64> blank{};blank.fill(0xff);
        for(unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s)p.b.boot.inner.memory.seed_slot(static_cast<Domain>(d),s,blank);
        const auto before=public_bytes(p.b);const auto ledger=mutations(p.b.backend.at(0,EvaluationNamespace::boot));
        const auto boot=mutations(p.b.boot),identity=mutations(p.b.identity);
        CHECK(!p.b.owner->start_recovery(p.b.role,17));CHECK(public_bytes(p.b)==before);
        CHECK(mutations(p.b.backend.at(0,EvaluationNamespace::boot))==ledger && mutations(p.b.boot)==boot && mutations(p.b.identity)==identity);++groups;
    }
    // Corrupt/torn, revoked, mixed evidence, and missing local identity refuse preflight.
    for(unsigned variant=0;variant<5;++variant) {
        RecoveryPair p;p.one_sided();
        if(variant==0)p.b.journal.arm(Fault::read_error);
        if(variant==1){PeerMembershipStore member(p.a.backend.at(0,EvaluationNamespace::membership));CHECK(member.initialize() && member.revoke());}
        if(variant==2){const auto proof=[&]{EnrollmentEvidenceStore e(p.a.backend.at(0,EvaluationNamespace::enrollment));IndependentInvitation v{};CHECK(e.initialize() && e.read(v));return v;}();EnrollmentEvidenceStore e(p.b.backend.at(0,EvaluationNamespace::enrollment));CHECK(e.initialize() && e.replace(proof));}
        if(variant==3){auto value=p.b.identity.inner.memory.slot_bytes(Domain::secret_material,0);value[0]^=1;p.b.identity.inner.memory.seed_slot(Domain::secret_material,0,value);}
        if(variant==4){const std::uint8_t byte=0;CHECK(p.b.binding.write_slot(Domain::outbound_counter_state,0,0,{&byte,1})==Error::none);}
        auto& n=variant==1?p.a:p.b;const auto before=public_bytes(n);const auto identity=mutations(n.identity);
        CHECK(!n.owner->start_recovery(n.role,17));CHECK(public_bytes(n)==before && mutations(n.identity)==identity);++groups;
    }
    // Recovery write failure cannot produce ready or silently erase to retry.
    for(unsigned domain=0;domain<4;++domain)for(auto fault:{Fault::write_before,Fault::write_after,Fault::sync_after}) {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();const auto journal=bytes(p.b.journal);
        auto& store=domain==0?p.b.backend.at(0,EvaluationNamespace::enrollment):
            domain==1?p.b.binding:domain==2?p.b.backend.at(0,EvaluationNamespace::membership):p.b.journal;
        store.arm(fault);
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));
        if(domain<3 || fault==Fault::write_before)CHECK(bytes(p.b.journal)==journal);
        no_traffic(p.b);++groups;
    }
    // A cancellation during a delegated journal observation after other writes
    // must still be caught before the irreversible final journal marker.
    {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();bool fired=false;
        const auto before=mutations(p.b.backend.at(0,EvaluationNamespace::membership));
        p.b.journal.callback=[&](char op){if(op=='r' && !fired && mutations(p.b.backend.at(0,EvaluationNamespace::membership))>before){fired=true;CHECK(p.b.request.cancel_exact(p.b.request.request()));}};
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));p.b.journal.callback=[](char){};
        CHECK(fired && p.b.journal_state()==EnrollmentJournalState::reconcile);++groups;
    }
    // A delegated final marker write can happen despite post-call cancellation.
    // Report failure, preserve the truthful committed public state, and require
    // a new normal rekey request before any subsequent traffic.
    {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();unsigned writes=0;bool fired=false;
        p.b.journal.callback=[&](char op){if(op=='w' && ++writes==7){fired=true;CHECK(p.b.request.cancel_exact(p.b.request.request()));}};
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));p.b.journal.callback=[](char){};
        CHECK(fired && p.b.journal_state()==EnrollmentJournalState::active_committed);no_traffic(p.b);
        p.a.restart(181);p.b.restart(251);
        CHECK(p.a.owner->start_rekey(p.a.role,17) && p.b.owner->start_rekey(p.b.role,17));
        p.activate();p.statuses();++groups;
    }
    {
        RecoveryPair p;p.one_sided();p.start();p.supply();p.compare();bool fired=false;
        p.b.binding.callback=[&](char op){if(op=='w' && !fired){fired=true;CHECK(!p.b.owner->finish_recovery_comparison(p.ar));}};
        CHECK(!p.b.owner->finish_recovery_comparison(p.ar));p.b.binding.callback=[](char){};
        CHECK(fired && p.b.journal_state()==EnrollmentJournalState::reconcile);++groups;
    }
    std::cout<<"PASS "<<groups<<" enrollment candidate recovery groups\n";
}
#endif
