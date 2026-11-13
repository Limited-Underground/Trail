#pragma once

// Group pair setup for Trail V1 pairwise groups (docs/security/GROUP_PAIRWISE_V1.md,
// "Introduced pairs", "Restart and rekey", "Recovery of interrupted pairs";
// implementation order step 2). Host-only logic.
//
// One PairOwner per pair on each board. It is the only thing that can start, answer or
// finish a pair setup, and the only thing that issues a VerifiedPairBinding. It binds the
// group name, both identities and both members' added-in versions (the membership
// incarnation), keeps the pair record private, and rechecks the live roster at every step:
// a removed peer, a re-added peer (new incarnation) or a roster conflict stops it.
//
// Not provided here: the handshake suite and key derivation (not selected; the binding pins
// both fresh session public keys for it), durable storage of the record (step 3), the
// authenticated peer activation message (step 3), radio encoding of the three messages, and
// pairs with the admin (their history comes from the in-person first pairing, bound later).
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
// Epochs (D-072): an epoch is used only when this board has verified the complete binding
// signed by both boards. A board proposes one above its highest used epoch; a lost start uses
// nothing, so the retry reuses the number with fresh values. A board accepts its next number,
// or one past it to catch up with a peer that used one this board missed (the peer verified
// the binding, this board did not see the confirm); the gap cannot grow beyond one. At the
// last epoch the pair is exhausted and must be set up again from a new roster entry.
//
// Time limit (D-073): the initiator allows the signed lifetime (60 s) from sending its start,
// the responder from receiving it; clocks are never compared. Worst-case total is 120 s.
//
// Purpose: an introduce needs the older member's acceptance and is started by the newer
// member; a rekey needs history for this incarnation (this board signed a binding after
// verifying the peer's part). Crossing starts: the higher epoch wins, then the lower
// fingerprint; the loser drops its unanswered start (nothing was used) and answers.

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
inline constexpr std::size_t kAnswersPerWindow = 3;           // distinct starts answered per window
inline constexpr std::size_t kRememberedStarts = 4;           // answered starts kept to drop repeats
inline constexpr std::size_t kRememberedLocalValues = 4;      // own challenges / ephemerals not reused

inline constexpr std::string_view kStartDomain{"OpenTrail group pair start v1"};
inline constexpr std::string_view kBindingDomain{"OpenTrail group pair binding v1"};

using Ephemeral = std::array<std::uint8_t, kEphemeralBytes>;
using Challenge = std::array<std::uint8_t, kChallengeBytes>;
using BootToken = std::array<std::uint8_t, kBootTokenBytes>;

enum class PairPurpose : std::uint8_t { introduce = 1, rekey = 2 };
enum class PairRole : std::uint8_t { initiator = 1, responder = 2 };

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
// challenge in it, for the pair and roster the owner checked at that moment. Issued only by
// PairOwner. Activation of its keys still goes through the owner's durable steps.
class VerifiedPairBinding {
public:
    [[nodiscard]] PairPurpose purpose() const { return start_.purpose; }
    [[nodiscard]] PairRole local_role() const { return role_; }
    [[nodiscard]] std::uint32_t epoch() const { return start_.epoch; }
    [[nodiscard]] const PairStart& start() const { return start_; }
    [[nodiscard]] const PairReply& reply() const { return reply_; }
    [[nodiscard]] const Signature& initiator_binding_signature() const { return confirm_.signature; }

private:
    friend class PairOwner;
    VerifiedPairBinding(PairRole role, const PairStart& s, const PairReply& r, const PairConfirm& c)
        : role_(role), start_(s), reply_(r), confirm_(c) {}
    PairRole role_;
    PairStart start_;
    PairReply reply_;
    PairConfirm confirm_;
};

enum class PairOutcome : std::uint8_t {
    ok = 0,
    resent_reply,         // identical start of the current attempt: the same reply again
    duplicate_start,      // a start this board already answered; ignored
    local_attempt_wins,   // crossing starts: this board's own start continues
    stale_owner,          // peer removed or re-added (new incarnation): make a new owner
    roster_not_active,    // no active roster (conflict, left, not a member)
    roster_changed,       // the roster changed during the attempt; attempt dropped
    no_consent,           // older member has not accepted the newer one
    wrong_role,           // introduce must be started by the more recently added member
    wrong_purpose,        // rekey without history for this incarnation
    not_for_this_pair,    // group, identities or roles do not match this owner
    wrong_signer,         // signer is not this board's identity
    peer_roster_newer,    // (verified start) fetch the peer's roster, then retry
    peer_roster_older,    // (verified start) the peer should fetch this board's roster
    peer_roster_differs,  // (verified start) same version, other digest: fetch and offer it
    bad_lifetime,
    bad_fresh_values,     // zero, reflected or reused fresh values
    bad_epoch,
    epoch_exhausted,
    bad_signature,
    rate_limited,
    expired,
    wrong_state,
    crypto_failure,
};

// Durable steps of the current attempt (step 3 stores them).
enum class PairStep : std::uint8_t { none = 0, awaiting_reply, awaiting_confirm, bound, activation_possible, committed };

class PairOwner {
public:
    // The owner for this board's pair with `peer` in the roster's group. Only pairs of two
    // non-admin members (introduced pairs); both must be listed in an active roster.
    static std::optional<PairOwner> create(const RosterState& roster, const PublicKey& peer);

    PairOwner(PairOwner&&) = default;
    PairOwner& operator=(PairOwner&&) = default;
    PairOwner(const PairOwner&) = delete;
    PairOwner& operator=(const PairOwner&) = delete;

    PairOutcome start(const RosterState& roster, const Ephemeral& eph, const Challenge& challenge,
                      const BootToken& boot, std::uint64_t now_ms, const RosterSigner& signer, PairStart& out);
    PairOutcome on_start(const RosterState& roster, const PairStart& start, const Ephemeral& eph,
                         const Challenge& challenge, const BootToken& boot, std::uint64_t now_ms,
                         const RosterCrypto& crypto, const RosterSigner& signer, PairReply& out);
    PairOutcome on_reply(const RosterState& roster, const PairReply& reply, std::uint64_t now_ms,
                         const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                         std::optional<VerifiedPairBinding>& verified);
    PairOutcome on_confirm(const RosterState& roster, const PairConfirm& confirm, std::uint64_t now_ms,
                           const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified);

    // Durable steps, only for the attempt this owner verified: ACTIVATION_POSSIBLE before the
    // first activation message, then commit (authenticated peer activation: step 3).
    bool activation_possible();
    bool commit();
    // Power loss or recovery: the volatile attempt is gone; an unfinished attempt is retired,
    // never resumed. Used epochs stay used.
    void restart();
    void cancel();

    [[nodiscard]] PairStep step() const { return step_; }
    [[nodiscard]] std::uint32_t used_epoch() const { return used_; }
    [[nodiscard]] std::uint32_t committed_epoch() const { return committed_; }
    [[nodiscard]] std::uint32_t next_epoch() const;  // 0 when exhausted
    [[nodiscard]] bool has_history() const { return history_; }
    [[nodiscard]] bool stale() const { return stale_; }
    [[nodiscard]] PairPurpose next_purpose() const {
        return history_ ? PairPurpose::rekey : PairPurpose::introduce;
    }

private:
    PairOwner() = default;
    struct Live {
        const Member* self{nullptr};
        const Member* peer{nullptr};
        Fingerprint self_fp{}, peer_fp{};
    };
    PairOutcome live(const RosterState& roster, Live& out);
    bool acceptable(std::uint32_t epoch) const;
    bool fresh_local(const Ephemeral& eph, const Challenge& challenge) const;
    void remember_local(const Ephemeral& eph, const Challenge& challenge);
    bool attempt_expired(std::uint64_t now_ms);
    void drop_attempt();

    // Pair record (durable in step 3).
    GroupName group_{};
    PublicKey self_key_{}, peer_key_{};
    std::uint32_t self_added_{0}, peer_added_{0};
    std::uint32_t used_{0};
    std::uint32_t committed_{0};
    bool history_{false};
    bool stale_{false};

    // Current attempt (volatile).
    PairStep step_{PairStep::none};
    PairRole role_{PairRole::initiator};
    PairStart start_{};
    PairReply reply_{};
    PairConfirm confirm_{};
    std::uint64_t attempt_time_{0}, deadline_{0};
    std::uint32_t attempt_roster_version_{0};
    Digest attempt_roster_digest_{};

    // Replay and budget memory (volatile).
    std::array<Signature, kRememberedStarts> answered_{};
    std::size_t answered_next_{0};
    std::array<std::uint64_t, kAnswersPerWindow> answer_times_{};
    std::size_t answer_count_{0};
    std::size_t answer_next_{0};
    std::array<Challenge, kRememberedLocalValues> own_challenges_{};
    std::array<Ephemeral, kRememberedLocalValues> own_ephemerals_{};
    std::size_t own_next_{0};
};

// Which of two crossing starts continues: the higher epoch, then the lower fingerprint.
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
