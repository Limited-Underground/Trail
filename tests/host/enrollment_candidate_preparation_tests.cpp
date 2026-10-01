#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_candidate_preparation.hpp"
#include <functional>

using namespace product_enrollment_test;
using namespace opentrail::companion;
namespace {
struct Authority final : EnrollmentCandidateLocalAuthority {
    EnrollmentCandidateLocalObservation value{};
    Port& port;
    std::function<void()> callback = [] {};
    bool available{true};
    std::uint64_t clock_offset{};
    explicit Authority(Port& p) : port(p) {}
    bool observe(EnrollmentCandidateLocalObservation& out) override {
        callback();
        value.authority.now_ms = port.value.now_ms + clock_offset;
        out = value;
        return available;
    }
};
struct HookPort final : FingerprintReviewPort {
    Port& port;
    std::function<void()> callback = [] {};
    bool show_ok{true};
    explicit HookPort(Port& p) : port(p) {}
    FingerprintReviewSample sample() override { callback(); return port.sample(); }
    bool show(const FingerprintReviewFrame& frame) override {
        callback(); return show_ok && port.show(frame);
    }
};
struct CandidateFixture {
    Node node;
    SelectedEnrollmentRequestOwner request;
    Authority authority{node.port};
    HookPort device{node.port};
    std::optional<EnrollmentCandidatePreparation> owner;
    explicit CandidateFixture(InvitationRole role = InvitationRole::initiator, unsigned seed = 1)
        : node(role, seed) {
        node.port.value.context.boot.fill(static_cast<unsigned char>(seed));
        node.port.value.context.generation = node.generation;
        node.port.value.context.request = 9;
        node.port.value.now_ms = 100;
        authority.value = {{DeviceNamePhase::connected, {1,2,3,4,5,6,7},100},
            8, {}, role, 17, node.port.value.context, 41};
        CHECK(request.admit(authority.value.authority,8,9,10) == SelectedEnrollmentRequestResult::admitted);
        authority.value.intent_request = request.request();
        owner.emplace(request,authority,node.identity,device,41);
    }
    ~CandidateFixture() { node.endpoint.reset(); node.preparation.reset(); }
    EvaluationEnrollmentCandidate candidate() {
        EvaluationEnrollmentCandidate value;
        CHECK(export_evaluation_enrollment_candidate(node.identity,value));
        return value;
    }
    void confirm(const EvaluationEnrollmentCandidate& peer) {
        CHECK(owner->begin(peer)); CHECK(owner->show_peer()); CHECK(owner->poll());
        node.port.value.now_ms += 1; node.port.value.button_down=true; CHECK(owner->poll());
        node.port.value.now_ms += 1000; node.port.value.button_down=false; CHECK(owner->poll());
    }
    void ready(const EvaluationEnrollmentCandidate& peer) {
        confirm(peer);
        CHECK(owner->take_review(node.reviewed));
        CHECK(node.reviewed->deadline() == request.request().deadline_ms);
        node.preparation.emplace(node.random,node.identity,node.allocator,
            owner->preparation_port(),std::move(*node.reviewed));
    }
    void new_request() {
        CHECK(request.cancel_exact(request.request()));
        ++authority.value.authority.context.transport_generation;
        ++authority.value.authority.context.session_nonce;
        authority.value.authority.now_ms = node.port.value.now_ms;
        CHECK(request.admit(authority.value.authority,8,29,30) == SelectedEnrollmentRequestResult::admitted);
        authority.value.intent_request=request.request();
        authority.value.review_context.request=29;
        node.port.value.context.request=29;
    }
};

void invitation(CandidateFixture& a, CandidateFixture& b) {
    RetainedEnrollmentIdentities pins{a.node.key,b.node.key};
    EnrollmentPossessionStatement statement{}; statement.group=17;
    statement.initiator_context={a.node.port.value.context.boot,a.node.generation,9};
    statement.responder_context={b.node.port.value.context.boot,b.node.generation,9};
    CHECK(a.node.preparation->prepare_challenge(statement.initiator_challenge));
    CHECK(b.node.preparation->prepare_challenge(statement.responder_challenge));
    EnrollmentPossessionProof proof{}; proof.statement=statement;
    EnrollmentPossessionAttempt ap(pins,statement),bp(pins,statement);
    CHECK(ap.sign(InvitationRole::initiator,a.node.identity,proof.initiator_signature));
    CHECK(bp.sign(InvitationRole::responder,b.node.identity,proof.responder_signature));
    std::optional<VerifiedEnrollmentPossession> av,bv;
    CHECK(ap.verify(proof,av)); CHECK(bp.verify(proof,bv));
    CHECK(a.node.preparation->accept_possession(*av)); CHECK(b.node.preparation->accept_possession(*bv));
    IndependentInvitationFields fields{}; fields.group=17;fields.epoch=1;fields.signer=a.node.key;
    fields.peer_a.fill(11);fields.peer_b.fill(12);fields.nonce.fill(13);
    fields.boot_a=statement.initiator_context.boot;fields.boot_b=statement.responder_context.boot;
    fields.issued_a_ms=a.node.port.value.now_ms;fields.issued_b_ms=b.node.port.value.now_ms;
    fields.window_a_ms=60000;fields.window_b_ms=60000;
    EnrollmentIdentityProof binding{};
    CHECK(a.node.preparation->issue_invitation(fields,binding.invitation));
    CHECK(a.node.preparation->sign_binding(binding.invitation,binding.initiator_signature));
    CHECK(b.node.preparation->sign_binding(binding.invitation,binding.responder_signature));
    CHECK(a.node.preparation->authorize(binding,a.node.trusted));
    CHECK(b.node.preparation->authorize(binding,b.node.trusted));
    CHECK(a.node.trusted && b.node.trusted);
    CHECK(a.node.trusted->binding().identities().initiator == a.node.key);
    CHECK(b.node.trusted->binding().identities().responder == b.node.key);
}
}
int main() {
    unsigned groups=0;
    {
        CandidateFixture a, b(InvitationRole::responder,81);
        a.ready(b.candidate()); b.ready(a.candidate()); invitation(a,b); ++groups;
    }
    // Export returns only a public evaluation identity; failures leave output intact.
    {
        CandidateFixture a; auto out=a.candidate(); const auto prior=out;
        a.node.identity.retire(); CHECK(!export_evaluation_enrollment_candidate(a.node.identity,out));
        CHECK(out.version==prior.version && out.profile==prior.profile && out.public_identity==prior.public_identity);
        ++groups;
    }
    for(unsigned variant=0;variant<13;++variant) {
        CandidateFixture a,b(InvitationRole::responder,81); auto peer=b.candidate();
        switch(variant) {
        case 0:++peer.version;break;
        case 1:++peer.profile;break;
        case 2:peer.public_identity.fill(0);break;
        case 3:peer=a.candidate();break;
        case 4:a.authority.value.group=0;break;
        case 5:a.authority.value.local_role=static_cast<InvitationRole>(0xff);break;
        case 6:++a.authority.value.intent_request.exchange_id;break;
        case 7:++a.authority.value.monotonic_domain;break;
        case 8:++a.authority.value.review_context.request;break;
        case 9:a.authority.value.review_context.generation=0;break;
        case 10:a.node.port.value.now_ms=a.request.request().deadline_ms;break;
        case 11:a.node.identity.retire();break;
        case 12:a.authority.available=false;break;
        }
        CHECK(!a.owner->begin(peer)); CHECK(a.owner->failed());
        CHECK(!a.request.pending()); CHECK(a.node.port.value.display_revision==0); ++groups;
    }
    // Request was admitted at 100. Starting review much later preserves 120100.
    {
        CandidateFixture a,b(InvitationRole::responder,81); a.node.port.value.now_ms=118000;
        a.ready(b.candidate()); CHECK(a.node.port.value.now_ms==119001);
        a.node.port.value.now_ms=120100;
        std::array<std::uint8_t,32> out{};out.fill(77);const auto before=out;
        CHECK(!a.node.preparation->prepare_challenge(out));CHECK(out==before);
        CHECK(!a.request.pending()); ++groups;
    }
    // The handed-off preparation keeps observing the original request/intent,
    // even though its real local physical-review receipt has already been consumed.
    for(unsigned variant=0;variant<13;++variant) {
        CandidateFixture a,b(InvitationRole::responder,81);a.ready(b.candidate());
        bool newer=false;
        switch(variant) {
        case 0:a.authority.value.authority.phase=DeviceNamePhase::disconnected;break;
        case 1:++a.authority.value.authority.context.owner_generation;break;
        case 2:++a.authority.value.connection_handle;break;
        case 3:a.authority.value.local_role=InvitationRole::responder;break;
        case 4:++a.authority.value.group;break;
        case 5:++a.authority.value.monotonic_domain;break;
        case 6:++a.authority.value.review_context.generation;break;
        case 7:a.node.port.value.now_ms=99;break;
        case 8:a.node.port.value.now_ms=a.request.request().deadline_ms;break;
        case 9:a.authority.clock_offset=1;break;
        case 10:a.node.identity.retire();break;
        case 11:a.owner->cancel();break;
        case 12:a.new_request();newer=true;break;
        }
        std::array<std::uint8_t,32> out{};out.fill(42);const auto before=out;
        CHECK(!a.node.preparation->prepare_challenge(out));CHECK(out==before);
        // The preparation owner may reject its identity before sampling the
        // guarded port. Its next observation still retires only this request.
        (void)a.owner->preparation_port().sample();
        CHECK(a.request.pending()==newer);
        if(newer) { CHECK(a.request.request().delivery_token==29);a.owner.reset();CHECK(a.request.pending()); }
        ++groups;
    }
    // Callbacks may cancel, replace the current request, or change authority at
    // delegated reads. A staged sample/receipt/challenge never survives that change.
    for(unsigned variant=0;variant<7;++variant) {
        CandidateFixture a,b(InvitationRole::responder,81);a.ready(b.candidate());
        bool called=false;
        a.device.callback=[&] {
            if(called)return;
            called=true;
            switch(variant) {
            case 0:a.owner->cancel();break;
            case 1:CHECK(!a.owner->poll());break;
            case 2:a.authority.value.authority.phase=DeviceNamePhase::disconnected;break;
            case 3:a.node.port.value.now_ms=a.request.request().deadline_ms;break;
            case 4:a.new_request();break;
            case 5:++a.authority.value.group;break;
            case 6:(void)a.owner->preparation_port().sample();break;
            }
        };
        std::array<std::uint8_t,32> out{};out.fill(24);const auto before=out;
        CHECK(!a.node.preparation->prepare_challenge(out));CHECK(out==before);
        CHECK(a.request.pending()==(variant==4)); ++groups;
    }
    // A new candidate cannot replace the one shown under an existing review.
    {
        CandidateFixture a,b(InvitationRole::responder,81);CHECK(a.owner->begin(b.candidate()));
        auto changed=b.candidate();changed.public_identity[0]^=1;
        CHECK(!a.owner->begin(changed));std::optional<ReviewedEnrollmentIdentity> out;
        CHECK(!a.owner->take_review(out));CHECK(!out);++groups;
    }
    // Public bytes cannot mint the physical confirmation or possession proof.
    {
        CandidateFixture a,b(InvitationRole::responder,81);CHECK(a.owner->begin(b.candidate()));
        CHECK(a.owner->show_peer());std::optional<ReviewedEnrollmentIdentity> out;
        CHECK(!a.owner->take_review(out));CHECK(!out);++groups;
    }
    // Expiry during rendering: fail closed, with no usable review after the draw.
    {
        CandidateFixture a,b(InvitationRole::responder,81); unsigned calls=0;
        a.device.callback=[&] { if(++calls==4)a.node.port.value.now_ms=120100; };
        CHECK(!a.owner->begin(b.candidate()));CHECK(a.owner->failed());++groups;
    }
    // Delegated callbacks cannot mutate the received value behind the frozen
    // candidate. Only the identity actually shown may appear in its receipt.
    {
        CandidateFixture a,b(InvitationRole::responder,81);auto peer=b.candidate();const auto key=peer.public_identity;
        a.device.callback=[&] { peer.public_identity[0]^=1; };
        a.ready(peer);CHECK(a.node.reviewed->identities().responder==key);++groups;
    }
    // The final extra handoff sample is significant: a late role change cannot
    // publish the staged receipt or overwrite a caller's existing output.
    {
        CandidateFixture a,b(InvitationRole::responder,81);
        b.confirm(a.candidate());std::optional<ReviewedEnrollmentIdentity> out;
        CHECK(b.owner->take_review(out));const auto before=out->identities();
        a.confirm(b.candidate());unsigned calls=0;
        a.device.callback=[&] { if(++calls==3)++a.authority.value.group; };
        CHECK(!a.owner->take_review(out));CHECK(out);
        CHECK(out->identities().initiator==before.initiator && out->identities().responder==before.responder);
        CHECK(a.owner->failed());++groups;
    }
    // Retirement during the real persistent identity owner's readback does not
    // admit a candidate or modify the already persisted identity.
    {
        CandidateFixture a,b(InvitationRole::responder,81);
        a.node.identity_storage.callback=[&](char) { a.node.identity.retire(); };
        CHECK(!a.owner->begin(b.candidate()));CHECK(a.owner->failed());++groups;
    }
    // A real receipt must not permit substituting the unguarded device port at
    // the downstream constructor and thereby dropping protected-request checks.
    {
        CandidateFixture a,b(InvitationRole::responder,81);a.confirm(b.candidate());
        std::optional<ReviewedEnrollmentIdentity> reviewed;CHECK(a.owner->take_review(reviewed));
        EnrollmentPreparationOwner unguarded(a.node.random,a.node.identity,a.node.allocator,
            a.device,std::move(*reviewed));
        std::array<std::uint8_t,32> out{};out.fill(31);const auto before=out;
        EnrollmentPreparationOwner reused(a.node.random,a.node.identity,a.node.allocator,
            a.owner->preparation_port(),std::move(*reviewed));
        CHECK(a.request.pending());CHECK(!reused.prepare_challenge(out));CHECK(out==before);
        a.owner->cancel();
        CHECK(!unguarded.prepare_challenge(out));CHECK(out==before);++groups;
    }
    // Move construction and assignment both keep the exact originating guard.
    {
        CandidateFixture a,b(InvitationRole::responder,81);a.confirm(b.candidate());b.confirm(a.candidate());
        std::optional<ReviewedEnrollmentIdentity> source,destination;
        CHECK(a.owner->take_review(source));CHECK(b.owner->take_review(destination));
        auto moved=std::move(*source);*destination=std::move(moved);
        EnrollmentPreparationOwner correct(a.node.random,a.node.identity,a.node.allocator,
            a.owner->preparation_port(),std::move(*destination));
        std::array<std::uint8_t,32> out{};CHECK(correct.prepare_challenge(out));++groups;
    }
    // Retirement in the terminal authority callback follows the last delegated
    // identity read. It must still suppress the staged challenge.
    {
        CandidateFixture a,b(InvitationRole::responder,81);a.ready(b.candidate());unsigned calls=0;
        a.authority.callback=[&] { if(++calls==8)a.node.identity.retire(); };
        std::array<std::uint8_t,32> out{};out.fill(51);const auto before=out;
        CHECK(!a.node.preparation->prepare_challenge(out));CHECK(out==before);CHECK(calls==8);++groups;
    }
    std::cout<<"PASS "<<groups<<" enrollment candidate preparation groups (host evaluation only)\n";
}
