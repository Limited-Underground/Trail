#pragma once

// Signed group roster for Trail V1 pairwise groups (docs/security/GROUP_PAIRWISE_V1.md,
// "The roster"; implementation order step 1). Host-only logic: record format, digest
// chain, signature domain and the member acceptance rules, including roster conflict.
//
// No signature algorithm, hash or fingerprint encoding is selected here (the design
// leaves them open). They are supplied through RosterCrypto / RosterSigner; sizes are
// the current budgets (32-byte public keys, digests and fingerprints, 64-byte
// signatures). The byte layout below is a version-1 candidate, not a frozen wire format.
//
// Requirements on the selected primitives (RosterCrypto):
// - verify() must be strict and non-malleable: for one body and key, no second byte string
//   may verify (otherwise a relay could make a "different" roster without the admin and
//   trigger a conflict). Rosters are always retransmitted as the exact signed bytes.
// - fingerprint() must be the same derivation enrollment uses, and must map distinct
//   accepted public keys to distinct fingerprints.
//
// Body layout (big-endian integers):
//   magic "OTGR" (4) | format version (1) | group ID (8, never 0) | roster version (4) |
//   previous roster digest (32) | location rule (1) | member count (1) |
//   per member: role (1) | added-in version (4) | identity public key (32) |
//               alias length (1) | alias (UTF-8, 1..16)
// Encoded roster = body | signature (64).
// Signature input = kSignatureDomain | body.
// Roster digest   = digest(kDigestDomain | body | signature), the complete signed roster.
// Group name      = (fingerprint(admin public key), group ID); the admin is member 0.
//
// What conflict detection covers (see the design's "Roster conflict"): a board compares
// every admin-signed roster it is offered against the versions and digests it has seen
// (the rosters it held and their previous-digest links, a bounded recent window), against
// the chain when exactly one version higher, and against the held member history. A fork
// whose branch point lies entirely in versions this board never saw cannot be seen from
// the offered roster alone; it is caught when this board later sees a roster or a peer
// digest from the other branch for a version it knows (full-digest comparison in
// handshakes, rekeys and roster transfers).

#include <array>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opentrail::identity::roster {

class PairOwner;  // group_pair.hpp

inline constexpr std::size_t kMaximumMembers = 6;
inline constexpr std::size_t kPublicKeyBytes = 32;
inline constexpr std::size_t kSignatureBytes = 64;
inline constexpr std::size_t kDigestBytes = 32;
inline constexpr std::size_t kFingerprintBytes = 32;
inline constexpr std::size_t kDigestPrefixBytes = 4;
inline constexpr std::size_t kMaximumAliasBytes = 16;
inline constexpr std::uint8_t kFormatVersion = 1;

// Bounded memory of seen (version, digest) pairs and of conflicts the user dismissed.
inline constexpr std::size_t kKnownDigests = 8;
inline constexpr std::size_t kResolvedConflicts = 4;

inline constexpr std::size_t kHeaderBytes = 4 + 1 + 8 + 4 + kDigestBytes + 1 + 1;
inline constexpr std::size_t kMemberFixedBytes = 1 + 4 + kPublicKeyBytes + 1;
inline constexpr std::size_t kMaximumBodyBytes =
    kHeaderBytes + kMaximumMembers * (kMemberFixedBytes + kMaximumAliasBytes);
inline constexpr std::size_t kMaximumRosterBytes = kMaximumBodyBytes + kSignatureBytes;

// Domain labels (retained technical prefix; no product name in protocol fields).
inline constexpr std::string_view kSignatureDomain{"OpenTrail group roster signature v1"};
inline constexpr std::string_view kDigestDomain{"OpenTrail group roster digest v1"};

using PublicKey = std::array<std::uint8_t, kPublicKeyBytes>;
using Signature = std::array<std::uint8_t, kSignatureBytes>;
using Digest = std::array<std::uint8_t, kDigestBytes>;
using Fingerprint = std::array<std::uint8_t, kFingerprintBytes>;
using DigestPrefix = std::array<std::uint8_t, kDigestPrefixBytes>;

enum class Role : std::uint8_t { admin = 1, member = 2 };
enum class LocationRule : std::uint8_t { off = 0, preferred = 1, required = 2 };

struct Member {
    Role role{Role::member};
    std::uint32_t added_in{0};
    PublicKey key{};
    std::array<std::uint8_t, kMaximumAliasBytes> alias{};
    std::uint8_t alias_bytes{0};
};

struct Roster {
    std::uint64_t group_id{0};
    std::uint32_t version{0};
    Digest previous{};
    LocationRule location_rule{LocationRule::off};
    std::array<Member, kMaximumMembers> members{};
    std::uint8_t member_count{0};
    Signature signature{};
};

struct GroupName {
    Fingerprint admin{};
    std::uint64_t group_id{0};
};

// Supplies the selected primitives. Each call returns false on internal failure.
class RosterCrypto {
public:
    virtual ~RosterCrypto() = default;
    virtual bool verify(const Signature& signature, const std::uint8_t* message,
                        std::size_t size, const PublicKey& key) const = 0;
    virtual bool digest(const std::uint8_t* message, std::size_t size, Digest& out) const = 0;
    virtual bool fingerprint(const PublicKey& key, Fingerprint& out) const = 0;
};

// The admin board's identity signing key.
class RosterSigner {
public:
    virtual ~RosterSigner() = default;
    [[nodiscard]] virtual const PublicKey& public_key() const = 0;
    virtual bool sign(const std::uint8_t* message, std::size_t size, Signature& out) const = 0;
};

enum class FormatError : std::uint8_t {
    none = 0,
    length,
    magic,
    format_version,
    group_id,
    version,
    previous_digest,
    location_rule,
    member_count,
    role,
    added_in,
    alias,
    duplicate_identity,
    duplicate_alias,
};

// Structural rules every roster must meet (independent of signature and history):
// group ID != 0; version >= 1; version 1 has an all-zero previous digest and lists only
// the admin, later versions have a non-zero previous digest; 1..6 members; member 0 is the
// only admin and was added in version 1; added-in versions strictly increase and none is
// higher than the roster's version; aliases are 1..16 bytes of UTF-8 without C0 or C1
// control characters or DEL; no duplicate public keys or aliases (exact bytes).
[[nodiscard]] FormatError validate(const Roster& roster);

// Returns the number of bytes written, or 0 (writing nothing) if the roster is invalid or
// the whole result does not fit.
std::size_t encode_body(const Roster& roster, std::uint8_t* out, std::size_t capacity);
std::size_t encode(const Roster& roster, std::uint8_t* out, std::size_t capacity);

// Strict decode of a complete encoded roster (exact length, no trailing bytes).
[[nodiscard]] FormatError decode(const std::uint8_t* bytes, std::size_t size, Roster& out);

bool roster_digest(const Roster& roster, const RosterCrypto& crypto, Digest& out);
[[nodiscard]] DigestPrefix digest_prefix(const Digest& digest);
bool group_name(const Roster& roster, const RosterCrypto& crypto, GroupName& out);
bool verify_signature(const Roster& roster, const PublicKey& admin, const RosterCrypto& crypto);

// ---- Admin board: building successor rosters (unsigned until sign()). ----
// Each successor is chained to the digest computed here from the supplied signed current
// roster (its signature is verified against its own admin first), so a caller cannot pair
// a roster with the wrong predecessor digest. Adding from the admin board's own in-person
// enrollment receipt is bound in a later step. The admin board signs a roster once, stores
// the exact signed bytes and reads them back, then sends them; after a power cut it
// resends those bytes and never signs a different roster with the same version.
bool create_first(std::uint64_t group_id, const PublicKey& admin, std::string_view alias,
                  LocationRule rule, Roster& out);
bool add_member(const Roster& current, const RosterCrypto& crypto, const PublicKey& key,
                std::string_view alias, Roster& out);
bool remove_member(const Roster& current, const RosterCrypto& crypto, const PublicKey& key,
                   Roster& out);
bool change_location_rule(const Roster& current, const RosterCrypto& crypto,
                          LocationRule rule, Roster& out);
bool sign(Roster& roster, const RosterSigner& signer);

// ---- Member board: acceptance. ----
enum class Outcome : std::uint8_t {
    accepted = 0,
    accepted_removed_self,  // stored; this board has left the group
    duplicate,              // same version and digest as the held roster
    stale,                  // older than the held roster (or than the join's minimum)
    resolved_replay,        // exact replay of a conflicting roster the user dismissed
    rejected_malformed,
    rejected_signature,     // not signed by the admin verified in person
    rejected_group,         // different group ID or admin identity
    rejected_identity,      // two listed keys derive the same fingerprint
    rejected_not_listed,    // first roster for this board does not list it
    conflict,               // admin-signed roster that forks the history (now blocked)
    blocked_by_conflict,    // unresolved conflict: no roster changes accepted
    not_member,             // left, removed or stopped trusting this group
    not_initialized,
    crypto_failure,         // a primitive failed; nothing changed
};

enum class ConflictKind : std::uint8_t {
    none = 0,
    same_version_different_roster,
    chain_break,       // exactly one version higher, previous digest does not match
    history_mismatch,  // across a gap, members disagree with the held roster's history
    known_version_mismatch,  // disagrees with a version and digest this board has seen
};

enum class State : std::uint8_t { uninitialized = 0, awaiting_roster, active, conflict, left, distrusted };

class RosterState {
public:
    // Pins the admin identity this board verified in person, the group ID (never 0) and
    // this board's own identity. minimum_version binds a fresh join to its confirmed join
    // attempt: an older roster (for example one from before an earlier removal) is refused.
    // A creator pins itself as admin and offers its signed version 1.
    bool initialize(const PublicKey& admin, std::uint64_t group_id, const PublicKey& own,
                    std::uint32_t minimum_version = 1);

    Outcome offer(const std::uint8_t* bytes, std::size_t size, const RosterCrypto& crypto);

    // Local user actions while in conflict. keep_current() also remembers the conflicting
    // roster so its exact replay is ignored instead of blocking the group again.
    bool keep_current();
    // Stop trusting the group (also the "leave" choice from a conflict): all later rosters
    // from this admin are refused.
    void stop_trusting();

    // The local user's one tap accepting a newer member (design "Joining", step 3). Only
    // for a member listed in the held roster who was added after this board; kept while
    // that member stays listed. Binding it to a trusted local input receipt is later work.
    bool accept_member(const PublicKey& key);
    [[nodiscard]] bool accepted(const PublicKey& key) const;
    // Maps a fingerprint to a member of the held roster. Lookup only: it confers no authority
    // and also answers while the roster is not active (conflict, left).
    bool key_for(const Fingerprint& fingerprint, PublicKey& out) const;

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] ConflictKind conflict_kind() const { return conflict_; }
    [[nodiscard]] bool has_roster() const { return has_roster_; }
    [[nodiscard]] const Roster& roster() const { return held_; }
    [[nodiscard]] const Digest& digest() const { return held_digest_; }
    // Fingerprints recomputed from the held roster's keys, in member order.
    [[nodiscard]] const std::array<Fingerprint, kMaximumMembers>& fingerprints() const {
        return held_fingerprints_;
    }

private:
    friend class PairOwner;  // reads the live roster at every pair step
    struct Known {
        std::uint32_t version{0};
        Digest digest{};
    };
    Outcome block(ConflictKind kind, const Digest& offered);
    bool consistent_history(const Roster& next) const;
    bool contradicts_known(std::uint32_t version, const Digest& digest) const;
    void remember(std::uint32_t version, const Digest& digest);

    State state_{State::uninitialized};
    ConflictKind conflict_{ConflictKind::none};
    PublicKey admin_{};
    PublicKey own_{};
    std::uint64_t group_id_{0};
    std::uint32_t minimum_version_{1};
    bool has_roster_{false};
    Roster held_{};
    Digest held_digest_{};
    std::array<Fingerprint, kMaximumMembers> held_fingerprints_{};
    std::array<Known, kKnownDigests> known_{};
    std::size_t known_next_{0};
    Digest conflict_digest_{};
    std::array<Digest, kResolvedConflicts> resolved_{};
    std::size_t resolved_next_{0};
    struct Accepted {
        PublicKey key{};
        std::uint32_t added_in{0};  // a removed and re-added member must be accepted again
    };
    std::array<Accepted, kMaximumMembers> accepted_{};
    std::size_t accepted_count_{0};
};

}  // namespace opentrail::identity::roster
