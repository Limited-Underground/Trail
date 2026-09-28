#include "product_enrollment_fixture.hpp"
using namespace product_enrollment_test;
static CallbackStorage& durable(Node& n,unsigned kind){
 switch(kind){case 0:return n.evidence;case 1:return n.binding_proof;case 2:return n.membership;default:return n.journal;}
}
static void inactive(Node& n){
 EvaluationRecord r{},saved=r;CHECK(!n.endpoint->send_status(1,r) && same_record(r,saved));
 CHECK(n.endpoint->secrets_cleared());
}
static void restart_never_resumes(Node& n){
 // Fresh session backing with the exact durable membership artifacts: restart
 // may recognize a fully committed record, but must never inherit traffic keys.
 CallbackStorage boot,roles,tx,rx,trust;security::test_support::FakeSecureRandomSource random;entropy(random,191);
 ProductEnrollmentActivation restarted(random,boot,roles,tx,rx,trust,n.journal,n.membership,n.evidence,n.binding_proof,n.clock,n.port,n.role,n.key);
 (void)restarted.prepare_identity();EvaluationRecord record{},saved=record;
 CHECK(!restarted.send_status(1,record) && same_record(record,saved) && restarted.secrets_cleared());
}
int main(){
 unsigned groups=0;
 {ProductPair p;p.activate();
  for(unsigned epoch=2;epoch<=3;++epoch){
   p.restart_and_compare();p.authorize(epoch);p.activate();
   EvaluationRecord r{};std::uint8_t output{};
   CHECK(p.a.endpoint->send_status(7,r));CHECK(p.b.endpoint->receive_status(r,output)&&output==7);
  }
  ++groups;}
 {ProductPair p;p.activate();const auto old_a=p.a.endpoint->public_identity(),old_b=p.b.endpoint->public_identity();
  p.restart_and_compare();CHECK(p.a.endpoint->public_identity()!=old_a && p.b.endpoint->public_identity()!=old_b);
  p.authorize(2);p.activate();CHECK(p.a.endpoint->ready() && p.b.endpoint->ready());
  for(unsigned i=1;i<=8;++i){EvaluationRecord r{};std::uint8_t v{};auto& from=i%2?p.a:p.b;auto& to=i%2?p.b:p.a;
   CHECK(from.endpoint->send_status(i,r));CHECK(to.endpoint->receive_status(r,v)&&v==i);}
  EnrollmentCommitCoordinator journal(p.a.journal);CHECK(journal.initialize());EnrollmentCommitContext context{};
  CHECK(journal.read_public(context) && context.epoch==2 && context.session_generation==2);
  PeerMembershipStore member(p.a.membership);CHECK(member.initialize());PeerMembership retained{};
  CHECK(member.read(retained) && retained.generation==2 && retained.state==MembershipState::active);
  ++groups;}
 {ProductPair p;p.activate();EvaluationRecord old{};CHECK(p.a.endpoint->send_status(1,old));
  p.restart_and_compare();p.authorize(2);p.activate();std::uint8_t output=99;
  CHECK(!p.b.endpoint->receive_status(old,output) && output==99 && p.b.endpoint->secrets_cleared());++groups;}
 for(bool reset:{false,true}){
  ProductPair p;p.activate();p.restart_and_compare();p.authorize(2);p.activate();
  CHECK(reset?p.a.endpoint->prepare_reset():p.a.endpoint->revoke());inactive(p.a);restart_never_resumes(p.a);++groups;
 }
 for(unsigned missing=0;missing<4;++missing){
  ProductPair p;p.activate();p.restart_and_compare();p.authorize(2);p.confirm();
  if(missing>0)ProductPair::control(p.a,p.b);
  if(missing>1)ProductPair::control(p.b,p.a);
  if(missing>2)ProductPair::control(p.a,p.b);
  CHECK(!p.a.endpoint->commit_membership());inactive(p.a);restart_never_resumes(p.a);++groups;
 }
 for(unsigned variant=0;variant<6;++variant){
  ProductPair p;p.activate();restart_node(p.a,p.a.key,151);restart_node(p.b,p.a.key,211);
  RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
  CHECK(p.a.comparison->begin(ac) && p.b.comparison->begin(bc));
  CHECK(p.a.comparison->sign(bc,ar) && p.b.comparison->sign(ac,br));
  switch(variant){
   case 0:br.signature[0]^=1;break;
   case 1:++br.initiator.context.generation;break;
   case 2:br.responder.challenge[0]^=1;break;
   case 3:p.a.comparison->cancel();break;
   case 4:++p.a.port.value.context.request;break;
   case 5:CHECK(p.a.comparison->verify(br,p.a.compared));break;
  }
  CHECK(!p.a.comparison->verify(br,p.a.compared));CHECK(!p.a.comparison->live());
  if(p.a.compared){
   p.a.preparation.emplace(p.a.random,p.a.identity,p.a.allocator,p.a.port,std::move(*p.a.compared));
   std::array<std::uint8_t,32> challenge{};CHECK(!p.a.preparation->prepare_challenge(challenge));
  }
  inactive(p.a);++groups;
 }
 // Every durable commit write/sync position, including failures after mutation.
 // Read failures cover entry and final snapshot; store unit suites exhaust reads.
 for(unsigned kind=0;kind<4;++kind){
  ProductPair baseline;baseline.activate();baseline.restart_and_compare();baseline.authorize(2);baseline.controls();
  auto& control=durable(baseline.a,kind);control.inner.clear();CHECK(baseline.a.endpoint->commit_membership());
  for(auto fault:{Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after,
                  Fault::erase_before,Fault::erase_after,Fault::read_error,Fault::short_read,Fault::corrupt_read}){
   const auto count=control.inner.count(operation(fault));
   for(unsigned nth=1;nth<=count;++nth){
    if(operation(fault)=='r' && nth!=1 && nth!=count)continue;
    ProductPair p;p.activate();p.restart_and_compare();p.authorize(2);p.controls();
    auto& storage=durable(p.a,kind);storage.arm(fault,nth);
    CHECK(!p.a.endpoint->commit_membership());inactive(p.a);
    storage.inner.clear();restart_never_resumes(p.a);++groups;
   }
  }
 }
 std::cout<<"PASS "<<groups<<" product enrollment rekey groups: real reconnect/status, stale packets, comparison replay, containment, commit interruption\n";
}
