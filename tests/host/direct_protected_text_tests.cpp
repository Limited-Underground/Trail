// Simulated transport and ESP NVS. Both boards use real direct pairing, F5
// rekey and Packet-v1. Board A uses the native D-163 catalog and packet lanes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main f5_status_regression_fixture_main
#include "f5_rekey_status_tests.cpp"
#undef main
#pragma GCC diagnostic pop

#include "direct_nvs_storage.hpp"
#include "opentrail/direct_text_session.hpp"

namespace fake {
void reset();
std::size_t changes();
}

namespace direct_text_test {
namespace target = opentrail::target::heltec_v4_bench;
using namespace opentrail::companion;
using f5_test::active_case;
using Packet = f5_test::Packet;
using Rekey = f5_test::Rekey;
using Board = f5_test::Board;
using Side = f5_test::Side;
using Owner = DirectConversationOwner;
using Native = target::PairingNvsStorage;
using Store = Native::Store;
using Error = persistence::PersistentKvBackendError;
using Text = DirectTextSession;
using Ticket = SelectedEnrollmentRequest;
constexpr auto kPurpose = EnrollmentCandidateSession::kDirectPairingGroup;

radio::ByteView view(const Packet& packet) { return {packet.data(), packet.size()}; }

class MemoryCatalog final : public persistence::PersistentKvBackend {
public:
    Error read_blob(const char*, const char*, const char*, std::uint8_t* bytes,
                    std::size_t capacity, std::size_t& size) override {
        size = value_ ? value_->size() : 0;
        if (!value_) return Error::not_found;
        CHECK(capacity == size);
        std::copy(value_->begin(), value_->end(), bytes);
        return Error::none;
    }
    Error write_blob(const char*, const char*, const char*, const std::uint8_t* bytes,
                     std::size_t size) override {
        CHECK(!staged_ && size == Owner::kCatalogBytes);
        staged_.emplace();
        std::copy_n(bytes, size, staged_->data());
        return Error::none;
    }
    Error erase_key(const char*, const char*, const char*) override { return Error::invalid_argument; }
    Error commit(const char*, const char*) override {
        CHECK(staged_);
        value_.swap(staged_);
        staged_.reset();
        return Error::none;
    }
private:
    using Record = std::array<std::uint8_t, Owner::kCatalogBytes>;
    std::optional<Record> value_, staged_;
};

class QueuedRadio final : public radio::RadioTransport {
public:
    std::size_t mtu() const override { return 163; }
    radio::TransportStatus status() const override {
        radio::TransportStatus result{};
        result.mtu_bytes = mtu();
        result.state = radio::RadioState::idle;
        result.transmit_queue_depth = queued ? 1 : 0;
        return result;
    }
    radio::SendResult send(radio::ByteView frame, std::uint64_t) override {
        if (queued) return {radio::RadioError::queue_full, 0};
        queued.emplace(frame.data, frame.data + frame.size);
        return {radio::RadioError::none, frame.size};
    }
    radio::ReceiveResult receive(radio::MutableByteView) override { return {}; }
    void service(std::uint64_t) override {}
    Packet launch(Text& session, std::uint64_t now) {
        CHECK(queued);
        CHECK(session.frame_eligible(view(*queued), now));
        auto frame = *queued;
        queued.reset();
        return frame;
    }
    std::optional<Packet> queued;
};

class PacketClaims final : public DirectPacketPort, public DirectTextStoragePort {
public:
    PacketClaims(QueuedRadio& radio, Native* native) : radio_(radio), native_(native) {}
    bool claim(const DirectIdentity& scope, std::uint64_t generation) override {
        CHECK(!generation_);
        if (native_) {
            claim_ = native_->claim_packets(scope, generation);
            if (!claim_) return false;
            constexpr std::array<Store, 4> lanes{Store::packet_data_tx, Store::packet_ack_tx,
                                                Store::packet_data_rx, Store::packet_ack_rx};
            for (std::size_t i = 0; i < lanes.size(); ++i)
                views_[i].emplace(*native_, lanes[i], *claim_);
        }
        scope_ = scope;
        generation_ = generation;
        return true;
    }
    bool quiesce_and_retire(const DirectIdentity& scope, std::uint64_t generation) override {
        if (!generation_) return !native_ || native_->retire_packets(scope, generation);
        if (scope != scope_ || generation != generation_) return false;
        if (consumer_) consumer_->close();
        radio_.queued.reset();
        for (auto& lane : views_) lane.reset();
        claim_.reset();
        generation_ = 0;
        return !native_ || native_->retire_packets(scope, generation);
    }
    Lanes packet_lanes(std::size_t slot, const DirectIdentity& scope, std::uint64_t generation) override {
        ++borrows;
        if (!scope_slot || *scope_slot != slot || scope != scope_ || generation != generation_) return {};
        Lanes result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = native_ ? static_cast<persistence::PersistentStorage*>(&*views_[i]) : &memory_[i];
        return result;
    }
    void bind(Text& session) { consumer_ = &session; }
    std::optional<std::size_t> scope_slot;
    unsigned borrows{};
private:
    QueuedRadio& radio_;
    Native* native_;
    Text* consumer_{};
    DirectIdentity scope_{};
    std::uint64_t generation_{};
    std::optional<Native::PacketClaim> claim_;
    std::array<std::optional<Native::PacketView>, 4> views_{};
    std::array<CallbackStorage, 4> memory_{};
};

class AuthenticatedPairing final : public DirectPairingPort {
public:
    AuthenticatedPairing(Side& side, target::DirectNvsStorage* native_scope)
        : side_(side), native_scope_(native_scope) {}
    bool begin(const Ticket& ticket) override {
        return side_.board.request.pending() && exact(ticket);
    }
    std::optional<DirectBinding> reviewed(const Ticket& ticket) override {
        if (!exact(ticket)) return std::nullopt;
        const auto proof = side_.board.session->direct_reviewed();
        if (!proof) return std::nullopt;
        return DirectBinding{proof->peer, proof->scope};
    }
    bool commit_local_vote(const Ticket& ticket, const DirectBinding& binding) override {
        return exact(ticket) && side_.board.session->commit_direct_vote({binding.peer, binding.scope});
    }
    bool peer_committed(const Ticket& ticket, const DirectBinding& binding) override {
        return exact(ticket) && side_.board.session->direct_peer_committed({binding.peer, binding.scope});
    }
    void cancel(const Ticket&) override { (void)side_.board.session->cancel(); }
    bool retire_scope(std::size_t slot, const DirectBinding&) override {
        if (side_.rekey) side_.rekey->retire_traffic();
        return !native_scope_ || native_scope_->clear_scope(slot);
    }
    std::uint64_t ready_session(const DirectBinding& binding) const override {
        if (!side_.rekey || !side_.rekey->ready_for_scope(binding.peer, binding.scope, kPurpose)) return 0;
        return side_.rekey->active_generation();
    }
    void open_rekey(std::size_t slot) {
        auto& board = side_.board;
        side_.refill_entropy();
        auto& evidence = board.backend.at(0, EvaluationNamespace::enrollment);
        side_.trust_view.emplace(board.journal, evidence, board.binding, board.completion,
                                 f5_test::local_identity(board));
        auto* native = native_scope_ ? native_scope_->scope(slot) : nullptr;
        persistence::PersistentStorage& state = native ? native->external(Store::f5_state) : side_.state;
        persistence::PersistentStorage& tail = native ? native->external(Store::f5_state_tail) : side_.tail;
        side_.rekey.emplace(Rekey::Stores{board.identity, board.journal,
            board.backend.at(0, EvaluationNamespace::membership), evidence, board.binding, state, tail},
            board.random, kPurpose, &Side::clock, &side_,
            Rekey::Trust{&Side::trust, &side_, &Side::seed_allowed, &Side::trust_guard});
        CHECK(side_.rekey->initialize());
    }
private:
    bool exact(const Ticket& ticket) const {
        // The underlying fixture drives the exact local code/role/context.
        // Retain its request handle for post-handshake admission, since F5 consumes it.
        const auto& current = side_.board.authority.value;
        const auto role = side_.board.role == InvitationRole::initiator
            ? CompanionEnrollmentRole::initiator : CompanionEnrollmentRole::responder;
        return ticket.connection_handle == 8 && ticket.delivery_token == 9 &&
            ticket.exchange_id == 10 && ticket.pairing_code == f5_test::pairing_code &&
            ticket.role == role && ticket.authority == current.context;
    }
    Side& side_;
    target::DirectNvsStorage* native_scope_;
};

void begin(Owner& owner, Board& board) {
    const auto role = board.role == InvitationRole::initiator
        ? CompanionEnrollmentRole::initiator : CompanionEnrollmentRole::responder;
    CHECK(owner.load());
    CHECK(owner.begin(board.authority.value, 8, 9, 10, role, f5_test::pairing_code) == DirectResult::ok);
}

void expect_text(Text& session, const Packet& expected, std::uint32_t id, std::uint64_t now) {
    const auto received = session.take_received(now);
    CHECK(received && received->identity.message_id == id && received->identity.counter);
    CHECK(received->size == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), received->bytes.begin()));
    CHECK(!session.take_received(now));
}

Packet offer(Text& session, QueuedRadio& radio, std::uint32_t id, const Packet& text, std::uint64_t now) {
    CHECK(!radio.queued);
    CHECK(session.send_text(id, view(text), now));
    CHECK(!radio.queued && session.send_state() == Text::SendState::queued);
    session.service(now);
    CHECK(radio.queued);
    auto changed = *radio.queued;
    changed.back() ^= 1;
    CHECK(!session.frame_eligible(view(changed), now));
    return radio.launch(session, now);
}

void exercise() {
    fake::reset();
    Native native_packets;
    target::DirectNvsStorage native_catalog;
    CHECK(native_packets.ready() && native_catalog.ready());
    MemoryCatalog host_catalog;
    Board board_a(InvitationRole::initiator, 1, 100, true);
    Board board_b(InvitationRole::responder, 81, 50100, true);
    Side a(board_a, 31), b(board_b, 131);
    QueuedRadio radio_a, radio_b;
    PacketClaims lanes_a(radio_a, &native_packets), lanes_b(radio_b, nullptr);
    AuthenticatedPairing pairing_a(a, &native_catalog), pairing_b(b, nullptr);
    Owner owner_a(native_catalog, pairing_a, lanes_a), owner_b(host_catalog, pairing_b, lanes_b);
    begin(owner_a, board_a);
    begin(owner_b, board_b);
    const auto ticket_a = owner_a.request(), ticket_b = owner_b.request();
    std::optional<DirectBinding> binding_a, binding_b;

    Limits limits{};
    limits.direct_completion = true;
    const auto result = pairing_adapter_test::drive(board_a, board_b, -1, -1, f5_test::pairing_code,
        pairing_adapter_test::TailFault::none, limits, false, [&](std::uint64_t) {
            if (binding_a) return;
            const auto review_a = pairing_a.reviewed(ticket_a), review_b = pairing_b.reviewed(ticket_b);
            if (!review_a || !review_b) return;
            CHECK(review_a->scope == review_b->scope && review_a->peer != review_b->peer);
            CHECK(owner_a.accept(board_a.authority.value, ticket_a) == DirectResult::ok);
            CHECK(owner_b.accept(board_b.authority.value, ticket_b) == DirectResult::ok);
            binding_a = review_a;
            binding_b = review_b;
        });
    CHECK(result.a == Outcome::paired && result.b == Outcome::paired && binding_a && binding_b);
    lanes_a.scope_slot = owner_a.scope_slot(*binding_a);
    lanes_b.scope_slot = owner_b.scope_slot(*binding_b);
    CHECK(lanes_a.scope_slot && *lanes_a.scope_slot < target::DirectNvsStorage::kScopeCount && lanes_b.scope_slot);

    pairing_a.open_rekey(*lanes_a.scope_slot);
    pairing_b.open_rekey(*lanes_b.scope_slot);
    f5_test::finish(a, b, f5_test::start(a), 2);
    CHECK(a.rekey->ready_for_scope(binding_a->peer, binding_a->scope, kPurpose));
    CHECK(!a.rekey->ready_for_scope(binding_b->peer, binding_a->scope, kPurpose));
    CHECK(!a.rekey->ready_for_scope(binding_a->peer, binding_a->scope, f5_test::pairing_code));
    auto other_scope = *binding_a;
    ++other_scope.scope[0];
    CHECK(!a.rekey->ready_for_scope(binding_a->peer, other_scope.scope, kPurpose));

    // A valid fresh cryptographic session cannot bypass the durable bilateral owner gate.
    f5_test::active_case = "local consent alone blocks packet views and text";
    const auto before = fake::changes();
    Text premature(owner_a, *binding_a, *a.rekey, lanes_a, radio_a);
    CHECK(!premature.start(a.now, a.now + 60000));
    CHECK(owner_a.activate(*binding_a) != DirectResult::ok);
    CHECK(!owner_a.traffic_grant(*binding_a) && lanes_a.borrows == 0 && fake::changes() == before);
    CHECK(owner_a.finish(board_a.authority.value, ticket_a) == DirectResult::ok);
    CHECK(owner_b.finish(board_b.authority.value, ticket_b) == DirectResult::ok);
    CHECK(owner_a.activate(*binding_a) == DirectResult::ok && owner_b.activate(*binding_b) == DirectResult::ok);
    const auto grant = owner_a.traffic_grant(*binding_a);
    CHECK(grant && grant->slot == *lanes_a.scope_slot && grant->session == a.rekey->active_generation());
    CHECK(!owner_a.traffic_grant(other_scope));

    f5_test::active_case = "scoped Packet-v1 bilateral text, ACK and replay";
    Text text_a(owner_a, *binding_a, *a.rekey, lanes_a, radio_a),
         text_b(owner_b, *binding_b, *b.rekey, lanes_b, radio_b);
    lanes_a.bind(text_a);
    lanes_b.bind(text_b);
    CHECK(text_a.start(a.now, a.now + 60000) && text_b.start(b.now, b.now + 60000));
    const Packet long_text(90, 'a'), peer_text{'B', 0x20, 0xf0, 0x9f, 0x9a, 0xb2, 0x0a};
    auto invalid = Packet(91, 'x');
    const auto initialized = fake::changes();
    CHECK(!text_a.send_text(1, view(invalid), a.now) && fake::changes() == initialized && !radio_a.queued);
    CHECK(text_a.send_text(3, view(long_text), a.now));
    const auto before_ingress = fake::changes();
    active_case = "peer B submits and launches text";
    const auto from_b = offer(text_b, radio_b, 2, peer_text, b.now);
    invalid = from_b;
    invalid.back() ^= 1;
    CHECK(text_a.receive(view(invalid), a.now) == Text::Ingress::refused && fake::changes() == before_ingress);
    CHECK(text_a.receive(view(from_b), a.now) == Text::Ingress::delivered);
    CHECK(fake::changes() > before_ingress);
    expect_text(text_a, peer_text, 2, a.now);
    text_a.service(a.now);
    // The earlier DATA is still pending when these ACKs reach the shared queue.
    // ACK retains the exact launch identity until it leaves the transport.
    active_case = "board A launches an ACK before its queued DATA";
    const auto ack_a = radio_a.launch(text_a, a.now);
    CHECK(ack_a[3] == static_cast<std::uint8_t>(protocol::v1::Type::ack));
    CHECK(text_a.send_state() == Text::SendState::queued);
    CHECK(text_b.receive(view(ack_a), b.now) == Text::Ingress::acknowledged);
    CHECK(text_b.send_state() == Text::SendState::acknowledged);
    const auto received = fake::changes();
    CHECK(text_a.receive(view(from_b), a.now) == Text::Ingress::duplicate);
    CHECK(!text_a.take_received(a.now) && fake::changes() == received);
    text_a.service(a.now);
    active_case = "board A launches the cached duplicate ACK";
    CHECK(radio_a.launch(text_a, a.now) == ack_a);

    // An ACK-required send rejected as busy follows its regular retry interval.
    a.now += 3999;
    text_a.service(a.now);
    CHECK(!radio_a.queued);
    ++a.now;
    text_a.service(a.now);
    active_case = "board A launches the deferred DATA";
    const auto from_a = radio_a.launch(text_a, a.now);
    CHECK(from_a.size() == 158 && text_b.receive(view(from_a), b.now) == Text::Ingress::delivered);
    expect_text(text_b, long_text, 3, b.now);
    text_b.service(b.now);
    active_case = "peer B launches its ACK";
    const auto ack_b = radio_b.launch(text_b, b.now);
    CHECK(text_a.receive(view(ack_b), a.now) == Text::Ingress::acknowledged);

    f5_test::active_case = "durable block cancels stale queued DATA and refuses authenticated ingress";
    const auto late_b = offer(text_b, radio_b, 4, peer_text, b.now);
    CHECK(text_a.send_text(5, view(long_text), a.now));
    text_a.service(a.now);
    CHECK(radio_a.queued && text_a.frame_eligible(view(*radio_a.queued), a.now));
    const auto stale = *radio_a.queued;
    CHECK(owner_a.block(*binding_a) == DirectResult::ok && owner_a.blocked(binding_a->peer));
    CHECK(!radio_a.queued && !text_a.frame_eligible(view(stale), a.now));
    const auto blocked = fake::changes();
    CHECK(text_a.receive(view(late_b), a.now) == Text::Ingress::refused);
    CHECK(!text_a.send_text(6, view(long_text), a.now) && !text_a.take_received(a.now));
    CHECK(fake::changes() == blocked && !owner_a.traffic_grant(*binding_a));
    CHECK(owner_a.unblock(binding_a->peer) == DirectResult::ok);
    CHECK(owner_a.activate(*binding_a) != DirectResult::ok);
    CHECK(!owner_a.traffic_grant(*binding_a) && !text_a.frame_eligible(view(stale), a.now));

    // B's unanswered DATA hits the existing delivery deadline. The session
    // retires its four lanes and F5 lease; it cannot preserve a queued state.
    b.now += 30000;
    text_b.service(b.now);
    CHECK(text_b.send_state() == Text::SendState::closed && !b.rekey->ready());
    CHECK(!radio_b.queued && !text_b.send_text(7, view(peer_text), b.now));
}
}  // namespace direct_text_test

int main() {
    direct_text_test::exercise();
    std::cout << "PASS 3 direct protected text groups\n";
    return 0;
}
