#pragma once

// F8 step 2c (D-170): one QR group join between two boards over the radio.
// It runs the signed QR session (group_qr_invite.hpp) and the Noise XK binding
// (group_qr_noise_binding.hpp) as one ordered exchange of radio frames:
//
//   joiner                         admin
//   Start, Noise 1   ----------->
//                    <-----------  Reply, Noise 2
//   Noise 3          ----------->
//   admission        <---------->  admission        (each after local approval)
//   confirmation     <---------->  confirmation     (each after local approval)
//   prepared         <---------->  prepared         (durable before it is sent)
//   committed        <---------->  committed        (durable before it is sent)
//
// QR frames keep their own signed layout ("OTGJ"). The three Noise messages go
// in a small envelope ("OTGN") that names the QR scope, so a board drops Noise
// messages for any other join. Every frame fits the 236-byte QR radio limit.
//
// Retries follow qr::Copies (a bounded number of exact copies per phase). A
// Noise message follows its companion QR frame each time that frame is
// accepted by the radio. A repeated peer frame from an earlier phase makes the
// board offer its own frame of that phase again, so one lost frame does not
// stall the exchange. The trusted local ports (approvals, durable receipts,
// clock, issued invites) stay with the board's qr::Authority; this class adds
// only the Noise result. Host model only: no radio callbacks, one app task.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "opentrail/group_qr_invite.hpp"
#include "opentrail/group_qr_noise_binding.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::security_evaluation {

namespace qr_join {

namespace roster = identity::roster;
namespace qr = identity::roster::qr;

inline constexpr std::array<std::uint8_t, 4> kQrMagic{'O', 'T', 'G', 'J'};
inline constexpr std::array<std::uint8_t, 4> kNoiseMagic{'O', 'T', 'G', 'N'};
inline constexpr std::uint8_t kNoiseVersion = 1;
// magic(4) version(1) stage(1) sender side(1) zero(1) scope(32) message
inline constexpr std::size_t kNoiseHeaderBytes = 8 + 32;

inline constexpr std::size_t noise_message_bytes(std::uint8_t stage) {
    return stage == 1 ? GroupQrNoiseBinding::kMessageOneBytes :
           stage == 2 ? GroupQrNoiseBinding::kMessageTwoBytes :
           stage == 3 ? GroupQrNoiseBinding::kMessageThreeBytes : 0;
}
inline constexpr qr::Side noise_sender(std::uint8_t stage) {
    return stage == 2 ? qr::Side::admin : qr::Side::joiner;
}
static_assert(kNoiseHeaderBytes + GroupQrNoiseBinding::kMessageThreeBytes <= qr::kRadioLimit);

struct NoiseFrame {
    std::uint8_t stage{};
    roster::Digest scope{};
    const std::uint8_t* message{};
    std::size_t size{};
};

inline bool encode_noise(std::uint8_t stage, const roster::Digest& scope, const std::uint8_t* message,
                         std::size_t size, qr::RadioFrame& out) {
    if (message == nullptr || size == 0 || size != noise_message_bytes(stage)) return false;
    qr::RadioFrame frame{};
    std::memcpy(frame.bytes.data(), kNoiseMagic.data(), kNoiseMagic.size());
    frame.bytes[4] = kNoiseVersion;
    frame.bytes[5] = stage;
    frame.bytes[6] = static_cast<std::uint8_t>(noise_sender(stage));
    frame.bytes[7] = 0;
    std::memcpy(frame.bytes.data() + 8, scope.data(), scope.size());
    std::memcpy(frame.bytes.data() + kNoiseHeaderBytes, message, size);
    frame.size = kNoiseHeaderBytes + size;
    out = frame;
    return true;
}

// Exact shape only. The scope and message are authenticated by Noise and by
// comparing the scope with the live join.
inline bool decode_noise(const std::uint8_t* bytes, std::size_t size, NoiseFrame& out) {
    if (bytes == nullptr || size < kNoiseHeaderBytes ||
        std::memcmp(bytes, kNoiseMagic.data(), kNoiseMagic.size()) != 0 || bytes[4] != kNoiseVersion ||
        bytes[7] != 0) return false;
    const auto stage = bytes[5];
    if (noise_message_bytes(stage) == 0 || size != kNoiseHeaderBytes + noise_message_bytes(stage) ||
        bytes[6] != static_cast<std::uint8_t>(noise_sender(stage))) return false;
    NoiseFrame frame{};
    frame.stage = stage;
    std::memcpy(frame.scope.data(), bytes + 8, frame.scope.size());
    frame.message = bytes + kNoiseHeaderBytes;
    frame.size = size - kNoiseHeaderBytes;
    out = frame;
    return true;
}

inline bool is_qr_frame(const std::uint8_t* bytes, std::size_t size) {
    return bytes != nullptr && size >= qr::kHeaderBytes && size <= qr::kRadioLimit &&
           std::memcmp(bytes, kQrMagic.data(), kQrMagic.size()) == 0;
}

// True for any frame that belongs to a QR group join, valid or not, so the
// radio loop can route it here instead of to pairing or messaging.
inline bool is_join_frame(const std::uint8_t* bytes, std::size_t size) {
    return is_qr_frame(bytes, size) ||
           (bytes != nullptr && size >= kNoiseMagic.size() &&
            std::memcmp(bytes, kNoiseMagic.data(), kNoiseMagic.size()) == 0);
}

}  // namespace qr_join

// Trusted local port: begins Noise with this board's own X25519 static, derived
// from its Ed25519 identity (D-172). The static never leaves the port.
class GroupQrJoinIdentity {
public:
    virtual ~GroupQrJoinIdentity() = default;
    virtual bool begin_noise(GroupQrNoiseBinding&, identity::roster::qr::Side,
                             const identity::roster::PublicKey& peer,
                             const ot_noise_xk_keypair& ephemeral,
                             const identity::roster::Digest& scope) = 0;
};

class GroupQrJoinConversation final : private identity::roster::qr::Authority {
public:
    using Side = identity::roster::qr::Side;
    using Kind = identity::roster::qr::Kind;
    using RadioFrame = identity::roster::qr::RadioFrame;
    using Key = GroupQrNoiseBinding::Key;

    enum class Phase : std::uint8_t {
        idle, handshake, admission, confirmation, prepared, committing, committed, aborted, failed,
    };

    // boot: this board's boot-scoped token (nonzero, fresh each boot).
    GroupQrJoinConversation(Side side, const identity::roster::RosterCrypto& crypto,
                            const identity::roster::RosterSigner& signer,
                            identity::roster::qr::Authority& authority, GroupQrJoinIdentity& identity,
                            security::SecureRandomSource& random, const identity::roster::BootToken& boot)
        : side_(side), crypto_(crypto), signer_(signer), outer_(authority), identity_(identity),
          random_(random), boot_(boot) {}
    GroupQrJoinConversation(const GroupQrJoinConversation&) = delete;
    GroupQrJoinConversation& operator=(const GroupQrJoinConversation&) = delete;
    ~GroupQrJoinConversation() override { sodium_memzero(&ephemeral_, sizeof ephemeral_); }

    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] Side side() const { return side_; }
    [[nodiscard]] const identity::roster::Digest& scope() const { return scope_; }

    // Joiner only: the invite this board verified and claimed, the local
    // timing of that Join, and the authoritative pair epoch.
    bool open(const identity::roster::qr::DescriptorBytes& invite, identity::roster::qr::Timing timing,
              std::uint32_t epoch) {
        namespace qr = identity::roster::qr;
        if (side_ != Side::joiner || phase_ != Phase::idle) return false;
        qr::Descriptor descriptor{};
        qr::Start start{};
        start.epoch = epoch;
        start.joiner = signer_.public_key();
        if (!epoch || !timing.deadline(deadline_) || !qr::verify_descriptor(invite, crypto_, descriptor) ||
            descriptor.admin == start.joiner || !fresh(start.fresh) ||
            !qr::scope_digest(invite, start.joiner, crypto_, start.scope) ||
            !qr::sign_start(start, crypto_, signer_, start_) ||
            !identity_.begin_noise(noise_, Side::joiner, descriptor.admin, ephemeral_, start.scope)) {
            return fail();
        }
        wipe_ephemeral();
        std::array<std::uint8_t, GroupQrNoiseBinding::kMessageOneBytes> message{};
        std::size_t size = 0;
        if (!noise_.write(message.data(), message.size(), size) ||
            !qr_join::encode_noise(1, start.scope, message.data(), size, noise_local_[0])) return fail();
        descriptor_ = invite;
        timing_ = timing;
        scope_ = start.scope;
        remember(start_);
        phase_ = Phase::handshake;
        return true;
    }

    // Any received radio frame. True when it belongs to a QR join (whether or
    // not this board used it), so the caller does not pass it elsewhere.
    bool receive(const std::uint8_t* bytes, std::size_t size) {
        if (!qr_join::is_join_frame(bytes, size)) return false;
        // A failed board, or a joiner that has not opened a join, drops them.
        // After its final receipt a board only answers the peer's repeats of it.
        if (phase_ == Phase::failed || (phase_ == Phase::idle && side_ == Side::joiner)) return true;
        qr_join::NoiseFrame noise{};
        if (qr_join::decode_noise(bytes, size, noise)) {
            on_noise(noise);
            return true;
        }
        RadioFrame frame{};
        std::memcpy(frame.bytes.data(), bytes, size);
        frame.size = size;
        on_qr(frame);
        return true;
    }

    // Advances local work: approvals, the transcript and the receipts.
    void service() {
        if (terminal()) return;
        if (phase_ != Phase::idle && outer_.now() >= deadline_) { fail(); return; }
        if (!session_) return;
        if (peer_aborted_) { finish_abort(); return; }
        if (phase_ == Phase::handshake && noise_.complete()) phase_ = Phase::admission;
        if (phase_ == Phase::admission) {
            if (!local(Kind::admission).size) {
                RadioFrame out{};
                if (session_->admission(out)) remember(out);
            }
            if (local(Kind::admission).size && peer_admission_ && session_->enter_transcript())
                phase_ = Phase::confirmation;
        }
        if (phase_ == Phase::confirmation) {
            if (!local(Kind::confirmed).size) {
                RadioFrame out{};
                if (session_->confirmation(out)) remember(out);
            }
            if (local(Kind::confirmed).size && peer_confirmation_) {
                RadioFrame out{};
                if (session_->receipt(Kind::prepared, out)) {
                    remember(out);
                    phase_ = Phase::prepared;
                }
            }
        }
        if (phase_ == Phase::prepared && peer_prepared_) {
            RadioFrame out{};
            if (session_->receipt(Kind::committed, out)) {
                remember(out);
                phase_ = Phase::committing;
            }
        }
        if (phase_ == Phase::committing && peer_committed_) phase_ = Phase::committed;
    }

    // The next frame to send now, if any. Report the radio's answer with sent().
    bool next(RadioFrame& out) {
        if (phase_ == Phase::idle || phase_ == Phase::failed) return false;
        if (follow_) { pending_ = *follow_; pending_noise_ = true; out = pending_; return true; }
        pending_noise_ = false;
        const auto now = outer_.now();
        // A frame made in the same poll as the next one still goes out first.
        for (const auto kind : {Kind::start, Kind::reply, Kind::admission, Kind::confirmed,
                                Kind::prepared, Kind::committed, Kind::aborted}) {
            if (!local(kind).size || sent_once_[index(kind)] || (latest_ != kind && !behind(kind))) continue;
            if (!copies_.offer(local(kind), timing_, now)) continue;
            pending_ = local(kind);
            out = pending_;
            return true;
        }
        if (echo_ && copies_.offer(local(*echo_), timing_, now)) {
            pending_ = local(*echo_);
            out = pending_;
            return true;
        }
        if (latest_ && copies_.offer(local(*latest_), timing_, now)) {
            pending_ = local(*latest_);
            out = pending_;
            return true;
        }
        return false;
    }

    void sent(bool accepted) {
        if (pending_noise_) {
            follow_.reset();
            pending_noise_ = false;
            return;
        }
        if (!pending_.size) return;
        const auto kind = static_cast<Kind>(pending_.bytes[5]);
        const auto frame = pending_;
        pending_ = {};
        if (!accepted || !copies_.accepted(frame, timing_, outer_.now())) return;
        sent_once_[index(kind)] = true;
        if (echo_ && *echo_ == kind) echo_.reset();
        // Each accepted copy carries this board's Noise message along with it.
        if (kind == Kind::start && noise_local_[0].size) follow_ = noise_local_[0];
        if (kind == Kind::reply) {
            copies_sent_reply_ = true;
            if (noise_local_[1].size) follow_ = noise_local_[1];
        }
        if (kind == Kind::admission && side_ == Side::joiner && noise_local_[2].size) follow_ = noise_local_[2];
    }

    // Local cancel. Sends an aborted receipt when the session still allows one.
    void cancel() {
        if (terminal()) return;
        if (!session_) { fail(); return; }
        finish_abort();
    }

    // The Noise pair keys, once, after both boards committed. The caller keeps
    // them only with the durable committed receipt (step 2d).
    bool pair_keys(Key& transmit, Key& receive) {
        return phase_ == Phase::committed && noise_.split(transmit, receive);
    }

private:
    bool terminal() const {
        return phase_ == Phase::committed || phase_ == Phase::aborted || phase_ == Phase::failed;
    }
    static std::size_t index(Kind kind) { return static_cast<std::size_t>(kind) - 1; }
    // Admission, confirmation and prepared stay owed to the peer after this
    // board moves on in the same poll.
    bool behind(Kind kind) const {
        return kind == Kind::admission || kind == Kind::confirmed || kind == Kind::prepared;
    }
    RadioFrame& local(Kind kind) { return local_[index(kind)]; }
    void remember(const RadioFrame& frame) {
        const auto kind = static_cast<Kind>(frame.bytes[5]);
        local(kind) = frame;
        latest_ = kind;
    }
    bool fail() {
        phase_ = Phase::failed;
        follow_.reset();
        echo_.reset();
        wipe_ephemeral();
        noise_.abort();
        return false;
    }
    void wipe_ephemeral() { sodium_memzero(&ephemeral_, sizeof ephemeral_); }

    bool fresh(identity::roster::qr::Fresh& out) {
        identity::roster::qr::Fresh value{};
        if (!fill(ephemeral_.secret, sizeof ephemeral_.secret) ||
            crypto_scalarmult_curve25519_base(ephemeral_.public_key, ephemeral_.secret) != 0 ||
            !fill(value.challenge.data(), value.challenge.size())) return false;
        std::memcpy(value.ephemeral.data(), ephemeral_.public_key, value.ephemeral.size());
        value.boot = boot_;
        out = value;
        return true;
    }
    bool fill(std::uint8_t* out, std::size_t size) {
        const auto result = random_.fill(out, size);
        return result.ok() && result.bytes_written == size;
    }

    void on_noise(const qr_join::NoiseFrame& frame) {
        if (phase_ == Phase::idle || frame.scope != scope_ || qr_join::noise_sender(frame.stage) == side_) return;
        auto& last = noise_peer_[frame.stage - 1];
        if (last.size) {
            // A repeat: the peer has not seen this board's next Noise message.
            if (last.size == frame.size && std::memcmp(last.bytes.data(), frame.message, frame.size) == 0 &&
                frame.stage == 2 && noise_local_[2].size) follow_ = noise_local_[2];
            return;
        }
        if (side_ == Side::admin && frame.stage == 3 && !noise_local_[1].size) return;
        if (side_ == Side::joiner && !session_) return;  // Noise 2 only after the verified Reply
        if (!noise_.read(frame.message, frame.size)) { fail(); return; }
        std::memcpy(last.bytes.data(), frame.message, frame.size);
        last.size = frame.size;
        if (frame.stage == 3) {
            if (latest_ == Kind::reply) latest_.reset();  // the joiner has the Reply
            return;
        }
        const std::uint8_t reply_stage = frame.stage + 1;
        std::array<std::uint8_t, GroupQrNoiseBinding::kMessageThreeBytes> message{};
        std::size_t size = 0;
        if (!noise_.write(message.data(), message.size(), size) ||
            !qr_join::encode_noise(reply_stage, scope_, message.data(), size, noise_local_[reply_stage - 1])) {
            fail();
            return;
        }
        // Noise 2 follows the Reply when the Reply has already gone out; Noise 3
        // goes at once and again with each admission copy.
        if (reply_stage == 3 || copies_sent_reply_) follow_ = noise_local_[reply_stage - 1];
    }

    void on_qr(const RadioFrame& frame) {
        const auto kind = static_cast<Kind>(frame.bytes[5]);
        if (frame.bytes[6] == static_cast<std::uint8_t>(side_)) return;
        if (phase_ == Phase::committed || phase_ == Phase::aborted) {
            if (kind == Kind::committed || kind == Kind::aborted) {
                const bool repeat = (kind == Kind::committed && peer_committed_) ||
                                    (kind == Kind::aborted && peer_aborted_);
                if (repeat && session_->accept_receipt(frame)) echo(phase_ == Phase::committed ?
                                                                    Kind::committed : Kind::aborted);
            }
            return;
        }
        if (kind == Kind::start) { on_start(frame); return; }
        if (kind == Kind::reply) { on_reply(frame); return; }
        if (!session_) return;
        switch (kind) {
        case Kind::admission:
            if (peer_admission_) { echo(Kind::admission); return; }
            peer_admission_ = session_->accept_admission(frame);
            return;
        case Kind::confirmed:
            if (peer_confirmation_) { echo(Kind::confirmed); return; }
            peer_confirmation_ = session_->accept_confirmation(frame);
            return;
        case Kind::prepared: case Kind::committed: case Kind::aborted: {
            const bool repeat = (kind == Kind::prepared && peer_prepared_) ||
                                (kind == Kind::committed && peer_committed_) ||
                                (kind == Kind::aborted && peer_aborted_);
            if (!session_->accept_receipt(frame)) return;
            if (repeat) { echo(kind); return; }
            if (kind == Kind::prepared) peer_prepared_ = true;
            if (kind == Kind::committed) peer_committed_ = true;
            if (kind == Kind::aborted) peer_aborted_ = true;
            return;
        }
        default:
            return;
        }
    }

    void echo(Kind kind) {
        if (local(kind).size) echo_ = kind;
        if (kind == Kind::admission && side_ == Side::joiner && noise_local_[2].size) follow_ = noise_local_[2];
    }

    void on_start(const RadioFrame& frame) {
        namespace qr = identity::roster::qr;
        if (side_ != Side::admin) return;
        if (phase_ != Phase::idle) {
            if (start_.size == frame.size &&
                std::memcmp(start_.bytes.data(), frame.bytes.data(), frame.size) == 0) echo(Kind::reply);
            return;
        }
        const auto invites = outer_.issued_invites();
        const auto now = outer_.now();
        std::size_t slot = 0;
        qr::Start start{};
        qr::Fresh mine{};
        RadioFrame reply{};
        if (!qr::resolve_start(frame, invites, signer_.public_key(), now, crypto_, slot) ||
            !qr::verify_start(frame, crypto_, start) || invites[slot].issued == nullptr) return;
        const auto timing = invites[slot].timing;
        std::uint64_t deadline = 0;
        if (!timing.deadline(deadline) || !fresh(mine) || !qr::sign_reply(frame, mine, crypto_, signer_, reply)) {
            wipe_ephemeral();
            return;
        }
        auto& session = session_.emplace(Side::admin, timing, crypto_, signer_, static_cast<identity::roster::qr::Authority&>(*this));
        if (!session.begin(*invites[slot].issued, frame, reply)) {
            // A Start this board cannot use leaves it free for the right one.
            session_.reset();
            wipe_ephemeral();
            return;
        }
        if (!identity_.begin_noise(noise_, Side::admin, start.joiner, ephemeral_, start.scope)) {
            fail();
            return;
        }
        wipe_ephemeral();
        descriptor_ = *invites[slot].issued;
        timing_ = timing;
        deadline_ = deadline;
        scope_ = start.scope;
        start_ = frame;
        reply_ = reply;
        remember(reply);
        phase_ = Phase::handshake;
    }

    void on_reply(const RadioFrame& frame) {
        if (side_ != Side::joiner || phase_ == Phase::idle) return;
        if (session_) {
            if (reply_.size == frame.size && std::memcmp(reply_.bytes.data(), frame.bytes.data(), frame.size) == 0 &&
                noise_local_[2].size) follow_ = noise_local_[2];
            return;
        }
        auto& session = session_.emplace(Side::joiner, timing_, crypto_, signer_, static_cast<identity::roster::qr::Authority&>(*this));
        if (!session.begin(descriptor_, start_, frame)) { session_.reset(); return; }
        reply_ = frame;
        latest_.reset();  // the admin has the Start
    }

    void finish_abort() {
        RadioFrame out{};
        if (session_ && session_->receipt(Kind::aborted, out)) {
            remember(out);
            phase_ = Phase::aborted;
            follow_.reset();
            noise_.abort();
            return;
        }
        fail();
    }

    // qr::Authority for the Session: the board's trusted ports, plus this
    // join's own Noise result as the verified handshake.
    std::uint64_t now() const override { return outer_.now(); }
    identity::roster::qr::LocalInvites issued_invites() const override { return outer_.issued_invites(); }
    bool fresh_unclaimed(const identity::roster::qr::Nonce& id) const override { return outer_.fresh_unclaimed(id); }
    bool current(const identity::roster::qr::Descriptor& d, const identity::roster::PublicKey& joiner,
                 std::uint32_t epoch) const override {
        return outer_.current(d, joiner, epoch);
    }
    bool approve(identity::roster::qr::Review review, Side side, const identity::roster::qr::Descriptor& d,
                 const identity::roster::PublicKey& peer, const identity::roster::Digest& digest) const override {
        return outer_.approve(review, side, d, peer, digest);
    }
    bool verified_handshake(const identity::roster::Digest& admitted, const identity::roster::Digest& initiator,
                            const identity::roster::Digest& responder,
                            identity::roster::Digest& out) const override {
        return noise_.verified(admitted, initiator, responder, out);
    }
    bool persist(const identity::roster::qr::Retained& value, std::uint64_t deadline) override {
        return outer_.persist(value, deadline);
    }
    bool readback(const identity::roster::qr::Retained& value) const override { return outer_.readback(value); }
    bool successor_readback(const std::uint8_t* bytes, std::size_t size) const override {
        return outer_.successor_readback(bytes, size);
    }

    const Side side_;
    const identity::roster::RosterCrypto& crypto_;
    const identity::roster::RosterSigner& signer_;
    identity::roster::qr::Authority& outer_;
    GroupQrJoinIdentity& identity_;
    security::SecureRandomSource& random_;
    const identity::roster::BootToken boot_;

    GroupQrNoiseBinding noise_;
    ot_noise_xk_keypair ephemeral_{};
    std::optional<identity::roster::qr::Session> session_;
    identity::roster::qr::Copies copies_;
    identity::roster::qr::Timing timing_{};
    std::uint64_t deadline_{};
    identity::roster::qr::DescriptorBytes descriptor_{};
    identity::roster::Digest scope_{};
    RadioFrame start_{}, reply_{};
    std::array<RadioFrame, 11> local_{};
    std::array<bool, 11> sent_once_{};
    std::array<RadioFrame, 3> noise_local_{};
    std::array<RadioFrame, 3> noise_peer_{};
    std::optional<Kind> latest_, echo_;
    std::optional<RadioFrame> follow_;
    RadioFrame pending_{};
    bool pending_noise_{}, copies_sent_reply_{};
    bool peer_admission_{}, peer_confirmation_{}, peer_prepared_{}, peer_committed_{}, peer_aborted_{};
    Phase phase_{Phase::idle};
};

}  // namespace opentrail::security_evaluation
