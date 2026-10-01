#include "product_enrollment_fixture.hpp"
#include "esp_stub.hpp"
#include "esp_timer.h"
#include "heltec_enrollment_input_arbiter.hpp"
#include "heltec_v4_oled.hpp"
#include "opentrail/enrollment_session_review_device_port.hpp"

using namespace product_enrollment_test;
using namespace opentrail::companion;
using namespace opentrail::target::heltec_v4_bench;
namespace stub=heltec_oled_stub;
namespace device_session_test {
using Event=CompanionFactoryResetGestureEvent;
using Phase=CompanionFactoryResetGesturePhase;
using Device=EnrollmentSessionReviewDevicePort<HeltecEnrollmentInputArbiter>;

// The existing SDK fixture is a singleton. Each board has its own SDK state;
// only the currently serviced board is installed while its app task executes.
// No copied observation substitutes for the arbiter's real GPIO/time sampling.
struct SdkScope {
    stub::State& saved;
    explicit SdkScope(stub::State& state):saved(state){std::swap(stub::state,saved);}
    ~SdkScope(){std::swap(stub::state,saved);}
};
struct Authority final : DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    std::function<void()> callback=[]{};
    DeviceNameAuthority current() noexcept override {
        callback();const auto raw=esp_timer_get_time();
        value.now_ms=raw<0?0:static_cast<std::uint64_t>(raw/1000);
        return value;
    }
};
struct Node {
    CallbackStorage identity,journal,binding,boot;
    Backend backend;security::test_support::FakeSecureRandomSource random;
    stub::State sdk;HeltecV4Oled oled;StartupDisplayOwner display{oled};
    HeltecEnrollmentInputArbiter io{display};Authority authority;
    SelectedEnrollmentRequestOwner request;std::optional<Device> device;
    std::optional<EnrollmentCandidateSession> owner;
    std::uint64_t storage_cost_us{},compute_cost_us{},storage_operations{},compute_operations{};
    unsigned reset_prompts{},reset_cancellations{},reset_commits{};
    std::vector<Event> dispatched;
    InvitationRole role;EvaluationEnrollmentCandidate candidate{};EvaluationEnrollmentOffer offer{};
    explicit Node(InvitationRole r,unsigned seed,std::uint64_t initial):role(r) {
        sdk.now_us=static_cast<std::int64_t>(initial*1000);authority.value.context.device=seed;
        {SdkScope scope(sdk);CHECK(display.start());CHECK(io.initialize());}
        advance(40,false);refill(seed);admit();construct();
    }
    ~Node(){SdkScope scope(sdk);owner.reset();device.reset();}
    // Follow the target's app-task reset event obligations. SDK-only tests do
    // not execute reset persistence/reboot; a commit would remain an explicit
    // dispatcher obligation rather than discarded or called completion.
    void dispatch(Event event) {
        if(event==Event::none)return;
        dispatched.push_back(event);
        if(event==Event::prompt_requested){++reset_prompts;CHECK(display.show_factory_reset_confirmation());}
        else if(event==Event::prompt_cancelled){++reset_cancellations;(void)display.clear_factory_reset_confirmation();}
        else if(event==Event::commit_requested)++reset_commits; // Intent/executor acceptance belongs downstream.
    }
    template<class F> auto call(F f) {
        SdkScope scope(sdk);dispatch(io.poll());
        if(compute_cost_us){stub::state.now_us+=static_cast<std::int64_t>(compute_cost_us);++compute_operations;}
        return f();
    }
    std::uint64_t now() const{return static_cast<std::uint64_t>(sdk.now_us/1000);}
    Event advance(std::uint64_t amount,bool down) {
        sdk.now_us+=static_cast<std::int64_t>(amount*1000);sdk.button_level=down?0:1;
        SdkScope scope(sdk);const auto event=io.poll();dispatch(event);return event;
    }
    void costed(std::uint64_t storage_us=100) {
        sdk.clock_read_cost_us=1000;sdk.gpio_read_cost_us=1000;sdk.draw_cost_us=3000;
        storage_cost_us=storage_us;compute_cost_us=7000;
        const auto callback=[this](char){++storage_operations;stub::state.now_us+=static_cast<std::int64_t>(storage_cost_us);};
        for(auto* store:{&identity,&journal,&binding,&boot})store->callback=callback;
        for(auto& generation:backend.stores)for(auto& store:generation)store.callback=callback;
    }
    void refill(unsigned seed) {
        std::array<std::uint8_t,512> bytes{};
        for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(seed+i);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);
    }
    void admit(){call([&]{CHECK(request.admit(authority.current(),8,9,10)==SelectedEnrollmentRequestResult::admitted);});}
    void construct(){device.emplace(io);owner.emplace(request,authority,*device,random,identity,journal,binding,backend,4,41,&boot);}
    void start(){call([&]{CHECK(owner->start(role,17));CHECK(owner->export_candidate(candidate));});}
    void restart(unsigned seed) {
        call([&]{CHECK(owner->close());owner.reset();device.reset();});
        authority.callback=[]{};sdk.on_draw=[]{};++authority.value.context.runtime;
        advance(1,false);advance(40,false);refill(seed);admit();construct();
    }
    bool poll(bool confirmation) {return call([&]{return confirmation?owner->poll_confirmation():owner->poll_review();});}
    bool at(std::uint64_t delta,bool down,bool confirmation=false) {
        advance(delta,down);return poll(confirmation);
    }
    void gesture(bool confirmation=false) {
        CHECK(at(20,false,confirmation));CHECK(at(1,true,confirmation));
        CHECK(at(20,true,confirmation));CHECK(at(1000,false,confirmation));
        CHECK(at(20,false,confirmation));
    }
    void review() {
        call([&]{CHECK(owner->show_peer());});
        CHECK(display.enrollment_review_status().revision==2);gesture();
        call([&]{CHECK(owner->finish_review(offer));});
        CHECK(offer.context.generation && offer.context.request==request.request().delivery_token);
    }
    EnrollmentJournalState journal_state(){return call([&]{EnrollmentCommitCoordinator read(journal);CHECK(read.initialize());return read.state();});}
    void no_traffic(bool cleared=true) {
        call([&]{EvaluationRecord out{};out.group=912;std::uint8_t value=91;
            CHECK(!owner->send_status(1,out));CHECK(out.group==912);
            CHECK(!owner->receive_status(out,value));CHECK(value==91 && !owner->ready());
            if(cleared)CHECK(owner->secrets_cleared());});
    }
};
struct Pair {
    Node a{InvitationRole::initiator,1,100},b{InvitationRole::responder,81,50100};
    bool retained{};unsigned epoch{1};
    std::uint64_t elapsed_start_a{},elapsed_start_b{};unsigned transfers{};
    std::uint64_t refusal_before{},refusal_last_authority{},refusal_after{},refusal_request_deadline{};
    explicit Pair(bool costs=false,std::uint64_t storage_us=100){if(costs){a.costed(storage_us);b.costed(storage_us);}elapsed_start_a=a.now();elapsed_start_b=b.now();
        a.start();b.start();a.call([&]{CHECK(a.owner->receive_candidate(b.candidate));});b.call([&]{CHECK(b.owner->receive_candidate(a.candidate));});}
    void reconstruct(){a.restart(131+epoch);b.restart(211+epoch);retained=true;++epoch;}
    void rekey(){reconstruct();a.call([&]{CHECK(a.owner->start_rekey(a.role,17));});b.call([&]{CHECK(b.owner->start_rekey(b.role,17));});}
    void review() {
        if(!retained){a.review();b.review();return;}
        RetainedEnrollmentChallenge ac{},bc{};RetainedEnrollmentResponse ar{},br{};
        a.call([&]{CHECK(a.owner->begin_retained_comparison(ac));});b.call([&]{CHECK(b.owner->begin_retained_comparison(bc));});
        a.call([&]{CHECK(a.owner->sign_retained_challenge(bc,ar));});b.call([&]{CHECK(b.owner->sign_retained_challenge(ac,br));});
        a.call([&]{CHECK(a.owner->finish_retained_comparison(br,a.offer));});b.call([&]{CHECK(b.owner->finish_retained_comparison(ar,b.offer));});
    }
    void begin() {
        review();std::array<std::uint8_t,64> as{},bs{};
        a.call([&]{CHECK(a.owner->prepare_possession(b.offer,as));});b.call([&]{CHECK(b.owner->prepare_possession(a.offer,bs));});
        a.call([&]{CHECK(a.owner->accept_possession(bs));});b.call([&]{CHECK(b.owner->accept_possession(as));});
        EvaluationEnrollmentClockMark am{},bm{};
        a.call([&]{CHECK(a.owner->clock_mark(am));});b.call([&]{CHECK(b.owner->clock_mark(bm));});CHECK(am.now_ms!=bm.now_ms);
        a.call([&]{CHECK(a.owner->receive_clock_mark(bm));});b.call([&]{CHECK(b.owner->receive_clock_mark(am));});
        IndependentInvitation invitation{};a.call([&]{CHECK(a.owner->issue_invitation(invitation));});
        const auto fields=independent_invitation_detail::decode(invitation);
        CHECK(fields.epoch==epoch && fields.issued_a_ms==am.now_ms && fields.issued_b_ms==bm.now_ms);
        CHECK(fields.window_a_ms==60000 && fields.window_b_ms==60000);
        a.call([&]{CHECK(a.owner->sign_invitation(invitation,as));});b.call([&]{CHECK(b.owner->sign_invitation(invitation,bs));});
        a.call([&]{CHECK(a.owner->begin_handshake(bs));});b.call([&]{CHECK(b.owner->begin_handshake(as));});
    }
    static void handshake_transfer(Node& from,Node& to) {
        HandshakeFrame frame{};from.call([&]{CHECK(from.owner->next_handshake(frame));});to.call([&]{CHECK(to.owner->receive_handshake(frame));});
    }
    void handshake(){begin();handshake_transfer(a,b);handshake_transfer(b,a);handshake_transfer(a,b);}
    void confirmation() {
        handshake();for(auto* n:{&a,&b}){CHECK(n->poll(true));
            CHECK(n->display.enrollment_review_status().revision==(retained?1U:3U));n->gesture(true);}
    }
    static void control(Node& from,Node& to) {
        EvaluationRecord record{};from.call([&]{CHECK(from.owner->next_control(record));});to.call([&]{CHECK(to.owner->receive_control(record));});
    }
    void controls(){const auto ad=a.request.request().deadline_ms,bd=b.request.request().deadline_ms;
        confirmation();control(a,b);control(b,a);control(a,b);control(b,a);
        CHECK(a.request.request().deadline_ms==ad && b.request.request().deadline_ms==bd);}
    void commit(){a.call([&]{CHECK(a.owner->commit() && a.owner->ready());});b.call([&]{CHECK(b.owner->commit() && b.owner->ready());});}
    void activate(){controls();commit();}
    void statuses(){for(unsigned i=1;i<=4;++i)for(auto* from:{&a,&b}){auto& to=from==&a?b:a;EvaluationRecord r{};std::uint8_t value{};
        from->call([&]{CHECK(from->owner->send_status(static_cast<std::uint8_t>(i),r));});
        to.call([&]{CHECK(to.owner->receive_status(r,value));});CHECK(value==i);++transfers;}}
    bool recovery(bool completion_required=true) {
        reconstruct();a.call([&]{CHECK(a.owner->start_recovery(a.role,17));});b.call([&]{CHECK(b.owner->start_recovery(b.role,17));});
        std::optional<EnrollmentIdentityProof> aa,bb;
        a.call([&]{CHECK(a.owner->export_recovery_archive(aa));});b.call([&]{CHECK(b.owner->export_recovery_archive(bb));});CHECK(aa || bb);
        if(!aa)a.call([&]{CHECK(a.owner->accept_recovery_archive(*bb));});
        if(!bb)b.call([&]{CHECK(b.owner->accept_recovery_archive(*aa));});
        EnrollmentRecoveryChallenge ac{},bc{};EnrollmentRecoveryResponse ar{},br{};
        a.call([&]{CHECK(a.owner->begin_recovery_comparison(ac));});b.call([&]{CHECK(b.owner->begin_recovery_comparison(bc));});
        a.call([&]{CHECK(a.owner->sign_recovery_challenge(bc,ar));});b.call([&]{CHECK(b.owner->sign_recovery_challenge(ac,br));});
        a.call([&]{CHECK(a.owner->finish_recovery_comparison(br));});const bool ok=b.call([&]{
            refusal_before=static_cast<std::uint64_t>(stub::state.now_us/1000);refusal_request_deadline=b.request.request().deadline_ms;
            const bool ok=b.owner->finish_recovery_comparison(ar);
            refusal_last_authority=b.authority.value.now_ms;refusal_after=static_cast<std::uint64_t>(stub::state.now_us/1000);
            return ok;});
        if(completion_required)CHECK(ok);
        if(ok)CHECK(a.journal_state()==EnrollmentJournalState::active_committed && b.journal_state()==EnrollmentJournalState::active_committed);
        return ok;
    }
};
}
namespace device_session_test {
int run(){unsigned groups=0;
    struct Elapsed {unsigned mode,transfers;std::uint64_t a,b,storage_a,storage_b,compute_a,compute_b;};
    std::vector<Elapsed> elapsed;
    std::array<std::uint64_t,4> pressure{};
    // Actual stores, cryptography and OLED/input owners complete first setup.
    {Pair p;const auto deadline=p.a.request.request().deadline_ms;p.controls();
     CHECK(p.a.request.request().deadline_ms==deadline);p.commit();p.statuses();
     CHECK(!p.a.request.pending() && !p.b.request.pending());
     CHECK(p.a.journal_state()==EnrollmentJournalState::active_committed && p.b.journal_state()==EnrollmentJournalState::active_committed);
     p.a.call([&]{CHECK(p.a.owner->close());});p.b.call([&]{CHECK(p.b.owner->close());});
     CHECK(!p.a.display.enrollment_review_status().lease && !p.b.display.enrollment_review_status().lease);
     CHECK(p.a.sdk.bounds_valid && p.b.sdk.bounds_valid && p.a.sdk.gpio_reads>0 && p.b.sdk.gpio_reads>0);
     CHECK(p.a.sdk.frames.size()>=5 && p.b.sdk.frames.size()>=5);++groups;}
    {Pair p;p.activate();p.statuses();p.rekey();p.activate();p.statuses();CHECK(p.epoch==2);++groups;}
    // A peer with an actual committed archive can recover the other endpoint,
    // then both still need a fresh invitation, transcript gesture and commit.
    for(bool reverse:{false,true}){Pair p;p.controls();auto& committed=reverse?p.b:p.a;
     committed.call([&]{CHECK(committed.owner->commit());});p.recovery();
     p.activate();p.statuses();CHECK(p.epoch==2);++groups;}
    // Raw bounce and held entry never replace a fresh stable page gesture.
    for(unsigned variant=0;variant<2;++variant){Pair p;
     if(variant==0)p.a.advance(1,true);
     p.a.call([&]{CHECK(p.a.owner->show_peer());});
     if(variant==0){CHECK(p.a.at(1100,true));CHECK(p.a.at(1,false));CHECK(p.a.at(20,false));}
     else {CHECK(p.a.at(20,false));CHECK(p.a.at(1,true));CHECK(p.a.at(9,false));CHECK(p.a.at(10,true));CHECK(p.a.at(10,false));CHECK(p.a.at(1100,false));}
     EvaluationEnrollmentOffer out{};out.group=77;p.a.call([&]{CHECK(!p.a.owner->finish_review(out));});CHECK(out.group==77);p.a.no_traffic();++groups;}
    // The old page's stable held press does not confirm a new page/revision.
    {Pair p;p.a.call([&]{CHECK(p.a.owner->show_peer());});CHECK(p.a.at(20,false));CHECK(p.a.at(1,true));CHECK(p.a.at(20,true));
     p.a.call([&]{CHECK(p.a.owner->show_local() && p.a.owner->show_peer());});
     CHECK(p.a.at(1100,false));CHECK(p.a.at(20,false));EvaluationEnrollmentOffer out{};
     p.a.call([&]{CHECK(!p.a.owner->finish_review(out));});p.a.no_traffic();++groups;}
    // Identity review's released gesture cannot be reused as transcript consent.
    {Pair p;p.handshake();CHECK(p.a.poll(true));CHECK(p.a.at(1100,false,true));
     EvaluationRecord out{};out.group=92;p.a.call([&]{CHECK(!p.a.owner->next_control(out));});CHECK(out.group==92);p.a.no_traffic();++groups;}
    // Late rendering is charged to the original selected-request deadline.
    {Pair p;const auto deadline=p.a.request.request().deadline_ms;bool fired=false;
     p.a.sdk.on_draw=[&]{if(!fired){fired=true;stub::state.now_us=static_cast<std::int64_t>(deadline*1000);}};
     p.a.call([&]{CHECK(!p.a.owner->show_peer());});CHECK(fired);p.a.no_traffic();
     CHECK(!p.a.request.pending() && !p.a.display.enrollment_review_status().lease);++groups;}
    // Reset can preempt between rendering and publication. Its overlay survives
    // session cleanup and the old enrollment gesture cannot commit reset.
    {Pair p;p.a.call([&]{CHECK(p.a.owner->show_peer());});CHECK(p.a.at(20,false));CHECK(p.a.at(1,true));CHECK(p.a.at(40,true));
     p.a.advance(9950,true);bool fired=false;p.a.sdk.on_draw=[&]{if(!fired){fired=true;stub::state.now_us+=20000;}};
     p.a.call([&]{CHECK(!p.a.owner->show_local());});CHECK(fired && p.a.io.status().phase==Phase::prompt_while_held);
     const auto reset_frame=p.a.sdk.frames.back();CHECK(p.a.advance(1,true)==Event::prompt_requested);
     p.a.no_traffic();CHECK(p.a.sdk.frames.back()==reset_frame && !p.a.display.enrollment_review_status().lease);
     p.a.advance(1,false);p.a.advance(40,false);
     CHECK(p.a.io.status().phase==Phase::confirmation_ready);p.a.advance(1000,false);CHECK(p.a.io.status().phase==Phase::confirmation_ready);++groups;}
    for(unsigned variant=0;variant<2;++variant){Pair p;
     if(variant==0){p.a.sdk.draw_failures=1;p.a.call([&]{CHECK(!p.a.owner->show_peer());});}
     else {p.a.call([&]{CHECK(p.a.owner->show_peer());});p.a.sdk.draw_failures=1;p.a.call([&]{CHECK(!p.a.owner->cancel());});}
     p.a.no_traffic();p.a.call([&]{CHECK(!p.a.owner->close());});
     CHECK(!p.a.display.status().available && !p.a.display.enrollment_review_status().lease);++groups;}
    // Cancellation and callback reentry poison the real serialized composition.
    for(unsigned variant=0;variant<3;++variant){Pair p;bool fired=false;
     p.a.sdk.on_draw=[&]{if(!fired){fired=true;if(variant==0)(void)p.a.owner->cancel();
        else if(variant==1)(void)p.a.owner->show_local();else CHECK(p.a.request.cancel_exact(p.a.request.request()));}};
     p.a.call([&]{CHECK(!p.a.owner->show_peer());});CHECK(fired);p.a.no_traffic();CHECK(!p.a.display.enrollment_review_status().lease);++groups;}
    // A replacement context/bind can never steal the live real display lease.
    {Pair p;FingerprintReviewContext context{};p.a.call([&]{context=p.a.device->sample().context;});CHECK(context.generation);
     auto wrong=context;++wrong.request;p.a.call([&]{CHECK(!p.a.io.bind_admitted_context(wrong));CHECK(!p.a.io.release_admitted_context(wrong));});
     CHECK(p.a.display.enrollment_review_status().lease);
     p.a.call([&]{CHECK(!p.a.owner->cancel());CHECK(p.a.owner->close());});p.a.no_traffic();++groups;}
    // An admitted context whose lease is refused must be released without
    // erasing the pairing page or trapping the next exact local owner.
    {Node n(InvitationRole::initiator,1,100),peer(InvitationRole::responder,81,50100);n.start();peer.start();
     n.call([&]{CHECK(n.display.show_pairing_pin({'1','2','3','4','5','6'}));});
     const auto visible=n.sdk.frames.back();const auto draws=n.sdk.frames.size();
     n.call([&]{CHECK(!n.owner->receive_candidate(peer.candidate));CHECK(n.owner->close());});
     CHECK(n.sdk.frames.size()==draws && n.sdk.frames.back()==visible);
     FingerprintReviewContext next{};next.boot.fill(7);next.generation=97;next.request=99;
     n.call([&]{CHECK(n.io.bind_admitted_context(next));auto stale=next;--stale.request;
        CHECK(!n.io.release_admitted_context(stale));EnrollmentDeviceObservation observed{};
        CHECK(n.io.observe(observed) && observed.context==next);CHECK(n.io.release_admitted_context(next));});
     CHECK(n.sdk.frames.size()==draws && n.sdk.frames.back()==visible);n.no_traffic();++groups;}
    // The adapter owns a fresh input sample even when no outer app tick occurs.
    {Pair p;p.a.sdk.now_us+=1000;const auto reads=p.a.sdk.gpio_reads;
     {SdkScope scope(p.a.sdk);CHECK(p.a.owner->show_peer());}
     CHECK(p.a.sdk.gpio_reads>reads);p.a.gesture();p.a.call([&]{CHECK(p.a.owner->finish_review(p.a.offer));});++groups;}
    // The adapter's fresh sample must cover a later independent authority clock
    // sample; the previous cached adapter fails this decisive one-ms regression.
    {Pair p;bool fired=false;p.a.authority.callback=[&]{if(!fired){fired=true;stub::state.now_us+=1000;}};
     p.a.call([&]{CHECK(p.a.owner->show_peer());});p.a.authority.callback=[]{};
     CHECK(fired);p.a.gesture();p.a.call([&]{CHECK(p.a.owner->finish_review(p.a.offer));});++groups;}
    // Both independent SDK clocks advance on actual timer/GPIO/draw calls;
    // actual storage callbacks and a labelled operation cost model time spent
    // in storage/signing. These are virtual elapsed measurements, not benchmarks.
    for(unsigned mode=0;mode<4;++mode){Pair p(true);
     auto a_start=p.elapsed_start_a,b_start=p.elapsed_start_b;std::uint64_t sa{},sb{},ca{},cb{};unsigned previous_transfers{};
     const auto checkpoint=[&]{a_start=p.a.now();b_start=p.b.now();sa=p.a.storage_operations;sb=p.b.storage_operations;
        ca=p.a.compute_operations;cb=p.b.compute_operations;previous_transfers=p.transfers;};
     if(mode==1){p.activate();p.statuses();checkpoint();p.rekey();}
     if(mode>=2){p.controls();auto& committed=mode==3?p.b:p.a;
        committed.call([&]{CHECK(committed.owner->commit());});checkpoint();p.recovery();}
     p.activate();p.statuses();CHECK(p.transfers-previous_transfers==8);
     const auto a=p.a.now()-a_start,b=p.b.now()-b_start;
     CHECK(a>=(mode==0?2000U:1000U) && b>=(mode==0?2000U:1000U) && p.a.storage_operations>sa && p.b.storage_operations>sb && p.a.compute_operations>ca && p.b.compute_operations>cb);
     CHECK(p.a.sdk.clock_reads>p.a.sdk.gpio_reads && p.b.sdk.clock_reads>p.b.sdk.gpio_reads);
     elapsed.push_back({mode,p.transfers-previous_transfers,a,b,p.a.storage_operations-sa,p.b.storage_operations-sb,p.a.compute_operations-ca,p.b.compute_operations-cb});++groups;}
    // A deliberately slower, uncalibrated public-store profile must still
    // refuse rather than extending either the request or recovery window.
    {Pair p(true,1000);p.controls();p.a.call([&]{CHECK(p.a.owner->commit());});CHECK(!p.recovery(false));
     pressure={p.refusal_before,p.refusal_last_authority,p.refusal_after,p.refusal_request_deadline};
     CHECK(p.refusal_last_authority>=p.refusal_before && p.refusal_after<p.refusal_request_deadline);
     p.b.no_traffic();CHECK(!p.b.display.enrollment_review_status().lease);++groups;}
    // Exact original deadline: the last pre-deadline page can render, but a
    // later tick at the deadline cannot poll or expose review/traffic authority.
    {Pair p;const auto deadline=p.a.request.request().deadline_ms;p.a.sdk.now_us=static_cast<std::int64_t>((deadline-1)*1000);
     p.a.call([&]{CHECK(p.a.owner->show_peer());});CHECK(p.a.request.request().deadline_ms==deadline);
     CHECK(!p.a.at(1,false));p.a.no_traffic();CHECK(!p.a.display.enrollment_review_status().lease);++groups;}
    {Pair p;p.begin();p.a.advance(60000,false);HandshakeFrame frame{};frame.step=71;
     p.a.call([&]{CHECK(!p.a.owner->next_handshake(frame));});CHECK(frame.step==71);p.a.no_traffic();++groups;}
    // The completed draw's held sample cannot reuse time spent before visibility.
    {Pair p;bool fired=false;p.a.sdk.on_draw=[&]{if(!fired){fired=true;stub::state.now_us+=1000000;stub::state.button_level=0;}};
     p.a.call([&]{CHECK(p.a.owner->show_peer());});CHECK(fired);p.a.sdk.on_draw=[]{};
     CHECK(p.a.at(20,false));CHECK(p.a.at(20,false));EvaluationEnrollmentOffer out{};
     p.a.call([&]{CHECK(!p.a.owner->finish_review(out));});p.a.no_traffic();++groups;}
    // Fresh sampling remains guarded against caller cancellation, invalid time
    // and arbiter callback reentry. Hooks model the SDK decision boundary.
    for(unsigned variant=0;variant<3;++variant){Pair p;bool fired=false;
     p.a.sdk.on_gpio=[&]{if(!fired){fired=true;
        if(variant==0)CHECK(p.a.request.cancel_exact(p.a.request.request()));
        else if(variant==1)stub::state.now_us=-1000;
        else {EnrollmentDeviceObservation out{};CHECK(!p.a.io.observe(out));}}};
     {SdkScope scope(p.a.sdk);CHECK(!p.a.owner->show_peer());}
     p.a.sdk.on_gpio=[]{};CHECK(fired);p.a.no_traffic();CHECK(!p.a.display.enrollment_review_status().lease);++groups;}
    // Reset crosses its hold threshold during the adapter's fresh sample.
    // Sampling must preserve its event; the real consuming poll supplies it
    // once to the mirrored dispatcher, whose overlay remains after cleanup.
    for(bool draw_transition:{false,true}){Pair p;p.a.call([&]{CHECK(p.a.owner->show_peer());});
     CHECK(p.a.at(20,false));CHECK(p.a.at(1,true));CHECK(p.a.at(40,true));
     const auto press=p.a.now()-40; // Actual BOOT first-down observation.
     p.a.sdk.now_us=static_cast<std::int64_t>((press+9999)*1000);
     bool fired=false;
     if(draw_transition)p.a.sdk.on_draw=[&]{if(!fired){fired=true;stub::state.now_us+=2000;}};
     else p.a.sdk.on_clock=[&]{if(!fired){fired=true;stub::state.now_us+=2000;}};
     {SdkScope scope(p.a.sdk);CHECK(!p.a.owner->show_local());}
     p.a.sdk.on_draw=[]{};p.a.sdk.on_clock=[]{};CHECK(fired && p.a.io.status().phase==Phase::prompt_while_held);
     CHECK(p.a.reset_prompts==0 && p.a.dispatched.empty());const auto visible=p.a.sdk.frames.back();
     CHECK(p.a.advance(0,true)==Event::prompt_requested);CHECK(p.a.reset_prompts==1);
     CHECK(p.a.advance(0,true)==Event::none && p.a.reset_prompts==1 && p.a.reset_commits==0);
     p.a.no_traffic();CHECK(p.a.sdk.frames.back()==visible && !p.a.display.enrollment_review_status().lease);
     p.a.advance(1,false);p.a.advance(40,false);CHECK(p.a.io.status().phase==Phase::confirmation_ready && p.a.reset_commits==0);++groups;}
    // Release gives reset a fresh debounce interval; a held previous gesture
    // cannot enter reset until release and a whole new physical hold.
    {Pair p;p.a.call([&]{CHECK(p.a.owner->show_peer());});CHECK(p.a.at(20,false));CHECK(p.a.at(1,true));CHECK(p.a.at(40,true));
     p.a.call([&]{CHECK(!p.a.owner->cancel());CHECK(p.a.owner->close());});p.a.advance(20000,true);CHECK(p.a.io.status().phase==Phase::awaiting_initial_release);
     p.a.advance(1,false);p.a.advance(39,false);CHECK(p.a.io.status().phase==Phase::awaiting_initial_release);
     p.a.advance(1,false);CHECK(p.a.io.status().phase==Phase::idle);p.a.no_traffic();++groups;}
    std::cout<<"PASS "<<groups<<" actual candidate device session groups\n";
    for(const auto& row:elapsed)std::cout<<"SIMULATED mode="<<row.mode<<" transfers="<<row.transfers
        <<" elapsed_a_ms="<<row.a<<" elapsed_b_ms="<<row.b<<" storage_a="<<row.storage_a<<" storage_b="<<row.storage_b
        <<" compute_a="<<row.compute_a<<" compute_b="<<row.compute_b
        <<" clock_us=1000 gpio_us=1000 draw_us=3000 store_us=100 compute_us=7000\n";
    std::cout<<"REFUSAL store_us=1000 before_ms="<<pressure[0]<<" last_authority_ms="<<pressure[1]
        <<" after_ms="<<pressure[2]<<" original_request_deadline_ms="<<pressure[3]<<"\n";
    return 0;
}
}
int main(){return device_session_test::run();}
