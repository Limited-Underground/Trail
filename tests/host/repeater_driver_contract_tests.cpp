// P5 host-only event sequences. Reuse the accepted synthetic ports and real
// P1-P3 composition rather than introducing another driver or lifecycle owner.
// The included suite remains independently runnable; only the six sequences
// below execute here. Its implementation is a declared build/hash dependency.
#define main repeater_dispatch_baseline_main
#include "repeater_dispatch_controller_tests.cpp"
#undef main

namespace {
void unchanged_after_poll(Harness& h, const RepeaterDispatchLedger& debit,
                          std::uint64_t now) {
    const auto requests = h.radio.requests;
    (void)h.controller->service(now);
    EXPECT(h.radio.requests == requests);
    EXPECT(same(h.ledger.record, debit));
}

// D1: delayed completion cannot be replaced by polling, receive-ready status,
// or an out-of-order rearm. The driver's private copy survives caller mutation.
void driver_accepted_delayed_completion() {
    Harness h; h.activate();
    std::array<std::uint8_t, 255> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i ^ 0xa5);
    }
    const auto expected = bytes;
    EXPECT(h.controller->admit(h.prepare_frame(1, kSource, {bytes.data(), bytes.size()}),
        {bytes.data(), bytes.size()}, 10).forwarding.queued);
    EXPECT(h.controller->service(11) == DF::none);
    const auto debit = h.ledger.record;
    bytes.fill(0);
    EXPECT(std::equal(expected.begin(), expected.end(), h.radio.copy.begin()));
    EXPECT(h.radio.size == expected.size() && h.radio.copies == 1);
    EXPECT(h.controller->status().occupied_frames == 1);
    const auto early_rearm = h.radio.issue_rearm();
    EXPECT(h.controller->rearm(early_rearm, 12) == DF::wrong_state);
    EXPECT(h.radio.rearm_unused); // wrong-phase evidence is not consumed.
    unchanged_after_poll(h, debit, 200);
    const auto terminal = h.radio.issue_terminal();
    EXPECT(h.controller->terminal(terminal, 411) == DF::none);
    EXPECT(h.controller->status().state == DS::awaiting_rearm);
    EXPECT(h.controller->terminal(terminal, 412) == DF::wrong_state);
    // Fresh post-terminal observation only: synthetic tuple checks do not prove
    // a physical producer's temporal ordering or quiescence guarantee.
    const auto fresh_rearm = h.radio.issue_rearm();
    EXPECT(h.controller->rearm(fresh_rearm, 413) == DF::none);
    EXPECT(h.controller->rearm(fresh_rearm, 414) == DF::wrong_state);
    EXPECT(h.controller->status().state == DS::ready);
    EXPECT(h.controller->status().occupied_frames == 0);
    unchanged_after_poll(h, debit, 415);
}

// D2: definitive reject still spends airtime; a stale successful terminal cannot
// turn the rejection into permission or dispatch the saved frame again.
void driver_rejected_then_explicit_rearm() {
    Harness h; h.radio.outcome = RepeaterDispatchSubmitOutcome::rejected;
    h.activate(); EXPECT(h.admit().forwarding.queued);
    EXPECT(h.controller->service(11) != DF::uncertain_emission);
    EXPECT(h.controller->status().state == DS::awaiting_rearm);
    const auto debit = h.ledger.record;
    EXPECT(debit.airtime_remaining_us == kCost && debit.global_remaining == 1);
    EXPECT(h.radio.requests == 1 && h.radio.copies == 0);
    EXPECT(h.controller->terminal(h.radio.issue_terminal(), 411) == DF::wrong_state);
    unchanged_after_poll(h, debit, 412);
    EXPECT(h.controller->rearm(h.radio.issue_rearm(), 413) == DF::none);
    unchanged_after_poll(h, debit, 414);
    EXPECT(h.admit(1, 415).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(same(h.ledger.record, debit) && h.radio.requests == 1);
}

// D3: unknown submission plus late success remains contained through actual
// reconstruction. Fresh consent and explicit retained radio reconciliation do
// not restore spent source credit or the old replay key.
void driver_uncertain_restart_and_late_success() {
    Harness h; h.radio.outcome = RepeaterDispatchSubmitOutcome::uncertain;
    h.activate(); EXPECT(h.admit().forwarding.queued);
    EXPECT(h.controller->service(11) == DF::uncertain_emission);
    const auto debit = h.ledger.record; const auto replay = h.replay.image();
    const auto terminal = h.radio.issue_terminal(); const auto rearm = h.radio.issue_rearm();
    EXPECT(h.controller->terminal(terminal, 411) == DF::wrong_state);
    EXPECT(h.controller->rearm(rearm, 412) == DF::wrong_state);
    unchanged_after_poll(h, debit, 413);
    EXPECT(h.controller->shutdown() == DF::none);
    ++h.config.lifecycle.instance_id; h.recreate();
    EXPECT(h.controller->recover(h.prepare_recover(), 500) == DF::radio_not_ready);
    EXPECT(h.radio.requests == 1 && same(h.ledger.record, debit));
    h.radio.proof.receive_ready = true; h.radio.proof.verified_quiescent = true;
    ++h.radio.proof.readiness_generation; ++h.radio.proof.quiescence_receipt;
    EXPECT(h.controller->recover(h.prepare_recover(), 501) == DF::none);
    EXPECT(h.controller->service(502) == DF::wrong_state);
    EXPECT(h.controller->consent(h.prepare_consent(), 503) == DF::none);
    EXPECT(h.admit(1, 504).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.replay.image() == replay);
    EXPECT(!h.admit(2, 505).forwarding.queued); // same source is still spent.
    EXPECT(same(h.ledger.record, debit) && h.radio.requests == 1);
}

// D4: receive rearm has no fabricated success enum. Missing evidence times out;
// explicit failure notification and unverified quiescence contain immediately.
void driver_failed_or_uncertain_receive_rearm() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        Harness h; start(h);
        EXPECT(h.controller->terminal(h.radio.issue_terminal(), 411) == DF::none);
        const auto debit = h.ledger.record;
        if (mode == 0) {
            EXPECT(h.controller->rearm(0, 412) == DF::invalid_event);
            unchanged_after_poll(h, debit, 1410);
            EXPECT(h.controller->service(1411) == DF::rearm_timeout);
        } else if (mode == 1) {
            EXPECT(h.controller->notify(h.controller->status().lifecycle.generation,
                Event::rearm_failed, 412) != DF::none);
        } else {
            const auto handle = h.radio.issue_rearm();
            h.radio.rearm_value.proof.verified_quiescent = false;
            EXPECT(h.controller->rearm(handle, 412) == DF::radio_changed);
        }
        // P2's volatile FIFO/pending bytes are retired. B deliberately keeps
        // one charged attempt reservation until trusted reconciliation; that
        // status is not a resurrected queue entry or permission to retransmit.
        EXPECT(h.controller->status().lifecycle.occupied_frames == 0);
        EXPECT(!h.controller->status().lifecycle.pending);
        EXPECT(h.controller->status().occupied_frames == 1);
        EXPECT(!h.radio.proof.receive_ready && !h.radio.proof.verified_quiescent);
        const auto late = h.radio.issue_rearm();
        EXPECT(h.controller->rearm(late, 1412) == DF::wrong_state);
        unchanged_after_poll(h, debit, 1413);
        EXPECT(h.radio.requests == 1 && h.radio.copies == 1);
    }
}

// D5: suspend before submit retires the entire sole FIFO, preserves both saved
// replay keys and admission debits, and reconstructs without saved-but-unsent TX.
void driver_suspend_saved_queue_and_restart() {
    Harness h; h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued);
    EXPECT(h.admit(2, 11, kSecond).forwarding.queued);
    EXPECT(h.controller->status().occupied_frames == 2);
    const auto debit = h.ledger.record; const auto replay = h.replay.image();
    const auto generation = h.controller->status().lifecycle.generation;
    EXPECT(h.controller->notify(generation, Event::suspend, 12) != DF::none);
    EXPECT(h.controller->status().occupied_frames == 0);
    unchanged_after_poll(h, debit, 13);
    EXPECT(h.radio.requests == 0 && h.replay.image() == replay);
    ++h.config.lifecycle.instance_id; h.recreate();
    EXPECT(h.controller->recover(h.prepare_recover(), 20) == DF::none);
    EXPECT(h.controller->service(21) == DF::wrong_state);
    EXPECT(h.controller->consent(h.prepare_consent(), 22) == DF::none);
    EXPECT(h.admit(1, 23).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(2, 24, kSecond).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    unchanged_after_poll(h, debit, 25);
    EXPECT(h.radio.requests == 0 && h.controller->status().occupied_frames == 0);
}

// D6: old attempt observations cannot finish a later attempt. Shutdown remains
// terminal even when all late driver observations appear successful.
void driver_old_attempt_and_terminal_shutdown() {
    Harness h; start(h);
    const auto old_attempt = h.radio.last_attempt;
    h.complete();
    EXPECT(h.admit(2, 413, kSecond).forwarding.queued);
    EXPECT(h.controller->service(414) == DF::none);
    const auto debit = h.ledger.record;
    const auto stale = h.radio.issue_terminal(); h.radio.terminal_value.attempt = old_attempt;
    EXPECT(h.controller->terminal(stale, 814) == DF::invalid_event);
    EXPECT(h.controller->status().state == DS::awaiting_terminal);
    EXPECT(h.controller->shutdown() == DF::none);
    EXPECT(h.controller->status().lifecycle.occupied_frames == 0);
    EXPECT(!h.controller->status().lifecycle.pending);
    EXPECT(h.controller->status().occupied_frames == 1); // retained uncertain attempt, not a FIFO
    EXPECT(!h.radio.proof.receive_ready && !h.radio.proof.verified_quiescent);
    EXPECT(h.controller->terminal(h.radio.issue_terminal(), 815) == DF::wrong_state);
    EXPECT(h.controller->rearm(h.radio.issue_rearm(), 816) == DF::wrong_state);
    EXPECT(h.controller->recover(h.prepare_recover(), 817) == DF::wrong_state);
    EXPECT(h.controller->consent(h.prepare_consent(), 818) == DF::wrong_state);
    unchanged_after_poll(h, debit, 819);
    EXPECT(h.radio.requests == 2 && h.radio.copies == 2);
    EXPECT(h.controller->status().state == DS::shutdown);
    EXPECT(debit.global_remaining == 0 && debit.airtime_remaining_us == 0);
}
} // namespace

int main() {
    driver_accepted_delayed_completion();
    driver_rejected_then_explicit_rearm();
    driver_uncertain_restart_and_late_success();
    driver_failed_or_uncertain_receive_rearm();
    driver_suspend_saved_queue_and_restart();
    driver_old_attempt_and_terminal_shutdown();
    if (failures != 0) {
        std::cerr << failures << " driver contract assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 P5 host driver contract sequence groups (D1-D6)\n";
    return EXIT_SUCCESS;
}
