// Host tests for group pair setup (GROUP_PAIRWISE_V1 implementation order step 2).
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

// ---- test-only stand-in primitives (same as group_roster_tests.cpp) ----
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

PublicKey key(std::uint8_t marker) {
    PublicKey k{};
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = static_cast<std::uint8_t>(marker * 37 + i);
    return k;
}
template <std::size_t N>
std::array<std::uint8_t, N> fresh(std::uint8_t marker) {
    std::array<std::uint8_t, N> v{};
    for (std::size_t i = 0; i < N; ++i) v[i] = static_cast<std::uint8_t>(marker + 3 * i + 1);
    return v;
}

const TestCrypto crypto{};
const PublicKey kA = key(1), kB = key(2), kC = key(3), kD = key(4), kX = key(9), kG = key(7);
constexpr std::uint64_t kGroup = 0x0123456789ABCDEFULL;
constexpr std::uint32_t kLife = 30000;

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
Signed rule(const Signed& cur, LocationRule lr) {
    Roster r{};
    EXPECT(change_location_rule(cur.roster, crypto, lr, r));
    return seal(r, cur.roster.members[0].key);
}

RosterState board(const PublicKey& own, const Signed& roster, const PublicKey& admin = kA) {
    RosterState s;
    EXPECT(s.initialize(admin, kGroup, own));
    EXPECT(s.offer(roster.bytes.data(), roster.bytes.size(), crypto) == Outcome::accepted);
    return s;
}
PairAuthority authority(const RosterState& s, const PublicKey& peer, PairPurpose p) {
    std::optional<PairAuthority> a;
    EXPECT(s.pair_authority(peer, p, a));
    return *a;
}

// A, B (added 2), C (added 3, newer than B).
const Signed v3 = added(added(first(), kB, "bob"), kC, "carol");

struct Run {
    PairOutcome start{}, reply{}, confirm{};
    std::optional<VerifiedPairBinding> init_verified, resp_verified;
    PairStart s{};
    PairReply r{};
    PairConfirm c{};
};

// One full exchange: `init` (local key ik) starts, `resp` (rk) answers.
Run exchange(const PairAuthority& ia, PairLedger& il, PairInitiator& ini, const PublicKey& ik,
             const PairAuthority& ra, PairLedger& rl, PairResponder& res, const PublicKey& rk,
             std::uint8_t salt, std::uint64_t now = 1000) {
    Run run;
    run.start = ini.start(ia, il, fresh<kEphemeralBytes>(salt), fresh<kChallengeBytes>(salt + 1),
                          fresh<kBootTokenBytes>(salt + 2), kLife, now, TestSigner(ik), run.s);
    if (run.start != PairOutcome::ok) return run;
    run.reply = res.on_start(ra, rl, run.s, fresh<kEphemeralBytes>(salt + 50), fresh<kChallengeBytes>(salt + 51),
                             fresh<kBootTokenBytes>(salt + 52), now + 10, crypto, TestSigner(rk), run.r);
    if (run.reply != PairOutcome::ok) return run;
    run.confirm = ini.on_reply(run.r, now + 20, crypto, TestSigner(ik), run.c, run.init_verified);
    if (run.confirm != PairOutcome::ok) return run;
    run.confirm = res.on_confirm(run.c, now + 30, crypto, run.resp_verified);
    return run;
}

// ---------------------------------------------------------------- tests

void test_introduce_needs_acceptance_and_non_admin_members() {
    RosterState b = board(kB, v3), c = board(kC, v3), a = board(kA, v3);
    std::optional<PairAuthority> none;
    EXPECT(!b.pair_authority(kC, PairPurpose::introduce, none));   // B has not accepted C
    EXPECT(c.pair_authority(kB, PairPurpose::introduce, none));    // the newer member need not
    EXPECT(!b.accept_member(kA));                                  // older, nothing to accept
    EXPECT(!c.accept_member(kB));
    EXPECT(!b.accept_member(kG));                                  // not listed
    EXPECT(b.accept_member(kC) && b.accepted(kC));
    EXPECT(b.pair_authority(kC, PairPurpose::introduce, none));
    EXPECT(!a.pair_authority(kB, PairPurpose::introduce, none));   // admin pair is in person
    EXPECT(!b.pair_authority(kA, PairPurpose::introduce, none));
    EXPECT(a.pair_authority(kB, PairPurpose::rekey, none));        // but it rekeys
    EXPECT(!b.pair_authority(kB, PairPurpose::introduce, none));   // not with itself
    EXPECT(!b.pair_authority(kG, PairPurpose::rekey, none));       // not listed
    // The newer member starts the introduce.
    EXPECT(authority(c, kB, PairPurpose::introduce).introduce_role() == PairRole::initiator);
    EXPECT(authority(b, kC, PairPurpose::introduce).introduce_role() == PairRole::responder);
}

void test_introduce_succeeds_and_binds_everything() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    PairInitiator ic;
    PairResponder rb;
    Run run = exchange(authority(c, kB, PairPurpose::introduce), lc, ic, kC,
                       authority(b, kC, PairPurpose::introduce), lb, rb, kB, 10);
    EXPECT(run.start == PairOutcome::ok && run.reply == PairOutcome::ok && run.confirm == PairOutcome::ok);
    EXPECT(run.init_verified && run.resp_verified);
    if (!run.init_verified || !run.resp_verified) return;
    const VerifiedPairBinding& vi = *run.init_verified;
    const VerifiedPairBinding& vr = *run.resp_verified;
    EXPECT(vi.local_role() == PairRole::initiator && vr.local_role() == PairRole::responder);
    EXPECT(vi.epoch() == 1 && vr.epoch() == 1);
    EXPECT(vi.start().roster_digest == b.digest() && vi.start().roster_version == 3);
    EXPECT(vi.start().initiator == c.fingerprints()[2] && vi.start().responder == b.fingerprints()[1]);
    EXPECT(vi.start().group.admin == b.fingerprints()[0] && vi.start().group.group_id == kGroup);
    EXPECT(vi.reply().responder_challenge == vr.reply().responder_challenge);
    EXPECT(vi.initiator_binding_signature() == vr.initiator_binding_signature());
    // Durable steps: both reserved epoch 1 before sending; then activation and commit.
    EXPECT(lc.step() == PairStep::prepared && lb.step() == PairStep::prepared);
    EXPECT(lc.highest_epoch() == 1 && lb.highest_epoch() == 1);
    EXPECT(!lc.commit(1));  // never committed without ACTIVATION_POSSIBLE first
    EXPECT(lc.activation_possible(1) && lc.commit(1) && lc.committed_epoch() == 1);
    EXPECT(lb.activation_possible(1) && lb.commit(1));
}

void test_wrong_role_and_wrong_purpose() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    PairInitiator ib;
    PairStart s{};
    // The older member may not start the introduce.
    EXPECT(ib.start(authority(b, kC, PairPurpose::introduce), lb, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife,
                    0, TestSigner(kB), s) == PairOutcome::wrong_role);
    EXPECT(lb.highest_epoch() == 0);
    // ...and a start from it, signed and otherwise valid, is refused by the newer member.
    PairStart forged{};
    forged.purpose = PairPurpose::introduce;
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    forged.group = ab.group(); forged.roster_version = 3; forged.roster_digest = ab.roster_digest();
    forged.epoch = 1; forged.initiator = ab.local_fingerprint(); forged.responder = ab.peer_fingerprint();
    forged.lifetime_ms = kLife; forged.initiator_ephemeral = fresh<32>(4);
    forged.initiator_challenge = fresh<32>(5); forged.initiator_boot = fresh<16>(6);
    std::array<std::uint8_t, kStartBytes> bytes{};
    TestSigner(kB).sign(bytes.data(), start_bytes(forged, bytes.data(), bytes.size()), forged.signature);
    PairResponder rc;
    PairReply r{};
    EXPECT(rc.on_start(authority(c, kB, PairPurpose::introduce), lc, forged, fresh<32>(7), fresh<32>(8), fresh<16>(9),
                       0, crypto, TestSigner(kC), r) == PairOutcome::wrong_role);
    EXPECT(lc.highest_epoch() == 0);
    // A rekey with no earlier attempt is refused on both sides.
    PairInitiator ic;
    EXPECT(ic.start(authority(c, kB, PairPurpose::rekey), lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0,
                    TestSigner(kC), s) == PairOutcome::wrong_purpose);
}

void test_substituted_identity_and_wrong_pair() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    PairInitiator ic;
    PairStart s{};
    EXPECT(ic.start(authority(c, kB, PairPurpose::introduce), lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0,
                    TestSigner(kC), s) == PairOutcome::ok);
    // Same start, re-signed by an identity that is not C.
    PairStart sub = s;
    std::array<std::uint8_t, kStartBytes> bytes{};
    TestSigner(kG).sign(bytes.data(), start_bytes(sub, bytes.data(), bytes.size()), sub.signature);
    PairResponder rb;
    PairReply r{};
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    EXPECT(rb.on_start(ab, lb, sub, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::bad_signature);
    EXPECT(lb.highest_epoch() == 0 && lb.step() == PairStep::none);
    // A start naming another responder, or a key the authority does not hold.
    PairStart other = s;
    other.responder = c.fingerprints()[0];
    EXPECT(rb.on_start(ab, lb, other, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    EXPECT(rb.on_start(ab, lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kD), r) ==
           PairOutcome::not_for_this_pair);
    // The genuine start still works afterwards (nothing was spent).
    EXPECT(rb.on_start(ab, lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::ok);
    // A reply signed by a substitute is refused and the attempt waits.
    PairReply bad = r;
    std::array<std::uint8_t, kBindingBytes> bb{};
    TestSigner(kG).sign(bb.data(), binding_bytes(s, bad, bb.data(), bb.size()), bad.signature);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(ic.on_reply(bad, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_signature && !v);
    EXPECT(ic.on_reply(r, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
}

void test_look_alike_group_id() {
    // Another admin X uses the same group ID with the same members.
    const Signed x3 = added(added(first(kX), kB, "bob"), kC, "carol");
    RosterState cx = board(kC, x3, kX);
    RosterState b = board(kB, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lc, lb;
    PairInitiator ic;
    PairStart s{};
    EXPECT(ic.start(authority(cx, kB, PairPurpose::introduce), lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0,
                    TestSigner(kC), s) == PairOutcome::ok);
    EXPECT(s.group.group_id == kGroup);
    PairResponder rb;
    PairReply r{};
    EXPECT(rb.on_start(authority(b, kC, PairPurpose::introduce), lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10,
                       crypto, TestSigner(kB), r) == PairOutcome::not_for_this_pair);
    EXPECT(lb.highest_epoch() == 0);
}

void test_roster_disagreement() {
    const Signed v4 = rule(v3, LocationRule::preferred);
    const Signed v4b = rule(v3, LocationRule::required);  // same version, different roster
    RosterState b3 = board(kB, v3), c4 = board(kC, v4), c4b = board(kC, v4b), b4 = board(kB, v4);
    EXPECT(b3.accept_member(kC) && b4.accept_member(kC));
    auto try_start = [](const RosterState& init_board, const RosterState& resp_board) {
        PairLedger li, lr;
        PairInitiator ini;
        PairResponder res;
        PairStart s{};
        PairReply r{};
        EXPECT(ini.start(authority(init_board, kB, PairPurpose::introduce), li, fresh<32>(1), fresh<32>(2),
                         fresh<16>(3), kLife, 0, TestSigner(kC), s) == PairOutcome::ok);
        const PairOutcome o = res.on_start(authority(resp_board, kC, PairPurpose::introduce), lr, s, fresh<32>(7),
                                           fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r);
        EXPECT(lr.highest_epoch() == 0 || o == PairOutcome::ok);
        return o;
    };
    EXPECT(try_start(c4, b3) == PairOutcome::peer_roster_newer);
    EXPECT(try_start(c4b, b4) == PairOutcome::peer_roster_differs);
    RosterState c3 = board(kC, v3);
    EXPECT(try_start(c3, b4) == PairOutcome::peer_roster_older);
    EXPECT(try_start(c4, b4) == PairOutcome::ok);
}

void test_replayed_messages_do_nothing() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    PairInitiator ic;
    PairResponder rb;
    const PairAuthority ac = authority(c, kB, PairPurpose::introduce);
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    Run one = exchange(ac, lc, ic, kC, ab, lb, rb, kB, 10, 1000);
    EXPECT(one.resp_verified.has_value());
    EXPECT(lc.activation_possible(1) && lc.commit(1) && lb.activation_possible(1) && lb.commit(1));
    // Replay the old start: its epoch is spent.
    PairReply r{};
    EXPECT(rb.on_start(ab, lb, one.s, fresh<32>(60), fresh<32>(61), fresh<16>(62), 99000, crypto, TestSigner(kB), r) ==
           PairOutcome::bad_epoch);
    EXPECT(lb.highest_epoch() == 1 && lb.committed_epoch() == 1 && lb.step() == PairStep::committed);
    // A new rekey: the old reply and old confirm do not complete it.
    const PairAuthority rc = authority(c, kB, PairPurpose::rekey);
    const PairAuthority rbk = authority(b, kC, PairPurpose::rekey);
    PairInitiator ic2;
    PairResponder rb2;
    PairStart s2{};
    EXPECT(ic2.start(rc, lc, fresh<32>(20), fresh<32>(21), fresh<16>(22), kLife, 100000, TestSigner(kC), s2) ==
           PairOutcome::ok);
    EXPECT(s2.epoch == 2);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(ic2.on_reply(one.r, 100010, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_signature && !v);
    PairReply r2{};
    EXPECT(rb2.on_start(rbk, lb, s2, fresh<32>(70), fresh<32>(71), fresh<16>(72), 100010, crypto, TestSigner(kB), r2) ==
           PairOutcome::ok);
    EXPECT(rb2.on_confirm(one.c, 100020, crypto, v) == PairOutcome::bad_signature && !v);
    // Until the new binding is proven, the committed pair is untouched.
    EXPECT(lb.committed_epoch() == 1);
    EXPECT(ic2.on_reply(r2, 100020, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(rb2.on_confirm(conf, 100030, crypto, v) == PairOutcome::ok && v);
}

void test_rate_limit_and_deadline() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    PairResponder rb;
    const PairAuthority ac = authority(c, kB, PairPurpose::introduce);
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    PairInitiator i1, i2, i3;
    PairStart s1{}, s2{}, s3{};
    PairReply r{};
    EXPECT(i1.start(ac, lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kC), s1) == PairOutcome::ok);
    EXPECT(rb.on_start(ab, lb, s1, fresh<32>(7), fresh<32>(8), fresh<16>(9), 0, crypto, TestSigner(kB), r) ==
           PairOutcome::ok);
    EXPECT(i2.start(ac, lc, fresh<32>(11), fresh<32>(12), fresh<16>(13), kLife, 100, TestSigner(kC), s2) ==
           PairOutcome::ok);
    EXPECT(rb.on_start(ab, lb, s2, fresh<32>(17), fresh<32>(18), fresh<16>(19), 100, crypto, TestSigner(kB), r) ==
           PairOutcome::rate_limited);
    EXPECT(lb.highest_epoch() == 1);
    EXPECT(rb.on_start(ab, lb, s2, fresh<32>(17), fresh<32>(18), fresh<16>(19), 100 + kStartAnswerIntervalMs, crypto,
                       TestSigner(kB), r) == PairOutcome::ok);
    // Deadline: the confirm arrives too late.
    std::optional<VerifiedPairBinding> v;
    PairConfirm conf{};
    EXPECT(i2.on_reply(r, 100 + kStartAnswerIntervalMs, crypto, TestSigner(kC), conf, v) == PairOutcome::ok);
    v.reset();
    EXPECT(rb.on_confirm(conf, 100 + kStartAnswerIntervalMs + kLife + 1, crypto, v) == PairOutcome::expired && !v);
    // Lifetime limits.
    EXPECT(i3.start(ac, lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kMaximumPairLifetimeMs + 1, 0, TestSigner(kC),
                    s3) == PairOutcome::bad_lifetime);
    EXPECT(i3.start(ac, lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), 0, 0, TestSigner(kC), s3) ==
           PairOutcome::bad_lifetime);
    // Zero or repeated fresh values.
    Ephemeral zero{};
    EXPECT(i3.start(ac, lc, zero, fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kC), s3) ==
           PairOutcome::bad_fresh_values);
}

void test_interrupted_activation_is_never_resumed() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    const PairAuthority ac = authority(c, kB, PairPurpose::introduce);
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    PairInitiator ic;
    PairResponder rb;
    Run one = exchange(ac, lc, ic, kC, ab, lb, rb, kB, 10);
    EXPECT(one.resp_verified.has_value());
    // C commits epoch 1; B loses power after ACTIVATION_POSSIBLE.
    EXPECT(lc.activation_possible(1) && lc.commit(1));
    EXPECT(lb.activation_possible(1));
    lb.restart();
    EXPECT(lb.step() == PairStep::none && lb.committed_epoch() == 0 && lb.highest_epoch() == 1);
    EXPECT(!lb.commit(1) && !lb.activation_possible(1));   // the attempt cannot be resumed
    // Next contact: C (committed) rekeys at the next epoch; B accepts after its interruption.
    PairInitiator ic2;
    PairResponder rb2;
    Run two = exchange(authority(c, kB, PairPurpose::rekey), lc, ic2, kC, authority(b, kC, PairPurpose::rekey), lb, rb2,
                       kB, 30, 200000);
    EXPECT(two.resp_verified.has_value() && two.s.epoch == 2);
    // The other way round: the initiator was interrupted, the responder committed.
    PairLedger lc3, lb3;
    PairInitiator ic3, ic4;
    PairResponder rb3, rb4;
    Run three = exchange(ac, lc3, ic3, kC, ab, lb3, rb3, kB, 40);
    EXPECT(three.resp_verified.has_value());
    EXPECT(lb3.activation_possible(1) && lb3.commit(1));
    EXPECT(lc3.activation_possible(1));
    lc3.restart();
    Run four = exchange(ac, lc3, ic4, kC, ab, lb3, rb4, kB, 50, 300000);   // C restarts with introduce
    EXPECT(four.resp_verified.has_value() && four.s.epoch == 2);
    // A start lost on the radio: the initiator moved on, the responder accepts the skip.
    PairLedger lc5, lb5;
    PairInitiator lost, ic5;
    PairResponder rb5;
    PairStart dropped{};
    EXPECT(lost.start(ac, lc5, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kC), dropped) ==
           PairOutcome::ok);
    lc5.restart();
    Run five = exchange(ac, lc5, ic5, kC, ab, lb5, rb5, kB, 60, 400000);
    EXPECT(five.resp_verified.has_value() && five.s.epoch == 2 && lb5.highest_epoch() == 2);
}

void test_simultaneous_rekey_starts() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    PairLedger lb, lc;
    {
        PairInitiator ic;
        PairResponder rb;
        Run one = exchange(authority(c, kB, PairPurpose::introduce), lc, ic, kC,
                           authority(b, kC, PairPurpose::introduce), lb, rb, kB, 10);
        EXPECT(one.resp_verified.has_value());
        EXPECT(lc.activation_possible(1) && lc.commit(1) && lb.activation_possible(1) && lb.commit(1));
    }
    const PairAuthority rc = authority(c, kB, PairPurpose::rekey);
    const PairAuthority rbk = authority(b, kC, PairPurpose::rekey);
    PairInitiator ib, ic;
    PairStart sb{}, sc{};
    EXPECT(ib.start(rbk, lb, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kB), sb) == PairOutcome::ok);
    EXPECT(ic.start(rc, lc, fresh<32>(4), fresh<32>(5), fresh<16>(6), kLife, 0, TestSigner(kC), sc) == PairOutcome::ok);
    EXPECT(sb.epoch == 2 && sc.epoch == 2);
    // Exactly one side wins.
    const bool b_wins = local_start_wins(b.fingerprints()[1], c.fingerprints()[2]);
    EXPECT(b_wins != local_start_wins(c.fingerprints()[2], b.fingerprints()[1]));
    PairLedger& loser_ledger = b_wins ? lc : lb;
    PairInitiator& loser_initiator = b_wins ? ic : ib;
    PairInitiator& winner_initiator = b_wins ? ib : ic;
    const PairStart& winning_start = b_wins ? sb : sc;
    const PairAuthority& loser_authority = b_wins ? rc : rbk;
    const PublicKey& loser_key = b_wins ? kC : kB;
    const PublicKey& winner_key = b_wins ? kB : kC;
    PairResponder loser_responder;
    PairReply r{};
    // Without yielding, the loser has already used epoch 2.
    EXPECT(loser_responder.on_start(loser_authority, loser_ledger, winning_start, fresh<32>(7), fresh<32>(8),
                                    fresh<16>(9), 10, crypto, TestSigner(loser_key), r) == PairOutcome::bad_epoch);
    EXPECT(loser_initiator.yield_to_peer(loser_ledger));
    EXPECT(loser_responder.on_start(loser_authority, loser_ledger, winning_start, fresh<32>(7), fresh<32>(8),
                                    fresh<16>(9), 10, crypto, TestSigner(loser_key), r) == PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(winner_initiator.on_reply(r, 20, crypto, TestSigner(winner_key), conf, v) == PairOutcome::ok && v);
    v.reset();
    EXPECT(loser_responder.on_confirm(conf, 30, crypto, v) == PairOutcome::ok && v);
    // Giving back a reservation applies only to the board's own unanswered start: not to
    // an epoch it answered (that binding is signed), and not after its start was answered.
    EXPECT(!loser_initiator.yield_to_peer(loser_ledger));
    EXPECT(loser_ledger.highest_epoch() == 2 && loser_ledger.step() == PairStep::prepared);
    EXPECT(!winner_initiator.yield_to_peer(b_wins ? lb : lc));
    PairInitiator fresh_start;
    PairLedger empty;
    EXPECT(!fresh_start.yield_to_peer(empty));
    EXPECT(loser_ledger.activation_possible(2));
}

void test_conflict_and_removal_stop_authority() {
    const Signed v4 = rule(v3, LocationRule::preferred);
    const Signed v4b = rule(v3, LocationRule::required);
    RosterState b = board(kB, v3);
    EXPECT(b.accept_member(kC));
    EXPECT(b.offer(v4.bytes.data(), v4.bytes.size(), crypto) == Outcome::accepted);
    EXPECT(b.accepted(kC));  // acceptance kept while C stays listed
    EXPECT(b.offer(v4b.bytes.data(), v4b.bytes.size(), crypto) == Outcome::conflict);
    EXPECT(!b.accept_member(kC));  // no acceptance while in conflict
    std::optional<PairAuthority> a;
    EXPECT(!b.pair_authority(kC, PairPurpose::introduce, a));
    EXPECT(!b.pair_authority(kC, PairPurpose::rekey, a));
    EXPECT(b.keep_current());
    EXPECT(b.pair_authority(kC, PairPurpose::introduce, a));
    // C removed, then added again: acceptance must be given again.
    Roster r5{}, r6{};
    EXPECT(remove_member(v4.roster, crypto, kC, r5));
    const Signed v5 = seal(r5, kA);
    EXPECT(add_member(v5.roster, crypto, kC, "carol", r6));
    const Signed v6 = seal(r6, kA);
    EXPECT(b.offer(v5.bytes.data(), v5.bytes.size(), crypto) == Outcome::accepted);
    EXPECT(!b.pair_authority(kC, PairPurpose::rekey, a));
    EXPECT(b.offer(v6.bytes.data(), v6.bytes.size(), crypto) == Outcome::accepted);
    EXPECT(!b.accepted(kC));
    EXPECT(!b.pair_authority(kC, PairPurpose::introduce, a));
    EXPECT(b.accept_member(kC) && b.pair_authority(kC, PairPurpose::introduce, a));
    PublicKey found{};
    EXPECT(b.key_for(b.fingerprints()[2], found) && found == kC);
    Fingerprint unknown{};
    EXPECT(!b.key_for(unknown, found));
}

void test_signed_bytes_layout() {
    PairStart s{};
    s.purpose = PairPurpose::rekey;
    s.group.admin = fresh<32>(90);
    s.group.group_id = kGroup;
    s.roster_version = 7;
    s.roster_digest = fresh<32>(91);
    s.epoch = 0x01020304;
    s.initiator = fresh<32>(92);
    s.responder = fresh<32>(93);
    s.lifetime_ms = 30000;
    s.initiator_ephemeral = fresh<32>(94);
    s.initiator_challenge = fresh<32>(95);
    s.initiator_boot = fresh<16>(96);
    PairReply r{};
    r.responder_ephemeral = fresh<32>(97);
    r.responder_challenge = fresh<32>(98);
    r.responder_boot = fresh<16>(99);
    std::array<std::uint8_t, kStartBytes> sb{};
    std::array<std::uint8_t, kBindingBytes> bb{};
    EXPECT(start_bytes(s, sb.data(), sb.size()) == kStartBytes);
    EXPECT(binding_bytes(s, r, bb.data(), bb.size()) == kBindingBytes);
    EXPECT(start_bytes(s, sb.data(), sb.size() - 1) == 0);
    EXPECT(std::memcmp(sb.data(), kStartDomain.data(), kStartDomain.size()) == 0);
    EXPECT(std::memcmp(bb.data(), kBindingDomain.data(), kBindingDomain.size()) == 0);
    const std::size_t c = kStartDomain.size();
    EXPECT(sb[c] == 2);                                           // purpose
    EXPECT(std::equal(s.group.admin.begin(), s.group.admin.end(), sb.begin() + c + 1));
    EXPECT(sb[c + 33] == 0x01 && sb[c + 40] == 0xEF);              // group ID, big-endian
    EXPECT(sb[c + 44] == 7);                                       // roster version
    const std::size_t e = c + 1 + 32 + 8 + 4 + 32;
    EXPECT(sb[e] == 1 && sb[e + 1] == 2 && sb[e + 2] == 3 && sb[e + 3] == 4);  // epoch
    // Every field changes the signed bytes.
    PairStart t = s;
    t.lifetime_ms += 1;
    std::array<std::uint8_t, kStartBytes> tb{};
    start_bytes(t, tb.data(), tb.size());
    EXPECT(tb != sb);
    PairReply u = r;
    u.responder_boot[0] ^= 1;
    std::array<std::uint8_t, kBindingBytes> ub{};
    binding_bytes(s, u, ub.data(), ub.size());
    EXPECT(ub != bb);
    EXPECT(kStartDomain != kBindingDomain && kBindingDomain != kSignatureDomain);
}


void test_more_refusals() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    const PairAuthority ac = authority(c, kB, PairPurpose::introduce);
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    // A start signed by C but naming another initiator.
    PairLedger lc, lb;
    PairInitiator ic;
    PairStart s{};
    EXPECT(ic.start(ac, lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kC), s) == PairOutcome::ok);
    PairStart renamed = s;
    renamed.initiator = c.fingerprints()[0];
    std::array<std::uint8_t, kStartBytes> bytes{};
    TestSigner(kC).sign(bytes.data(), start_bytes(renamed, bytes.data(), bytes.size()), renamed.signature);
    PairResponder rb;
    PairReply r{};
    EXPECT(rb.on_start(ab, lb, renamed, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::not_for_this_pair);
    EXPECT(lb.highest_epoch() == 0);
    // The initiator's own deadline.
    EXPECT(rb.on_start(ab, lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::ok);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(ic.on_reply(r, kLife + 1, crypto, TestSigner(kC), conf, v) == PairOutcome::expired && !v);
    EXPECT(ic.on_reply(r, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::wrong_state);
    // After committing, the initiator rekeys instead of introducing again.
    PairLedger committed, other_side;
    PairInitiator i2, i3;
    PairResponder r2;
    Run one = exchange(ac, committed, i2, kC, ab, other_side, r2, kB, 30, 50000);
    EXPECT(one.resp_verified.has_value());
    EXPECT(committed.activation_possible(committed.attempt_epoch()) && committed.commit(committed.attempt_epoch()));
    EXPECT(i3.start(ac, committed, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 60000, TestSigner(kC), s) ==
           PairOutcome::wrong_purpose);
    // Giving back a reservation fails once this board has since answered another start.
    PairLedger lx;
    PairInitiator mine;
    PairStart own{};
    EXPECT(mine.start(ac, lx, fresh<32>(41), fresh<32>(42), fresh<16>(43), kLife, 0, TestSigner(kC), own) ==
           PairOutcome::ok);
    EXPECT(lx.reserve(own.epoch + 1));  // as if it answered a later start at the next epoch
    EXPECT(!mine.yield_to_peer(lx));
    EXPECT(lx.highest_epoch() == own.epoch + 1);
}

void test_purpose_mismatch_and_reflection() {
    RosterState b = board(kB, v3), c = board(kC, v3);
    EXPECT(b.accept_member(kC));
    const PairAuthority ac = authority(c, kB, PairPurpose::introduce);
    PairLedger lc, lb;
    PairInitiator ic;
    PairStart s{};
    EXPECT(ic.start(ac, lc, fresh<32>(1), fresh<32>(2), fresh<16>(3), kLife, 0, TestSigner(kC), s) == PairOutcome::ok);
    // A rekey authority cannot be used to answer an introduce (it skips the acceptance check).
    PairResponder rb;
    PairReply r{};
    EXPECT(rb.on_start(authority(b, kC, PairPurpose::rekey), lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10,
                       crypto, TestSigner(kB), r) == PairOutcome::not_for_this_pair);
    // Reflected values: the responder's challenge or ephemeral equal to the initiator's.
    const PairAuthority ab = authority(b, kC, PairPurpose::introduce);
    EXPECT(rb.on_start(ab, lb, s, fresh<32>(7), s.initiator_challenge, fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::bad_fresh_values);
    EXPECT(rb.on_start(ab, lb, s, s.initiator_ephemeral, fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::bad_fresh_values);
    EXPECT(lb.highest_epoch() == 0);
    EXPECT(rb.on_start(ab, lb, s, fresh<32>(7), fresh<32>(8), fresh<16>(9), 10, crypto, TestSigner(kB), r) ==
           PairOutcome::ok);
    PairReply reflected = r;
    reflected.responder_challenge = s.initiator_challenge;
    std::array<std::uint8_t, kBindingBytes> bb{};
    TestSigner(kB).sign(bb.data(), binding_bytes(s, reflected, bb.data(), bb.size()), reflected.signature);
    PairConfirm conf{};
    std::optional<VerifiedPairBinding> v;
    EXPECT(ic.on_reply(reflected, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_fresh_values && !v);
    PairReply reflected_eph = r;
    reflected_eph.responder_ephemeral = s.initiator_ephemeral;
    TestSigner(kB).sign(bb.data(), binding_bytes(s, reflected_eph, bb.data(), bb.size()), reflected_eph.signature);
    EXPECT(ic.on_reply(reflected_eph, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::bad_fresh_values && !v);
    EXPECT(ic.on_reply(r, 20, crypto, TestSigner(kC), conf, v) == PairOutcome::ok && v);
}
}  // namespace

int main() {
    test_introduce_needs_acceptance_and_non_admin_members();
    test_introduce_succeeds_and_binds_everything();
    test_wrong_role_and_wrong_purpose();
    test_substituted_identity_and_wrong_pair();
    test_look_alike_group_id();
    test_roster_disagreement();
    test_replayed_messages_do_nothing();
    test_rate_limit_and_deadline();
    test_interrupted_activation_is_never_resumed();
    test_simultaneous_rekey_starts();
    test_conflict_and_removal_stop_authority();
    test_signed_bytes_layout();
    test_more_refusals();
    test_purpose_mismatch_and_reflection();
    if (failures != 0) {
        std::cerr << failures << " group pair assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 14 group pair scenario groups\n";
    return EXIT_SUCCESS;
}
