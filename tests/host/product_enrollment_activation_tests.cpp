#include "product_enrollment_fixture.hpp"
using namespace product_enrollment_test;
int main(){
    unsigned groups=0;
    {ProductPair p;p.activate();CHECK(p.a.endpoint->ready()&&p.b.endpoint->ready());
     for(unsigned i=1;i<=8;++i){EvaluationRecord r{};std::uint8_t v{};auto& from=i%2?p.a:p.b;auto& to=i%2?p.b:p.a;
        CHECK(from.endpoint->send_status(i,r));CHECK(to.endpoint->receive_status(r,v)&&v==i);}
     CHECK(p.a.endpoint->close()&&p.b.endpoint->close());CHECK(p.a.endpoint->secrets_cleared()&&p.b.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.confirm();CHECK(!p.a.endpoint->commit_membership());CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.controls();p.a.membership.arm(Fault::write_before);CHECK(!p.a.endpoint->commit_membership());
     EvaluationRecord r{},before=r;CHECK(!p.a.endpoint->send_status(1,r));CHECK(same_record(r,before));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.confirm();p.a.journal.arm(Fault::write_before);EvaluationRecord r{},before=r;
     CHECK(!p.a.endpoint->next_control(r));CHECK(same_record(r,before));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.activate();p.a.membership.arm(Fault::read_error);CHECK(!p.a.endpoint->ready());CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.activate();CHECK(p.a.endpoint->close());
     ProductEnrollmentActivation restarted(p.a.random,p.a.boot,p.a.roles,p.a.tx,p.a.rx,p.a.trust,p.a.journal,p.a.membership,p.a.evidence,p.a.binding_proof,p.a.clock,p.a.port,p.a.role,p.a.key);
     CHECK(restarted.prepare_identity());EvaluationRecord old{};CHECK(!restarted.send_status(1,old));CHECK(restarted.secrets_cleared());++groups;}
    {ProductPair p;p.begin();CHECK(!p.a.endpoint->begin(*p.a.trusted));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;CHECK(!p.a.endpoint->begin(*p.b.trusted));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.a.preparation->cancel();CHECK(!p.a.endpoint->begin(*p.a.trusted));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    for(auto fault:{Fault::write_after,Fault::partial_write,Fault::sync_after,Fault::read_error}){
        ProductPair p;p.controls();p.a.membership.arm(fault,fault==Fault::read_error?3:1);
        CHECK(!p.a.endpoint->commit_membership());CHECK(p.a.endpoint->secrets_cleared());++groups;
    }
    {ProductPair p;p.confirm();CHECK(p.a.endpoint->close());EvaluationRecord r{},before=r;
     CHECK(!p.a.endpoint->next_control(r));CHECK(same_record(r,before));++groups;}
    {ProductPair p;p.activate();p.a.clock.value.now_ms=61101;EvaluationRecord r{},before=r;
     CHECK(!p.a.endpoint->send_status(1,r));CHECK(same_record(r,before));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.controls();p.a.membership.callback=[&](char){CHECK(!p.a.endpoint->ready());};
     CHECK(!p.a.endpoint->commit_membership());CHECK(p.a.endpoint->secrets_cleared());++groups;}
    for(unsigned variant=0;variant<5;++variant){
        ProductPair p;p.handshake();CHECK(p.a.endpoint->poll_confirmation());
        CHECK(p.a.port.frame.purpose==EnrollmentDisplayPurpose::transcript_confirmation);
        switch(variant){
        case 0:++p.a.port.value.display_revision;break;
        case 1:++p.a.port.value.context.request;break;
        case 2:p.a.port.value.now_ms=1100;break;
        case 3:p.a.port.value.now_ms=61101;break;
        case 4:p.a.preparation->cancel();break;
        }
        CHECK(!p.a.endpoint->poll_confirmation());CHECK(p.a.endpoint->secrets_cleared());++groups;
    }
    {ProductPair p;p.handshake();p.a.port.value.button_down=true;CHECK(p.a.endpoint->poll_confirmation());
     p.a.port.value.now_ms=2102;p.a.clock.value.now_ms=2102;p.a.port.value.button_down=false;CHECK(p.a.endpoint->poll_confirmation());
     EvaluationRecord r{};CHECK(!p.a.endpoint->next_control(r));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    for(unsigned duration:{500U,3001U}){
        ProductPair p;p.handshake();CHECK(p.a.endpoint->poll_confirmation());
        p.a.port.value.now_ms=1102;p.a.clock.value.now_ms=1102;p.a.port.value.button_down=true;CHECK(p.a.endpoint->poll_confirmation());
        p.a.port.value.now_ms+=duration;p.a.clock.value.now_ms+=duration;p.a.port.value.button_down=false;
        CHECK(!p.a.endpoint->poll_confirmation());CHECK(p.a.endpoint->secrets_cleared());++groups;
    }
    {ProductPair p;p.activate();std::uint64_t newer{};CHECK(p.a.allocator.allocate(newer)&&newer==2);
     EvaluationRecord r{},before=r;CHECK(!p.a.endpoint->send_status(1,r));CHECK(same_record(r,before));CHECK(p.a.endpoint->secrets_cleared());++groups;}
    {ProductPair p;p.activate();p.a.preparation->cancel();CHECK(!p.a.endpoint->ready());CHECK(p.a.endpoint->secrets_cleared());++groups;}
    std::cout<<"PASS "<<groups<<" product enrollment activation groups: actual crypto/handshake/control/status, durable failure and restart refusal\n";
}
