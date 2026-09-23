#pragma once
// Evaluation enrollment bench composition; explicit OTENROLL1 USB or opt-in RF. The host
// provisions evaluation trust pins; this is not a production trust channel.
// One serialized app owner supplies monotonic context/time and isolated storage.
// A clock/context does not by itself detect physical USB disconnection. Requires
// initialized crypto/entropy, display and the raw local button. No host command
// asserts local confirmation or peer activation; authenticated peer controls
// remain mandatory after both independent physical button decisions.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include "opentrail/independent_handshake_endpoint.hpp"
#include "opentrail/pair_bench_session.hpp"
#include "opentrail/companion_status_bridge.hpp"
#include "opentrail/enrolled_peer_transport.hpp"
#include "opentrail/pair_radio_driver.hpp"
#include "opentrail/enrolled_session_observer.hpp"

namespace opentrail::security_evaluation {
class EnrolledBenchSession final {
public:
    static constexpr std::size_t maximum_line_bytes = 700;
    static constexpr std::size_t output_capacity = 768;
    static constexpr std::uint64_t maximum_session_ms = 120000;
    using Output = std::array<char, output_capacity>;

    EnrolledBenchSession(security::SecureRandomSource& random, EvaluationStorageBank& bank,
                         EnrollmentEvidenceStore& evidence, ConfirmationAuthority& authority,
                         PairBenchDisplay& display, PairRadioDriver* radio = nullptr,
                         EnrolledSessionObserver* observer = nullptr)
        : random_(random), bank_(bank), evidence_(evidence), authority_(authority), display_(display), radio_(radio), observer_(observer) {}
    ~EnrolledBenchSession() { (void)close(); }
    EnrolledBenchSession(const EnrolledBenchSession&) = delete;
    EnrolledBenchSession& operator=(const EnrolledBenchSession&) = delete;

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
                tokens.values[0] != "OTENROLL1") {note(EnrolledFault::input);return false;}
            if (tokens.count == 2 && tokens.values[1] == "CLOSE") {
                const bool cleaned = cleanup();
                return writer.text("OTENROLL1 CLOSED ") && writer.number(cleaned ? 1 : 0) && writer.text("\n");
            }
            if (tokens.count == 2 && (tokens.values[1] == "RFSTAT" || tokens.values[1] == "RFREADY")) {
                if (!radio_ || !observe(!closed_)) return false;
                if (!closed_ && !service_completion()) return false;
                const auto stats=radio_->statistics();
                if (!observe(!closed_)) return false;
                const bool readiness = tokens.values[1] == "RFREADY";
                return writer.text(readiness ? "OTENROLL1 RFREADY " : "OTENROLL1 RFSTAT ") && writer.number(stats.tx_attempts) && writer.text(" ") &&
                    writer.number(stats.tx_completed) && writer.text(" ") && writer.number(stats.rx_frames) && writer.text(" ") &&
                    writer.number(stats.rx_errors) && writer.text(" ") && writer.number(stats.stopped?1:0) &&
                    (!readiness || (writer.text(" ") && writer.number(radio_->receive_ready()?1:0))) && writer.text("\n");
            }
            if (tokens.count == 2 && tokens.values[1] == "STATUS") {
                if (!observe(!closed_)) return false;
                if (!closed_ && !service_completion()) return false;
                return writer.text("OTENROLL1 STATUS ") &&
                    writer.number(static_cast<unsigned>(state())) && writer.text(" ") &&
                    writer.number(last_now_) && writer.text(" ") &&
                    writer.number(secrets_cleared() ? 1 : 0) && writer.text("\n");
            }
            if (closed_ || !observe()) return false;
            if (!service_completion()) return false;
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
        if(observer_)observer_->tick_begin(state()==EndpointState::review);
        const bool accepted=operation([&] {
            if (closed_) return cleanup_ok_;
            if (!observe()) return false;
            const auto button_now = last_now_;
            // Preserve the physical release timestamp from before housekeeping;
            // radio work must not lengthen the measured physical hold.
            if (!service_completion()) return false;
            if (!endpoint_ || endpoint_->state() != EndpointState::review) {
                return show_state_if_changed() && observe();
            }
            if (!refresh_offer()) return false;
            if (!review_displayed_) {
                if (!display_.show_review(offer_->role(), offer_->transcript())) {note(EnrolledFault::display);return false;}
                if(!observe())return false;
                review_displayed_ = true;
                milestone(EnrolledMilestone::review_ready, last_now_);
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
                milestone(EnrolledMilestone::button_pressed, pressed_at_);
                return true;
            }
            if (!pressed_) return true;
            // Durable offer readback must not inflate a short physical hold
            // into an accepted one. Start after readback, end before readback.
            if (!button_pressed) milestone(EnrolledMilestone::button_released, button_now);
            const auto held = button_now - pressed_at_;
            if (last_now_ - pressed_at_ > 3000) return false;
            if (button_pressed) return true;
            pressed_ = false;
            if (held < 500) return true;
            if (!endpoint_->confirm(*offer_) || !observe()) return false;
            milestone(EnrolledMilestone::confirmation_accepted, last_now_);
            offer_ = nullptr;
            return show_state_if_changed() && observe();
        });
        if(observer_)observer_->tick_end();
        return accepted;
    }

    // Cleanup is independent of time/USB availability and is never deferred to
    // a destructor as the acceptance path. STATUS/CLOSE remain queryable after
    // closure; INIT and every session operation stay permanently unavailable.
    bool close() {
        if (busy_) { note(EnrolledFault::reentry);revoked_ = true; return false; }
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
    // Explicit evaluation record encoding shared with the experimental radio
    // adapter. This USB protocol does not select a production wire.
    static std::array<std::uint8_t,108> encode_record(const EvaluationRecord& r) {
        std::array<std::uint8_t,108> bytes{}; EnrolledPeerTransport::encode_record(r,bytes.data()); return bytes;
    }
    static EvaluationRecord decode_record(const std::array<std::uint8_t,108>& bytes) {
        EvaluationRecord r{}; EnrolledPeerTransport::decode_record(bytes.data(),r); return r;
    }
    bool identity(Writer& writer) {
        return writer.text("OTENROLL1 ID ") &&
            writer.hex(endpoint_->public_identity().data(), 32) && writer.text(" ") &&
            writer.hex(endpoint_->boot_context().data(), 16) && writer.text(" ") &&
            writer.number(last_now_) && writer.text("\n");
    }
    bool dispatch(const Tokens& tokens, Writer& writer) {
        const auto name = tokens.values[1];
        if (name == "HELLO" && tokens.count == 2) {
            start_session();
            return writer.text("OTENROLL1 READY 1 ") && writer.number(last_now_) && writer.text("\n");
        }
        if (name == "INIT" && tokens.count == 5) {
            if (init_attempted_ || endpoint_) return false;
            start_session();
            init_attempted_ = true;
            std::uint64_t role = 0, group = 0;
            InvitationKey signer{};
            if (!decimal(tokens.values[2], role) || (role != 1 && role != 2) ||
                !unhex(tokens.values[3], signer.data(), signer.size()) ||
                !decimal(tokens.values[4], group) || group == 0) return false;
            role_ = static_cast<InvitationRole>(role);
            endpoint_.emplace(random_, bank_, evidence_, authority_, role_, signer, group);
            if (!endpoint_->initialize() || !endpoint_->prepare_identity() || !observe()) return false;
            return identity(writer);
        }
        if (!endpoint_) return false;
        if (name == "TIME" && tokens.count == 2) return identity(writer);
        if (name == "TRAFFIC" && tokens.count == 2) {
            const bool ready = endpoint_->ready();
            if (ready) activation_ready_pending_ = true;
            return writer.text("OTENROLL1 TRAFFIC ") && writer.number(ready ? 1 : 0) && writer.text("\n");
        }
        if (name == "BEGIN" && tokens.count == 3) {
            IndependentInvitation invitation{};
            if (tokens.values[2].size() != 504 ||
                !unhex(tokens.values[2].substr(0, 376), invitation.payload.data(), invitation.payload.size()) ||
                !unhex(tokens.values[2].substr(376), invitation.signature.data(), invitation.signature.size()) ||
                !endpoint_->begin(invitation)) return false;
            const auto fields = independent_invitation_detail::decode(invitation);
            issued_ = role_ == InvitationRole::initiator ? fields.issued_a_ms : fields.issued_b_ms;
            deadline_ = issued_ + (role_ == InvitationRole::initiator ? fields.window_a_ms : fields.window_b_ms);
            invitation_begun_ = true;
            return writer.text("OTENROLL1 OK BEGIN\n");
        }
        if (name == "RADIO" && tokens.count == 2) {
            if (!radio_ || transport_ || usb_used_ || !invitation_begun_) return false;
            radio_attempted_ = true;
            if (!radio_->start(deadline_,16) || !observe()) return false;
            const bool initiator = role_ == InvitationRole::initiator;
            transport_.emplace(*endpoint_,*radio_,HandshakeTransportConfig{initiator?1U:2U,initiator?2U:1U,1});
            return writer.text("OTENROLL1 OK RADIO\n");
        }
        if (name == "RFSEND" && tokens.count == 2)
            return transport_ && transport_->send_handshake(last_now_) && writer.text("OTENROLL1 OK RFSEND\n");
        if (name == "RFCONTROL" && tokens.count == 2)
            return transport_ && transport_->send_control(last_now_) && writer.text("OTENROLL1 OK RFCONTROL\n");
        if (name == "RFSTATUS" && tokens.count == 3) {
            std::uint64_t code=0;
            return transport_ && decimal(tokens.values[2],code) && code>=1 && code<=4 &&
                transport_->send_status(static_cast<std::uint8_t>(code),last_now_) && writer.text("OTENROLL1 OK RFSTATUS\n");
        }
        // Additive command: older OTENROLL1 implementations refuse it closed.
        // The initial RFPOLL still owns starting queued TX.
        if (name == "RFFINISH" && tokens.count == 2) {
            if (!transport_ || !radio_ || !transport_->finish_transmit(last_now_,[&] {
                return observe() && radio_->rearm_after_transmit() && observe();
            })) return false;
            const auto stats=radio_->statistics();
            return writer.text("OTENROLL1 RFFINISH ") && writer.number(stats.tx_attempts) && writer.text(" ") &&
                writer.number(stats.tx_completed) && writer.text(" ") && writer.number(stats.rx_frames) && writer.text(" ") &&
                writer.number(stats.rx_errors) && writer.text(" ") && writer.number(stats.stopped?1:0) &&
                writer.text(" ") && writer.number(radio_->receive_ready()?1:0) && writer.text("\n");
        }
        if (name == "RFPOLL" && tokens.count == 2) {
            if (!transport_) return false;
            std::uint8_t status=0;
            const auto result=transport_->poll(last_now_,status,[&] {
                // Final transport durable checks follow this admitted RX-only
                // maintenance before the command can publish any response.
                return radio_ && observe() && radio_->rearm_after_receive() && observe();
            });
            switch(result) {
                case EnrolledTransportPoll::waiting:return writer.text("OTENROLL1 RF WAIT\n");
                case EnrolledTransportPoll::handshake:return writer.text("OTENROLL1 RF HANDSHAKE\n");
                case EnrolledTransportPoll::control:return writer.text("OTENROLL1 RF CONTROL\n");
                case EnrolledTransportPoll::status:return status>=1 && status<=4 && writer.text("OTENROLL1 RF RECEIVED ") && writer.number(status) && writer.text("\n");
                case EnrolledTransportPoll::refused:return false;
            }
            return false;
        }
        const bool usb = name=="SEND" || name=="FRAME" || name=="NEXTCONTROL" || name=="CONTROL" || name=="SENDSTATUS" || name=="STATUSFRAME";
        if (usb && transport_) return false;
        if (usb) usb_used_=true;
        if (name == "SEND" && tokens.count == 2) {
            HandshakeFrame frame{};
            const bool sent = endpoint_->next_handshake(frame) &&
                writer.text("OTENROLL1 FRAME ") && writer.number(frame.step) && writer.text(" ") &&
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
                endpoint_->receive_handshake(frame) && writer.text("OTENROLL1 OK FRAME\n");
            sodium_memzero(frame.payload.data(), frame.payload.size());
            return received;
        }
        if ((name == "REVOKE" || name == "RESET") && tokens.count == 2) {
            const bool done = name == "REVOKE" ? endpoint_->revoke() : endpoint_->prepare_reset();
            if (!done || !cleanup()) return false;
            return writer.text("OTENROLL1 OK ") && writer.text(name) && writer.text("\n");
        }
        if ((name == "NEXTCONTROL" && tokens.count == 2) ||
            (name == "SENDSTATUS" && tokens.count == 3)) {
            EvaluationRecord record{};
            bool ok = false;
            if (name == "NEXTCONTROL") ok = endpoint_->next_control(record);
            else {
                std::uint64_t code = 0;
                if (!decimal(tokens.values[2], code) || code < 1 || code > 4) return false;
                CompanionStatusBridge bridge(*endpoint_);
                ok = bridge.encrypt({companion::CompanionActionKind::quick_status,
                    static_cast<protocol::QuickStatusKind>(code), 0}, record);
            }
            const auto bytes = encode_record(record);
            ok = ok && writer.text(name == "NEXTCONTROL" ? "OTENROLL1 CONTROL " : "OTENROLL1 STATUSFRAME ") &&
                writer.hex(bytes.data(), bytes.size()) && writer.text("\n");
            sodium_memzero(&record, sizeof record);
            return ok;
        }
        if ((name == "CONTROL" || name == "STATUSFRAME") && tokens.count == 3) {
            std::array<std::uint8_t,108> bytes{};
            if (!unhex(tokens.values[2], bytes.data(), bytes.size())) return false;
            const auto record = decode_record(bytes);
            if (name == "CONTROL") return endpoint_->receive_control(record) && writer.text("OTENROLL1 OK CONTROL\n");
            protocol::QuickStatusPayload status{};
            CompanionStatusBridge bridge(*endpoint_);
            return bridge.decrypt(record, status) && writer.text("OTENROLL1 RECEIVED ") &&
                writer.number(static_cast<std::uint8_t>(status.kind)) && writer.text("\n");
        }
        if (name == "REVIEW" && tokens.count == 2) {
            return refresh_offer() && writer.text("OTENROLL1 REVIEW ") &&
                writer.hex(offer_->transcript().data(), 32) && writer.text(" ") &&
                writer.number(offer_->deadline_ms()) && writer.text(" ") &&
                writer.number(last_now_) && writer.text("\n");
        }
        return false;
    }
    bool service_completion() {
        // Completion accounts for an existing physical transmission. It cannot
        // send queued work or admit a packet; those retain every durable guard.
        if (radio_attempted_ && !radio_->service_pending_transmit()) return false;
        return observe();
    }
    bool refresh_offer() {
        if (endpoint_->state() != EndpointState::review) return false;
        const auto* current = endpoint_->offer();
        if (current == nullptr || (offer_ != nullptr && offer_ != current) || !observe()) return false;
        offer_ = current;
        return true;
    }
    bool show_state_if_changed() {
        const auto current = state();
        if (state_displayed_ && displayed_state_ == current) return true;
        if (!display_.show_state(current)) {note(EnrolledFault::display);return false;}
        if(!observe())return false;
        displayed_state_ = current;
        state_displayed_ = true;
        return true;
    }
    bool reject(EnrolledFailureReason reason, const ConfirmationSample& sample, std::uint64_t previous) {
        const EnrolledFailureDetail detail{EnrolledFailureLayer::bench_session, reason, true,
            invitation_begun_, session_started_, sample.now_ms, previous, issued_, deadline_, started_at_};
        if (observer_) observer_->rejection(detail);
        note(reason == EnrolledFailureReason::reentry ? EnrolledFault::reentry :
            reason == EnrolledFailureReason::entropy ? EnrolledFault::entropy : EnrolledFault::authority_clock);
        return false;
    }
    bool observe(bool enforce_limits = true) {
        const auto sample = authority_.sample();
        const auto previous = last_now_;
        if (revoked_) return reject(EnrolledFailureReason::reentry, sample, previous);
        if (sample.context.transport_generation == 0 || sample.context.session_nonce == 0)
            return reject(EnrolledFailureReason::invalid_context, sample, previous);
        if (sample.now_ms == std::numeric_limits<std::uint64_t>::max())
            return reject(EnrolledFailureReason::invalid_clock, sample, previous);
        if (clock_seen_ && sample.context != context_) return reject(EnrolledFailureReason::context_changed, sample, previous);
        if (clock_seen_ && sample.now_ms < last_now_) return reject(EnrolledFailureReason::clock_regression, sample, previous);
        if (!clock_seen_) context_ = sample.context;
        last_now_ = sample.now_ms;
        clock_seen_ = true;
        if (!enforce_limits) return true;
        if (random_.state() != security::EntropyState::ready) return reject(EnrolledFailureReason::entropy, sample, previous);
        if (revoked_) return reject(EnrolledFailureReason::reentry, sample, previous);
        if (session_started_ && last_now_ - started_at_ >= maximum_session_ms)
            return reject(EnrolledFailureReason::session_expired, sample, previous);
        if (invitation_begun_ && last_now_ < issued_)
            return reject(EnrolledFailureReason::window_before_issued, sample, previous);
        if (invitation_begun_ && last_now_ >= deadline_)
            return reject(EnrolledFailureReason::window_expired, sample, previous);
        return true;
    }
    void start_session() {
        if (!session_started_) { started_at_ = last_now_; session_started_ = true; }
    }
    // Consume the rejecting check's recorded detail, before cleanup can invoke
    // unrelated callbacks. Neither clock nor durable authority is sampled here.
    EnrolledFault classify_failure() {
        auto detail = transport_ ? transport_->consume_failure_detail() : EnrolledFailureDetail{};
        const auto endpoint_detail = endpoint_ ? endpoint_->consume_failure_detail() : EnrolledFailureDetail{};
        if (detail.reason == EnrolledFailureReason::none) detail = endpoint_detail;
        const bool window = endpoint_ && endpoint_->consume_window_expired();
        if (detail.reason != EnrolledFailureReason::none && observer_) observer_->rejection(detail);
        if (enrolled_authority_rejection(detail.reason) || window) return EnrolledFault::authority_clock;
        if (detail.reason == EnrolledFailureReason::entropy) return EnrolledFault::entropy;
        if (detail.reason == EnrolledFailureReason::reentry) return EnrolledFault::reentry;
        return EnrolledFault::session_protocol;
    }
    template<class Action> bool operation(Action action) {
        if (busy_) { note(EnrolledFault::reentry);revoked_ = true; return false; }
        busy_ = true;
        const bool accepted = action();
        if (!accepted || revoked_) {
            note(revoked_?EnrolledFault::reentry:classify_failure());
            refused_ = true;
            (void)cleanup();
        }
        if (accepted && !revoked_ && !closed_) {
            if (invitation_begun_ && !invitation_reported_) {
                invitation_reported_ = true;
                milestone(EnrolledMilestone::invitation_started, last_now_);
            }
            if (activation_ready_pending_ && !activation_reported_) {
                activation_reported_ = true;
                milestone(EnrolledMilestone::activation_ready, last_now_);
            }
        }
        busy_ = false;
        return accepted && !revoked_;
    }
    bool cleanup() {
        if (closed_) return cleanup_ok_;
        if(observer_)observer_->before_cleanup(refused_ || revoked_);
        closed_ = true;
        offer_ = nullptr;
        pressed_ = false;
        const bool radio_clean = !radio_attempted_ || radio_->stop();
        const bool transport_clean = !transport_ || transport_->close();
        const bool endpoint_closed = !endpoint_ || endpoint_->cancel();
        const bool cleared = !endpoint_ || endpoint_->secrets_cleared();
        cleanup_ok_ = radio_clean && transport_clean && endpoint_closed && cleared;
        if (!cleanup_ok_) refused_ = true;
        // Best-effort final presentation cannot replace the real cleanup result.
        (void)display_.show_state(state());
        return cleanup_ok_;
    }

    void milestone(EnrolledMilestone event, std::uint64_t now) {
        if (observer_) observer_->milestone(event, now, issued_, deadline_);
    }
    void note(EnrolledFault fault){if(observer_)observer_->fault(fault);}
    security::SecureRandomSource& random_;
    EvaluationStorageBank& bank_;
    EnrollmentEvidenceStore& evidence_;
    ConfirmationAuthority& authority_;
    PairBenchDisplay& display_;
    PairRadioDriver* radio_{nullptr};
    EnrolledSessionObserver* observer_{nullptr};
    std::optional<EnrolledPeerEndpoint> endpoint_;
    std::optional<EnrolledPeerTransport> transport_;
    bool radio_attempted_{false}, usb_used_{false};
    const IndependentConfirmationOffer* offer_{nullptr};
    ConfirmationContext context_{};
    InvitationRole role_{InvitationRole::initiator};
    EndpointState displayed_state_{EndpointState::empty};
    std::uint64_t started_at_{0}, last_now_{0}, issued_{0}, deadline_{0}, pressed_at_{0};
    bool clock_seen_{false}, session_started_{false}, init_attempted_{false}, invitation_begun_{false};
    bool busy_{false}, revoked_{false}, closed_{false}, cleanup_ok_{false}, refused_{false};
    bool review_displayed_{false}, release_seen_{false}, pressed_{false}, state_displayed_{false};
    bool invitation_reported_{false}, activation_ready_pending_{false}, activation_reported_{false};
};
} // namespace opentrail::security_evaluation
