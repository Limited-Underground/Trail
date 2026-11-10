// Controlled observations exercise the actual P1-P3 owners through the new port.
// Existing dispatch support is an explicit source/hash dependency, not rerun here.
#define main repeater_dispatch_baseline_main
#include "repeater_dispatch_controller_tests.cpp"
#undef main
#include "../../firmware/targets/sensecap_solar_p1_pro_smoke/src/p1pro_sx1262_radio_port.hpp"

namespace {
using namespace opentrail::p1pro;
class ControlledBackend final : public Sx1262Backend {
public:
    explicit ControlledBackend(FakeRadio& raw) : raw_(raw) {}
    bool snapshot(RepeaterDispatchRadioProof& out) noexcept override { return raw_.observe(out); }
    RepeaterDispatchSubmitResult submit_checked(const RepeaterDispatchAttempt& attempt,
        const RepeaterDispatchRadioProof& expected, opentrail::radio::ByteView bytes) noexcept override {
        return raw_.submit(attempt, expected, bytes);
    }
    bool take_terminal(std::uint64_t handle, RepeaterDispatchTerminal& out) noexcept override {
        return raw_.consume_terminal(handle, raw_.last_attempt, out);
    }
    bool take_rearm(std::uint64_t handle, RepeaterDispatchRearm& out) noexcept override {
        return raw_.consume_rearm(handle, raw_.last_attempt, out);
    }
    void invalidate() noexcept override { raw_.invalidate_quiescence(); }
private:
    FakeRadio& raw_;
};
struct AdapterHarness : Harness {
    ControlledBackend backend{radio};
    Sx1262RetainedState retained{};
    P1ProSx1262RadioPort port{backend, retained};
    AdapterHarness() { recreate_adapter(); }
    ~AdapterHarness() { controller.reset(); }
    void recreate_adapter() {
        controller.reset(); controller.emplace(config, provider, roles, replay, ledger, port);
    }
};
void no_retry(AdapterHarness& h, const RepeaterDispatchLedger& debit, std::uint64_t now) {
    const auto requests = h.radio.requests;
    (void)h.controller->service(now);
    EXPECT(h.radio.requests == requests);
    EXPECT(same(h.ledger.record, debit));
}

// A1: real owner's pending bytes -> one copied driver reservation -> explicit completion/rearm.
void accepted_copy_and_one_use() {
    AdapterHarness h; h.activate();
    std::array<std::uint8_t,255> bytes{};
    for (std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(i^0xa5);
    const auto original=bytes;
    EXPECT(h.controller->admit(h.prepare_frame(1,kSource,{bytes.data(),bytes.size()}),
        {bytes.data(),bytes.size()},10).forwarding.queued);
    EXPECT(h.controller->service(11)==DF::none);
    const auto debit=h.ledger.record;
    bytes.fill(0);
    EXPECT(h.retained.size==255 && h.radio.size==255);
    EXPECT(std::equal(original.begin(),original.end(),h.retained.bytes.begin()));
    EXPECT(std::equal(original.begin(),original.end(),h.radio.copy.begin()));
    EXPECT(h.controller->status().occupied_frames==1);
    const auto terminal=h.radio.issue_terminal();
    EXPECT(h.controller->terminal(terminal,411)==DF::none);
    EXPECT(h.controller->terminal(terminal,412)==DF::wrong_state);
    const auto rearm=h.radio.issue_rearm();
    EXPECT(h.controller->rearm(rearm,413)==DF::none);
    EXPECT(h.controller->rearm(rearm,414)==DF::wrong_state);
    EXPECT(h.retained.phase==Sx1262Phase::idle && h.retained.size==0);
    EXPECT(std::all_of(h.retained.bytes.begin(),h.retained.bytes.end(),[](auto b){return b==0;}));
    no_retry(h,debit,415);
}
// A2: wrong-phase rearm and field-mismatched terminal are not permission.
void out_of_order_and_stale_observations() {
    AdapterHarness h; start(h);
    const auto debit=h.ledger.record;
    const auto early=h.radio.issue_rearm();
    EXPECT(h.controller->rearm(early,12)==DF::wrong_state);
    EXPECT(h.retained.phase==Sx1262Phase::pending);
    const auto stale=h.radio.issue_terminal(); ++h.radio.terminal_value.attempt.budget_epoch;
    EXPECT(h.controller->terminal(stale,411)==DF::invalid_event);
    EXPECT(h.retained.phase==Sx1262Phase::pending);
    no_retry(h,debit,412);
    const auto good=h.radio.issue_terminal();
    EXPECT(h.controller->terminal(good,413)==DF::none);
    const auto fresh=h.radio.issue_rearm();
    EXPECT(h.controller->rearm(fresh,414)==DF::none);
    EXPECT(h.radio.requests==1);
}
// A3: BUSY/rejection debit is irreversible, and neither polling nor rearm retries.
void rejected_and_busy_no_refund() {
    for (auto outcome:{RepeaterDispatchSubmitOutcome::rejected,RepeaterDispatchSubmitOutcome::busy}) {
        AdapterHarness h; h.radio.outcome=outcome;h.activate();EXPECT(h.admit().forwarding.queued);
        (void)h.controller->service(11);
        EXPECT(h.controller->status().state==DS::awaiting_rearm);
        EXPECT(h.retained.phase==Sx1262Phase::awaiting_rearm);
        const auto debit=h.ledger.record;
        EXPECT(debit.airtime_remaining_us==kCost && debit.global_remaining==1);
        no_retry(h,debit,12);
        EXPECT(h.controller->rearm(h.radio.issue_rearm(),13)==DF::none);
        no_retry(h,debit,14);
        EXPECT(h.admit(1,15).forwarding.disposition==SingleRepeaterDisposition::duplicate);
        EXPECT(h.radio.requests==1 && h.radio.copies==0);
    }
}
// A4: possible emission and short acceptance survive reconstruction as uncertainty.
void uncertain_and_partial_restart() {
    for (unsigned mode=0;mode<3;++mode) {
        AdapterHarness h;
        h.radio.outcome=mode==0?RepeaterDispatchSubmitOutcome::uncertain:
            mode==1?RepeaterDispatchSubmitOutcome::partial:RepeaterDispatchSubmitOutcome::accepted_pending;
        h.radio.wrong_accepted_size=mode==2;
        h.activate();EXPECT(h.admit().forwarding.queued);
        EXPECT(h.controller->service(11)==DF::uncertain_emission);
        const auto debit=h.ledger.record;const auto replay=h.replay.image();
        EXPECT(h.retained.invalidated && h.retained.phase==Sx1262Phase::contained);
        EXPECT(h.controller->terminal(h.radio.issue_terminal(),411)==DF::wrong_state);
        EXPECT(h.controller->rearm(h.radio.issue_rearm(),412)==DF::wrong_state);
        no_retry(h,debit,413);
        ++h.config.lifecycle.instance_id;h.recreate_adapter();
        EXPECT(h.controller->recover(h.prepare_recover(),500)==DF::radio_not_ready);
        // Even forged readiness from this test backend cannot clear retained invalidation.
        h.radio.proof.receive_ready=true;h.radio.proof.verified_quiescent=true;
        ++h.radio.proof.readiness_generation;++h.radio.proof.quiescence_receipt;
        EXPECT(h.controller->recover(h.prepare_recover(),501)==DF::radio_not_ready);
        EXPECT(h.replay.image()==replay && same(h.ledger.record,debit) && h.radio.requests==1);
    }
}
// A5: absent/invalid rearm evidence never releases the queue or restores credit.
void rearm_failure_and_timeout() {
    for (unsigned mode=0;mode<3;++mode) {
        AdapterHarness h;start(h);
        EXPECT(h.controller->terminal(h.radio.issue_terminal(),411)==DF::none);
        const auto debit=h.ledger.record;
        if(mode==0) {
            EXPECT(h.controller->rearm(0,412)==DF::invalid_event);
            EXPECT(h.controller->service(1411)==DF::rearm_timeout);
        } else if(mode==1) {
            EXPECT(h.controller->notify(h.controller->status().lifecycle.generation,Event::rearm_failed,412)!=DF::none);
        } else {
            const auto bad=h.radio.issue_rearm();h.radio.rearm_value.proof.verified_quiescent=false;
            EXPECT(h.controller->rearm(bad,412)!=DF::none);
            // Missing trustworthy evidence must hit the existing bounded deadline.
            (void)h.controller->service(1411);
        }
        EXPECT(h.retained.invalidated);
        EXPECT(h.controller->status().lifecycle.occupied_frames==0);
        EXPECT(!h.controller->status().lifecycle.pending);
        EXPECT(h.controller->rearm(h.radio.issue_rearm(),1412)==DF::wrong_state);
        no_retry(h,debit,1413);
    }
}
// A6: saved-but-unsent frames retire through actual P2, with replay and credit preserved.
void suspend_saved_queue_restart() {
    AdapterHarness h;h.activate();
    EXPECT(h.admit(1,10).forwarding.queued);EXPECT(h.admit(2,11,kSecond).forwarding.queued);
    const auto debit=h.ledger.record;const auto replay=h.replay.image();
    EXPECT(h.controller->notify(h.controller->status().lifecycle.generation,Event::suspend,12)!=DF::none);
    EXPECT(h.controller->status().occupied_frames==0 && h.radio.requests==0);
    ++h.config.lifecycle.instance_id;h.recreate_adapter();
    EXPECT(h.controller->recover(h.prepare_recover(),20)==DF::none);
    EXPECT(h.controller->service(21)==DF::wrong_state);
    EXPECT(h.controller->consent(h.prepare_consent(),22)==DF::none);
    EXPECT(h.admit(1,23).forwarding.disposition==SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(2,24,kSecond).forwarding.disposition==SingleRepeaterDisposition::duplicate);
    no_retry(h,debit,25);EXPECT(h.replay.image()==replay && h.radio.requests==0);
}
// A7: terminal shutdown revokes readiness, including for a reconstructed port.
void shutdown_and_port_reconstruction() {
    AdapterHarness h;start(h);const auto debit=h.ledger.record;
    EXPECT(h.controller->shutdown()==DF::none);
    EXPECT(h.controller->status().lifecycle.occupied_frames==0);
    // Ordinary invalidation preserves receipt custody, but never readiness.
    EXPECT(h.retained.invalidated);
    P1ProSx1262RadioPort reconstructed(h.backend,h.retained);RepeaterDispatchRadioProof proof{};
    EXPECT(!reconstructed.observe(proof));
    EXPECT(h.controller->terminal(h.radio.issue_terminal(),411)==DF::wrong_state);
    EXPECT(h.controller->rearm(h.radio.issue_rearm(),412)==DF::wrong_state);
    EXPECT(h.controller->recover(h.prepare_recover(),413)==DF::wrong_state);
    no_retry(h,debit,414);EXPECT(h.radio.requests==1);
}
// A8: no authority / invalid tuples / another submit do not reach the backend.
void admission_and_single_reservation_bounds() {
    AdapterHarness h;
    EXPECT(h.controller->service(0)==DF::wrong_state);EXPECT(h.radio.requests==0);
    RepeaterDispatchAttempt invalid{};
    EXPECT(h.port.submit(invalid,h.radio.proof,{kFrame.data(),kFrame.size()}).outcome==RepeaterDispatchSubmitOutcome::tuple_refused);
    EXPECT(h.radio.requests==0);
    start(h);const auto attempt=h.radio.last_attempt;
    EXPECT(h.port.submit(attempt,h.retained.before,{kFrame.data(),kFrame.size()}).outcome==RepeaterDispatchSubmitOutcome::tuple_refused);
    EXPECT(h.radio.requests==1 && h.retained.phase==Sx1262Phase::pending);
}
// A9: absent terminal times out; explicit failed terminal is still uncertain emission.
void terminal_failure_and_deadline() {
    {
        AdapterHarness h;start(h);const auto debit=h.ledger.record;
        EXPECT(h.controller->service(1011)==DF::terminal_timeout);
        EXPECT(h.retained.invalidated);no_retry(h,debit,1012);
    }
    {
        AdapterHarness h;start(h);const auto debit=h.ledger.record;
        EXPECT(h.controller->terminal(h.radio.issue_terminal(RepeaterDispatchTerminalOutcome::failed),411)==DF::uncertain_emission);
        EXPECT(h.controller->status().state==DS::contained);
        EXPECT(h.retained.invalidated);
        EXPECT(h.controller->rearm(h.radio.issue_rearm(),412)==DF::wrong_state);
        no_retry(h,debit,413);EXPECT(h.radio.requests==1);
    }
}
// A10: callback reentry cannot submit or mint proof; invalidation during submission is uncertain.
struct AdapterReentry {
    AdapterHarness* harness;
    bool observe_refused{false}, submit_refused{false};
};
void invalidate_during_submit(void* opaque) noexcept {
    auto& context=*static_cast<AdapterReentry*>(opaque);auto& h=*context.harness;
    RepeaterDispatchRadioProof out{};
    context.observe_refused=!h.port.observe(out);
    context.submit_refused=h.port.submit(h.radio.last_attempt,h.retained.before,
        {kFrame.data(),kFrame.size()}).outcome==RepeaterDispatchSubmitOutcome::tuple_refused;
    h.port.invalidate_quiescence();
}
void reentry_and_mid_submit_invalidation() {
    AdapterHarness h;h.activate();EXPECT(h.admit().forwarding.queued);
    AdapterReentry context{&h};h.radio.submit_hook=invalidate_during_submit;h.radio.hook_context=&context;
    EXPECT(h.controller->service(11)==DF::uncertain_emission);
    EXPECT(context.observe_refused && context.submit_refused);
    EXPECT(h.retained.invalidated && h.retained.phase==Sx1262Phase::contained);
    EXPECT(h.radio.requests==1 && h.radio.copies==0);
    const auto debit=h.ledger.record;
    EXPECT(debit.global_remaining==1 && debit.airtime_remaining_us==kCost);
    no_retry(h,debit,12);
    EXPECT(h.controller->rearm(h.radio.issue_rearm(),13)==DF::wrong_state);
}
}
int main() {
    accepted_copy_and_one_use();out_of_order_and_stale_observations();
    rejected_and_busy_no_refund();uncertain_and_partial_restart();rearm_failure_and_timeout();
    suspend_saved_queue_restart();shutdown_and_port_reconstruction();
    admission_and_single_reservation_bounds();terminal_failure_and_deadline();
    reentry_and_mid_submit_invalidation();
    if(failures){std::cerr<<failures<<" adapter assertion(s) failed\n";return EXIT_FAILURE;}
    std::cout<<"PASS: 10 P1-Pro SX1262 adapter sequence groups (A1-A10)\n";return EXIT_SUCCESS;
}
