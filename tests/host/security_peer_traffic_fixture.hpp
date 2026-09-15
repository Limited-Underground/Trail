#pragma once
// Real crypto and independent authorities; deterministic host-only I/O faults.
#include <functional>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/independent_peer_traffic_endpoint.hpp"
#include "fake_secure_random.hpp"

namespace peer_traffic_test {
using namespace invitation_lifecycle_test;
class CallbackStorage final : public persistence::PersistentStorage {
public:
    Storage inner;
    std::function<void(char)> callback = [](char) {};
    persistence::StorageReadResult read_slot(Domain d, std::size_t s,
        persistence::MutableStorageByteView v) override {
        callback('r'); return inner.read_slot(d, s, v);
    }
    Error erase_slot(Domain d, std::size_t s) override {
        callback('e'); return inner.erase_slot(d, s);
    }
    Error write_slot(Domain d, std::size_t s, std::size_t o,
        persistence::StorageByteView v) override {
        callback('w'); return inner.write_slot(d, s, o, v);
    }
    Error sync_slot(Domain d, std::size_t s) override {
        callback('s'); return inner.sync_slot(d, s);
    }
    void arm(Fault fault, unsigned nth = 1) { inner.arm(fault, nth); }
    unsigned mutations() const { return inner.mutations(); }
};
struct Source final : ConfirmationAuthority {
    ConfirmationSample value{{1,1},100};
    std::function<void()> callback = [] {};
    ConfirmationSample sample() override { callback(); return value; }
};
struct SignedIndependentInvitation {
    IndependentInvitationFields fields{};
    IndependentInvitation invitation{};
    std::array<unsigned char,64> secret{};
    SignedIndependentInvitation() {
        SignedInvitation prior;
        fields.group = 17; fields.epoch = 1;
        fields.signer = prior.fields.signer; secret = prior.secret;
        fields.nonce.fill(3);
        fields.issued_a_ms = 100; fields.window_a_ms = 900;
        fields.issued_b_ms = 70000; fields.window_b_ms = 300;
    }
    ~SignedIndependentInvitation() { sodium_memzero(secret.data(), secret.size()); }
    void sign() {
        CHECK(encode_independent_invitation(fields, invitation));
        CHECK(crypto_sign_detached(invitation.signature.data(), nullptr,
            invitation.payload.data(), invitation.payload.size(), secret.data()) == 0);
    }
};
struct Peer {
    CallbackStorage boot, role, tx, rx, trust;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    IndependentPeerTrafficEndpoint endpoint;
    Peer(InvitationRole role_value, const InvitationKey& signer, unsigned offset)
        : endpoint(random, boot, role, tx, rx, trust, source, role_value, signer) {
        std::array<unsigned char,64> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i + offset);
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
    }
    unsigned traffic_mutations() const { return tx.mutations() + rx.mutations(); }
};
inline const IndependentConfirmationOffer& offer(Peer& peer) {
    const auto* value = peer.endpoint.offer(); CHECK(value); return *value;
}
struct Pair {
    SignedIndependentInvitation signed_invite;
    Peer a{InvitationRole::initiator, signed_invite.fields.signer, 0};
    Peer b{InvitationRole::responder, signed_invite.fields.signer, 80};
    Pair() {
        b.source.value = {{5,8},70000};
        { InvitationBootAuthority previous(b.boot); CHECK(previous.start()); }
        CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
        signed_invite.fields.peer_a = a.endpoint.public_identity();
        signed_invite.fields.peer_b = b.endpoint.public_identity();
        signed_invite.fields.boot_a = a.endpoint.boot_context();
        signed_invite.fields.boot_b = b.endpoint.boot_context();
        CHECK(signed_invite.fields.boot_a != signed_invite.fields.boot_b);
        signed_invite.sign();
    }
    void begin() {
        CHECK(a.endpoint.begin(signed_invite.invitation, b.endpoint.public_identity()));
        CHECK(b.endpoint.begin(signed_invite.invitation, a.endpoint.public_identity()));
    }
    static void deliver(Peer& from, Peer& to, unsigned step) {
        HandshakeFrame frame{}; CHECK(from.endpoint.next_handshake(frame));
        CHECK(frame.step == step); CHECK(to.endpoint.receive_handshake(frame));
    }
    void handshake() { begin(); deliver(a,b,1); deliver(b,a,2); deliver(a,b,3); }
    void confirm_local() {
        CHECK(a.endpoint.confirm(offer(a))); CHECK(b.endpoint.confirm(offer(b)));
    }
    static EvaluationRecord control(Peer& from, Peer& to) {
        EvaluationRecord record{}; CHECK(from.endpoint.next_control(record));
        CHECK(to.endpoint.receive_control(record)); return record;
    }
    void activate() {
        handshake(); confirm_local();
        control(a,b); control(b,a); control(a,b); control(b,a);
        CHECK(a.endpoint.ready() && b.endpoint.ready());
    }
};
inline bool same_record(const EvaluationRecord& a, const EvaluationRecord& b) {
    return a.group == b.group && a.epoch == b.epoch && a.counter == b.counter &&
        a.sender == b.sender && a.recipient == b.recipient && a.ciphertext == b.ciphertext;
}
} // namespace peer_traffic_test
