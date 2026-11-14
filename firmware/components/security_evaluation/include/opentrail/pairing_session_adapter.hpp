#pragma once
// Connects the LoRa pairing coordinator (protocol/pairing_coordinator.hpp) to
// the enrollment session (V1 F5, step R2a; plan:
// docs/security/F5-R2-BOARD-PAIRING-PLAN.md).
//
// It converts session values to and from the exact frame payloads, with
// explicit little-endian fields (never object layout), and drives the local
// steps through the session's review observer:
//   review  - own key page, press; peer key page, press; then the offer.
//   confirm - transcript confirmation page, press.
//   commit  - durable commit, then ready.
// Every received payload is untrusted; the session authenticates it. This
// adapter adds no trust decision and holds no private key.

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/pairing_coordinator.hpp"

namespace opentrail::security_evaluation {

namespace pairing_codec {
using protocol::pairing::FrameType;
using protocol::pairing::payload_bytes;

class Writer {
public:
    explicit Writer(std::uint8_t* out) : out_(out) {}
    template <class T> void number(T v) {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            out_[at_++] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> (8 * i));
        }
    }
    template <std::size_t N> void bytes(const std::array<std::uint8_t, N>& v) {
        for (auto b : v) out_[at_++] = b;
    }
    std::size_t size() const { return at_; }
private:
    std::uint8_t* out_;
    std::size_t at_{0};
};
class Reader {
public:
    explicit Reader(const std::uint8_t* in) : in_(in) {}
    template <class T> void number(T& v) {
        std::uint64_t x = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) x |= static_cast<std::uint64_t>(in_[at_++]) << (8 * i);
        v = static_cast<T>(x);
    }
    template <std::size_t N> void bytes(std::array<std::uint8_t, N>& v) {
        for (auto& b : v) b = in_[at_++];
    }
    std::size_t size() const { return at_; }
private:
    const std::uint8_t* in_;
    std::size_t at_{0};
};

inline std::size_t put(std::uint8_t* out, const EvaluationEnrollmentCandidate& v) {
    Writer w(out); w.number(v.version); w.number(v.profile); w.bytes(v.public_identity); return w.size();
}
inline std::size_t get(const std::uint8_t* in, EvaluationEnrollmentCandidate& v) {
    Reader r(in); r.number(v.version); r.number(v.profile); r.bytes(v.public_identity); return r.size();
}
inline void put_context(Writer& w, const EnrollmentPossessionContext& c) {
    w.bytes(c.boot); w.number(c.generation); w.number(c.request);
}
inline void get_context(Reader& r, EnrollmentPossessionContext& c) {
    r.bytes(c.boot); r.number(c.generation); r.number(c.request);
}
inline std::size_t put(std::uint8_t* out, const EvaluationEnrollmentOffer& v) {
    Writer w(out);
    w.number(v.version); w.number(static_cast<std::uint8_t>(v.role)); w.number(v.group);
    w.bytes(v.session_identity); put_context(w, v.context); w.bytes(v.challenge);
    return w.size();
}
inline std::size_t get(const std::uint8_t* in, EvaluationEnrollmentOffer& v) {
    Reader r(in); std::uint8_t role = 0;
    r.number(v.version); r.number(role); v.role = static_cast<InvitationRole>(role); r.number(v.group);
    r.bytes(v.session_identity); get_context(r, v.context); r.bytes(v.challenge);
    return r.size();
}
inline std::size_t put(std::uint8_t* out, const EvaluationEnrollmentClockMark& v) {
    Writer w(out); w.number(v.version); put_context(w, v.context); w.number(v.now_ms); return w.size();
}
inline std::size_t get(const std::uint8_t* in, EvaluationEnrollmentClockMark& v) {
    Reader r(in); r.number(v.version); get_context(r, v.context); r.number(v.now_ms); return r.size();
}
inline std::size_t put(std::uint8_t* out, const HandshakeFrame& v) {
    Writer w(out); w.number(v.version); w.number(v.step); w.number(v.payload_bytes);
    for (auto b : v.payload) w.number(static_cast<std::uint8_t>(b));
    return w.size();
}
inline std::size_t get(const std::uint8_t* in, HandshakeFrame& v) {
    Reader r(in); r.number(v.version); r.number(v.step); r.number(v.payload_bytes);
    for (auto& b : v.payload) { std::uint8_t x = 0; r.number(x); b = x; }
    return r.size();
}
inline std::size_t put(std::uint8_t* out, const EvaluationRecord& v) {
    Writer w(out); w.number(v.group); w.number(v.counter); w.number(v.epoch);
    w.bytes(v.sender); w.bytes(v.recipient);
    for (auto b : v.ciphertext) w.number(static_cast<std::uint8_t>(b));
    return w.size();
}
inline std::size_t get(const std::uint8_t* in, EvaluationRecord& v) {
    Reader r(in); r.number(v.group); r.number(v.counter); r.number(v.epoch);
    r.bytes(v.sender); r.bytes(v.recipient);
    for (auto& b : v.ciphertext) { std::uint8_t x = 0; r.number(x); b = x; }
    return r.size();
}
}  // namespace pairing_codec

class PairingSessionAdapter final {
public:
    using FrameType = protocol::pairing::FrameType;
    using LocalStep = protocol::pairing::LocalStep;
    using LocalResult = protocol::pairing::LocalResult;

    enum class Prompt : std::uint8_t { none, own_key, peer_key, transcript };

    explicit PairingSessionAdapter(EnrollmentCandidateSession& session) : session_(session) {}
    PairingSessionAdapter(const PairingSessionAdapter&) = delete;
    PairingSessionAdapter& operator=(const PairingSessionAdapter&) = delete;

    bool produce(FrameType t, std::uint8_t* out) {
        using namespace pairing_codec;
        switch (t) {
            case FrameType::candidate: {
                EvaluationEnrollmentCandidate v{};
                return session_.export_candidate(v) && fits(t, put(out, v));
            }
            case FrameType::offer:
                return have_local_offer_ && fits(t, put(out, local_offer_));
            case FrameType::possession: {
                std::array<std::uint8_t, 64> sig{};
                if (!have_peer_offer_ || !session_.prepare_possession(peer_offer_, sig)) return false;
                copy(sig, out);
                return true;
            }
            case FrameType::clock_mark: {
                EvaluationEnrollmentClockMark v{};
                return session_.clock_mark(v) && fits(t, put(out, v));
            }
            case FrameType::invitation_payload:
                if (!session_.issue_invitation(invitation_)) return false;
                have_payload_ = have_signature_ = true;
                copy(invitation_.payload, out);
                return true;
            case FrameType::invitation_signature:
                if (!have_signature_) return false;
                copy(invitation_.signature, out);
                return true;
            case FrameType::invitation_cosign: {
                std::array<std::uint8_t, 64> sig{};
                if (!have_payload_ || !have_signature_ || !session_.sign_invitation(invitation_, sig)) return false;
                copy(sig, out);
                return true;
            }
            case FrameType::handshake: {
                HandshakeFrame v{};
                return session_.next_handshake(v) && fits(t, put(out, v));
            }
            case FrameType::control: {
                EvaluationRecord v{};
                return session_.next_control(v) && fits(t, put(out, v));
            }
            case FrameType::status:
                return false;  // not used during pairing
        }
        return false;
    }

    bool consume(FrameType t, const std::uint8_t* in) {
        using namespace pairing_codec;
        switch (t) {
            case FrameType::candidate: {
                EvaluationEnrollmentCandidate v{};
                return fits(t, get(in, v)) && session_.receive_candidate(v);
            }
            case FrameType::offer:
                if (have_peer_offer_) return false;
                have_peer_offer_ = fits(t, get(in, peer_offer_));
                return have_peer_offer_;
            case FrameType::possession: {
                std::array<std::uint8_t, 64> sig{};
                copy(in, sig);
                return session_.accept_possession(sig);
            }
            case FrameType::clock_mark: {
                EvaluationEnrollmentClockMark v{};
                return fits(t, get(in, v)) && session_.receive_clock_mark(v);
            }
            case FrameType::invitation_payload:
                if (have_payload_) return false;
                copy(in, invitation_.payload);
                have_payload_ = true;
                return true;
            case FrameType::invitation_signature:
                if (have_signature_) return false;
                copy(in, invitation_.signature);
                have_signature_ = true;
                return true;
            case FrameType::invitation_cosign: {
                std::array<std::uint8_t, 64> sig{};
                copy(in, sig);
                return session_.begin_handshake(sig);
            }
            case FrameType::handshake: {
                HandshakeFrame v{};
                return fits(t, get(in, v)) && session_.receive_handshake(v);
            }
            case FrameType::control: {
                EvaluationRecord v{};
                return fits(t, get(in, v)) && session_.receive_control(v);
            }
            case FrameType::status:
                return false;
        }
        return false;
    }

    LocalResult local(LocalStep step, std::uint64_t /*now_ms*/) {
        if (step == LocalStep::review) return review();
        if (step == LocalStep::confirm) return confirm();
        return session_.commit() && session_.ready() ? LocalResult::done : LocalResult::failed;
    }

    // What the person is asked to look at and press, for the status line.
    Prompt prompt() const { return prompt_; }

private:
    template <std::size_t N> static void copy(const std::array<std::uint8_t, N>& a, std::uint8_t* out) {
        for (std::size_t i = 0; i < N; ++i) out[i] = a[i];
    }
    template <std::size_t N> static void copy(const std::uint8_t* in, std::array<std::uint8_t, N>& a) {
        for (std::size_t i = 0; i < N; ++i) a[i] = in[i];
    }
    static bool fits(FrameType t, std::size_t n) { return n == protocol::pairing::payload_bytes(t); }

    // Review tokens are local observer handles, never sent. Distinct, nonzero.
    static EnrollmentReviewToken token(std::uint8_t which) {
        EnrollmentReviewToken t{};
        for (std::size_t i = 0; i < t.size(); ++i) t[i] = static_cast<std::uint8_t>(0xA0 + which + i);
        return t;
    }

    LocalResult observe(EnrollmentReviewPurpose purpose, std::uint8_t which, Prompt shown,
                        bool& started, bool& confirmed) {
        EnrollmentReviewState state{};
        if (!started) {
            if (!session_.review_start(purpose, token(which), state)) return LocalResult::failed;
            started = true;
            prompt_ = shown;
            return LocalResult::pending;
        }
        if (!session_.review_poll(token(which), state)) return LocalResult::failed;
        confirmed = state.confirmed;
        return confirmed ? LocalResult::done : LocalResult::pending;
    }

    LocalResult review() {
        if (!own_confirmed_) {
            const auto r = observe(EnrollmentReviewPurpose::local, 1, Prompt::own_key, own_started_, own_confirmed_);
            return r == LocalResult::done ? LocalResult::pending : r;
        }
        if (!peer_confirmed_) {
            const auto r = observe(EnrollmentReviewPurpose::peer, 2, Prompt::peer_key, peer_started_, peer_confirmed_);
            if (r != LocalResult::done) return r;
        }
        prompt_ = Prompt::none;
        if (!session_.finish_review(local_offer_)) return LocalResult::failed;
        have_local_offer_ = true;
        return LocalResult::done;
    }

    LocalResult confirm() {
        const auto r = observe(EnrollmentReviewPurpose::code, 3, Prompt::transcript, code_started_, code_confirmed_);
        if (r == LocalResult::done) prompt_ = Prompt::none;
        return r;
    }

    EnrollmentCandidateSession& session_;
    EvaluationEnrollmentOffer local_offer_{}, peer_offer_{};
    IndependentInvitation invitation_{};
    bool have_local_offer_{}, have_peer_offer_{}, have_payload_{}, have_signature_{};
    bool own_started_{}, own_confirmed_{}, peer_started_{}, peer_confirmed_{};
    bool code_started_{}, code_confirmed_{};
    Prompt prompt_{Prompt::none};
};

}  // namespace opentrail::security_evaluation
