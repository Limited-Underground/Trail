#pragma once

// D-163 host-only QR invitation contract. Raw signed radio frames and durable
// authority are distinct. No target adapter or transport is selected here.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/group_pair.hpp"

namespace opentrail::identity::roster::qr {

inline constexpr std::size_t kRadioLimit = 236;
inline constexpr std::size_t kHeaderBytes = 8;
inline constexpr std::size_t kStartBytes = 220;
inline constexpr std::size_t kReplyBytes = 184;
inline constexpr std::size_t kBindingBytes = 104;
inline constexpr std::size_t kReceiptBytes = 168;
inline constexpr std::size_t kRecoveryChallengeBytes = 152;
inline constexpr std::size_t kRecoveryReplyBytes = 200;
inline constexpr std::size_t kDescriptorBaseBytes = 167;
inline constexpr std::size_t kNameBytes = 120;
inline constexpr std::size_t kNameUnits = 40;
inline constexpr std::size_t kInvites = 4;
inline constexpr std::uint64_t kRequestMs = 300000;
inline constexpr std::uint64_t kInviteMs = 180000;
inline constexpr std::uint64_t kFirstCreationMs = 15000;
inline constexpr std::uint64_t kLaterCreationMs = 10000;
inline constexpr std::uint64_t kFirstCopyMs = 6500;
inline constexpr std::uint64_t kLaterCopyMs = 3000;

enum class Side : std::uint8_t { joiner = 1, admin = 2 };
enum class Kind : std::uint8_t {
    start = 1, reply = 2, admission = 3, confirmed = 4, prepared = 5,
    committed = 6, aborted = 7, recovery_challenge = 8, recovery_prepared = 9,
    recovery_committed = 10, recovery_aborted = 11,
};
enum class Review : std::uint8_t { fingerprints, transcript };

using Nonce = std::array<std::uint8_t, 16>;
struct RadioFrame {
    std::array<std::uint8_t, kRadioLimit> bytes{};
    std::size_t size{};
};
struct DescriptorBytes {
    std::array<std::uint8_t, kDescriptorBaseBytes + kNameBytes> bytes{};
    std::size_t size{};
};
struct Descriptor {
    PublicKey admin{};
    std::uint64_t group{};
    std::uint32_t roster_version{};
    Digest roster_digest{};
    Nonce invite_id{};
    LocationRule rule{LocationRule::off};
    std::array<std::uint8_t, kNameBytes> label{};
    std::uint8_t label_bytes{};
};
struct Fresh {
    Ephemeral ephemeral{};
    Challenge challenge{};
    BootToken boot{};
};
struct Start {
    Digest scope{};
    std::uint32_t epoch{};
    PublicKey joiner{};
    Fresh fresh{};
};
struct Reply {
    Digest start_hash{};
    Fresh fresh{};
};
struct Decision {
    Digest scope{}, transaction{}, manifest{};
    Side side{};
    Kind kind{};
};
struct PublicState {
    PublicKey joiner{}, admin{};
    Digest roster_digest{}, transcript{};
    Nonce invite_id{};
    std::uint64_t group{};
    std::uint32_t roster_version{}, epoch{};
    LocationRule rule{};
};
struct Retained {
    Decision decision{};
    PublicState state{};
    PublicKey local{}, peer{};
};
struct Timing {
    std::uint64_t request_started{}, invitation_issued{};
    bool deadline(std::uint64_t& out) const;
};
struct LocalInvite {
    const DescriptorBytes* issued{};
    Timing timing{};
};
using LocalInvites = std::array<LocalInvite, kInvites>;

// Codec only. Signing bytes does not insert a locally issued invitation.
bool issue_descriptor(const Descriptor&, const RosterCrypto&, const RosterSigner&, DescriptorBytes&);
bool verify_descriptor(const DescriptorBytes&, const RosterCrypto&, Descriptor&);
bool scope_digest(const DescriptorBytes&, const PublicKey& joiner, const RosterCrypto&, Digest&);
bool sign_start(const Start&, const RosterCrypto&, const RosterSigner&, RadioFrame&);
bool verify_start(const RadioFrame&, const RosterCrypto&, Start&);
bool resolve_start(const RadioFrame&, const LocalInvites&,
                   const PublicKey& admin, std::uint64_t now, const RosterCrypto&, std::size_t& slot);
bool sign_reply(const RadioFrame& start, const Fresh&, const RosterCrypto&, const RosterSigner&, RadioFrame&);
bool verify_reply(const RadioFrame&, const PublicKey& admin, const RosterCrypto&, Reply&);
bool verify_receipt(const RadioFrame&, const PublicKey& signer, const RosterCrypto&, Decision&);
bool public_manifest(const PublicState&, const Digest& scope, const RosterCrypto&, Digest&);

// Boot-scoped authorized issuer owned by the admin's trusted local port.
// Its caller supplies a new, unique 128-bit ID from board entropy. The codec
// rejects zero but cannot attest entropy or establish uniqueness across boot.
class IssuedInvites {
public:
    bool issue(const Descriptor&, Timing, std::uint64_t now, const RosterCrypto&,
               const RosterSigner&, DescriptorBytes&);
    bool revoke(const DescriptorBytes&);
    LocalInvites view() const;
private:
    struct Entry {
        DescriptorBytes wire{};
        Timing timing{};
        std::uint64_t group{};
    };
    std::array<Entry, kInvites> entries_{};
    std::uint64_t last_{};
    bool busy_{};
};

// A scheduler may retry only the first exact local frame of each phase.
// Admission is checked afresh immediately before a send; queued work alone
// does not spend a copy. This is a host policy model, with no radio callbacks.
class Copies {
public:
    bool offer(const RadioFrame& frame, const Timing&, std::uint64_t now);
    bool accepted(const RadioFrame& frame, const Timing&, std::uint64_t now);
private:
    std::array<RadioFrame, 11> frame_{};
    std::array<unsigned, 11> accepted_{};
    std::array<std::uint64_t, 11> accepted_at_{};
    std::optional<Timing> timing_;
    std::uint64_t last_{};
};

// These are trusted local ports. A radio/phone payload cannot provide these
// observations. Review binds the exact frame and the current display lease.
// Persist must refuse a late positive marker after payload readback, preserve
// exact prior records across uncertainty, and atomically consume the original
// invite ID independently of the derived scope and candidate peer. A later
// retained phase may only advance the same exact transaction and identities.
// The host adapter exercises failure/readback cuts.
class Authority {
public:
    virtual ~Authority() = default;
    virtual std::uint64_t now() const = 0;
    virtual LocalInvites issued_invites() const = 0;
    virtual bool fresh_unclaimed(const Nonce&) const = 0;
    // Validate the exact locally issued or accepted invite, current roster,
    // authoritative pair epoch under this local owner. Consuming an invite
    // may preserve the already-running exact transaction's authorization only.
    virtual bool current(const Descriptor&, const PublicKey& joiner, std::uint32_t epoch) const = 0;
    virtual bool approve(Review, Side, const Descriptor&, const PublicKey& peer, const Digest&) const = 0;
    virtual bool verified_handshake(const Digest& origin, const Digest& initiator_key,
                                    const Digest& responder_key, Digest& out) const = 0;
    virtual bool persist(const Retained&, std::uint64_t original_deadline) = 0;
    virtual bool readback(const Retained&) const = 0;
    virtual bool successor_readback(const std::uint8_t*, std::size_t) const = 0;
};

class Session {
public:
    Session(Side, Timing, const RosterCrypto&, const RosterSigner&, Authority&);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool begin(const DescriptorBytes&, const RadioFrame& start, const RadioFrame& reply);
    bool admission(RadioFrame& out);
    bool accept_admission(const RadioFrame&);
    bool enter_transcript();
    bool confirmation(RadioFrame& out);
    bool accept_confirmation(const RadioFrame&);
    bool receipt(Kind, RadioFrame& out);
    bool accept_receipt(const RadioFrame&);

    // Admin's proposal is still not membership. Membership evidence requires
    // the exact verified signed successor and the authoritative readback.
    bool successor(const Roster& current, std::string_view alias, Roster& out);
    bool membership(const Roster& previous, std::string_view alias,
                    const std::uint8_t* signed_successor, std::size_t bytes);

private:
    class Operation {
    public:
        explicit Operation(Session& owner) : owner_(owner), entered_(!owner.busy_) {
            if (entered_) owner_.busy_ = true;
            else owner_.failed_ = true;
        }
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        ~Operation() { if (entered_) owner_.busy_ = false; }
        explicit operator bool() const { return entered_; }
    private:
        Session& owner_;
        bool entered_;
    };
    bool live(std::uint64_t reserve = 1);
    bool local_binding(Kind, const Digest&, RadioFrame&);
    bool peer_binding(Kind, const Digest&, const RadioFrame&);
    bool shape_successor(const Roster&, std::string_view, Roster&);
    bool decision(Kind, Decision& out) const;
    Retained retained(const Decision&) const;
    const Side local_;
    const Timing timing_;
    const RosterCrypto& crypto_;
    const RosterSigner& signer_;
    Authority& authority_;
    Descriptor descriptor_{};
    PublicKey joiner_{}, local_key_{}, peer_{};
    Digest scope_{}, origin_{}, transcript_{}, manifest_{}, transaction_{};
    PublicState state_{};
    Fresh initiator_{}, responder_{};
    std::uint32_t epoch_{};
    std::uint64_t deadline_{}, last_{};
    RadioFrame local_admission_{}, local_confirmation_{}, peer_admission_frame_{}, peer_confirmation_frame_{};
    std::optional<Decision> local_decision_, peer_decision_;
    RadioFrame staged_receipt_{}, peer_receipt_{};
    Digest successor_digest_{};
    bool active_{}, failed_{}, busy_{}, peer_admission_{}, peer_confirmation_{}, membership_complete_{};
};

// Recovery authenticates retained evidence only. It provides no Session rearm
// and no authority to start a roster change or new invitation after reboot.
class RecoveryProbe {
public:
    RecoveryProbe(const Retained&, const RosterCrypto&, const RosterSigner&, Authority&);
    RecoveryProbe(const RecoveryProbe&) = delete;
    RecoveryProbe& operator=(const RecoveryProbe&) = delete;
    bool challenge(const Nonce&, RadioFrame&);
    bool observe(const RadioFrame&, Kind& peer_stage);
private:
    const Retained retained_;
    const RosterCrypto& crypto_;
    const RosterSigner& signer_;
    Authority& authority_;
    RadioFrame pending_{};
    bool consumed_{};
};
bool recovery_reply(const Retained& retained, const RadioFrame& challenge,
                    const Nonce&, const RosterCrypto&, const RosterSigner&, Authority&, RadioFrame&);

}  // namespace opentrail::identity::roster::qr
