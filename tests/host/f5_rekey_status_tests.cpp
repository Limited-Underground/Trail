// Both endpoints start with real D-112 peer-confirmed first-pairing records.
#define OPENTRAIL_PAIRING_ADAPTER_FIXTURE_ONLY
#include "pairing_session_adapter_tests.cpp"
#include "opentrail/f5_pairing_trust.hpp"
#include "opentrail/f5_rekey_session.hpp"
#include "opentrail/f5_status_session.hpp"
#include <memory>

namespace f5_test {
using Board = pairing_adapter_test::Board;
using Rekey = opentrail::security_evaluation::F5RekeySession;
using Status = opentrail::companion::F5StatusSession;
using Frame = Rekey::Frame;
using Packet = std::vector<std::uint8_t>;
using PairingCompletionJournal = opentrail::security_evaluation::PairingCompletionJournal;
constexpr std::uint32_t pairing_code = 123456;
const char* active_case = "fixture setup";
#undef CHECK
#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << active_case << " failed at " << __FILE__ << ':' << __LINE__ \
                  << " " #expression "\n"; \
        std::exit(1); \
    } \
} while (false)

struct Radio final : radio::RadioTransport {
    std::vector<Packet> sent;
    radio::RadioState state{radio::RadioState::idle};
    std::size_t mtu() const override { return 255; }
    radio::TransportStatus status() const override {
        radio::TransportStatus value{};
        value.state = state;
        value.mtu_bytes = 255;
        return value;
    }
    radio::SendResult send(radio::ByteView value, std::uint64_t) override {
        sent.emplace_back(value.data, value.data + value.size);
        return {radio::RadioError::none, value.size};
    }
    radio::ReceiveResult receive(radio::MutableByteView) override { return {}; }
    void service(std::uint64_t) override {}
};

InvitationKey local_identity(Board& board) {
    EnrollmentIdentityStore identity(board.identity, board.random);
    CHECK(identity.load_existing() == EnrollmentIdentityStore::LoadResult::ready);
    InvitationKey pinned{};
    CHECK(identity.public_key(pinned));
    return pinned;
}

struct Side {
    Board& board;
    CallbackStorage state, tail;
    std::array<std::array<CallbackStorage, 4>, 33> packets;
    Radio radio;
    std::optional<F5PairingTrustView> trust_view;
    std::optional<Rekey> rekey;
    std::optional<Status> status;
    std::uint64_t now{100000};
    unsigned seed;
    bool trust_available{true};
    explicit Side(Board& retained, unsigned entropy_seed) : board(retained), seed(entropy_seed) {}

    static std::uint64_t clock(void* context) { return static_cast<Side*>(context)->now; }
    static bool trust_guard(void* context) {
        const auto& side = *static_cast<Side*>(context);
        return side.trust_available && side.trust_view->available();
    }
    static bool trust(void* context, InvitationKey& scope) {
        auto& side = *static_cast<Side*>(context);
        return side.trust_available && side.trust_view->read(scope);
    }
    static bool seed_allowed(void* context) {
        const auto& side = *static_cast<Side*>(context);
        for (const auto& allocation : side.packets) {
            for (const auto& lane : allocation) {
                for (const auto& slot : pairing_adapter_test::persistent_bytes(lane)) {
                    if (!std::all_of(slot.begin(), slot.end(), [](auto byte) { return byte == 0xff; })) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    void refill_entropy() {
        board.refill(seed);
        seed += 17;
    }
    bool open() {
        refill_entropy();
        trust_view.emplace(board.journal, board.backend.at(0, EvaluationNamespace::enrollment),
                           board.binding, board.completion, local_identity(board));
        rekey.emplace(Rekey::Stores{board.identity, board.journal,
            board.backend.at(0, EvaluationNamespace::membership),
            board.backend.at(0, EvaluationNamespace::enrollment), board.binding, state, tail},
            board.random, pairing_code, &clock, this, Rekey::Trust{&trust, this, &seed_allowed, &trust_guard});
        return rekey->initialize();
    }
    void power_cut() {
        // A power cut preserves the bytes at the cut. Host RAII destruction
        // otherwise writes retirement markers that actual lost power cannot.
        if (status) {
            const auto generation = lease_generation;
            auto& lanes = packets.at(generation);
            const auto tx = lanes[0].inner.memory;
            const auto ack_tx = lanes[1].inner.memory;
            const auto rx = lanes[2].inner.memory;
            const auto ack_rx = lanes[3].inner.memory;
            status.reset();
            lanes[0].inner.memory = tx;
            lanes[1].inner.memory = ack_tx;
            lanes[2].inner.memory = rx;
            lanes[3].inner.memory = ack_rx;
        }
        rekey.reset();
        trust_view.reset();
        radio.sent.clear();
        now = 100;
    }
    void reboot() {
        power_cut();
        CHECK(open());
        CHECK(!rekey->ready());
    }
    void quiet_old_status() {
        status.reset();
        rekey->release_retired_leases();
    }
    void open_status() {
        CHECK(!status && rekey->ready());
        lease_generation = rekey->active_generation();
        auto& lanes = packets.at(lease_generation);
        status.emplace(rekey->data_tx(), rekey->ack_tx(), rekey->data_rx(), rekey->ack_rx(),
                       lanes[0], lanes[1], lanes[2], lanes[3], radio);
        CHECK(status->start(now, now+60000));
    }
    std::uint64_t lease_generation{};
};

struct RetainedPair {
    Board a;
    Board b;
    explicit RetainedPair(unsigned seed_a = 1, unsigned seed_b = 81)
        : a(InvitationRole::initiator, seed_a, 100), b(InvitationRole::responder, seed_b, 50100) {
        const auto result = pairing_adapter_test::drive(a, b);
        CHECK(result.ready_a && result.ready_b);
        CHECK(result.completion_a.phase == PairingCompletionPhase::peer_confirmed);
        CHECK(result.completion_b.phase == PairingCompletionPhase::peer_confirmed);
        CHECK(a.session->close() && b.session->close());
    }
};

struct Pair {
    std::unique_ptr<RetainedPair> owned;
    Board& retained_a;
    Board& retained_b;
    Side a;
    Side b;
    explicit Pair(bool initialize = true, RetainedPair* retained = nullptr)
        : owned(retained ? nullptr : std::make_unique<RetainedPair>()),
          retained_a((retained ? *retained : *owned).a), retained_b((retained ? *retained : *owned).b),
          a(retained_a, 151), b(retained_b, 211) {
        if (initialize) CHECK(a.open() && b.open());
    }
};

Frame start(Side& origin) {
    Frame frame{};
    CHECK(origin.rekey->start(frame));
    CHECK(frame.size == 236 && frame.bytes[5] == 1);
    return frame;
}

Frame receive(Side& destination, const Frame& input, bool duplicate = false) {
    Frame output{};
    if (!destination.rekey->receive(input, output)) {
        std::cerr << "unexpected control rejection kind=" << static_cast<unsigned>(input.bytes[5])
                  << " U=" << destination.rekey->used_epoch()
                  << " C=" << destination.rekey->committed_epoch()
                  << " A=" << destination.rekey->generation()
                  << " clock=" << destination.now << '\n';
        CHECK(false);
    }
    if (duplicate) {
        Frame repeated{};
        CHECK(destination.rekey->receive(input, repeated));
        CHECK(repeated.size == output.size && repeated.bytes == output.bytes);
    }
    return output;
}

void refuse(Side& destination, const Frame& input) {
    Frame output{};
    CHECK(!destination.rekey->receive(input, output));
    CHECK(output.size == 0);
}

void ready_at(Side& initiator, Side& responder, std::uint32_t epoch) {
    CHECK(initiator.rekey->ready() && responder.rekey->ready());
    CHECK(initiator.rekey->used_epoch() == epoch && responder.rekey->used_epoch() == epoch);
    CHECK(initiator.rekey->committed_epoch() == epoch && responder.rekey->committed_epoch() == epoch);
    CHECK(initiator.rekey->data_tx().context.epoch == epoch);
    CHECK(responder.rekey->data_tx().context.epoch == epoch);
    CHECK(initiator.rekey->data_tx().key == responder.rekey->data_rx().key);
    CHECK(initiator.rekey->ack_tx().key == responder.rekey->ack_rx().key);
    CHECK(initiator.rekey->data_tx().key != initiator.rekey->ack_tx().key);
    CHECK(initiator.rekey->data_tx().key != responder.rekey->data_tx().key);
}

void finish(Side& initiator, Side& responder, Frame frame,
            std::uint32_t epoch, bool duplicate = false) {
    responder.now += 1000; // Stable receive-rate budget between freshly signed attempts.
    for (unsigned kind = 1; kind <= 7; ++kind) {
        CHECK(frame.size && frame.bytes[5] == kind);
        frame = receive(kind % 2 ? responder : initiator, frame, duplicate);
    }
    CHECK(frame.size == 0);
    ready_at(initiator, responder, epoch);
}

Frame advance(Side& initiator, Side& responder, unsigned count) {
    auto frame = start(initiator);
    for (unsigned index = 0; index < count; ++index) {
        frame = receive(index % 2 ? initiator : responder, frame);
    }
    return frame;
}

void expire_attempt(Side& side) {
    side.now += 60000;
    Frame output{};
    (void)side.rekey->service(output);
    CHECK(output.size == 0);
}

Packet send(Side& from, std::uint32_t id, bool complete = true) {
    CHECK(from.status->send_status(id, from.now));
    CHECK(from.status->state() == Status::State::queued);
    const auto before = from.radio.sent.size();
    from.status->service(from.now);
    CHECK(from.radio.sent.size() == before+1);
    CHECK(from.status->state() == Status::State::queued);
    if (complete) {
        from.status->on_data_transmitted();
        CHECK(from.status->state() == Status::State::transmitted);
    }
    return from.radio.sent.back();
}

Packet accept_data(Side& to, const Packet& data) {
    const auto count = to.radio.sent.size();
    CHECK(to.status->receive({data.data(), data.size()}, to.now));
    CHECK(to.status->received_count() == 1);
    CHECK(to.status->received_status() == protocol::QuickStatusKind::ok);
    to.status->service(to.now);
    CHECK(to.radio.sent.size() == count+1);
    return to.radio.sent.back();
}

void accept_ack(Side& from, const Packet& ack) {
    CHECK(from.status->receive({ack.data(), ack.size()}, from.now));
    CHECK(from.status->state() == Status::State::acknowledged);
}

void exchange_statuses(Side& a, Side& b, std::uint32_t first) {
    a.open_status();
    b.open_status();
    accept_ack(a, accept_data(b, send(a, first)));
    accept_ack(b, accept_data(a, send(b, first+1)));
}

void one_board_restarts(bool original_responder_restarts, bool ack_lost) {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    pair.a.open_status();
    pair.b.open_status();
    auto& restarting = original_responder_restarts ? pair.b : pair.a;
    auto& running = original_responder_restarts ? pair.a : pair.b;
    auto* const original_owner = &*running.rekey;
    auto* const original_status = &*running.status;
    const auto old_generation = running.rekey->active_generation();
    const auto old_domain = running.rekey->data_tx().domain;
    const auto data = send(restarting, 101, ack_lost);
    Packet ack;
    if (ack_lost) ack = accept_data(running, data);
    CHECK(restarting.status->state() != Status::State::acknowledged);
    restarting.reboot();
    CHECK(!restarting.status && restarting.radio.sent.empty());
    CHECK(restarting.rekey->used_epoch() == 2 && restarting.rekey->committed_epoch() == 2);
    CHECK(running.rekey->ready() && &*running.rekey == original_owner);
    const auto offer = start(restarting);
    running.now += 1000;
    auto reply = receive(running, offer, true);
    CHECK(running.rekey->ready() && &*running.status == original_status);
    CHECK(running.rekey->data_tx().domain == old_domain);
    // Old Packet authority survives an ordinary START and its exact duplicate.
    // Queue a real protected status before the live peer proves its challenge.
    CHECK(send(running, 103, false).size() != 0);
    for (unsigned kind = 2; kind <= 7; ++kind) {
        CHECK(reply.size && reply.bytes[5] == kind);
        reply = receive(kind % 2 ? running : restarting, reply);
    }
    CHECK(reply.size == 0);
    ready_at(restarting, running, 3);
    CHECK(&*running.rekey == original_owner);
    CHECK(running.rekey->active_generation() > old_generation);
    CHECK(running.rekey->data_tx().domain != old_domain);
    const auto queued = running.radio.sent.size();
    running.status->service(running.now+4000);
    CHECK(running.radio.sent.size() == queued);
    CHECK(running.status->state() == Status::State::failed);
    running.quiet_old_status();
    CHECK(running.rekey->ready()); // Old status cannot retire the successor.
    restarting.open_status();
    running.open_status();
    CHECK(!running.status->receive({data.data(), data.size()}, running.now));
    CHECK(running.status->received_count() == 0);
    const auto fresh = send(restarting, 111);
    if (ack_lost) {
        CHECK(!restarting.status->receive({ack.data(), ack.size()}, restarting.now));
        CHECK(restarting.status->state() == Status::State::transmitted);
    }
    accept_ack(restarting, accept_data(running, fresh));
    accept_ack(running, accept_data(restarting, send(running, 112)));
}

void first_seed_cut() {
    Pair pair(false);
    pair.a.state.arm(Fault::write_after);
    CHECK(!pair.a.open());
    CHECK(!pair.a.rekey->ready());
    pair.a.state.inner.clear();
    pair.a.reboot();
    CHECK(pair.a.rekey->used_epoch() == 1 && pair.a.rekey->committed_epoch() == 0);
    CHECK(pair.b.open());
    finish(pair.a, pair.b, start(pair.a), 2);
}

void seed_denies_old_packet_footprint() {
    Pair pair(false);
    pair.a.packets[1][0].inner.memory.corrupt_byte(domain, 0, 0, 1);
    const auto before = pairing_adapter_test::persistent_bytes(pair.a.packets[1][0]);
    CHECK(!pair.a.open());
    CHECK(!pair.a.rekey->ready());
    CHECK(pairing_adapter_test::persistent_bytes(pair.a.packets[1][0]) == before);
}

void allocation_before_start_not_reused() {
    Pair pair;
    const auto offer_never_launched = start(pair.a);
    const auto used = pair.a.rekey->used_epoch();
    const auto reserved = pair.a.rekey->generation();
    CHECK(reserved != 0);
    pair.a.reboot();
    CHECK(pair.a.rekey->generation() == reserved && pair.a.rekey->used_epoch() == used);
    const auto fresh_offer = start(pair.a);
    CHECK(pair.a.rekey->generation() == reserved+1);
    CHECK(fresh_offer.bytes != offer_never_launched.bytes);
    finish(pair.a, pair.b, fresh_offer, 2);
}

void lost_start_uses_no_epoch() {
    Pair pair;
    const auto lost = start(pair.b);
    expire_attempt(pair.b);
    CHECK(pair.b.rekey->used_epoch() == 1 && pair.a.rekey->used_epoch() == 1);
    auto fresh = start(pair.b);
    CHECK(fresh.bytes != lost.bytes);
    finish(pair.b, pair.a, fresh, 2);
}

void catch_up_cut_before_reply() {
    Pair pair;
    const auto confirm_lost = advance(pair.a, pair.b, 2);
    CHECK(confirm_lost.bytes[5] == 3);
    CHECK(pair.a.rekey->used_epoch() == 2 && pair.b.rekey->used_epoch() == 1);
    pair.a.reboot();
    // Existing responder attempt must no longer block a higher authenticated START.
    const auto catch_up = start(pair.a);
    pair.b.tail.arm(Fault::write_before);
    refuse(pair.b, catch_up);
    pair.b.tail.inner.clear();
    pair.b.reboot();
    CHECK(pair.b.rekey->used_epoch() == 1);
    finish(pair.a, pair.b, catch_up, 3);
}

void catch_up_reply_lost() {
    Pair pair;
    (void)advance(pair.a, pair.b, 2); // I saved PREPARED, R did not see CONFIRM.
    pair.a.reboot();
    const auto catch_up = start(pair.a);
    pair.b.now += 1000;
    auto reply = receive(pair.b, catch_up, true);
    CHECK(reply.bytes[5] == 2);
    CHECK(pair.b.rekey->used_epoch() == 2 && pair.b.rekey->committed_epoch() == 0);
    pair.b.reboot(); // Durable intermediate burn survives loss of the reply.
    CHECK(pair.b.rekey->used_epoch() == 2);
    finish(pair.a, pair.b, catch_up, 3);
}

void ordinary_reply_before_binding() {
    Pair pair;
    const auto reply = advance(pair.a, pair.b, 1);
    CHECK(reply.bytes[5] == 2);
    CHECK(pair.a.rekey->used_epoch() == 1 && pair.b.rekey->used_epoch() == 1);
    pair.b.reboot();
    pair.a.reboot();
    finish(pair.b, pair.a, start(pair.b), 2);
}

void initiator_prepared_torn() {
    Pair pair;
    const auto reply = advance(pair.a, pair.b, 1);
    pair.a.state.arm(Fault::write_after);
    refuse(pair.a, reply);
    pair.a.state.inner.clear();
    pair.a.reboot();
    CHECK(pair.a.rekey->used_epoch() == 1);
    pair.b.reboot();
    finish(pair.a, pair.b, start(pair.a), 2);
}

void lost_confirm_restarts_initiator() {
    Pair pair;
    (void)advance(pair.a, pair.b, 2);
    CHECK(pair.a.rekey->used_epoch() == 2 && pair.a.rekey->committed_epoch() == 0);
    CHECK(pair.b.rekey->used_epoch() == 1);
    pair.a.reboot();
    CHECK(pair.a.rekey->used_epoch() == 2);
    finish(pair.a, pair.b, start(pair.a), 3);
}

void stale_lower_peer_schedules_nudge() {
    Pair pair;
    (void)advance(pair.a, pair.b, 2);
    CHECK(pair.a.rekey->used_epoch() == 2 && pair.b.rekey->used_epoch() == 1);
    pair.b.reboot();
    const auto stale_start = start(pair.b);
    Frame withheld{};
    (void)pair.a.rekey->receive(stale_start, withheld);
    CHECK(withheld.size == 0 && pair.a.rekey->used_epoch() == 2);
    Frame nudge{};
    CHECK(pair.a.rekey->service(nudge));
    CHECK(nudge.size == 236 && nudge.bytes[5] == 1);
    finish(pair.a, pair.b, nudge, 3);
}

void responder_prepared_or_activation_torn(bool cut_after_prepared) {
    Pair pair;
    const auto confirm = advance(pair.a, pair.b, 2);
    pair.b.state.arm(Fault::erase_after, cut_after_prepared ? 6 : 1);
    refuse(pair.b, confirm);
    pair.b.state.inner.clear();
    pair.b.reboot();
    CHECK(pair.b.rekey->used_epoch() == (cut_after_prepared ? 2u : 1u));
    pair.a.reboot();
    finish(pair.a, pair.b, start(pair.a), 3);
}

void before_or_after_activation_and_commit(unsigned delivered, unsigned epoch_before_i,
                                          unsigned epoch_before_r, bool restart_initiator) {
    Pair pair;
    const auto next = advance(pair.a, pair.b, delivered);
    CHECK(next.bytes[5] == delivered+1);
    CHECK(pair.a.rekey->used_epoch() == 2 && pair.b.rekey->used_epoch() == 2);
    CHECK(pair.a.rekey->committed_epoch() == epoch_before_i);
    CHECK(pair.b.rekey->committed_epoch() == epoch_before_r);
    CHECK(pair.a.rekey->ready() == (delivered >= 6));
    CHECK(!pair.b.rekey->ready());
    auto& restarting = restart_initiator ? pair.a : pair.b;
    auto& running = restart_initiator ? pair.b : pair.a;
    restarting.reboot();
    CHECK(restarting.rekey->used_epoch() == 2);
    expire_attempt(running);
    finish(restarting, running, start(restarting), 3);
}

void phase_write_torn(unsigned delivered, bool fault_initiator,
                      unsigned epoch_before_i, unsigned epoch_before_r) {
    Pair pair;
    const auto incoming = advance(pair.a, pair.b, delivered);
    auto& failing = fault_initiator ? pair.a : pair.b;
    auto& peer = fault_initiator ? pair.b : pair.a;
    failing.tail.arm(Fault::write_after);
    refuse(failing, incoming);
    failing.tail.inner.clear();
    failing.reboot();
    CHECK(pair.a.rekey->committed_epoch() == epoch_before_i);
    CHECK(pair.b.rekey->committed_epoch() == epoch_before_r);
    expire_attempt(peer);
    finish(failing, peer, start(failing), 3);
}

void later_catch_up_overtakes_live_older_activation() {
    Pair pair;
    const auto stale_activation = advance(pair.a, pair.b, 4); // Both activation intents at 2.
    CHECK(stale_activation.bytes[5] == 5);
    pair.a.reboot();
    const auto epoch_three = start(pair.a);
    pair.b.now += 1000;
    const auto epoch_three_reply = receive(pair.b, epoch_three);
    (void)receive(pair.a, epoch_three_reply); // I burns 3, R misses CONFIRM.
    CHECK(pair.a.rekey->used_epoch() == 3 && pair.b.rekey->used_epoch() == 2);
    pair.a.reboot();
    const auto epoch_four = start(pair.a);
    pair.b.now += 1000;
    auto reply = receive(pair.b, epoch_four);
    CHECK(pair.b.rekey->used_epoch() == 3);
    refuse(pair.b, stale_activation);
    CHECK(!pair.b.rekey->ready());
    for (unsigned kind = 2; kind <= 7; ++kind) {
        CHECK(reply.bytes[5] == kind);
        reply = receive(kind % 2 ? pair.b : pair.a, reply);
    }
    ready_at(pair.a, pair.b, 4);
}

void crossed_starts_have_one_winner() {
    Pair pair;
    auto a = start(pair.a);
    auto b = start(pair.b);
    Frame left{}, right{};
    const bool accepted_left = pair.a.rekey->receive(b, left);
    const bool accepted_right = pair.b.rekey->receive(a, right);
    CHECK(accepted_left != accepted_right);
    CHECK((left.size == 236) == accepted_left && (right.size == 236) == accepted_right);
    auto& initiator = accepted_right ? pair.a : pair.b;
    auto& responder = accepted_right ? pair.b : pair.a;
    auto frame = accepted_right ? right : left;
    for (unsigned kind = 2; kind <= 7; ++kind) {
        CHECK(frame.bytes[5] == kind);
        frame = receive(kind % 2 ? responder : initiator, frame);
    }
    ready_at(initiator, responder, 2);
}

void expiry_does_not_close_old_lease_or_extend_attempt() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    pair.a.open_status();
    pair.b.open_status();
    const auto first = start(pair.a);
    pair.a.now += 30000;
    auto reply = receive(pair.b, first, true);
    CHECK(reply.bytes[5] == 2 && pair.a.rekey->ready());
    pair.a.now += 30000;
    refuse(pair.a, reply);
    CHECK(pair.a.rekey->ready());
    // Packet policy owns its distinct local expiry. An RX or START does not renew it.
    pair.a.status->service(pair.a.now);
    CHECK(pair.a.status->state() == Status::State::failed);
    CHECK(pair.b.rekey->ready());
}

void region_or_trust_loss_has_no_pending_traffic() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    pair.a.open_status();
    pair.b.open_status();
    pair.a.radio.state = radio::RadioState::offline;
    CHECK(!pair.a.status->send_status(9, pair.a.now));
    CHECK(pair.a.radio.sent.empty());
    pair.b.trust_available = false;
    CHECK(!pair.b.rekey->ready());
    Frame frame{};
    CHECK(!pair.b.rekey->start(frame) && frame.size == 0);
    CHECK(!pair.b.status->send_status(9, pair.b.now));
    CHECK(pair.b.radio.sent.empty());
}

void allocation_exhaustion_with_unconsumed_epoch(bool inbound_at_capacity) {
    Pair pair;
    for (std::uint64_t expected = 1; expected < 32; ++expected) {
        pair.a.refill_entropy();
        (void)start(pair.a); // No peer receives these attempts.
        CHECK(pair.a.rekey->generation() == expected);
        CHECK(pair.a.rekey->used_epoch() == 1);
        expire_attempt(pair.a);
    }
    pair.a.refill_entropy();
    finish(pair.a, pair.b, start(pair.a), 2);
    CHECK(pair.a.rekey->active_generation() == 32);
    const auto head = pairing_adapter_test::persistent_bytes(pair.a.state);
    const auto tail = pairing_adapter_test::persistent_bytes(pair.a.tail);
    const auto check_capacity = [&] {
        if (inbound_at_capacity) {
            refuse(pair.a, start(pair.b));
        } else {
            Frame next{};
            CHECK(!pair.a.rekey->start(next) && next.size == 0);
        }
        CHECK(pair.a.rekey->generation() == 32 && pair.a.rekey->used_epoch() == 2);
        CHECK(pair.a.rekey->committed_epoch() == 2);
        CHECK(pairing_adapter_test::persistent_bytes(pair.a.state) == head);
        CHECK(pairing_adapter_test::persistent_bytes(pair.a.tail) == tail);
    };
    check_capacity();
    pair.a.reboot();
    check_capacity();
}

void completed_corruption_refuses_without_fallback() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    auto& side = pair.a;
    side.power_cut();
    // Both marker-complete banks contain records. Damage a finalized payload,
    // not the marker, so falling back to the previous vote would be unsafe.
    side.state.inner.memory.corrupt_byte(Domain::configuration, 0, 8, 1);
    const auto original = pairing_adapter_test::persistent_bytes(side.state);
    CHECK(!side.open() && !side.rekey->ready());
    CHECK(pairing_adapter_test::persistent_bytes(side.state) == original);
}

void unreadable_bank_refuses_without_writing() {
    Pair pair;
    pair.a.power_cut();
    const auto before = pairing_adapter_test::persistent_bytes(pair.a.state);
    pair.a.tail.arm(Fault::read_error);
    CHECK(!pair.a.open() && !pair.a.rekey->ready());
    CHECK(pairing_adapter_test::persistent_bytes(pair.a.state) == before);
}

using Journal = opentrail::security_evaluation::F5RekeyJournal;

Journal::Record seed_record() {
    Journal::Record seed{};
    seed.used = 1;
    seed.proof_size = 64;
    std::fill_n(seed.proof.begin(), seed.proof_size, 0x27);
    return seed;
}

bool validate_base(void* context, const Journal::Record& value) {
    const auto& expected = *static_cast<const Journal::Record*>(context);
    return value.kind == Journal::ProofKind::base && value.proof == expected.proof;
}

struct Media {
    CallbackStorage parts[2];
    Journal::Record seed{seed_record()};
    Journal::Record current{};
    std::optional<Journal> writer;
    bool open() {
        writer.emplace(parts[0], parts[1]);
        return writer->initialize(seed, &validate_base, &seed);
    }
    explicit Media(unsigned previous_updates) {
        CHECK(open() && writer->seed());
        current = seed;
        for (unsigned count = 0; count < previous_updates; ++count) {
            ++current.allocation;
            CHECK(writer->update(current));
        }
        clear_trace();
    }
    void clear_trace() {
        for (auto& part : parts) part.inner.clear();
    }
    Journal::Record next() const {
        auto result = current;
        ++result.allocation;
        return result;
    }
};

unsigned journal_io_cut_grid() {
    unsigned cuts = 0;
    // After two and three updates respectively, either target bank holds a
    // formerly committed record. The new write must invalidate its marker
    // first. Sweep every exposed read, erase, write and sync in each half.
    for (unsigned prior : {2u, 3u}) {
        Media trace(prior);
        CHECK(trace.writer->update(trace.next()));
        for (unsigned half = 0; half < 2; ++half) {
            for (const auto fault : invitation_lifecycle_test::faults) {
                const auto operation = invitation_lifecycle_test::operation(fault);
                const auto maximum = trace.parts[half].inner.count(operation);
                CHECK(maximum > 0);
                for (unsigned nth = 1; nth <= maximum; ++nth) {
                    Media trial(prior);
                    trial.parts[half].arm(fault, nth);
                    CHECK(!trial.writer->update(trial.next()));
                    trial.writer.reset();
                    trial.clear_trace();
                    CHECK(trial.open());
                    Journal::Record observed{};
                    CHECK(trial.writer->load(observed));
                    CHECK(Journal::equal(observed, trial.current) || Journal::equal(observed, trial.next()));
                    CHECK(observed.used == 1 && observed.committed == 0);
                    ++cuts;
                }
            }
        }
    }
    return cuts;
}

struct MarkerCut final : persistence::PersistentStorage {
    CallbackStorage media;
    std::size_t prefix{};
    bool cutting_installation{};
    bool fired{};
    persistence::StorageReadResult read_slot(Domain d, std::size_t s,
        persistence::MutableStorageByteView out) override { return media.read_slot(d, s, out); }
    Error erase_slot(Domain d, std::size_t s) override { return media.erase_slot(d, s); }
    Error write_slot(Domain d, std::size_t s, std::size_t o,
                     persistence::StorageByteView in) override {
        if (prefix && !fired && o == 60 && in.size == 4 &&
            static_cast<bool>(in.data[0]) == cutting_installation) {
            CHECK(media.write_slot(d, s, o, {in.data, prefix}) == Error::none);
            fired = true;
            return Error::io_failure;
        }
        return media.write_slot(d, s, o, in);
    }
    Error sync_slot(Domain d, std::size_t s) override { return media.sync_slot(d, s); }
};

void marker_prefix_grid() {
    for (bool install : {false, true}) {
        for (std::size_t prefix = 1; prefix <= 3; ++prefix) {
            CallbackStorage first;
            MarkerCut second;
            auto seed = seed_record(), current = seed;
            {
                Journal writer(first, second);
                CHECK(writer.initialize(seed, &validate_base, &seed) && writer.seed());
                current.allocation = 1;
                CHECK(writer.update(current));
                second.prefix = prefix;
                second.cutting_installation = install;
                auto next = current;
                ++next.allocation;
                CHECK(!writer.update(next) && second.fired);
            }
            Journal resumed(first, second);
            CHECK(resumed.initialize(seed, &validate_base, &seed));
            Journal::Record observed{};
            CHECK(resumed.load(observed) && Journal::equal(observed, current));
        }
    }
}

void golden_bank_and_wrong_sequence_refusal() {
    Media media(1);
    const auto encoded = Journal::encode(media.current, 2);
    CHECK(encoded.size() == 640);
    CHECK(std::memcmp(encoded.data(), "OTFJ", 4) == 0 && encoded[4] == 2);
    CHECK(encoded[15] == 2 && encoded[23] == 1 && encoded[27] == 1 && encoded[33] == 64);
    CHECK(encoded[40] == 0x27 && encoded[103] == 0x27 && encoded[104] == 0);
    CHECK(std::memcmp(encoded.data()+636, "F5J2", 4) == 0);
    Journal::Record read{};
    std::uint64_t sequence{};
    CHECK(Journal::decode(encoded, read, sequence) && sequence == 2 && Journal::equal(read, media.current));
    auto newer = media.current;
    ++newer.allocation;
    const auto wrong_parity = Journal::encode(newer, 4); // Even sequence in bank 0.
    for (std::size_t part = 0; part < 2; ++part) {
        for (std::size_t d = 0; d < persistence::kStorageDomainCount; ++d) {
            std::array<std::uint8_t, 64> slot{};
            std::copy_n(wrong_parity.data()+(part*5+d)*slot.size(), slot.size(), slot.begin());
            media.parts[part].inner.memory.seed_slot(static_cast<Domain>(d), 0, slot);
        }
    }
    media.writer.reset();
    CHECK(!media.open());
}

void trust_gate_refuses_empty_and_wrong_peer() {
    Pair pair(false);
    CallbackStorage empty;
    InvitationKey scope{}, peer_scope{};
    auto& a = pair.retained_a;
    auto& b = pair.retained_b;
    auto& evidence_a = a.backend.at(0, EvaluationNamespace::enrollment);
    auto& evidence_b = b.backend.at(0, EvaluationNamespace::enrollment);
    const auto local_a = local_identity(a);
    const auto local_b = local_identity(b);
    CHECK(read_f5_pairing_trust(a.journal, evidence_a, a.binding, a.completion, local_a, scope));
    CHECK(read_f5_pairing_trust(b.journal, evidence_b, b.binding, b.completion, local_b, peer_scope));
    CHECK(scope == peer_scope);
    CHECK(!read_f5_pairing_trust(a.journal, evidence_a, a.binding, empty, local_a, scope));
    CHECK(!read_f5_pairing_trust(a.journal, evidence_a, a.binding, b.completion, local_a, scope));
    RetainedPair unrelated(31, 111);
    auto& foreign = unrelated.b;
    auto& foreign_evidence = foreign.backend.at(0, EvaluationNamespace::enrollment);
    CHECK(!read_f5_pairing_trust(a.journal, foreign_evidence, foreign.binding, a.completion, local_a, scope));
    F5PairingTrustView view(a.journal, evidence_a, a.binding, a.completion, local_a);
    CHECK(view.read(scope) && view.available());
    a.completion.inner.memory.corrupt_byte(Domain::configuration, 0, 0, 1);
    const auto corrupted = pairing_adapter_test::persistent_bytes(a.completion);
    const auto verified_scope = scope;
    CHECK(!view.read(scope) && !view.available() && scope == verified_scope);
    CHECK(pairing_adapter_test::persistent_bytes(a.completion) == corrupted);
}

void changed_retained_provenance_fails_closed() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    pair.retained_a.binding.arm(Fault::read_error);
    CHECK(!pair.a.rekey->ready());
    CHECK(!pair.a.rekey->data_tx().active);
}

void actual_provenance_change_during_storage_write_withholds_control() {
    Pair pair;
    const auto offer = start(pair.a);
    bool changed = false;
    pair.b.tail.callback = [&](char operation) {
        if (operation == 'w' && !changed) {
            changed = true;
            pair.retained_b.completion.inner.memory.corrupt_byte(Domain::configuration, 0, 0, 1);
        }
    };
    refuse(pair.b, offer);
    CHECK(changed && !pair.b.rekey->ready());
}

void queued_control_loses_exact_launch_permission() {
    {
        Pair pair;
        const auto offer = start(pair.a);
        CHECK(pair.a.rekey->frame_eligible(offer));
        const auto reply = receive(pair.b, offer);
        const auto confirmation = receive(pair.a, reply);
        CHECK(!pair.a.rekey->frame_eligible(offer));
        CHECK(pair.a.rekey->frame_eligible(confirmation));
        pair.retained_a.binding.arm(Fault::read_error);
        CHECK(!pair.a.rekey->frame_eligible(confirmation));
        CHECK(pair.a.rekey->failed());
    }
    {
        Pair pair;
        const auto offer = start(pair.b);
        CHECK(pair.b.rekey->frame_eligible(offer));
        pair.b.now = pair.b.rekey->attempt_deadline();
        CHECK(!pair.b.rekey->frame_eligible(offer));
    }
    {
        Pair pair;
        const auto offer = start(pair.a);
        CHECK(pair.a.rekey->frame_eligible(offer));
        pair.a.tail.arm(Fault::read_error);
        CHECK(!pair.a.rekey->frame_eligible(offer));
        CHECK(pair.a.rekey->failed());
    }
}

void reentry_contains_authority() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    bool entered = false;
    pair.a.state.callback = [&](char) {
        if (!entered) {
            entered = true;
            CHECK(!pair.a.rekey->ready());
        }
    };
    CHECK(!pair.a.rekey->ready());
    CHECK(entered && !pair.a.rekey->data_tx().active);
}

void duplicates_tampering_and_acks() {
    Pair pair;
    auto initial = start(pair.a);
    auto tampered = initial;
    tampered.bytes[172] ^= 1;
    const auto before = pairing_adapter_test::persistent_bytes(pair.b.state);
    refuse(pair.b, tampered);
    CHECK(pairing_adapter_test::persistent_bytes(pair.b.state) == before);
    finish(pair.a, pair.b, initial, 2, true);
    pair.a.open_status();
    pair.b.open_status();
    const auto data = send(pair.a, 17);
    auto invalid_data = data;
    invalid_data.back() ^= 1;
    const auto replay_state = pairing_adapter_test::persistent_bytes(pair.b.packets.at(pair.b.lease_generation)[2]);
    CHECK(!pair.b.status->receive({invalid_data.data(), invalid_data.size()}, pair.b.now));
    CHECK(pair.b.status->received_count() == 0);
    CHECK(pairing_adapter_test::persistent_bytes(pair.b.packets.at(pair.b.lease_generation)[2]) == replay_state);
    auto ack = accept_data(pair.b, data);
    accept_ack(pair.a, ack);
    CHECK(pair.b.status->receive({data.data(), data.size()}, pair.b.now));
    pair.b.status->service(pair.b.now);
    CHECK(pair.b.status->received_count() == 1 && pair.b.radio.sent.back() == ack);
    accept_ack(pair.b, accept_data(pair.a, send(pair.b, 19)));
}

void retries_use_identical_packet_bytes() {
    Pair pair;
    finish(pair.a, pair.b, start(pair.a), 2);
    pair.a.open_status();
    const auto data = send(pair.a, 1);
    pair.a.now += 4000;
    pair.a.status->service(pair.a.now);
    CHECK(pair.a.radio.sent.size() == 2 && pair.a.radio.sent.back() == data);
    CHECK(pair.a.status->state() == Status::State::transmitted);
    pair.a.now += 30000;
    pair.a.status->service(pair.a.now);
    CHECK(pair.a.status->state() == Status::State::failed);
}

void abandoned_same_epoch_offer_is_replaced() {
    Pair pair;
    const auto abandoned = start(pair.a);
    pair.a.reboot();
    const auto fresh = start(pair.a);
    CHECK(fresh.bytes != abandoned.bytes);
    const auto first_reply = receive(pair.b, abandoned, true);
    const auto original_generation = pair.b.rekey->generation();
    CHECK(pair.b.rekey->used_epoch() == 1 && original_generation == 1);
    refuse(pair.b, fresh); // New answers remain rate limited.
    CHECK(pair.b.rekey->generation() == original_generation);
    pair.b.now += 1000;
    auto frame = receive(pair.b, fresh, true);
    CHECK(frame.bytes != first_reply.bytes && frame.bytes[5] == 2);
    CHECK(pair.b.rekey->generation() == original_generation+1);
    CHECK(pair.b.rekey->used_epoch() == 1);
    pair.b.now += 1000;
    refuse(pair.b, abandoned); // Even outside the rate window, lower peer A cannot evict.
    CHECK(pair.b.rekey->generation() == original_generation+1);
    for (unsigned kind = 2; kind <= 7; ++kind) {
        CHECK(frame.bytes[5] == kind);
        frame = receive(kind % 2 ? pair.b : pair.a, frame);
    }
    ready_at(pair.a, pair.b, 2);
}

using Memory = persistence::test_support::MemoryPersistentStorage;
using JournalImage = std::array<Memory, 2>;

JournalImage snapshot(const Side& side) { return {side.state.inner.memory, side.tail.inner.memory}; }

std::vector<std::pair<std::uint64_t, Journal::Record>> complete_banks(const JournalImage& media) {
    std::vector<std::pair<std::uint64_t, Journal::Record>> records;
    for (std::size_t bank = 0; bank < 2; ++bank) {
        Journal::Bank bytes{};
        for (std::size_t part = 0; part < 2; ++part) {
            for (std::size_t d = 0; d < 5; ++d) {
                const auto& slot = media[part].slot_bytes(static_cast<Domain>(d), bank);
                std::copy(slot.begin(), slot.end(), bytes.begin() + (part*5+d)*slot.size());
            }
        }
        std::uint64_t sequence{};
        Journal::Record record{};
        if (Journal::decode(bytes, record, sequence)) records.emplace_back(sequence, record);
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return records;
}

struct PhaseWrite {
    JournalImage prior;
    Journal::Record baseline, current, next;
};

bool validate_captured(void* context, const Journal::Record& record) {
    const auto& allowed = *static_cast<const std::vector<Journal::Record>*>(context);
    return std::any_of(allowed.begin(), allowed.end(), [&](const auto& exact) { return Journal::equal(exact, record); });
}

std::vector<PhaseWrite> capture_real_phase_writes() {
    std::vector<PhaseWrite> writes;
    RetainedPair retained;
    // Use a real authenticated handshake for every control on both roles. A
    // lost prior local START flips the target bank without raising used epoch.
    for (bool shifted : {false, true}) {
        Pair pair(true, &retained);
        const auto seed_a = complete_banks(snapshot(pair.a)).at(0).second;
        const auto seed_b = complete_banks(snapshot(pair.b)).at(0).second;
        if (shifted) {
            (void)start(pair.a);
            (void)start(pair.b);
            expire_attempt(pair.a);
            expire_attempt(pair.b);
        }
        auto control = start(pair.a);
        for (unsigned kind = 1; kind <= 7; ++kind) {
            auto& target = kind % 2 ? pair.b : pair.a;
            const auto& baseline = kind % 2 ? seed_b : seed_a;
            auto before = snapshot(target);
            auto last_complete = complete_banks(before).back();
            auto capture_commit = [&](char operation) {
                if (operation != 's') return;
                auto after = snapshot(target);
                const auto complete = complete_banks(after).back();
                if (complete.first == last_complete.first) return;
                CHECK(complete.first == last_complete.first+1);
                writes.push_back({before, baseline, last_complete.second, complete.second});
                before = after;
                last_complete = complete;
            };
            const auto count = writes.size();
            if (kind >= 2 && kind <= 6) target.tail.callback = capture_commit;
            control = receive(target, control);
            target.tail.callback = [](char) {};
            if (kind >= 2 && kind <= 6) CHECK(writes.size() == count+(kind == 3 ? 2u : 1u));
        }
        ready_at(pair.a, pair.b, 2);
    }
    return writes;
}

unsigned phase_mutation_cut_grid() {
    unsigned total = 0;
    const auto writes = capture_real_phase_writes();
    CHECK(writes.size() == 12);
    constexpr std::array<Fault, 6> mutations{Fault::erase_before, Fault::erase_after,
        Fault::write_before, Fault::write_after, Fault::partial_write, Fault::sync_after};
    // The physical journal algorithm is exercised using exact, already
    // authenticated snapshots from the real controls. Replaying a transaction
    // per cut avoids regenerating the same signatures several hundred times.
    // Integrated phase cases separately check control publication is withheld.
    for (const auto& write : writes) {
        std::vector<Journal::Record> trusted{write.baseline, write.next};
        for (const auto& [sequence, record] : complete_banks(write.prior)) {
            CHECK(sequence != 0);
            trusted.push_back(record);
        }
        CallbackStorage trace[2];
        trace[0].inner.memory = write.prior[0];
        trace[1].inner.memory = write.prior[1];
        Journal reference(trace[0], trace[1]);
        CHECK(reference.initialize(write.baseline, &validate_captured, &trusted));
        for (auto& part : trace) part.inner.clear();
        CHECK(reference.update(write.next));
        for (unsigned half = 0; half < 2; ++half) {
            for (const auto fault : mutations) {
                const auto count = trace[half].inner.count(invitation_lifecycle_test::operation(fault));
                CHECK(count > 0);
                for (unsigned nth = 1; nth <= count; ++nth) {
                    CallbackStorage cut[2];
                    cut[0].inner.memory = write.prior[0];
                    cut[1].inner.memory = write.prior[1];
                    {
                        Journal writer(cut[0], cut[1]);
                        CHECK(writer.initialize(write.baseline, &validate_captured, &trusted));
                        cut[half].arm(fault, nth);
                        CHECK(!writer.update(write.next));
                    }
                    for (auto& part : cut) part.inner.clear();
                    Journal resumed(cut[0], cut[1]);
                    if (!resumed.initialize(write.baseline, &validate_captured, &trusted)) {
                        std::cerr << "phase cut reopen U=" << write.next.used << " C=" << write.next.committed
                                  << " phase=" << static_cast<unsigned>(write.next.phase) << " half=" << half
                                  << " fault=" << static_cast<unsigned>(fault) << " nth=" << nth << '\n';
                        CHECK(false);
                    }
                    Journal::Record observed{};
                    CHECK(resumed.load(observed));
                    CHECK(Journal::equal(observed, write.current) || Journal::equal(observed, write.next));
                    CHECK(observed.allocation == write.current.allocation);
                    ++total;
                }
            }
        }
    }
    return total;
}

unsigned calls{};
std::string_view filter;
template<class Action>
void scenario(const char* name, Action action) {
    if (!filter.empty() && std::string_view(name).find(filter)==std::string_view::npos) return;
    active_case = name;
    action();
    ++calls;
    if (std::getenv("F5_TEST_TRACE")) std::cerr << "Passed " << name << '\n';
}
void run_cases() {
    scenario("Table 1: first_seed_cut", [] { first_seed_cut(); });
    scenario("Table 2: allocation_before_start_not_reused", [] { allocation_before_start_not_reused(); });
    scenario("Table 3: lost_start_uses_no_epoch", [] { lost_start_uses_no_epoch(); });
    scenario("Table 4: catch_up_cut_before_reply", [] { catch_up_cut_before_reply(); });
    scenario("Table 5: catch_up_reply_lost", [] { catch_up_reply_lost(); });
    scenario("Table 6: ordinary_reply_before_binding", [] { ordinary_reply_before_binding(); });
    scenario("Table 7: initiator_prepared_torn", [] { initiator_prepared_torn(); });
    scenario("Table 8: lost_confirm_restarts_initiator", [] { lost_confirm_restarts_initiator(); });
    scenario("Table 9: stale_lower_peer_schedules_nudge", [] { stale_lower_peer_schedules_nudge(); });
    scenario("Table 10a: responder_prepared_or_activation_torn", [] { responder_prepared_or_activation_torn(false); });
    scenario("Table 10b: responder_prepared_or_activation_torn", [] { responder_prepared_or_activation_torn(true); });
    scenario("Table 11: before_or_after_activation_and_commit", [] { before_or_after_activation_and_commit(3, 0, 0, true); });
    scenario("Table 12a: phase_write_torn", [] { phase_write_torn(3, true, 0, 0); });
    scenario("Table 12b: before_or_after_activation_and_commit", [] { before_or_after_activation_and_commit(4, 0, 0, false); });
    scenario("Table 13: phase_write_torn", [] { phase_write_torn(4, false, 0, 0); });
    scenario("Table 14: before_or_after_activation_and_commit", [] { before_or_after_activation_and_commit(5, 0, 2, true); });
    scenario("Table 15: phase_write_torn", [] { phase_write_torn(5, true, 0, 2); });
    scenario("Table 16: before_or_after_activation_and_commit", [] { before_or_after_activation_and_commit(6, 2, 2, false); });
    scenario("Table 17a: one_board_restarts", [] { one_board_restarts(false, false); });
    scenario("Table 17b, failing-first direction: one_board_restarts", [] { one_board_restarts(true, false); });
    scenario("Table 18: one_board_restarts", [] { one_board_restarts(true, true); });
    scenario("Table 19: later_catch_up_overtakes_live_older_activation", [] { later_catch_up_overtakes_live_older_activation(); });
    scenario("Table 20: crossed_starts_have_one_winner", [] { crossed_starts_have_one_winner(); });
    scenario("Table 21a: expiry_does_not_close_old_lease_or_extend_attempt", [] { expiry_does_not_close_old_lease_or_extend_attempt(); });
    scenario("Table 21b: region_or_trust_loss_has_no_pending_traffic", [] { region_or_trust_loss_has_no_pending_traffic(); });
    scenario("Table 22a / M3 initiator: allocation_exhaustion_with_unconsumed_epoch", [] { allocation_exhaustion_with_unconsumed_epoch(false); });
    scenario("Table 22b / M3 responder: allocation_exhaustion_with_unconsumed_epoch", [] { allocation_exhaustion_with_unconsumed_epoch(true); });
    scenario("Table 23a: completed_corruption_refuses_without_fallback", [] { completed_corruption_refuses_without_fallback(); });
    scenario("Table 23b: unreadable_bank_refuses_without_writing", [] { unreadable_bank_refuses_without_writing(); });
}
} // namespace f5_test

int main(int argc, char** argv) {
    using namespace f5_test;
    CHECK(argc <= 2);
    if (argc == 2) filter = argv[1];
    run_cases();
    scenario("seed_denies_old_packet_footprint", seed_denies_old_packet_footprint);
    unsigned cuts = 0, phase_cuts = 0;
    scenario("journal_io_cut_grid", [&] { cuts = journal_io_cut_grid(); });
    scenario("marker_prefix_grid", marker_prefix_grid);
    scenario("abandoned_same_epoch_offer_is_replaced M4", abandoned_same_epoch_offer_is_replaced);
    scenario("phase_mutation_cut_grid M6", [&] { phase_cuts = phase_mutation_cut_grid(); });
    scenario("golden_bank_and_wrong_sequence_refusal", golden_bank_and_wrong_sequence_refusal);
    scenario("trust_gate_refuses_empty_and_wrong_peer", trust_gate_refuses_empty_and_wrong_peer);
    scenario("changed_retained_provenance_fails_closed", changed_retained_provenance_fails_closed);
    scenario("actual_provenance_change_during_storage_write_withholds_control",
             actual_provenance_change_during_storage_write_withholds_control);
    scenario("queued_control_loses_exact_launch_permission", queued_control_loses_exact_launch_permission);
    scenario("reentry_contains_authority", reentry_contains_authority);
    scenario("duplicates_tampering_and_acks", duplicates_tampering_and_acks);
    scenario("retries_use_identical_packet_bytes", retries_use_identical_packet_bytes);
    CHECK(calls > 0);
    std::cout << "PASS " << calls << " real retained F5 rekey/status groups ("
              << cuts << " journal I/O cuts, " << phase_cuts << " phase mutation cuts";
    if (filter.empty()) std::cout << ", 6 marker prefix cuts, 29 interruption cases";
    std::cout << ")\n";
}
