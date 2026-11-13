#pragma once

// Group pair setup for Trail V1 pairwise groups (docs/security/GROUP_PAIRWISE_V1.md,
// "Introduced pairs", "Restart and rekey", "Recovery of interrupted pairs";
// implementation order step 2). Host-only logic.
//
// GroupState is the single authority for one group on one board: it holds the roster and the
// pair records of every introduced pair together. It cannot be copied or moved: it stays where
// it was created, so no callback can swap it out from under a running step. Every pair step goes through it and is checked against its own live
// roster; applying a new roster removes the records of removed or re-added members and drops
// attempts the change affects. Only GroupState issues a VerifiedPairBinding.
//
// Not provided here: the handshake suite and key derivation (not selected; the binding pins
// both fresh session public keys for it), durable storage (step 3 keeps exactly one
// GroupState per group), the authenticated peer activation message (step 3), radio encoding
// of the three messages, and pairs with the admin (bound later to the in-person pairing).
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
// Epochs (D-072, D-074): an epoch is used when this board verifies the complete binding
// signed by both boards. A board proposes one above its highest used epoch; a lost start uses
// nothing, so the retry reuses the number with fresh values. A board accepts its next number,
// or one past it: a start signed by the peer at used + 2 counts as proof that the peer used the
// number between (D-074), so this board records that number as used at once. Only that peer
// can make the claim, so a dishonest member can only burn numbers on its own pair. The gap
// stays at most one under any number of lost messages. At the last epoch the pair is exhausted
// and must be set up again from a new roster entry.
//
// Activation intent (ACTIVATION_POSSIBLE) is never retired by a start alone: it stays until a
// new binding for this pair verifies, or until restart. A board holding it may start a rekey
// itself, so two uncertain boards always reach a fresh epoch. Activation and commit name the
// exact binding by its id, and only while no later epoch is known to be used (a D-074
// catch-up past an intent keeps the intent for recovery but refuses its commit).
//
// Time limit (D-073): the initiator allows the signed lifetime (60 s) from sending its start,
// the responder from receiving it; clocks are never compared. Worst-case total is 120 s.
//
// Purpose: an introduce needs the older member's acceptance and is started by the newer
// member; a rekey needs history for this membership (this board signed a binding after
// verifying the peer's part). Crossing starts: an expired own start does not count; an
// introduce beats a rekey (its sender has no history and would refuse the rekey); otherwise
// the higher epoch wins, then the lower fingerprint. The loser drops its unanswered start
// (nothing was used) and answers.

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
inline constexpr std::uint32_t kPairLifetimeMs = 60000;       // D-073, per side
inline constexpr std::uint32_t kAnswerWindowMs = 5000;        // responder budget window
inline constexpr std::size_t kAnswersPerWindow = 3;           // new answers per window (also pending slots)
inline constexpr std::size_t kResendsPerWindow = 3;           // repeated replies per window
inline constexpr std::size_t kRememberedStarts = 8;           // answered starts kept to drop repeats
inline constexpr std::size_t kRememberedChallenges = 8;       // own challenges checked for repeats
inline constexpr std::size_t kMaximumPeers = kMaximumMembers - 2;  // introduced peers per board

inline constexpr std::string_view kStartDomain{"OpenTrail group pair start v1"};
inline constexpr std::string_view kBindingDomain{"OpenTrail group pair binding v1"};

using Ephemeral = std::array<std::uint8_t, kEphemeralBytes>;
using Challenge = std::array<std::uint8_t, kChallengeBytes>;
using BootToken = std::array<std::uint8_t, kBootTokenBytes>;

enum class PairPurpose : std::uint8_t { introduce = 1, rekey = 2 };
enum class PairRole : std::uint8_t { initiator = 1, responder = 2 };

// The board's random source for challenges (one trusted component; never caller values).
class RandomSource {
public:
    virtual ~RandomSource() = default;
    virtual bool fill(std::uint8_t* out, std::size_t size) = 0;
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
// challenge in it, for the pair and roster GroupState checked at that moment.
class VerifiedPairBinding {
public:
    [[nodiscard]] PairPurpose purpose() const { return start_.purpose; }
    [[nodiscard]] PairRole local_role() const { return role_; }
    [[nodiscard]] std::uint32_t epoch() const { return start_.epoch; }
    [[nodiscard]] const PairStart& start() const { return start_; }
    [[nodiscard]] const PairReply& reply() const { return reply_; }
    [[nodiscard]] const Signature& initiator_binding_signature() const { return confirm_.signature; }
    // Names this exact binding for activation and commit: unique among every binding this
    // program issues, across all groups, until the board restarts (step 3 keeps it unique
    // across restarts for anything it stores).
    [[nodiscard]] std::uint64_t id() const { return id_; }

private:
    friend class PairRecord;
    VerifiedPairBinding(std::uint64_t id, PairRole role, const PairStart& s, const PairReply& r, const PairConfirm& c)
        : id_(id), role_(role), start_(s), reply_(r), confirm_(c) {}
    std::uint64_t id_;
    PairRole role_;
    PairStart start_;
    PairReply reply_;
    PairConfirm confirm_;
};

enum class PairOutcome : std::uint8_t {
    ok = 0,
    resent_reply,         // identical start of a pending answer: the same reply again
    duplicate_start,      // a start this board already answered and no longer holds
    local_attempt_wins,   // crossing starts: this board's own start continues
    no_pair,              // peer not an introduced member of the active roster
    roster_not_active,    // no active roster (conflict, left, not a member)
    roster_changed,       // the roster changed since the attempt or binding
    no_consent,           // older member has not accepted the newer one
    wrong_role,           // introduce must be started by the more recently added member
    wrong_purpose,        // rekey without history for this membership
    not_for_this_pair,    // group, identities or purpose do not match
    wrong_signer,         // signer is not this board's identity
    peer_roster_newer,    // (verified start) fetch the peer's roster, then retry
    peer_roster_older,    // (verified start) the peer should fetch this board's roster
    peer_roster_differs,  // (verified start) same version, other digest: fetch and offer it
    bad_lifetime,
    bad_fresh_values,     // zero, reflected or repeated fresh values
    bad_epoch,
    epoch_exhausted,
    bad_signature,
    rate_limited,         // answer budget used, or every pending slot holds a live answer
    expired,
    wrong_state,
    busy,                 // a pair step is already running (no nested calls from providers)
    crypto_failure,
};

enum class PairStep : std::uint8_t { none = 0, awaiting_reply, awaiting_confirm, bound, activation_possible, committed };

struct PairStatus {
    PairStep step{PairStep::none};  // own start first, then pending answers, then the binding stage
    PairStep binding{PairStep::none};  // none, bound, activation_possible or committed
    std::uint64_t binding_id{0};
    std::uint32_t used_epoch{0};
    std::uint32_t committed_epoch{0};
    std::uint32_t next_epoch{0};  // 0 when exhausted
    bool history{false};
    PairPurpose next_purpose{PairPurpose::introduce};
};

// Per-pair record, own start, pending answers and binding stage. Internal to GroupState.
class PairRecord {
private:
    friend class GroupState;
    struct Live {
        const Member* self{nullptr};
        const Member* peer{nullptr};
        Fingerprint self_fp{}, peer_fp{};
    };
    struct Pending {
        bool live{false};
        PairStart start{};
        PairReply reply{};
        std::uint64_t time{0}, deadline{0};
    };
    PairRecord() = default;

    PairOutcome live(const RosterState& roster, Live& out) const;
    bool acceptable(std::uint32_t epoch) const;
    std::uint32_t next_epoch() const;
    bool new_challenge(RandomSource& rng, Challenge& out);
    void expire(std::uint64_t now_ms);
    void drop_unfinished();  // own start, pending answers, and a binding not yet ACTIVATION_POSSIBLE
    static bool within_budget(const std::array<std::uint64_t, kAnswersPerWindow>& times, std::size_t count,
                              std::uint64_t now_ms);
    static void spend(std::array<std::uint64_t, kAnswersPerWindow>& times, std::size_t& next, std::size_t& count,
                      std::uint64_t now_ms);
    void bind(std::uint64_t id, PairRole role, const PairStart& s, const PairReply& r, const PairConfirm& c,
              std::optional<VerifiedPairBinding>& verified);

    PairOutcome start(const RosterState& roster, const Ephemeral& eph, const BootToken& boot, std::uint64_t now_ms,
                      const RosterSigner& signer, RandomSource& rng, PairStart& out);
    PairOutcome on_start(const RosterState& roster, const PairStart& start, const Ephemeral& eph, const BootToken& boot,
                         std::uint64_t now_ms, const RosterCrypto& crypto, const RosterSigner& signer, RandomSource& rng,
                         PairReply& out);
    PairOutcome on_reply(const RosterState& roster, const PairReply& reply, std::uint64_t now_ms,
                         const RosterCrypto& crypto, const RosterSigner& signer, std::uint64_t id, PairConfirm& out,
                         std::optional<VerifiedPairBinding>& verified);
    PairOutcome on_confirm(const RosterState& roster, const PairConfirm& confirm, std::uint64_t now_ms,
                           const RosterCrypto& crypto, std::uint64_t id, std::optional<VerifiedPairBinding>& verified);
    PairOutcome activation_possible(const RosterState& roster, std::uint64_t id);
    PairOutcome commit(const RosterState& roster, std::uint64_t id);
    void roster_applied(const RosterState& roster);
    void restart();
    PairStatus status() const;

    // Pair record (durable in step 3).
    GroupName group_{};
    PublicKey self_key_{}, peer_key_{};
    std::uint32_t self_added_{0}, peer_added_{0};
    std::uint32_t used_{0};
    std::uint32_t committed_{0};
    bool history_{false};

    // Binding stage: none, bound, activation_possible, committed (activation intent durable in step 3).
    PairStep stage_{PairStep::none};
    std::uint64_t bound_id_{0};
    PairStart bound_start_{};

    // Own start (volatile).
    bool own_live_{false};
    PairStart own_start_{};
    std::uint64_t own_time_{0}, own_deadline_{0};

    // Answers awaiting a confirm (volatile).
    std::array<Pending, kAnswersPerWindow> pending_{};

    // Replay and budget memory (volatile).
    std::array<Signature, kRememberedStarts> answered_{};
    std::size_t answered_next_{0};
    std::array<std::uint64_t, kAnswersPerWindow> answer_times_{};
    std::size_t answer_next_{0}, answer_count_{0};
    std::array<std::uint64_t, kAnswersPerWindow> resend_times_{};
    std::size_t resend_next_{0}, resend_count_{0};
    std::array<Challenge, kRememberedChallenges> own_challenges_{};
    std::size_t own_next_{0};
};

// The single authority for one group on this board. Not reentrant: while a pair step runs,
// providers (signer, crypto, random source) may not change the roster or start another step.
// Each pair step copies its message inputs first, so a provider changing the caller's objects
// has no effect on what is verified, signed or issued.
class GroupState {
public:
    GroupState() = default;
    GroupState(const GroupState&) = delete;
    GroupState& operator=(const GroupState&) = delete;
    GroupState(GroupState&&) = delete;
    GroupState& operator=(GroupState&&) = delete;

    // Roster (see RosterState). Every accepted roster is applied to the pair records; a
    // conflict drops unfinished work; stopping trust ends every pair record.
    bool initialize(const PublicKey& admin, std::uint64_t group_id, const PublicKey& own,
                    std::uint32_t minimum_version = 1);
    Outcome offer(const std::uint8_t* bytes, std::size_t size, const RosterCrypto& crypto);
    bool keep_current();
    void stop_trusting();
    bool accept_member(const PublicKey& key);
    [[nodiscard]] const RosterState& roster() const { return roster_; }

    // Pair steps. `peer` is the other member's identity key (key_for maps a fingerprint).
    PairOutcome pair_start(const PublicKey& peer, const Ephemeral& eph, const BootToken& boot, std::uint64_t now_ms,
                           const RosterSigner& signer, RandomSource& rng, PairStart& out);
    PairOutcome pair_on_start(const PairStart& start, const Ephemeral& eph, const BootToken& boot, std::uint64_t now_ms,
                              const RosterCrypto& crypto, const RosterSigner& signer, RandomSource& rng, PairReply& out);
    PairOutcome pair_on_reply(const PublicKey& peer, const PairReply& reply, std::uint64_t now_ms,
                              const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                              std::optional<VerifiedPairBinding>& verified);
    PairOutcome pair_on_confirm(const PublicKey& peer, const PairConfirm& confirm, std::uint64_t now_ms,
                                const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified);
    // Durable steps for the exact binding (VerifiedPairBinding::id): ACTIVATION_POSSIBLE before
    // the first activation message, then commit (authenticated peer activation: step 3). Both
    // recheck that the roster is the one the binding names and that no later epoch is used.
    PairOutcome pair_activation_possible(const PublicKey& peer, std::uint64_t binding_id);
    PairOutcome pair_commit(const PublicKey& peer, std::uint64_t binding_id);
    // Drops the own start, pending answers and a binding not yet ACTIVATION_POSSIBLE.
    void pair_cancel(const PublicKey& peer);
    // Power loss or recovery: every unfinished attempt and activation intent is retired, never
    // resumed.
    void restart();
    [[nodiscard]] std::optional<PairStatus> pair_status(const PublicKey& peer) const;

private:
    PairRecord* record(const PublicKey& peer, bool create);
    const PairRecord* find(const PublicKey& peer) const;
    void apply_roster();

    RosterState roster_{};
    std::array<std::optional<PairRecord>, kMaximumPeers> pairs_{};
    bool busy_{false};
};

// Which of two crossing starts continues (after expiry and purpose checks): the higher epoch,
// then the lower fingerprint.
[[nodiscard]] bool start_wins(const PairStart& mine, const PairStart& theirs);

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
