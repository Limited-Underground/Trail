#include <functional>
#include <memory>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/evaluation_confirmation_owner.hpp"
#include "fake_secure_random.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<EvaluationConfirmationOwner>);
static_assert(!std::is_move_constructible_v<EvaluationConfirmationOwner>);
static_assert(!std::is_copy_assignable_v<ConfirmationOffer>);

struct Source final : ConfirmationAuthority {
    ConfirmationSample value{{1, 1}, 100};
    std::function<void()> callback = [] {};
    unsigned samples{0};
    ConfirmationSample sample() override { ++samples; callback(); return value; }
};
struct Peer {
    security::test_support::FakeSecureRandomSource random;
    AuthorizedPolicySession session;
    Source source;
    std::unique_ptr<RoleInvitationAuthority> authority;
    std::unique_ptr<EvaluationConfirmationOwner> owner;
    Peer(Storage& tx, Storage& rx, unsigned offset) : session(random, tx, rx) {
        std::array<unsigned char, 64> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i + offset);
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
        CHECK(session.generate_identity());
    }
    void bind(Storage& store, InvitationBootAuthority& boot, InvitationRole role, const InvitationFields& fields) {
        authority = std::make_unique<RoleInvitationAuthority>(store, boot, role, fields.signer, fields.peer_a, fields.peer_b);
        owner = std::make_unique<EvaluationConfirmationOwner>(session, *authority, source);
    }
};
struct Pair {
    Storage boot_store, a_store, b_store, a_tx, a_rx, b_tx, b_rx;
    InvitationBootAuthority boot{boot_store};
    SignedInvitation signature;
    Peer a{a_tx, a_rx, 0}, b{b_tx, b_rx, 80};
    Pair() {
        CHECK(boot.start());
        signature.fields.peer_a = a.session.public_identity();
        signature.fields.peer_b = b.session.public_identity();
        signature.fields.boot_context = boot.context(); signature.sign();
        a.bind(a_store, boot, InvitationRole::initiator, signature.fields);
        b.bind(b_store, boot, InvitationRole::responder, signature.fields);
        b.source.value.context.session_nonce = 2;
    }
    void begin() { CHECK(a.owner->begin(signature.invitation)); CHECK(b.owner->begin(signature.invitation)); }
    void handshake() {
        begin(); std::array<unsigned char, 128> bytes{}; std::size_t size = 0;
        CHECK(a.owner->write(bytes.data(), bytes.size(), size)); CHECK(b.owner->read(bytes.data(), size));
        CHECK(b.owner->write(bytes.data(), bytes.size(), size)); CHECK(a.owner->read(bytes.data(), size));
        CHECK(a.owner->write(bytes.data(), bytes.size(), size)); CHECK(b.owner->read(bytes.data(), size));
        CHECK(a.owner->finish()); CHECK(b.owner->finish());
    }
    unsigned mutations() const { return a_tx.mutations() + a_rx.mutations() + b_tx.mutations() + b_rx.mutations(); }
};
static const ConfirmationOffer& offer(Peer& peer) { const auto* result = peer.owner->offer(); CHECK(result); return *result; }
static void refused(Peer& peer) {
    CHECK(peer.owner->outcome() == ConfirmationOutcome::refused);
    CHECK(peer.owner->offer() == nullptr);
    CHECK(peer.session.secrets_cleared());
}
static void cannot_reconstruct(Pair& pair) {
    Storage tx, rx; Peer replacement(tx, rx, 0);
    replacement.bind(pair.a_store, pair.boot, InvitationRole::initiator, pair.signature.fields);
    CHECK(!replacement.owner->begin(pair.signature.invitation));
    refused(replacement); CHECK(tx.mutations() + rx.mutations() == 0);
}

int main() {
    unsigned groups = 0;
    {
        Pair p; CHECK(p.a.owner->offer() == nullptr); p.handshake(); CHECK(p.mutations() == 0);
        for (auto* peer : {&p.a, &p.b}) {
            const auto& o = offer(*peer); const auto role = peer == &p.a ? InvitationRole::initiator : InvitationRole::responder;
            CHECK(o.context() == peer->source.value.context && o.role() == role);
            CHECK(o.group() == p.signature.fields.group && o.epoch() == p.signature.fields.epoch);
            CHECK(o.nonce() == p.signature.fields.nonce && o.deadline_ms() == p.signature.fields.deadline_ms);
            CHECK(o.peer() == (peer == &p.a ? p.b.session.public_identity() : p.a.session.public_identity()));
            CHECK(o.transcript() == peer->session.transcript() && o.transcript() == p.a.session.transcript());
            CHECK(peer->owner->confirm(o)); CHECK(peer->owner->outcome() == ConfirmationOutcome::local_confirmed);
            const auto count = p.mutations();
            CHECK(!peer->owner->confirm(o) && !peer->owner->cancel(o)); CHECK(p.mutations() == count);
            CHECK(peer->owner->offer() == nullptr);
        }
        CHECK(p.a_tx.mutations() > 0 && p.a_rx.mutations() > 0 && p.b_tx.mutations() > 0 && p.b_rx.mutations() > 0);
        // The consuming target, not this owner API, retains the actual evaluation
        // sessions. This real-crypto assertion establishes successful core calls.
        EvaluationRecord record{}; const std::array<unsigned char,8> plain{1,2,3,4,5,6,7,8}; std::array<unsigned char,8> decoded{};
        CHECK(p.a.session.seal(plain, record, 101)); CHECK(p.b.session.open(record, decoded, 101) && decoded == plain);
        const auto before = p.a_rx.mutations(); CHECK(p.a.owner->close()); CHECK(p.a_rx.mutations() > before);
        CHECK(p.a.session.retired() && p.a.session.secrets_cleared()); const auto after = p.mutations();
        CHECK(p.a.owner->close() && p.mutations() == after); CHECK(p.b.owner->close()); ++groups;
    }
    {
        Pair p; p.handshake(); const auto& o = offer(p.a); CHECK(p.a.owner->cancel(o));
        CHECK(p.a.owner->outcome() == ConfirmationOutcome::cancelled && p.a.session.secrets_cleared());
        CHECK(p.mutations() == 0 && !p.a.owner->confirm(o) && !p.a.owner->cancel(o));
        cannot_reconstruct(p); ++groups;
    }
    for (unsigned invalid = 0; invalid < 6; ++invalid) {
        Pair p; p.handshake(); const auto& o = offer(p.a);
        if (invalid == 0) p.a.source.value.now_ms = 1'000;
        if (invalid == 1) p.a.source.value.now_ms = 99;
        if (invalid == 2) p.a.source.value.context.transport_generation++;
        if (invalid == 3) p.a.source.value.context.session_nonce++;
        if (invalid == 4) p.a.source.value.context = {};
        if (invalid == 5) p.a.source.value.now_ms = std::numeric_limits<std::uint64_t>::max();
        CHECK(!p.a.owner->confirm(o)); refused(p.a); CHECK(p.mutations() == 0);
        p.a.source.value = {{1, 1}, 100}; CHECK(!p.a.owner->begin(p.signature.invitation));
        cannot_reconstruct(p); ++groups;
    }
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        Pair p; p.handshake(); if (invalid == 0) p.a.source.value.now_ms = 1'000; else p.a.source.value.context = {};
        CHECK(p.a.owner->offer() == nullptr); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    {
        Pair p; p.handshake(); const ConfirmationOffer copied(offer(p.a));
        CHECK(!p.a.owner->confirm(copied)); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    {
        Pair p; p.handshake(); CHECK(!p.a.owner->cancel(offer(p.b))); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        Pair p;
        if (invalid == 0) p.signature.invitation.signature[0] ^= 1;
        if (invalid == 1) p.a.source.value.context = {};
        if (invalid == 2) p.a.source.value.now_ms = 1'000;
        CHECK(!p.a.owner->begin(p.signature.invitation)); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    for (unsigned during = 0; during < 3; ++during) {
        Pair p; if (during != 0) p.handshake(); const ConfirmationOffer* o = during ? &offer(p.a) : nullptr;
        const auto before = p.a.source.samples;
        p.a.source.callback = [&] {
            if (p.a.source.samples == before + 1) {
                if (during == 2) CHECK(!p.a.owner->close());
                else if (o) CHECK(!p.a.owner->confirm(*o));
                else CHECK(!p.a.owner->begin(p.signature.invitation));
            }
        };
        if (o) CHECK(!p.a.owner->confirm(*o)); else CHECK(!p.a.owner->begin(p.signature.invitation));
        p.a.source.callback = [] {}; refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    // Actual writes have completed when the post-confirm authority sample fails.
    // Cleanup must retire RX, wipe, and never report local confirmation success.
    for (unsigned change = 0; change < 3; ++change) {
        Pair p; p.handshake(); const auto& o = offer(p.a); const auto before = p.a.source.samples;
        p.a.source.callback = [&] {
            if (p.a.source.samples == before + 2) {
                if (change == 0) p.a.source.value.now_ms = 1'000;
                if (change == 1) p.a.source.value.context.session_nonce++;
                if (change == 2) p.a.source.value.now_ms = 99;
            }
        };
        CHECK(!p.a.owner->confirm(o)); p.a.source.callback = [] {}; refused(p.a);
        CHECK(p.a_tx.mutations() > 0 && p.a_rx.mutations() > 0 && p.a.session.retired());
        const auto count = p.mutations(); CHECK(!p.a.owner->confirm(o)); CHECK(p.mutations() == count); ++groups;
    }
    for (auto fault : faults) for (bool receive : {false, true}) {
        Pair p; p.handshake(); const auto& o = offer(p.a); (receive ? p.a_rx : p.a_tx).arm(fault);
        CHECK(!p.a.owner->confirm(o)); refused(p.a);
        const auto count = p.mutations(); (receive ? p.a_rx : p.a_tx).clear();
        CHECK(!p.a.owner->confirm(o) && p.mutations() == count); cannot_reconstruct(p); ++groups;
    }
    {
        Pair p; p.handshake(); const auto& o = offer(p.a); p.a_store.arm(Fault::read_error);
        CHECK(!p.a.owner->confirm(o)); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    {
        Pair p; p.handshake(); const auto& o = offer(p.a); InvitationBootAuthority new_boot(p.boot_store); CHECK(new_boot.start());
        CHECK(!p.a.owner->confirm(o)); refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    {
        Pair p; p.handshake(); const auto& a = offer(p.a); const auto& b = offer(p.b);
        CHECK(p.a.owner->confirm(a)); p.b_tx.arm(Fault::write_after); CHECK(!p.b.owner->confirm(b)); refused(p.b);
        CHECK(p.a.owner->close()); CHECK(p.a.session.retired() && p.a.session.secrets_cleared()); ++groups;
    }
    {
        Pair p; p.begin(); std::array<unsigned char,128> bytes{}; bytes.fill(0xa5); std::size_t size = 999;
        const auto before = p.a.source.samples;
        p.a.source.callback = [&] { if (p.a.source.samples == before + 2) p.a.source.value.now_ms = 1'000; };
        CHECK(!p.a.owner->write(bytes.data(), bytes.size(), size)); CHECK(size == 0);
        p.a.source.callback = [] {}; refused(p.a); CHECK(p.mutations() == 0); ++groups;
    }
    {
        Pair p; p.handshake(); const auto& o = offer(p.a); CHECK(p.a.owner->confirm(o));
        p.a_rx.arm(Fault::sync_after); CHECK(!p.a.owner->close()); refused(p.a); ++groups;
    }
    std::cout << "PASS " << groups << " actual confirmation owner groups\n";
}
