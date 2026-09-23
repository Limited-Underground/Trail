#pragma once
#include "security_peer_traffic_fixture.hpp"
#include "opentrail/product_enrollment_activation.hpp"
using namespace peer_traffic_test;
namespace product_enrollment_test {
struct Backend final : EvaluationGenerationBackend {
    std::array<std::array<CallbackStorage,7>,5> stores;
    CallbackStorage& at(std::uint64_t g,EvaluationNamespace n){CHECK(g<5);return stores[g][static_cast<unsigned>(n)];}
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
    CallbackStorage identity_storage,boot,roles,tx,rx,trust,journal,membership,evidence,binding_proof;
    security::test_support::FakeSecureRandomSource random,identity_random;
    EnrollmentIdentityStore identity{identity_storage,identity_random};
    Backend backend; GenerationLedgerStorage ledger{backend}; SessionGenerationAllocator allocator{ledger,backend,4};
    Source clock; Port port; InvitationKey key{}; InvitationRole role;
    std::uint64_t generation{};
    std::optional<ProductEnrollmentActivation> endpoint;
    std::optional<ReviewedEnrollmentIdentity> reviewed;
    std::optional<EnrollmentPreparationOwner> preparation;
    std::optional<TrustedEnrollmentBinding> trusted;
    std::optional<EnrollmentRetainedStateOwner> comparison;
    std::optional<ComparedRetainedEnrollment> compared;
    Node(InvitationRole r,unsigned seed):role(r){
        entropy(random,seed);entropy(identity_random,seed+40);CHECK(identity.initialize());CHECK(identity.public_key(key));
        CHECK(allocator.initialize());CHECK(allocator.allocate(generation));clock.value={{seed+1,seed+2},1101};
    }
    void prepare(const InvitationKey& signer,const InvitationKey& peer){
        endpoint.emplace(random,boot,roles,tx,rx,trust,journal,membership,evidence,binding_proof,clock,port,role,signer);
        if(role==InvitationRole::responder){InvitationBootAuthority previous(boot);CHECK(previous.start());}
        CHECK(endpoint->prepare_identity());
        port.value.context={endpoint->boot_context(),generation,1};port.value.now_ms=100;
        EnrollmentFingerprintReview review(port,key,role,17);CHECK(review.begin(peer));CHECK(review.show_peer());CHECK(review.poll());
        port.value.now_ms=101;port.value.button_down=true;CHECK(review.poll());
        port.value.now_ms=1101;port.value.button_down=false;CHECK(review.poll());CHECK(review.take_review(reviewed));
        preparation.emplace(random,identity,allocator,port,std::move(*reviewed));
    }
};
inline void restart_node(Node& n,const InvitationKey& signer,unsigned seed) {
    CHECK(n.endpoint->close());n.trusted.reset();n.preparation.reset();n.compared.reset();n.comparison.reset();n.endpoint.reset();
    CHECK(n.allocator.allocate(n.generation));entropy(n.random,seed+static_cast<unsigned>(n.generation)*7);
    auto& boot=n.boot;
    auto& roles=n.backend.at(n.generation,EvaluationNamespace::role);
    auto& tx=n.backend.at(n.generation,EvaluationNamespace::transmit);
    auto& rx=n.backend.at(n.generation,EvaluationNamespace::receive);
    auto& activation=n.backend.at(n.generation,EvaluationNamespace::activation);
    n.clock.value.now_ms=1101;
    n.endpoint.emplace(n.random,boot,roles,tx,rx,activation,n.journal,n.membership,n.evidence,n.binding_proof,n.clock,n.port,n.role,signer);
    CHECK(n.endpoint->prepare_identity());n.port.value.context={n.endpoint->boot_context(),n.generation,n.generation};
    n.port.value.now_ms=1101;n.port.value.button_down=false;n.clock.value.now_ms=1101;
    CHECK(n.endpoint->prepare_retained_comparison(n.random,n.identity,n.allocator,n.comparison));
}
struct ProductPair {
    Node a{InvitationRole::initiator,1},b{InvitationRole::responder,81};
    ProductPair(){
        a.prepare(a.key,b.key);b.prepare(a.key,a.key);authorize(1);
    }
    void authorize(unsigned epoch){
        RetainedEnrollmentIdentities pins{a.key,b.key};EnrollmentPossessionStatement statement{};statement.group=17;
        statement.initiator_context={a.endpoint->boot_context(),a.generation,a.port.value.context.request};
        statement.responder_context={b.endpoint->boot_context(),b.generation,b.port.value.context.request};
        CHECK(a.preparation->prepare_challenge(statement.initiator_challenge));
        CHECK(b.preparation->prepare_challenge(statement.responder_challenge));
        EnrollmentPossessionProof proof{};proof.statement=statement;EnrollmentPossessionAttempt ap(pins,statement),bp(pins,statement);
        CHECK(ap.sign(InvitationRole::initiator,a.identity,proof.initiator_signature));
        CHECK(bp.sign(InvitationRole::responder,b.identity,proof.responder_signature));
        std::optional<VerifiedEnrollmentPossession> av,bv;CHECK(ap.verify(proof,av));CHECK(bp.verify(proof,bv));
        CHECK(a.preparation->accept_possession(*av));CHECK(b.preparation->accept_possession(*bv));
        IndependentInvitationFields f{};f.group=17;f.epoch=epoch;f.signer=a.key;
        f.peer_a=a.endpoint->public_identity();f.peer_b=b.endpoint->public_identity();
        f.boot_a=a.endpoint->boot_context();f.boot_b=b.endpoint->boot_context();
        f.nonce.fill(static_cast<unsigned char>(epoch+2));f.issued_a_ms=1101;f.issued_b_ms=1101;f.window_a_ms=60000;f.window_b_ms=60000;
        EnrollmentIdentityProof binding{};CHECK(a.preparation->issue_invitation(f,binding.invitation));
        CHECK(a.preparation->sign_binding(binding.invitation,binding.initiator_signature));
        CHECK(b.preparation->sign_binding(binding.invitation,binding.responder_signature));
        CHECK(a.preparation->authorize(binding,a.trusted));CHECK(b.preparation->authorize(binding,b.trusted));
    }
    void restart_and_compare(){
        restart_node(a,a.key,151);restart_node(b,a.key,211);
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        CHECK(a.comparison->begin(ac));CHECK(b.comparison->begin(bc));
        CHECK(a.comparison->sign(bc,ar));CHECK(b.comparison->sign(ac,br));
        CHECK(a.comparison->verify(br,a.compared));CHECK(b.comparison->verify(ar,b.compared));
        a.preparation.emplace(a.random,a.identity,a.allocator,a.port,std::move(*a.compared));
        b.preparation.emplace(b.random,b.identity,b.allocator,b.port,std::move(*b.compared));
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
