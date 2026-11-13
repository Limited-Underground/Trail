// Host tests for the signed group roster (GROUP_PAIRWISE_V1 implementation order step 1).
// The crypto below is a deterministic TEST-ONLY stand-in: anyone can forge its
// "signatures". It exercises the roster rules, not any algorithm, which is not selected.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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
    bool fail_digest{false};
    bool verify(const Signature& s, const std::uint8_t* m, std::size_t n, const PublicKey& k) const override {
        Signature expected{};
        fill_hash(expected, join(k.data(), k.size(), m, n), 7);
        return expected == s;
    }
    bool digest(const std::uint8_t* m, std::size_t n, Digest& out) const override {
        if (fail_digest) return false;
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

const TestCrypto crypto{};
const PublicKey kAdmin = key(1), kB = key(2), kC = key(3), kD = key(4), kE = key(5), kF = key(6), kG = key(7);
const PublicKey kOtherAdmin = key(9);
constexpr std::uint64_t kGroup = 0x0123456789ABCDEFULL;

struct Signed {
    Roster roster{};
    Digest digest{};
    std::vector<std::uint8_t> bytes;
};

Signed seal(Roster r, const PublicKey& signer_key = kAdmin) {
    Signed s{};
    EXPECT(sign(r, TestSigner(signer_key)) || signer_key != r.members[0].key);
    if (signer_key != r.members[0].key) {  // forged by someone else: sign the body directly
        std::array<std::uint8_t, kSignatureDomain.size() + kMaximumBodyBytes> in{};
        std::memcpy(in.data(), kSignatureDomain.data(), kSignatureDomain.size());
        const std::size_t n = encode_body(r, in.data() + kSignatureDomain.size(), kMaximumBodyBytes);
        TestSigner(signer_key).sign(in.data(), kSignatureDomain.size() + n, r.signature);
    }
    s.roster = r;
    s.bytes.resize(kMaximumRosterBytes);
    s.bytes.resize(encode(r, s.bytes.data(), s.bytes.size()));
    EXPECT(!s.bytes.empty());
    EXPECT(roster_digest(r, crypto, s.digest));
    return s;
}

Signed first(LocationRule rule = LocationRule::off) {
    Roster r{};
    EXPECT(create_first(kGroup, kAdmin, "admin", rule, r));
    return seal(r);
}
Signed added(const Signed& cur, const PublicKey& k, const char* alias) {
    Roster r{};
    EXPECT(add_member(cur.roster, cur.digest, k, alias, r));
    return seal(r);
}
Signed removed(const Signed& cur, const PublicKey& k) {
    Roster r{};
    EXPECT(remove_member(cur.roster, cur.digest, k, r));
    return seal(r);
}

Outcome offer(RosterState& s, const Signed& r) { return s.offer(r.bytes.data(), r.bytes.size(), crypto); }

RosterState member_state(const PublicKey& own) {
    RosterState s;
    EXPECT(s.initialize(kAdmin, kGroup, own));
    return s;
}

// ---------------------------------------------------------------- tests

void test_round_trip_and_size_budget() {
    Signed v = first();
    const std::array<std::pair<PublicKey, const char*>, 5> joiners{{
        {kB, "bob"}, {kC, "carol"}, {kD, "dave"}, {kE, "erin"}, {kF, "frank"}}};
    for (const auto& joiner : joiners) v = added(v, joiner.first, joiner.second);
    EXPECT(v.roster.member_count == 6);
    EXPECT(v.roster.version == 6);
    Roster decoded{};
    EXPECT(decode(v.bytes.data(), v.bytes.size(), decoded) == FormatError::none);
    std::vector<std::uint8_t> again(kMaximumRosterBytes);
    again.resize(encode(decoded, again.data(), again.size()));
    EXPECT(again == v.bytes);
    // Seventh member refused.
    Roster seventh{};
    EXPECT(!add_member(v.roster, v.digest, kG, "gina", seventh));
    // Six members with maximum aliases: the design's 5-fragment budget at a 94-byte frame.
    EXPECT(kMaximumRosterBytes == 439);
    EXPECT(kMaximumRosterBytes <= 5 * 94);
}

void test_strict_decode_rejects_malformed() {
    Signed v = added(added(first(), kB, "bob"), kC, "carol");
    auto mutate = [&](std::size_t at, std::uint8_t value) {
        std::vector<std::uint8_t> b = v.bytes;
        b[at] = value;
        Roster r{};
        return decode(b.data(), b.size(), r);
    };
    Roster r{};
    EXPECT(mutate(0, 'X') == FormatError::magic);
    EXPECT(mutate(4, 2) == FormatError::format_version);
    EXPECT(decode(v.bytes.data(), v.bytes.size() - 1, r) == FormatError::length);
    std::vector<std::uint8_t> longer = v.bytes; longer.push_back(0);
    EXPECT(decode(longer.data(), longer.size(), r) == FormatError::length);
    EXPECT(mutate(49, 3) == FormatError::location_rule);
    EXPECT(mutate(50, 0) == FormatError::member_count);
    EXPECT(mutate(50, 7) == FormatError::member_count);
    EXPECT(mutate(51, 2) == FormatError::role);               // admin not first
    EXPECT(mutate(51, 9) == FormatError::role);
    EXPECT(mutate(55, 2) == FormatError::added_in);           // admin added-in must be 1
    // Version 0, and version 1 with a previous digest.
    EXPECT(mutate(16, 0) != FormatError::none);
    Signed one = first();
    std::vector<std::uint8_t> b = one.bytes; b[17] = 1;
    EXPECT(decode(b.data(), b.size(), r) == FormatError::previous_digest);
    EXPECT(decode(nullptr, 0, r) == FormatError::length);
}

void test_structural_rules() {
    Signed v = added(added(first(), kB, "bob"), kC, "carol");
    Roster r = v.roster;
    r.members[2].added_in = 2;  // not strictly increasing
    EXPECT(validate(r) == FormatError::added_in);
    r = v.roster; r.members[2].added_in = 4;  // later than roster version
    EXPECT(validate(r) == FormatError::added_in);
    r = v.roster; r.members[2].key = kB;
    EXPECT(validate(r) == FormatError::duplicate_identity);
    r = v.roster; r.members[2].alias = r.members[1].alias; r.members[2].alias_bytes = r.members[1].alias_bytes;
    EXPECT(validate(r) == FormatError::duplicate_alias);
    r = v.roster; r.members[1].role = Role::admin;
    EXPECT(validate(r) == FormatError::role);
    r = v.roster; r.previous.fill(0);
    EXPECT(validate(r) == FormatError::previous_digest);
    // Aliases: empty, too long, control character, invalid UTF-8, overlong, surrogate.
    Roster bad{};
    EXPECT(!create_first(kGroup, kAdmin, "", LocationRule::off, bad));
    EXPECT(!create_first(kGroup, kAdmin, "seventeen-bytes!!", LocationRule::off, bad));
    EXPECT(create_first(kGroup, kAdmin, "sixteen-bytes!!!", LocationRule::off, bad));
    EXPECT(!create_first(kGroup, kAdmin, std::string_view("a\nb"), LocationRule::off, bad));
    EXPECT(!create_first(kGroup, kAdmin, "\xC3", LocationRule::off, bad));
    EXPECT(!create_first(kGroup, kAdmin, "\xC0\xAF", LocationRule::off, bad));
    EXPECT(!create_first(kGroup, kAdmin, "\xED\xA0\x80", LocationRule::off, bad));
    EXPECT(create_first(kGroup, kAdmin, "Zo\xC3\xAB \xF0\x9F\x8C\xB2", LocationRule::off, bad));
    // Version 1 lists only the admin.
    Signed one = first();
    r = one.roster; r.member_count = 2; r.members[1] = v.roster.members[1]; r.members[1].added_in = 1;
    EXPECT(validate(r) != FormatError::none);
}

void test_signature_domain_and_authority() {
    Signed v = first();
    RosterState s = member_state(kAdmin);
    // Signed by a different key: refused, and it cannot cause a conflict.
    Signed forged = seal(v.roster, kOtherAdmin);
    EXPECT(offer(s, forged) == Outcome::rejected_signature);
    EXPECT(s.state() == State::awaiting_roster);
    // Any changed body byte breaks the signature.
    std::vector<std::uint8_t> tampered = v.bytes;
    tampered[10] ^= 1;
    EXPECT(s.offer(tampered.data(), tampered.size(), crypto) == Outcome::rejected_signature);
    // Signature without the roster domain label is refused.
    Roster raw = v.roster;
    std::array<std::uint8_t, kMaximumBodyBytes> body{};
    const std::size_t n = encode_body(raw, body.data(), body.size());
    TestSigner(kAdmin).sign(body.data(), n, raw.signature);
    std::vector<std::uint8_t> raw_bytes(kMaximumRosterBytes);
    raw_bytes.resize(encode(raw, raw_bytes.data(), raw_bytes.size()));
    EXPECT(s.offer(raw_bytes.data(), raw_bytes.size(), crypto) == Outcome::rejected_signature);
    // Signer must be the roster's admin.
    Roster r = v.roster;
    EXPECT(!sign(r, TestSigner(kB)));
    // The genuine roster is accepted by the creator.
    EXPECT(offer(s, v) == Outcome::accepted);
    EXPECT(s.state() == State::active);
}

void test_group_name_binding() {
    Signed v = added(first(), kB, "bob");
    RosterState s = member_state(kB);
    // Same admin, different group ID.
    Roster other_id = v.roster; other_id.group_id ^= 1;
    EXPECT(offer(s, seal(other_id)) == Outcome::rejected_group);
    // Same group ID, different admin (signed by that admin): not the admin verified in person.
    Roster other_admin{};
    EXPECT(create_first(kGroup, kOtherAdmin, "admin", LocationRule::off, other_admin));
    Signed oa = seal(other_admin, kOtherAdmin);
    Roster oa2{};
    EXPECT(add_member(oa.roster, oa.digest, kB, "bob", oa2));
    EXPECT(offer(s, seal(oa2, kOtherAdmin)) == Outcome::rejected_signature);
    // Group name is the admin fingerprint plus group ID.
    GroupName a{}, b{};
    EXPECT(group_name(v.roster, crypto, a));
    EXPECT(group_name(oa2, crypto, b));
    EXPECT(a.group_id == b.group_id && a.admin != b.admin);
    // Signed by the pinned admin, but naming a different admin identity as member 0.
    Roster renamed = v.roster;
    renamed.members[0].key = kOtherAdmin;
    EXPECT(offer(s, seal(renamed, kAdmin)) == Outcome::rejected_group);
    EXPECT(offer(s, v) == Outcome::accepted);
}

// Records the exact bytes given to the primitives, to pin the domain labels.
class RecordingCrypto final : public RosterCrypto {
public:
    mutable std::vector<std::uint8_t> verified, digested;
    bool verify(const Signature& s, const std::uint8_t* m, std::size_t n, const PublicKey& k) const override {
        verified.assign(m, m + n);
        return crypto.verify(s, m, n, k);
    }
    bool digest(const std::uint8_t* m, std::size_t n, Digest& out) const override {
        digested.assign(m, m + n);
        return crypto.digest(m, n, out);
    }
    bool fingerprint(const PublicKey& k, Fingerprint& out) const override { return crypto.fingerprint(k, out); }
};

void test_domain_labels_are_bound() {
    Signed v = added(first(), kB, "bob");
    RecordingCrypto rec;
    RosterState s = member_state(kB);
    EXPECT(s.offer(v.bytes.data(), v.bytes.size(), rec) == Outcome::accepted);
    const std::size_t body = v.bytes.size() - kSignatureBytes;
    EXPECT(rec.verified.size() == kSignatureDomain.size() + body);
    EXPECT(std::memcmp(rec.verified.data(), kSignatureDomain.data(), kSignatureDomain.size()) == 0);
    EXPECT(std::memcmp(rec.verified.data() + kSignatureDomain.size(), v.bytes.data(), body) == 0);
    EXPECT(rec.digested.size() == kDigestDomain.size() + v.bytes.size());
    EXPECT(std::memcmp(rec.digested.data(), kDigestDomain.data(), kDigestDomain.size()) == 0);
    EXPECT(std::memcmp(rec.digested.data() + kDigestDomain.size(), v.bytes.data(), v.bytes.size()) == 0);
    EXPECT(kSignatureDomain != kDigestDomain);
}

void test_joiner_first_roster_must_list_it() {
    Signed v2 = added(first(), kB, "bob");
    RosterState c = member_state(kC);
    EXPECT(offer(c, v2) == Outcome::rejected_not_listed);
    EXPECT(!c.has_roster());
    Signed v3 = added(v2, kC, "carol");
    EXPECT(offer(c, v3) == Outcome::accepted);
    EXPECT(c.roster().version == 3);
}

void test_chain_duplicate_stale_and_conflict() {
    Signed v1 = first();
    Signed v2 = added(v1, kB, "bob");
    Signed v3 = added(v2, kC, "carol");
    RosterState s = member_state(kB);
    EXPECT(offer(s, v2) == Outcome::accepted);
    EXPECT(offer(s, v2) == Outcome::duplicate);
    EXPECT(offer(s, v1) == Outcome::stale);
    // Version 3 that does not continue the chain.
    Roster broken = v3.roster; broken.previous[0] ^= 0x55;
    EXPECT(offer(s, seal(broken)) == Outcome::conflict);
    EXPECT(s.state() == State::conflict);
    EXPECT(s.conflict_kind() == ConflictKind::chain_break);
    EXPECT(offer(s, v3) == Outcome::blocked_by_conflict);
    EXPECT(s.roster().version == 2);
    EXPECT(s.keep_current());
    EXPECT(offer(s, v3) == Outcome::accepted);
    EXPECT(s.digest() == v3.digest);
}

void test_same_version_different_roster_is_conflict() {
    Signed v2 = added(first(), kB, "bob");
    Signed v3 = added(v2, kC, "carol");
    Signed v3_other = added(v2, kD, "dave");
    RosterState s = member_state(kB);
    EXPECT(offer(s, v2) == Outcome::accepted);
    EXPECT(offer(s, v3) == Outcome::accepted);
    // An unsigned look-alike cannot trigger the conflict.
    EXPECT(offer(s, seal(v3_other.roster, kOtherAdmin)) == Outcome::rejected_signature);
    EXPECT(s.state() == State::active);
    EXPECT(offer(s, v3_other) == Outcome::conflict);
    EXPECT(s.conflict_kind() == ConflictKind::same_version_different_roster);
    EXPECT(s.roster().members[2].key == kC);
    // The user leaves the group instead of keeping the current roster.
    s.stop_trusting();
    EXPECT(s.state() == State::distrusted);
    EXPECT(offer(s, v3) == Outcome::not_member);
}

void test_gap_accepted_when_history_agrees() {
    Signed v2 = added(first(), kB, "bob");
    Signed v3 = added(v2, kC, "carol");
    Signed v4 = removed(v3, kC);
    Signed v5 = added(v4, kD, "dave");
    Signed v6 = added(v5, kC, "carol");  // re-added: new added-in version
    RosterState b = member_state(kB);
    EXPECT(offer(b, v3) == Outcome::accepted);
    EXPECT(offer(b, v6) == Outcome::accepted);  // missed v4 and v5
    EXPECT(b.roster().version == 6);
    EXPECT(b.roster().member_count == 4);
}

void test_gap_with_forked_history_is_conflict() {
    Signed v2 = added(first(), kB, "bob");
    Signed v3 = added(v2, kC, "carol");
    // A fork from v2 where D was added at version 3 instead of C.
    Signed f3 = added(v2, kD, "dave");
    Signed f4 = added(f3, kE, "erin");
    Signed f5 = added(f4, kF, "frank");
    RosterState b = member_state(kB);
    EXPECT(offer(b, v3) == Outcome::accepted);
    EXPECT(offer(b, f5) == Outcome::conflict);
    EXPECT(b.conflict_kind() == ConflictKind::history_mismatch);
    // Exactly one higher, chained correctly, but rewriting a held member's history.
    RosterState c = member_state(kB);
    EXPECT(offer(c, v3) == Outcome::accepted);
    Roster r{};
    EXPECT(add_member(v3.roster, v3.digest, kE, "erin", r));
    r.members[2].alias[0] = 'K';  // alias change alone is presentation: still consistent
    Signed ok = seal(r);
    EXPECT(offer(c, ok) == Outcome::accepted);
    RosterState d = member_state(kB);
    EXPECT(offer(d, v3) == Outcome::accepted);
    Roster swapped{};
    EXPECT(add_member(v3.roster, v3.digest, kE, "erin", swapped));
    swapped.members[2].key = kG;  // C replaced by another identity under the same added-in version
    EXPECT(offer(d, seal(swapped)) == Outcome::conflict);
    EXPECT(d.conflict_kind() == ConflictKind::history_mismatch);
}

void test_removal_of_self_and_leaving() {
    Signed v3 = added(added(first(), kB, "bob"), kC, "carol");
    RosterState c = member_state(kC);
    EXPECT(offer(c, v3) == Outcome::accepted);
    Signed v4 = removed(v3, kC);
    EXPECT(offer(c, v4) == Outcome::accepted_removed_self);
    EXPECT(c.state() == State::left);
    EXPECT(c.roster().version == 4);  // stored first, so pairs can be retired from it
    EXPECT(offer(c, added(v4, kD, "dave")) == Outcome::not_member);
    // The admin can never be removed.
    Roster r{};
    EXPECT(!remove_member(v3.roster, v3.digest, kAdmin, r));
    EXPECT(!remove_member(v3.roster, v3.digest, kG, r));
}

void test_location_rule_and_version_limit() {
    Signed v2 = added(first(), kB, "bob");
    RosterState b = member_state(kB);
    EXPECT(offer(b, v2) == Outcome::accepted);
    Roster r{};
    EXPECT(change_location_rule(v2.roster, v2.digest, LocationRule::required, r));
    EXPECT(offer(b, seal(r)) == Outcome::accepted);
    EXPECT(b.roster().location_rule == LocationRule::required);
    Roster max = v2.roster;
    max.version = 0xFFFFFFFFU;
    EXPECT(!add_member(max, v2.digest, kC, "carol", r));
    Digest zero{};
    EXPECT(!add_member(v2.roster, zero, kC, "carol", r));
}

void test_digest_covers_whole_roster_and_crypto_failure() {
    Signed v2 = added(first(), kB, "bob");
    Roster changed = v2.roster;
    changed.location_rule = LocationRule::preferred;
    Digest d{};
    EXPECT(roster_digest(changed, crypto, d) && d != v2.digest);
    Roster resigned = v2.roster;
    resigned.signature[63] ^= 1;
    EXPECT(roster_digest(resigned, crypto, d) && d != v2.digest);
    EXPECT(digest_prefix(v2.digest)[0] == v2.digest[0] && digest_prefix(v2.digest)[3] == v2.digest[3]);
    TestCrypto failing;
    failing.fail_digest = true;
    RosterState b = member_state(kB);
    EXPECT(b.offer(v2.bytes.data(), v2.bytes.size(), failing) == Outcome::crypto_failure);
    EXPECT(!b.has_roster());
    RosterState none;
    EXPECT(offer(none, v2) == Outcome::not_initialized);
    EXPECT(!b.initialize(kAdmin, kGroup, kB));
}

}  // namespace

int main() {
    test_round_trip_and_size_budget();
    test_strict_decode_rejects_malformed();
    test_structural_rules();
    test_signature_domain_and_authority();
    test_group_name_binding();
    test_domain_labels_are_bound();
    test_joiner_first_roster_must_list_it();
    test_chain_duplicate_stale_and_conflict();
    test_same_version_different_roster_is_conflict();
    test_gap_accepted_when_history_agrees();
    test_gap_with_forked_history_is_conflict();
    test_removal_of_self_and_leaving();
    test_location_rule_and_version_limit();
    test_digest_covers_whole_roster_and_crypto_failure();
    if (failures != 0) {
        std::cerr << failures << " group roster assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 14 group roster scenario groups\n";
    return EXIT_SUCCESS;
}
