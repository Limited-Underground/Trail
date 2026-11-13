#pragma once

// Group pair setup for Trail V1 pairwise groups (docs/security/GROUP_PAIRWISE_V1.md,
// "Introduced pairs", "Restart and rekey", "Recovery of interrupted pairs";
// implementation order step 2). Host-only logic.
//
// What this provides: the typed pair authority that only the roster can issue, the
// binding both long-term identities sign, the start / reply / confirm exchange that
// proves each side's own fresh challenge was signed back by the peer, the "who starts"
// rules, the epoch rules, and the interrupted-attempt rule (never resumed).
//
// What it does not provide: the handshake suite and key derivation (not selected; the
// output VerifiedPairBinding pins both fresh session public keys for it), durable
// storage (PairLedger models the durable steps in memory; the storage owner comes in
// step 3), and the radio encoding of the three messages. Signatures use the same
// RosterCrypto / RosterSigner primitives as the roster (identity keys).
//
// Signed bytes (big-endian integers), domain-separated from each other and the roster:
//   start   = kStartDomain   | common | initiator ephemeral | initiator challenge |
//             initiator boot token
//   binding = kBindingDomain | common | initiator ephemeral | responder ephemeral |
//             initiator challenge | responder challenge | initiator boot token |
//             responder boot token
//   common  = purpose (1) | admin fingerprint (32) | group ID (8) | roster version (4) |
//             roster digest (32) | epoch (4) | initiator fingerprint (32) |
//             responder fingerprint (32) | lifetime ms (4)
// The initiator signs start and binding; the responder signs binding.
//
// Epochs: every pair keeps the highest epoch it has ever reserved (durable); each side
// reserves before it sends. A board proposes highest + 1 and accepts any proposal higher
// than its own highest, only from a start signed by the peer's identity. Refining the
// design's "exactly epoch + 1": a start lost on the radio uses up an epoch on the
// initiator only, so after lost starts the peer must accept a skip or the two boards
// never agree again. Zero, equal, older and wrap (no epoch above 0xFFFFFFFF) are
// refused; a replayed start cannot exceed an epoch the peer itself reserved.
// Purpose: an initiator starts a first pairing (introduce) until it has committed one,
// then rekeys. A responder answers an introduce at any time (its authority still requires
// acceptance and the role order) and a rekey only after an earlier attempt with that peer
// (committed or interrupted). So boards that disagree after an interruption still meet.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "opentrail/group_roster.hpp"

namespace opentrail::identity::roster {

inline constexpr std::size_t kEphemeralBytes = 32;
inline constexpr std::size_t kChallengeBytes = 32;
inline constexpr std::size_t kBootTokenBytes = 16;
inline constexpr std::uint32_t kMaximumPairLifetimeMs = 60000;  // invitation window
inline constexpr std::uint32_t kStartAnswerIntervalMs = 5000;   // per peer rate limit

inline constexpr std::string_view kStartDomain{"OpenTrail group pair start v1"};
inline constexpr std::string_view kBindingDomain{"OpenTrail group pair binding v1"};

using Ephemeral = std::array<std::uint8_t, kEphemeralBytes>;
using Challenge = std::array<std::uint8_t, kChallengeBytes>;
using BootToken = std::array<std::uint8_t, kBootTokenBytes>;

enum class PairPurpose : std::uint8_t { introduce = 1, rekey = 2 };
enum class PairRole : std::uint8_t { initiator = 1, responder = 2 };

// Issued only by RosterState::pair_authority. Names the exact roster (version and full
// digest), the group, and both identities. For introduce it stands in for the in-person
// fingerprint review and local confirmation of a first pairing.
class PairAuthority {
public:
    [[nodiscard]] PairPurpose purpose() const { return purpose_; }
    [[nodiscard]] const GroupName& group() const { return group_; }
    [[nodiscard]] std::uint32_t roster_version() const { return roster_version_; }
    [[nodiscard]] const Digest& roster_digest() const { return roster_digest_; }
    [[nodiscard]] const PublicKey& local_key() const { return local_key_; }
    [[nodiscard]] const PublicKey& peer_key() const { return peer_key_; }
    [[nodiscard]] const Fingerprint& local_fingerprint() const { return local_fp_; }
    [[nodiscard]] const Fingerprint& peer_fingerprint() const { return peer_fp_; }
    // The role this board must take for an introduce (the more recently added member
    // starts). For a rekey either side may start.
    [[nodiscard]] PairRole introduce_role() const { return introduce_role_; }

private:
    friend class RosterState;
    PairAuthority() = default;
    PairPurpose purpose_{PairPurpose::introduce};
    GroupName group_{};
    std::uint32_t roster_version_{0};
    Digest roster_digest_{};
    PublicKey local_key_{}, peer_key_{};
    Fingerprint local_fp_{}, peer_fp_{};
    PairRole introduce_role_{PairRole::initiator};
};

// Durable per-pair epoch record (modelled in memory; step 3 stores it).
enum class PairStep : std::uint8_t { none = 0, prepared, activation_possible, committed };

class PairLedger {
public:
    [[nodiscard]] std::uint32_t committed_epoch() const { return committed_; }
    [[nodiscard]] std::uint32_t highest_epoch() const { return highest_; }
    [[nodiscard]] PairStep step() const { return step_; }
    [[nodiscard]] std::uint32_t attempt_epoch() const { return attempt_; }

    [[nodiscard]] std::uint32_t next_epoch() const;          // 0 if none is possible
    [[nodiscard]] bool acceptable(std::uint32_t epoch) const;
    // Reserves an epoch for a new attempt (PREPARED). Any unfinished attempt is retired.
    bool reserve(std::uint32_t epoch);
    // Before the first activation message: ACTIVATION_POSSIBLE for the reserved epoch.
    bool activation_possible(std::uint32_t epoch);
    // Local durable commit after authenticated peer activation evidence.
    bool commit(std::uint32_t epoch);
    // Restart or contact after an interruption: an unfinished attempt is retired, never
    // resumed. Its epoch stays used.
    void restart();

private:
    friend class PairInitiator;
    bool release(std::uint32_t epoch);
    std::uint32_t committed_{0};
    std::uint32_t highest_{0};
    std::uint32_t attempt_{0};
    std::uint32_t before_attempt_{0};
    PairStep step_{PairStep::none};
};

struct PairStart {
    PairPurpose purpose{PairPurpose::introduce};
    GroupName group{};
    std::uint32_t roster_version{0};
    Digest roster_digest{};
    std::uint32_t epoch{0};
    Fingerprint initiator{}, responder{};
    std::uint32_t lifetime_ms{0};
    Ephemeral initiator_ephemeral{};
    Challenge initiator_challenge{};
    BootToken initiator_boot{};
    Signature signature{};  // initiator, over the start bytes
};

struct PairReply {
    Ephemeral responder_ephemeral{};
    Challenge responder_challenge{};
    BootToken responder_boot{};
    Signature signature{};  // responder, over the binding bytes
};

struct PairConfirm {
    Signature signature{};  // initiator, over the binding bytes
};

// Proof that both identities signed the complete binding with this board's own fresh
// challenge in it. Only the pair attempts below create it. The selected handshake derives
// keys from it; the old pair may be retired only after this exists.
class VerifiedPairBinding {
public:
    [[nodiscard]] PairPurpose purpose() const { return start_.purpose; }
    [[nodiscard]] PairRole local_role() const { return role_; }
    [[nodiscard]] std::uint32_t epoch() const { return start_.epoch; }
    [[nodiscard]] const PairStart& start() const { return start_; }
    [[nodiscard]] const PairReply& reply() const { return reply_; }
    [[nodiscard]] const Signature& initiator_binding_signature() const { return confirm_.signature; }

private:
    friend class PairInitiator;
    friend class PairResponder;
    VerifiedPairBinding(PairRole role, const PairStart& s, const PairReply& r, const PairConfirm& c)
        : role_(role), start_(s), reply_(r), confirm_(c) {}
    PairRole role_;
    PairStart start_;
    PairReply reply_;
    PairConfirm confirm_;
};

enum class PairOutcome : std::uint8_t {
    ok = 0,
    wrong_role,           // introduce must be started by the more recently added member
    wrong_purpose,        // introduce with a committed pair, or rekey without one
    bad_lifetime,
    bad_fresh_values,     // zero or repeated ephemeral / challenge / boot token
    bad_epoch,            // not acceptable under the epoch rule
    ledger_busy,          // ledger refused the reservation
    not_for_this_pair,    // group, identities or roles do not match the authority
    peer_roster_newer,    // fetch the peer's roster first, then retry
    peer_roster_older,    // the peer should fetch this board's roster
    peer_roster_differs,  // same version, different digest: fetch and offer it (conflict check)
    bad_signature,
    rate_limited,
    expired,
    wrong_state,
    crypto_failure,
};

// Which of two simultaneous rekey starts continues: the one started by the lower
// fingerprint (bytewise). Returns true if the local start continues.
[[nodiscard]] bool local_start_wins(const Fingerprint& local, const Fingerprint& peer);

class PairInitiator {
public:
    // Reserves ledger.next_epoch() (unless `epoch` is given) and produces the signed start.
    PairOutcome start(const PairAuthority& authority, PairLedger& ledger, const Ephemeral& eph,
                      const Challenge& challenge, const BootToken& boot, std::uint32_t lifetime_ms,
                      std::uint64_t now_ms, const RosterSigner& signer, PairStart& out);
    // Verifies the responder's binding signature (it contains this board's challenge) and
    // produces the confirm plus the verified binding.
    PairOutcome on_reply(const PairReply& reply, std::uint64_t now_ms, const RosterCrypto& crypto,
                         const RosterSigner& signer, PairConfirm& out,
                         std::optional<VerifiedPairBinding>& verified);
    void cancel() { state_ = State::done; }
    // Simultaneous starts: the side whose start loses (local_start_wins() false) drops its
    // own start while it is still unanswered and gives back that epoch reservation, so it
    // can answer the winning start. Refused once a reply was accepted (a binding exists).
    bool yield_to_peer(PairLedger& ledger);

private:
    enum class State : std::uint8_t { idle, awaiting_reply, done };
    State state_{State::idle};
    PairStart start_{};
    PublicKey peer_key_{};
    std::uint64_t deadline_{0};
};

// One per peer, long-lived: it also holds the per-peer answer rate limit. A new start
// replaces an unfinished attempt (the ledger retires it).
class PairResponder {
public:
    // Checks the start against this board's own authority for the claimed initiator,
    // reserves the epoch and answers with its binding signature.
    PairOutcome on_start(const PairAuthority& authority, PairLedger& ledger, const PairStart& start,
                         const Ephemeral& eph, const Challenge& challenge, const BootToken& boot,
                         std::uint64_t now_ms, const RosterCrypto& crypto, const RosterSigner& signer,
                         PairReply& out);
    // Verifies the initiator's binding signature (it contains this board's challenge).
    PairOutcome on_confirm(const PairConfirm& confirm, std::uint64_t now_ms, const RosterCrypto& crypto,
                           std::optional<VerifiedPairBinding>& verified);
    void cancel() { state_ = State::done; }

private:
    enum class State : std::uint8_t { idle, awaiting_confirm, done };
    State state_{State::idle};
    PairStart start_{};
    PairReply reply_{};
    PublicKey peer_key_{};
    std::uint64_t deadline_{0};
    bool answered_{false};
    std::uint64_t last_answer_ms_{0};
};

// Signing inputs, exposed for tests and the future wire encoder.
std::size_t start_bytes(const PairStart& start, std::uint8_t* out, std::size_t capacity);
std::size_t binding_bytes(const PairStart& start, const PairReply& reply, std::uint8_t* out,
                          std::size_t capacity);
inline constexpr std::size_t kCommonBytes = 1 + kFingerprintBytes + 8 + 4 + kDigestBytes + 4 +
                                            2 * kFingerprintBytes + 4;
inline constexpr std::size_t kStartBytes =
    kStartDomain.size() + kCommonBytes + kEphemeralBytes + kChallengeBytes + kBootTokenBytes;
inline constexpr std::size_t kBindingBytes = kBindingDomain.size() + kCommonBytes +
                                             2 * (kEphemeralBytes + kChallengeBytes + kBootTokenBytes);

}  // namespace opentrail::identity::roster
