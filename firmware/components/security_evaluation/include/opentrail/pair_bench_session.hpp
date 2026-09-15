#pragma once
// Physically authorized bench composition; USB by default, optional explicit RF. The host
// provisions evaluation trust pins; this is not a production trust channel.
// One serialized app owner supplies live USB custody/time, isolated storage,
// initialized crypto/entropy, display and the raw local button. No host command
// confirms, activates traffic, or asserts that the other device confirmed.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include "opentrail/independent_handshake_endpoint.hpp"
#include "opentrail/pair_bench_radio_channel.hpp"

namespace opentrail::security_evaluation {
class PairBenchDisplay {
public:
    virtual ~PairBenchDisplay() = default;
    virtual bool show_review(InvitationRole role, const InvitationKey& transcript) = 0;
    virtual bool show_state(EndpointState state) = 0;
};

class PairBenchSession final {
public:
    static constexpr std::size_t maximum_line_bytes = 700;
    static constexpr std::size_t output_capacity = 768;
    static constexpr std::uint64_t maximum_session_ms = 120000;
    using Output = std::array<char, output_capacity>;

    PairBenchSession(security::SecureRandomSource& random,
                     persistence::PersistentStorage& boot, persistence::PersistentStorage& role,
                     persistence::PersistentStorage& tx, persistence::PersistentStorage& rx,
                     ConfirmationAuthority& authority, PairBenchDisplay& display,
                     PairBenchRadioChannel* radio = nullptr)
        : random_(random), boot_(boot), role_storage_(role), tx_(tx), rx_(rx),
          authority_(authority), display_(display), radio_(radio) {}
    ~PairBenchSession() { (void)close(); }
    PairBenchSession(const PairBenchSession&) = delete;
    PairBenchSession& operator=(const PairBenchSession&) = delete;

    // A line excludes CR/LF. Exactly one space separates canonical tokens;
    // uppercase hex and unsigned decimal without leading zeroes are required.
    // The first HELLO/INIT starts the 120s window; startup ticks and READY
    // output permit sequential device preparation without creating an attempt.
    // A successful response includes one LF. Failure publishes zero bytes and
    // closes this one attempt; the app may emit its fixed refusal category.
    bool command(std::string_view line, Output& output, std::size_t& size) {
        std::array<char, maximum_line_bytes> copied{};
        const bool bounded = !line.empty() && line.size() <= copied.size();
        if (bounded) {
            for (std::size_t i = 0; i < line.size(); ++i) copied[i] = line[i];
        }
        output.fill(0);
        size = 0;
        Output staged{};
        Writer writer{staged};
        const bool accepted = operation([&] {
            Tokens tokens{};
            if (!bounded || !tokenize(std::string_view(copied.data(), line.size()), tokens) ||
                tokens.values[0] != "OTPAIR1") return false;
            if (tokens.count == 2 && tokens.values[1] == "CLOSE") {
                const bool cleaned = cleanup();
                return writer.text("OTPAIR1 CLOSED ") && writer.number(cleaned ? 1 : 0) && writer.text("\n");
            }
            if (tokens.count == 2 && tokens.values[1] == "RADIOSTAT") {
                if (!radio_ || !observe(!closed_)) return false;
                const auto stats = radio_->statistics();
                const auto reported = state() == EndpointState::review && !radio_->complete() ? EndpointState::handshake : state();
                return writer.text("OTPAIR1 RADIOSTAT ") && writer.number(static_cast<unsigned>(reported)) &&
                    writer.text(" ") && writer.number(stats.tx_attempts) && writer.text(" ") &&
                    writer.number(stats.tx_completed) && writer.text(" ") && writer.number(stats.rx_frames) &&
                    writer.text(" ") && writer.number(stats.rx_errors) && writer.text(" ") &&
                    writer.number(radio_->elapsed_ms()) && writer.text(" ") && writer.number(stats.stopped ? 1 : 0) && writer.text("\n");
            }
            if (tokens.count == 2 && tokens.values[1] == "STATUS") {
                if (!observe(!closed_)) return false;
                return writer.text("OTPAIR1 STATUS ") &&
                    writer.number(static_cast<unsigned>(state())) && writer.text(" ") &&
                    writer.number(last_now_) && writer.text(" ") &&
                    writer.number(secrets_cleared() ? 1 : 0) && writer.text("\n");
            }
            if (closed_ || !observe()) return false;
            if (!dispatch(tokens, writer) || !observe()) return false;
            return true;
        });
        if (accepted) { output = staged; size = writer.size; }
        sodium_memzero(copied.data(), copied.size());
        sodium_memzero(staged.data(), staged.size());
        return accepted;
    }

    // The target polls the raw active button level. Release must be observed in
    // a later tick after review display succeeds. A fresh 500..3000ms hold then
    // release authorizes exactly the cached owner offer; a longer hold refuses.
    bool tick(bool button_pressed) {
        return operation([&] {
            if (closed_) return cleanup_ok_;
            if (!observe()) return false;
            const auto button_now = last_now_;
            if (radio_ && radio_->armed()) {
                if (!radio_->tick(last_now_) || !observe()) return false;
                if (!radio_->complete()) return true;
            }
            if (!endpoint_ || endpoint_->state() != EndpointState::review) {
                return show_state_if_changed() && observe();
            }
            if (!refresh_offer()) return false;
            if (!review_displayed_) {
                if (!display_.show_review(offer_->role(), offer_->transcript()) || !observe()) return false;
                review_displayed_ = true;
                release_seen_ = false;
                pressed_ = false;
                return true;
            }
            if (!release_seen_) {
                if (!button_pressed) release_seen_ = true;
                return true;
            }
            if (button_pressed && !pressed_) {
                pressed_ = true;
                pressed_at_ = last_now_;
                return true;
            }
            if (!pressed_) return true;
            // Durable offer readback must not inflate a short physical hold
            // into an accepted one. Start after readback, end before readback.
            const auto held = button_now - pressed_at_;
            if (last_now_ - pressed_at_ > 3000) return false;
            if (button_pressed) return true;
            pressed_ = false;
            if (held < 500) return true;
            if (!endpoint_->confirm(*offer_) || !observe()) return false;
            offer_ = nullptr;
            return show_state_if_changed() && observe();
        });
    }

    // Cleanup is independent of time/USB availability and is never deferred to
    // a destructor as the acceptance path. STATUS/CLOSE remain queryable after
    // closure; INIT and every session operation stay permanently unavailable.
    bool close() {
        if (busy_) { revoked_ = true; return false; }
        busy_ = true;
        const bool result = cleanup();
        busy_ = false;
        return result && !revoked_;
    }
    EndpointState state() const {
        if (refused_) return EndpointState::refused;
        if (closed_) return EndpointState::cancelled;
        return endpoint_ ? endpoint_->state() : EndpointState::empty;
    }
    bool secrets_cleared() const { return !endpoint_ || endpoint_->secrets_cleared(); }
    bool cleanup_ok() const { return closed_ && cleanup_ok_; }

private:
    struct Tokens { std::array<std::string_view, 5> values{}; std::size_t count{0}; };
    struct Writer {
        Output& output;
        std::size_t size{0};
        bool text(std::string_view value) {
            if (value.size() > output.size() - size) return false;
            for (char c : value) output[size++] = c;
            return true;
        }
        bool number(std::uint64_t value) {
            std::array<char, 20> digits{};
            std::size_t count = 0;
            do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value != 0);
            if (count > output.size() - size) return false;
            while (count != 0) output[size++] = digits[--count];
            return true;
        }
        bool hex(const std::uint8_t* bytes, std::size_t count) {
            constexpr char alphabet[] = "0123456789ABCDEF";
            if (count > (output.size() - size) / 2) return false;
            for (std::size_t i = 0; i < count; ++i) {
                output[size++] = alphabet[bytes[i] >> 4];
                output[size++] = alphabet[bytes[i] & 15];
            }
            return true;
        }
    };

    static bool tokenize(std::string_view line, Tokens& output) {
        if (line.empty() || line.front() == ' ' || line.back() == ' ') return false;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i != line.size() && line[i] != ' ') {
                if (line[i] < '!' || line[i] > '~') return false;
                continue;
            }
            if (i == start || output.count == output.values.size()) return false;
            output.values[output.count++] = line.substr(start, i - start);
            start = i + 1;
        }
        return output.count >= 2;
    }
    static bool decimal(std::string_view text, std::uint64_t& output) {
        if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
        std::uint64_t value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') return false;
            const auto digit = static_cast<unsigned>(c - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
            value = value * 10 + digit;
        }
        output = value;
        return true;
    }
    static bool unhex(std::string_view text, std::uint8_t* output, std::size_t count) {
        if (text.size() != count * 2) return false;
        for (std::size_t i = 0; i < count; ++i) {
            unsigned byte = 0;
            for (unsigned j = 0; j < 2; ++j) {
                const char c = text[i * 2 + j];
                if (c >= '0' && c <= '9') byte = byte * 16 + static_cast<unsigned>(c - '0');
                else if (c >= 'A' && c <= 'F') byte = byte * 16 + static_cast<unsigned>(c - 'A' + 10);
                else return false;
            }
            output[i] = static_cast<std::uint8_t>(byte);
        }
        return true;
    }
    bool identity(Writer& writer) {
        return writer.text("OTPAIR1 ID ") &&
            writer.hex(endpoint_->public_identity().data(), 32) && writer.text(" ") &&
            writer.hex(endpoint_->boot_context().data(), 16) && writer.text(" ") &&
            writer.number(last_now_) && writer.text("\n");
    }
    bool dispatch(const Tokens& tokens, Writer& writer) {
        const auto name = tokens.values[1];
        if (name == "HELLO" && tokens.count == 2) {
            start_session();
            return writer.text("OTPAIR1 READY 1 ") && writer.number(last_now_) && writer.text("\n");
        }
        if (name == "RADIOINFO" && tokens.count == 2) {
            return radio_ && session_started_ && writer.text("OTPAIR1 RADIOINFO 1 915000000 125000 7 5 2 154 2\n");
        }
        if (radio_ && (name == "SEND" || name == "FRAME")) return false;
        if (name == "INIT" && tokens.count == 4) {
            if (init_attempted_ || endpoint_) return false;
            start_session();
            init_attempted_ = true;
            std::uint64_t role = 0;
            InvitationKey signer{};
            if (!decimal(tokens.values[2], role) || (role != 1 && role != 2) ||
                !unhex(tokens.values[3], signer.data(), signer.size())) return false;
            role_ = static_cast<InvitationRole>(role);
            endpoint_.emplace(random_, boot_, role_storage_, tx_, rx_, authority_, role_, signer);
            if (!endpoint_->prepare_identity() || !observe()) return false;
            return identity(writer);
        }
        if (!endpoint_) return false;
        if (name == "TIME" && tokens.count == 2) return identity(writer);
        if (name == "INV" && tokens.count == 4) {
            InvitationKey peer{};
            IndependentInvitation invitation{};
            if (!unhex(tokens.values[2], peer.data(), peer.size()) || tokens.values[3].size() != 504 ||
                !unhex(tokens.values[3].substr(0, 376), invitation.payload.data(), invitation.payload.size()) ||
                !unhex(tokens.values[3].substr(376), invitation.signature.data(), invitation.signature.size()) ||
                !endpoint_->begin(invitation, peer)) return false;
            const auto fields = independent_invitation_detail::decode(invitation);
            issued_ = role_ == InvitationRole::initiator ? fields.issued_a_ms : fields.issued_b_ms;
            deadline_ = issued_ + (role_ == InvitationRole::initiator ? fields.window_a_ms : fields.window_b_ms);
            invitation_begun_ = true;
            return writer.text("OTPAIR1 OK INV\n");
        }
        if (name == "RADIO" && tokens.count == 2) {
            return radio_ && invitation_begun_ && radio_->begin(*endpoint_, role_, deadline_, last_now_) &&
                observe() && writer.text("OTPAIR1 OK RADIO\n");
        }
        if (name == "SEND" && tokens.count == 2) {
            HandshakeFrame frame{};
            const bool sent = endpoint_->next_send(frame) &&
                writer.text("OTPAIR1 FRAME ") && writer.number(frame.step) && writer.text(" ") &&
                writer.number(frame.payload_bytes) && writer.text(" ") &&
                writer.hex(frame.payload.data(), frame.payload_bytes) && writer.text("\n");
            sodium_memzero(frame.payload.data(), frame.payload.size());
            return sent;
        }
        if (name == "FRAME" && tokens.count == 5) {
            std::uint64_t step = 0, bytes = 0;
            HandshakeFrame frame{};
            if (!decimal(tokens.values[2], step) || step < 1 || step > 3 ||
                !decimal(tokens.values[3], bytes) || bytes == 0 || bytes > frame.payload.size()) return false;
            frame.version = 1;
            frame.step = static_cast<std::uint8_t>(step);
            frame.payload_bytes = static_cast<std::uint16_t>(bytes);
            const bool received = unhex(tokens.values[4], frame.payload.data(), frame.payload_bytes) &&
                endpoint_->receive(frame) && writer.text("OTPAIR1 OK FRAME\n");
            sodium_memzero(frame.payload.data(), frame.payload.size());
            return received;
        }
        if (name == "REVIEW" && tokens.count == 2) {
            return refresh_offer() && writer.text("OTPAIR1 REVIEW ") &&
                writer.hex(offer_->transcript().data(), 32) && writer.text(" ") &&
                writer.number(offer_->deadline_ms()) && writer.text(" ") &&
                writer.number(last_now_) && writer.text("\n");
        }
        return false;
    }
    bool refresh_offer() {
        if ((radio_ && !radio_->complete()) || endpoint_->state() != EndpointState::review) return false;
        const auto* current = endpoint_->offer();
        if (current == nullptr || (offer_ != nullptr && offer_ != current) || !observe()) return false;
        offer_ = current;
        return true;
    }
    bool show_state_if_changed() {
        const auto current = state();
        if (state_displayed_ && displayed_state_ == current) return true;
        if (!display_.show_state(current) || !observe()) return false;
        displayed_state_ = current;
        state_displayed_ = true;
        return true;
    }
    bool observe(bool enforce_limits = true) {
        const auto sample = authority_.sample();
        if (revoked_ || sample.context.transport_generation == 0 || sample.context.session_nonce == 0 ||
            sample.now_ms == std::numeric_limits<std::uint64_t>::max() ||
            (clock_seen_ && (sample.context != context_ || sample.now_ms < last_now_))) return false;
        if (!clock_seen_) context_ = sample.context;
        last_now_ = sample.now_ms;
        clock_seen_ = true;
        if (!enforce_limits) return true;
        return random_.state() == security::EntropyState::ready && !revoked_ &&
            (!session_started_ || last_now_ - started_at_ < maximum_session_ms) &&
            (!invitation_begun_ || (last_now_ >= issued_ && last_now_ < deadline_));
    }
    void start_session() {
        if (!session_started_) { started_at_ = last_now_; session_started_ = true; }
    }
    template<class Action> bool operation(Action action) {
        if (busy_) { revoked_ = true; return false; }
        busy_ = true;
        const bool accepted = action();
        if (!accepted || revoked_) {
            refused_ = true;
            (void)cleanup();
        }
        busy_ = false;
        return accepted && !revoked_;
    }
    bool cleanup() {
        if (closed_) return cleanup_ok_;
        closed_ = true;
        offer_ = nullptr;
        pressed_ = false;
        const bool radio_clean = !radio_ || radio_->close();
        const bool endpoint_closed = !endpoint_ || endpoint_->close();
        const bool cleared = !endpoint_ || endpoint_->secrets_cleared();
        cleanup_ok_ = radio_clean && endpoint_closed && cleared;
        if (!cleanup_ok_) refused_ = true;
        // Best-effort final presentation cannot replace the real cleanup result.
        (void)display_.show_state(state());
        return cleanup_ok_;
    }

    security::SecureRandomSource& random_;
    persistence::PersistentStorage& boot_;
    persistence::PersistentStorage& role_storage_;
    persistence::PersistentStorage& tx_;
    persistence::PersistentStorage& rx_;
    ConfirmationAuthority& authority_;
    PairBenchDisplay& display_;
    PairBenchRadioChannel* radio_{nullptr};
    std::optional<IndependentHandshakeEndpoint> endpoint_;
    const IndependentConfirmationOffer* offer_{nullptr};
    ConfirmationContext context_{};
    InvitationRole role_{InvitationRole::initiator};
    EndpointState displayed_state_{EndpointState::empty};
    std::uint64_t started_at_{0}, last_now_{0}, issued_{0}, deadline_{0}, pressed_at_{0};
    bool clock_seen_{false}, session_started_{false}, init_attempted_{false}, invitation_begun_{false};
    bool busy_{false}, revoked_{false}, closed_{false}, cleanup_ok_{false}, refused_{false};
    bool review_displayed_{false}, release_seen_{false}, pressed_{false}, state_displayed_{false};
};
} // namespace opentrail::security_evaluation
