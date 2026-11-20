#include "opentrail/hopping_tx.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace opentrail::radio;

namespace {

constexpr auto kWindow = std::uint64_t{25'600'000};
constexpr auto kCharge = std::uint32_t{100'000};
const std::array<std::uint8_t, 237> payload{};

class Journal final : public HoppingJournal {
public:
    JournalRead read(HoppingJournalRecord& destination) override {
        destination = record;
        return result;
    }

    bool replace(const HoppingJournalRecord& source) override {
        ++writes;
        if (trace != nullptr) trace->emplace_back("journal");
        if (fail_before) return false;
        record = source;
        result = JournalRead::present;
        return !fail_after;
    }

    HoppingJournalRecord record{};
    JournalRead result{JournalRead::empty};
    bool fail_before{false};
    bool fail_after{false};
    unsigned writes{0};
    std::vector<std::string>* trace{nullptr};
};

class Radio final : public HoppingRadioPort {
public:
    HopClock clock() override { return {now, continuous}; }
    bool confirmed_quiet() override { return quiet; }
    bool retune(std::uint32_t frequency_hz) override {
        trace.emplace_back("retune");
        tuned = frequency_hz;
        return tune_ok;
    }
    bool settle() override { trace.emplace_back("settle"); return settle_ok; }
    bool drain_receive() override { trace.emplace_back("drain RX"); return drain_ok; }
    bool cad_clear(std::uint32_t frequency_hz) override {
        trace.emplace_back("CAD");
        assert(frequency_hz == tuned);
        return cad_ok;
    }
    bool final_check(const HoppingTxRequest&, std::uint32_t frequency_hz) override {
        trace.emplace_back("final check");
        assert(frequency_hz == tuned);
        now += final_delay;
        return authorized;
    }
    HopLaunch launch(const HoppingTxRequest& request, std::uint32_t frequency_hz,
                     std::uint64_t latest_end_us) override {
        trace.emplace_back("launch");
        assert(frequency_hz == tuned);
        assert(now < latest_end_us);
        if (outcome != HopLaunch::certain_no_rf) emitted.push_back(tuned);
        last_charge = request.charge_us;
        quiet = outcome == HopLaunch::certain_no_rf;
        return outcome;
    }
    bool tx_done() override {
        trace.emplace_back("TxDone");
        return quiet && done;
    }

    bool continuous{true};
    bool quiet{true};
    bool done{true};
    bool tune_ok{true};
    bool settle_ok{true};
    bool drain_ok{true};
    bool cad_ok{true};
    bool authorized{true};
    HopLaunch outcome{HopLaunch::started};
    std::uint64_t now{0};
    std::uint64_t final_delay{0};
    std::uint32_t tuned{0};
    std::uint32_t last_charge{0};
    std::vector<std::uint32_t> emitted;
    std::vector<std::string> trace;
};

struct Fixture {
    explicit Fixture(std::uint8_t cursor = 0) : tx(journal, radio) {
        assert(provision_hopping_cursor(journal, cursor));
        journal.trace = &radio.trace;
        assert(tx.recover());
        radio.now = kWindow;
    }

    HoppingTxRequest request(std::size_t size = 1, std::uint32_t charge = kCharge) const {
        return {{payload.data(), size}, radio.now + 200, charge};
    }

    void finish() {
        radio.now += radio.last_charge;
        radio.quiet = true;
        assert(tx.service() == HopTxResult::completed);
    }

    Journal journal;
    Radio radio;
    HoppingTx tx;
};

void rolling_boundaries() {
    HoppingDwellLedger ledger;
    ledger.reset_after_silence(0);
    assert(ledger.reserve(7, {kWindow - 1, true}, kWindow, 1) == HopTxResult::cooldown);
    assert(ledger.reserve(7, {kWindow, true}, kWindow + 1, 250'000) == HopTxResult::reserved);
    assert(ledger.reserve(9, {kWindow, true}, kWindow, 10) == HopTxResult::busy);
    ledger.retain();
    assert(ledger.reserve(7, {kWindow, true}, kWindow, 140'001) == HopTxResult::dwell_exhausted);
    assert(ledger.reserve(7, {kWindow, true}, kWindow, 140'000) == HopTxResult::reserved);
    ledger.retain();
    assert(ledger.reserve(7, {kWindow, true}, kWindow, 1) == HopTxResult::dwell_exhausted);
    assert(ledger.reserve(8, {kWindow, true}, kWindow, 390'000) == HopTxResult::reserved);
    ledger.cancel();
    assert(ledger.reserve(8, {kWindow, true}, kWindow, 390'000) == HopTxResult::reserved);
    ledger.cancel();
    const auto boundary = 2 * kWindow + 250'001;
    assert(ledger.reserve(7, {boundary - 1, true}, boundary - 1, 250'001) == HopTxResult::dwell_exhausted);
    assert(ledger.reserve(7, {boundary, true}, boundary, 390'000) == HopTxResult::reserved);
}

void clock_faults() {
    HoppingDwellLedger ledger;
    ledger.reset_after_silence(0);
    assert(ledger.reserve(0, {kWindow, true}, kWindow, 390'000) == HopTxResult::reserved);
    ledger.retain();
    assert(ledger.reserve(0, {kWindow - 1, true}, kWindow, 1) == HopTxResult::clock_fault);
    assert(ledger.reserve(0, {2 * kWindow, true}, 2 * kWindow, 1) == HopTxResult::clock_fault);
    ledger.reset_after_silence(0);
    assert(ledger.reserve(1, {3 * kWindow, false}, 3 * kWindow, 1) == HopTxResult::clock_fault);
    ledger.reset_after_silence(0);
    const auto max = std::numeric_limits<std::uint64_t>::max();
    assert(ledger.reserve(1, {max - 40, true}, max - 10, 11) == HopTxResult::clock_fault);
    assert(ledger.reserve(1, {0, true}, 50, 1) == HopTxResult::clock_fault);
}

void ordering_and_rotation() {
    Fixture f(61);
    std::array<bool, 64> seen{};
    const std::vector<std::string> tx_path{"retune", "settle", "drain RX", "CAD",
                                           "journal", "final check", "launch"};
    for (unsigned visit = 0; visit != seen.size(); ++visit) {
        f.radio.trace.clear();
        assert(f.tx.try_launch(f.request()) == HopTxResult::started);
        assert(f.radio.trace == tx_path);
        const auto index = kHybridHopPermutation[(61 + visit) % seen.size()];
        assert(!seen[index]);
        seen[index] = true;
        assert(f.radio.emitted.back() == 902'300'000u + index * 200'000u);
        assert(f.tx.try_launch(f.request()) == HopTxResult::busy);
        f.finish();
        assert(f.radio.trace.back() == "TxDone");
    }
    assert(f.tx.next_position() == 61);
    assert(f.radio.emitted.size() == 64);
}

void certainty_and_refusal() {
    Fixture f;
    assert(f.tx.try_launch(f.request(237, 380'000)) == HopTxResult::invalid_request);
    assert(f.tx.try_launch(f.request(0)) == HopTxResult::invalid_request);
    assert(f.tx.try_launch(f.request(236, 100)) == HopTxResult::invalid_request);
    assert(f.radio.trace.empty());
    f.radio.tune_ok = false;
    assert(f.tx.try_launch(f.request()) == HopTxResult::radio_refused);
    assert(f.radio.trace == std::vector<std::string>{"retune"});
    f.radio.tune_ok = true;
    f.radio.cad_ok = false;
    const auto initial_writes = f.journal.writes;
    assert(f.tx.try_launch(f.request()) == HopTxResult::cad_busy);
    assert(f.journal.writes == initial_writes && f.tx.next_position() == 0);
    f.radio.cad_ok = true;
    f.radio.authorized = false;
    assert(f.tx.try_launch(f.request(236, 380'000)) == HopTxResult::no_rf);
    assert(f.tx.next_position() == 0);
    f.radio.authorized = true;
    f.radio.final_delay = 201;
    assert(f.tx.try_launch(f.request(236, 380'000)) == HopTxResult::no_rf);
    assert(f.tx.next_position() == 0 && f.radio.emitted.empty());
    f.radio.final_delay = 0;
    f.radio.outcome = HopLaunch::certain_no_rf;
    assert(f.tx.try_launch(f.request(236, 380'000)) == HopTxResult::no_rf);
    assert(f.tx.next_position() == 0);
    f.radio.outcome = HopLaunch::started;
    assert(f.tx.try_launch(f.request(236, 380'000)) == HopTxResult::started);
    f.finish();
    assert(f.tx.next_position() == 1);
}

void torn_write_and_cooldown() {
    for (const bool after : {false, true}) {
        Fixture f(63);
        f.journal.fail_before = !after;
        f.journal.fail_after = after;
        assert(f.tx.try_launch(f.request()) == HopTxResult::journal_fault);
        assert(f.radio.emitted.empty());
        assert(f.tx.try_launch(f.request()) == HopTxResult::not_ready);
        f.journal.fail_before = f.journal.fail_after = false;
        Radio reset_radio;
        HoppingTx recovered(f.journal, reset_radio);
        assert(recovered.recover());
        assert(recovered.next_position() == (after ? 0 : 63));
        reset_radio.now = kWindow - 1;
        assert(recovered.try_launch({{payload.data(), 1}, kWindow + 500, kCharge}) == HopTxResult::cooldown);
        assert(reset_radio.emitted.empty());
        reset_radio.now = kWindow;
        assert(recovered.try_launch({{payload.data(), 1}, kWindow + 500, kCharge}) == HopTxResult::started);
    }
    Fixture corrupt;
    corrupt.journal.record[7] ^= 1;
    HoppingTx withdrawn(corrupt.journal, corrupt.radio);
    assert(!withdrawn.recover());
    assert(!provision_hopping_cursor(corrupt.journal, 0));
}

void uncertain_timeout_and_no_refund() {
    Fixture f;
    f.radio.outcome = HopLaunch::uncertain;
    assert(f.tx.try_launch(f.request(236, 390'000)) == HopTxResult::uncertain);
    assert(f.tx.next_position() == 1);
    assert(f.tx.try_launch(f.request()) == HopTxResult::not_ready);
    assert(!f.tx.recover());
    f.radio.quiet = true;
    assert(f.tx.recover());
    assert(f.tx.next_position() == 1);
    assert(f.tx.try_launch(f.request()) == HopTxResult::cooldown);
    f.radio.now += kWindow;
    f.radio.outcome = HopLaunch::started;
    assert(f.tx.try_launch(f.request()) == HopTxResult::started);
    f.radio.now += 200 + kCharge;
    assert(f.tx.service() == HopTxResult::timed_out);
    assert(f.tx.next_position() == 2);
    assert(f.tx.try_launch(f.request()) == HopTxResult::not_ready);
    f.radio.quiet = true;
    assert(f.tx.recover());
    assert(f.tx.next_position() == 2);
    assert(f.tx.try_launch(f.request()) == HopTxResult::cooldown);
}

void prior_charges_survive_full_rotation() {
    Fixture f;
    assert(f.tx.try_launch(f.request(236, 390'000)) == HopTxResult::started);
    f.finish();
    for (unsigned i = 1; i != 64; ++i) {
        assert(f.tx.try_launch(f.request()) == HopTxResult::started);
        f.finish();
    }
    assert(f.tx.try_launch(f.request()) == HopTxResult::dwell_exhausted);
    assert(f.tx.next_position() == 0);
    // End includes the allowed delay to SetTx, not just the time of reserve.
    f.radio.now = 2 * kWindow + 390'200;
    assert(f.tx.try_launch(f.request()) == HopTxResult::started);
}

}  // namespace

int main() {
    rolling_boundaries();
    clock_faults();
    ordering_and_rotation();
    certainty_and_refusal();
    torn_write_and_cooldown();
    uncertain_timeout_and_no_refund();
    prior_charges_survive_full_rotation();
    std::cout << "7 hopping TX groups passed\n";
}
