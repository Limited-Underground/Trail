#pragma once
// Actual v2 session traffic proof; entropy and persistence are host fixtures.
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/independent_authorized_policy_session.hpp"
#include "fake_secure_random.hpp"

namespace opentrail_independent_session_test {
using namespace invitation_lifecycle_test;

class CallbackStorage final : public persistence::PersistentStorage {
public:
    Storage inner;
    std::function<void()> before_read;
    persistence::StorageReadResult read_slot(Domain d, std::size_t slot,
                                             persistence::MutableStorageByteView bytes) override {
        auto callback = std::move(before_read);
        before_read = {};
        if (callback) callback();
        return inner.read_slot(d, slot, bytes);
    }
    Error erase_slot(Domain d, std::size_t slot) override { return inner.erase_slot(d, slot); }
    Error write_slot(Domain d, std::size_t slot, std::size_t offset,
                     persistence::StorageByteView bytes) override {
        return inner.write_slot(d, slot, offset, bytes);
    }
    Error sync_slot(Domain d, std::size_t slot) override { return inner.sync_slot(d, slot); }
};

struct SessionPair {
    Storage boot_store_a, boot_store_b, role_store_b, tx_a, rx_a, tx_b, rx_b;
    CallbackStorage role_store_a;
    InvitationBootAuthority boot_a{boot_store_a}, boot_b{boot_store_b};
    security::test_support::FakeSecureRandomSource random_a, random_b;
    IndependentAuthorizedPolicySession a{random_a, tx_a, rx_a}, b{random_b, tx_b, rx_b};
    std::optional<IndependentRoleInvitationAuthority> role_a, role_b;
    IndependentInvitationFields fields{};
    IndependentInvitation invitation{};
    std::array<unsigned char, 64> signing_secret{};

    static void initialize_random(security::test_support::FakeSecureRandomSource& random, unsigned offset) {
        std::array<unsigned char, 64> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<unsigned char>(offset + i);
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
    }
    SessionPair() {
        initialize_random(random_a, 0);
        initialize_random(random_b, 80);
        { InvitationBootAuthority previous(boot_store_b); CHECK(previous.start()); }
        CHECK(boot_a.start() && boot_b.start());
        CHECK(boot_a.context() != boot_b.context());
        CHECK(a.generate_identity() && b.generate_identity());
        SignedInvitation signer;
        fields.group = 17;
        fields.epoch = 1;
        fields.signer = signer.fields.signer;
        signing_secret = signer.secret;
        fields.peer_a = a.public_identity();
        fields.peer_b = b.public_identity();
        fields.nonce.fill(3);
        fields.boot_a = boot_a.context();
        fields.boot_b = boot_b.context();
        fields.issued_a_ms = 100;
        fields.window_a_ms = 900;
        fields.issued_b_ms = 70000;
        fields.window_b_ms = 300;
        CHECK(encode_independent_invitation(fields, invitation));
        CHECK(crypto_sign_detached(invitation.signature.data(), nullptr, invitation.payload.data(),
                                   invitation.payload.size(), signing_secret.data()) == 0);
        role_a.emplace(role_store_a, boot_a, InvitationRole::initiator,
                       fields.signer, fields.peer_a, fields.peer_b);
        role_b.emplace(role_store_b, boot_b, InvitationRole::responder,
                       fields.signer, fields.peer_a, fields.peer_b);
        CHECK(a.bind_authority(*role_a) && b.bind_authority(*role_b));
    }
    ~SessionPair() { sodium_memzero(signing_secret.data(), signing_secret.size()); }

    void handshake(const IndependentInvitation& invitation_a, const IndependentInvitation& invitation_b) {
        CHECK(a.begin(invitation_a, 100) && b.begin(invitation_b, 70000));
        std::array<unsigned char, 128> frame{};
        std::size_t bytes = 0;
        CHECK(a.write(frame.data(), frame.size(), bytes, 101));
        CHECK(b.read(frame.data(), bytes, 70001));
        CHECK(b.write(frame.data(), frame.size(), bytes, 70002));
        CHECK(a.read(frame.data(), bytes, 102));
        CHECK(a.write(frame.data(), frame.size(), bytes, 103));
        CHECK(b.read(frame.data(), bytes, 70003));
        CHECK(a.finish(104) && b.finish(70004));
        CHECK(a.transcript() == b.transcript());
        CHECK(tx_a.mutations() == 0 && rx_a.mutations() == 0 &&
              tx_b.mutations() == 0 && rx_b.mutations() == 0);
    }
};

inline void require_retired(Storage& storage) {
    security_eval::EvaluationReplayStore::Context context{};
    const auto& bytes = storage.memory.slot_bytes(domain, 0);
    std::copy_n(bytes.begin() + 12, context.size(), context.begin());
    const auto mutations = storage.mutations();
    security_eval::EvaluationReplayStore inspected(storage);
    CHECK(inspected.start(context, false) == security_eval::ReplayError::retired);
    CHECK(storage.mutations() == mutations);
}

inline unsigned independent_session_traffic_tests() {
    unsigned groups = 0;
    {
        SessionPair pair;
        pair.handshake(pair.invitation, pair.invitation);
        CHECK(pair.a.confirm(pair.a.transcript(), 105));
        CHECK(pair.tx_b.mutations() == 0 && pair.rx_b.mutations() == 0);
        CHECK(pair.b.confirm(pair.b.transcript(), 70005));

        const std::array<unsigned char, 8> from_a{1,2,3,4,5,6,7,8};
        const std::array<unsigned char, 8> from_b{8,7,6,5,4,3,2,1};
        std::array<unsigned char, 8> plaintext{};
        EvaluationRecord message_a{}, message_b{};
        CHECK(pair.a.seal(from_a, message_a, 106));
        CHECK(pair.b.open(message_a, plaintext, 70006) && plaintext == from_a);
        plaintext.fill(0xA5);
        const auto unchanged = plaintext;
        CHECK(!pair.b.open(message_a, plaintext, 70007) && plaintext == unchanged && !pair.b.failed());
        CHECK(pair.b.seal(from_b, message_b, 70008));
        CHECK(pair.a.open(message_b, plaintext, 107) && plaintext == from_b);
        plaintext = unchanged;
        CHECK(!pair.a.open(message_b, plaintext, 108) && plaintext == unchanged && !pair.a.failed());

        CHECK(pair.a.cancel() && pair.b.cancel());
        CHECK(pair.a.retired() && pair.b.retired() && pair.a.secrets_cleared() && pair.b.secrets_cleared());
        require_retired(pair.rx_a);
        require_retired(pair.rx_b);
        ++groups;
    }
    {
        SessionPair pair;
        const IndependentInvitation original = pair.invitation;
        bool changed = false;
        pair.role_store_a.before_read = [&] {
            changed = true;
            pair.invitation.payload.fill(0);
            pair.invitation.signature.fill(0);
        };
        // The wrapper must use the same stable invitation for the durable claim
        // and the real core, even if its caller's packet changes during storage.
        pair.handshake(pair.invitation, original);
        CHECK(changed && pair.role_a->current() && pair.role_b->current());
        CHECK(pair.a.cancel() && pair.b.cancel());
        CHECK(pair.a.secrets_cleared() && pair.b.secrets_cleared());
        CHECK(pair.tx_a.mutations() == 0 && pair.rx_a.mutations() == 0 &&
              pair.tx_b.mutations() == 0 && pair.rx_b.mutations() == 0);
        ++groups;
    }
    return groups;
}
} // namespace opentrail_independent_session_test
