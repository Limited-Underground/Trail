#include "opentrail/durable_group_roster.hpp"
#include "opentrail/group_qr_invite.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef OPENTRAIL_QR_REAL_CRYPTO
#include <sodium/crypto_core_ed25519.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_sign.h>
#endif

namespace {
namespace r = opentrail::identity::roster;
namespace qr = r::qr;
namespace p = opentrail::persistence;
using KvError = p::PersistentKvBackendError;
using RosterBytes = std::array<std::uint8_t, r::kMaximumRosterBytes>;
using qr::Kind;
using qr::Side;

void check(bool pass, const char* text, int line) {
    if (!pass) { std::cerr << "QR contract failed line " << line << ": " << text << '\n'; std::exit(1); }
}
#define CHECK(v) check((v), #v, __LINE__)

template<std::size_t N>
std::array<std::uint8_t, N> bytes(std::uint8_t marker) {
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<std::uint8_t>(marker + i * 17);
    return out;
}

#ifndef OPENTRAIL_QR_REAL_CRYPTO
// Test seam for the dependency-free Windows host matrix. These deterministic
// bytes are forgeable. The separate pinned-sodium run uses real SHA-256/Ed25519.
void fixture_bytes(const r::PublicKey& key, const std::uint8_t* p, std::size_t size,
                   std::uint8_t* output, std::size_t count) {
    for (std::size_t j = 0; j < count; ++j) {
        std::uint64_t lane = 14695981039346656037ULL ^ (j * 0x9e3779b97f4a7c15ULL);
        for (const auto key_byte : key) lane = (lane ^ key_byte) * 1099511628211ULL;
        for (std::size_t i = 0; i < size; ++i) lane = (lane ^ p[i]) * 1099511628211ULL;
        output[j] = static_cast<std::uint8_t>(lane >> (8 * (j % 8)));
    }
}
#endif

class Crypto final : public r::RosterCrypto {
public:
    bool verify(const r::Signature& sig, const std::uint8_t* p, std::size_t n,
                const r::PublicKey& key) const override {
#ifdef OPENTRAIL_QR_REAL_CRYPTO
        return crypto_core_ed25519_is_valid_point(key.data()) == 1 &&
               crypto_core_ed25519_is_valid_point(sig.data()) == 1 &&
               crypto_sign_verify_detached(sig.data(), p, n, key.data()) == 0;
#else
        r::Signature expected{};
        fixture_bytes(key, p, n, expected.data(), expected.size());
        return expected == sig;
#endif
    }
    bool digest(const std::uint8_t* p, std::size_t n, r::Digest& out) const override {
#ifdef OPENTRAIL_QR_REAL_CRYPTO
        return crypto_hash_sha256(out.data(), p, n) == 0;
#else
        fixture_bytes({}, p, n, out.data(), out.size());
        return true;
#endif
    }
    bool fingerprint(const r::PublicKey& key, r::Fingerprint& out) const override {
#ifdef OPENTRAIL_QR_REAL_CRYPTO
        if (crypto_core_ed25519_is_valid_point(key.data()) != 1) return false;
#else
        if (key == r::PublicKey{}) return false;
#endif
        out = key;
        return true;
    }
};

class Signer final : public r::RosterSigner {
public:
    explicit Signer(std::uint8_t tag) {
        const auto seed = bytes<32>(tag);
#ifdef OPENTRAIL_QR_REAL_CRYPTO
        CHECK(crypto_sign_seed_keypair(key_.data(), secret_.data(), seed.data()) == 0);
#else
        key_ = seed;
#endif
    }
    const r::PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* p, std::size_t n, r::Signature& sig) const override {
#ifdef OPENTRAIL_QR_REAL_CRYPTO
        return crypto_sign_detached(sig.data(), nullptr, p, n, secret_.data()) == 0;
#else
        fixture_bytes(key_, p, n, sig.data(), sig.size());
        return true;
#endif
    }
private:
    r::PublicKey key_{};
#ifdef OPENTRAIL_QR_REAL_CRYPTO
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret_{};
#endif
};

bool same(const qr::Decision& a, const qr::Decision& b) {
    return a.scope == b.scope && a.transaction == b.transaction && a.manifest == b.manifest &&
           a.side == b.side && a.kind == b.kind;
}
bool same_ticket(const qr::Descriptor& a, const qr::Descriptor& b) {
    return a.group == b.group && a.admin == b.admin && a.roster_version == b.roster_version &&
           a.roster_digest == b.roster_digest && a.invite_id == b.invite_id && a.rule == b.rule &&
           a.label == b.label && a.label_bytes == b.label_bytes;
}

// Keep the durable test seam separate from the synthetic QR receipt marker.
// Each namespace has an independent staged write and a committed memory image.
class MemoryKv final : public p::PersistentKvBackend {
public:
    explicit MemoryKv(std::string_view name) : name_(name) {}
    bool fail_after_apply{false};
    unsigned commits{};

    KvError read_blob(const char* partition, const char* ns, const char* key, std::uint8_t* out,
                      std::size_t capacity, std::size_t& size) override {
        CHECK(std::string_view(partition) == "ot_state" && ns == name_);
        const auto found = committed_.find(key);
        if (found == committed_.end()) return KvError::not_found;
        size = found->second.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(found->second.begin(), found->second.end(), out);
        return KvError::none;
    }

    KvError write_blob(const char* partition, const char* ns, const char* key,
                       const std::uint8_t* bytes, std::size_t size) override {
        CHECK(std::string_view(partition) == "ot_state" && ns == name_ && staged_.empty());
        staged_key_ = key;
        staged_.assign(bytes, bytes + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        CHECK(false);
        return KvError::io_failure;
    }

    KvError commit(const char* partition, const char* ns) override {
        CHECK(std::string_view(partition) == "ot_state" && ns == name_ && !staged_.empty());
        committed_[staged_key_] = std::move(staged_);
        staged_.clear();
        ++commits;
        return fail_after_apply ? KvError::io_failure : KvError::none;
    }

private:
    const std::string_view name_;
    std::map<std::string, std::vector<std::uint8_t>> committed_;
    std::string staged_key_;
    std::vector<std::uint8_t> staged_;
};

class Port final : public qr::Authority {
public:
    enum class Cut { none, before_marker, after_marker, late_marker, bad_readback, wrong_state };
    std::uint64_t tick{3000};
    Side local{};
    r::PublicKey peer{};
    qr::Descriptor issued{};
    qr::IssuedInvites local_issuer{};
    std::optional<qr::LocalInvites> injected_invites;
    const Crypto* crypto{};
    bool admit{true}, fingerprint_review{true}, transcript_review{true}, handshake_ok{true};
    Cut cut{Cut::none};
    std::optional<qr::Retained> durable;
    qr::Session* reentry{};
    bool rejected_reentry{};
    std::uint32_t epoch{0x01020304};
    std::array<std::uint8_t, r::kMaximumRosterBytes> signed_roster{};
    std::size_t signed_size{};
    r::DurableGroupRoster* successor_storage{};
    unsigned writes{};
    std::uint64_t cutoff{};
    std::uint64_t now() const override { return tick; }
    qr::LocalInvites issued_invites() const override {
        return injected_invites ? *injected_invites : local_issuer.view();
    }
    bool fresh_unclaimed(const qr::Nonce& id) const override { return !durable || durable->state.invite_id != id; }
    bool current(const qr::Descriptor& d, const r::PublicKey& candidate, std::uint32_t proposal) const override {
        return admit && same_ticket(issued, d) && proposal == epoch &&
               candidate == (local == Side::admin ? peer : joiner_key);
    }
    r::PublicKey joiner_key{};
    bool approve(qr::Review review, Side side, const qr::Descriptor& d,
                 const r::PublicKey& remote, const r::Digest& digest) const override {
        return side == local && remote == peer && same_ticket(issued, d) && digest != r::Digest{} &&
               (review == qr::Review::fingerprints ? fingerprint_review : transcript_review);
    }
    bool verified_handshake(const r::Digest& admission, const r::Digest& i,
                            const r::Digest& a, r::Digest& out) const override {
        std::array<std::uint8_t, 96> transcript{};
        std::copy(admission.begin(), admission.end(), transcript.begin());
        std::copy(i.begin(), i.end(), transcript.begin() + 32);
        std::copy(a.begin(), a.end(), transcript.begin() + 64);
        return handshake_ok && crypto->digest(transcript.data(), transcript.size(), out);
    }
    bool persist(const qr::Retained& d, std::uint64_t end) override {
        ++writes;
        if (reentry) {
            qr::RadioFrame output{};
            rejected_reentry = !reentry->receipt(d.decision.kind, output);
        }
        cutoff = end;
        if (cut == Cut::late_marker) tick = end;
        if (tick >= end || cut == Cut::before_marker ||
            (durable && durable->state.invite_id == d.state.invite_id &&
             (durable->decision.scope != d.decision.scope ||
              durable->decision.transaction != d.decision.transaction ||
              durable->decision.manifest != d.decision.manifest ||
              durable->local != d.local || durable->peer != d.peer))) return false;
        durable = d;
        if (cut == Cut::wrong_state) ++durable->state.epoch;
        return cut != Cut::after_marker;
    }
    bool readback(const qr::Retained& d) const override {
        r::Digest manifest{};
        return cut != Cut::bad_readback && durable.has_value() && same(d.decision, durable->decision) &&
               d.local == durable->local && d.peer == durable->peer &&
               qr::public_manifest(durable->state, durable->decision.scope, *crypto, manifest) &&
               manifest == d.decision.manifest;
    }
    bool successor_readback(const std::uint8_t* p, std::size_t size) const override {
        if (successor_storage) {
            RosterBytes readback{};
            const auto read_size = successor_storage->copy_signed_roster(readback.data(), readback.size());
            return read_size && size == read_size && std::equal(p, p + size, readback.begin());
        }
        return signed_size && size == signed_size && std::equal(p, p + size, signed_roster.begin());
    }
};

constexpr qr::Timing timing{1000, 2000};
const Crypto crypto;
const Signer admin(21), joiner(81), outsider(137);
const qr::Fresh jf{bytes<32>(1), bytes<32>(2), bytes<16>(3)};
const qr::Fresh af{bytes<32>(4), bytes<32>(5), bytes<16>(6)};

struct Pair {
    r::Roster current{};
    qr::Descriptor issued{};
    qr::DescriptorBytes descriptor{};
    qr::RadioFrame start{}, reply{};
    Port a{}, j{};
    qr::Session a_session{Side::admin, timing, crypto, admin, a};
    qr::Session j_session{Side::joiner, timing, crypto, joiner, j};

    Pair() {
        CHECK(r::create_first(0x1020304050607080ULL, admin.public_key(), "Admin", r::LocationRule::required, current));
        CHECK(r::sign(current, admin));
        issued.admin = admin.public_key();
        issued.group = current.group_id;
        issued.roster_version = current.version;
        CHECK(r::roster_digest(current, crypto, issued.roster_digest));
        issued.invite_id = bytes<16>(71);
        issued.rule = current.location_rule;
        constexpr std::string_view name = "Ridge Team";
        std::copy(name.begin(), name.end(), issued.label.begin());
        issued.label_bytes = static_cast<std::uint8_t>(name.size());
        CHECK(a.local_issuer.issue(issued, timing, timing.invitation_issued, crypto, admin, descriptor));
        qr::Start first{{}, 0x01020304, joiner.public_key(), jf};
        CHECK(qr::scope_digest(descriptor, joiner.public_key(), crypto, first.scope));
        CHECK(qr::sign_start(first, crypto, joiner, start));
        CHECK(qr::sign_reply(start, af, crypto, admin, reply));
        a.local = Side::admin; a.peer = joiner.public_key(); a.issued = issued; a.crypto = &crypto;
        j.local = Side::joiner; j.peer = admin.public_key(); j.issued = issued; j.crypto = &crypto;
        j.joiner_key = joiner.public_key();
    }
    void begin() {
        CHECK(a_session.begin(descriptor, start, reply));
        CHECK(j_session.begin(descriptor, start, reply));
    }
    void through_handshake() {
        begin();
        qr::RadioFrame av{}, jv{};
        CHECK(a_session.admission(av) && j_session.admission(jv));
        CHECK(av.size == qr::kBindingBytes && jv.size == qr::kBindingBytes);
        CHECK(a_session.accept_admission(jv) && j_session.accept_admission(av));
        CHECK(a_session.enter_transcript() && j_session.enter_transcript());
    }
    void through_confirmation() {
        through_handshake();
        qr::RadioFrame av{}, jv{};
        CHECK(a_session.confirmation(av) && j_session.confirmation(jv));
        CHECK(a_session.accept_confirmation(jv) && j_session.accept_confirmation(av));
    }
    void through_prepare() {
        through_confirmation();
        qr::RadioFrame av{}, jv{};
        CHECK(a_session.receipt(Kind::prepared, av));
        CHECK(j_session.receipt(Kind::prepared, jv));
        CHECK(a_session.accept_receipt(jv) && j_session.accept_receipt(av));
    }
    void through_commit() {
        through_prepare();
        qr::RadioFrame av{}, jv{};
        CHECK(a_session.receipt(Kind::committed, av));
        CHECK(j_session.receipt(Kind::committed, jv));
        CHECK(a_session.accept_receipt(jv) && j_session.accept_receipt(av));
    }
};

void layouts() {
    Pair pair;
    const auto* q = pair.descriptor.bytes.data();
    CHECK(std::equal(q, q + 4, std::string_view("OTGQ").begin()));
    CHECK(pair.descriptor.size == qr::kDescriptorBaseBytes + pair.issued.label_bytes);
    CHECK(q[4] == 1 && q[5] == 1 && q[6] == 2 && q[7] == 0);
    CHECK(std::equal(q + 8, q + 40, admin.public_key().begin()));
    CHECK(q[40] == 0x10 && q[47] == 0x80 && q[51] == 1);
    CHECK(q[100] == 2 && q[101] == 1 && q[102] == pair.issued.label_bytes);
    qr::Descriptor decoded{};
    CHECK(qr::verify_descriptor(pair.descriptor, crypto, decoded) && same_ticket(decoded, pair.issued));
    const auto* s = pair.start.bytes.data();
    CHECK(std::equal(s, s + 4, std::string_view("OTGJ").begin()));
    CHECK(pair.start.size == 220 && s[4] == 1 && s[5] == 1 && s[6] == 1 && s[7] == 0);
    CHECK(s[40] == 1 && s[41] == 2 && s[42] == 3 && s[43] == 4);
    CHECK(std::equal(s + 44, s + 76, joiner.public_key().begin()));
    CHECK(std::equal(s + 76, s + 108, jf.ephemeral.begin()));
    CHECK(std::equal(s + 108, s + 140, jf.challenge.begin()));
    CHECK(std::equal(s + 140, s + 156, jf.boot.begin()));
    const auto* a = pair.reply.bytes.data();
    CHECK(pair.reply.size == 184 && a[5] == 2 && a[6] == 2 && a[7] == 0);
    CHECK(std::equal(a + 40, a + 72, af.ephemeral.begin()));
    CHECK(std::equal(a + 72, a + 104, af.challenge.begin()));
    CHECK(std::equal(a + 104, a + 120, af.boot.begin()));
    CHECK(pair.start.size <= qr::kRadioLimit && pair.reply.size <= qr::kRadioLimit);
}

void strict_frames_and_scope() {
    Pair pair;
    for (std::size_t i = 0; i < pair.descriptor.size; ++i) {
        auto changed = pair.descriptor;
        changed.bytes[i] ^= 1;
        qr::Descriptor out = pair.issued;
        CHECK(!qr::verify_descriptor(changed, crypto, out) && same_ticket(out, pair.issued));
    }
    for (std::size_t i = 0; i < pair.start.size; ++i) {
        auto changed = pair.start;
        changed.bytes[i] ^= 1;
        qr::Start out{};
        CHECK(!qr::verify_start(changed, crypto, out) && out.epoch == 0);
    }
    for (std::size_t i = 0; i < pair.reply.size; ++i) {
        auto changed = pair.reply;
        changed.bytes[i] ^= 1;
        qr::Reply out{};
        CHECK(!qr::verify_reply(changed, admin.public_key(), crypto, out) && out.start_hash == r::Digest{});
    }
    qr::Start unused{};
    auto overlength = pair.start;
    overlength.size = 236;
    CHECK(!qr::verify_start(overlength, crypto, unused));
    overlength.size = 237;
    CHECK(!qr::verify_start(overlength, crypto, unused));
    auto malformed = pair.issued;
    malformed.label = {};
    malformed.label[0] = 0xc0; malformed.label[1] = 0xaf; malformed.label_bytes = 2;
    qr::DescriptorBytes ignored{};
    CHECK(!qr::issue_descriptor(malformed, crypto, admin, ignored));
    malformed = pair.issued;
    malformed.roster_version = std::numeric_limits<std::uint32_t>::max();
    CHECK(!qr::issue_descriptor(malformed, crypto, admin, ignored));

    qr::RadioFrame reflected{};
    CHECK(!qr::sign_reply(pair.start, jf, crypto, admin, reflected));
    CHECK(!qr::sign_reply(pair.start, af, crypto, joiner, reflected));
    CHECK(qr::sign_reply(pair.start, af, crypto, outsider, reflected));
    CHECK(!pair.j_session.begin(pair.descriptor, pair.start, reflected));

    auto altered = pair.issued;
    altered.rule = r::LocationRule::off;
    CHECK(qr::issue_descriptor(altered, crypto, admin, ignored));
    CHECK(!pair.a_session.begin(ignored, pair.start, pair.reply));
    altered = pair.issued;
    altered.admin = outsider.public_key();
    CHECK(qr::issue_descriptor(altered, crypto, outsider, ignored));
    CHECK(!pair.j_session.begin(ignored, pair.start, pair.reply));
}

void matching_only_locally_issued_live_ticket() {
    Pair pair;
    std::array<qr::LocalInvite, qr::kInvites> invitations{};
    std::size_t slot = 99;
    CHECK(!qr::resolve_start(pair.start, invitations, admin.public_key(), 3000, crypto, slot) && slot == 99);
    invitations[2] = {&pair.descriptor, timing};
    CHECK(qr::resolve_start(pair.start, invitations, admin.public_key(), 3000, crypto, slot) && slot == 2);
    CHECK(!qr::resolve_start(pair.start, invitations, outsider.public_key(), 3000, crypto, slot));
    CHECK(!qr::resolve_start(pair.start, invitations, admin.public_key(), 182000, crypto, slot));
    invitations[1] = invitations[2];
    CHECK(!qr::resolve_start(pair.start, invitations, admin.public_key(), 3000, crypto, slot));
    pair.a.injected_invites = invitations;
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply));
    pair.a.injected_invites = qr::LocalInvites{};
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply));
    auto wrong_timing = pair.a.local_issuer.view();
    wrong_timing[0].timing.invitation_issued += 1;
    pair.a.injected_invites = wrong_timing;
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply));
    pair.a.injected_invites.reset();

    qr::DescriptorBytes duplicate{};
    auto other_ticket = pair.issued;
    other_ticket.invite_id[0] ^= 1;
    CHECK(!pair.a.local_issuer.issue(other_ticket, {1000, 3000}, 3000, crypto, admin, duplicate));
    CHECK(qr::issue_descriptor(other_ticket, crypto, admin, duplicate));
    invitations = pair.a.local_issuer.view();
    invitations[1] = {&duplicate, {1000, 3000}};
    CHECK(!qr::resolve_start(pair.start, invitations, admin.public_key(), 3000, crypto, slot));
    pair.a.injected_invites = invitations;
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply));
    pair.a.injected_invites.reset();

    CHECK(pair.a.local_issuer.revoke(pair.descriptor));
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply));
    CHECK(pair.a.local_issuer.issue(other_ticket, {1000, 3000}, 3000, crypto, admin, duplicate));
    CHECK(!pair.a_session.begin(pair.descriptor, pair.start, pair.reply) && pair.a.writes == 0);

    Pair stale;
    stale.a.tick = 182000;
    CHECK(!stale.a_session.begin(stale.descriptor, stale.start, stale.reply));
}

void both_independent_board_confirmations() {
    Pair pair;
    pair.begin();
    qr::RadioFrame jv{}, av{}, missing{};
    pair.a.fingerprint_review = false;
    CHECK(pair.j_session.admission(jv) && !pair.a_session.admission(av));
    CHECK(pair.a_session.accept_admission(jv));
    CHECK(!pair.j_session.enter_transcript() && !pair.a_session.enter_transcript());
    pair.a.fingerprint_review = true;
    CHECK(pair.a_session.admission(av) && pair.j_session.accept_admission(av));
    pair.a.handshake_ok = false;
    CHECK(!pair.a_session.enter_transcript());
    pair.a.handshake_ok = true;
    CHECK(pair.a_session.enter_transcript() && pair.j_session.enter_transcript());
    CHECK(!pair.a_session.accept_confirmation(jv));  // old fingerprint approval is another domain
    pair.j.transcript_review = false;
    CHECK(pair.a_session.confirmation(av) && !pair.j_session.confirmation(jv));
    CHECK(!pair.a_session.receipt(Kind::prepared, missing) && !pair.j_session.receipt(Kind::prepared, missing));
    pair.j.transcript_review = true;
    CHECK(pair.j_session.confirmation(jv) && pair.j_session.accept_confirmation(av));
    CHECK(pair.a_session.accept_confirmation(jv));
    CHECK(!pair.a_session.accept_confirmation(av));  // its own board event cannot stand in for the other board
    CHECK(pair.a_session.receipt(Kind::prepared, av) && pair.j_session.receipt(Kind::prepared, jv));
    CHECK(av.size == 168 && jv.size == 168 && pair.a.writes == 1 && pair.j.writes == 1);
    qr::Decision decoded{};
    CHECK(qr::verify_receipt(av, admin.public_key(), crypto, decoded) && same(decoded, pair.a.durable->decision));
    CHECK(!qr::verify_receipt(av, joiner.public_key(), crypto, decoded));
    for (std::size_t i = 0; i < av.size; ++i) {
        auto changed = av;
        changed.bytes[i] ^= 1;
        CHECK(!qr::verify_receipt(changed, admin.public_key(), crypto, decoded));
    }
    CHECK(pair.a_session.receipt(Kind::prepared, missing) && missing.bytes == av.bytes && pair.a.writes == 1);
    CHECK(pair.j_session.accept_receipt(av) && pair.j_session.accept_receipt(av));
    CHECK(!pair.a_session.receipt(Kind::committed, missing));  // peer Prepared has not arrived
}

void original_deadlines_and_copy_budgets() {
    std::uint64_t value = 0;
    CHECK(timing.deadline(value) && value == 182000);
    CHECK((qr::Timing{1000, 280000}.deadline(value) && value == 301000));
    CHECK((!qr::Timing{1000, 999}.deadline(value)));
    CHECK((!qr::Timing{std::numeric_limits<std::uint64_t>::max() - 1,
                       std::numeric_limits<std::uint64_t>::max()}.deadline(value)));
    Pair late;
    late.through_handshake();
    late.a.tick = 167001;
    qr::RadioFrame out{};
    CHECK(!late.a_session.confirmation(out) && !out.size);
    Pair pair;
    pair.through_handshake();
    pair.a.tick = pair.j.tick = 167000;
    qr::RadioFrame ar{}, jr{};
    CHECK(pair.a_session.confirmation(ar) && pair.j_session.confirmation(jr));
    CHECK(pair.a_session.accept_confirmation(jr) && pair.j_session.accept_confirmation(ar));
    pair.a.tick = 172000;
    CHECK(pair.a_session.receipt(Kind::prepared, ar) && pair.a.cutoff == 182000);
    pair.j.tick = 172001;
    CHECK(!pair.j_session.receipt(Kind::prepared, jr) && pair.j.writes == 0);
    pair.a.tick = 171999;
    CHECK(!pair.a_session.receipt(Kind::prepared, ar));
    pair.a.tick = 172000;
    CHECK(!pair.a_session.receipt(Kind::prepared, ar));

    qr::Copies exact;
    CHECK(!exact.accepted(ar, timing, 175500));
    CHECK(exact.offer(ar, timing, 175500));
    CHECK(exact.offer(ar, timing, 175500));  // a queued or refunded frame spends nothing
    auto changed = ar; changed.bytes[90] ^= 1;
    CHECK(!exact.accepted(changed, timing, 175500));
    CHECK(exact.accepted(ar, timing, 175500));
    CHECK(exact.offer(ar, timing, 179000) && exact.accepted(ar, timing, 179000));
    CHECK(!exact.offer(ar, timing, 179001));
    qr::Copies capped;
    for (unsigned i = 0; i < 4; ++i) {
        const auto now = 3000 + 3000 * i;
        CHECK(capped.offer(ar, timing, now) && capped.accepted(ar, timing, now));
    }
    CHECK(!capped.offer(ar, timing, 30000));
    qr::Copies original;
    CHECK(original.offer(ar, timing, 175500));
    CHECK(!original.accepted(ar, timing, 175501));
    CHECK(!original.offer(ar, {1000, 3000}, 175501));  // no expiry reload
}

void roster_add_only_after_exact_commit_and_readback() {
    Pair pair;
    r::Roster next{};
    pair.through_prepare();
    CHECK(!pair.a_session.successor(pair.current, "New", next));
    qr::RadioFrame ar{}, jr{};
    CHECK(pair.a_session.receipt(Kind::committed, ar) && pair.j_session.receipt(Kind::committed, jr));
    CHECK(pair.j_session.accept_receipt(ar));
    CHECK(!pair.a_session.successor(pair.current, "New", next));  // missing authenticated peer Committed
    CHECK(pair.a_session.accept_receipt(jr));
    CHECK(!pair.j_session.successor(pair.current, "New", next));  // member cannot sign an admin proposal
    auto stale = pair.current;
    CHECK(r::change_location_rule(pair.current, crypto, r::LocationRule::preferred, stale));
    CHECK(r::sign(stale, admin));
    CHECK(!pair.a_session.successor(stale, "New", next));
    CHECK(pair.a_session.successor(pair.current, "New", next));
    CHECK(!pair.a_session.successor(pair.current, "Different", next));  // never two bodies for N+1
    CHECK(next.version == 2 && next.member_count == 2 && next.members[1].key == joiner.public_key());
    CHECK(r::sign(next, admin));
    auto& volume = pair.a.signed_roster;
    const auto written = r::encode(next, volume.data(), volume.size());
    CHECK(written != 0 && !pair.a_session.membership(pair.current, "New", volume.data(), written));
    pair.a.signed_size = written;
    auto invalid = volume; invalid[written - 1] ^= 0x40;
    CHECK(!pair.a_session.membership(pair.current, "New", invalid.data(), written));
    CHECK(!pair.a_session.membership(pair.current, "Different", volume.data(), written));
    CHECK(pair.a_session.membership(pair.current, "New", volume.data(), written));
    CHECK(!pair.a_session.membership(pair.current, "New", volume.data(), written));
    CHECK(!pair.a_session.successor(pair.current, "New", next));
}

std::vector<std::uint8_t> signed_bytes(r::Roster& roster) {
    RosterBytes bytes{};
    CHECK(r::sign(roster, admin));
    const auto size = r::encode(roster, bytes.data(), bytes.size());
    CHECK(size != 0);
    return {bytes.begin(), bytes.begin() + size};
}

void expect_snapshot(r::DurableGroupRoster& owner, const r::Roster& expected,
                     const std::vector<std::uint8_t>& wire, const r::Fingerprint& local) {
    r::DurableRosterSnapshot snapshot{};
    RosterBytes actual{};
    r::Digest digest{};
    CHECK(r::roster_digest(expected, crypto, digest));
    CHECK(owner.snapshot(snapshot, actual.data(), actual.size()) == r::DurableRosterError::none);
    CHECK(snapshot.state == r::State::active && snapshot.group.admin == local &&
          snapshot.group.group_id == expected.group_id && snapshot.version == expected.version &&
          snapshot.digest == digest && snapshot.bytes == wire.size());
    CHECK(std::equal(wire.begin(), wire.end(), actual.begin()));
}

void qr_successor_through_durable_roster() {
    using Error = r::DurableRosterError;
    for (const bool applied_unconfirmed : {false, true}) {
        Pair pair;
        pair.through_commit();
        r::Roster next{}, other_first{}, other_current{};
        CHECK(pair.a_session.successor(pair.current, "New", next));
        const auto initial = signed_bytes(pair.current);
        const auto successor = signed_bytes(next);
        // Same author and local board, different qualified group. Preserve an
        // existing mapping as well as that group's exact signed bytes.
        CHECK(r::create_first(pair.current.group_id + 1, admin.public_key(), "Admin",
                              r::LocationRule::off, other_first));
        const auto other_initial = signed_bytes(other_first);
        CHECK(r::add_member(other_first, crypto, outsider.public_key(), "Existing", other_current));
        const auto other_wire = signed_bytes(other_current);
        r::Fingerprint local{}, peer{}, applicant{};
        CHECK(crypto.fingerprint(admin.public_key(), local));
        CHECK(crypto.fingerprint(outsider.public_key(), peer));
        CHECK(crypto.fingerprint(joiner.public_key(), applicant));
        MemoryKv roster_kv("ot_group_rost"), index_kv("ot_group_idx");
        std::optional<r::DurablePairScope> prior_mapping;
        {
            p::GroupPairIndex index(index_kv, local);
            CHECK(index.initialize() == p::GroupPairIndexError::none);
            r::DurableGroupRoster target(roster_kv, index, crypto, admin.public_key(),
                                        pair.current.group_id, admin.public_key());
            r::DurableGroupRoster other(roster_kv, index, crypto, admin.public_key(),
                                       other_first.group_id, admin.public_key());
            CHECK(target.create_owned(initial.data(), initial.size()) == Error::none);
            CHECK(other.create_owned(other_initial.data(), other_initial.size()) == Error::none);
            const auto installed = other.offer(other_wire.data(), other_wire.size());
            CHECK(installed.error == Error::none && installed.outcome == r::Outcome::accepted);
            CHECK(other.accept_member(outsider.public_key()) == Error::none);
            prior_mapping = other.reserve_pair(peer);
            CHECK(prior_mapping.has_value());

            pair.a.successor_storage = &target;
            const auto roster_commits = roster_kv.commits;
            const auto index_commits = index_kv.commits;
            CHECK(!pair.a_session.membership(pair.current, "New", successor.data(), successor.size()));
            const auto wrong = target.offer(other_wire.data(), other_wire.size());
            CHECK(wrong.error == Error::none && wrong.outcome == r::Outcome::rejected_group);
            CHECK(roster_kv.commits == roster_commits && index_kv.commits == index_commits);
            expect_snapshot(target, pair.current, initial, local);

            roster_kv.fail_after_apply = applied_unconfirmed;
            const auto result = target.offer(successor.data(), successor.size());
            roster_kv.fail_after_apply = false;
            CHECK(roster_kv.commits == roster_commits + 1);
            CHECK(result.error == (applied_unconfirmed ? Error::storage_failure : Error::none));
            if (!applied_unconfirmed) CHECK(result.outcome == r::Outcome::accepted);

            // Even a matching successor sent to the QR session must read back
            // from its own scope, and an uncertain owner returns no authority.
            pair.a.successor_storage = &other;
            CHECK(!pair.a_session.membership(pair.current, "New", successor.data(), successor.size()));
            pair.a.successor_storage = &target;
            CHECK(pair.a_session.membership(pair.current, "New", successor.data(), successor.size()) ==
                  !applied_unconfirmed);
            CHECK(!pair.a_session.membership(pair.current, "New", successor.data(), successor.size()));
            pair.a.successor_storage = nullptr;
        }
        // Reconcile the applied image with fresh owners. This verifies roster
        // recovery without resuming a QR session or granting pair authority.
        p::GroupPairIndex reopened_index(index_kv, local);
        r::DurableGroupRoster reopened(roster_kv, reopened_index, crypto, admin.public_key(),
                                      pair.current.group_id, admin.public_key());
        CHECK(reopened.reopen() == Error::none);
        expect_snapshot(reopened, next, successor, local);
        CHECK(!reopened.find_pair(applicant));

        r::DurableGroupRoster unaffected(roster_kv, reopened_index, crypto, admin.public_key(),
                                        other_current.group_id, admin.public_key());
        CHECK(unaffected.reopen() == Error::none);
        expect_snapshot(unaffected, other_current, other_wire, local);
        const auto mapping = unaffected.find_pair(peer);
        CHECK(mapping.has_value() && mapping->location.slot == prior_mapping->location.slot &&
              mapping->location.incarnation == prior_mapping->location.incarnation &&
              mapping->roster_version == prior_mapping->roster_version &&
              mapping->roster_digest == prior_mapping->roster_digest);
    }
}

void cuts_retain_only_exact_authority() {
    for (const auto cut : {Port::Cut::before_marker, Port::Cut::after_marker, Port::Cut::late_marker,
                           Port::Cut::bad_readback, Port::Cut::wrong_state}) {
        Pair pair;
        pair.through_confirmation();
        pair.a.cut = cut;
        qr::RadioFrame out{};
        CHECK(!pair.a_session.receipt(Kind::prepared, out) && out.size == 0);
        CHECK(!pair.a_session.receipt(Kind::prepared, out) && pair.a.writes == 1);
        const bool retained = cut == Port::Cut::after_marker || cut == Port::Cut::bad_readback ||
                              cut == Port::Cut::wrong_state;
        CHECK(pair.a.durable.has_value() == retained);
        CHECK(pair.j.writes == 0);
    }
    Pair nested;
    nested.through_confirmation();
    nested.a.reentry = &nested.a_session;
    qr::RadioFrame refused{};
    CHECK(!nested.a_session.receipt(Kind::prepared, refused) && nested.a.rejected_reentry &&
          !refused.size && nested.a.writes == 1);
    Pair aborted;
    aborted.through_prepare();
    qr::RadioFrame out{};
    CHECK(aborted.j_session.receipt(Kind::aborted, out));
    CHECK(aborted.a_session.accept_receipt(out));
    CHECK(!aborted.a_session.receipt(Kind::committed, out));

    Pair pair;
    pair.through_prepare();
    pair.a.cut = Port::Cut::after_marker;
    CHECK(!pair.a_session.receipt(Kind::committed, out));
    CHECK(pair.a.durable->decision.kind == Kind::committed && pair.j.durable->decision.kind == Kind::prepared);
    pair.a.cut = Port::Cut::none;
    pair.a.admit = false;
    pair.j.admit = false;
    qr::Session rebooted(Side::joiner, timing, crypto, joiner, pair.j);
    CHECK(!rebooted.begin(pair.descriptor, pair.start, pair.reply));

    const auto challenge_nonce = bytes<16>(85);
    const auto fresh_nonce = bytes<16>(167);
    qr::RecoveryProbe reader(*pair.j.durable, crypto, joiner, pair.j);
    qr::RadioFrame challenge{}, response{};
    CHECK(reader.challenge(challenge_nonce, challenge) && challenge.size == 152);
    CHECK(!reader.challenge(challenge_nonce, out));
    CHECK(!qr::recovery_reply(*pair.a.durable, challenge, challenge_nonce,
                              crypto, admin, pair.a, response));
    CHECK(qr::recovery_reply(*pair.a.durable, challenge, fresh_nonce,
                             crypto, admin, pair.a, response) && response.size == 200);
    auto altered = response;
    altered.bytes[105] ^= 2;
    Kind observed{};
    CHECK(!reader.observe(altered, observed));
    CHECK(reader.observe(response, observed) && observed == Kind::committed);
    CHECK(!reader.observe(response, observed));
    qr::RecoveryProbe another(*pair.j.durable, crypto, joiner, pair.j);
    CHECK(another.challenge(bytes<16>(89), out) && !another.observe(response, observed));
    CHECK(pair.a.durable->state.invite_id == pair.issued.invite_id);
    r::Digest foreign_scope{};
    CHECK(qr::scope_digest(pair.descriptor, outsider.public_key(), crypto, foreign_scope));
    CHECK(foreign_scope != pair.a.durable->decision.scope);
    qr::Start foreign{foreign_scope, 0x01020304, outsider.public_key(), jf};
    qr::RadioFrame foreign_start{}, foreign_reply{};
    CHECK(qr::sign_start(foreign, crypto, outsider, foreign_start));
    CHECK(qr::sign_reply(foreign_start, af, crypto, admin, foreign_reply));
    std::size_t local_match = 99;
    CHECK(qr::resolve_start(foreign_start, pair.a.local_issuer.view(), admin.public_key(),
                            3000, crypto, local_match));
    pair.a.admit = true;
    pair.a.peer = outsider.public_key();  // Valid new candidate, same consumed ticket.
    qr::Session replay(Side::admin, timing, crypto, admin, pair.a);
    const auto writes = pair.a.writes;
    CHECK(!replay.begin(pair.descriptor, foreign_start, foreign_reply) && pair.a.writes == writes);
    auto changed_scope = *pair.a.durable;
    changed_scope.decision.scope = foreign_scope;
    CHECK(!pair.a.persist(changed_scope, 182000));  // Even a concurrent marker is rejected atomically.
    CHECK(pair.a.durable->state.invite_id == pair.issued.invite_id &&
          pair.a.durable->decision.scope != foreign_scope);
    auto invented = *pair.j.durable; invented.decision.transaction[0] ^= 1;
    qr::RecoveryProbe false_probe(invented, crypto, joiner, pair.j);
    CHECK(!false_probe.challenge(challenge_nonce, out));
    invented = *pair.j.durable;
    invented.peer = outsider.public_key();
    qr::RecoveryProbe false_identity(invented, crypto, joiner, pair.j);
    CHECK(!false_identity.challenge(challenge_nonce, out));
    r::Roster next{};
    CHECK(!rebooted.successor(pair.current, "New", next));
}

}  // namespace

int main() {
    layouts();
    strict_frames_and_scope();
    matching_only_locally_issued_live_ticket();
    both_independent_board_confirmations();
    original_deadlines_and_copy_budgets();
    roster_add_only_after_exact_commit_and_readback();
    qr_successor_through_durable_roster();
    cuts_retain_only_exact_authority();
#ifdef OPENTRAIL_QR_REAL_CRYPTO
    std::cout << "PASS 8 QR invitation contract groups (pinned Ed25519/SHA-256)\n";
#else
    std::cout << "PASS 8 QR invitation contract groups (deterministic crypto seam)\n";
#endif
}
