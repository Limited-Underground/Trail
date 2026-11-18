#pragma once
// First board pairing over LoRa, product composition (V1 F5, step R2a 5b).
// Plan: docs/security/F5-R2-BOARD-PAIRING-PLAN.md. Decisions D-076, D-082,
// D-085, D-086, D-087.
//
// App task only. When the owner's phone has an admitted product pairing request
// (role and code, D-085), this owner starts exactly one enrollment session for
// it. It copies the request into its own app-task request owner; the phone's
// request still governs: every active tick re-reads it, and once it is
// cancelled, expires or the connection changes, the attempt ends.
//
// Trust is never decided here. The enrollment session authenticates every
// frame; the person compares fingerprints and the transcript on each board and
// presses the button (BOOT arbiter, which also preempts for factory reset).
//
// After local commit this owner keeps answering the peer's repeats for
// kLingerMs (review R2A-1), so a lost final control frame can still be
// recovered. Local "paired" means this board committed; it is not proof that
// the other board did.
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
        paired,       // this board committed (peer not proven)
        failed,       // attempt ended without pairing on this board
        closed,       // terminal (factory reset)
    };
    static constexpr std::uint64_t kLingerMs = 15000;

    BoardPairingRuntime(HeltecEnrollmentInputArbiter& input, security::SecureRandomSource& random)
        : input_(input), random_(random) {}
    ~BoardPairingRuntime() override { close(); }
    BoardPairingRuntime(const BoardPairingRuntime&) = delete;
    BoardPairingRuntime& operator=(const BoardPairingRuntime&) = delete;

    // radio_ready: the radio is running and transmit is permitted now. A new
    // attempt starts only when it is ready. Returns a frame to offer to the
    // radio (out_size 0 if none); report the result with sent().
    void service(std::uint64_t now_ms, bool radio_ready, protocol::pairing::FrameBuffer& out,
                 std::size_t& out_size);
    // Whether the radio accepted the frame the last service() offered.
    void sent(bool accepted, std::uint64_t now_ms);
    void unsent();
    // Received LoRa bytes. Non-pairing traffic is ignored by the coordinator.
    void on_receive(const std::uint8_t* data, std::size_t size);
    // The radio stopped: an attempt in progress ends.
    void radio_lost();
    // Terminal: called before factory reset. Closes the session and unmounts storage.
    void close();

    Phase phase() const { return phase_; }
    // True while frames may flow: running, or lingering after a local commit.
    bool active() const { return phase_ == Phase::running || lingering_; }
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
    companion::SelectedEnrollmentRequestOwner request_{
        companion::SelectedEnrollmentRequestOwner::kBoardPairingLimitMs};  // D-110
    companion::SelectedEnrollmentRequest phone_{};
    std::uint64_t last_ms_{0}, linger_until_ms_{0};
    bool clock_seen_{false}, have_phone_{false}, busy_{false}, lingering_{false};
    std::optional<PairingNvsStorage> storage_;  // mounted once per boot (review R2A-8)
    std::optional<targets::heltec_v4_bench::EnrollmentIdentityNvsStorage> identity_;
    std::optional<Device> device_;
    std::optional<Session> session_;
    std::optional<security_evaluation::PairingSessionAdapter> adapter_;
    std::optional<Coordinator> coordinator_;
    Phase phase_{Phase::idle};
};

}  // namespace opentrail::target::heltec_v4_bench
