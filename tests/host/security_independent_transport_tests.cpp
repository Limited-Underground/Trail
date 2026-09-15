#include <algorithm>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "security_independent_session_fixture.hpp"
#include "opentrail/independent_handshake_transport.hpp"
#include "fake_secure_random.hpp"
#include "fake_radio_transport.hpp"

namespace transport_test {
using namespace invitation_lifecycle_test;
using radio::test_support::FakeRadioTransport;
using Result = HandshakeTransportPoll;
static_assert(!std::is_copy_constructible_v<IndependentHandshakeTransport>);
static_assert(IndependentHandshakeTransport::maximum_packet_bytes == 154);

struct Authority final : ConfirmationAuthority {
    ConfirmationSample value{{1, 1}, 100};
    ConfirmationSample sample() override { return value; }
};

// Fault controls wrap the maintained copying/queueing transport, not a second
// transport implementation. Captured frames are public test handshake bytes.
struct Link final : radio::RadioTransport {
    FakeRadioTransport inner;
    std::optional<std::size_t> reported_mtu;
    std::function<void(std::string_view)> callback = [](std::string_view) {};
    std::function<void(std::array<std::uint8_t, 255>&, std::size_t&)> mutate;
    std::array<std::uint8_t, 255> last_frame{};
    std::size_t last_size{0};
    bool short_send{false}, malformed_no_data{false};
    radio::RadioError receive_error{radio::RadioError::none};
    unsigned send_calls{0}, receive_calls{0}, service_calls{0};
    explicit Link(std::size_t mtu = 154) : inner(mtu) {}
    std::size_t mtu() const override {
        callback("mtu");
        return reported_mtu ? *reported_mtu : inner.mtu();
    }
    radio::TransportStatus status() const override { return inner.status(); }
    radio::SendResult send(radio::ByteView bytes, std::uint64_t now) override {
        ++send_calls;
        CHECK(bytes.size <= last_frame.size());
        std::copy_n(bytes.data, bytes.size, last_frame.begin());
        last_size = bytes.size;
        if (mutate) mutate(last_frame, last_size);
        callback("send");
        if (short_send) return {radio::RadioError::none, bytes.size - 1};
        return inner.send({last_frame.data(), last_size}, now);
    }
    radio::ReceiveResult receive(radio::MutableByteView destination) override {
        ++receive_calls;
        callback("receive");
        if (receive_error != radio::RadioError::none) return {receive_error, 0, {}};
        if (malformed_no_data) return {radio::RadioError::no_data, 1, {}};
        return inner.receive(destination);
    }
    void service(std::uint64_t now) override {
        ++service_calls;
        callback("service");
        inner.service(now);
    }
};

struct Peer {
    Storage boot, role, tx, rx;
    security::test_support::FakeSecureRandomSource random;
    Authority authority;
    IndependentHandshakeEndpoint endpoint;
    Link link;
    IndependentHandshakeTransport transport;
    Peer(InvitationRole role_value, const InvitationKey& signer, unsigned offset,
         HandshakeTransportConfig config)
        : endpoint(random, boot, role, tx, rx, authority, role_value, signer),
          transport(endpoint, link, config) {
        std::array<unsigned char, 64> entropy{};
        for (unsigned i = 0; i < entropy.size(); ++i)
            entropy[i] = static_cast<unsigned char>(offset + i);
        CHECK(random.load_bytes(entropy.data(), entropy.size()));
        random.set_state(security::EntropyState::ready);
    }
    unsigned session_writes() const { return tx.mutations() + rx.mutations(); }
    std::uint64_t now() const { return authority.value.now_ms; }
};

struct Pair {
    SignedInvitation signer;
    Peer a, b;
    IndependentInvitation invitation{};
    Pair(HandshakeTransportConfig a_config = {11, 22, 9})
        : a(InvitationRole::initiator, signer.fields.signer, 0, a_config),
          b(InvitationRole::responder, signer.fields.signer, 80, {22, 11, 9}) {
        b.authority.value = {{5, 8}, 70000};
        { InvitationBootAuthority previous(b.boot); CHECK(previous.start()); }
        CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
        CHECK(a.endpoint.boot_context() != b.endpoint.boot_context());
        IndependentInvitationFields fields{};
        fields.group = 17; fields.epoch = 1; fields.signer = signer.fields.signer;
        fields.peer_a = a.endpoint.public_identity(); fields.peer_b = b.endpoint.public_identity();
        fields.nonce.fill(3);
        fields.boot_a = a.endpoint.boot_context(); fields.boot_b = b.endpoint.boot_context();
        fields.issued_a_ms = 100; fields.window_a_ms = 900;
        fields.issued_b_ms = 70000; fields.window_b_ms = 300;
        CHECK(encode_independent_invitation(fields, invitation));
        CHECK(crypto_sign_detached(invitation.signature.data(), nullptr, invitation.payload.data(),
                                   invitation.payload.size(), signer.secret.data()) == 0);
        CHECK(a.endpoint.begin(invitation, b.endpoint.public_identity()));
        CHECK(b.endpoint.begin(invitation, a.endpoint.public_identity()));
        a.link.inner.connect(b.link.inner); b.link.inner.connect(a.link.inner);
    }
    void transfer(Peer& from, Peer& to, unsigned step) {
        CHECK(from.transport.send(from.now()));
        CHECK(from.link.last_size <= 154);
        const auto packet = protocol::decode_packet({from.link.last_frame.data(), from.link.last_size});
        CHECK(packet.decoded() && packet.packet.header.message_id == step);
        CHECK(from.transport.poll(from.now()) == Result::waiting);
        CHECK(to.transport.poll(to.now()) == Result::frame);
    }
    void handshake() { transfer(a, b, 1); transfer(b, a, 2); transfer(a, b, 3); }
};

void closed(Peer& peer) {
    CHECK(peer.endpoint.secrets_cleared());
    const auto calls = peer.link.send_calls + peer.link.receive_calls + peer.link.service_calls;
    CHECK(!peer.transport.send(peer.now()));
    CHECK(peer.transport.poll(peer.now()) == Result::refused);
    CHECK(calls == peer.link.send_calls + peer.link.receive_calls + peer.link.service_calls);
}

void repair_crc(std::array<std::uint8_t, 255>& frame, std::size_t size) {
    CHECK(size >= protocol::kPacketOverheadBytes);
    const auto crc = protocol::crc16_ccitt_false({frame.data(), size - 2});
    frame[size - 2] = static_cast<std::uint8_t>(crc);
    frame[size - 1] = static_cast<std::uint8_t>(crc >> 8);
}

unsigned happy_and_loss() {
    unsigned groups = 0;
    {
        Pair p;
        radio::LinkMetadata untrusted{};
        untrusted.received_at_ms = std::numeric_limits<std::uint64_t>::max();
        untrusted.frequency_hz = 1; untrusted.frequency_valid = true;
        p.a.link.inner.set_link_metadata(untrusted);
        CHECK(p.a.transport.poll(p.a.now()) == Result::waiting);
        CHECK(p.b.transport.poll(p.b.now()) == Result::waiting);
        p.handshake();
        const auto* a = p.a.endpoint.offer(); const auto* b = p.b.endpoint.offer();
        CHECK(a && b && a->transcript() == b->transcript());
        CHECK(a->deadline_ms() == 1000 && b->deadline_ms() == 70300);
        CHECK(p.a.session_writes() == 0 && p.b.session_writes() == 0);
        CHECK(p.a.endpoint.confirm(*a) && p.b.endpoint.confirm(*b));
        CHECK(p.a.transport.poll(p.a.now()) == Result::waiting);
        CHECK(p.b.transport.poll(p.b.now()) == Result::waiting);
        CHECK(p.a.transport.close() && p.b.transport.close());
        opentrail_independent_session_test::require_retired(p.a.rx);
        opentrail_independent_session_test::require_retired(p.b.rx);
        closed(p.a); closed(p.b); ++groups;
    }
    for (unsigned lost_step = 1; lost_step <= 3; ++lost_step) {
        Pair p;
        if (lost_step >= 2) p.transfer(p.a, p.b, 1);
        if (lost_step >= 3) p.transfer(p.b, p.a, 2);
        auto& sender = lost_step == 2 ? p.b : p.a;
        auto& receiver = lost_step == 2 ? p.a : p.b;
        sender.link.inner.drop_next_transmissions(1);
        CHECK(sender.transport.send(sender.now()));
        CHECK(sender.transport.poll(sender.now()) == Result::waiting);
        CHECK(receiver.transport.poll(receiver.now()) == Result::waiting);
        const auto sends = sender.link.send_calls;
        CHECK(sender.transport.poll(sender.now()) == Result::waiting);
        CHECK(sender.link.send_calls == sends); // No regeneration or automatic retry.
        p.a.authority.value.now_ms = 1000; p.b.authority.value.now_ms = 70300;
        CHECK(p.a.transport.poll(p.a.now()) == Result::refused);
        CHECK(p.b.transport.poll(p.b.now()) == Result::refused);
        CHECK(p.a.session_writes() == 0 && p.b.session_writes() == 0);
        closed(p.a); closed(p.b); ++groups;
    }
    return groups;
}

unsigned wire_refusals() {
    unsigned groups = 0;
    {
        Pair p; p.transfer(p.a, p.b, 1);
        CHECK(p.a.link.inner.send({p.a.link.last_frame.data(), p.a.link.last_size}, p.a.now()).accepted());
        p.a.link.inner.service(p.a.now());
        CHECK(p.b.transport.poll(p.b.now()) == Result::refused);
        CHECK(p.b.session_writes() == 0); closed(p.b); ++groups;
    }
    // Mutations cover both generic packet admission and canonical handshake bytes.
    // Recomputed CRC is deliberately not treated as cryptographic authentication.
    for (unsigned fault = 0; fault < 14; ++fault) {
        Pair p;
        p.a.link.mutate = [fault](auto& frame, std::size_t& size) {
            switch (fault) {
                case 0: frame[2] = 1; break; // Outer experimental version.
                case 1: frame[3] = 1; break; // Valid generic position type, wrong lane.
                case 2: frame[4] = 1; break;
                case 3: frame[6] ^= 1; break; // Routing source.
                case 4: frame[10] ^= 1; break; // Routing network.
                case 5: frame[14] = 0; break;
                case 6: frame[21] = 2; break; // Header/payload step mismatch.
                case 7: frame[22] = 0; frame[23] = 0; break;
                case 8: frame[22] = 129; frame[23] = 0; break;
                case 9: frame[20] = 2; break; // Inner version.
                case 10: frame[14] = 3; frame[21] = 3; break; // Future step.
                case 11: frame[24] ^= 1; break; // Actual Noise bytes, valid CRC.
                case 12: --frame[18]; break; // One octet beyond declared packet payload.
                default: frame[size - 1] ^= 1; return; // Wrong CRC.
            }
            repair_crc(frame, size);
        };
        CHECK(p.a.transport.send(p.a.now()));
        CHECK(p.a.transport.poll(p.a.now()) == Result::waiting);
        CHECK(p.b.transport.poll(p.b.now()) == Result::refused);
        CHECK(p.b.session_writes() == 0); closed(p.b); ++groups;
    }
    return groups;
}

unsigned callback_and_transport_faults() {
    unsigned groups = 0;
    for (const auto mtu : {std::size_t{0}, std::size_t{153}, std::size_t{256}}) {
        Pair p; p.a.link.reported_mtu = mtu;
        CHECK(!p.a.transport.send(p.a.now()));
        CHECK(p.a.link.send_calls == 0 && p.a.session_writes() == 0);
        closed(p.a); ++groups;
    }
    for (unsigned fault = 0; fault < 3; ++fault) {
        Pair p;
        if (fault == 0) p.a.link.short_send = true;
        if (fault == 1) p.a.link.inner.fail_next_send(radio::RadioError::io_failure);
        if (fault == 2) p.a.link.inner.fail_next_send(radio::RadioError::queue_full);
        CHECK(!p.a.transport.send(p.a.now()));
        CHECK(p.a.link.send_calls == 1 && p.a.session_writes() == 0);
        closed(p.a); ++groups;
    }
    for (bool malformed : {false, true}) {
        Pair p;
        p.b.link.malformed_no_data = malformed;
        if (!malformed) p.b.link.receive_error = radio::RadioError::io_failure;
        CHECK(p.b.transport.poll(p.b.now()) == Result::refused);
        closed(p.b); ++groups;
    }
    for (const std::string_view event : {"mtu", "send", "service", "receive"}) {
        Pair p;
        p.a.link.callback = [&](std::string_view actual) {
            if (actual == event) p.a.authority.value.now_ms = 1000;
        };
        if (event == "send") CHECK(!p.a.transport.send(p.a.now()));
        else CHECK(p.a.transport.poll(p.a.now()) == Result::refused);
        CHECK(p.a.session_writes() == 0); closed(p.a); ++groups;
    }
    {
        Pair p;
        p.b.link.callback = [&](std::string_view event) {
            if (event == "receive") ++p.b.authority.value.context.session_nonce;
        };
        CHECK(p.b.transport.poll(p.b.now()) == Result::refused);
        closed(p.b); ++groups;
    }
    for (bool nested_close : {false, true}) {
        Pair p; bool called = false;
        p.a.link.callback = [&](std::string_view event) {
            if (event != "send" || called) return;
            called = true;
            if (nested_close) CHECK(!p.a.transport.close());
            else CHECK(p.a.transport.poll(p.a.now()) == Result::refused);
        };
        CHECK(!p.a.transport.send(p.a.now()) && called);
        CHECK(!p.a.transport.close()); closed(p.a); ++groups;
    }
    return groups;
}

unsigned lifecycle_guards() {
    unsigned groups = 0;
    {
        for (const auto config : {HandshakeTransportConfig{0, 22, 9}, {11, 0, 9},
                                  {11, 22, 0}, {11, 11, 9}}) {
            Pair p(config); CHECK(!p.a.transport.send(p.a.now()));
            CHECK(p.a.link.send_calls == 0); closed(p.a);
        }
        ++groups;
    }
    for (const auto clock : {std::uint64_t{99}, std::numeric_limits<std::uint64_t>::max()}) {
        Pair p; CHECK(p.a.transport.poll(100) == Result::waiting);
        CHECK(p.a.transport.poll(clock) == Result::refused);
        closed(p.a); ++groups;
    }
    {
        Pair p; p.handshake();
        const auto* offer = p.a.endpoint.offer(); CHECK(offer && p.a.endpoint.confirm(*offer));
        p.a.rx.arm(Fault::write_before);
        CHECK(!p.a.transport.close()); CHECK(!p.a.transport.close());
        closed(p.a); ++groups;
    }
    {
        Pair p; CHECK(p.a.transport.send(p.a.now()));
        CHECK(p.a.link.inner.status().transmit_queue_depth == 1);
        CHECK(p.a.transport.close()); closed(p.a);
        // The transport contract has no cancellation. Previously copied public
        // bytes may still arrive, but cannot reactivate the closed local endpoint.
        p.a.link.inner.service(p.a.now());
        CHECK(p.b.transport.poll(p.b.now()) == Result::frame);
        CHECK(p.b.session_writes() == 0 && p.b.transport.close()); ++groups;
    }
    return groups;
}
} // namespace transport_test

int main() {
    const unsigned groups = transport_test::happy_and_loss() + transport_test::wire_refusals() +
        transport_test::callback_and_transport_faults() + transport_test::lifecycle_guards();
    std::cout << "PASS " << groups << " independent transport groups\n";
}
