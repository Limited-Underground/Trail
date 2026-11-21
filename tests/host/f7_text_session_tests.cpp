// Reuse the retained D-112 and F5 fixtures without altering the F5 regression executable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main f5_status_regression_fixture_main
#include "f5_rekey_status_tests.cpp"
#undef main
#pragma GCC diagnostic pop
#include <utility>

namespace f7_test {

using f5_test::Packet;
using f5_test::Pair;
using f5_test::Side;
using f5_test::Status;
using f5_test::active_case;
namespace packet = opentrail::protocol::v1;
using packet::selected_packet_detail::Binding;

radio::ByteView view(const Packet& bytes) {
    return {bytes.data(), bytes.size()};
}

// Build a valid authenticated frame independently of the application admission
// so the receiver sees even the payloads a conforming sender would refuse.
Packet authenticated_frame(const Binding& binding, std::uint32_t id, std::uint64_t counter,
                           const Packet& plaintext) {
    packet::Header header{};
    header.type = binding.purpose;
    header.epoch = binding.context.epoch;
    header.sender_alias = binding.context.sender_alias;
    header.recipient_alias = binding.context.recipient_alias;
    header.message_id = id;
    header.counter = counter;
    header.ciphertext_bytes = static_cast<std::uint16_t>(plaintext.size());
    std::array<std::uint8_t, packet::aad_bytes> aad{};
    const auto nonce = security::compose_aead_nonce(
        {binding.lease_domain, binding.domain, binding.prefix, counter});
    CHECK(nonce.composed());
    CHECK(packet::aad(header, binding.context, aad) == packet::Error::none);
    std::array<std::uint8_t, packet::max_mtu> cipher{};
    unsigned long long encrypted_bytes = 0;
    CHECK(plaintext.size() + packet::tag_bytes <= cipher.size());
    CHECK(crypto_aead_chacha20poly1305_ietf_encrypt(cipher.data(), &encrypted_bytes,
        plaintext.data(), plaintext.size(), aad.data(), aad.size(), nullptr,
        nonce.bytes.data(), binding.key.data()) == 0);
    CHECK(encrypted_bytes == plaintext.size() + packet::tag_bytes);
    Packet frame(packet::max_mtu);
    const auto sealed = packet::assemble_frame(header, binding.context,
        {cipher.data(), plaintext.size()}, {cipher.data() + plaintext.size(), packet::tag_bytes},
        {frame.data(), frame.size()}, 163);
    CHECK(sealed.error == packet::Error::none);
    frame.resize(sealed.encoded_bytes);
    return frame;
}

Packet otxt(const Packet& text) {
    Packet encoded{'O', 'T', 'X', 'T', 1, 0, 0, static_cast<std::uint8_t>(text.size())};
    encoded.insert(encoded.end(), text.begin(), text.end());
    return encoded;
}

#ifndef F7_STATUS_ONLY_BASELINE
void open_text(Side& side, Status::TextArm& arm, std::uint64_t deadline,
               radio::RadioTransport& transport) {
    CHECK(!side.status && side.rekey->ready());
    side.lease_generation = side.rekey->active_generation();
    auto& lanes = side.packets[side.lease_generation];
    side.status.emplace(side.rekey->data_tx(), side.rekey->ack_tx(), side.rekey->data_rx(),
                        side.rekey->ack_rx(), lanes[0], lanes[1], lanes[2], lanes[3],
                        transport, &arm);
    CHECK(side.status->start(side.now, deadline));
}

void open_text(Side& side, Status::TextArm& arm) {
    open_text(side, arm, side.now + 60000, side.radio);
}
#endif

void protocol_text_admission() {
#ifndef F7_STATUS_ONLY_BASELINE
    Status::TextArm arm{};
#endif
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
#ifdef F7_STATUS_ONLY_BASELINE
    pair.b.open_status();
#else
    open_text(pair.b, arm);
#endif
    const Packet text{'M', 'e', 's', 'h', 'T', 'r', 'a', 'i', 'l'};
    const auto data = authenticated_frame(pair.a.rekey->data_tx(), 7, 1, otxt(text));

    // Exact same acceptance assertion against the main F5 status admission and
    // the successor's selected F7 text admission on genuine retained-pair keys.
    CHECK(pair.b.status->receive(view(data), pair.b.now));
    CHECK(pair.b.status->received_count() == 1);
#ifndef F7_STATUS_ONLY_BASELINE
    auto received = pair.b.status->received_text();
    CHECK(received.size == text.size() && received.data != nullptr);
    CHECK(std::equal(text.begin(), text.end(), received.data));
#endif
}

#ifndef F7_STATUS_ONLY_BASELINE
class LimitedRadio final : public radio::RadioTransport {
public:
    explicit LimitedRadio(f5_test::Radio& port) : port_(port) {}
    std::size_t mtu() const override { return 154; }
    radio::TransportStatus status() const override {
        auto result = port_.status();
        result.mtu_bytes = mtu();
        return result;
    }
    radio::SendResult send(radio::ByteView bytes, std::uint64_t now) override {
        CHECK(bytes.size <= mtu());
        return port_.send(bytes, now);
    }
    radio::ReceiveResult receive(radio::MutableByteView bytes) override {
        return port_.receive(bytes);
    }
    void service(std::uint64_t now) override { port_.service(now); }

private:
    f5_test::Radio& port_;
};

struct PacketMedia {
    using Slots = decltype(pairing_adapter_test::persistent_bytes(std::declval<const CallbackStorage&>()));
    std::array<Slots, 4> bytes;
    std::array<std::uint64_t, 4> mutations{};

    bool operator==(const PacketMedia& other) const {
        return bytes == other.bytes && mutations == other.mutations;
    }
};

PacketMedia packet_media(const Side& side) {
    PacketMedia media{};
    const auto& lanes = side.packets.at(side.lease_generation);
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        media.bytes[lane] = pairing_adapter_test::persistent_bytes(lanes[lane]);
        for (std::size_t d = 0; d < persistence::kStorageDomainCount; ++d) {
            const auto counts = lanes[lane].inner.memory.counters(static_cast<persistence::StorageDomain>(d));
            media.mutations[lane] += counts.erases + counts.writes + counts.syncs;
        }
    }
    return media;
}

void check_text(Side& side, const Packet& expected) {
    CHECK(side.status->received_count() == 1);
    const auto received = side.status->received_text();
    CHECK(received.size == expected.size() && received.data != nullptr);
    CHECK(std::equal(expected.begin(), expected.end(), received.data));
}

Packet send_text(Side& from, std::uint32_t id, const Packet& text) {
    const auto before = from.radio.sent.size();
    CHECK(from.status->send_text(id, view(text), from.now));
    CHECK(from.status->state() == Status::State::queued && from.radio.sent.size() == before);
    from.status->service(from.now);
    CHECK(from.radio.sent.size() == before + 1);
    CHECK(from.status->state() == Status::State::queued);
    from.status->on_data_transmitted();
    CHECK(from.status->state() == Status::State::transmitted);
    return from.radio.sent.back();
}

Packet receive_text(Side& to, const Packet& frame, const Packet& text) {
    const auto before = to.radio.sent.size();
    CHECK(to.status->receive(view(frame), to.now));
    check_text(to, text);
    to.status->service(to.now);
    CHECK(to.radio.sent.size() == before + 1);
    return to.radio.sent.back();
}

void refuse_input_without_writes(Side& side, const Packet& frame) {
    const auto media = packet_media(side);
    const auto sent = side.radio.sent.size();
    const auto received = side.status->received_count();
    CHECK(!side.status->receive(view(frame), side.now));
    CHECK(packet_media(side) == media && side.radio.sent.size() == sent);
    CHECK(side.status->received_count() == received);
}

void bilateral_delivery_and_ack() {
    Status::TextArm arm_a{}, arm_b{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    open_text(pair.a, arm_a);
    open_text(pair.b, arm_b);
    const Packet text_a(90, 'x');
    const Packet text_b{'M', 'e', 's', 'h', 0x20, 0xf0, 0x9f, 0x9a, 0xb2, 0x0a};
    const auto data_a = send_text(pair.a, 11, text_a);
    const auto data_b = send_text(pair.b, 12, text_b);
    CHECK(data_a.size() == 158 && data_b.size() == 68 + text_b.size());
    const auto ack_b = receive_text(pair.b, data_a, text_a);
    const auto ack_a = receive_text(pair.a, data_b, text_b);
    CHECK(ack_b.size() == 68 && ack_a.size() == 68);

    Packet reference(8);
    reference.back() = 1;
    refuse_input_without_writes(pair.a, authenticated_frame(pair.b.rekey->ack_tx(), 12, 9, reference));
    reference.back() = 2;
    refuse_input_without_writes(pair.a, authenticated_frame(pair.b.rekey->ack_tx(), 11, 10, reference));
    CHECK(pair.a.status->state() == Status::State::transmitted);
    f5_test::accept_ack(pair.a, ack_b);
    f5_test::accept_ack(pair.b, ack_a);

    const auto media = packet_media(pair.b);
    CHECK(receive_text(pair.b, data_a, text_a) == ack_b);
    CHECK(packet_media(pair.b) == media);
    const auto repeated = Packet{'n', 'e', 'x', 't'};
    refuse_input_without_writes(pair.b, authenticated_frame(pair.a.rekey->data_tx(), 13, 2, otxt(repeated)));
    check_text(pair.b, text_a);
}

void refuse_outbound_before_allocation() {
    Status::TextArm arm{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    open_text(pair.a, arm);
    const auto before = packet_media(pair.a);
    for (const auto& invalid : {Packet(91, 'x'), Packet{0xc0, 0xaf}, Packet{}}) {
        CHECK(!pair.a.status->send_text(11, view(invalid), pair.a.now));
        CHECK(packet_media(pair.a) == before && pair.a.radio.sent.empty());
    }
    CHECK(!pair.a.status->send_status(11, pair.a.now));
    CHECK(packet_media(pair.a) == before && pair.a.radio.sent.empty());
    const Packet valid{'o', 'k'};
    CHECK(!send_text(pair.a, 11, valid).empty());
    const auto after = packet_media(pair.a);
    CHECK(!pair.a.status->send_text(12, view(valid), pair.a.now));
    CHECK(packet_media(pair.a) == after && pair.a.radio.sent.size() == 1);
}

void refuse_authenticated_invalid_payloads() {
    Status::TextArm arm{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    open_text(pair.b, arm);
    const auto& binding = pair.a.rekey->data_tx();
    const Packet text{'o', 'k'};
    auto reserved = otxt(text);
    reserved[5] = 1;
    refuse_input_without_writes(pair.b, authenticated_frame(binding, 12, 5, reserved));
    refuse_input_without_writes(pair.b, authenticated_frame(binding, 13, 6, otxt({0xc0, 0xaf})));
    refuse_input_without_writes(pair.b, authenticated_frame(binding, 14, 7, otxt(Packet(91, 'x'))));
    Packet status(protocol::kQuickStatusPayloadBytes);
    CHECK(protocol::encode_quick_status({protocol::QuickStatusKind::ok},
        {status.data(), status.size()}).encoded());
    refuse_input_without_writes(pair.b, authenticated_frame(binding, 15, 8, status));
    auto bad_tag = authenticated_frame(binding, 16, 9, otxt(text));
    CHECK(!bad_tag.empty());
    bad_tag[bad_tag.size() - 1] ^= 1;
    refuse_input_without_writes(pair.b, bad_tag);
    CHECK(pair.b.status->received_count() == 0 && pair.b.status->received_text().size == 0);
    pair.b.status->service(pair.b.now);
    CHECK(pair.b.radio.sent.empty());

    // Lower, first valid counter still succeeds after higher rejected counters.
    const auto valid = authenticated_frame(binding, 17, 1, otxt(text));
    CHECK(receive_text(pair.b, valid, text).size() == 68);
}

void refuse_at_smaller_link_budget() {
    Status::TextArm arm_a{}, arm_b{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    LimitedRadio radio_a(pair.a.radio), radio_b(pair.b.radio);
    open_text(pair.a, arm_a, pair.a.now + 60000, radio_a);
    open_text(pair.b, arm_b, pair.b.now + 60000, radio_b);
    const Packet too_long(87, 'x');
    const auto media = packet_media(pair.a);
    CHECK(!pair.a.status->send_text(19, view(too_long), pair.a.now));
    CHECK(packet_media(pair.a) == media && pair.a.radio.sent.empty());
    const auto oversized_frame = authenticated_frame(pair.a.rekey->data_tx(), 20, 9, otxt(too_long));
    CHECK(oversized_frame.size() == 155);
    refuse_input_without_writes(pair.b, oversized_frame);

    const Packet boundary(86, 'x');
    const auto frame = send_text(pair.a, 21, boundary);
    CHECK(frame.size() == 154);
    f5_test::accept_ack(pair.a, receive_text(pair.b, frame, boundary));
    pair.a.status.reset();
    pair.b.status.reset();
}

void late_arrival_and_launch_deadline() {
    Status::TextArm arm_a{}, arm_b{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    const auto end_b = pair.b.now + 60000;
    open_text(pair.a, arm_a);
    open_text(pair.b, arm_b);
    const Packet text{'o', 'k'};
    const auto frame = send_text(pair.a, 31, text);
    const auto latest_launch = pair.a.now + 30000;
    CHECK(pair.a.status->frame_deadline(view(frame)) == latest_launch);
    CHECK(pair.a.status->frame_eligible(view(frame), latest_launch - 1));
    CHECK(!pair.a.status->frame_eligible(view(frame), latest_launch));

    pair.b.now = end_b;
    refuse_input_without_writes(pair.b, frame);
    const auto media = packet_media(pair.b);
    CHECK(!pair.b.status->send_text(31, view(text), pair.b.now));
    CHECK(packet_media(pair.b) == media && pair.b.radio.sent.empty());
    CHECK(pair.b.status->received_text().size == 0);

    // A sampled expiry stays terminal even if the next clock read rolls back
    // before periodic service has retired the Packet owner.
    pair.b.now = end_b - 1;
    refuse_input_without_writes(pair.b, frame);
    CHECK(!pair.b.status->send_text(31, view(text), pair.b.now));
    CHECK(packet_media(pair.b) == media && pair.b.radio.sent.empty());
    pair.b.status->service(pair.b.now);
    CHECK(pair.b.status->closed() && pair.b.radio.sent.empty());
}

void consumed_arm_survives_successor() {
    Status::TextArm arm_a{}, arm_b{}, restarted_arm_b{};
    Pair pair;
    f5_test::finish(pair.a, pair.b, f5_test::start(pair.a), 2);
    const auto original_end_a = pair.a.now + 60000;
    open_text(pair.a, arm_a);
    open_text(pair.b, arm_b);
    const Packet text{'o', 'k'};
    const auto first_a = send_text(pair.a, 41, text);
    f5_test::accept_ack(pair.a, receive_text(pair.b, first_a, text));
    const auto first_b = send_text(pair.b, 42, text);
    f5_test::accept_ack(pair.b, receive_text(pair.a, first_b, text));

    // A's Packet lease changes, but A keeps the same arm. B really reopens its
    // retained stores and receives a new local arm after the simulated reboot.
    pair.b.reboot();
    f5_test::finish(pair.b, pair.a, f5_test::start(pair.b), 3);
    pair.a.quiet_old_status();
    open_text(pair.a, arm_a, original_end_a, pair.a.radio);
    open_text(pair.b, restarted_arm_b);
    const auto media = packet_media(pair.a);
    const auto sends = pair.a.radio.sent.size();
    CHECK(!pair.a.status->send_text(43, view(text), pair.a.now));
    CHECK(packet_media(pair.a) == media && pair.a.radio.sent.size() == sends);
    const auto fresh_b = send_text(pair.b, 44, text);
    refuse_input_without_writes(pair.a, fresh_b);
    pair.a.status->service(pair.a.now);
    CHECK(packet_media(pair.a) == media && pair.a.radio.sent.size() == sends);
    CHECK(pair.a.status->received_count() == 0 && pair.a.status->received_text().size == 0);
}
#endif

} // namespace f7_test

int main() {
    f5_test::active_case = "authenticated OTXT admission after retained-pair F5 rekey";
    f7_test::protocol_text_admission();
#ifndef F7_STATUS_ONLY_BASELINE
    using namespace f7_test;
    active_case = "bidirectional delivery, matching ACK and duplicate cache";
    bilateral_delivery_and_ack();
    active_case = "empty, invalid and oversized outbound text";
    refuse_outbound_before_allocation();
    active_case = "malformed, oversized and wrong-kind authenticated input";
    refuse_authenticated_invalid_payloads();
    active_case = "154-byte link ceiling";
    refuse_at_smaller_link_budget();
    active_case = "launch deadline and late text ingress";
    late_arrival_and_launch_deadline();
    active_case = "original text arm across peer restart and successor rekey";
    consumed_arm_survives_successor();
    std::cout << "PASS 7 retained-pair F7 text session groups\n";
#endif
    return 0;
}
