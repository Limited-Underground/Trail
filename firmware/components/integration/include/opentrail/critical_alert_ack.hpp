#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/critical_alert.hpp"

namespace opentrail::integration {

inline constexpr std::uint8_t kCriticalAlertAckSchemaVersion = 0;
inline constexpr std::size_t kCriticalAlertAckFrameBytes = 64;

enum class AlertAckDisposition : std::uint8_t { accepted = 1, rejected = 2 };
enum class AlertAckReason : std::uint8_t {
    none = 0,
    unauthorized = 1,
    stale = 2,
    duplicate = 3,
    conflict = 4,
    rate_limited = 5,
    malformed = 6,
    unsupported = 7,
    internal_error = 8,
};

struct CriticalAlertAck {
    AlertAckDisposition disposition{AlertAckDisposition::accepted};
    AlertAckReason reason{AlertAckReason::none};
    AlertState state{AlertState::asserted};
    std::uint64_t consumer_id{0};
    std::uint64_t producer_id{0};
    std::uint64_t event_id{0};
    std::uint64_t condition_id{0};
    std::uint32_t consumer_boot_session_id{0};
    std::uint32_t ack_sequence{0};
    std::uint32_t observed_alert_age_ms{0};
};

enum class AlertAckCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    malformed,
    unsupported_version,
    noncanonical,
    integrity_failure,
    unknown_enum,
    invalid_identity,
    inconsistent_disposition,
    age_out_of_range,
};

struct AlertAckEncodeResult {
    AlertAckCodecError error{AlertAckCodecError::invalid_argument};
    std::size_t encoded_bytes{0};
    [[nodiscard]] constexpr bool encoded() const {
        return error == AlertAckCodecError::none;
    }
};

struct AlertAckDecodeResult {
    AlertAckCodecError error{AlertAckCodecError::malformed};
    CriticalAlertAck acknowledgement{};
    [[nodiscard]] constexpr bool decoded() const {
        return error == AlertAckCodecError::none;
    }
};

[[nodiscard]] AlertAckCodecError validate_critical_alert_ack(
    const CriticalAlertAck& acknowledgement);
[[nodiscard]] AlertAckEncodeResult encode_critical_alert_ack(
    const CriticalAlertAck& acknowledgement,
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes>& output);
[[nodiscard]] AlertAckDecodeResult decode_critical_alert_ack(
    const std::uint8_t* data,
    std::size_t size);

}  // namespace opentrail::integration
