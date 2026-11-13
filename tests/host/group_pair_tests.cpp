// Host tests for group pair setup (GROUP_PAIRWISE_V1 implementation order step 2, after two
// independent reviews: GroupState as the single authority, D-072 epochs, D-073 time limit).
// The crypto and random source below are deterministic TEST-ONLY stand-ins: anyone can forge
// these "signatures". They exercise the pair rules, not any algorithm, which is not selected.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
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

// ---- test-only stand-ins ----
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
// Counter-based random source; `repeat` makes the next fill return the previous value.
class TestRandom final : public RandomSource {
public:
    bool repeat{false};
    bool fail{false};
    bool fill(std::uint8_t* out, std::size_t size) override {
        if (fail) return false;
        if (!repeat) ++counter_;
        repeat = false;
        for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>((counter_ >> (8 * (i % 8))) + i + 1);
        return true;
    }
private:
    std::uint64_t counter_{1000};
};
TestRandom rng;

PublicKey key(std::uint8_t marker) {
    PublicKey k{};
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = static_cast<std::uint8_t>(marker * 37 + i);
    return k;
}
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

// A, B (added 2), C (added 3, newer than B).
const Signed v3 = added(added(first(), kB, "bob"), kC, "carol");

// GroupState cannot move, so a Side owns it on the heap and refers to it.
struct Side {
    std::unique_ptr<GroupState> owner;
    GroupState& g;
    PublicKey me;
    PublicKey peer;
};
Outcome offer(Side& s, const Signed& r) { return s.g.offer(r.bytes.data(), r.bytes.size(), crypto); }
Side side(const PublicKey& me, const PublicKey& peer, const Signed& r = v3, bool tap = true) {
    auto owner = std::make_unique<GroupState>();
    GroupState& g = *owner;
    Side s{std::move(owner), g, me, peer};
    EXPECT(s.g.initialize(kA, kGroup, me));
    EXPECT(offer(s, r) == Outcome::accepted);
    if (tap && me == kB && peer == kC) EXPECT(s.g.accept_member(kC));
    return s;
}
PairOutcome start(Side& s, std::uint64_t t, PairStart& out) {
    return s.g.pair_start(s.peer, fresh<32>(), fresh<16>(), t, TestSigner(s.me), rng, out);
}
PairOutcome answer(Side& s, const PairStart& m, std::uint64_t t, PairReply& out) {
    return s.g.pair_on_start(m, fresh<32>(), fresh<16>(), t, crypto, TestSigner(s.me), rng, out);
}
PairOutcome reply_in(Side& s, const PairReply& r, std::uint64_t t, PairConfirm& c, std::optional<VerifiedPairBinding>& v) {
    return s.g.pair_on_reply(s.peer, r, t, crypto, TestSigner(s.me), c, v);
}
PairOutcome confirm_in(Side& s, const PairConfirm& c, std::uint64_t t, std::optional<VerifiedPairBinding>& v) {
    return s.g.pair_on_confirm(s.peer, c, t, crypto, v);
}
PairStatus st(const Side& s) {
    const auto x = s.g.pair_status(s.peer);
    return x ? *x : PairStatus{};
}

struct Run {
    PairOutcome start{}, reply{}, confirm{}, confirmed{};
    std::optional<VerifiedPairBinding> init_verified, resp_verified;
    PairStart s{};
    PairReply r{};
    PairConfirm c{};
};
Run exchange(Side& init, Side& resp, std::uint64_t now = 1000, bool deliver_confirm = true) {
    Run run;
    run.start = start(init, now, run.s);
    if (run.start != PairOutcome::ok) return run;
    run.reply = answer(resp, run.s, now + 10, run.r);
    if (run.reply != PairOutcome::ok) return run;
    run.confirm = reply_in(init, run.r, now + 20, run.c, run.init_verified);
    if (run.confirm != PairOutcome::ok || !deliver_confirm) return run;
    run.confirmed = confirm_in(resp, run.c, now + 30, run.resp_verified);
    return run;
}
bool complete(const Run& r) {
    return r.start == PairOutcome::ok && r.reply == PairOutcome::ok && r.confirm == PairOutcome::ok &&
           r.confirmed == PairOutcome::ok && r.init_verified && r.resp_verified;
}
void activate(Side& s) {
    const std::uint64_t id = st(s).binding_id;
    EXPECT(s.g.pair_activation_possible(s.peer, id) == PairOutcome::ok);
    EXPECT(s.g.pair_commit(s.peer, id) == PairOutcome::ok);
}
PairStart signed_start(PairStart s, const PublicKey& signer) {
    std::array<std::uint8_t, kStartBytes> b{};
    TestSigner(signer).sign(b.data(), start_bytes(s, b.data(), b.size()), s.signature);
    return s;
}
bool gap_ok(const Side& a, const Side& b) {
    const std::uint32_t x = st(a).used_epoch, y = st(b).used_epoch;
    return (x > y ? x - y : y - x) <= 1;
}

// ---------------------------------------------------------------- tests

void test_pairs_only_between_introduced_members() {
    Side b = side(kB, kC);
    PairStart s{};
    EXPECT(b.g.pair_start(kA, fresh<32>(), fresh<16>(), 0, TestSigner(kB), rng, s) == PairOutcome::no_pair);  // admin pair
    EXPECT(b.g.pair_start(kB, fresh<32>(), fresh<16>(), 0, TestSigner(kB), rng, s) == PairOutcome::no_pair);  // itself
    EXPECT(b.g.pair_start(kG, fresh<32>(), fresh<16>(), 0, TestSigner(kB), rng, s) == PairOutcome::no_pair);  // not listed
    Side a = side(kA, kB);
    EXPECT(a.g.pair_start(kB, fresh<32>(), fresh<16>(), 0, TestSigner(kA), rng, s) == PairOutcome::no_pair);
    EXPECT(offer(b, rule(v3, LocationRule::preferred)) == Outcome::accepted);
    EXPECT(offer(b, rule(v3, LocationRule::required)) == Outcome::conflict);
    EXPECT(start(b, 0, s) == PairOutcome::roster_not_active);
    EXPECT(b.g.keep_current());
}

void test_introduce_succeeds_and_binds_everything() {
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(st(c).next_purpose == PairPurpose::introduce);
    Run run = exchange(c, b);
    EXPECT(complete(run));
    if (!run.init_verified || !run.resp_verified) return;
    EXPECT(run.init_verified->epoch() == 1 && run.resp_verified->epoch() == 1);
    EXPECT(run.init_verified->local_role() == PairRole::initiator && run.resp_verified->local_role() == PairRole::responder);
    EXPECT(run.s.roster_digest == b.g.roster().digest() && run.s.lifetime_ms == kPairLifetimeMs);
    EXPECT(run.s.initiator == c.g.roster().fingerprints()[2] && run.s.responder == b.g.roster().fingerprints()[1]);
    EXPECT(run.init_verified->initiator_binding_signature() == run.resp_verified->initiator_binding_signature());
    EXPECT(st(c).step == PairStep::bound && st(b).step == PairStep::bound);
    const std::uint64_t cid = run.init_verified->id();
    EXPECT(cid != 0 && st(c).binding_id == cid);
    EXPECT(c.g.pair_commit(kB, cid) == PairOutcome::wrong_state);              // never before ACTIVATION_POSSIBLE
    EXPECT(c.g.pair_activation_possible(kB, cid + 100) == PairOutcome::wrong_state); // exact binding only
    activate(c);
    activate(b);
    EXPECT(st(c).committed_epoch == 1 && st(b).committed_epoch == 1);
    EXPECT(st(c).next_purpose == PairPurpose::rekey && st(b).history);
}

void test_consent_role_and_history() {
    Side b = side(kB, kC, v3, false), c = side(kC, kB);
    EXPECT(exchange(c, b).reply == PairOutcome::no_consent);
    EXPECT(st(b).used_epoch == 0 && !st(b).history);
    Side b2 = side(kB, kC), c2 = side(kC, kB);
    PairStart s{};
    EXPECT(start(b2, 0, s) == PairOutcome::wrong_role);   // the older member may not start an introduce
    // A forged introduce from the older member, and a rekey without history, are refused.
    PairStart f{};
    f.purpose = PairPurpose::introduce;
    f.group = {c2.g.roster().fingerprints()[0], kGroup};
    f.roster_version = 3; f.roster_digest = c2.g.roster().digest(); f.epoch = 1;
    f.initiator = c2.g.roster().fingerprints()[1]; f.responder = c2.g.roster().fingerprints()[2];
    f.lifetime_ms = kPairLifetimeMs;
    f.initiator_ephemeral = fresh<32>(); f.initiator_challenge = fresh<32>(); f.initiator_boot = fresh<16>();
    PairReply r{};
    EXPECT(answer(c2, signed_start(f, kB), 0, r) == PairOutcome::wrong_role);
    PairStart k = f;
    k.purpose = PairPurpose::rekey;
    std::swap(k.initiator, k.responder);
    EXPECT(answer(b2, signed_start(k, kC), 0, r) == PairOutcome::wrong_purpose);
    EXPECT(!st(b2).history && st(b2).used_epoch == 0);
}

void test_removal_and_readd_end_the_pair() {
    // Review 1 probe B / review 2 probe B: nothing finishes or commits after removal.
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b, 1000, false);
    EXPECT(run.confirm == PairOutcome::ok);
    const Signed v4 = removed(v3, kC);
    EXPECT(offer(b, v4) == Outcome::accepted);
    std::optional<VerifiedPairBinding> v;
    EXPECT(confirm_in(b, run.c, 1040, v) == PairOutcome::no_pair && !v);
    EXPECT(!b.g.pair_status(kC).has_value());
    // A bound pair cannot activate or commit after the peer is removed.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(complete(exchange(c2, b2)));
    const std::uint64_t id2 = st(b2).binding_id;
    EXPECT(offer(b2, v4) == Outcome::accepted);
    EXPECT(b2.g.pair_activation_possible(kC, id2) == PairOutcome::no_pair);
    EXPECT(b2.g.pair_commit(kC, id2) == PairOutcome::no_pair);
    // Re-added: a new membership, a new record, a new tap (review 1 probe D).
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(complete(exchange(c3, b3)));
    activate(c3);
    activate(b3);
    const Signed v5 = added(v4, kC, "carol");
    EXPECT(offer(b3, v4) == Outcome::accepted);
    EXPECT(offer(b3, v5) == Outcome::accepted);
    Side c5 = side(kC, kB, v5);
    EXPECT(!b3.g.roster().accepted(kC));
    EXPECT(exchange(c5, b3).reply == PairOutcome::no_consent);
    EXPECT(b3.g.accept_member(kC));
    Run again = exchange(c5, b3, 200000);
    EXPECT(complete(again) && again.s.epoch == 1 && again.s.purpose == PairPurpose::introduce);
    // Review 3 probe D: the old membership's binding id cannot select the new binding.
    Side c6 = side(kC, kB), b6 = side(kB, kC);
    Run old_run = exchange(c6, b6);
    EXPECT(complete(old_run));
    const std::uint64_t old_id = old_run.resp_verified->id();
    EXPECT(offer(b6, v4) == Outcome::accepted);
    EXPECT(offer(b6, v5) == Outcome::accepted);
    EXPECT(b6.g.accept_member(kC));
    Side c7 = side(kC, kB, v5);
    Run new_run = exchange(c7, b6, 300000);
    EXPECT(complete(new_run) && new_run.s.epoch == old_run.s.epoch);
    EXPECT(new_run.resp_verified->id() != old_id);
    EXPECT(b6.g.pair_activation_possible(kC, old_id) == PairOutcome::wrong_state);
    EXPECT(b6.g.pair_activation_possible(kC, new_run.resp_verified->id()) == PairOutcome::ok);
}

void test_roster_change_drops_attempts_and_blocks_activation() {
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b);
    EXPECT(complete(run));
    const Signed v4 = rule(v3, LocationRule::preferred);
    const std::uint64_t id = st(b).binding_id;
    EXPECT(offer(b, v4) == Outcome::accepted);
    EXPECT(st(b).step == PairStep::none);                                    // bound attempt dropped
    EXPECT(b.g.pair_activation_possible(kC, id) == PairOutcome::wrong_state);
    // ACTIVATION_POSSIBLE survives a roster change but cannot commit.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(complete(exchange(c2, b2)));
    const std::uint64_t id2 = st(b2).binding_id;
    EXPECT(b2.g.pair_activation_possible(kC, id2) == PairOutcome::ok);
    EXPECT(offer(b2, v4) == Outcome::accepted);
    EXPECT(b2.g.pair_commit(kC, id2) == PairOutcome::roster_changed);
    // A reply under the old roster is refused.
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(start(c3, 0, s) == PairOutcome::ok);
    EXPECT(answer(b3, s, 10, r) == PairOutcome::ok);
    EXPECT(offer(c3, v4) == Outcome::accepted);
    EXPECT(reply_in(c3, r, 20, conf, v) == PairOutcome::wrong_state && !v);  // attempt dropped on apply
}

// Review 2 probe D and review 4: a GroupState never moves (no second owner, no swap from a
// callback), and a binding id names one binding in the whole program.
static_assert(!std::is_copy_constructible_v<GroupState> && !std::is_copy_assignable_v<GroupState>);
static_assert(!std::is_move_constructible_v<GroupState> && !std::is_move_assignable_v<GroupState>);

Side other_group_side(const PublicKey& me, const PublicKey& peer) {
    Roster other{};
    EXPECT(create_first(kGroup + 1, kA, "admin", LocationRule::off, other));
    const Signed other_v3 = added(added(seal(other, kA), kB, "bob"), kC, "carol");
    auto owner = std::make_unique<GroupState>();
    GroupState& g = *owner;
    Side s{std::move(owner), g, me, peer};
    EXPECT(s.g.initialize(kA, kGroup + 1, me));
    EXPECT(offer(s, other_v3) == Outcome::accepted);
    if (me == kB) EXPECT(s.g.accept_member(kC));
    return s;
}

void test_group_state_stays_in_place_and_ids_are_unique() {
    // Two groups with the same peer: their first bindings get different ids, and neither id
    // works in the other group (review 4 probe D).
    Side xc = side(kC, kB), xb = side(kB, kC);
    Side yc = other_group_side(kC, kB), yb = other_group_side(kB, kC);
    const Run x = exchange(xc, xb, 0);
    const Run y = exchange(yc, yb, 100);
    EXPECT(complete(x) && complete(y));
    const std::uint64_t xid = x.resp_verified->id(), yid = y.resp_verified->id();
    EXPECT(xid != yid && x.init_verified->id() != y.init_verified->id());
    EXPECT(yb.g.pair_activation_possible(kC, xid) == PairOutcome::wrong_state);
    EXPECT(yb.g.pair_activation_possible(kC, yid) == PairOutcome::ok);
    // A group state created later in the same place does not accept an id from before.
    std::optional<GroupState> slot;
    slot.emplace();
    EXPECT(slot->initialize(kA, kGroup, kB));
    EXPECT(slot->offer(v3.bytes.data(), v3.bytes.size(), crypto) == Outcome::accepted);
    EXPECT(slot->accept_member(kC));
    Side c = side(kC, kB);
    PairStart s{};
    PairReply r{};
    PairConfirm f{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(start(c, 0, s) == PairOutcome::ok);
    EXPECT(slot->pair_on_start(s, fresh<32>(), fresh<16>(), 10, crypto, TestSigner(kB), rng, r) == PairOutcome::ok);
    EXPECT(reply_in(c, r, 20, f, v) == PairOutcome::ok && v);
    EXPECT(slot->pair_on_confirm(kC, f, 30, crypto, v) == PairOutcome::ok && v);
    const std::uint64_t old_id = v->id();
    slot.reset();
    slot.emplace();
    EXPECT(slot->initialize(kA, kGroup, kB));
    EXPECT(slot->offer(v3.bytes.data(), v3.bytes.size(), crypto) == Outcome::accepted);
    EXPECT(slot->accept_member(kC));
    Side c2 = side(kC, kB);  // a new first pairing with the new group state
    EXPECT(start(c2, 1000, s) == PairOutcome::ok);
    EXPECT(slot->pair_on_start(s, fresh<32>(), fresh<16>(), 1010, crypto, TestSigner(kB), rng, r) == PairOutcome::ok);
    EXPECT(reply_in(c2, r, 1020, f, v) == PairOutcome::ok && v);
    EXPECT(slot->pair_on_confirm(kC, f, 1030, crypto, v) == PairOutcome::ok && v && v->id() != old_id);
    EXPECT(slot->pair_activation_possible(kC, old_id) == PairOutcome::wrong_state);
}

void test_retired_attempt_and_signer_failure() {
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b, 10, false);
    EXPECT(run.confirm == PairOutcome::ok);
    b.g.restart();
    std::optional<VerifiedPairBinding> v;
    EXPECT(confirm_in(b, run.c, 40, v) == PairOutcome::wrong_state && !v);
    EXPECT(st(b).used_epoch == 0);
    // A signer failure on a new answer changes nothing; the earlier answer still completes.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    Run first_run = exchange(c2, b2, 0, false);
    EXPECT(first_run.confirm == PairOutcome::ok);
    PairStart other = first_run.s;
    other.initiator_challenge = fresh<32>();
    PairReply r2{};
    EXPECT(b2.g.pair_on_start(signed_start(other, kC), fresh<32>(), fresh<16>(), 20, crypto, FailingSigner(kB), rng, r2) ==
           PairOutcome::crypto_failure);
    EXPECT(confirm_in(b2, first_run.c, 30, v) == PairOutcome::ok && v && v->epoch() == 1);
}

void test_signer_is_pinned() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(start(c, 10, s) == PairOutcome::ok);
    EXPECT(answer(b, s, 11, r) == PairOutcome::ok);
    EXPECT(c.g.pair_on_reply(kB, r, 12, crypto, TestSigner(kX), conf, v) == PairOutcome::wrong_signer && !v);
    EXPECT(reply_in(c, r, 13, conf, v) == PairOutcome::ok && v);
    PairStart s2{};
    EXPECT(c.g.pair_start(kB, fresh<32>(), fresh<16>(), 20, TestSigner(kX), rng, s2) == PairOutcome::wrong_signer);
    EXPECT(b.g.pair_on_start(s, fresh<32>(), fresh<16>(), 21, crypto, TestSigner(kX), rng, r) == PairOutcome::wrong_signer);
}

void test_replayed_starts_cannot_displace_the_live_answer() {
    // Review 2 probe A, both orders: old start before and after the live one.
    for (bool old_first : {true, false}) {
        Side c = side(kC, kB), b = side(kB, kC);
        PairStart old{}, live{};
        PairReply r_old{}, r_live{};
        EXPECT(start(c, 0, old) == PairOutcome::ok);
        EXPECT(start(c, 100000, live) == PairOutcome::ok && live.epoch == old.epoch);
        if (old_first) EXPECT(answer(b, old, 100000, r_old) == PairOutcome::ok);
        EXPECT(answer(b, live, 100001, r_live) == PairOutcome::ok);
        if (!old_first) EXPECT(answer(b, old, 100002, r_old) == PairOutcome::ok);
        PairConfirm conf{};
        std::optional<VerifiedPairBinding> v;
        EXPECT(reply_in(c, r_old, 100003, conf, v) == PairOutcome::bad_signature && !v);
        EXPECT(reply_in(c, r_live, 100004, conf, v) == PairOutcome::ok && v);
        v.reset();
        EXPECT(confirm_in(b, conf, 100005, v) == PairOutcome::ok && v);
        EXPECT(st(b).used_epoch == 1 && st(c).used_epoch == 1);
    }
}

void test_budgets_and_cached_replies() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r1{}, r2{};
    EXPECT(start(c, 0, s) == PairOutcome::ok);
    EXPECT(answer(b, s, 10, r1) == PairOutcome::ok);
    for (std::size_t i = 0; i < kResendsPerWindow; ++i) {
        EXPECT(answer(b, s, 20 + i, r2) == PairOutcome::resent_reply && r2.signature == r1.signature);
    }
    EXPECT(answer(b, s, 30, r2) == PairOutcome::rate_limited);   // resends are budgeted too
    EXPECT(answer(b, s, 10 + kAnswerWindowMs + 30, r2) == PairOutcome::resent_reply);
    // Distinct starts: three new answers per window.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    std::size_t ok = 0;
    PairOutcome last = PairOutcome::ok;
    for (int i = 0; i < 4; ++i) {
        PairStart x{};
        PairReply rr{};
        EXPECT(start(c2, 100 + i, x) == PairOutcome::ok);
        last = answer(b2, x, 100 + i, rr);
        if (last == PairOutcome::ok) ++ok;
    }
    EXPECT(ok == kAnswersPerWindow && last == PairOutcome::rate_limited);
    // A cached reply is never resent under a newer roster (review 2 probe A).
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    PairStart s3{};
    PairReply r3{};
    EXPECT(start(c3, 0, s3) == PairOutcome::ok);
    EXPECT(answer(b3, s3, 1, r3) == PairOutcome::ok);
    EXPECT(offer(b3, rule(v3, LocationRule::preferred)) == Outcome::accepted);
    EXPECT(answer(b3, s3, 2, r3) == PairOutcome::duplicate_start);
}

void test_time_limit_per_side() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(start(c, 0, s) == PairOutcome::ok);
    EXPECT(answer(b, s, 1000, r) == PairOutcome::ok);
    EXPECT(reply_in(c, r, kPairLifetimeMs, conf, v) == PairOutcome::ok && v);   // exactly 60 s
    v.reset();
    EXPECT(confirm_in(b, conf, 1000 + kPairLifetimeMs + 1, v) == PairOutcome::wrong_state && !v);  // expired
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(start(c2, 500, s) == PairOutcome::ok);
    EXPECT(answer(b2, s, 600, r) == PairOutcome::ok);
    EXPECT(reply_in(c2, r, 500 + kPairLifetimeMs + 1, conf, v) == PairOutcome::expired);
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(start(c3, 5000, s) == PairOutcome::ok);
    EXPECT(answer(b3, s, UINT64_MAX - 10, r) == PairOutcome::ok);
    EXPECT(reply_in(c3, r, 4999, conf, v) == PairOutcome::expired);             // clock regression
    PairStart long_life = s;
    long_life.lifetime_ms = kPairLifetimeMs + 1;
    EXPECT(answer(b3, signed_start(long_life, kC), 0, r) == PairOutcome::bad_lifetime);
}

void test_epochs_converge_under_repeated_loss() {
    // Review 2 probe C: two lost confirms in a row, then any number more.
    Side c = side(kC, kB), b = side(kB, kC);
    for (std::uint32_t round = 1; round <= 4; ++round) {
        Run lost = exchange(c, b, round * 100000, false);
        EXPECT(lost.confirm == PairOutcome::ok);
        EXPECT(gap_ok(c, b));
        c.g.restart();
        b.g.restart();
    }
    EXPECT(st(c).used_epoch == 4 && st(b).used_epoch == 3);
    Run done = exchange(c, b, 900000);
    EXPECT(complete(done) && done.s.epoch == 5 && st(b).used_epoch == 5);
    // Lost replies use nothing; the retry reuses the number.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    PairStart s{};
    PairReply r{};
    EXPECT(start(c2, 0, s) == PairOutcome::ok);
    EXPECT(answer(b2, s, 10, r) == PairOutcome::ok);
    Run retry = exchange(c2, b2, 70000);
    EXPECT(complete(retry) && retry.s.epoch == 1);
    // Equal, older and a skip of two are refused; one past the next is a catch-up.
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    PairStart base{};
    EXPECT(start(c3, 0, base) == PairOutcome::ok);
    for (std::uint32_t e : {0U, 3U, 0xFFFFFFFFU}) {
        PairStart x = base;
        x.epoch = e;
        EXPECT(answer(b3, signed_start(x, kC), 10, r) == PairOutcome::bad_epoch);
    }
    PairStart two = base;
    two.epoch = 2;
    EXPECT(answer(b3, signed_start(two, kC), 10, r) == PairOutcome::ok && st(b3).used_epoch == 1);
}

void test_crossing_starts() {
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(complete(exchange(c, b)));
    activate(c);
    activate(b);
    PairStart sb{}, sc{};
    PairReply r{};
    EXPECT(start(b, 0, sb) == PairOutcome::ok);
    EXPECT(start(c, 0, sc) == PairOutcome::ok);
    const bool b_wins = start_wins(sb, sc);
    Side& winner = b_wins ? b : c;
    Side& loser = b_wins ? c : b;
    EXPECT(answer(winner, b_wins ? sc : sb, 10, r) == PairOutcome::local_attempt_wins);
    EXPECT(answer(loser, b_wins ? sb : sc, 10, r) == PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(reply_in(winner, r, 20, conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(confirm_in(loser, conf, 30, v) == PairOutcome::ok && v && v->epoch() == 2);
    // An expired own start does not win (review 2 probe C).
    activate(winner);
    activate(loser);
    PairStart lost{}, live{};
    EXPECT(start(winner, 1000, lost) == PairOutcome::ok);
    EXPECT(start(loser, 1000 + kPairLifetimeMs + 1, live) == PairOutcome::ok);
    EXPECT(lost.epoch == live.epoch);
    EXPECT(answer(winner, live, 1000 + kPairLifetimeMs + 10, r) == PairOutcome::ok);
}

void test_introduce_beats_rekey_when_history_differs() {
    // Review 2 probe C: the older board answered a first start whose reply was lost; it has
    // history and starts a rekey, the newer board has none and starts an introduce.
    Side older = side(kB, kC), newer = side(kC, kB);
    PairStart first_start{}, rekey{}, intro{};
    PairReply lost{}, r{};
    EXPECT(start(newer, 0, first_start) == PairOutcome::ok);
    EXPECT(answer(older, first_start, 10, lost) == PairOutcome::ok);
    older.g.restart();
    newer.g.restart();
    EXPECT(start(older, 70000, rekey) == PairOutcome::ok && rekey.purpose == PairPurpose::rekey);
    EXPECT(start(newer, 70000, intro) == PairOutcome::ok && intro.purpose == PairPurpose::introduce);
    EXPECT(answer(newer, rekey, 70010, r) == PairOutcome::wrong_purpose);
    EXPECT(answer(older, intro, 70010, r) == PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(reply_in(newer, r, 70020, conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(confirm_in(older, conf, 70030, v) == PairOutcome::ok && v);
}

void test_activation_intent_and_recovery() {
    // Review 3: a start alone never retires activation intent; a new verified binding does.
    Side c = side(kC, kB), b = side(kB, kC);
    EXPECT(complete(exchange(c, b, 0)));
    activate(c);
    const std::uint64_t id1 = st(b).binding_id;
    EXPECT(b.g.pair_activation_possible(kC, id1) == PairOutcome::ok);
    PairStart archived{};
    EXPECT(start(c, 100, archived) == PairOutcome::ok && archived.epoch == 2);
    c.g.pair_cancel(kB);
    PairReply r{};
    EXPECT(answer(b, archived, 100 + kPairLifetimeMs + 1, r) == PairOutcome::ok);
    EXPECT(st(b).step == PairStep::awaiting_confirm && st(b).binding == PairStep::activation_possible);
    EXPECT(b.g.pair_commit(kC, id1) == PairOutcome::ok);   // the intent survived the delayed start
    EXPECT(st(b).committed_epoch == 1);
    // A newly verified binding replaces the intent.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    EXPECT(complete(exchange(c2, b2, 0)));
    activate(c2);
    EXPECT(b2.g.pair_activation_possible(kC, st(b2).binding_id) == PairOutcome::ok);
    Run two = exchange(c2, b2, 70000);
    EXPECT(complete(two) && two.s.epoch == 2 && st(b2).used_epoch == 2 && st(b2).binding == PairStep::bound);
    activate(c2);
    activate(b2);
    EXPECT(st(b2).committed_epoch == 2);
    // Both boards uncertain: either may start the recovery rekey (review 3 suspected risk).
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(complete(exchange(c3, b3, 0)));
    EXPECT(c3.g.pair_activation_possible(kB, st(c3).binding_id) == PairOutcome::ok);
    EXPECT(b3.g.pair_activation_possible(kC, st(b3).binding_id) == PairOutcome::ok);
    Run three = exchange(b3, c3, 100000);
    EXPECT(complete(three) && three.s.epoch == 2 && three.s.purpose == PairPurpose::rekey);
}

void test_hints_after_signature_and_pair_naming() {
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart s{};
    PairReply r{};
    EXPECT(start(c, 0, s) == PairOutcome::ok);
    PairStart forged = s;
    ++forged.roster_version;
    EXPECT(answer(b, forged, 0, r) == PairOutcome::bad_signature);
    forged = s;
    forged.roster_digest[0] ^= 1;
    EXPECT(answer(b, forged, 0, r) == PairOutcome::bad_signature);
    PairStart x = s;
    x.responder = b.g.roster().fingerprints()[0];
    EXPECT(answer(b, signed_start(x, kC), 0, r) == PairOutcome::not_for_this_pair);
    x = s;
    x.group.admin[0] ^= 1;
    EXPECT(answer(b, signed_start(x, kC), 0, r) == PairOutcome::not_for_this_pair);
    x = s;
    x.group.group_id ^= 1;
    EXPECT(answer(b, signed_start(x, kC), 0, r) == PairOutcome::not_for_this_pair);
    x = s;
    x.initiator = b.g.roster().fingerprints()[0];   // names the admin: no introduced pair
    EXPECT(answer(b, signed_start(x, kC), 0, r) == PairOutcome::no_pair);
    EXPECT(st(b).used_epoch == 0);
    const Signed v4 = rule(v3, LocationRule::preferred);
    const Signed v4b = rule(v3, LocationRule::required);
    Side c4 = side(kC, kB, v4), c4b = side(kC, kB, v4b), b4 = side(kB, kC, v4);
    EXPECT(exchange(c4, b).reply == PairOutcome::peer_roster_newer);
    EXPECT(exchange(c, b4).reply == PairOutcome::peer_roster_older);
    EXPECT(exchange(c4b, b4).reply == PairOutcome::peer_roster_differs);
    EXPECT(complete(exchange(c4, b4, 200000)));
}

void test_challenges_come_from_the_random_source() {
    // Review 2 probe A: an old reply cannot verify a recreated start.
    Side c = side(kC, kB), b = side(kB, kC);
    PairStart old{}, again{};
    PairReply old_reply{};
    EXPECT(start(c, 0, old) == PairOutcome::ok);
    EXPECT(answer(b, old, 1, old_reply) == PairOutcome::ok);
    c.g.pair_cancel(kB);
    for (int i = 0; i < 6; ++i) {
        PairStart x{};
        EXPECT(start(c, 2 + i, x) == PairOutcome::ok);
        c.g.pair_cancel(kB);
    }
    EXPECT(start(c, 70000, again) == PairOutcome::ok && again.epoch == old.epoch);
    EXPECT(again.initiator_challenge != old.initiator_challenge);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(reply_in(c, old_reply, 70001, conf, v) == PairOutcome::bad_signature && !v);
    // A repeated or failing random source is refused.
    PairStart y{};
    rng.repeat = true;
    EXPECT(start(c, 70002, y) == PairOutcome::crypto_failure);
    rng.fail = true;
    EXPECT(start(c, 70003, y) == PairOutcome::crypto_failure);
    rng.fail = false;
    EXPECT(start(c, 70004, y) == PairOutcome::ok);
    // Reflection by a reply.
    PairReply reflected{};
    EXPECT(answer(b, y, 70005, reflected) == PairOutcome::ok);
    reflected.responder_challenge = y.initiator_challenge;
    std::array<std::uint8_t, kBindingBytes> bb{};
    TestSigner(kB).sign(bb.data(), binding_bytes(y, reflected, bb.data(), bb.size()), reflected.signature);
    EXPECT(reply_in(c, reflected, 70006, conf, v) == PairOutcome::bad_fresh_values && !v);
    // Zero ephemeral or boot token.
    Ephemeral zero{};
    EXPECT(c.g.pair_start(kB, zero, fresh<16>(), 70007, TestSigner(kC), rng, y) == PairOutcome::bad_fresh_values);
}

void test_outputs_never_stale() {
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b);
    EXPECT(complete(run));
    std::optional<VerifiedPairBinding> v = run.resp_verified;
    EXPECT(confirm_in(b, run.c, 2000, v) == PairOutcome::wrong_state && !v);
    std::optional<VerifiedPairBinding> w = run.init_verified;
    PairConfirm conf{};
    EXPECT(reply_in(c, run.r, 2000, conf, w) == PairOutcome::wrong_state && !w);
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
    EXPECT(sb[c + 33] == 0x01 && sb[c + 40] == 0xEF && sb[c + 44] == 7);
    const std::size_t e = c + 1 + 32 + 8 + 4 + 32;
    EXPECT(sb[e] == 1 && sb[e + 1] == 2 && sb[e + 2] == 3 && sb[e + 3] == 4);
    auto changed = [&](auto mutate) {
        PairStart t = s;
        mutate(t);
        std::array<std::uint8_t, kStartBytes> tb{};
        start_bytes(t, tb.data(), tb.size());
        return tb != sb;
    };
    EXPECT(changed([](PairStart& t) { t.purpose = PairPurpose::introduce; }));
    EXPECT(changed([](PairStart& t) { t.group.admin[5] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.group.group_id ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.roster_version += 1; }));
    EXPECT(changed([](PairStart& t) { t.roster_digest[9] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.epoch += 1; }));
    EXPECT(changed([](PairStart& t) { t.initiator[3] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.responder[3] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.lifetime_ms += 1; }));
    EXPECT(changed([](PairStart& t) { t.initiator_ephemeral[0] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.initiator_challenge[0] ^= 1; }));
    EXPECT(changed([](PairStart& t) { t.initiator_boot[0] ^= 1; }));
    for (int field = 0; field < 3; ++field) {
        PairReply u = r;
        if (field == 0) u.responder_ephemeral[0] ^= 1;
        if (field == 1) u.responder_challenge[0] ^= 1;
        if (field == 2) u.responder_boot[0] ^= 1;
        std::array<std::uint8_t, kBindingBytes> ub{};
        binding_bytes(s, u, ub.data(), ub.size());
        EXPECT(ub != bb);
    }
    // A start signature is not a binding signature, and a binding cannot be transplanted.
    PairStart signed_s = signed_start(s, kC);
    EXPECT(!crypto.verify(signed_s.signature, bb.data(), bb.size(), kC));
    EXPECT(kStartDomain != kBindingDomain && kBindingDomain != kSignatureDomain);
}


void test_forged_confirm_and_initiator_restart() {
    // A confirm not signed by the peer over this binding is refused; the answer stays pending.
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b, 0, false);
    EXPECT(run.confirm == PairOutcome::ok);
    PairConfirm forged = run.c;
    forged.signature[0] ^= 1;
    std::optional<VerifiedPairBinding> v;
    EXPECT(confirm_in(b, forged, 40, v) == PairOutcome::bad_signature && !v);
    PairConfirm by_other{};
    std::array<std::uint8_t, kBindingBytes> bb{};
    TestSigner(kX).sign(bb.data(), binding_bytes(run.s, run.r, bb.data(), bb.size()), by_other.signature);
    EXPECT(confirm_in(b, by_other, 41, v) == PairOutcome::bad_signature && !v);
    EXPECT(st(b).step == PairStep::awaiting_confirm);
    EXPECT(confirm_in(b, run.c, 42, v) == PairOutcome::ok && v);
    // Restart retires the initiator's attempts too: an awaited reply, and a bound binding.
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    PairStart s{};
    PairReply r{};
    PairConfirm conf{};
    EXPECT(start(c2, 0, s) == PairOutcome::ok);
    EXPECT(answer(b2, s, 10, r) == PairOutcome::ok);
    c2.g.restart();
    EXPECT(reply_in(c2, r, 20, conf, v) == PairOutcome::wrong_state && !v);
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    Run bound = exchange(c3, b3, 0, false);
    EXPECT(bound.confirm == PairOutcome::ok && st(c3).step == PairStep::bound);
    c3.g.restart();
    EXPECT(st(c3).step == PairStep::none && st(c3).used_epoch == 1);
    EXPECT(c3.g.pair_activation_possible(kB, bound.init_verified->id()) == PairOutcome::wrong_state);
}

class RosterChangingSigner final : public RosterSigner {
public:
    RosterChangingSigner(Side& side, const Signed& next) : side_(side), next_(next) {}
    const PublicKey& public_key() const override { return side_.me; }
    bool sign(const std::uint8_t* m, std::size_t n, Signature& s) const override {
        result = offer(side_, next_);
        return TestSigner(side_.me).sign(m, n, s);
    }
    mutable Outcome result{Outcome::accepted};
private:
    Side& side_;
    const Signed& next_;
};

void test_review3_fixes() {
    // Old starts cannot evict a live answer, in either window (probe A).
    for (bool live_first : {true, false}) {
        Side c = side(kC, kB), b = side(kB, kC);
        std::array<PairStart, 3> archived{};
        for (std::size_t i = 0; i < archived.size(); ++i) EXPECT(start(c, i, archived[i]) == PairOutcome::ok);
        PairStart live{};
        PairReply reply{}, old_reply{};
        PairConfirm confirm{};
        std::optional<VerifiedPairBinding> v;
        EXPECT(start(c, 100000, live) == PairOutcome::ok);
        if (live_first) {
            EXPECT(answer(b, live, 100010, reply) == PairOutcome::ok);
            EXPECT(reply_in(c, reply, 100020, confirm, v) == PairOutcome::ok && v);
        }
        std::size_t answered = 0;
        for (std::size_t i = 0; i < archived.size(); ++i) {
            if (answer(b, archived[i], 105010 + i, old_reply) == PairOutcome::ok) ++answered;
        }
        if (live_first) {
            EXPECT(answered == kAnswersPerWindow - 1);   // two free slots; the live answer keeps its own
            EXPECT(confirm_in(b, confirm, 105020, v) == PairOutcome::ok && v);
        } else {
            EXPECT(answered == kAnswersPerWindow);
            EXPECT(answer(b, live, 105020, reply) == PairOutcome::rate_limited);  // full: wait for a slot to expire
            EXPECT(answer(b, live, 105010 + kPairLifetimeMs + 1, reply) == PairOutcome::ok);
        }
    }
    // Cancel on the responder retires its pending answers (probe A).
    Side c = side(kC, kB), b = side(kB, kC);
    Run run = exchange(c, b, 0, false);
    EXPECT(run.confirm == PairOutcome::ok && st(b).step == PairStep::awaiting_confirm);
    b.g.pair_cancel(kC);
    std::optional<VerifiedPairBinding> v;
    EXPECT(st(b).step == PairStep::none);
    EXPECT(confirm_in(b, run.c, 30, v) == PairOutcome::wrong_state && !v);
    EXPECT(st(b).used_epoch == 0);
    // A provider cannot change the roster while a pair step runs (probe B).
    Side c2 = side(kC, kB), b2 = side(kB, kC);
    const Signed v4 = rule(v3, LocationRule::preferred);
    PairStart s{};
    PairReply r{};
    PairConfirm f{};
    EXPECT(start(c2, 0, s) == PairOutcome::ok);
    EXPECT(answer(b2, s, 10, r) == PairOutcome::ok);
    RosterChangingSigner nested(c2, v4);
    EXPECT(c2.g.pair_on_reply(kB, r, 20, crypto, nested, f, v) == PairOutcome::ok && v);
    EXPECT(nested.result == Outcome::busy && c2.g.roster().roster().version == 3);
    EXPECT(v->epoch() == 1 && v->start().roster_version == 3);
    EXPECT(c2.g.pair_activation_possible(kB, v->id()) == PairOutcome::ok);
    // Stop trusting ends every pair record (probe B).
    Side c3 = side(kC, kB), b3 = side(kB, kC);
    EXPECT(complete(exchange(c3, b3)));
    activate(b3);
    b3.g.stop_trusting();
    EXPECT(!b3.g.pair_status(kC).has_value());
    EXPECT(b3.g.pair_commit(kC, 1) == PairOutcome::no_pair);
    // A conflict drops unfinished work; keeping the roster does not resume it (probe B).
    const Signed held = rule(v3, LocationRule::preferred);
    const Signed fork = rule(v3, LocationRule::required);
    Side c4 = side(kC, kB, held), b4 = side(kB, kC, held);
    Run pending = exchange(c4, b4, 0, false);
    EXPECT(pending.confirm == PairOutcome::ok);
    EXPECT(offer(b4, fork) == Outcome::conflict);
    EXPECT(b4.g.keep_current());
    EXPECT(confirm_in(b4, pending.c, 40, v) == PairOutcome::wrong_state && !v);
    // An ACTIVATION_POSSIBLE binding is kept through a conflict and may commit after keep_current.
    Side c5 = side(kC, kB, held), b5 = side(kB, kC, held);
    EXPECT(complete(exchange(c5, b5)));
    const std::uint64_t id5 = st(b5).binding_id;
    EXPECT(b5.g.pair_activation_possible(kC, id5) == PairOutcome::ok);
    EXPECT(offer(b5, fork) == Outcome::conflict);
    EXPECT(b5.g.pair_commit(kC, id5) == PairOutcome::roster_not_active);
    EXPECT(b5.g.keep_current());
    EXPECT(b5.g.pair_commit(kC, id5) == PairOutcome::ok);
    // Binding ids: commit needs the exact id; each binding gets its own id on both sides;
    // restart retires activation intent.
    Side c7 = side(kC, kB), b7 = side(kB, kC);
    Run first7 = exchange(c7, b7, 0);
    EXPECT(complete(first7));
    const std::uint64_t i1 = first7.init_verified->id();
    EXPECT(c7.g.pair_activation_possible(kB, i1) == PairOutcome::ok);
    EXPECT(c7.g.pair_commit(kB, i1 + 1) == PairOutcome::wrong_state);
    EXPECT(c7.g.pair_commit(kB, i1) == PairOutcome::ok);
    activate(b7);
    Run second7 = exchange(c7, b7, 100000);
    EXPECT(complete(second7) && second7.init_verified->id() != i1);
    EXPECT(c7.g.pair_activation_possible(kB, second7.init_verified->id()) == PairOutcome::ok);
    c7.g.restart();
    EXPECT(st(c7).binding == PairStep::none);
    EXPECT(c7.g.pair_commit(kB, second7.init_verified->id()) == PairOutcome::wrong_state);
    // The answer budget holds when a higher epoch frees the slots inside one window.
    Side c8 = side(kC, kB), b8 = side(kB, kC);
    PairStart base8{};
    EXPECT(start(c8, 0, base8) == PairOutcome::ok);
    for (int i = 0; i < 3; ++i) {
        PairStart x{};
        EXPECT(start(c8, 1 + i, x) == PairOutcome::ok);
        EXPECT(answer(b8, x, 10 + i, r) == PairOutcome::ok);
    }
    PairStart higher = base8;
    higher.epoch = 2;
    EXPECT(answer(b8, signed_start(higher, kC), 20, r) == PairOutcome::rate_limited);
    // A binding overtaken by a catch-up cannot be activated.
    Side c9 = side(kC, kB), b9 = side(kB, kC);
    Run r9 = exchange(c9, b9, 0);
    EXPECT(complete(r9));
    PairStart jump = r9.s;
    jump.purpose = PairPurpose::rekey;
    jump.epoch = 3;
    EXPECT(answer(b9, signed_start(jump, kC), 100000, r) == PairOutcome::ok && st(b9).used_epoch == 2);
    EXPECT(b9.g.pair_activation_possible(kC, r9.resp_verified->id()) == PairOutcome::wrong_state);
    // D-074: a dishonest peer's signed claims move only its own pair's numbers.
    Side c6 = side(kC, kB), b6 = side(kB, kC);
    PairStart one{};
    EXPECT(start(c6, 0, one) == PairOutcome::ok);
    PairStart claim = one;
    claim.epoch = 2;
    EXPECT(answer(b6, signed_start(claim, kC), 10, r) == PairOutcome::ok && st(b6).used_epoch == 1);
    // The lying board's own next start (epoch 1) no longer fits: only its own pair is stuck.
    Run after = exchange(c6, b6, 100000);
    EXPECT(after.reply == PairOutcome::bad_epoch && st(b6).used_epoch == 1);
}

// Both signatures of an issued binding check out over its own contents.
bool matches_signatures(const VerifiedPairBinding& binding) {
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const auto n = binding_bytes(binding.start(), binding.reply(), bytes.data(), bytes.size());
    Fingerprint ifp{};
    PublicKey ik{}, rk{};
    for (const PublicKey* k : {&kB, &kC, &kD}) {
        if (crypto.fingerprint(*k, ifp) && ifp == binding.start().initiator) ik = *k;
        if (crypto.fingerprint(*k, ifp) && ifp == binding.start().responder) rk = *k;
    }
    return n != 0 && crypto.verify(binding.reply().signature, bytes.data(), n, rk) &&
           crypto.verify(binding.initiator_binding_signature(), bytes.data(), n, ik);
}

// A provider changes the caller's own message object while the step runs.
class ReplyChangingSigner final : public RosterSigner {
public:
    explicit ReplyChangingSigner(PairReply& input) : input_(input) {}
    const PublicKey& public_key() const override { return kC; }
    bool sign(const std::uint8_t* m, std::size_t n, Signature& s) const override {
        input_.responder_challenge[0] ^= 1;
        return TestSigner(kC).sign(m, n, s);
    }
private:
    PairReply& input_;
};
class StartChangingSigner final : public RosterSigner {
public:
    explicit StartChangingSigner(PairStart& input) : input_(input) {}
    const PublicKey& public_key() const override { return kB; }
    bool sign(const std::uint8_t* m, std::size_t n, Signature& s) const override {
        input_.initiator_challenge[0] ^= 1;
        return TestSigner(kB).sign(m, n, s);
    }
private:
    PairStart& input_;
};

void test_review4_fixes() {
    // A callback changing the caller's reply cannot change what is issued (probe B).
    {
        Side c = side(kC, kB), b = side(kB, kC);
        PairStart s{};
        PairReply r{};
        PairConfirm f{};
        std::optional<VerifiedPairBinding> v, w;
        EXPECT(start(c, 0, s) == PairOutcome::ok);
        EXPECT(answer(b, s, 10, r) == PairOutcome::ok);
        EXPECT(c.g.pair_on_reply(kB, r, 20, crypto, ReplyChangingSigner(r), f, v) == PairOutcome::ok && v);
        EXPECT(v && matches_signatures(*v));
        EXPECT(confirm_in(b, f, 30, w) == PairOutcome::ok && w && matches_signatures(*w));
        EXPECT(v && w && v->reply().responder_challenge == w->reply().responder_challenge);
    }
    // A callback changing the caller's start on the responder cannot change what is kept.
    {
        Side c = side(kC, kB), b = side(kB, kC);
        PairStart s{}, held{};
        PairReply r{};
        PairConfirm f{};
        std::optional<VerifiedPairBinding> v, w;
        EXPECT(start(c, 0, s) == PairOutcome::ok);
        held = s;
        EXPECT(b.g.pair_on_start(held, fresh<32>(), fresh<16>(), 10, crypto, StartChangingSigner(held), rng, r) ==
               PairOutcome::ok);
        EXPECT(held.initiator_challenge != s.initiator_challenge);
        EXPECT(reply_in(c, r, 20, f, v) == PairOutcome::ok && v);
        EXPECT(confirm_in(b, f, 30, w) == PairOutcome::ok && w && matches_signatures(*w));
        EXPECT(w && w->start().initiator_challenge == s.initiator_challenge);
    }
    // An honest catch-up past an activation intent: the intent stays, its commit is refused,
    // and the later epoch completes (probe C).
    {
        Side c = side(kC, kB), b = side(kB, kC);
        const Run first = exchange(c, b, 0);
        EXPECT(complete(first));
        activate(c);
        const std::uint64_t old_id = first.resp_verified->id();
        EXPECT(b.g.pair_activation_possible(kC, old_id) == PairOutcome::ok);
        const Run second = exchange(c, b, 100, false);
        EXPECT(second.confirm == PairOutcome::ok && second.s.epoch == 2);
        PairStart third{};
        PairReply r{};
        EXPECT(start(c, 200, third) == PairOutcome::ok && third.epoch == 3);
        EXPECT(answer(b, third, 210, r) == PairOutcome::ok && st(b).used_epoch == 2);
        EXPECT(st(b).binding == PairStep::activation_possible && st(b).binding_id == old_id);
        EXPECT(b.g.pair_commit(kC, old_id) == PairOutcome::wrong_state);
        EXPECT(st(b).committed_epoch == 0);
        PairConfirm cfm{};
        std::optional<VerifiedPairBinding> v;
        EXPECT(reply_in(c, r, 230, cfm, v) == PairOutcome::ok && v);
        EXPECT(confirm_in(b, cfm, 240, v) == PairOutcome::ok && v && v->id() != old_id);
        activate(c);
        activate(b);
        EXPECT(st(b).committed_epoch == 3 && st(c).committed_epoch == 3);
    }
    // Without a catch-up the intent still commits after a delayed ordinary start (review 3).
    {
        Side c = side(kC, kB), b = side(kB, kC);
        const Run first = exchange(c, b, 0);
        EXPECT(complete(first));
        activate(c);
        const std::uint64_t old_id = first.resp_verified->id();
        EXPECT(b.g.pair_activation_possible(kC, old_id) == PairOutcome::ok);
        PairStart next{};
        PairReply r{};
        EXPECT(start(c, 100, next) == PairOutcome::ok && next.epoch == 2);
        EXPECT(answer(b, next, 110, r) == PairOutcome::ok && st(b).used_epoch == 1);
        EXPECT(b.g.pair_commit(kC, old_id) == PairOutcome::ok && st(b).committed_epoch == 1);
    }
}
}  // namespace

int main() {
    test_pairs_only_between_introduced_members();
    test_introduce_succeeds_and_binds_everything();
    test_consent_role_and_history();
    test_removal_and_readd_end_the_pair();
    test_roster_change_drops_attempts_and_blocks_activation();
    test_group_state_stays_in_place_and_ids_are_unique();
    test_retired_attempt_and_signer_failure();
    test_signer_is_pinned();
    test_replayed_starts_cannot_displace_the_live_answer();
    test_budgets_and_cached_replies();
    test_time_limit_per_side();
    test_epochs_converge_under_repeated_loss();
    test_crossing_starts();
    test_introduce_beats_rekey_when_history_differs();
    test_activation_intent_and_recovery();
    test_hints_after_signature_and_pair_naming();
    test_challenges_come_from_the_random_source();
    test_outputs_never_stale();
    test_signed_bytes_layout();
    test_forged_confirm_and_initiator_restart();
    test_review4_fixes();
    test_review3_fixes();
    if (failures != 0) {
        std::cerr << failures << " group pair assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 22 group pair scenario groups\n";
    return EXIT_SUCCESS;
}
