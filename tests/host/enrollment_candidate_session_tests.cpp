#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_candidate_session.hpp"

using namespace product_enrollment_test;
using namespace opentrail::companion;
namespace session_test {
struct Authority final : DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    std::function<void()> callback=[]{};
    DeviceNameAuthority current() noexcept override {callback();return value;}
};
struct Device final : EnrollmentSessionDevicePort {
    Authority& authority;FingerprintReviewSample value{};FingerprintReviewFrame frame{};
    bool bound{},available{true},release_ok{true};unsigned releases{};
    std::function<void()> callback=[]{};
    explicit Device(Authority& a):authority(a){}
    bool bind_context(const FingerprintReviewContext& c) override {
        callback();if(bound || !available)return false;
        value.context=c;bound=true;return true;
    }
    bool release_context(const FingerprintReviewContext& c) override {
        callback();++releases;if(!bound || !(c==value.context))return false;
        bound=false;return release_ok;
    }
    FingerprintReviewSample sample() override {
        callback();value.now_ms=authority.value.now_ms;return value;
    }
    bool show(const FingerprintReviewFrame& f) override {
        callback();if(!available)return false;
        frame=f;value.display_revision=f.revision;return true;
    }
};
struct FreshNode {
    CallbackStorage identity,journal,binding,boot;
    Backend backend;security::test_support::FakeSecureRandomSource random;
    Authority authority;Device device{authority};SelectedEnrollmentRequestOwner request;
    std::optional<EnrollmentCandidateSession> owner;
    InvitationRole role;bool dedicated_boot{};EvaluationEnrollmentCandidate candidate{};EvaluationEnrollmentOffer offer{};
    explicit FreshNode(InvitationRole r=InvitationRole::initiator,unsigned seed=1,std::uint64_t now=100,bool durable=false):role(r),dedicated_boot(durable) {
        authority.value.now_ms=now;authority.value.context.device=seed;
        refill(seed);admit();construct();
    }
    void refill(unsigned seed) {
        std::array<std::uint8_t,512> bytes{};
        for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(seed+i);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);
    }
    void admit() {CHECK(request.admit(authority.value,8,9,10)==SelectedEnrollmentRequestResult::admitted);}
    void construct() {owner.emplace(request,authority,device,random,identity,journal,binding,backend,4,41,dedicated_boot?&boot:nullptr);}
    void start() {CHECK(owner->start(role,17));CHECK(owner->export_candidate(candidate));}
    void confirm_review() {
        CHECK(owner->show_peer());CHECK(owner->poll_review());
        ++authority.value.now_ms;device.value.button_down=true;CHECK(owner->poll_review());
        authority.value.now_ms+=1000;device.value.button_down=false;CHECK(owner->poll_review());
        CHECK(owner->finish_review(offer));
        CHECK(offer.context.boot==device.value.context.boot);
        CHECK(offer.context.generation==device.value.context.generation);
        CHECK(offer.context.request==request.request().delivery_token);
    }
    void replace_request() {
        if(request.pending())CHECK(request.cancel_exact(request.request()));
        ++authority.value.context.transport_generation;++authority.value.context.session_nonce;
        CHECK(request.admit(authority.value,8,19,20)==SelectedEnrollmentRequestResult::admitted);
    }
    void restart(unsigned seed=171) {
        CHECK(owner->close());owner.reset();authority.callback=[]{};device.callback=[]{};
        ++authority.value.context.runtime;refill(seed);admit();construct();
    }
    EnrollmentJournalState journal_state() {
        EnrollmentCommitCoordinator read(journal);CHECK(read.initialize());return read.state();
    }
};
struct FreshPair {
    FreshNode a,b;bool retained{};unsigned epoch{1};
    explicit FreshPair(bool durable=false,bool start_now=true):a(InvitationRole::initiator,1,100,durable),b(InvitationRole::responder,81,50100,durable) {
        if(start_now){a.start();b.start();CHECK(a.owner->receive_candidate(b.candidate));CHECK(b.owner->receive_candidate(a.candidate));}
    }
    void reconstruct_retained() {
        CHECK(a.owner->close());CHECK(b.owner->close());
        a.restart(131+epoch);b.restart(211+epoch);retained=true;++epoch;
    }
    void restart_retained() {
        reconstruct_retained();
        CHECK(a.owner->start_rekey(a.role,17));CHECK(b.owner->start_rekey(b.role,17));
    }
    void review() {
        if(!retained){a.confirm_review();b.confirm_review();return;}
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        CHECK(a.owner->begin_retained_comparison(ac));CHECK(b.owner->begin_retained_comparison(bc));
        CHECK(a.owner->sign_retained_challenge(bc,ar));CHECK(b.owner->sign_retained_challenge(ac,br));
        CHECK(a.owner->finish_retained_comparison(br,a.offer));CHECK(b.owner->finish_retained_comparison(ar,b.offer));
    }
    void possession() {
        review();std::array<std::uint8_t,64> as{},bs{};
        CHECK(a.owner->prepare_possession(b.offer,as));CHECK(b.owner->prepare_possession(a.offer,bs));
        CHECK(a.owner->accept_possession(bs));CHECK(b.owner->accept_possession(as));
    }
    IndependentInvitation invite() {
        possession();EvaluationEnrollmentClockMark am{},bm{};
        CHECK(a.owner->clock_mark(am));CHECK(b.owner->clock_mark(bm));
        CHECK(am.now_ms!=bm.now_ms);CHECK(a.owner->receive_clock_mark(bm));CHECK(b.owner->receive_clock_mark(am));
        IndependentInvitation invitation{};CHECK(a.owner->issue_invitation(invitation));
        const auto f=independent_invitation_detail::decode(invitation);
        CHECK(f.peer_a==a.offer.session_identity && f.peer_b==b.offer.session_identity);
        CHECK(f.boot_a==a.offer.context.boot && f.boot_b==b.offer.context.boot);
        CHECK(f.issued_a_ms==am.now_ms && f.issued_b_ms==bm.now_ms);
        CHECK(f.window_a_ms==60000 && f.window_b_ms==60000);
        CHECK(f.epoch==epoch);
        return invitation;
    }
    void begin() {
        const auto invitation=invite();std::array<std::uint8_t,64> as{},bs{};
        CHECK(a.owner->sign_invitation(invitation,as));CHECK(b.owner->sign_invitation(invitation,bs));
        CHECK(a.owner->begin_handshake(bs));CHECK(b.owner->begin_handshake(as));
    }
    static void handshake_transfer(FreshNode& a,FreshNode& b) {
        HandshakeFrame f{};CHECK(a.owner->next_handshake(f));CHECK(b.owner->receive_handshake(f));
    }
    void handshake() {begin();handshake_transfer(a,b);handshake_transfer(b,a);handshake_transfer(a,b);}
    void confirmation() {
        handshake();for(auto* n:{&a,&b}) {
            CHECK(n->owner->poll_confirmation());
            CHECK(n->device.frame.purpose==EnrollmentDisplayPurpose::transcript_confirmation);
            ++n->authority.value.now_ms;n->device.value.button_down=true;CHECK(n->owner->poll_confirmation());
            n->authority.value.now_ms+=1000;n->device.value.button_down=false;CHECK(n->owner->poll_confirmation());
        }
    }
    static void control(FreshNode& a,FreshNode& b) {
        EvaluationRecord record{};CHECK(a.owner->next_control(record));CHECK(b.owner->receive_control(record));
    }
    void controls() {confirmation();control(a,b);control(b,a);control(a,b);control(b,a);}
    void activate() {controls();CHECK(a.owner->commit());CHECK(b.owner->commit());CHECK(a.owner->ready());CHECK(b.owner->ready());}
};
auto bytes(const CallbackStorage& storage) {
    std::array<std::array<std::uint8_t,persistence::kPersistentSlotBytes>,
        persistence::kStorageDomainCount*persistence::kPersistentSlotCount> all{};
    for(std::size_t d=0;d<persistence::kStorageDomainCount;++d)
        for(std::size_t slot=0;slot<persistence::kPersistentSlotCount;++slot)
            all[d*2+slot]=storage.inner.memory.slot_bytes(static_cast<Domain>(d),slot);
    return all;
}
unsigned mutations(const CallbackStorage& storage) {
    unsigned count=0;
    for(std::size_t d=0;d<persistence::kStorageDomainCount;++d) {
        const auto c=storage.inner.memory.counters(static_cast<Domain>(d));count+=c.erases+c.writes+c.syncs;
    }
    return count;
}
struct ResetMarker final : DeviceFactoryResetMarkerPort {
    DeviceFactoryResetMarkerSnapshot value{DeviceFactoryResetPortError::none,DeviceFactoryResetMarkerState::intent_committed,47};
    unsigned reads{};std::function<void()> callback=[]{};
    DeviceFactoryResetMarkerSnapshot load() override {++reads;callback();return value;}
    DeviceFactoryResetMarkerSnapshot commit_intent_and_readback(std::uint64_t) override {CHECK(false);return {};}
    DeviceFactoryResetMarkerSnapshot complete_cleanup_and_readback() override {CHECK(false);return {};}
    DeviceFactoryResetReceiptConsumeSnapshot consume_completion_receipt_and_readback() override {CHECK(false);return {};}
};
PeerMembership retained_member(FreshNode& node) {
    PeerMembershipStore store(node.backend.at(0,EvaluationNamespace::membership));PeerMembership value{};
    CHECK(store.initialize());CHECK(store.read(value));return value;
}
void no_traffic(FreshNode& node) {
    EvaluationRecord frame{};frame.group=912;std::uint8_t value=91;
    CHECK(!node.owner->send_status(1,frame));CHECK(frame.group==912);
    CHECK(!node.owner->receive_status(frame,value));CHECK(value==91);
    CHECK(!node.owner->ready());CHECK(node.owner->secrets_cleared());
}
}
using namespace session_test;
#ifndef OPENTRAIL_SESSION_FIXTURE_ONLY
int main() {
    unsigned groups=0;
    {
        FreshPair p;p.activate();CHECK(!p.a.request.pending() && !p.b.request.pending());
        CHECK(p.a.journal_state()==EnrollmentJournalState::active_committed);
        CHECK(p.b.journal_state()==EnrollmentJournalState::active_committed);
        // Completed radio authority is independent of a later phone disconnect.
        p.a.authority.value.phase=DeviceNamePhase::disconnected;
        ++p.a.authority.value.context.owner;++p.a.authority.value.context.transport_generation;
        p.b.authority.value.phase=DeviceNamePhase::disconnected;
        for(unsigned i=0;i<4;++i)for(auto* from:{&p.a,&p.b}) {
            auto& to=from==&p.a?p.b:p.a;EvaluationRecord r{};std::uint8_t value=0;
            CHECK(from->owner->send_status(static_cast<std::uint8_t>(i+1),r));
            CHECK(to.owner->receive_status(r,value));CHECK(value==i+1);
        }
        CHECK(p.a.owner->close());CHECK(p.b.owner->close());
        CHECK(p.a.owner->secrets_cleared() && p.b.owner->secrets_cleared());++groups;
    }
    for(unsigned variant=0;variant<5;++variant) {
        FreshNode a,b(InvitationRole::responder,81);a.start();b.start();auto peer=b.candidate;
        if(variant==0)++peer.version;
        if(variant==1)++peer.profile;
        if(variant==2)peer.public_identity.fill(0);
        if(variant==3)peer=a.candidate;
        if(variant==4)CHECK(a.owner->receive_candidate(peer));
        CHECK(!a.owner->receive_candidate(peer));CHECK(a.owner->failed());CHECK(a.owner->secrets_cleared());++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshNode a;
        CHECK(!a.owner->start(variant==0?static_cast<InvitationRole>(0xff):a.role,variant==1?0:17) || variant==2);
        if(variant==2)CHECK(!a.owner->start(a.role,17));
        CHECK(a.owner->failed());++groups;
    }
    for(unsigned variant=0;variant<8;++variant) {
        FreshPair p;p.review();auto peer=p.b.offer;std::array<std::uint8_t,64> signature{};signature.fill(73);const auto before=signature;
        if(variant==0)++peer.version;
        if(variant==1)++peer.group;
        if(variant==2)peer.role=p.a.role;
        if(variant==3)peer.session_identity=p.a.offer.session_identity;
        if(variant==4)peer.context.boot.fill(0);
        if(variant==5)peer.context.generation=0;
        if(variant==6)peer.context.request=0;
        if(variant==7)peer.challenge=p.a.offer.challenge;
        CHECK(!p.a.owner->prepare_possession(peer,signature));CHECK(signature==before);++groups;
    }
    // Real signatures refuse a peer contribution changed between endpoints.
    for(unsigned variant=0;variant<3;++variant) {
        FreshPair p;p.review();auto peer=p.b.offer;
        if(variant==0)peer.challenge[0]^=1;
        if(variant==1)++peer.context.request;
        if(variant==2)peer.context.boot[0]^=1;
        std::array<std::uint8_t,64> as{},bs{};
        CHECK(p.a.owner->prepare_possession(peer,as));CHECK(p.b.owner->prepare_possession(p.a.offer,bs));
        CHECK(!p.a.owner->accept_possession(bs));++groups;
    }
    for(unsigned variant=0;variant<5;++variant) {
        FreshPair p;p.begin();HandshakeFrame out{};out.step=77;
        if(variant==0)CHECK(p.a.request.cancel_exact(p.a.request.request()));
        if(variant==1)p.a.authority.value.phase=DeviceNamePhase::disconnected;
        if(variant==2)p.a.replace_request();
        if(variant==3)p.a.authority.value.now_ms=p.a.request.request().deadline_ms;
        if(variant==4)--p.a.authority.value.now_ms;
        CHECK(!p.a.owner->next_handshake(out));CHECK(out.step==77);
        CHECK(p.a.owner->secrets_cleared());if(variant==2)CHECK(p.a.request.pending());++groups;
    }
    {
        FreshPair p;const auto original_deadline=p.a.request.request().deadline_ms;
        p.a.authority.value.now_ms+=70000;p.b.authority.value.now_ms+=70000;p.begin();
        CHECK(p.a.authority.value.now_ms+60000>original_deadline);
        CHECK(p.a.request.request().deadline_ms==original_deadline);
        p.a.authority.value.now_ms=original_deadline;
        HandshakeFrame out{};out.step=83;CHECK(!p.a.owner->next_handshake(out));CHECK(out.step==83);++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshPair p;p.confirmation();EvaluationRecord out{};out.group=99;
        if(variant==0)CHECK(p.a.request.cancel_exact(p.a.request.request()));
        if(variant==1)p.a.replace_request();
        if(variant==2)p.a.authority.value.now_ms+=60000;
        CHECK(!p.a.owner->next_control(out));CHECK(out.group==99);CHECK(p.a.owner->secrets_cleared());++groups;
    }
    // Reentry/cancellation in delegated hardware/storage callbacks must not publish.
    for(unsigned variant=0;variant<4;++variant) {
        FreshPair p;bool fired=false;
        auto hook=[&]{if(!fired){fired=true;if(variant==0)(void)p.a.owner->show_local();else if(variant==1)(void)p.a.owner->cancel();else if(variant==2)(void)p.a.request.cancel_exact(p.a.request.request());else p.a.replace_request();}};
        p.a.device.callback=hook;
        CHECK(!p.a.owner->show_peer());CHECK(fired);CHECK(p.a.owner->secrets_cleared());
        if(variant==3)CHECK(p.a.request.pending());
        ++groups;
    }
    for(unsigned variant=0;variant<4;++variant) {
        FreshNode a;CallbackStorage* store=variant==0?&a.identity:variant==1?&a.journal:variant==2?&a.binding:&a.backend.at(0,EvaluationNamespace::membership);
        store->arm(Fault::read_error);CHECK(!a.owner->start(a.role,17));CHECK(a.owner->failed());++groups;
    }
    // Any retained record, even incomplete, forbids reseeding a missing identity.
    for(unsigned variant=0;variant<4;++variant) {
        FreshNode a;CallbackStorage* store=variant==0?&a.journal:variant==1?&a.binding:variant==2?&a.backend.at(0,EvaluationNamespace::membership):&a.backend.at(0,EvaluationNamespace::enrollment);
        const std::uint8_t byte=0;CHECK(store->write_slot(Domain::outbound_counter_state,0,0,{&byte,1})==Error::none);
        CHECK(!a.owner->start(a.role,17));
        for(auto* untouched:{&a.identity,&a.backend.at(0,EvaluationNamespace::boot)}) {
            for(std::size_t d=0;d<persistence::kStorageDomainCount;++d) {
                const auto space=static_cast<Domain>(d);
                const auto count=untouched->inner.memory.counters(space);
                CHECK(count.writes==0 && count.erases==0 && count.syncs==0);
                for(std::size_t slot=0;slot<persistence::kPersistentSlotCount;++slot)
                    for(const auto byte:untouched->inner.memory.slot_bytes(space,slot))CHECK(byte==0xff);
            }
        }
        ++groups;
    }
    {
        FreshPair p;p.activate();CHECK(p.a.owner->close());p.a.owner.reset();
        const auto identity_before=p.a.identity.inner.memory.slot_bytes(Domain::secret_material,0);
        const auto ledger_before=p.a.backend.at(0,EvaluationNamespace::boot).inner.memory.slot_bytes(domain,0);
        ++p.a.authority.value.context.runtime;p.a.admit();p.a.construct();
        CHECK(!p.a.owner->start(p.a.role,17));
        CHECK(p.a.identity.inner.memory.slot_bytes(Domain::secret_material,0)==identity_before);
        CHECK(p.a.backend.at(0,EvaluationNamespace::boot).inner.memory.slot_bytes(domain,0)==ledger_before);
        EvaluationRecord out{};out.group=99;CHECK(!p.a.owner->send_status(1,out));CHECK(out.group==99);++groups;
    }
    {
        FreshPair p;p.begin();HandshakeFrame old{};CHECK(p.a.owner->next_handshake(old));
        p.a.restart();CHECK(p.a.owner->start(p.a.role,17));
        CHECK(!p.a.owner->receive_handshake(old));CHECK(p.a.owner->secrets_cleared());++groups;
    }
    {
        FreshPair p;p.controls();CHECK(p.a.request.cancel_exact(p.a.request.request()));
        CHECK(!p.a.owner->commit());CHECK(p.a.journal_state()!=EnrollmentJournalState::active_committed);++groups;
    }
    // Cancellation inside persistence is sampled by the final journal guard.
    // An already-written final marker cannot honestly be rolled back by refusal.
    for(unsigned variant=0;variant<4;++variant) {
        FreshPair p;p.controls();unsigned writes=0;bool fired=false,final_written=false;
        const auto interrupt=[&]{
            fired=true;
            if(variant==1)p.a.replace_request();
            else CHECK(p.a.request.cancel_exact(p.a.request.request()));
        };
        if(variant<2) {
            p.a.binding.callback=[&](char operation){if(operation=='w' && !fired)interrupt();};
        } else {
            p.a.journal.callback=[&](char operation){
                if(operation=='w' && ++writes==7) {
                    final_written=true;if(variant==2)interrupt();
                }
                if(operation=='r' && final_written && variant==3 && !fired)interrupt();
            };
        }
        CHECK(!p.a.owner->commit());CHECK(fired);CHECK(!p.a.owner->ready());
        CHECK(p.a.owner->secrets_cleared());
        p.a.binding.callback=[](char){};p.a.journal.callback=[](char){};
        const auto persisted=p.a.journal_state();
        CHECK(persisted==(variant<2?EnrollmentJournalState::reconcile:EnrollmentJournalState::active_committed));
        if(variant==1)CHECK(p.a.request.pending());
        const auto identity_before=p.a.identity.inner.memory.slot_bytes(Domain::secret_material,0);
        const auto ledger_before=p.a.backend.at(0,EvaluationNamespace::boot).inner.memory.slot_bytes(domain,0);
        if(p.a.request.pending())CHECK(p.a.request.cancel_exact(p.a.request.request()));
        p.a.restart();CHECK(!p.a.owner->start(p.a.role,17));
        CHECK(p.a.identity.inner.memory.slot_bytes(Domain::secret_material,0)==identity_before);
        CHECK(p.a.backend.at(0,EvaluationNamespace::boot).inner.memory.slot_bytes(domain,0)==ledger_before);
        ++groups;
    }
    // The terminal marker guard must resample request authority AFTER its own
    // delegated identity/generation/endpoint reads, before entering write7.
    {
        unsigned final_guard_read=0;
        {
            FreshPair baseline;baseline.controls();unsigned reads=0,writes=0;
            baseline.a.backend.at(0,EvaluationNamespace::boot).callback=[&](char op){if(op=='r')++reads;};
            baseline.a.journal.callback=[&](char op){if(op=='w' && ++writes==7)final_guard_read=reads;};
            CHECK(baseline.a.owner->commit());CHECK(writes==7 && final_guard_read>0);
        }
        FreshPair p;p.controls();unsigned reads=0,writes=0;bool fired=false;
        p.a.backend.at(0,EvaluationNamespace::boot).callback=[&](char op){
            if(op=='r' && ++reads==final_guard_read) {
                fired=true;CHECK(writes==6);CHECK(p.a.request.cancel_exact(p.a.request.request()));
            }
        };
        p.a.journal.callback=[&](char op){if(op=='w')++writes;};
        CHECK(!p.a.owner->commit());CHECK(fired);CHECK(writes==6);
        CHECK(p.a.owner->secrets_cleared());
        p.a.backend.at(0,EvaluationNamespace::boot).callback=[](char){};p.a.journal.callback=[](char){};
        EnrollmentCommitCoordinator read(p.a.journal);
        CHECK(!read.initialize());CHECK(read.state()==EnrollmentJournalState::unavailable);
        p.a.restart();CHECK(!p.a.owner->start(p.a.role,17));++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshPair p;p.possession();EvaluationEnrollmentClockMark bm{};
        CHECK(p.b.owner->clock_mark(bm));
        if(variant==0)++bm.version;
        if(variant==1)++bm.context.generation;
        if(variant==2)bm.now_ms=std::numeric_limits<std::uint64_t>::max();
        CHECK(!p.a.owner->receive_clock_mark(bm));++groups;
    }
    for(unsigned variant=0;variant<4;++variant) {
        FreshPair p;auto invite=p.invite();
        if(variant==0)invite.payload[30]^=1;
        if(variant==1)invite.signature[0]^=1;
        if(variant==2)invite.payload[176]^=1;
        if(variant==3)invite.payload[12]^=1;
        std::array<std::uint8_t,64> out{};out.fill(91);const auto before=out;
        CHECK(!p.b.owner->sign_invitation(invite,out));CHECK(out==before);++groups;
    }
    {
        FreshPair p;p.begin();bool fired=false;
        p.a.authority.callback=[&]{if(!fired){fired=true;(void)p.a.owner->show_local();}};
        HandshakeFrame out{};out.step=93;CHECK(!p.a.owner->next_handshake(out));CHECK(fired);CHECK(out.step==93);++groups;
    }
    {
        FreshPair p;p.controls();bool fired=false;
        p.a.backend.at(0,EvaluationNamespace::enrollment).callback=[&](char op){
            if(op=='w' && !fired){fired=true;(void)p.a.owner->cancel();}
        };
        CHECK(!p.a.owner->commit());CHECK(fired);CHECK(p.a.owner->secrets_cleared());++groups;
    }
    {
        FreshPair p;p.a.device.release_ok=false;
        p.a.authority.value.now_ms=p.a.request.request().deadline_ms;
        CHECK(!p.a.owner->show_peer());CHECK(p.a.device.releases==1);
        CHECK(!p.a.owner->close());CHECK(p.a.owner->secrets_cleared());CHECK(p.a.device.releases==1);++groups;
    }
    {
        FreshPair p;p.a.confirm_review();bool fired=false;
        p.a.identity.callback=[&](char op){if(op=='r' && !fired){fired=true;p.a.replace_request();}};
        EvaluationEnrollmentCandidate out{};out.public_identity.fill(79);const auto before=out;
        CHECK(!p.a.owner->export_candidate(out));CHECK(fired);CHECK(out.public_identity==before.public_identity);
        CHECK(p.a.request.pending());++groups;
    }
    // A live authority loss closes secrets without silently changing membership.
    {
        FreshPair p;p.activate();p.a.authority.value.context.runtime++;
        EvaluationRecord out{};out.group=94;CHECK(!p.a.owner->send_status(1,out));CHECK(out.group==94);
        CHECK(p.a.owner->secrets_cleared());CHECK(p.a.journal_state()==EnrollmentJournalState::active_committed);++groups;
    }
    {
        FreshPair p(true);p.activate();const auto first_a=p.a.offer,first_b=p.b.offer;
        for(unsigned epoch=2;epoch<=3;++epoch) {
            p.restart_retained();
            CHECK(p.a.device.value.context.boot==p.b.device.value.context.boot); // Real equal local counters.
            p.activate();
            CHECK(p.a.offer.context.boot!=first_a.context.boot && p.b.offer.context.boot!=first_b.context.boot);
            CHECK(p.a.offer.context.generation==epoch && p.b.offer.context.generation==epoch);
            CHECK(p.a.offer.session_identity!=first_a.session_identity && p.b.offer.session_identity!=first_b.session_identity);
            for(unsigned i=0;i<4;++i)for(auto* from:{&p.a,&p.b}) {
                auto& to=from==&p.a?p.b:p.a;EvaluationRecord r{};std::uint8_t value=0;
                CHECK(from->owner->send_status(static_cast<std::uint8_t>(i+1),r));CHECK(to.owner->receive_status(r,value));CHECK(value==i+1);
            }
            EnrollmentCommitCoordinator journal(p.a.journal);CHECK(journal.initialize());EnrollmentCommitContext record{};
            CHECK(journal.read_public(record));CHECK(record.epoch==epoch && record.session_generation==epoch);
            PeerMembershipStore member(p.a.backend.at(0,EvaluationNamespace::membership));CHECK(member.initialize());PeerMembership retained{};
            CHECK(member.read(retained));CHECK(retained.generation==epoch && retained.state==MembershipState::active);
        }
        ++groups;
    }
    {
        FreshPair p(true);p.activate();EvaluationRecord old{};CHECK(p.a.owner->send_status(3,old));
        p.restart_retained();p.activate();std::uint8_t value=87;
        CHECK(!p.b.owner->receive_status(old,value));CHECK(value==87 && p.b.owner->secrets_cleared());++groups;
    }
    {
        FreshPair p(true);p.activate();p.reconstruct_retained();
        CHECK(p.a.owner->start_rekey(p.a.role,17));RetainedEnrollmentChallenge old{};CHECK(p.a.owner->begin_retained_comparison(old));
        CHECK(p.a.owner->close());p.a.restart(174);CHECK(p.a.owner->start_rekey(p.a.role,17));CHECK(p.b.owner->start_rekey(p.b.role,17));
        p.activate();CHECK(p.a.offer.context.generation==3 && p.b.offer.context.generation==2);
        CHECK(p.a.offer.context.boot!=old.context.boot);++groups;
    }
    // Preflight must never provision identity or allocate on retained mismatch.
    for(unsigned variant=0;variant<11;++variant) {
        FreshPair p(true);p.activate();p.reconstruct_retained();
        std::array<std::uint8_t,64> blank{};blank.fill(0xff);
        if(variant==0)p.a.identity.inner.memory.seed_slot(Domain::secret_material,0,blank);
        if(variant==1)p.a.identity.inner.memory.corrupt_byte(Domain::secret_material,0,10,1);
        if(variant==2)p.a.boot.inner.memory.seed_slot(domain,0,blank);
        if(variant==3)p.a.journal.inner.memory.corrupt_byte(Domain::configuration,0,9,1);
        if(variant==4)p.a.binding.inner.memory.corrupt_byte(Domain::configuration,0,9,1);
        if(variant==5)p.a.backend.at(0,EvaluationNamespace::enrollment).inner.memory.corrupt_byte(Domain::configuration,0,9,1);
        if(variant==6 || variant==7) {
            PeerMembershipStore member(p.a.backend.at(0,EvaluationNamespace::membership));CHECK(member.initialize());
            CHECK(variant==6?member.revoke():member.prepare_reset());
        }
        if(variant==10)p.a.identity.arm(Fault::read_error);
        const auto identity=bytes(p.a.identity),ledger=bytes(p.a.backend.at(0,EvaluationNamespace::boot)),boot=bytes(p.a.boot);
        const auto iw=mutations(p.a.identity),lw=mutations(p.a.backend.at(0,EvaluationNamespace::boot)),bw=mutations(p.a.boot);
        const auto entropy=p.a.random.consumed_byte_count();
        CHECK(!p.a.owner->start_rekey(variant==8?InvitationRole::responder:p.a.role,variant==9?18:17));
        CHECK(bytes(p.a.identity)==identity && bytes(p.a.backend.at(0,EvaluationNamespace::boot))==ledger && bytes(p.a.boot)==boot);
        CHECK(mutations(p.a.identity)==iw && mutations(p.a.backend.at(0,EvaluationNamespace::boot))==lw && mutations(p.a.boot)==bw);
        CHECK(p.a.random.consumed_byte_count()==entropy);++groups;
    }
    {
        FreshPair p;p.activate();p.reconstruct_retained();
        CHECK(!p.a.owner->start_rekey(p.a.role,17));CHECK(p.a.random.consumed_byte_count()==0);++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshNode a;CHECK(a.owner->close());a.owner.reset();CHECK(a.request.cancel_exact(a.request.request()));a.admit();
        auto* aliased=variant==0?&a.identity:variant==1?&a.journal:&a.binding;
        a.owner.emplace(a.request,a.authority,a.device,a.random,a.identity,a.journal,a.binding,a.backend,4,41,aliased);
        CHECK(!a.owner->start(a.role,17));CHECK(mutations(a.identity)==0 && mutations(a.journal)==0 && mutations(a.binding)==0);++groups;
    }
    for(unsigned variant=0;variant<7;++variant) {
        FreshPair p(true);p.activate();p.restart_retained();
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        CHECK(p.a.owner->begin_retained_comparison(ac));CHECK(p.b.owner->begin_retained_comparison(bc));
        CHECK(p.a.owner->sign_retained_challenge(bc,ar));CHECK(p.b.owner->sign_retained_challenge(ac,br));
        if(variant==0)br.signature[0]^=1;
        if(variant==1)++br.initiator.context.generation;
        if(variant==2)br.responder.challenge[0]^=1;
        if(variant==3)br=ar; // Reflected local response cannot prove peer possession.
        if(variant==4)p.a.authority.value.phase=DeviceNamePhase::disconnected;
        if(variant==5)p.a.replace_request();
        if(variant==6)p.a.authority.value.now_ms+=60000;
        EvaluationEnrollmentOffer out{};out.group=82;
        CHECK(!p.a.owner->finish_retained_comparison(br,out));CHECK(out.group==82);
        CHECK(p.a.owner->secrets_cleared());if(variant==5)CHECK(p.a.request.pending());++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        CHECK(p.a.owner->begin_retained_comparison(ac));CHECK(p.b.owner->begin_retained_comparison(bc));
        CHECK(p.a.owner->sign_retained_challenge(bc,ar));CHECK(p.b.owner->sign_retained_challenge(ac,br));
        CHECK(p.a.owner->close());CHECK(p.b.owner->close());p.a.restart(175);p.b.restart(215);
        CHECK(p.a.owner->start_rekey(p.a.role,17));CHECK(p.b.owner->start_rekey(p.b.role,17));
        CHECK(p.a.owner->begin_retained_comparison(ac));CHECK(p.b.owner->begin_retained_comparison(bc));
        RetainedEnrollmentResponse fresh{};CHECK(p.a.owner->sign_retained_challenge(bc,fresh));
        EvaluationEnrollmentOffer out{};out.group=86;CHECK(!p.a.owner->finish_retained_comparison(br,out));CHECK(out.group==86);++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();bool fired=false;
        p.a.identity.callback=[&](char op){if(op=='r' && !fired){fired=true;p.a.replace_request();}};
        RetainedEnrollmentChallenge out{};out.context.request=88;
        CHECK(!p.a.owner->begin_retained_comparison(out));CHECK(fired && out.context.request==88 && p.a.request.pending());++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();bool fired=false;
        p.a.device.callback=[&]{if(!fired){fired=true;(void)p.a.owner->show_peer();}};
        RetainedEnrollmentChallenge out{};out.context.request=89;
        CHECK(!p.a.owner->begin_retained_comparison(out));CHECK(fired && out.context.request==89);CHECK(p.a.owner->secrets_cleared());++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();
        const auto deadline=p.a.request.request().deadline_ms;p.a.authority.value.now_ms+=70000;p.b.authority.value.now_ms+=70000;
        p.begin();CHECK(p.a.authority.value.now_ms+60000>deadline);p.a.authority.value.now_ms=deadline;
        HandshakeFrame out{};out.step=93;CHECK(!p.a.owner->next_handshake(out));CHECK(out.step==93);++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();p.controls();bool fired=false;
        p.a.binding.callback=[&](char op){if(op=='w' && !fired){fired=true;CHECK(p.a.request.cancel_exact(p.a.request.request()));}};
        CHECK(!p.a.owner->commit());CHECK(fired && p.a.owner->secrets_cleared());p.a.binding.callback=[](char){};
        CHECK(p.a.owner->close());CHECK(p.b.owner->close());p.a.restart(176);
        CHECK(!p.a.owner->start_rekey(p.a.role,17));++groups;
    }
    {
        FreshPair p(true);p.activate();p.restart_retained();p.controls();CHECK(p.a.owner->commit());
        // One peer committed epoch2, the other retains incomplete intent. Neither
        // endpoint may repair that mismatch by blindly beginning another rekey.
        CHECK(p.a.owner->close());CHECK(p.b.owner->close());p.b.restart(217);
        CHECK(!p.b.owner->start_rekey(p.b.role,17));++groups;
    }
    for(unsigned variant=0;variant<4;++variant) {
        FreshPair p(true);p.activate();p.restart_retained();
        if(variant==0)CHECK(!p.a.owner->receive_candidate(p.b.candidate));
        if(variant==1)CHECK(!p.a.owner->show_peer());
        if(variant==2) {
            std::array<std::uint8_t,64> fabricated{};CHECK(!p.a.owner->begin_handshake(fabricated));
        }
        if(variant==3) {
            p.review();EvaluationRecord out{};out.group=84;
            CHECK(!p.a.owner->send_status(1,out));CHECK(out.group==84);
        }
        CHECK(p.a.owner->secrets_cleared());++groups;
    }
    {
        FreshNode a(InvitationRole::initiator,1,100,true);
        CHECK(!a.owner->start_rekey(a.role,17));CHECK(mutations(a.identity)==0 && mutations(a.boot)==0);
        CHECK(mutations(a.backend.at(0,EvaluationNamespace::boot))==0);CHECK(a.random.consumed_byte_count()==0);++groups;
    }
    {
        FreshPair p(true);p.activate();p.reconstruct_retained();p.a.owner.reset();
        CHECK(p.a.request.pending()); // The unstarted owner never captured this request.
        p.a.owner.emplace(p.a.request,p.a.authority,p.a.device,p.a.random,p.a.identity,p.a.journal,p.a.binding,p.a.backend,1,41,&p.a.boot);
        const auto before=bytes(p.a.backend.at(0,EvaluationNamespace::boot));
        CHECK(!p.a.owner->start_rekey(p.a.role,17));CHECK(p.a.random.consumed_byte_count()==0);
        CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::boot))==before);++groups;
    }
    using CS=EnrollmentContainmentState;
    {
        FreshPair p(true);p.activate();const auto prior=retained_member(p.a);
        const auto identity=bytes(p.a.identity),journal=bytes(p.a.journal),binding=bytes(p.a.binding),boot=bytes(p.a.boot);
        const auto ledger=bytes(p.a.backend.at(0,EvaluationNamespace::boot));
        const auto result=p.a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.resources_released && !result.interrupted);
        CHECK(result.membership==CS::verified_terminal && result.evidence==CS::not_attempted && result.binding==CS::not_attempted);
        auto terminal=retained_member(p.a);CHECK(terminal.state==MembershipState::revoked && terminal.binding==prior.binding);
        CHECK(terminal.generation==prior.generation+1);
        const auto writes=mutations(p.a.backend.at(0,EvaluationNamespace::membership));
        CHECK(p.a.owner->revoke_local_membership().membership==CS::verified_terminal);
        CHECK(mutations(p.a.backend.at(0,EvaluationNamespace::membership))==writes);
        CHECK(bytes(p.a.identity)==identity && bytes(p.a.journal)==journal && bytes(p.a.binding)==binding && bytes(p.a.boot)==boot);
        CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::boot))==ledger);no_traffic(p.a);
        p.a.restart();CHECK(!p.a.owner->start_rekey(p.a.role,17));++groups;
    }
    for(unsigned variant=0;variant<4;++variant) {
        FreshPair p(true);p.activate();
        if(variant==0)CHECK(p.a.owner->close());
        if(variant==1){p.a.authority.value.context.runtime++;CHECK(!p.a.owner->ready());}
        if(variant==2)p.a.device.release_ok=false;
        if(variant==3){p.restart_retained();p.a.authority.value.now_ms=p.a.request.request().deadline_ms;p.a.authority.value.phase=DeviceNamePhase::disconnected;}
        const auto result=p.a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.resources_released==(variant!=2));
        CHECK(result.membership==CS::verified_terminal && retained_member(p.a).state==MembershipState::revoked);
        no_traffic(p.a);++groups;
    }
    {
        FreshPair p(true);const auto original=p.a.request.request();p.a.replace_request();
        const auto result=p.a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.membership==CS::verified_absent);
        CHECK(p.a.request.pending() && p.a.request.request().delivery_token!=original.delivery_token);no_traffic(p.a);++groups;
    }
    {
        FreshNode a;const auto result=a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.membership==CS::unavailable && a.request.pending());
        CHECK(mutations(a.backend.at(0,EvaluationNamespace::membership))==0);++groups;
    }
    for(unsigned reset=0;reset<2;++reset) {
        FreshPair p(true);p.activate();CHECK(p.a.owner->close());p.a.refill(180);p.a.admit();
        EnrollmentCandidateSession newer(p.a.request,p.a.authority,p.a.device,p.a.random,p.a.identity,p.a.journal,p.a.binding,p.a.backend,4,41,&p.a.boot);
        CHECK(newer.start_rekey(p.a.role,17));
        const auto member=bytes(p.a.backend.at(0,EvaluationNamespace::membership)),evidence=bytes(p.a.backend.at(0,EvaluationNamespace::enrollment)),binding=bytes(p.a.binding);
        ResetMarker marker;const auto result=reset?p.a.owner->contain_after_reset_intent(marker):p.a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.membership==CS::unavailable && p.a.request.pending());
        CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::membership))==member && bytes(p.a.backend.at(0,EvaluationNamespace::enrollment))==evidence && bytes(p.a.binding)==binding);
        CHECK(newer.close());++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshPair p(true);p.activate();
        if(variant==1)CHECK(p.a.owner->revoke_local_membership().membership==CS::verified_terminal);
        if(variant==2){p.restart_retained();CHECK(p.a.owner->close());}
        ResetMarker marker;IndependentInvitation original{};
        EnrollmentEvidenceStore before(p.a.backend.at(0,EvaluationNamespace::enrollment));CHECK(before.initialize() && before.read(original));
        const auto identity=bytes(p.a.identity),journal=bytes(p.a.journal),boot=bytes(p.a.boot),ledger=bytes(p.a.backend.at(0,EvaluationNamespace::boot));
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.volatile_cleared && result.resources_released && result.reset_intent_verified && !result.interrupted);
        CHECK(result.membership==CS::verified_terminal && result.evidence==CS::verified_terminal && result.binding==CS::verified_terminal);
        CHECK(retained_member(p.a).state==MembershipState::reset_pending);
        EnrollmentEvidenceStore evidence(p.a.backend.at(0,EvaluationNamespace::enrollment));IndependentInvitation prior{};
        CHECK(evidence.initialize() && !evidence.empty() && !evidence.read(prior));
        EnrollmentBindingStore binding(p.a.binding);std::optional<VerifiedIdentityBinding> proof;
        CHECK(binding.initialize() && !binding.empty() && !binding.read(original,proof));
        const auto mw=mutations(p.a.backend.at(0,EvaluationNamespace::membership)),ew=mutations(p.a.backend.at(0,EvaluationNamespace::enrollment)),bw=mutations(p.a.binding);
        CHECK(p.a.owner->contain_after_reset_intent(marker).binding==CS::verified_terminal);
        CHECK(p.a.owner->revoke_local_membership().membership==CS::unavailable);
        CHECK(mutations(p.a.backend.at(0,EvaluationNamespace::membership))==mw && mutations(p.a.backend.at(0,EvaluationNamespace::enrollment))==ew && mutations(p.a.binding)==bw);
        CHECK(bytes(p.a.identity)==identity && bytes(p.a.journal)==journal && bytes(p.a.boot)==boot && bytes(p.a.backend.at(0,EvaluationNamespace::boot))==ledger);
        no_traffic(p.a);p.a.restart();CHECK(!p.a.owner->start_rekey(p.a.role,17));++groups;
    }
    {
        FreshPair p(true);ResetMarker marker;p.a.device.release_ok=false;
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.volatile_cleared && !result.resources_released && result.membership==CS::verified_absent);
        CHECK(result.evidence==CS::verified_terminal && result.binding==CS::verified_terminal);no_traffic(p.a);++groups;
    }
    for(unsigned variant=0;variant<7;++variant) {
        FreshPair p(true);p.activate();ResetMarker marker;
        if(variant==0)marker.value.state=DeviceFactoryResetMarkerState::absent;
        if(variant==1)marker.value.state=DeviceFactoryResetMarkerState::receipt_pending;
        if(variant==2)marker.value.state=DeviceFactoryResetMarkerState::invalid;
        if(variant==3)marker.value.error=DeviceFactoryResetPortError::uncertain;
        if(variant==4)marker.callback=[&]{if(marker.reads==2)++marker.value.reset_receipt;};
        if(variant==5)marker.callback=[&]{if(marker.reads==2)marker.value.state=DeviceFactoryResetMarkerState::receipt_pending;};
        if(variant==6)marker.value.error=DeviceFactoryResetPortError::known_no_change;
        const auto member=bytes(p.a.backend.at(0,EvaluationNamespace::membership)),evidence=bytes(p.a.backend.at(0,EvaluationNamespace::enrollment)),binding=bytes(p.a.binding);
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.volatile_cleared && !result.reset_intent_verified && result.membership==CS::unavailable);
        CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::membership))==member && bytes(p.a.backend.at(0,EvaluationNamespace::enrollment))==evidence && bytes(p.a.binding)==binding);++groups;
    }
    for(auto fault:{Fault::write_before,Fault::write_after,Fault::partial_write})for(unsigned ordinal=1;ordinal<=3;++ordinal) {
        FreshPair p(true);p.activate();auto& member=p.a.backend.at(0,EvaluationNamespace::membership);member.arm(fault,ordinal);
        const auto result=p.a.owner->revoke_local_membership();
        CHECK(result.volatile_cleared && result.membership==CS::unavailable);member.inner.clear();
        PeerMembershipStore read(member);PeerMembership value{};const bool available=read.initialize() && read.read(value);
        if(fault==Fault::write_before && ordinal==1)CHECK(available && value.state==MembershipState::active);
        else CHECK(!available || value.state==MembershipState::revoked);
        no_traffic(p.a);p.a.restart();const bool resumed=p.a.owner->start_rekey(p.a.role,17);
        CHECK(resumed==(fault==Fault::write_before && ordinal==1));++groups;
    }
    for(unsigned domain_index=0;domain_index<3;++domain_index) {
        FreshPair p(true);p.activate();ResetMarker marker;
        auto& failed=domain_index==0?p.a.backend.at(0,EvaluationNamespace::membership):domain_index==1?p.a.backend.at(0,EvaluationNamespace::enrollment):p.a.binding;
        failed.arm(Fault::write_before);const auto result=p.a.owner->contain_after_reset_intent(marker);failed.inner.clear();
        CHECK(result.volatile_cleared);
        CHECK(result.membership==(domain_index==0?CS::unavailable:CS::verified_terminal));
        CHECK(result.evidence==(domain_index==1?CS::unavailable:CS::verified_terminal));
        CHECK(result.binding==(domain_index==2?CS::unavailable:CS::verified_terminal));
        no_traffic(p.a);p.a.restart();CHECK(!p.a.owner->start_rekey(p.a.role,17));++groups;
    }
    for(unsigned variant=0;variant<6;++variant) {
        FreshPair p(true);p.activate();ResetMarker marker;bool fired=false;
        auto nested=[&]{if(fired)return;fired=true;
            if(variant==0)(void)p.a.owner->revoke_local_membership();
            if(variant==1)(void)p.a.owner->contain_after_reset_intent(marker);
            if(variant==2)(void)p.a.owner->ready();
            if(variant==3)(void)p.a.owner->cancel();
            if(variant==4)(void)p.a.owner->close();
            if(variant==5)(void)p.a.owner->show_peer();};
        if(variant==5)p.a.device.callback=nested;
        else p.a.backend.at(0,EvaluationNamespace::membership).callback=[&](char op){if(op=='w')nested();};
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(fired && result.interrupted && result.volatile_cleared);
        CHECK(result.membership==CS::verified_terminal && result.evidence==CS::verified_terminal && result.binding==CS::verified_terminal);
        CHECK(retained_member(p.a).state==MembershipState::reset_pending);++groups;
    }
    {
        FreshPair p(true);p.activate();ResetMarker marker;bool fired=false;
        p.a.backend.at(0,EvaluationNamespace::membership).callback=[&](char op){if(op=='w' && !fired){fired=true;p.a.replace_request();}};
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(fired && result.membership==CS::verified_terminal && p.a.request.pending());++groups;
    }
    {
        FreshPair p(true);p.activate();ResetMarker marker;bool fired=false;
        p.a.backend.at(0,EvaluationNamespace::membership).callback=[&](char op){if(op=='w' && !fired){fired=true;++marker.value.reset_receipt;}};
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(fired && !result.reset_intent_verified && result.membership==CS::unavailable && result.evidence==CS::unavailable && result.binding==CS::unavailable);
        no_traffic(p.a);++groups;
    }
    {
        FreshPair p(true);bool fired=false;
        p.a.device.callback=[&]{if(!fired){fired=true;CHECK(p.a.owner->revoke_local_membership().interrupted);}};
        CHECK(!p.a.owner->show_peer());CHECK(fired && p.a.owner->secrets_cleared());++groups;
    }
    for(auto fault:{Fault::erase_before,Fault::sync_after}) {
        FreshPair p(true);p.activate();auto& member=p.a.backend.at(0,EvaluationNamespace::membership);member.arm(fault);
        const auto result=p.a.owner->revoke_local_membership();member.inner.clear();
        CHECK(result.volatile_cleared && result.membership==CS::unavailable);
        PeerMembershipStore reconstructed(member);CHECK(!reconstructed.initialize());
        p.a.restart();CHECK(!p.a.owner->start_rekey(p.a.role,17));++groups;
    }
    for(unsigned variant=0;variant<2;++variant) {
        FreshPair p(true);p.activate();CHECK(p.a.owner->close());auto& ledger=p.a.backend.at(0,EvaluationNamespace::boot);
        if(variant==0)ledger.arm(Fault::read_error);
        else ledger.inner.memory.corrupt_byte(domain,0,9,1);
        const auto member=bytes(p.a.backend.at(0,EvaluationNamespace::membership)),evidence=bytes(p.a.backend.at(0,EvaluationNamespace::enrollment)),binding=bytes(p.a.binding);
        ResetMarker marker;const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.volatile_cleared && result.membership==CS::unavailable && result.evidence==CS::unavailable && result.binding==CS::unavailable);
        CHECK(bytes(p.a.backend.at(0,EvaluationNamespace::membership))==member && bytes(p.a.backend.at(0,EvaluationNamespace::enrollment))==evidence && bytes(p.a.binding)==binding);
        ledger.inner.clear();++groups;
    }
    for(unsigned variant=0;variant<3;++variant) {
        FreshPair p(true);p.activate();ResetMarker marker;
        auto& broken=variant==0?p.a.backend.at(0,EvaluationNamespace::membership):variant==1?p.a.backend.at(0,EvaluationNamespace::enrollment):p.a.binding;
        broken.inner.memory.corrupt_byte(domain,0,9,1);const auto original=bytes(broken);const auto writes=mutations(broken);
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.volatile_cleared && bytes(broken)==original && mutations(broken)==writes);
        CHECK(result.membership==(variant==0?CS::unavailable:CS::verified_terminal));
        CHECK(result.evidence==(variant==1?CS::unavailable:CS::verified_terminal));
        CHECK(result.binding==(variant==2?CS::unavailable:CS::verified_terminal));++groups;
    }
    {
        FreshPair p(true);p.activate();ResetMarker marker;marker.value.reset_receipt=0; // Legitimate local physical reset.
        const auto result=p.a.owner->contain_after_reset_intent(marker);
        CHECK(result.reset_intent_verified && result.membership==CS::verified_terminal && result.evidence==CS::verified_terminal && result.binding==CS::verified_terminal);++groups;
    }
    {
        FreshPair p(true);p.activate();bool fired=false;GenerationLedgerStorage ledger(p.a.backend);
        SessionGenerationAllocator replacement(ledger,p.a.backend,4);
        auto& member=p.a.backend.at(0,EvaluationNamespace::membership);const auto original=bytes(member);const auto writes=mutations(member);
        member.callback=[&](char op){if(op=='r' && !fired){fired=true;CHECK(replacement.initialize());std::uint64_t next{};CHECK(replacement.allocate(next) && next==2);}};
        const auto result=p.a.owner->revoke_local_membership();
        CHECK(fired && result.membership==CS::unavailable && result.volatile_cleared);
        CHECK(bytes(member)==original && mutations(member)==writes);++groups;
    }
    std::cout<<"PASS "<<groups<<" enrollment candidate session groups\n";
}
#endif
