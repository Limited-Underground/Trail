#pragma once
// First board pairing over LoRa: message order, early arrival, duplicates and
// bounded resends (V1 F5, step R2a; plan: docs/security/F5-R2-BOARD-PAIRING-PLAN.md).
//
// This owner only sequences frames. It never decides trust: every payload is
// handed to the Adapter (the enrollment session), which authenticates it, and
// the person confirms fingerprints and the transcript on each board. A refused
// payload ends the attempt.
//
// Sends: service() only offers a frame; the caller reports sent(accepted). Only
// frames the radio accepted count against max_sends (D-087), and every
// produced frame must be accepted before a local step (including commit) runs.
//
// Loss recovery, within Limits:
// - A board repeats each frame it sent, every resend_after_ms, until a later
//   frame from the peer proves the peer received it (in the peer's script, the
//   frame was sent after the receive of ours). Hearing the peer's copy of a
//   frame exchanged at the same time is not proof: on half-duplex radios our
//   copy may have collided with it (review of I-057).
// - A repeated frame from the peer means the peer missed something. Both scripts
//   are fixed, so the repeat identifies exactly which of our frames the peer is
//   waiting for; that frame is sent again.
//
// Adapter contract (one serialized app task):
//   bool produce(FrameType, std::uint8_t* out)          // exact payload size
//   bool consume(FrameType, const std::uint8_t* in)     // exact payload size
//   LocalResult local(LocalStep, std::uint64_t now_ms)  // review, confirm, commit
//   bool remaining_ms(std::uint64_t now_ms, std::uint64_t& out)
//       // time left before the session itself refuses (its request and signed
//       // invitation deadlines, on its own clock); false if it cannot tell

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/pairing_frame.hpp"

namespace opentrail::protocol::pairing {

enum class LocalStep : std::uint8_t { review, confirm, commit };
enum class LocalResult : std::uint8_t { pending, done, failed };

enum class StepKind : std::uint8_t { send, receive, local };
struct Step {
    StepKind kind;
    FrameType frame;   // send/receive
    LocalStep local;   // local
};

namespace detail {
constexpr Step S(FrameType t) { return {StepKind::send, t, LocalStep::review}; }
constexpr Step R(FrameType t) { return {StepKind::receive, t, LocalStep::review}; }
constexpr Step L(LocalStep s) { return {StepKind::local, FrameType::candidate, s}; }
using F = FrameType;
using LS = LocalStep;
}  // namespace detail

inline constexpr std::size_t kScriptSteps = 22;
using Script = std::array<Step, kScriptSteps>;

// Order taken from the two-endpoint session tests. A starts ("Start pairing").
inline constexpr Script kInitiatorScript{
    detail::S(detail::F::candidate), detail::R(detail::F::candidate),
    detail::L(detail::LS::review),
    detail::S(detail::F::offer), detail::R(detail::F::offer),
    detail::S(detail::F::possession), detail::R(detail::F::possession),
    detail::S(detail::F::clock_mark), detail::R(detail::F::clock_mark),
    detail::S(detail::F::invitation_payload), detail::S(detail::F::invitation_signature),
    detail::S(detail::F::invitation_cosign), detail::R(detail::F::invitation_cosign),
    detail::S(detail::F::handshake), detail::R(detail::F::handshake),
    detail::S(detail::F::handshake),
    detail::L(detail::LS::confirm),
    detail::S(detail::F::control), detail::R(detail::F::control),
    detail::S(detail::F::control), detail::R(detail::F::control),
    detail::L(detail::LS::commit),
};
// B joins ("Join pairing").
inline constexpr Script kResponderScript{
    detail::S(detail::F::candidate), detail::R(detail::F::candidate),
    detail::L(detail::LS::review),
    detail::S(detail::F::offer), detail::R(detail::F::offer),
    detail::S(detail::F::possession), detail::R(detail::F::possession),
    detail::S(detail::F::clock_mark), detail::R(detail::F::clock_mark),
    detail::R(detail::F::invitation_payload), detail::R(detail::F::invitation_signature),
    detail::S(detail::F::invitation_cosign), detail::R(detail::F::invitation_cosign),
    detail::R(detail::F::handshake), detail::S(detail::F::handshake),
    detail::R(detail::F::handshake),
    detail::L(detail::LS::confirm),
    detail::R(detail::F::control), detail::S(detail::F::control),
    detail::R(detail::F::control), detail::S(detail::F::control),
    detail::L(detail::LS::commit),
};

// The scripts must mirror each other: every frame one board sends, in order
// per type, is a frame the other board receives.
constexpr bool scripts_mirror(const Script& a, const Script& b) {
    for (std::uint8_t t = 1; t <= 10; ++t) {
        int a_send = 0, b_recv = 0, b_send = 0, a_recv = 0;
        for (const auto& s : a) {
            if (static_cast<std::uint8_t>(s.frame) != t || s.kind == StepKind::local) continue;
            (s.kind == StepKind::send ? a_send : a_recv) += 1;
        }
        for (const auto& s : b) {
            if (static_cast<std::uint8_t>(s.frame) != t || s.kind == StepKind::local) continue;
            (s.kind == StepKind::send ? b_send : b_recv) += 1;
        }
        if (a_send != b_recv || b_send != a_recv) return false;
    }
    return true;
}
static_assert(scripts_mirror(kInitiatorScript, kResponderScript),
              "pairing scripts must mirror each other");

struct Limits {
    std::uint64_t resend_after_ms{3000};
    // Half-duplex radios (review of I-057): after a shared step both boards are
    // ready to send in the same instant. The responder holds each new frame this
    // long (more than one frame's airtime at the bench profile, SF7: up to 0.31 s for the 194-byte invitation)
    // and resends on a shorter interval, so neither the first copies nor the
    // resends collide.
    std::uint64_t responder_turn_delay_ms{600};
    std::uint64_t responder_resend_after_ms{2300};
    std::uint8_t max_sends{4};          // per frame (D-107; was 2 under D-087)
    std::uint64_t attempt_limit_ms{300000};  // D-110 (was 120 s, D-087)
    // D-106 (I-057): the first frame, the candidate "hello", repeats on a slow
    // interval for the whole window, so a board started while the other phone's
    // person is still typing the code still receives it. The two roles use
    // different intervals so half-duplex boards never stay in lockstep and
    // collide on every repeat (review of I-057). 30 sends cover 120 s at 4.3 s.
    std::uint64_t hello_resend_after_ms{5000};            // initiator
    std::uint64_t hello_responder_resend_after_ms{4300};  // responder
    std::uint8_t hello_max_sends{70};  // 70 x 4.3 s covers the 300 s window (D-110)
    // The final exchange is A control 1, B control 1, A control 2, B control 2;
    // B commits once its control 2 is accepted, A once it receives it. A board
    // produces its first control only with final_reserve_ms left, and each later
    // control only with final_step_reserve_ms left, measured to the earlier of
    // its own window and the session's deadlines. So a board that sends a control
    // can still receive the answer it needs, and a board that could not finish
    // fails before the peer can commit (reviews of I-057: late confirmations let
    // the responder commit after the initiator had expired).
    std::uint64_t final_reserve_ms{15000};
    std::uint64_t final_step_reserve_ms{10000};
    // While running, a control frame goes to the radio only with time left for
    // its answer: its first copy with final_first_send_floor_ms (room for that
    // copy and one resend, resend_after_ms later), a resend with
    // final_send_floor_ms (reviews of I-057; a lossless round trip is about 1.4 s). The radio may refuse offers for a
    // while (a transmit hold), so the first copy can go out long after the frame
    // was made.
    std::uint64_t final_first_send_floor_ms{6500};
    // 3 s, not less: a later resend can collide with the peer's held final reply
    // and spend the last send (sixth review of I-057).
    std::uint64_t final_send_floor_ms{3000};
    // Neither board transmits until this long after it last heard a frame from
    // the peer, so it never answers into the rest of the peer's burst (a simple
    // stand-in for listen-before-talk; review of I-057). 0 disables.
    std::uint64_t quiet_after_rx_ms{500};
};

enum class Outcome : std::uint8_t { running, paired, failed };
enum class FailReason : std::uint8_t {
    none, adapter_refused, local_failed, expired,
    frame_store_full, bad_state
};

template <class Adapter>
class PairingCoordinator final {
public:
    static constexpr std::size_t kSlots = 16;

    PairingCoordinator(Adapter& adapter, bool initiator, std::uint32_t code,
                       Limits limits = {})
        : adapter_(adapter), initiator_(initiator),
          own_(initiator ? kInitiatorScript : kResponderScript),
          peer_(initiator ? kResponderScript : kInitiatorScript),
          code_(code), limits_(limits) {}
    PairingCoordinator(const PairingCoordinator&) = delete;
    PairingCoordinator& operator=(const PairingCoordinator&) = delete;

    // The owner has already started the session for this request.
    bool begin(std::uint64_t now_ms) {
        if (begun_ || !code_valid(code_) || limits_.max_sends == 0 || limits_.hello_max_sends == 0) {
            return fail(FailReason::bad_state);
        }
        begun_ = true;
        start_ms_ = now_ms;
        return true;
    }

    // Received radio bytes. Other traffic and other attempts are ignored.
    void on_receive(const std::uint8_t* data, std::size_t size) {
        if (outcome_ == Outcome::failed || !begun_) return;
        Frame f{};
        if (decode(data, size, code_, f) != DecodeResult::ok) return;
        heard_ = true;  // stamped with the time at the next service()
        for (std::size_t i = 0; i < consumed_count_; ++i) {
            if (same(consumed_[i], f)) {  // peer repeats: it missed our reply
                answer_repeat(f.type, consumed_[i].instance);
                return;
            }
        }
        if (outcome_ != Outcome::running) return;
        for (std::size_t i = 0; i < inbox_count_; ++i) {
            if (same(inbox_[i], f)) return;  // duplicate still waiting
        }
        if (inbox_count_ == kSlots) { (void)fail(FailReason::frame_store_full); return; }
        store(inbox_[inbox_count_++], f, 0);
    }

    // Advance the script and return a frame to transmit now, if any.
    bool service(std::uint64_t now_ms, FrameBuffer& out, std::size_t& out_size) {
        out_size = 0;
        if (outcome_ == Outcome::failed || !begun_) return false;
        if (outcome_ == Outcome::running &&
            (now_ms < start_ms_ || now_ms - start_ms_ >= limits_.attempt_limit_ms)) {
            return fail(FailReason::expired);
        }
        now_ms_ = now_ms;
        if (outcome_ == Outcome::running && !advance(now_ms)) return false;
        if (heard_) { heard_ = false; heard_ms_ = now_ms; heard_any_ = true; }
        if (heard_any_ && now_ms - heard_ms_ < limits_.quiet_after_rx_ms) return false;  // wait for quiet
        return transmit(now_ms, out, out_size);
    }

    // Reports what the radio did with the frame the last service() offered.
    // Only an accepted frame counts against max_sends (D-087); a refused or
    // held frame is offered again on a later service().
    void sent(bool accepted, std::uint64_t now_ms) {
        if (offered_ == 0) return;
        auto& o = sent_[offered_ - 1];
        offered_ = 0;
        if (!accepted) return;
        ++o.sends;
        o.last_ms = now_ms;
        o.wait_ms = interval(o);
        o.due = false;
    }

    Outcome outcome() const { return outcome_; }
    FailReason reason() const { return reason_; }
    std::size_t step() const { return step_; }
    static constexpr std::size_t steps() { return kScriptSteps; }

private:
    struct Stored {
        FrameType type{};
        std::uint8_t instance{};  // 1-based count of this type from the peer
        std::uint8_t size{};
        std::array<std::uint8_t, kMaxPayloadBytes> payload{};
    };
    struct Outgoing {
        FrameType type{};
        std::uint8_t instance{};  // 1-based count of this type we sent
        FrameBuffer bytes{};
        std::size_t size{};
        std::uint8_t sends{};
        std::uint64_t last_ms{};
        std::uint64_t created_ms{};
        bool due{};
        bool received{};
        std::uint64_t wait_ms{};  // until the next timed resend          // the peer's later frames prove it has this one               // resend requested by a peer repeat
    };

    // D-106: the candidate hello uses its own limits; every other frame keeps D-087.
    std::uint8_t cap(const Outgoing& o) const {
        return o.type == FrameType::candidate ? limits_.hello_max_sends : limits_.max_sends;
    }
    std::uint64_t interval(const Outgoing& o) const {
        if (o.type != FrameType::candidate) {
            return initiator_ ? limits_.resend_after_ms : limits_.responder_resend_after_ms;
        }
        return initiator_ ? limits_.hello_resend_after_ms : limits_.hello_responder_resend_after_ms;
    }

    static bool same(const Stored& s, const Frame& f) {
        if (s.type != f.type || s.size != f.size) return false;
        for (std::size_t i = 0; i < f.size; ++i) {
            if (s.payload[i] != f.payload[i]) return false;
        }
        return true;
    }
    static void store(Stored& s, const Frame& f, std::uint8_t instance) {
        s.type = f.type;
        s.instance = instance;
        s.size = static_cast<std::uint8_t>(f.size);
        for (std::size_t i = 0; i < f.size; ++i) s.payload[i] = f.payload[i];
    }

    // The peer repeated its `instance`-th frame of `type`. In its script, find
    // the receive it is blocked on after that send, and mark our frame due.
    void answer_repeat(FrameType type, std::uint8_t instance) {
        int seen = 0;
        std::size_t at = kScriptSteps;
        for (std::size_t i = 0; i < kScriptSteps; ++i) {
            if (peer_[i].kind == StepKind::send && peer_[i].frame == type &&
                ++seen == instance) { at = i; break; }
        }
        if (at == kScriptSteps) return;
        for (std::size_t i = at + 1; i < kScriptSteps; ++i) {
            if (peer_[i].kind != StepKind::receive) continue;
            const auto wanted = peer_[i].frame;
            std::uint8_t index = 0;
            for (std::size_t j = 0; j <= i; ++j) {
                if (peer_[j].kind == StepKind::receive && peer_[j].frame == wanted) ++index;
            }
            for (std::size_t k = 0; k < sent_count_; ++k) {
                // Hellos repeat on their own timer until proven received, and a
                // frame the peer has proven it received needs no answer: both
                // would only echo repeats back and forth (review of I-057).
                if (sent_[k].type == FrameType::candidate || sent_[k].received) continue;
                if (sent_[k].type == wanted && sent_[k].instance == index) {
                    sent_[k].due = true;
                }
            }
            return;
        }
    }

    // Index in `script` of the instance-th step of `kind` for `type`, or kScriptSteps.
    static std::size_t position(const Script& script, StepKind kind, FrameType type,
                                std::uint8_t instance) {
        std::uint8_t seen = 0;
        for (std::size_t i = 0; i < kScriptSteps; ++i) {
            if (script[i].kind == kind && script[i].frame == type && ++seen == instance) return i;
        }
        return kScriptSteps;
    }

    // We consumed the peer's instance-th frame of `type`. Every frame of ours the
    // peer's script receives before sending that one has arrived.
    void mark_received(FrameType type, std::uint8_t instance) {
        const auto sent_at = position(peer_, StepKind::send, type, instance);
        for (std::size_t k = 0; k < sent_count_; ++k) {
            auto& o = sent_[k];
            if (position(peer_, StepKind::receive, o.type, o.instance) < sent_at) {
                o.received = true;
                o.due = false;  // an answer queued before the proof is no longer needed
            }
        }
    }

    // Time left before this board or its session refuses.
    bool remaining(std::uint64_t now_ms, std::uint64_t& out) {
        const auto used = now_ms - start_ms_;
        out = used >= limits_.attempt_limit_ms ? 0 : limits_.attempt_limit_ms - used;
        std::uint64_t session = 0;
        if (!adapter_.remaining_ms(now_ms, session)) return false;
        if (session < out) out = session;
        return true;
    }

    bool sent_any(FrameType type) const {
        for (std::size_t k = 0; k < sent_count_; ++k) {
            if (sent_[k].type == type) return true;
        }
        return false;
    }

    bool unsent_pending() const {
        for (std::size_t k = 0; k < sent_count_; ++k) {
            if (sent_[k].sends == 0) return true;
        }
        return false;
    }

    bool advance(std::uint64_t now_ms) {
        while (step_ < kScriptSteps) {
            const Step& st = own_[step_];
            if (st.kind == StepKind::send) {
                if (sent_count_ == kSlots) return fail(FailReason::frame_store_full);
                if (st.frame == FrameType::control) {
                    std::uint64_t left = 0;
                    if (!remaining(now_ms, left)) return fail(FailReason::local_failed);
                    const auto need = sent_any(FrameType::control) ? limits_.final_step_reserve_ms
                                                                   : limits_.final_reserve_ms;
                    if (left < need) return fail(FailReason::expired);  // too late for both boards
                }
                std::array<std::uint8_t, kMaxPayloadBytes> payload{};
                if (!adapter_.produce(st.frame, payload.data())) {
                    return fail(FailReason::adapter_refused);
                }
                auto& o = sent_[sent_count_];
                o = {};
                o.type = st.frame;
                o.created_ms = now_ms;
                for (std::size_t k = 0; k < sent_count_; ++k) {
                    if (sent_[k].type == st.frame) ++o.instance;
                }
                ++o.instance;
                if (!encode(st.frame, code_, payload.data(), payload_bytes(st.frame),
                            o.bytes, o.size)) {
                    return fail(FailReason::bad_state);
                }
                ++sent_count_;
                ++step_;
                continue;
            }
            if (st.kind == StepKind::receive) {
                // Our pending frames go out before we act on the peer's.
                if (unsent_pending()) return true;
                std::size_t found = inbox_count_;
                for (std::size_t i = 0; i < inbox_count_; ++i) {
                    if (inbox_[i].type == st.frame) { found = i; break; }
                }
                if (found == inbox_count_) return true;  // wait
                Stored got = inbox_[found];
                for (std::size_t i = found + 1; i < inbox_count_; ++i) inbox_[i - 1] = inbox_[i];
                --inbox_count_;
                if (!adapter_.consume(st.frame, got.payload.data())) {
                    return fail(FailReason::adapter_refused);
                }
                if (consumed_count_ == kSlots) return fail(FailReason::frame_store_full);
                got.instance = 1;
                for (std::size_t k = 0; k < consumed_count_; ++k) {
                    if (consumed_[k].type == got.type) ++got.instance;
                }
                consumed_[consumed_count_++] = got;
                mark_received(got.type, got.instance);
                ++step_;
                continue;
            }
            // Every produced frame is accepted by the radio at least once before
            // a local step; in particular the final control goes out before commit.
            if (unsent_pending()) return true;
            const auto r = adapter_.local(st.local, now_ms);
            if (r == LocalResult::pending) return true;
            if (r == LocalResult::failed) return fail(FailReason::local_failed);
            ++step_;
        }
        outcome_ = Outcome::paired;
        return true;
    }

    bool transmit(std::uint64_t now_ms, FrameBuffer& out, std::size_t& out_size) {
        // 1. First sends, in order.
        for (std::size_t k = 0; k < sent_count_; ++k) {
            if (sent_[k].sends != 0) continue;
            if (!initiator_ && now_ms - sent_[k].created_ms < limits_.responder_turn_delay_ms) {
                return false;  // the responder's turn has not come yet
            }
            return offer(k, out, out_size);
        }
        // 2. Frames the peer asked for by repeating.
        for (std::size_t k = 0; k < sent_count_; ++k) {
            auto& o = sent_[k];
            if (!o.due) continue;
            if (o.sends < cap(o)) return offer(k, out, out_size);
            o.due = false;
        }
        if (outcome_ != Outcome::running) return false;
        // 3. Repeat every frame the peer has not proved it received, on the resend
        //    interval. Once every send is used, keep listening: the peer's person may still be
        //    reviewing. The request window (attempt_limit_ms) ends the attempt.
        for (std::size_t k = 0; k < sent_count_; ++k) {
            auto& o = sent_[k];
            if (o.received || o.sends >= cap(o)) continue;
            if (now_ms - o.last_ms >= o.wait_ms) {
                return offer(k, out, out_size);
            }
        }
        return false;
    }

    // Offers a frame to the caller. It counts as a send only when the caller
    // reports that the radio accepted it (sent(true, ...)); D-087.
    bool offer(std::size_t k, FrameBuffer& out, std::size_t& out_size) {
        if (outcome_ == Outcome::running && sent_[k].type == FrameType::control) {
            std::uint64_t left = 0;
            const auto floor = sent_[k].sends == 0 ? limits_.final_first_send_floor_ms
                                                   : limits_.final_send_floor_ms;
            if (!remaining(now_ms_, left) || left < floor) return false;
        }
        out = sent_[k].bytes;
        out_size = sent_[k].size;
        offered_ = k + 1;
        return true;
    }

    bool fail(FailReason reason) {
        // A paired result is final; later problems only stop the answers.
        if (outcome_ == Outcome::running) { outcome_ = Outcome::failed; reason_ = reason; }
        return false;
    }

    Adapter& adapter_;
    bool initiator_;
    const Script& own_;
    const Script& peer_;
    std::uint32_t code_;
    Limits limits_;
    std::size_t step_{0};
    std::array<Stored, kSlots> inbox_{};
    std::size_t inbox_count_{0};
    std::array<Stored, kSlots> consumed_{};
    std::size_t consumed_count_{0};
    std::array<Outgoing, kSlots> sent_{};
    std::size_t sent_count_{0};
    std::size_t offered_{0};  // 1-based index of the frame last offered
    std::uint64_t start_ms_{0};
    bool heard_{false}, heard_any_{false};
    std::uint64_t now_ms_{0};  // of the current service()
    std::uint64_t heard_ms_{0};
    bool begun_{false};
    Outcome outcome_{Outcome::running};
    FailReason reason_{FailReason::none};
};

}  // namespace opentrail::protocol::pairing
