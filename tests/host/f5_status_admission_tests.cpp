// M5: authenticate real Packet-v1 bytes before testing application admission.
#include "opentrail/f5_status_session.hpp"
#include "memory_persistent_storage.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ot = opentrail;
namespace packet = ot::protocol::v1;
namespace persist = ot::persistence;
using packet::selected_packet_detail::Binding;
using packet::selected_packet_detail::Frame;
using Storage = persist::test_support::MemoryPersistentStorage;
using Session = ot::companion::F5StatusSession;
using Status = ot::protocol::QuickStatusKind;
constexpr auto counter_domain = persist::StorageDomain::outbound_counter_state;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); std::exit(1); } } while (false)

struct CaptureRadio final : ot::radio::RadioTransport {
    std::vector<std::vector<std::uint8_t>> sends;
    std::size_t mtu() const override { return 163; }
    ot::radio::TransportStatus status() const override {
        ot::radio::TransportStatus s{};
        s.state = ot::radio::RadioState::idle;
        return s;
    }
    ot::radio::SendResult send(ot::radio::ByteView value, std::uint64_t) override {
        sends.emplace_back(value.data, value.data + value.size);
        return {ot::radio::RadioError::none, value.size};
    }
    ot::radio::ReceiveResult receive(ot::radio::MutableByteView) override { return {}; }
    void service(std::uint64_t) override {}
};

Binding binding(std::uint8_t from, std::uint8_t to, packet::Type purpose) {
    Binding b{};
    b.context.sender.fill(from);
    b.context.recipient.fill(to);
    b.context.transcript.fill(7);
    b.context.sender_alias = from;
    b.context.recipient_alias = to;
    b.context.group = 17;
    b.context.epoch = 2;
    b.context.direction = from < to ? 0 : 1;
    b.purpose = purpose;
    b.key.fill(static_cast<std::uint8_t>(from * 4 + static_cast<unsigned>(purpose)));
    b.prefix.fill(b.key[0]);
    b.domain.fill(b.key[0]);
    b.lease_domain = b.domain;
    b.active = true;
    b.validity = [](void*, std::uint64_t, std::uint64_t) { return true; };
    return b;
}

Frame seal(const Binding& binding, std::uint64_t counter, packet::View plain) {
    const auto& context = binding.context;
    packet::Header header{};
    header.type = binding.purpose;
    header.epoch = context.epoch;
    header.sender_alias = context.sender_alias;
    header.recipient_alias = context.recipient_alias;
    header.message_id = static_cast<std::uint32_t>(counter);
    header.counter = counter;
    header.ciphertext_bytes = static_cast<std::uint16_t>(plain.size);
    std::array<std::uint8_t, packet::aad_bytes> aad{};
    const auto nonce = ot::security::compose_aead_nonce(
        {binding.lease_domain, binding.domain, binding.prefix, counter});
    CHECK(nonce.composed() && packet::aad(header, context, aad) == packet::Error::none);
    std::array<std::uint8_t, 64> cipher{};
    unsigned long long length = 0;
    CHECK(plain.size + packet::tag_bytes <= cipher.size());
    CHECK(crypto_aead_chacha20poly1305_ietf_encrypt(cipher.data(), &length, plain.data, plain.size,
        aad.data(), aad.size(), nullptr, nonce.bytes.data(), binding.key.data()) == 0);
    CHECK(length == plain.size + packet::tag_bytes);
    Frame frame{};
    const auto result = packet::assemble_frame(header, context, {cipher.data(), plain.size},
        {cipher.data() + plain.size, packet::tag_bytes}, {frame.bytes.data(), frame.bytes.size()}, 163);
    CHECK(result.error == packet::Error::none);
    frame.size = result.encoded_bytes;
    return frame;
}

struct Fixture {
    Binding data_tx, ack_tx, data_rx, ack_rx;
    std::array<Storage,4> stores;
    CaptureRadio radio;
    Session session;
    std::array<std::uint8_t, ot::protocol::kQuickStatusPayloadBytes> valid{};

    explicit Fixture(Session::StatusArm* arm = nullptr, std::uint8_t local = 1, std::uint8_t peer = 2)
        : data_tx(binding(local, peer, packet::Type::data)), ack_tx(binding(local, peer, packet::Type::ack)),
          data_rx(binding(peer, local, packet::Type::data)), ack_rx(binding(peer, local, packet::Type::ack)),
          session(data_tx, ack_tx, data_rx, ack_rx, stores[0], stores[1], stores[2], stores[3], radio,
                  nullptr, arm) {
        CHECK(session.start(100, 60100));
        CHECK(ot::protocol::encode_quick_status({ot::protocol::QuickStatusKind::ok},
            {valid.data(), valid.size()}).encoded());
    }
    Frame status(std::uint64_t counter) const { return seal(data_rx, counter, {valid.data(), valid.size()}); }
    bool receive(const Frame& frame, std::uint64_t now) { return session.receive({frame.bytes.data(), frame.size}, now); }
    auto media() const {
        return std::array{stores[1].slot_bytes(counter_domain,0), stores[1].slot_bytes(counter_domain,1),
            stores[2].slot_bytes(counter_domain,0), stores[2].slot_bytes(counter_domain,1)};
    }
    std::uint32_t mutations() const {
        const auto tx = stores[1].counters(counter_domain);
        const auto rx = stores[2].counters(counter_domain);
        return tx.erases + tx.writes + tx.syncs + rx.erases + rx.writes + rx.syncs;
    }
};

void unsupported_payload_does_not_advance() {
    Fixture f;
    const auto before = f.media();
    const auto mutations = f.mutations();
    const std::array<std::uint8_t,3> unsupported{1, 2, 3};
    const auto frame = seal(f.data_rx, 2, {unsupported.data(), unsupported.size()});
    CHECK(!f.receive(frame, 101));
    CHECK(f.media() == before && f.mutations() == mutations);
    f.session.service(101);
    CHECK(f.radio.sends.empty());
    CHECK(f.receive(f.status(1), 102));
    CHECK(f.session.received_count() == 1);
}

void second_status_preserves_first_receipt() {
    Fixture f;
    const auto first = f.status(1);
    CHECK(f.receive(first, 101));
    f.session.service(101);
    CHECK(f.radio.sends.size() == 1);
    const auto before = f.media();
    const auto mutations = f.mutations();
    CHECK(!f.receive(f.status(2), 102));
    f.session.service(102);
    CHECK(f.media() == before && f.mutations() == mutations && f.radio.sends.size() == 1);
    CHECK(f.receive(first, 103));
    f.session.service(103);
    CHECK(f.session.received_count() == 1 && f.radio.sends.size() == 2);
    CHECK(f.radio.sends[0] == f.radio.sends[1] && f.media() == before && f.mutations() == mutations);
}

void queued_data_has_its_own_deadline() {
    Fixture f;
    CHECK(f.session.send_status(9, 101));
    f.session.service(101);
    CHECK(f.radio.sends.size() == 1);
    const auto& bytes = f.radio.sends[0];
    const ot::radio::ByteView frame{bytes.data(), bytes.size()};
    CHECK(f.session.frame_deadline(frame) == 30101);
    CHECK(f.session.frame_eligible(frame, 30100));
    CHECK(!f.session.frame_eligible(frame, 30101));
    std::array<std::uint8_t,163> other{};
    CHECK(bytes.size() <= other.size());
    std::copy(bytes.begin(), bytes.end(), other.begin());
    other[0] ^= 1;
    CHECK(!f.session.frame_eligible({other.data(), bytes.size()}, 101));
    f.session.close();
    CHECK(!f.session.frame_eligible(frame, 101));
}

void status_uses_remaining_arm() {
    Fixture f;
    CHECK(f.session.send_status(10, 40000));
    f.session.service(40000);
    CHECK(f.radio.sends.size() == 1);
    const auto& bytes = f.radio.sends[0];
    const ot::radio::ByteView frame{bytes.data(), bytes.size()};
    CHECK(f.session.frame_deadline(frame) == 60100);
    CHECK(f.session.frame_eligible(frame, 60099));
    CHECK(!f.session.frame_eligible(frame, 60100));
}

// Each status uses real authenticated DATA and returns an authenticated ACK.
void all_statuses_preserve_their_kind() {
    constexpr std::array kinds{Status::ok, Status::need_assistance, Status::anyone_online, Status::available_to_help};
    for (const auto kind : kinds) {
        Session::StatusArm sender_arm, receiver_arm;
        Fixture sender(&sender_arm);
        Fixture receiver(&receiver_arm, 2, 1);

        CHECK(sender.session.send_status(31, kind, 101));
        CHECK(sender_arm.sent && !sender.session.send_status(32, kind, 101));
        sender.session.service(101);
        CHECK(sender.radio.sends.size() == 1);
        sender.session.on_data_transmitted();
        const auto& data = sender.radio.sends[0];
        CHECK(receiver.session.receive({data.data(), data.size()}, 102));
        CHECK(receiver_arm.received && receiver.session.received_count() == 1);
        CHECK(receiver.session.received_status() == kind);
        receiver.session.service(102);
        CHECK(receiver.radio.sends.size() == 1);
        const auto& ack = receiver.radio.sends[0];
        CHECK(sender.session.receive({ack.data(), ack.size()}, 103));
        CHECK(sender.session.state() == Session::State::acknowledged);
    }
}

void invalid_kind_preserves_status_arm_slots() {
    Session::StatusArm arm;
    Fixture f(&arm);
    const auto invalid = static_cast<Status>(5);
    CHECK(!f.session.send_status(1, invalid, 101));
    CHECK(!f.session.send_status(0, Status::ok, 101));
    CHECK(!arm.sent && f.session.state() == Session::State::idle);

    // Keep the envelope checksum valid so the unknown kind is the rejection.
    auto malformed = f.valid;
    malformed[6] = static_cast<std::uint8_t>(invalid);
    const auto crc = ot::protocol::quick_status_crc32(malformed.data(), 8);
    for (unsigned i = 0; i < 4; ++i) malformed[8 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    const auto before = f.media();
    CHECK(!f.receive(seal(f.data_rx, 2, {malformed.data(), malformed.size()}), 101));
    CHECK(!arm.received && f.media() == before);
    CHECK(f.receive(f.status(1), 102) && arm.received);
    CHECK(f.session.send_status(1, Status::need_assistance, 103) && arm.sent);
}

void status_slots_survive_session_replacement() {
    Session::StatusArm arm;
    Fixture old(&arm);
    CHECK(old.session.send_status(1, Status::available_to_help, 101));
    CHECK(old.receive(old.status(1), 102));
    old.session.close();
    Fixture replacement(&arm);
    CHECK(!replacement.session.send_status(2, Status::anyone_online, 103));
    CHECK(!replacement.receive(replacement.status(2), 103));
    CHECK(replacement.session.received_count() == 0);
    replacement.session.service(60100);
    CHECK(arm.expired && replacement.session.closed());
}

int main() {
    unsupported_payload_does_not_advance();
    second_status_preserves_first_receipt();
    queued_data_has_its_own_deadline();
    status_uses_remaining_arm();
    all_statuses_preserve_their_kind();
    invalid_kind_preserves_status_arm_slots();
    status_slots_survive_session_replacement();
    std::puts("PASS 7 F5 status semantic admission and launch groups");
}
