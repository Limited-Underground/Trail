#pragma once
// First board pairing over LoRa, product composition (V1 F5, step R2a 5b).
// Plan: docs/security/F5-R2-BOARD-PAIRING-PLAN.md. Decisions D-076, D-082,
// D-085, D-086.
//
// App task only. When the owner's phone has an admitted product pairing request
// (role and code, D-085), this owner starts exactly one enrollment session for
// it. It copies the request into its own app-task request owner; the phone's
// request still governs: once it is cancelled, expires or the connection
// changes, current() reports the authority unavailable and the session stops.
//
// Trust is never decided here. The enrollment session authenticates every
// frame; the person compares fingerprints and the transcript on each board and
// presses the button (BOOT arbiter, which also preempts for factory reset).
#include <cstddef>
#include <cstdint>
#include <optional>

#include "enrollment_identity_nvs_storage.hpp"
#include "heltec_enrollment_input_arbiter.hpp"
#include "heltec_v4_secure_random.hpp"
#include "pairing_nvs_storage.hpp"
#include "opentrail/enrollment_session_review_device_port.hpp"
#include "opentrail/pairing_coordinator.hpp"
#include "opentrail/pairing_session_adapter.hpp"

namespace opentrail::target::heltec_v4_bench {

class BoardPairingRuntime final : private companion::DeviceNameAuthoritySource {
public:
    enum class Phase : std::uint8_t {
        idle,         // no attempt
        running,      // exchanging frames or waiting for a press
        paired,       // committed and ready on this board
        failed,       // attempt ended without pairing
        closed,       // terminal (factory reset)
    };

    BoardPairingRuntime(HeltecEnrollmentInputArbiter& input, security::SecureRandomSource& random)
        : input_(input), random_(random) {}
    ~BoardPairingRuntime() override { close(); }
    BoardPairingRuntime(const BoardPairingRuntime&) = delete;
    BoardPairingRuntime& operator=(const BoardPairingRuntime&) = delete;

    // Starts an attempt for a new admitted request, advances the current one,
    // and returns a frame to transmit now (out_size 0 if none).
    void service(std::uint64_t now_ms, protocol::pairing::FrameBuffer& out, std::size_t& out_size);
    // Received LoRa bytes. Non-pairing traffic is ignored by the coordinator.
    void on_receive(const std::uint8_t* data, std::size_t size);
    // Terminal: called before factory reset. Closes the session and unmounts storage.
    void close();

    Phase phase() const { return phase_; }
    bool active() const { return phase_ == Phase::running; }
    security_evaluation::PairingSessionAdapter::Prompt prompt() const {
        return adapter_ ? adapter_->prompt() : security_evaluation::PairingSessionAdapter::Prompt::none;
    }

private:
    using Session = security_evaluation::EnrollmentCandidateSession;
    using Device = security_evaluation::EnrollmentSessionReviewDevicePort<HeltecEnrollmentInputArbiter>;
    using Coordinator = protocol::pairing::PairingCoordinator<security_evaluation::PairingSessionAdapter>;
    static constexpr std::uint64_t kClockDomain = 0x52324131;  // "R2A1", local constant

    companion::DeviceNameAuthority current() noexcept override;
    bool start(const companion::SelectedEnrollmentRequest& phone, std::uint64_t now_ms);
    void end(Phase result);
    bool same_phone_request(const companion::SelectedEnrollmentRequest& r) const;

    HeltecEnrollmentInputArbiter& input_;
    security::SecureRandomSource& random_;
    companion::SelectedEnrollmentRequestOwner request_{};
    companion::SelectedEnrollmentRequest phone_{};
    std::uint64_t last_ms_{0};
    bool clock_seen_{false}, have_phone_{false}, busy_{false};
    std::optional<PairingNvsStorage> storage_;
    std::optional<targets::heltec_v4_bench::EnrollmentIdentityNvsStorage> identity_;
    std::optional<Device> device_;
    std::optional<Session> session_;
    std::optional<security_evaluation::PairingSessionAdapter> adapter_;
    std::optional<Coordinator> coordinator_;
    Phase phase_{Phase::idle};
};

}  // namespace opentrail::target::heltec_v4_bench
