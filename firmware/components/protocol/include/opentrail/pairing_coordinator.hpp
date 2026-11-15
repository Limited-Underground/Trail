#pragma once
// First board pairing over LoRa: message order, early arrival, duplicates and
// bounded resends (V1 F5, step R2a; plan: docs/security/F5-R2-BOARD-PAIRING-PLAN.md).
//
// This owner only sequences frames. It never decides trust: every payload is
// handed to the Adapter (the enrollment session), which authenticates it, and
// the person confirms fingerprints and the transcript on each board. A refused
// payload ends the attempt.
//
// Loss recovery, within Limits:
// - While waiting to receive, a board repeats the frames it sent since its last
//   receive, every resend_after_ms.
// - A repeated frame from the peer means the peer missed something. Both scripts
//   are fixed, so the repeat identifies exactly which of our frames the peer is
//   waiting for; that frame is sent again.
//
// Adapter contract (one serialized app task):
//   bool produce(FrameType, std::uint8_t* out)          // exact payload size
//   bool consume(FrameType, const std::uint8_t* in)     // exact payload size
//   LocalResult local(LocalStep, std::uint64_t now_ms)  // review, confirm, commit

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
    std::uint8_t max_sends{2};          // per frame (D-078 test limits)
    std::uint64_t attempt_limit_ms{120000};
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
        : adapter_(adapter),
          own_(initiator ? kInitiatorScript : kResponderScript),
          peer_(initiator ? kResponderScript : kInitiatorScript),
          code_(code), limits_(limits) {}
    PairingCoordinator(const PairingCoordinator&) = delete;
    PairingCoordinator& operator=(const PairingCoordinator&) = delete;

    // The owner has already started the session for this request.
    bool begin(std::uint64_t now_ms) {
        if (begun_ || !code_valid(code_) || limits_.max_sends == 0) {
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
        if (outcome_ == Outcome::running && !advance(now_ms)) return false;
        return transmit(now_ms, out, out_size);
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
        bool due{};               // resend requested by a peer repeat
    };

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
                if (sent_[k].type == wanted && sent_[k].instance == index) {
                    sent_[k].due = true;
                }
            }
            return;
        }
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
                std::array<std::uint8_t, kMaxPayloadBytes> payload{};
                if (!adapter_.produce(st.frame, payload.data())) {
                    return fail(FailReason::adapter_refused);
                }
                auto& o = sent_[sent_count_];
                o = {};
                o.type = st.frame;
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
                batch_start_ = sent_count_;
                ++step_;
                continue;
            }
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
            if (sent_[k].sends == 0) return emit(sent_[k], now_ms, out, out_size);
        }
        // 2. Frames the peer asked for by repeating.
        for (std::size_t k = 0; k < sent_count_; ++k) {
            auto& o = sent_[k];
            if (!o.due) continue;
            o.due = false;
            if (o.sends < limits_.max_sends) return emit(o, now_ms, out, out_size);
        }
        if (outcome_ != Outcome::running) return false;
        const bool waiting = step_ < kScriptSteps && own_[step_].kind == StepKind::receive;
        if (!waiting) return false;
        // 3. While waiting, repeat this batch on the resend interval. Once
        //    every send is used, keep listening: the peer's person may still be
        //    reviewing. The request window (attempt_limit_ms) ends the attempt.
        for (std::size_t k = batch_start_; k < sent_count_; ++k) {
            auto& o = sent_[k];
            if (o.sends >= limits_.max_sends) continue;
            if (now_ms - o.last_ms >= limits_.resend_after_ms) {
                return emit(o, now_ms, out, out_size);
            }
        }
        return false;
    }

    static bool emit(Outgoing& o, std::uint64_t now_ms, FrameBuffer& out, std::size_t& out_size) {
        out = o.bytes;
        out_size = o.size;
        ++o.sends;
        o.last_ms = now_ms;
        return true;
    }

    bool fail(FailReason reason) {
        // A paired result is final; later problems only stop the answers.
        if (outcome_ == Outcome::running) { outcome_ = Outcome::failed; reason_ = reason; }
        return false;
    }

    Adapter& adapter_;
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
    std::size_t batch_start_{0};
    std::uint64_t start_ms_{0};
    bool begun_{false};
    Outcome outcome_{Outcome::running};
    FailReason reason_{FailReason::none};
};

}  // namespace opentrail::protocol::pairing
