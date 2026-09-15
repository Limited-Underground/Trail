#pragma once
// One explicitly armed, role-bound radio handshake. Routing selectors are not
// identity. No retry, fragmentation, host confirmation or membership capability.
#include <optional>
#include "opentrail/pair_bench_radio_channel.hpp"
#include "opentrail/independent_handshake_transport.hpp"
#include "opentrail/pair_radio_driver.hpp"
namespace opentrail::security_evaluation {
class PairRadioChannel final : public PairBenchRadioChannel {
public:
    explicit PairRadioChannel(PairRadioDriver& driver) : driver_(driver) {}
    ~PairRadioChannel() { (void)close(); }
    PairRadioChannel(const PairRadioChannel&) = delete;
    PairRadioChannel& operator=(const PairRadioChannel&) = delete;
    bool begin(IndependentHandshakeEndpoint& endpoint, InvitationRole role,
               std::uint64_t deadline_ms, std::uint64_t now_ms) {
        return operation([&] {
            if (armed_ || endpoint_ || !time(now_ms) || deadline_ms <= now_ms ||
                (role != InvitationRole::initiator && role != InvitationRole::responder)) return false;
            endpoint_ = &endpoint; role_ = role;
            started_at_ = now_ms; deadline_ = deadline_ms;
            maximum_tx_ = role == InvitationRole::initiator ? 2U : 1U;
            expected_rx_ = role == InvitationRole::initiator ? 1U : 2U;
            if (endpoint.state() != EndpointState::handshake || !fresh()) return false;
            transport_.emplace(endpoint, driver_, HandshakeTransportConfig{
                role == InvitationRole::initiator ? 1U : 2U,
                role == InvitationRole::initiator ? 2U : 1U, 1U});
            start_attempted_ = true;
            if (!driver_.start(deadline_ms, maximum_tx_) || !fresh()) return false;
            armed_ = true;
            return sample();
        });
    }
    bool tick(std::uint64_t now_ms) {
        return operation([&] {
            if (!armed_ || !time(now_ms) || !fresh() || now_ms >= deadline_) return false;
            if (complete_) return true;
            if (role_ == InvitationRole::initiator && sent_ == 0) {
                if (!send(now_ms)) return false;
            }
            const auto received = transport_->poll(now_ms);
            if (!fresh() || received == HandshakeTransportPoll::refused) return false;
            if (received == HandshakeTransportPoll::frame) {
                ++received_;
                if (received_ > expected_rx_) return false;
                if ((role_ == InvitationRole::responder && received_ == 1 && sent_ == 0) ||
                    (role_ == InvitationRole::initiator && received_ == 1 && sent_ == 1)) {
                    if (!send(now_ms)) return false;
                }
            }
            if (!sample()) return false;
            if (endpoint_->state() == EndpointState::review && sent_ == maximum_tx_ &&
                received_ == expected_rx_ && stats_.tx_attempts == maximum_tx_ &&
                stats_.tx_completed == maximum_tx_ && stats_.rx_frames == expected_rx_) {
                // In particular A's final frame must complete before FEM/RX stop.
                if (!fresh() || !stop_driver() || !fresh()) return false;
                complete_ = true;
            }
            return true;
        });
    }
    bool close() {
        if (busy_) { revoked_ = true; return false; }
        if (closed_) return cleanup_ok_ && !revoked_;
        busy_ = true;
        cleanup();
        busy_ = false;
        return cleanup_ok_ && !revoked_;
    }
    bool armed() const { return armed_; }
    bool complete() const { return complete_ && !closed_ && !revoked_; }
    PairBenchRadioStatistics statistics() const override { return {stats_.tx_attempts, stats_.tx_completed, stats_.rx_frames, stats_.rx_errors, stats_.stopped}; }
    std::uint64_t elapsed_ms() const { return elapsed_; }
private:
    bool fresh() { return !revoked_ && endpoint_ && endpoint_->poll() && !revoked_; }
    bool time(std::uint64_t now) {
        if (now == std::numeric_limits<std::uint64_t>::max() || (clock_seen_ && now < last_now_)) return false;
        clock_seen_ = true; last_now_ = now;
        if (armed_ && !complete_) elapsed_ = now - started_at_;
        return true;
    }
    bool send(std::uint64_t now) {
        if (sent_ >= maximum_tx_ || !fresh() || !transport_->send(now) || !fresh()) return false;
        ++sent_;
        return true;
    }
    bool sample() {
        if (!fresh()) return false;
        const auto next = driver_.statistics();
        if (!fresh()) return false;
        const bool valid = !next.stopped && next.tx_attempts >= stats_.tx_attempts &&
            next.tx_completed >= stats_.tx_completed && next.rx_frames >= stats_.rx_frames &&
            next.tx_attempts <= maximum_tx_ && next.tx_completed <= next.tx_attempts &&
            next.rx_frames <= expected_rx_ && next.rx_errors == 0;
        stats_ = next;
        return valid;
    }
    bool stop_driver() {
        if (stop_attempted_) return stop_ok_;
        stop_attempted_ = true;
        stop_ok_ = driver_.stop();
        stats_ = driver_.statistics();
        stop_ok_ = stop_ok_ && stats_.stopped && !revoked_;
        return stop_ok_;
    }
    void cleanup() {
        closed_ = true;
        // Independent cleanup: a failed physical stop cannot suppress wiping.
        const bool stopped = !start_attempted_ || stop_driver();
        const bool adapter_closed = !transport_ || transport_->close();
        transport_.reset(); // Destroy while the attached endpoint is still alive.
        const bool endpoint_closed = !endpoint_ || endpoint_->close();
        const bool cleared = !endpoint_ || endpoint_->secrets_cleared();
        cleanup_ok_ = stopped && adapter_closed && endpoint_closed && cleared;
    }
    template<class Action> bool operation(Action action) {
        if (busy_) { revoked_ = true; return false; }
        if (closed_) return false;
        busy_ = true;
        const bool accepted = action() && !revoked_;
        if (!accepted) cleanup();
        busy_ = false;
        return accepted;
    }
    PairRadioDriver& driver_;
    IndependentHandshakeEndpoint* endpoint_{nullptr};
    std::optional<IndependentHandshakeTransport> transport_;
    InvitationRole role_{InvitationRole::initiator};
    PairRadioStatistics stats_{};
    std::uint64_t started_at_{0}, deadline_{0}, last_now_{0}, elapsed_{0};
    unsigned maximum_tx_{0}, expected_rx_{0}, sent_{0}, received_{0};
    bool clock_seen_{false}, armed_{false}, complete_{false}, start_attempted_{false};
    bool stop_attempted_{false}, stop_ok_{false}, busy_{false}, revoked_{false}, closed_{false}, cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
