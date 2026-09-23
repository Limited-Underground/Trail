#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_retained_state.hpp"
#include <type_traits>
using namespace product_enrollment_test;
namespace {
struct RetainedNode {
    Node& node;
    EnrollmentCommitCoordinator journal;
    PeerMembershipStore membership;
    EnrollmentEvidenceStore evidence;
    EnrollmentBindingStore bindings;
    EnrollmentRetainedStateOwner owner;
    RetainedEnrollmentChallenge challenge;
    RetainedEnrollmentResponse response;
    std::optional<ComparedRetainedEnrollment> receipt;
    RetainedNode(Node& n):node(n),journal(n.journal),membership(n.membership),evidence(n.evidence),bindings(n.binding_proof),
        owner(n.random,journal,membership,evidence,bindings,n.identity,n.allocator,n.port,n.role){
        CHECK(n.endpoint->close());CHECK(n.allocator.allocate(n.generation));
        InvitationBootAuthority boot(n.boot);CHECK(boot.start());
        n.port.value.context={boot.context(),n.generation,2};n.port.value.now_ms=3000;
        entropy(n.random,n.role==InvitationRole::initiator?11:91);
        CHECK(journal.initialize());CHECK(membership.initialize());CHECK(evidence.initialize());CHECK(bindings.initialize());
    }
};
struct RetainedPair {
    ProductPair pair;
    std::optional<RetainedNode> a,b;
    RetainedPair(){pair.activate();a.emplace(pair.a);b.emplace(pair.b);}
    void begin(){CHECK(a->owner.begin(a->challenge));CHECK(b->owner.begin(b->challenge));}
    void sign(){begin();CHECK(a->owner.sign(b->challenge,a->response));CHECK(b->owner.sign(a->challenge,b->response));}
    void verify(){sign();CHECK(a->owner.verify(b->response,a->receipt));CHECK(b->owner.verify(a->response,b->receipt));}
};
}
int main(){
    static_assert(!std::is_default_constructible_v<ComparedRetainedEnrollment>);
    static_assert(!std::is_copy_constructible_v<ComparedRetainedEnrollment>);
    static_assert(std::is_move_constructible_v<ComparedRetainedEnrollment>);
    unsigned groups=0;
    {RetainedPair p;p.verify();CHECK(p.a->receipt && p.b->receipt);CHECK(p.a->owner.current() && p.b->owner.current());
     CHECK(p.a->receipt->prior().identities().initiator==p.pair.a.key);CHECK(p.a->receipt->context()==p.a->challenge.context);
     CHECK(p.a->receipt->deadline()==63000);++groups;}
    {RetainedPair p;p.sign();auto bad=p.b->response;bad.signature[0]^=1;CHECK(!p.a->owner.verify(bad,p.a->receipt));CHECK(!p.a->receipt);++groups;}
    {RetainedPair p;p.sign();auto bad=p.b->response;++bad.initiator.context.request;CHECK(!p.a->owner.verify(bad,p.a->receipt));CHECK(!p.a->receipt);++groups;}
    {RetainedPair p;p.sign();CHECK(!p.a->owner.verify(p.a->response,p.a->receipt));CHECK(!p.a->receipt);++groups;}
    {RetainedPair p;p.sign();CHECK(p.a->membership.revoke());CHECK(!p.a->owner.verify(p.b->response,p.a->receipt));CHECK(!p.a->receipt);++groups;}
    {RetainedPair p;p.verify();CHECK(p.a->membership.prepare_reset());CHECK(!p.a->owner.current());CHECK(!p.a->owner.live());++groups;}
    {RetainedPair p;p.begin();CHECK(p.a->evidence.prepare_reset());RetainedEnrollmentResponse output{};output.signature.fill(99);const auto before=output.signature;
     CHECK(!p.a->owner.sign(p.b->challenge,output));CHECK(output.signature==before);++groups;}
    {RetainedPair p;p.begin();CHECK(p.a->bindings.prepare_reset());CHECK(!p.a->owner.sign(p.b->challenge,p.a->response));++groups;}
    {RetainedPair p;p.begin();++p.pair.a.port.value.display_revision;CHECK(!p.a->owner.sign(p.b->challenge,p.a->response));++groups;}
    {RetainedPair p;p.sign();p.pair.a.port.value.now_ms=63000;CHECK(!p.a->owner.verify(p.b->response,p.a->receipt));++groups;}
    {RetainedPair p;p.sign();p.pair.a.port.value.now_ms=2999;CHECK(!p.a->owner.verify(p.b->response,p.a->receipt));++groups;}
    {RetainedPair p;p.begin();auto bad=p.b->challenge;bad.challenge=p.a->challenge.challenge;CHECK(!p.a->owner.sign(bad,p.a->response));++groups;}
    {RetainedPair p;p.sign();CHECK(p.a->owner.verify(p.b->response,p.a->receipt));CHECK(!p.a->owner.verify(p.b->response,p.a->receipt));CHECK(!p.a->owner.live());++groups;}
    {RetainedPair p;p.begin();p.pair.a.journal.callback=[&](char){p.a->owner.cancel();};
     CHECK(!p.a->owner.sign(p.b->challenge,p.a->response));CHECK(!p.a->owner.live());++groups;}
    {RetainedPair p;p.begin();p.pair.a.evidence.callback=[&](char){CHECK(!p.a->owner.current());};
     CHECK(!p.a->owner.sign(p.b->challenge,p.a->response));CHECK(!p.a->owner.live());++groups;}
    {RetainedPair p;p.verify();CHECK(p.a->membership.revoke());CHECK(!p.a->owner.retained_current());CHECK(!p.a->owner.live());++groups;}
    {RetainedPair p;p.verify();++p.pair.a.port.value.display_revision;CHECK(p.a->owner.retained_current());CHECK(!p.a->owner.current());++groups;}
    {RetainedPair p;p.pair.a.port.value.context.boot=independent_invitation_detail::decode(p.pair.a.trusted->binding().invitation()).boot_a;
     CHECK(!p.a->owner.begin(p.a->challenge));++groups;}
    {RetainedPair p;p.begin();auto bad=p.b->challenge;bad.context.boot=independent_invitation_detail::decode(p.pair.a.trusted->binding().invitation()).boot_b;
     CHECK(!p.a->owner.sign(bad,p.a->response));++groups;}
    std::cout<<"PASS "<<groups<<" retained state groups: real activated journals, identity signatures, replay and tombstone containment\n";
}
