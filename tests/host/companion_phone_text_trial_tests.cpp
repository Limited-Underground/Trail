#include "opentrail/companion_phone_text_trial.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace cp = opentrail::companion;
namespace tc = cp::text_carrier;
using Trial = cp::PhoneTextTrial;
using View = opentrail::protocol::v1::View;
using Bytes = std::vector<std::uint8_t>;
using Link = cp::DeviceNameContext;

void check(bool ok, const char* test) {
    if (!ok) { std::cerr << "FAIL: " << test << '\n'; std::exit(1); }
}

struct TrialTestPort final : Trial::Port {
    Link link{1, 1, 2, 3, 31, 31, 42};
    Trial::Pair pair{};
    Trial::Progress progress{};
    Trial::Inbound inbound{};
    Bytes queued;
    unsigned writes{};
    std::uint64_t admission_clock{};

    TrialTestPort(std::uint8_t local, std::uint8_t remote) {
        pair.local.fill(local);
        pair.remote.fill(remote);
        pair.group = 11;
        pair.epoch = 12;
        pair.lease = 13;
        pair.device_generation = 14;
        pair.revision = 15;
    }
    bool owner_current(const Link& caller) override { return caller == link; }
    bool observe(std::uint64_t now, Trial::Pair& value) override {
        if (now >= 60000) return false;
        value = pair;
        value.remaining_ms = static_cast<std::uint32_t>(60000 - now);
        return true;
    }
    Trial::Submission submit(const Link& caller, std::uint64_t lease, View text,
                             std::uint64_t now, std::uint64_t deadline) override {
        if (caller != link || lease != pair.lease || std::max(now, admission_clock) >= deadline ||
            now >= 60000 || writes) return {};
        ++writes;
        queued.assign(text.data, text.data + text.size);
        return {Trial::Identity{19, 23}, false};
    }
    Trial::Progress outgoing(std::uint64_t lease) override {
        return lease == pair.lease ? progress : Trial::Progress{};
    }
    Trial::Inbound incoming(std::uint64_t lease) override {
        return lease == pair.lease ? inbound : Trial::Inbound{};
    }
};

void put(Bytes& b, unsigned offset, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) b[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

template<class Board>
Bytes query(const Board& port) {
    Bytes b(76);
    std::copy_n("OTC0", 4, b.begin());
    b[5] = 6; b[6] = 9; b[17] = 1; b[18] = 56;
    put(b, 8, port.link.session_nonce, 4);
    put(b, 12, 3, 4);
    std::copy_n("OTY0", 4, b.begin() + 20);
    b[24] = b[25] = 1;
    std::copy(port.pair.remote.begin(), port.pair.remote.end(), b.begin() + 28);
    put(b, 60, port.pair.group, 8);
    put(b, 68, port.pair.epoch, 4);
    put(b, 72, port.link.session_nonce, 4);
    return b;
}

template<class Board>
Bytes request(const Board& port, const Bytes& text, std::uint32_t exchange = 5) {
    Bytes b(tc::max_record_bytes);
    const tc::Header header{{port.link.session_nonce, 3, port.pair.device_generation}, exchange};
    const auto n = tc::encode(tc::Send{header, port.pair.epoch, 9, {text.data(), text.size()}},
                              {b.data(), b.size()}, true);
    check(n != 0, "encode send fixture");
    b.resize(n);
    return b;
}

std::size_t command(Trial& t, const Link& from, const Bytes& input, Bytes& out, std::uint64_t now) {
    return t.command(from, {input.data(), input.size()}, {out.data(), out.size()}, now);
}

template<class T>
T decode(const Bytes& out, std::size_t n) {
    const auto value = tc::decode({out.data(), n}, true);
    check(value && std::holds_alternative<T>(*value), "exact D-035 output kind");
    return std::get<T>(*value);
}

#ifdef TASK6_REAL_F5
// Reuse the same retained D-112 board fixtures as F5 and F7. Only the
// handset and physical radio callbacks are simulated in this composition.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main task6_f5_fixture_main
#include "f5_rekey_status_tests.cpp"
#undef main
#pragma GCC diagnostic pop

struct RetainedPort final : Trial::Port {
    f5_test::Side& side;
    Link link{1, 1, 2, 3, 31, 31, 42};
    Trial::Pair pair{};
    f5_test::Status::TextArm arm{};
    bool tx_done{};

    explicit RetainedPort(f5_test::Side& side) : side(side) {
        const auto& binding = side.rekey->data_tx().context;
        const auto allocation = side.rekey->active_generation();
        const auto lease = side.rekey->lease_id();
        pair = {binding.sender, binding.recipient, binding.group, lease, allocation, 1,
                binding.epoch, 60000};
        auto& lanes = side.packets[allocation];
        side.lease_generation = allocation;
        side.status.emplace(side.rekey->data_tx(), side.rekey->ack_tx(), side.rekey->data_rx(),
                            side.rekey->ack_rx(), lanes[0], lanes[1], lanes[2], lanes[3],
                            side.radio, &arm);
        check(side.status->start(side.now, side.now + 60000), "open real F5 text arm");
    }

    bool owner_current(const Link& caller) override { return caller == link; }
    bool observe(std::uint64_t now, Trial::Pair& value) override {
        if (!side.status || side.status->closed() || now < side.now ||
            !side.rekey->packet_eligible(pair.lease, pair.device_generation)) return false;
        value = pair;
        return true;
    }
    Trial::Submission submit(const Link& caller, std::uint64_t lease, View text,
                             std::uint64_t now, std::uint64_t deadline) override {
        if (caller != link || lease != pair.lease || now >= deadline ||
            !side.status->send_text(17, {text.data, text.size}, now)) return {};
        const auto id = side.status->sent_identity();
        if (!id) return {};
        return {Trial::Identity{id->message_id, id->counter}, false};
    }
    Trial::Progress outgoing(std::uint64_t lease) override {
        if (lease != pair.lease) return {};
        switch (side.status->state()) {
            case f5_test::Status::State::acknowledged: return {tx_done, Trial::Terminal::delivered};
            case f5_test::Status::State::failed: return {tx_done, Trial::Terminal::failed};
            default: return {tx_done, Trial::Terminal::none};
        }
    }
    Trial::Inbound incoming(std::uint64_t lease) override {
        if (lease != pair.lease) return {};
        const auto id = side.status->received_identity();
        if (!id) return {};
        const auto text = side.status->received_text();
        return {{id->message_id, id->counter}, {text.data, text.size}};
    }
};

void exact_f5_round_trip() {
    f5_test::Pair boards;
    f5_test::finish(boards.a, boards.b, f5_test::start(boards.a), 2);
    RetainedPort a(boards.a), b(boards.b);
    Trial sender(a), receiver(b);
    Bytes out(tc::max_record_bytes);
    check(command(sender, a.link, query(a), out, a.side.now) == 136 && out[26] == 3, "real sender proof");
    check(command(receiver, b.link, query(b), out, b.side.now) == 136 && out[26] == 3, "real receiver proof");

    const Bytes text{0x4d, 0x65, 0x73, 0x68, 0x20, 0xe7, 0x95, 0x8c};
    const auto accepted = decode<tc::Result>(out, command(sender, a.link, request(a, text), out, a.side.now));
    check(accepted.disposition == 1 && accepted.message_id == 17 && accepted.counter != 0,
          "phone acceptance comes from actual F5 queue");
    a.side.status->service(a.side.now);
    check(a.side.radio.sent.size() == 1, "one real encrypted DATA");
    a.side.status->on_data_transmitted();
    a.tx_done = true;
    const auto data = a.side.radio.sent.back();
    check(b.side.status->receive({data.data(), data.size()}, b.side.now), "actual peer accepts DATA");
    const auto received = decode<tc::Received>(out, receiver.next_event(b.link, {out.data(), out.size()}, b.side.now));
    check(received.message_id == accepted.message_id && received.counter == accepted.counter &&
          received.text.size == text.size() && std::equal(text.begin(), text.end(), received.text.data),
          "phone receives exact authenticated text and identity");
    b.side.status->service(b.side.now);
    const auto ack = b.side.radio.sent.back();
    check(a.side.status->receive({ack.data(), ack.size()}, a.side.now), "actual matching peer ACK");
    const auto sent = decode<tc::Delivery>(out, sender.next_event(a.link, {out.data(), out.size()}, a.side.now));
    const auto delivered = decode<tc::Delivery>(out, sender.next_event(a.link, {out.data(), out.size()}, a.side.now));
    check(sent.phase == 2 && delivered.phase == 3 && delivered.counter == accepted.counter,
          "typed status projection uses actual F5 evidence");
    check(b.side.status->receive({data.data(), data.size()}, b.side.now) &&
          receiver.next_event(b.link, {out.data(), out.size()}, b.side.now) == 0,
          "real RF duplicate can be ACKed without another handset delivery");
}
#endif

int main() {
    TrialTestPort a(0xa1, 0xb2), b(0xb2, 0xa1);
    Trial first(a), second(b);
    Bytes out(tc::max_record_bytes);
    auto current_other_owner = a.link;
    ++current_other_owner.controller;
    const auto qa = query(a);
    check(command(first, current_other_owner, qa, out, 8) == 0, "wrong owner gets no proof");
    check(command(first, a.link, qa, out, 9) == 136 && out[26] == 3, "current owner gets OTY0 proof");
    auto generation_at = [&](unsigned offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{out[offset + i]} << (i * 8);
        return value;
    };
    check(a.pair.lease != a.pair.device_generation &&
          generation_at(104) == a.pair.device_generation &&
          generation_at(112) == a.pair.device_generation,
          "restored board presents one durable generation across both OTY0 fields");
    check(command(first, a.link, qa, out, 10) == 0, "one proof per F5 lease");
    check(command(second, b.link, query(b), out, 10) == 136 && out[26] == 3, "recipient owner proof");
    const Bytes text(90, 't');
    const auto send = request(a, text);
    check(send.size() == 148, "maximum text fits one carrier record");
    check(command(first, current_other_owner, send, out, 11) == 0 && !a.writes, "wrong owner no send");
    auto oversize = send;
    oversize.push_back('x');
    check(command(first, a.link, oversize, out, 12) == 0 && !a.writes, "oversize refuses before effects");
    const auto accepted = decode<tc::Result>(out, command(first, a.link, send, out, 13));
    check(accepted.disposition == 1 && accepted.message_id == 19 && accepted.counter == 23 &&
          a.queued == text, "acceptance carries exact queued DATA identity");
    check(command(first, a.link, send, out, 14) == 0 && a.writes == 1, "duplicate no second queue admission");
    check(first.next_event(a.link, {out.data(), out.size()}, 15) == 0, "queue is not a sent event");
    a.progress = {true, Trial::Terminal::delivered};
    const auto sent = decode<tc::Delivery>(out, first.next_event(a.link, {out.data(), out.size()}, 17));
    const auto ack = decode<tc::Delivery>(out, first.next_event(a.link, {out.data(), out.size()}, 18));
    check(sent.phase == 2 && sent.header.exchange == 1 && ack.phase == 3 && ack.header.exchange == 2 &&
          ack.receipt == accepted.receipt && ack.origin_exchange == accepted.header.exchange,
          "local Tx then matching authenticated ACK");
    b.inbound = {{19, 23}, {a.queued.data(), a.queued.size()}};
    const auto rx = decode<tc::Received>(out, second.next_event(b.link, {out.data(), out.size()}, 19));
    check(rx.message_id == 19 && rx.counter == 23 && rx.text.size == text.size() &&
          std::equal(text.begin(), text.end(), rx.text.data), "exact peer phone text");
    check(second.next_event(b.link, {out.data(), out.size()}, 20) == 0, "duplicate RF never repeats phone event");
    second.retire(b.link);
    check(second.next_event(b.link, {out.data(), out.size()}, 21) == 0, "retired phone link gets no event");

    TrialTestPort late(0xb2, 0xa1);
    Trial expiring(late);
    check(command(expiring, late.link, query(late), out, 55000) == 136, "short approved remaining arm");
    check(command(expiring, late.link, request(late, text), out, 60000) == 0 && !late.writes,
          "expired arm never queues data");
    TrialTestPort missing(0xa1, 0xb2);
    Trial no_ack(missing);
    check(command(no_ack, missing.link, query(missing), out, 100) == 136, "no-ACK case has proof");
    check(decode<tc::Result>(out, command(no_ack, missing.link, request(missing, text), out, 102)).disposition == 1,
          "unreachable peer still permits bounded local queue");
    missing.progress = {true, Trial::Terminal::failed};
    check(decode<tc::Delivery>(out, no_ack.next_event(missing.link, {out.data(), out.size()}, 9000)).phase == 2,
          "sent was real");
    check(decode<tc::Delivery>(out, no_ack.next_event(missing.link, {out.data(), out.size()}, 9001)).phase == 5,
          "no matching ACK means failed");
#ifdef TASK6_REAL_F5
    exact_f5_round_trip();
#endif
    TrialTestPort crossing(0xa1, 0xb2);
    Trial edge(crossing);
    check(command(edge, crossing.link, query(crossing), out, 100) == 136, "30-second proof starts");
    crossing.admission_clock = 30100;
    const auto denied = decode<tc::Result>(out, command(edge, crossing.link, request(crossing, text), out, 30099));
    check(denied.disposition == 2 && crossing.writes == 0,
          "local clock at admission refuses expired proof even inside RF arm");
    std::cout << "PASS TASK-6 bridge owner, exactly-once, expiry and no-ACK cases\n";
}
