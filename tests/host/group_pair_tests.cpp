// Host tests for group pair setup (GROUP_PAIRWISE_V1 implementation order step 2, after the
// independent review of 9176227: one owner per pair, D-072 epochs, D-073 time limit).
// The crypto below is a deterministic TEST-ONLY stand-in: anyone can forge its
// "signatures". It exercises the pair rules, not any algorithm, which is not selected.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "opentrail/group_pair.hpp"
#include "opentrail/group_roster.hpp"

namespace {

using namespace opentrail::identity::roster;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

// ---- test-only stand-in primitives ----
std::uint64_t fnv(const std::uint8_t* p, std::size_t n, std::uint64_t seed) {
    std::uint64_t h = 1469598103934665603ULL ^ seed;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ULL; h ^= h >> 32;
    return h;
}
template <std::size_t N>
void fill_hash(std::array<std::uint8_t, N>& out, const std::vector<std::uint8_t>& in, std::uint64_t seed) {
    for (std::size_t lane = 0; lane < N / 8; ++lane) {
        const std::uint64_t h = fnv(in.data(), in.size(), seed * 131 + lane);
        for (std::size_t b = 0; b < 8; ++b) out[lane * 8 + b] = static_cast<std::uint8_t>(h >> (8 * b));
    }
}
std::vector<std::uint8_t> join(const std::uint8_t* a, std::size_t an, const std::uint8_t* b, std::size_t bn) {
    std::vector<std::uint8_t> v(a, a + an);
    v.insert(v.end(), b, b + bn);
    return v;
}
class TestCrypto final : public RosterCrypto {
public:
    bool verify(const Signature& s, const std::uint8_t* m, std::size_t n, const PublicKey& k) const override {
        Signature expected{};
        fill_hash(expected, join(k.data(), k.size(), m, n), 7);
        return expected == s;
    }
    bool digest(const std::uint8_t* m, std::size_t n, Digest& out) const override {
        fill_hash(out, std::vector<std::uint8_t>(m, m + n), 3);
        return true;
    }
    bool fingerprint(const PublicKey& k, Fingerprint& out) const override {
        fill_hash(out, std::vector<std::uint8_t>(k.begin(), k.end()), 5);
        return true;
    }
};
class TestSigner final : public RosterSigner {
public:
    explicit TestSigner(const PublicKey& key) : key_(key) {}
    const PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* m, std::size_t n, Signature& out) const override {
        fill_hash(out, join(key_.data(), key_.size(), m, n), 7);
        return true;
    }
private:
    PublicKey key_;
};
class FailingSigner final : public RosterSigner {
public:
    explicit FailingSigner(const PublicKey& key) : key_(key) {}
    const PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t*, std::size_t, Signature&) const override { return false; }
private:
    PublicKey key_;
};

PublicKey key(std::uint8_t marker) {
    PublicKey k{};
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = static_cast<std::uint8_t>(marker * 37 + i);
    return k;
}
// Fresh values: each call returns a new value (owners refuse reuse of their own values).
std::uint8_t counter = 1;
template <std::size_t N>
std::array<std::uint8_t, N> fresh() {
    const std::uint8_t m = counter++;
    std::array<std::uint8_t, N> v{};
    for (std::size_t i = 0; i < N; ++i) v[i] = static_cast<std::uint8_t>(m + 7 * i + 1);
    v[0] = m;
    v[1] = static_cast<std::uint8_t>(counter);
    return v;
}

const TestCrypto crypto{};
const PublicKey kA = key(1), kB = key(2), kC = key(3), kD = key(4), kX = key(9), kG = key(7);
constexpr std::uint64_t kGroup = 0x0123456789ABCDEFULL;

struct Signed {
    Roster roster{};
    std::vector<std::uint8_t> bytes;
};
Signed seal(Roster r, const PublicKey& signer) {
    Signed s{};
    EXPECT(sign(r, TestSigner(signer)));
    s.roster = r;
    s.bytes.resize(kMaximumRosterBytes);
    s.bytes.resize(encode(r, s.bytes.data(), s.bytes.size()));
    return s;
}
Signed first(const PublicKey& admin = kA) {
    Roster r{};
    EXPECT(create_first(kGroup, admin, "admin", LocationRule::off, r));
    return seal(r, admin);
}
Signed added(const Signed& cur, const PublicKey& k, const char* alias) {
    Roster r{};
    EXPECT(add_member(cur.roster, crypto, k, alias, r));
    return seal(r, cur.roster.members[0].key);
}
Signed removed(const Signed& cur, const PublicKey& k) {
    Roster r{};
    EXPECT(remove_member(cur.roster, crypto, k, r));
    return seal(r, cur.roster.members[0].key);
}
Signed rule(const Signed& cur, LocationRule lr) {
    Roster r{};
    EXPECT(change_location_rule(cur.roster, crypto, lr, r));
    return seal(r, cur.roster.members[0].key);
}
Outcome offer(RosterState& s, const Signed& r) { return s.offer(r.bytes.data(), r.bytes.size(), crypto); }
RosterState board(const PublicKey& own, const Signed& roster, const PublicKey& admin = kA) {
    RosterState s;
    EXPECT(s.initialize(admin, kGroup, own));
    EXPECT(offer(s, roster) == Outcome::accepted);
    return s;
}
PairOwner owner(const RosterState& s, const PublicKey& peer) {
    auto o = PairOwner::create(s, peer);
    EXPECT(o.has_value());
    return std::move(*o);
}

// A, B (added 2), C (added 3, newer than B).
const Signed v3 = added(added(first(), kB, "bob"), kC, "carol");

// One board: its roster, its owner for the pair, its identity.
struct Side {
    RosterState roster;
    PairOwner pair;
    PublicKey me;
};
Side side(const PublicKey& me, const PublicKey& peer, const Signed& r = v3) {
    RosterState s = board(me, r);
    if (me == kB && peer == kC) EXPECT(s.accept_member(kC));
    PairOwner o = owner(s, peer);
    return Side{std::move(s), std::move(o), me};
}

struct Run {
    PairOutcome start{}, reply{}, confirm{}, confirmed{};
    std::optional<VerifiedPairBinding> init_verified, resp_verified;
    PairStart s{};
    PairReply r{};
    PairConfirm c{};
};
Run exchange(Side& init, Side& resp, std::uint64_t now = 1000) {
    Run run;
    run.start = init.pair.start(init.roster, fresh<32>(), fresh<32>(), fresh<16>(), now, TestSigner(init.me), run.s);
    if (run.start != PairOutcome::ok) return run;
    run.reply = resp.pair.on_start(resp.roster, run.s, fresh<32>(), fresh<32>(), fresh<16>(), now + 10, crypto,
                                   TestSigner(resp.me), run.r);
    if (run.reply != PairOutcome::ok) return run;
    run.confirm = init.pair.on_reply(init.roster, run.r, now + 20, crypto, TestSigner(init.me), run.c, run.init_verified);
    if (run.confirm != PairOutcome::ok) return run;
    run.confirmed = resp.pair.on_confirm(resp.roster, run.c, now + 30, crypto, run.resp_verified);
    return run;
}
bool complete(const Run& r) {
    return r.start == PairOutcome::ok && r.reply == PairOutcome::ok && r.confirm == PairOutcome::ok &&
           r.confirmed == PairOutcome::ok && r.init_verified && r.resp_verified;
}
void activate(Side& s) {
    EXPECT(s.pair.activation_possible());
    EXPECT(s.pair.commit());
}
PairStart signed_start(PairStart s, const PublicKey& signer) {
    std::array<std::uint8_t, kStartBytes> b{};
    TestSigner(signer).sign(b.data(), start_bytes(s, b.data(), b.size()), s.signature);
    return s;
}

// ---------------------------------------------------------------- tests

void test_owner_creation() {
    RosterState b = board(kB, v3), a = board(kA, v3);
    EXPECT(!PairOwner::create(b, kA).has_value());   // admin pair: in-person, not introduced
    EXPECT(!PairOwner::create(a, kB).has_value());
    EXPECT(!PairOwner::create(b, kB).has_value());   // itself
    EXPECT(!PairOwner::create(b, kG).has_value());   // not listed
    EXPECT(PairOwner::create(b, kC).has_value());
    const Signed fork = rule(v3, LocationRule::required);
    EXPECT(offer(b, rule(v3, LocationRule::preferred)) == Outcome::accepted);
    EXPECT(offer(b, fork) == Outcome::conflict);
    EXPECT(!PairOwner::create(b, kC).has_value());   // no owner while in conflict
}

void test_introduce_succeeds_and_binds_everything() {
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(c.pair.next_purpose() == PairPurpose::introduce);
    Run run = exchange(c, b);
    EXPECT(complete(run));
    if (!run.init_verified || !run.resp_verified) return;
    EXPECT(run.init_verified->epoch() == 1 && run.resp_verified->epoch() == 1);
    EXPECT(run.init_verified->local_role() == PairRole::initiator && run.resp_verified->local_role() == PairRole::responder);
    EXPECT(run.s.roster_digest == b.roster.digest() && run.s.lifetime_ms == kPairLifetimeMs);
    EXPECT(run.s.initiator == c.roster.fingerprints()[2] && run.s.responder == b.roster.fingerprints()[1]);
    EXPECT(run.init_verified->initiator_binding_signature() == run.resp_verified->initiator_binding_signature());
    EXPECT(c.pair.step() == PairStep::bound && b.pair.step() == PairStep::bound);
    EXPECT(!c.pair.commit());                       // never commit before ACTIVATION_POSSIBLE
    activate(c);
    activate(b);
    EXPECT(c.pair.committed_epoch() == 1 && b.pair.committed_epoch() == 1);
    EXPECT(c.pair.next_purpose() == PairPurpose::rekey && b.pair.has_history());
}

void test_consent_and_role() {
    // B has not accepted C.
    RosterState bs = board(kB, v3);
    Side b{std::move(bs), owner(board(kB, v3), kC), kB};
    Side c = side(kC, kB);
    Run run = exchange(c, b);
    EXPECT(run.reply == PairOutcome::no_consent);
    EXPECT(b.pair.step() == PairStep::none && b.pair.used_epoch() == 0);
    // The older member may not start the introduce.
    Side b2 = side(kB, kC), c2 = side(kC, kB);
    PairStart s{};
    EXPECT(b2.pair.start(b2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kB), s) == PairOutcome::wrong_role);
    // A forged introduce start signed by B is refused by C for the role.
    PairStart forged{};
    forged.purpose = PairPurpose::introduce;
    forged.group = {c2.roster.fingerprints()[0], kGroup};
    forged.roster_version = 3; forged.roster_digest = c2.roster.digest(); forged.epoch = 1;
    forged.initiator = c2.roster.fingerprints()[1]; forged.responder = c2.roster.fingerprints()[2];
    forged.lifetime_ms = kPairLifetimeMs;
    forged.initiator_ephemeral = fresh<32>(); forged.initiator_challenge = fresh<32>(); forged.initiator_boot = fresh<16>();
    PairReply r{};
    EXPECT(c2.pair.on_start(c2.roster, signed_start(forged, kB), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto,
                            TestSigner(kC), r) == PairOutcome::wrong_role);
    // A rekey start to a board without history for this pair is refused (review probe B).
    PairStart rekey = forged;
    rekey.purpose = PairPurpose::rekey;
    EXPECT(b2.pair.on_start(b2.roster, signed_start([&] { PairStart x = rekey; std::swap(x.initiator, x.responder); return x; }(), kC),
                            fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) == PairOutcome::wrong_purpose);
    EXPECT(b2.pair.used_epoch() == 0 && !b2.pair.has_history());
}

void test_removal_makes_owner_stale() {
    // Review probe B: owners made on v3 survive nothing once C is removed.
    Side c = side(kC, kB), b = side(kB, kC);
    const Signed v4 = removed(v3, kC);
    EXPECT(offer(b.roster, v4) == Outcome::accepted);
    Run run = exchange(c, b);
    EXPECT(run.start == PairOutcome::ok && run.reply == PairOutcome::stale_owner && b.pair.stale());
    EXPECT(offer(c.roster, v4) == Outcome::accepted_removed_self);
    PairStart s{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::roster_not_active);
    // Mid-attempt: C removed after B answered; the confirm cannot produce a binding.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    PairStart s2{};
    PairReply r2{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c2.pair.start(c2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s2) == PairOutcome::ok);
    EXPECT(b2.pair.on_start(b2.roster, s2, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r2) == PairOutcome::ok);
    EXPECT(c2.pair.on_reply(c2.roster, r2, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
    EXPECT(offer(b2.roster, v4) == Outcome::accepted);
    v.reset();
    EXPECT(b2.pair.on_confirm(b2.roster, conf, 30, crypto, v) == PairOutcome::stale_owner && !v);
    EXPECT(!b2.pair.activation_possible());
}

void test_re_added_member_needs_new_consent() {
    // Review probe D: the old owner of a committed pair cannot rekey after removal and re-add.
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(complete(exchange(c, b)));
    activate(c);
    activate(b);
    const Signed v4 = removed(v3, kC);
    const Signed v5 = added(v4, kC, "carol");
    EXPECT(offer(b.roster, v4) == Outcome::accepted);
    EXPECT(offer(b.roster, v5) == Outcome::accepted);
    RosterState c5 = board(kC, v5);
    Side c_new{std::move(c5), PairOwner(owner(board(kC, v5), kB)), kC};
    Run old_owner = exchange(c_new, b);   // B's retained owner from the earlier incarnation
    EXPECT(old_owner.reply == PairOutcome::stale_owner && b.pair.stale());
    EXPECT(!b.roster.accepted(kC));
    // A new owner for the new incarnation starts without history and needs B's tap.
    Side b_new{board(kB, v5), owner(board(kB, v5), kC), kB};
    EXPECT(!b_new.pair.has_history());
    Side c_newer{board(kC, v5), owner(board(kC, v5), kB), kC};
    EXPECT(exchange(c_newer, b_new).reply == PairOutcome::no_consent);
    EXPECT(b_new.roster.accept_member(kC));
    Side c_last{board(kC, v5), owner(board(kC, v5), kB), kC};
    EXPECT(complete(exchange(c_last, b_new)));
}

void test_retired_attempt_cannot_verify() {
    // Review probe A: restart after answering; the late confirm is refused.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 10, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 20, crypto, TestSigner(kB), r) == PairOutcome::ok);
    EXPECT(c.pair.on_reply(c.roster, r, 30, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
    b.pair.restart();
    v.reset();
    EXPECT(b.pair.on_confirm(b.roster, conf, 40, crypto, v) == PairOutcome::wrong_state && !v);
    EXPECT(b.pair.used_epoch() == 0 && !b.pair.activation_possible());
    // Review probe C: a signer failure on a new start changes nothing; the attempt that
    // still exists completes consistently.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    PairStart s1{};
    PairReply r1{}, r2{};
    PairConfirm conf1{};
    std::optional<VerifiedPairBinding> v1;
    EXPECT(c2.pair.start(c2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s1) == PairOutcome::ok);
    EXPECT(b2.pair.on_start(b2.roster, s1, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r1) == PairOutcome::ok);
    EXPECT(c2.pair.on_reply(c2.roster, r1, 20, crypto, TestSigner(kC), conf1, v1) == PairOutcome::ok);
    PairStart other = s1;
    other.initiator_challenge = fresh<32>();
    other = signed_start(other, kC);
    EXPECT(b2.pair.on_start(b2.roster, other, fresh<32>(), fresh<32>(), fresh<16>(), 6010, crypto, FailingSigner(kB), r2) ==
           PairOutcome::crypto_failure);
    EXPECT(b2.pair.step() == PairStep::awaiting_confirm);
    v1.reset();
    EXPECT(b2.pair.on_confirm(b2.roster, conf1, 6020, crypto, v1) == PairOutcome::ok && v1 && v1->epoch() == 1);
    EXPECT(b2.pair.activation_possible());
}

void test_no_public_numeric_transitions() {
    // Review: commit must not be reachable with numbers alone.
    Side c = side(kC, kB);
    EXPECT(!c.pair.activation_possible() && !c.pair.commit() && c.pair.committed_epoch() == 0);
    PairStart s{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(!c.pair.activation_possible() && !c.pair.commit());   // awaiting reply: nothing verified
    c.pair.cancel();
    EXPECT(c.pair.step() == PairStep::none && c.pair.used_epoch() == 0);
}

void test_confirm_signer_is_pinned() {
    // Review probe B: on_reply with another signer.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 10, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 11, crypto, TestSigner(kB), r) == PairOutcome::ok);
    EXPECT(c.pair.on_reply(c.roster, r, 12, crypto, TestSigner(kX), conf, v) == PairOutcome::wrong_signer && !v);
    EXPECT(c.pair.step() == PairStep::awaiting_reply);
    EXPECT(c.pair.on_reply(c.roster, r, 13, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(b.pair.on_confirm(b.roster, conf, 14, crypto, v) == PairOutcome::ok && v);
    PairStart s2{};
    PairReply r2{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 20, TestSigner(kX), s2) == PairOutcome::wrong_signer);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 21, crypto, TestSigner(kX), r2) == PairOutcome::wrong_signer);
}

void test_unheard_old_start_cannot_block_the_live_one() {
    // Review probe A: a lost start uses nothing; the retry reuses the number (D-072).
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart lost{}, live{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), lost) == PairOutcome::ok);
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 100000, TestSigner(kC), live) == PairOutcome::ok);
    EXPECT(lost.epoch == 1 && live.epoch == 1 && c.pair.used_epoch() == 0);
    PairReply r_old{}, r_live{};
    EXPECT(b.pair.on_start(b.roster, lost, fresh<32>(), fresh<32>(), fresh<16>(), 100000, crypto, TestSigner(kB), r_old) == PairOutcome::ok);
    EXPECT(b.pair.used_epoch() == 0);   // answering uses nothing
    EXPECT(b.pair.on_start(b.roster, live, fresh<32>(), fresh<32>(), fresh<16>(), 100001, crypto, TestSigner(kB), r_live) == PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.on_reply(c.roster, r_old, 100002, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_signature && !v);
    EXPECT(c.pair.on_reply(c.roster, r_live, 100003, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(b.pair.on_confirm(b.roster, conf, 100004, crypto, v) == PairOutcome::ok && v);
    // The old start once more: already answered.
    EXPECT(b.pair.on_start(b.roster, lost, fresh<32>(), fresh<32>(), fresh<16>(), 100005, crypto, TestSigner(kB), r_old) ==
           PairOutcome::duplicate_start);
}

void test_time_limit_per_side() {
    // D-073: the initiator counts 60 s from its start, the responder from receipt.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 1000, crypto, TestSigner(kB), r) == PairOutcome::ok);
    EXPECT(c.pair.on_reply(c.roster, r, kPairLifetimeMs, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);  // exactly 60 s
    v.reset();
    EXPECT(b.pair.on_confirm(b.roster, conf, 1000 + kPairLifetimeMs + 1, crypto, v) == PairOutcome::expired && !v);
    EXPECT(b.pair.step() == PairStep::none);
    // Initiator side: one millisecond late.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(c2.pair.start(c2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 500, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b2.pair.on_start(b2.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 600, crypto, TestSigner(kB), r) == PairOutcome::ok);
    EXPECT(c2.pair.on_reply(c2.roster, r, 500 + kPairLifetimeMs + 1, crypto, TestSigner(kC), conf, v) == PairOutcome::expired);
    // Clock regression fails closed; a huge clock does not overflow.
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(c3.pair.start(c3.roster, fresh<32>(), fresh<32>(), fresh<16>(), 5000, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b3.pair.on_start(b3.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), UINT64_MAX - 10, crypto, TestSigner(kB), r) == PairOutcome::ok);
    EXPECT(c3.pair.on_reply(c3.roster, r, 4999, crypto, TestSigner(kC), conf, v) == PairOutcome::expired);
    EXPECT(b3.pair.on_confirm(b3.roster, conf, UINT64_MAX, crypto, v) == PairOutcome::bad_signature);  // still inside its window
    // Lifetime in the start is bounded.
    PairStart long_life = s;
    long_life.lifetime_ms = kPairLifetimeMs + 1;
    EXPECT(b3.pair.on_start(b3.roster, signed_start(long_life, kC), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto,
                            TestSigner(kB), r) == PairOutcome::bad_lifetime);
}

void test_epochs_d072() {
    // Lost reply: nothing used on either side; the retry reuses the number.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r) == PairOutcome::ok);
    Run again = exchange(c, b, 70000);
    EXPECT(complete(again) && again.s.epoch == 1);
    activate(c);
    activate(b);
    // Lost confirm: C used 2, B did not; C's next start (3) is a catch-up of one for B.
    PairStart s2{};
    PairReply r2{};
    PairConfirm lost_confirm{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 200000, TestSigner(kC), s2) == PairOutcome::ok);
    EXPECT(s2.purpose == PairPurpose::rekey && s2.epoch == 2);
    EXPECT(b.pair.on_start(b.roster, s2, fresh<32>(), fresh<32>(), fresh<16>(), 200010, crypto, TestSigner(kB), r2) == PairOutcome::ok);
    EXPECT(c.pair.on_reply(c.roster, r2, 200020, crypto, TestSigner(kC), lost_confirm, v) == PairOutcome::ok);
    EXPECT(c.pair.used_epoch() == 2 && b.pair.used_epoch() == 1);
    c.pair.restart();
    b.pair.restart();
    Run three = exchange(c, b, 300000);
    EXPECT(complete(three) && three.s.epoch == 3);
    // Equal, older and a skip of two are refused.
    Side c4 = side(kC, kB), b4 = side(kB, kC);
    PairStart base{};
    EXPECT(c4.pair.start(c4.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), base) == PairOutcome::ok);
    for (std::uint32_t e : {0U, 3U, 0xFFFFFFFFU}) {
        PairStart x = base;
        x.epoch = e;
        EXPECT(b4.pair.on_start(b4.roster, signed_start(x, kC), fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto,
                                TestSigner(kB), r) == PairOutcome::bad_epoch);
    }
    PairStart two = base;
    two.epoch = 2;  // one past the next number: catch-up allowed
    EXPECT(b4.pair.on_start(b4.roster, signed_start(two, kC), fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto,
                            TestSigner(kB), r) == PairOutcome::ok);
}

void test_crossing_starts() {
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(complete(exchange(c, b)));
    activate(c);
    activate(b);
    // Equal epochs: the lower fingerprint wins; the loser answers, the winner ignores.
    PairStart sb{}, sc{};
    PairReply rb{}, rc{};
    EXPECT(b.pair.start(b.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kB), sb) == PairOutcome::ok);
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), sc) == PairOutcome::ok);
    EXPECT(sb.epoch == 2 && sc.epoch == 2);
    const bool b_wins = start_wins(sb, sc);
    EXPECT(b_wins != start_wins(sc, sb));
    Side& winner = b_wins ? b : c;
    Side& loser = b_wins ? c : b;
    const PairStart& win_start = b_wins ? sb : sc;
    const PairStart& lose_start = b_wins ? sc : sb;
    EXPECT(winner.pair.on_start(winner.roster, lose_start, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto,
                                TestSigner(winner.me), rb) == PairOutcome::local_attempt_wins);
    EXPECT(loser.pair.on_start(loser.roster, win_start, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto,
                               TestSigner(loser.me), rc) == PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(winner.pair.on_reply(winner.roster, rc, 20, crypto, TestSigner(winner.me), conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(loser.pair.on_confirm(loser.roster, conf, 30, crypto, v) == PairOutcome::ok && v && v->epoch() == 2);
    // Unequal floors (review probe C): after a lost confirm, the higher epoch wins.
    activate(winner);
    activate(loser);
    PairStart s3{};
    PairReply r3{};
    PairConfirm lost{};
    v.reset();
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 1000, TestSigner(kC), s3) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s3, fresh<32>(), fresh<32>(), fresh<16>(), 1010, crypto, TestSigner(kB), r3) == PairOutcome::ok);
    EXPECT(c.pair.on_reply(c.roster, r3, 1020, crypto, TestSigner(kC), lost, v) == PairOutcome::ok);
    c.pair.restart();
    b.pair.restart();
    PairStart hi{}, lo{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 9000, TestSigner(kC), hi) == PairOutcome::ok);
    EXPECT(b.pair.start(b.roster, fresh<32>(), fresh<32>(), fresh<16>(), 9000, TestSigner(kB), lo) == PairOutcome::ok);
    EXPECT(hi.epoch == 4 && lo.epoch == 3);
    EXPECT(c.pair.on_start(c.roster, lo, fresh<32>(), fresh<32>(), fresh<16>(), 9010, crypto, TestSigner(kC), r3) ==
           PairOutcome::bad_epoch);
    EXPECT(b.pair.on_start(b.roster, hi, fresh<32>(), fresh<32>(), fresh<16>(), 9010, crypto, TestSigner(kB), r3) == PairOutcome::ok);
}

void test_roster_hints_only_after_signature() {
    // Review probes A and B: forged hints are refused as bad signatures.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    PairStart forged = s;
    ++forged.roster_version;
    EXPECT(b.pair.on_start(b.roster, forged, fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) == PairOutcome::bad_signature);
    forged = s;
    forged.roster_digest[0] ^= 1;
    EXPECT(b.pair.on_start(b.roster, forged, fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) == PairOutcome::bad_signature);
    // Signed disagreements give the hints.
    const Signed v4 = rule(v3, LocationRule::preferred);
    const Signed v4b = rule(v3, LocationRule::required);
    Side c4{board(kC, v4), owner(board(kC, v4), kB), kC};
    Side c4b{board(kC, v4b), owner(board(kC, v4b), kB), kC};
    Side b4 = side(kB, kC, v4);
    EXPECT(exchange(c4, b).reply == PairOutcome::peer_roster_newer);
    EXPECT(exchange(c, b4).reply == PairOutcome::peer_roster_older);
    EXPECT(exchange(c4b, b4).reply == PairOutcome::peer_roster_differs);
    EXPECT(complete(exchange(c4, b4)));
}

void test_roster_change_mid_attempt() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r) == PairOutcome::ok);
    const Signed v4 = rule(v3, LocationRule::preferred);
    EXPECT(offer(c.roster, v4) == Outcome::accepted);
    EXPECT(c.pair.on_reply(c.roster, r, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::roster_changed && !v);
    EXPECT(c.pair.step() == PairStep::none && !c.pair.stale());
    // Conflict: no step at all until the user acts.
    Side c2 = side(kC, kB);
    EXPECT(offer(c2.roster, v4) == Outcome::accepted);
    EXPECT(offer(c2.roster, rule(v3, LocationRule::required)) == Outcome::conflict);
    PairStart s2{};
    EXPECT(c2.pair.start(c2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s2) == PairOutcome::roster_not_active);
    EXPECT(c2.roster.keep_current());
    EXPECT(c2.pair.start(c2.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s2) == PairOutcome::ok);
}

void test_resend_duplicate_and_budget() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r1{}, r2{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r1) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 20, crypto, TestSigner(kB), r2) == PairOutcome::resent_reply);
    EXPECT(r1.signature == r2.signature && r1.responder_challenge == r2.responder_challenge);
    // Distinct starts: three answers per window, then rate limited.
    std::size_t answered = 1;
    PairOutcome last = PairOutcome::ok;
    for (int i = 0; i < 3; ++i) {
        PairStart x{};
        EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 100 + i, TestSigner(kC), x) == PairOutcome::ok);
        PairReply rr{};
        last = b.pair.on_start(b.roster, x, fresh<32>(), fresh<32>(), fresh<16>(), 100 + i, crypto, TestSigner(kB), rr);
        if (last == PairOutcome::ok) ++answered;
    }
    EXPECT(answered == kAnswersPerWindow && last == PairOutcome::rate_limited);
    PairStart y{};
    PairReply ry{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 10 + kAnswerWindowMs, TestSigner(kC), y) == PairOutcome::ok);
    EXPECT(b.pair.on_start(b.roster, y, fresh<32>(), fresh<32>(), fresh<16>(), 10 + kAnswerWindowMs, crypto, TestSigner(kB), ry) == PairOutcome::ok);
}

void test_fresh_values() {
    Side c = side(kC, kB), b = side(kB, kC);
    const Ephemeral e = fresh<32>();
    const Challenge ch = fresh<32>();
    PairStart s{}, s2{};
    PairReply r{};
    EXPECT(c.pair.start(c.roster, e, ch, fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(c.pair.start(c.roster, fresh<32>(), ch, fresh<16>(), 1, TestSigner(kC), s2) == PairOutcome::bad_fresh_values);  // reused challenge
    EXPECT(c.pair.start(c.roster, e, fresh<32>(), fresh<16>(), 1, TestSigner(kC), s2) == PairOutcome::bad_fresh_values);   // reused ephemeral
    Ephemeral zero{};
    EXPECT(c.pair.start(c.roster, zero, fresh<32>(), fresh<16>(), 1, TestSigner(kC), s2) == PairOutcome::bad_fresh_values);
    // Reflection by the responder, and by a reply.
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), s.initiator_challenge, fresh<16>(), 10, crypto, TestSigner(kB), r) == PairOutcome::bad_fresh_values);
    EXPECT(b.pair.on_start(b.roster, s, s.initiator_ephemeral, fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r) == PairOutcome::bad_fresh_values);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), r) == PairOutcome::ok);
    PairReply reflected = r;
    reflected.responder_challenge = s.initiator_challenge;
    std::array<std::uint8_t, kBindingBytes> bb{};
    TestSigner(kB).sign(bb.data(), binding_bytes(s, reflected, bb.data(), bb.size()), reflected.signature);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(c.pair.on_reply(c.roster, reflected, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_fresh_values && !v);
}

void test_interrupted_activation_converges() {
    // One side committed, the other lost power after ACTIVATION_POSSIBLE: never resumed.
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(complete(exchange(c, b)));
    activate(c);
    EXPECT(b.pair.activation_possible());
    b.pair.restart();
    EXPECT(b.pair.step() == PairStep::none && b.pair.committed_epoch() == 0 && b.pair.used_epoch() == 1);
    EXPECT(!b.pair.commit());
    Run two = exchange(c, b, 100000);
    EXPECT(complete(two) && two.s.epoch == 2 && two.s.purpose == PairPurpose::rekey);
    // The other way round: the initiator was interrupted before committing.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(complete(exchange(c2, b2)));
    activate(b2);
    EXPECT(c2.pair.activation_possible());
    c2.pair.restart();
    Run again = exchange(c2, b2, 100000);
    EXPECT(complete(again) && again.s.epoch == 2);
    // A start sent while waiting in ACTIVATION_POSSIBLE is refused: commit or restart first.
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(complete(exchange(c3, b3)));
    EXPECT(c3.pair.activation_possible());
    PairStart s{};
    EXPECT(c3.pair.start(c3.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::wrong_state);
}

void test_outputs_never_stale() {
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b);
    EXPECT(complete(run));
    std::optional<VerifiedPairBinding> v = run.resp_verified;
    EXPECT(b.pair.on_confirm(b.roster, run.c, 2000, crypto, v) == PairOutcome::wrong_state && !v);  // cleared on refusal
}

void test_signed_bytes_layout() {
    PairStart s{};
    s.purpose = PairPurpose::rekey;
    s.group.admin = fresh<32>();
    s.group.group_id = kGroup;
    s.roster_version = 7;
    s.roster_digest = fresh<32>();
    s.epoch = 0x01020304;
    s.initiator = fresh<32>();
    s.responder = fresh<32>();
    s.lifetime_ms = 30000;
    s.initiator_ephemeral = fresh<32>();
    s.initiator_challenge = fresh<32>();
    s.initiator_boot = fresh<16>();
    PairReply r{};
    r.responder_ephemeral = fresh<32>();
    r.responder_challenge = fresh<32>();
    r.responder_boot = fresh<16>();
    std::array<std::uint8_t, kStartBytes> sb{};
    std::array<std::uint8_t, kBindingBytes> bb{};
    EXPECT(start_bytes(s, sb.data(), sb.size()) == kStartBytes);
    EXPECT(binding_bytes(s, r, bb.data(), bb.size()) == kBindingBytes);
    EXPECT(start_bytes(s, sb.data(), sb.size() - 1) == 0);
    EXPECT(std::memcmp(sb.data(), kStartDomain.data(), kStartDomain.size()) == 0);
    EXPECT(std::memcmp(bb.data(), kBindingDomain.data(), kBindingDomain.size()) == 0);
    const std::size_t c = kStartDomain.size();
    EXPECT(sb[c] == 2);
    EXPECT(std::equal(s.group.admin.begin(), s.group.admin.end(), sb.begin() + c + 1));
    EXPECT(sb[c + 33] == 0x01 && sb[c + 40] == 0xEF);
    EXPECT(sb[c + 44] == 7);
    const std::size_t e = c + 1 + 32 + 8 + 4 + 32;
    EXPECT(sb[e] == 1 && sb[e + 1] == 2 && sb[e + 2] == 3 && sb[e + 3] == 4);
    // Each field changes the signed bytes.
    auto changed_start = [&](auto mutate) {
        PairStart t = s;
        mutate(t);
        std::array<std::uint8_t, kStartBytes> tb{};
        start_bytes(t, tb.data(), tb.size());
        return tb != sb;
    };
    EXPECT(changed_start([](PairStart& t) { t.purpose = PairPurpose::introduce; }));
    EXPECT(changed_start([](PairStart& t) { t.group.admin[5] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.group.group_id ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.roster_version += 1; }));
    EXPECT(changed_start([](PairStart& t) { t.roster_digest[9] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.epoch += 1; }));
    EXPECT(changed_start([](PairStart& t) { t.initiator[3] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.responder[3] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.lifetime_ms += 1; }));
    EXPECT(changed_start([](PairStart& t) { t.initiator_ephemeral[0] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.initiator_challenge[0] ^= 1; }));
    EXPECT(changed_start([](PairStart& t) { t.initiator_boot[0] ^= 1; }));
    for (int field = 0; field < 3; ++field) {
        PairReply u = r;
        if (field == 0) u.responder_ephemeral[0] ^= 1;
        if (field == 1) u.responder_challenge[0] ^= 1;
        if (field == 2) u.responder_boot[0] ^= 1;
        std::array<std::uint8_t, kBindingBytes> ub{};
        binding_bytes(s, u, ub.data(), ub.size());
        EXPECT(ub != bb);
    }
    EXPECT(kStartDomain != kBindingDomain && kBindingDomain != kSignatureDomain);
}


void test_start_must_name_this_pair() {
    // Signed by the right peer, but naming another initiator, responder or group.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    EXPECT(c.pair.start(c.roster, fresh<32>(), fresh<32>(), fresh<16>(), 0, TestSigner(kC), s) == PairOutcome::ok);
    PairStart x = s;
    x.initiator = b.roster.fingerprints()[0];
    EXPECT(b.pair.on_start(b.roster, signed_start(x, kC), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    x = s;
    x.responder = b.roster.fingerprints()[0];
    EXPECT(b.pair.on_start(b.roster, signed_start(x, kC), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    x = s;
    x.group.admin[0] ^= 1;   // look-alike group: same group ID, another admin
    EXPECT(b.pair.on_start(b.roster, signed_start(x, kC), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    x = s;
    x.group.group_id ^= 1;
    EXPECT(b.pair.on_start(b.roster, signed_start(x, kC), fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    EXPECT(b.pair.step() == PairStep::none && b.pair.used_epoch() == 0);
    EXPECT(b.pair.on_start(b.roster, s, fresh<32>(), fresh<32>(), fresh<16>(), 0, crypto, TestSigner(kB), r) == PairOutcome::ok);
}
}  // namespace

int main() {
    test_owner_creation();
    test_introduce_succeeds_and_binds_everything();
    test_consent_and_role();
    test_removal_makes_owner_stale();
    test_re_added_member_needs_new_consent();
    test_retired_attempt_cannot_verify();
    test_no_public_numeric_transitions();
    test_confirm_signer_is_pinned();
    test_unheard_old_start_cannot_block_the_live_one();
    test_time_limit_per_side();
    test_epochs_d072();
    test_crossing_starts();
    test_roster_hints_only_after_signature();
    test_roster_change_mid_attempt();
    test_resend_duplicate_and_budget();
    test_fresh_values();
    test_interrupted_activation_converges();
    test_outputs_never_stale();
    test_signed_bytes_layout();
    test_start_must_name_this_pair();
    if (failures != 0) {
        std::cerr << failures << " group pair assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 20 group pair scenario groups\n";
    return EXIT_SUCCESS;
}
