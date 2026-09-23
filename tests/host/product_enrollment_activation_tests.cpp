#include "security_peer_traffic_fixture.hpp"
#include "opentrail/product_enrollment_activation.hpp"
using namespace peer_traffic_test;
namespace {
struct Backend final : EvaluationGenerationBackend {
    std::array<std::array<CallbackStorage,7>,3> stores;
    CallbackStorage& at(std::uint64_t g,EvaluationNamespace n){CHECK(g<3);return stores[g][static_cast<unsigned>(n)];}
    persistence::StorageReadResult read(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s,persistence::MutableStorageByteView v) override{return at(g,n).read_slot(d,s,v);}
    Error erase(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s) override{return at(g,n).erase_slot(d,s);}
    Error write(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s,std::size_t o,persistence::StorageByteView v) override{return at(g,n).write_slot(d,s,o,v);}
    Error sync(std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s) override{return at(g,n).sync_slot(d,s);}
};
struct Port final : FingerprintReviewPort {
    FingerprintReviewSample value{};
    FingerprintReviewFrame frame{};
    FingerprintReviewSample sample() override{return value;}
    bool show(const FingerprintReviewFrame& f) override{frame=f;value.display_revision=f.revision;return true;}
};
void entropy(security::test_support::FakeSecureRandomSource& random,unsigned seed) {
    std::array<unsigned char,128> bytes{};
    for(unsigned i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(seed+i);
    CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);
}
struct Node {
    CallbackStorage identity_storage,boot,roles,tx,rx,trust,journal,membership;
    security::test_support::FakeSecureRandomSource random,identity_random;
    EnrollmentIdentityStore identity{identity_storage,identity_random};
    Backend backend; GenerationLedgerStorage ledger{backend}; SessionGenerationAllocator allocator{ledger,backend,2};
    Source clock; Port port; InvitationKey key{}; InvitationRole role;
    std::uint64_t generation{};
    std::optional<ProductEnrollmentActivation> endpoint;
    std::optional<ReviewedEnrollmentIdentity> reviewed;
    std::optional<EnrollmentPreparationOwner> preparation;
    std::optional<TrustedEnrollmentBinding> trusted;
    Node(InvitationRole r,unsigned seed):role(r){
        entropy(random,seed);entropy(identity_random,seed+40);CHECK(identity.initialize());CHECK(identity.public_key(key));
        CHECK(allocator.initialize());CHECK(allocator.allocate(generation));clock.value={{seed+1,seed+2},1101};
    }
    void prepare(const InvitationKey& signer,const InvitationKey& peer){
        endpoint.emplace(random,boot,roles,tx,rx,trust,journal,membership,clock,port,role,signer);
        if(role==InvitationRole::responder){InvitationBootAuthority previous(boot);CHECK(previous.start());}
        CHECK(endpoint->prepare_identity());
        port.value.context={endpoint->boot_context(),generation,1};port.value.now_ms=100;
        EnrollmentFingerprintReview review(port,key,role,17);CHECK(review.begin(peer));CHECK(review.show_peer());CHECK(review.poll());
        port.value.now_ms=101;port.value.button_down=true;CHECK(review.poll());
        port.value.now_ms=1101;port.value.button_down=false;CHECK(review.poll());CHECK(review.take_review(reviewed));
        preparation.emplace(random,identity,allocator,port,std::move(*reviewed));
    }
};
struct ProductPair {
    Node a{InvitationRole::initiator,1},b{InvitationRole::responder,81};
    ProductPair(){
        a.prepare(a.key,b.key);b.prepare(a.key,a.key);
        RetainedEnrollmentIdentities pins{a.key,b.key};EnrollmentPossessionStatement statement{};statement.group=17;
        statement.initiator_context={a.endpoint->boot_context(),a.generation,1};
        statement.responder_context={b.endpoint->boot_context(),b.generation,1};
        CHECK(a.preparation->prepare_challenge(statement.initiator_challenge));
        CHECK(b.preparation->prepare_challenge(statement.responder_challenge));
        EnrollmentPossessionProof proof{};proof.statement=statement;EnrollmentPossessionAttempt ap(pins,statement),bp(pins,statement);
        CHECK(ap.sign(InvitationRole::initiator,a.identity,proof.initiator_signature));
        CHECK(bp.sign(InvitationRole::responder,b.identity,proof.responder_signature));
        std::optional<VerifiedEnrollmentPossession> av,bv;CHECK(ap.verify(proof,av));CHECK(bp.verify(proof,bv));
        CHECK(a.preparation->accept_possession(*av));CHECK(b.preparation->accept_possession(*bv));
        IndependentInvitationFields f{};f.group=17;f.epoch=1;f.signer=a.key;
        f.peer_a=a.endpoint->public_identity();f.peer_b=b.endpoint->public_identity();
        f.boot_a=a.endpoint->boot_context();f.boot_b=b.endpoint->boot_context();
        f.nonce.fill(3);f.issued_a_ms=1101;f.issued_b_ms=1101;f.window_a_ms=60000;f.window_b_ms=60000;
        EnrollmentIdentityProof binding{};CHECK(a.preparation->issue_invitation(f,binding.invitation));
        CHECK(a.preparation->sign_binding(binding.invitation,binding.initiator_signature));
        CHECK(b.preparation->sign_binding(binding.invitation,binding.responder_signature));
        CHECK(a.preparation->authorize(binding,a.trusted));CHECK(b.preparation->authorize(binding,b.trusted));
    }
    void begin(){CHECK(a.endpoint->begin(*a.trusted));CHECK(b.endpoint->begin(*b.trusted));}
    void handshake(){begin();auto transfer=[](Node& from,Node& to){HandshakeFrame frame{};CHECK(from.endpoint->next_handshake(frame));CHECK(to.endpoint->receive_handshake(frame));};transfer(a,b);transfer(b,a);transfer(a,b);}
    void confirm(){handshake();for(auto* n:{&a,&b}){CHECK(n->endpoint->poll_confirmation());
        n->port.value.now_ms=1102;n->port.value.button_down=true;n->clock.value.now_ms=1102;CHECK(n->endpoint->poll_confirmation());
        n->port.value.now_ms=2102;n->port.value.button_down=false;n->clock.value.now_ms=2102;CHECK(n->endpoint->poll_confirmation());}}
    static void control(Node& from,Node& to){EvaluationRecord r{};CHECK(from.endpoint->next_control(r));CHECK(to.endpoint->receive_control(r));}
    void controls(){confirm();control(a,b);control(b,a);control(a,b);control(b,a);}
    void activate(){controls();CHECK(a.endpoint->commit_membership());CHECK(b.endpoint->commit_membership());}
};
}
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
     ProductEnrollmentActivation restarted(p.a.random,p.a.boot,p.a.roles,p.a.tx,p.a.rx,p.a.trust,p.a.journal,p.a.membership,p.a.clock,p.a.port,p.a.role,p.a.key);
     CHECK(!restarted.prepare_identity());CHECK(restarted.secrets_cleared());++groups;}
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
