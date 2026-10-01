#define OPENTRAIL_SESSION_FIXTURE_ONLY
#include "enrollment_candidate_session_tests.cpp"
#include "opentrail/enrollment_candidate_reset.hpp"

namespace reset_test {
using Phase=DeviceFactoryResetPhase;using MarkerState=DeviceFactoryResetMarkerState;using PortError=DeviceFactoryResetPortError;
struct Marker final : DeviceFactoryResetMarkerPort {
    DeviceFactoryResetMarkerSnapshot value{PortError::none,MarkerState::absent,0};
    PortError commit_error{PortError::none},finish_error{PortError::none},consume_error{PortError::none};
    unsigned commits{},finishes{},consumes{};std::function<void(char)> callback=[](char){};
    DeviceFactoryResetMarkerSnapshot load() override {callback('r');return value;}
    DeviceFactoryResetMarkerSnapshot commit_intent_and_readback(std::uint64_t receipt) override {
        callback('c');++commits;
        if(commit_error==PortError::known_no_change)return {commit_error,MarkerState::absent,0};
        value={PortError::none,MarkerState::intent_committed,receipt};auto out=value;out.error=commit_error;return out;
    }
    DeviceFactoryResetMarkerSnapshot complete_cleanup_and_readback() override {
        callback('f');++finishes;
        if(finish_error!=PortError::none)return {finish_error,value.state,value.reset_receipt};
        value.state=value.reset_receipt?MarkerState::receipt_pending:MarkerState::absent;return value;
    }
    DeviceFactoryResetReceiptConsumeSnapshot consume_completion_receipt_and_readback() override {
        callback('x');++consumes;
        if(consume_error!=PortError::none)return {consume_error,value.reset_receipt,false};
        const auto receipt=value.reset_receipt;value={PortError::none,MarkerState::absent,0};return {PortError::none,receipt,true};
    }
};
// Explicit simulations of non-enrollment user data and the separate BLE stack.
struct OtherUser final : DeviceFactoryResetUserDomainPort {
    bool absent{},fail{};unsigned erases{};std::function<void()> callback=[]{};
    DeviceFactoryResetAbsenceSnapshot inspect_absence() override {callback();return {PortError::none,absent};}
    DeviceFactoryResetAbsenceSnapshot erase_all_and_verify_absent() override {callback();++erases;if(fail)return {};absent=true;return {PortError::none,true};}
};
struct Bonds final : DeviceFactoryResetBondDomainPort {
    bool absent{},fail{};unsigned erases{};std::function<void()> callback=[]{};
    DeviceFactoryResetAbsenceSnapshot inspect_empty() override {callback();return {PortError::none,absent};}
    DeviceFactoryResetAbsenceSnapshot erase_all_and_verify_empty() override {callback();++erases;if(fail)return {};absent=true;return {PortError::none,true};}
};
struct ResetPair : FreshPair {
    Marker marker;OtherUser other;Bonds bonds;std::optional<EnrollmentCandidateReset> reset;
    ResetPair():FreshPair(true,false) {
        a.owner.reset();construct();CHECK(reset->restore().accepted());CHECK(reset->status().phase==Phase::idle_old_state);
        bind(a.owner);a.start();b.start();CHECK(a.owner->receive_candidate(b.candidate));CHECK(b.owner->receive_candidate(a.candidate));
    }
    ~ResetPair(){a.owner.reset();}
    void construct(){reset.emplace(a.identity,a.journal,a.binding,a.boot,a.backend,4,marker,other,bonds);}
    void bind(std::optional<EnrollmentCandidateSession>& out){CHECK(reset->emplace_session(out,a.request,a.authority,a.device,a.random,41));}
    bool all_absent() {
        for(auto* store:{&a.identity,&a.journal,&a.binding,&a.boot})for(const auto& slot:bytes(*store))for(auto byte:slot)if(byte!=0xff)return false;
        for(auto& generation:a.backend.stores)for(auto& store:generation)for(const auto& slot:bytes(store))for(auto byte:slot)if(byte!=0xff)return false;
        return true;
    }
    void seed_all() {
        std::array<std::uint8_t,64> data{};data.fill(0x5a);
        for(auto* store:{&a.identity,&a.journal,&a.binding,&a.boot})for(unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s)store->inner.memory.seed_slot(static_cast<Domain>(d),s,data);
        for(auto& generation:a.backend.stores)for(auto& store:generation)for(unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s)store.inner.memory.seed_slot(static_cast<Domain>(d),s,data);
    }
};
unsigned related_mutations(ResetPair& p) {
    unsigned count=mutations(p.a.journal)+mutations(p.a.binding)+mutations(p.a.boot);
    for(const auto& generation:p.a.backend.stores)for(const auto& store:generation)count+=mutations(store);
    return count;
}
}
void activate_new_pair(EnrollmentCandidateSession& a,FreshNode& local,FreshNode& b) {
 CHECK(a.start(local.role,17));b.start();
 EvaluationEnrollmentCandidate candidate{};CHECK(a.export_candidate(candidate));
 CHECK(a.receive_candidate(b.candidate));CHECK(b.owner->receive_candidate(candidate));
 auto review=[](EnrollmentCandidateSession& s,FreshNode& n,EvaluationEnrollmentOffer& offer){
  CHECK(s.show_peer());CHECK(s.poll_review());++n.authority.value.now_ms;n.device.value.button_down=true;CHECK(s.poll_review());
  n.authority.value.now_ms+=1000;n.device.value.button_down=false;CHECK(s.poll_review());CHECK(s.finish_review(offer));};
 EvaluationEnrollmentOffer ao{},bo{};review(a,local,ao);review(*b.owner,b,bo);
 std::array<std::uint8_t,64> as{},bs{};
 CHECK(a.prepare_possession(bo,as));CHECK(b.owner->prepare_possession(ao,bs));
 CHECK(a.accept_possession(bs));CHECK(b.owner->accept_possession(as));
 EvaluationEnrollmentClockMark am{},bm{};CHECK(a.clock_mark(am));CHECK(b.owner->clock_mark(bm));
 CHECK(a.receive_clock_mark(bm));CHECK(b.owner->receive_clock_mark(am));
 IndependentInvitation invite{};CHECK(a.issue_invitation(invite));
 CHECK(a.sign_invitation(invite,as));CHECK(b.owner->sign_invitation(invite,bs));
 CHECK(a.begin_handshake(bs));CHECK(b.owner->begin_handshake(as));
 auto handshake=[](EnrollmentCandidateSession& from,EnrollmentCandidateSession& to){HandshakeFrame frame{};CHECK(from.next_handshake(frame));CHECK(to.receive_handshake(frame));};
 handshake(a,*b.owner);handshake(*b.owner,a);handshake(a,*b.owner);
 auto confirm=[](EnrollmentCandidateSession& s,FreshNode& n){CHECK(s.poll_confirmation());++n.authority.value.now_ms;n.device.value.button_down=true;CHECK(s.poll_confirmation());n.authority.value.now_ms+=1000;n.device.value.button_down=false;CHECK(s.poll_confirmation());};
 confirm(a,local);confirm(*b.owner,b);
 auto control=[](EnrollmentCandidateSession& from,EnrollmentCandidateSession& to){EvaluationRecord frame{};CHECK(from.next_control(frame));CHECK(to.receive_control(frame));};
 control(a,*b.owner);control(*b.owner,a);control(a,*b.owner);control(*b.owner,a);
 CHECK(a.commit());CHECK(b.owner->commit());CHECK(a.ready());CHECK(b.owner->ready());
 EvaluationRecord frame{};std::uint8_t value=0;CHECK(a.send_status(1,frame));CHECK(b.owner->receive_status(frame,value));CHECK(value==1);
}

#ifndef OPENTRAIL_RESET_FIXTURE_ONLY
using namespace reset_test;
int main() {
    unsigned groups=0;
    for(auto receipt:{std::uint64_t{0},std::uint64_t{83}}) {
        ResetPair p;p.activate();const auto epoch=p.reset->incarnation();
        CHECK(p.reset->begin_local_reset(*p.a.owner,receipt).accepted());CHECK(p.a.owner->quiescent());
        CHECK(p.reset->incarnation()!=epoch && p.marker.value.state==MarkerState::intent_committed);
        CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent() && p.other.absent && p.bonds.absent);
        CHECK(p.reset->status().phase==Phase::reboot_unowned_permitted);CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));
        p.a.owner.reset();p.construct();CHECK(p.reset->restore().accepted());
        if(receipt){CHECK(p.reset->status().phase==Phase::completion_receipt_pending);CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));CHECK(p.reset->consume_completion_receipt().accepted());}
        CHECK(p.reset->status().phase==Phase::idle_unowned);CHECK(p.reset->enrollment_allowed(p.reset->incarnation()));
        // Unowned reset status never supplies a new phone-authorized request.
        const auto identity_writes=mutations(p.a.identity);p.bind(p.a.owner);CHECK(!p.a.owner->start(p.a.role,17));CHECK(mutations(p.a.identity)==identity_writes);++groups;
    }
    {
        ResetPair p;CHECK(p.a.owner->close());p.seed_all();CHECK(p.reset->begin_local_reset(*p.a.owner).accepted());
        CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent());
        for(auto& generation:p.a.backend.stores)for(auto& store:generation)for(unsigned d=0;d<5;++d)CHECK(store.inner.memory.counters(static_cast<Domain>(d)).erases>=2);
        ++groups;
    }
    for(auto error:{PortError::known_no_change,PortError::uncertain}) {
        ResetPair p,ordinary_close;p.activate();ordinary_close.activate();CHECK(ordinary_close.a.owner->close());p.marker.commit_error=error;
        CHECK(!p.reset->begin_local_reset(*p.a.owner,9).accepted());CHECK(p.a.owner->quiescent());
        // Compare all domains against equivalent ordinary close, including its
        // mandatory RX/activation retirement, not an unsafe no-write teardown.
        CHECK(bytes(p.a.identity)==bytes(ordinary_close.a.identity) && bytes(p.a.journal)==bytes(ordinary_close.a.journal));
        CHECK(bytes(p.a.binding)==bytes(ordinary_close.a.binding) && bytes(p.a.boot)==bytes(ordinary_close.a.boot));
        for(unsigned g=0;g<5;++g)for(unsigned n=0;n<7;++n)CHECK(bytes(p.a.backend.stores[g][n])==bytes(ordinary_close.a.backend.stores[g][n]));
        CHECK(p.reset->status().phase==(error==PortError::known_no_change?Phase::idle_old_state:Phase::reconciliation_required));++groups;
    }
    for(auto fault:{Fault::erase_before,Fault::sync_after,Fault::read_error}) {
        ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner).accepted());const auto before=related_mutations(p);
        p.a.identity.arm(fault);CHECK(!p.reset->continue_cleanup().accepted());CHECK(related_mutations(p)==before);
        CHECK(p.marker.value.state==MarkerState::intent_committed && p.reset->status().phase==Phase::cleanup_required);
        p.a.identity.inner.clear();CHECK(p.reset->continue_cleanup().accepted() && p.all_absent());++groups;
    }
    {
        ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner).accepted());const auto before=related_mutations(p);unsigned reads=0;
        p.a.identity.callback=[&](char op){if(op=='r' && ++reads==11){std::array<std::uint8_t,64> value{};p.a.identity.inner.memory.seed_slot(Domain::outbound_counter_state,0,value);}};
        CHECK(!p.reset->continue_cleanup().accepted());CHECK(reads>=11 && related_mutations(p)==before);++groups;
    }
    for(unsigned part=0;part<4;++part) {
        ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner,44).accepted());
        if(part==0)p.a.backend.at(4,EvaluationNamespace::enrollment).arm(Fault::erase_after);
        if(part==1)p.other.fail=true;
        if(part==2)p.bonds.fail=true;
        if(part==3)p.marker.finish_error=PortError::uncertain;
        CHECK(!p.reset->continue_cleanup().accepted());CHECK(p.reset->status().phase==Phase::cleanup_required);
        CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));
        p.a.backend.at(4,EvaluationNamespace::enrollment).inner.clear();p.other.fail=p.bonds.fail=false;p.marker.finish_error=PortError::none;
        CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent());++groups;
    }
    {
        ResetPair p;p.activate();p.a.device.release_ok=false;
        CHECK(p.reset->begin_local_reset(*p.a.owner).accepted());CHECK(p.a.owner->quiescent());CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent());++groups;
    }
    {
        ResetPair p;bool fired=false;const auto before=related_mutations(p);
        p.a.device.callback=[&]{if(!fired){fired=true;CHECK(!p.reset->begin_local_reset(*p.a.owner).accepted());CHECK(!p.reset->continue_cleanup().accepted());}};
        CHECK(!p.a.owner->show_peer());CHECK(fired && p.a.owner->quiescent());CHECK(p.marker.commits==0 && related_mutations(p)==before);++groups;
    }
    {
        ResetPair p;p.activate();CHECK(p.b.owner->close());CHECK(!p.reset->begin_local_reset(*p.b.owner).accepted());
        CHECK(p.a.owner->ready() && p.marker.commits==0);CHECK(!p.reset->begin_after_quiescence().accepted());++groups;
    }
    {
        ResetPair p;std::optional<EnrollmentCandidateSession> duplicate;
        CHECK(!p.reset->emplace_session(duplicate,p.a.request,p.a.authority,p.a.device,p.a.random,41));CHECK(!duplicate);++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner,23).accepted());bool fired=false;
        p.a.identity.callback=[&](char op){if(op=='e' && !fired){fired=true;
            if(variant==0)++p.marker.value.reset_receipt;
            if(variant==1)p.marker.value.state=MarkerState::absent;
            if(variant==2)CHECK(!p.reset->continue_cleanup().accepted());}};
        const auto before=related_mutations(p);CHECK(!p.reset->continue_cleanup().accepted());
        CHECK(fired && related_mutations(p)==before && p.marker.finishes==0);++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        ResetPair p;p.activate();bool fired=false;
        p.a.identity.callback=[&](char op){if(op=='r' && !fired){fired=true;p.marker.value={PortError::none,MarkerState::intent_committed,71};}};
        EvaluationRecord out{};out.group=84;CHECK(!p.a.owner->send_status(1,out));CHECK(out.group==84 && fired && p.a.owner->quiescent());
        if(variant==1)p.marker.value.state=MarkerState::absent;
        if(variant==2)p.marker.value={PortError::none,MarkerState::absent,0};
        CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner,91).accepted());CHECK(p.reset->continue_cleanup().accepted());
        p.a.owner.reset();p.construct();CHECK(p.reset->restore().accepted());
        if(variant==0)p.other.absent=false;
        if(variant==1)p.bonds.absent=false;
        if(variant==2)p.marker.consume_error=PortError::uncertain;
        CHECK(!p.reset->consume_completion_receipt().accepted());CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));
        CHECK(p.marker.value.state==MarkerState::receipt_pending);++groups;
    }
    {
        ResetPair p;p.activate();CHECK(p.a.owner->close());p.marker.value={PortError::none,MarkerState::receipt_pending,99};
        p.a.owner.reset();p.construct();CHECK(p.reset->restore().accepted());CHECK(p.reset->status().phase==Phase::cleanup_required);
        CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent() && p.marker.value.reset_receipt==99);++groups;
    }
    {
        ResetPair p;CHECK(p.a.owner->close());p.a.owner.reset();p.marker.value.state=MarkerState::invalid;p.construct();
        CHECK(!p.reset->restore().accepted());CHECK(!p.reset->begin_after_quiescence().accepted());
        CHECK(p.reset->begin_confirmed_recovery_after_quiescence().accepted());CHECK(p.reset->continue_cleanup().accepted());CHECK(p.all_absent());++groups;
    }
    for(auto capacity:{std::uint64_t{0},std::numeric_limits<std::uint64_t>::max()}) {
        ResetPair p;CHECK(p.a.owner->close());p.a.owner.reset();p.reset.reset();
        p.reset.emplace(p.a.identity,p.a.journal,p.a.binding,p.a.boot,p.a.backend,capacity,p.marker,p.other,p.bonds);
        CHECK(!p.reset->restore().accepted() && !p.reset->begin_confirmed_recovery_after_quiescence().accepted());CHECK(p.marker.commits==0);++groups;
    }
 {
  ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner).accepted());CHECK(p.reset->continue_cleanup().accepted());
  CHECK(p.all_absent());
  // Preserve both old session and its blocked gate while a NEW gate owns the reset data.
  EnrollmentCandidateReset next(p.a.identity,p.a.journal,p.a.binding,p.a.boot,p.a.backend,4,p.marker,p.other,p.bonds);
  CHECK(next.restore().accepted() && next.status().phase==Phase::idle_unowned);
  ++p.a.authority.value.context.runtime;p.a.refill(117);p.a.admit();
  std::optional<EnrollmentCandidateSession> fresh;
  CHECK(next.emplace_session(fresh,p.a.request,p.a.authority,p.a.device,p.a.random,41));
  FreshNode peer(InvitationRole::responder,213,80100,true);
  activate_new_pair(*fresh,p.a,peer);
  GenerationLedgerStorage ledger(p.a.backend);PeerMembershipStore allocation(ledger);PeerMembership generation{};
  CHECK(allocation.initialize() && allocation.read(generation) && generation.generation==1);
  const auto member=bytes(p.a.backend.at(0,EvaluationNamespace::membership));
  const auto evidence=bytes(p.a.backend.at(0,EvaluationNamespace::enrollment)),binding=bytes(p.a.binding);
  const auto stale=p.a.owner->revoke_local_membership();
  CHECK(stale.membership==EnrollmentContainmentState::unavailable);
  CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::membership))==member);
  CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::enrollment))==evidence && bytes(p.a.binding)==binding);
  CHECK(fresh->ready());fresh.reset();
  ++groups;
 }
 {
  ResetPair p;p.activate();CHECK(p.reset->begin_local_reset(*p.a.owner,91).accepted());CHECK(p.reset->continue_cleanup().accepted());
  p.a.owner.reset();p.construct();CHECK(p.reset->restore().accepted() && p.reset->status().phase==Phase::completion_receipt_pending);
  std::array<std::uint8_t,64> orphan{};orphan.fill(0x67);
  p.a.backend.at(4,EvaluationNamespace::enrollment).inner.memory.seed_slot(Domain::outbound_counter_state,1,orphan);
  CHECK(!p.reset->consume_completion_receipt().accepted());CHECK(p.marker.consumes==0 && p.marker.value.reset_receipt==91);
  CHECK(!p.reset->enrollment_allowed(p.reset->incarnation()));
  p.construct();CHECK(p.reset->restore().accepted() && p.reset->status().phase==Phase::cleanup_required);
  CHECK(p.reset->continue_cleanup().accepted() && p.all_absent());
  CHECK(p.marker.value.state==MarkerState::receipt_pending && p.marker.value.reset_receipt==91);
  p.construct();CHECK(p.reset->restore().accepted());CHECK(p.reset->consume_completion_receipt().accepted());
  CHECK(p.reset->status().phase==Phase::idle_unowned);
  ++groups;
 }
    std::cout<<"PASS "<<groups<<" enrollment candidate reset groups\n";
}
#endif
