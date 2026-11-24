// F8 step 2c: one QR group join between two boards over a modeled radio link.
// Real libsodium (pinned 1.0.22) through tests/host/f5_rekey_status_ci.py --suite qr-join.
#include "opentrail/group_qr_join_conversation.hpp"

#include <sodium/crypto_core_ed25519.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_sign.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
using opentrail::security_evaluation::GroupQrJoinConversation;
using opentrail::security_evaluation::GroupQrJoinIdentity;
using opentrail::security_evaluation::GroupQrNoiseBinding;
namespace join = opentrail::security_evaluation::qr_join;
namespace r = opentrail::identity::roster;
namespace qr = r::qr;
namespace sec = opentrail::security;
using Side = qr::Side;
using Kind = qr::Kind;
using Phase = GroupQrJoinConversation::Phase;

void check(bool value, const char* expression, int line) {
    if (value) return;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

template<std::size_t N>
std::array<std::uint8_t, N> bytes(std::uint8_t marker) {
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<std::uint8_t>(marker + i * 17 + 1);
    return out;
}

class Crypto final : public r::RosterCrypto {
public:
    bool verify(const r::Signature& sig, const std::uint8_t* p, std::size_t n, const r::PublicKey& key) const override {
        return crypto_core_ed25519_is_valid_point(key.data()) == 1 &&
               crypto_sign_verify_detached(sig.data(), p, n, key.data()) == 0;
    }
    bool digest(const std::uint8_t* p, std::size_t n, r::Digest& out) const override {
        return crypto_hash_sha256(out.data(), p, n) == 0;
    }
    bool fingerprint(const r::PublicKey& key, r::Fingerprint& out) const override {
        if (crypto_core_ed25519_is_valid_point(key.data()) != 1) return false;
        out = key;
        return true;
    }
};
const Crypto crypto;

// One board identity: signs QR frames and begins Noise with its derived static.
class Identity final : public r::RosterSigner, public GroupQrJoinIdentity {
public:
    explicit Identity(std::uint8_t tag) {
        const auto seed = bytes<32>(tag);
        CHECK(crypto_sign_seed_keypair(key_.data(), secret_.data(), seed.data()) == 0);
    }
    const r::PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* p, std::size_t n, r::Signature& sig) const override {
        // During a join the board signer is asked only for domain-labeled QR frames.
        CHECK(!qr_only || (n > 13 && std::string_view(reinterpret_cast<const char*>(p), 13) == "OpenTrail QR "));
        return crypto_sign_detached(sig.data(), nullptr, p, n, secret_.data()) == 0;
    }
    bool begin_noise(GroupQrNoiseBinding& binding, Side side, const r::PublicKey& peer,
                     const ot_noise_xk_keypair& ephemeral, const r::Digest& scope) override {
        ++noise_begins;
        ot_noise_xk_keypair local{};
        const bool ok = GroupQrNoiseBinding::x25519_static(secret_, local) &&
                        binding.begin(side, local, peer, ephemeral, scope);
        sodium_memzero(&local, sizeof local);
        return ok;
    }
    unsigned noise_begins{};
    bool qr_only{false};
private:
    r::PublicKey key_{};
    std::array<std::uint8_t, 64> secret_{};
};

// Distinct deterministic bytes; the pinned recipe builds no system RNG.
class Random final : public sec::SecureRandomSource {
public:
    explicit Random(std::uint8_t lane) : lane_(lane) {}
    sec::EntropyState state() const override { return sec::EntropyState::ready; }
    sec::RandomFillResult fill(std::uint8_t* out, std::size_t size) override {
        if (broken) return {sec::RandomFillError::entropy_failed, 0};
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>((counter_++ * 131u) ^ (lane_ * 29u) ^ (i * 7u) ^ 0x5au);
        return {sec::RandomFillError::none, size};
    }
    bool broken{false};
private:
    std::uint8_t lane_;
    unsigned counter_{1};
};

bool same_decision(const qr::Decision& a, const qr::Decision& b) {
    return a.scope == b.scope && a.transaction == b.transaction && a.manifest == b.manifest &&
           a.side == b.side && a.kind == b.kind;
}

// The board's trusted local ports, as a host model.
class Port final : public qr::Authority {
public:
    Port(const std::uint64_t& clock, Side side) : clock_(clock), side_(side) {}
    std::uint64_t now() const override { return clock_; }
    qr::LocalInvites issued_invites() const override { return issuer.view(); }
    bool fresh_unclaimed(const qr::Nonce& id) const override { return !durable || durable->state.invite_id != id; }
    bool current(const qr::Descriptor& d, const r::PublicKey& joiner, std::uint32_t proposal) const override {
        return admit && d.group == group && proposal == epoch && joiner == expected_joiner;
    }
    bool approve(qr::Review review, Side side, const qr::Descriptor& d, const r::PublicKey& remote,
                 const r::Digest& digest) const override {
        ++(review == qr::Review::fingerprints ? fingerprint_asks : transcript_asks);
        return side == side_ && d.group == group && remote == expected_peer && digest != r::Digest{} &&
               (review == qr::Review::fingerprints ? fingerprints_ok : transcript_ok);
    }
    bool verified_handshake(const r::Digest&, const r::Digest&, const r::Digest&, r::Digest&) const override {
        return false;  // The conversation supplies the Noise result.
    }
    bool persist(const qr::Retained& value, std::uint64_t deadline) override {
        if (clock_ >= deadline && value.decision.kind != Kind::aborted) return false;
        durable = value;
        kinds.push_back(value.decision.kind);
        return true;
    }
    bool readback(const qr::Retained& value) const override {
        return durable && same_decision(durable->decision, value.decision) && durable->local == value.local &&
               durable->peer == value.peer;
    }
    bool successor_readback(const std::uint8_t*, std::size_t) const override { return false; }

    qr::IssuedInvites issuer{};
    std::uint64_t group{};
    std::uint32_t epoch{7};
    r::PublicKey expected_joiner{}, expected_peer{};
    bool admit{true}, fingerprints_ok{true}, transcript_ok{true};
    mutable unsigned fingerprint_asks{}, transcript_asks{};
    std::optional<qr::Retained> durable;
    std::vector<Kind> kinds;
private:
    const std::uint64_t& clock_;
    Side side_;
};

const Identity outsider_id(137);

// A frame label for loss rules: QR kind, or 100 + Noise stage.
int label(const qr::RadioFrame& f) {
    return join::is_qr_frame(f.bytes.data(), f.size) ? f.bytes[5] : 100 + f.bytes[5];
}

struct Link {
    std::uint64_t clock{10000};
    Port a{clock, Side::admin}, j{clock, Side::joiner};
    Identity admin{21}, joiner{81};
    Random ar{1}, jr{2};
    GroupQrJoinConversation admin_side{Side::admin, crypto, admin, a, admin, ar, bytes<16>(3)};
    GroupQrJoinConversation joiner_side{Side::joiner, crypto, joiner, j, joiner, jr, bytes<16>(4)};
    qr::DescriptorBytes invite{};
    qr::Timing admin_timing{}, joiner_timing{};
    // (sender, label) -> copies seen; deliver() decides per copy.
    std::map<std::pair<Side, int>, unsigned> seen;
    std::function<bool(Side, const qr::RadioFrame&, unsigned)> deliver = [](Side, const qr::RadioFrame&, unsigned) {
        return true;
    };
    std::vector<std::pair<Side, int>> log;
    std::size_t largest{};

    Link() {
        r::Roster current{};
        CHECK(r::create_first(0x1020304050607080ULL, admin.public_key(), "Admin", r::LocationRule::required, current));
        CHECK(r::sign(current, admin));
        qr::Descriptor d{};
        d.admin = admin.public_key();
        d.group = current.group_id;
        d.roster_version = current.version;
        CHECK(r::roster_digest(current, crypto, d.roster_digest));
        d.invite_id = bytes<16>(71);
        d.rule = current.location_rule;
        constexpr std::string_view name = "Ridge Team";
        std::copy(name.begin(), name.end(), d.label.begin());
        d.label_bytes = static_cast<std::uint8_t>(name.size());
        admin_timing = {clock, clock};
        CHECK(a.issuer.issue(d, admin_timing, clock, crypto, admin, invite));
        a.group = j.group = d.group;
        a.expected_joiner = j.expected_joiner = joiner.public_key();
        a.expected_peer = joiner.public_key();
        j.expected_peer = admin.public_key();
        clock += 2000;  // The joiner scans the code a little later.
        joiner_timing = {clock, clock};
        admin.qr_only = joiner.qr_only = true;
    }

    void step(GroupQrJoinConversation& from, GroupQrJoinConversation& to) {
        from.service();
        qr::RadioFrame frame{};
        if (!from.next(frame)) return;
        CHECK(frame.size <= qr::kRadioLimit);
        largest = std::max(largest, frame.size);
        from.sent(true);
        const auto key = std::make_pair(from.side(), label(frame));
        const auto copy = seen[key]++;
        log.push_back(key);
        if (deliver(from.side(), frame, copy)) CHECK(to.receive(frame.bytes.data(), frame.size));
    }
    // Runs both boards in 100 ms polls until both finish or time runs out.
    void run(std::uint64_t for_ms = 400000) {
        const auto end = clock + for_ms;
        while (clock < end && !(finished(admin_side) && finished(joiner_side))) {
            step(joiner_side, admin_side);
            step(admin_side, joiner_side);
            clock += 100;
        }
    }
    static bool finished(const GroupQrJoinConversation& c) {
        return c.phase() == Phase::committed || c.phase() == Phase::aborted || c.phase() == Phase::failed;
    }
    bool open() { return joiner_side.open(invite, joiner_timing, 7); }
};

bool zero(const GroupQrJoinConversation::Key& key) {
    return std::all_of(key.begin(), key.end(), [](std::uint8_t b) { return b == 0; });
}

void clean_join() {
    Link link;
    CHECK(link.open());
    CHECK(link.joiner_side.phase() == Phase::handshake);
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && link.joiner_side.phase() == Phase::committed);
    CHECK(link.a.kinds == std::vector<Kind>({Kind::prepared, Kind::committed}));
    CHECK(link.j.kinds == std::vector<Kind>({Kind::prepared, Kind::committed}));
    CHECK(link.admin_side.scope() == link.joiner_side.scope());
    // Each board sent every one of its frames at least once.
    for (const auto side : {Side::joiner, Side::admin}) {
        for (const auto kind : {Kind::admission, Kind::confirmed, Kind::prepared, Kind::committed}) {
            CHECK(link.seen.count({side, static_cast<int>(kind)}) == 1);
        }
    }
    CHECK(link.largest == qr::kStartBytes && link.largest <= qr::kRadioLimit);
    // The exchange order on a clean link.
    const std::vector<std::pair<Side, int>> first{
        {Side::joiner, 1}, {Side::admin, 2}, {Side::joiner, 101}, {Side::admin, 102}, {Side::joiner, 103}};
    CHECK(std::equal(first.begin(), first.end(), link.log.begin()));
    GroupQrJoinConversation::Key atx{}, arx{}, jtx{}, jrx{};
    CHECK(link.admin_side.pair_keys(atx, arx) && link.joiner_side.pair_keys(jtx, jrx));
    CHECK(atx == jrx && arx == jtx && atx != arx && !zero(atx) && !zero(arx));
    CHECK(!link.admin_side.pair_keys(atx, arx));  // once
    CHECK(link.admin.noise_begins == 1 && link.joiner.noise_begins == 1);
}

void lost_first_copy_of_every_frame() {
    Link link;
    link.deliver = [](Side, const qr::RadioFrame&, unsigned copy) { return copy != 0; };
    CHECK(link.open());
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && link.joiner_side.phase() == Phase::committed);
    GroupQrJoinConversation::Key atx{}, arx{}, jtx{}, jrx{};
    CHECK(link.admin_side.pair_keys(atx, arx) && link.joiner_side.pair_keys(jtx, jrx) && atx == jrx && arx == jtx);

    // The admin's last committed receipt is lost every time: the joiner's
    // repeats of its own committed receipt bring the admin's copies back.
    Link tail;
    tail.deliver = [](Side from, const qr::RadioFrame& f, unsigned copy) {
        return !(from == Side::admin && label(f) == static_cast<int>(Kind::committed) && copy < 3);
    };
    CHECK(tail.open());
    tail.run();
    CHECK(tail.admin_side.phase() == Phase::committed && tail.joiner_side.phase() == Phase::committed);
}

void waits_for_both_approvals() {
    Link link;
    link.j.fingerprints_ok = false;
    link.a.transcript_ok = false;
    CHECK(link.open());
    link.run(30000);
    CHECK(link.joiner_side.phase() == Phase::admission);
    CHECK(link.j.fingerprint_asks > 0 && link.j.kinds.empty());
    CHECK(std::none_of(link.log.begin(), link.log.end(), [](const auto& e) {
        return e.first == Side::joiner && e.second == static_cast<int>(Kind::admission);
    }));
    link.j.fingerprints_ok = true;
    link.run(30000);
    CHECK(link.admin_side.phase() == Phase::confirmation && link.a.kinds.empty());
    CHECK(std::none_of(link.log.begin(), link.log.end(), [](const auto& e) {
        return e.first == Side::admin && e.second == static_cast<int>(Kind::confirmed);
    }));
    link.a.transcript_ok = true;
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && link.joiner_side.phase() == Phase::committed);
}

void noise_envelope() {
    std::array<std::uint8_t, 64> message{};
    message.fill(9);
    const auto scope = bytes<32>(5);
    qr::RadioFrame f{};
    CHECK(join::encode_noise(1, scope, message.data(), 48, f) && f.size == 88);
    CHECK(std::string_view(reinterpret_cast<const char*>(f.bytes.data()), 4) == "OTGN");
    CHECK(f.bytes[4] == 1 && f.bytes[5] == 1 && f.bytes[6] == static_cast<std::uint8_t>(Side::joiner) && f.bytes[7] == 0);
    join::NoiseFrame out{};
    CHECK(join::decode_noise(f.bytes.data(), f.size, out) && out.stage == 1 && out.scope == scope && out.size == 48);
    CHECK(join::encode_noise(2, scope, message.data(), 48, f) && f.size == 88 &&
          f.bytes[6] == static_cast<std::uint8_t>(Side::admin));
    CHECK(join::encode_noise(3, scope, message.data(), 64, f) && f.size == 104);
    CHECK(!join::encode_noise(3, scope, message.data(), 48, f) && !join::encode_noise(0, scope, message.data(), 48, f) &&
          !join::encode_noise(4, scope, message.data(), 48, f) && !join::encode_noise(1, scope, nullptr, 48, f));
    CHECK(join::encode_noise(3, scope, message.data(), 64, f));
    for (std::size_t i : {4u, 5u, 6u, 7u}) {
        auto bad = f;
        bad.bytes[i] ^= 1;
        CHECK(!join::decode_noise(bad.bytes.data(), bad.size, out));
    }
    CHECK(!join::decode_noise(f.bytes.data(), f.size - 1, out) && !join::decode_noise(f.bytes.data(), f.size + 1, out));
    CHECK(join::is_join_frame(f.bytes.data(), f.size));
    const std::array<std::uint8_t, 21> beacon{'O', 'T', 'L', 'T', 1};
    CHECK(!join::is_join_frame(beacon.data(), beacon.size()));
    const std::array<std::uint8_t, 8> pairing{'O', 'T', 0x10, 0x01};
    CHECK(!join::is_join_frame(pairing.data(), pairing.size()));

    // Frames that are not a QR join's go back to the caller untouched.
    Link link;
    CHECK(!link.admin_side.receive(beacon.data(), beacon.size()));
    CHECK(!link.joiner_side.receive(pairing.data(), pairing.size()));
}

void strangers_and_other_scopes_are_ignored() {
    Link link;
    CHECK(link.open());
    // A board with no invite of its own hears the joiner and stays idle.
    Port op{link.clock, Side::admin};
    Identity outsider{137};
    Random orand{9};
    GroupQrJoinConversation stranger{Side::admin, crypto, outsider, op, outsider, orand, bytes<16>(8)};
    bool hold_noise_two = true;
    link.deliver = [&](Side from, const qr::RadioFrame& f, unsigned) {
        if (from == Side::joiner) {
            CHECK(stranger.receive(f.bytes.data(), f.size));
            CHECK(stranger.phase() == Phase::idle);
        }
        return !(hold_noise_two && from == Side::admin && label(f) == 102);
    };
    // The joiner holds the verified Reply and waits for Noise 2. Noise 2 for
    // another scope is dropped without harm.
    link.run(2000);
    CHECK(link.joiner_side.phase() == Phase::handshake);
    std::array<std::uint8_t, 48> junk{};
    junk.fill(0x33);
    qr::RadioFrame other{};
    CHECK(join::encode_noise(2, bytes<32>(99), junk.data(), junk.size(), other));
    CHECK(link.joiner_side.receive(other.bytes.data(), other.size));
    CHECK(link.joiner_side.phase() == Phase::handshake);
    // So is a joiner-sent stage arriving back at the joiner.
    CHECK(join::encode_noise(1, link.joiner_side.scope(), junk.data(), junk.size(), other));
    CHECK(link.joiner_side.receive(other.bytes.data(), other.size));
    CHECK(link.joiner_side.phase() == Phase::handshake);
    hold_noise_two = false;
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && link.joiner_side.phase() == Phase::committed);
    CHECK(stranger.phase() == Phase::idle && outsider.noise_begins == 0);
}

void substituted_reply_is_refused() {
    Link link;
    std::optional<qr::RadioFrame> start;
    link.deliver = [&](Side from, const qr::RadioFrame& f, unsigned) {
        if (from == Side::joiner && label(f) == static_cast<int>(Kind::start)) start = f;
        return true;
    };
    CHECK(link.open());
    link.step(link.joiner_side, link.admin_side);
    CHECK(start.has_value());
    // The outsider signs a Reply to the real Start with fresh values.
    qr::Fresh fresh{bytes<32>(40), bytes<32>(41), bytes<16>(42)};
    qr::RadioFrame forged{};
    CHECK(qr::sign_reply(*start, fresh, crypto, outsider_id, forged));
    CHECK(link.joiner_side.receive(forged.bytes.data(), forged.size));
    CHECK(link.joiner_side.phase() == Phase::handshake);
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && link.joiner_side.phase() == Phase::committed);
}

void tampered_noise_ends_the_join_without_a_commit() {
    Link link;
    link.deliver = [](Side from, const qr::RadioFrame& f, unsigned) {
        return !(from == Side::admin && label(f) == 102);
    };
    CHECK(link.open());
    link.run(3000);
    // Hand the joiner a tampered Noise 2 for its live scope.
    std::array<std::uint8_t, 48> junk{};
    junk.fill(0x44);
    qr::RadioFrame bad{};
    CHECK(join::encode_noise(2, link.joiner_side.scope(), junk.data(), junk.size(), bad));
    CHECK(link.joiner_side.receive(bad.bytes.data(), bad.size));
    CHECK(link.joiner_side.phase() == Phase::failed);
    qr::RadioFrame none{};
    CHECK(!link.joiner_side.next(none));
    link.deliver = [](Side, const qr::RadioFrame&, unsigned) { return true; };
    link.run();
    CHECK(link.admin_side.phase() == Phase::failed && link.joiner_side.phase() == Phase::failed);
    CHECK(link.a.kinds.empty() && link.j.kinds.empty());
}

void deadline_and_cancel() {
    // Silence after the handshake: both boards stop at the 3-minute invite
    // deadline with nothing durable.
    Link quiet;
    quiet.deliver = [](Side, const qr::RadioFrame& f, unsigned) { return label(f) > 100 || label(f) <= 2; };
    CHECK(quiet.open());
    quiet.run();
    CHECK(quiet.admin_side.phase() == Phase::failed && quiet.joiner_side.phase() == Phase::failed);
    CHECK(quiet.a.kinds.empty() && quiet.j.kinds.empty());
    CHECK(quiet.clock >= quiet.admin_timing.invitation_issued + qr::kInviteMs);

    // The joiner cancels during the transcript review: both boards keep an
    // aborted decision and nothing commits.
    Link cancel;
    cancel.j.transcript_ok = false;
    CHECK(cancel.open());
    cancel.run(30000);
    CHECK(cancel.joiner_side.phase() == Phase::confirmation);
    cancel.joiner_side.cancel();
    CHECK(cancel.joiner_side.phase() == Phase::aborted);
    cancel.run();
    CHECK(cancel.admin_side.phase() == Phase::aborted);
    CHECK(cancel.j.kinds == std::vector<Kind>({Kind::aborted}) && cancel.a.kinds == std::vector<Kind>({Kind::aborted}));
    GroupQrJoinConversation::Key tx{}, rx{};
    CHECK(!cancel.admin_side.pair_keys(tx, rx) && !cancel.joiner_side.pair_keys(tx, rx));
}

void replay_after_use_and_refusals() {
    Link link;
    std::optional<qr::RadioFrame> start;
    link.deliver = [&](Side from, const qr::RadioFrame& f, unsigned) {
        if (from == Side::joiner && label(f) == static_cast<int>(Kind::start) && !start) start = f;
        return true;
    };
    CHECK(link.open());
    link.run();
    CHECK(link.admin_side.phase() == Phase::committed && start.has_value());
    // The same Start replayed to a new admin conversation on the same board:
    // the invite is consumed, so nothing begins.
    Identity admin{21};
    Random again{5};
    GroupQrJoinConversation second{Side::admin, crypto, admin, link.a, admin, again, bytes<16>(6)};
    CHECK(second.receive(start->bytes.data(), start->size));
    CHECK(second.phase() == Phase::idle && admin.noise_begins == 0);

    // A roster that is no longer current refuses the Start too.
    Link stale;
    stale.a.admit = false;
    CHECK(stale.open());
    stale.run(20000);
    CHECK(stale.admin_side.phase() == Phase::idle && stale.joiner_side.phase() == Phase::handshake);

    // Opening refusals: wrong role, zero epoch, broken entropy, a second open.
    Link role;
    CHECK(!role.admin_side.open(role.invite, role.joiner_timing, 7) && role.admin_side.phase() == Phase::idle);
    Link epoch;
    CHECK(!epoch.joiner_side.open(epoch.invite, epoch.joiner_timing, 0) && epoch.joiner_side.phase() == Phase::failed);
    Link entropy;
    entropy.jr.broken = true;
    CHECK(!entropy.open() && entropy.joiner_side.phase() == Phase::failed && entropy.joiner.noise_begins == 0);
    Link twice;
    CHECK(twice.open() && !twice.open() && twice.joiner_side.phase() == Phase::handshake);
    // An admin board never joins its own invite.
    Port own{twice.clock, Side::joiner};
    Identity own_admin{21};
    Random r3{7};
    GroupQrJoinConversation self{Side::joiner, crypto, own_admin, own, own_admin, r3, bytes<16>(9)};
    CHECK(!self.open(twice.invite, twice.joiner_timing, 7) && own_admin.noise_begins == 0);
}

}  // namespace

int main() {
    clean_join();
    lost_first_copy_of_every_frame();
    waits_for_both_approvals();
    noise_envelope();
    strangers_and_other_scopes_are_ignored();
    substituted_reply_is_refused();
    tampered_noise_ends_the_join_without_a_commit();
    deadline_and_cancel();
    replay_after_use_and_refusals();
    std::cout << "PASS 9 QR join conversation groups (pinned libsodium)\n";
    return EXIT_SUCCESS;
}
