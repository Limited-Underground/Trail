#pragma once

#include <array>
#include <cstdint>

#include "opentrail/critical_alert.hpp"
#include "opentrail/critical_alert_ack.hpp"

namespace opentrail::integration {

enum class CriticalAlertAckResponseError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    inconsistent_decision,
    response_suppressed,
    producer_mismatch,
    clock_regression,
    observed_age_out_of_range,
    codec_failure,
};

struct CriticalAlertAckResponderConfiguration {
    std::uint64_t consumer_id{0};
    std::uint32_t consumer_boot_session_id{0};
    std::uint32_t initial_ack_sequence{0};
};

struct CriticalAlertAckResponse {
    CriticalAlertAckResponseError error{
        CriticalAlertAckResponseError::invalid_state};
    AlertAckCodecError codec_error{AlertAckCodecError::none};
    CriticalAlertAck acknowledgement{};
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> frame{};

    [[nodiscard]] constexpr bool produced() const {
        return error == CriticalAlertAckResponseError::none;
    }
};

struct CriticalAlertAckResponderStatus {
    bool running{false};
    std::uint32_t next_ack_sequence{0};
    std::uint32_t produced{0};
    std::uint32_t accepted{0};
    std::uint32_t rejected{0};
    std::uint32_t suppressed{0};
    std::uint32_t failures{0};
};

// Converts only final CriticalAlertIngress decisions into OGK0 bytes. The
// transport remains responsible for authenticating OpenTrail to OpenGauge and
// for delivering the response to the same peer that supplied the context.
class CriticalAlertAckResponder {
public:
    [[nodiscard]] CriticalAlertAckResponseError start(
        const CriticalAlertAckResponderConfiguration& configuration);
    void stop();

    [[nodiscard]] CriticalAlertAckResponse respond(
        const AlertIngressResult& decision,
        const AlertIngressContext& context,
        std::uint64_t response_monotonic_ms);
    [[nodiscard]] CriticalAlertAckResponderStatus status() const;

private:
    CriticalAlertAckResponderConfiguration configuration_{};
    CriticalAlertAckResponderStatus status_{};
};

}  // namespace opentrail::integration
