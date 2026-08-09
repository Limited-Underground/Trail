#include "opentrail/critical_alert_ack.hpp"

#include <algorithm>

namespace opentrail::integration {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'G', 'K', '0'}};
constexpr std::size_t kChecksumOffset = 60;
constexpr std::uint32_t kMaximumObservedAgeMs = 86400000U;

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}
void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}
std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}
std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}
bool known_disposition(AlertAckDisposition value) {
    return value == AlertAckDisposition::accepted ||
           value == AlertAckDisposition::rejected;
}
bool known_reason(AlertAckReason value) {
    return static_cast<std::uint8_t>(value) <=
           static_cast<std::uint8_t>(AlertAckReason::internal_error);
}
bool known_state(AlertState value) {
    return value == AlertState::asserted || value == AlertState::cleared;
}

}  // namespace

AlertAckCodecError validate_critical_alert_ack(
    const CriticalAlertAck& acknowledgement) {
    if (!known_disposition(acknowledgement.disposition) ||
        !known_reason(acknowledgement.reason) ||
        !known_state(acknowledgement.state)) {
        return AlertAckCodecError::unknown_enum;
    }
    if (acknowledgement.consumer_id == 0 ||
        acknowledgement.producer_id == 0 ||
        acknowledgement.event_id == 0 ||
        acknowledgement.condition_id == 0 ||
        acknowledgement.consumer_boot_session_id == 0) {
        return AlertAckCodecError::invalid_identity;
    }
    if ((acknowledgement.disposition == AlertAckDisposition::accepted &&
         acknowledgement.reason != AlertAckReason::none) ||
        (acknowledgement.disposition == AlertAckDisposition::rejected &&
         acknowledgement.reason == AlertAckReason::none)) {
        return AlertAckCodecError::inconsistent_disposition;
    }
    return acknowledgement.observed_alert_age_ms > kMaximumObservedAgeMs
               ? AlertAckCodecError::age_out_of_range
               : AlertAckCodecError::none;
}

AlertAckEncodeResult encode_critical_alert_ack(
    const CriticalAlertAck& acknowledgement,
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes>& output) {
    const auto validation = validate_critical_alert_ack(acknowledgement);
    if (validation != AlertAckCodecError::none) {
        return {validation, 0};
    }
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kCriticalAlertAckSchemaVersion;
    candidate[5] = static_cast<std::uint8_t>(kCriticalAlertAckFrameBytes);
    candidate[6] = static_cast<std::uint8_t>(acknowledgement.disposition);
    candidate[7] = static_cast<std::uint8_t>(acknowledgement.reason);
    candidate[8] = static_cast<std::uint8_t>(acknowledgement.state);
    write_u64(candidate.data() + 12, acknowledgement.consumer_id);
    write_u64(candidate.data() + 20, acknowledgement.producer_id);
    write_u64(candidate.data() + 28, acknowledgement.event_id);
    write_u64(candidate.data() + 36, acknowledgement.condition_id);
    write_u32(candidate.data() + 44,
              acknowledgement.consumer_boot_session_id);
    write_u32(candidate.data() + 48, acknowledgement.ack_sequence);
    write_u32(candidate.data() + 52,
              acknowledgement.observed_alert_age_ms);
    write_u32(candidate.data() + kChecksumOffset,
              crc32(candidate.data(), kChecksumOffset));
    output = candidate;
    return {AlertAckCodecError::none, output.size()};
}

AlertAckDecodeResult decode_critical_alert_ack(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr || size != kCriticalAlertAckFrameBytes) {
        return {AlertAckCodecError::invalid_argument};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data) ||
        data[5] != kCriticalAlertAckFrameBytes) {
        return {AlertAckCodecError::malformed};
    }
    if (data[4] != kCriticalAlertAckSchemaVersion) {
        return {AlertAckCodecError::unsupported_version};
    }
    if (data[9] != 0 || data[10] != 0 || data[11] != 0 ||
        data[56] != 0 || data[57] != 0 || data[58] != 0 || data[59] != 0) {
        return {AlertAckCodecError::noncanonical};
    }
    if (read_u32(data + kChecksumOffset) != crc32(data, kChecksumOffset)) {
        return {AlertAckCodecError::integrity_failure};
    }
    CriticalAlertAck acknowledgement{};
    acknowledgement.disposition = static_cast<AlertAckDisposition>(data[6]);
    acknowledgement.reason = static_cast<AlertAckReason>(data[7]);
    acknowledgement.state = static_cast<AlertState>(data[8]);
    acknowledgement.consumer_id = read_u64(data + 12);
    acknowledgement.producer_id = read_u64(data + 20);
    acknowledgement.event_id = read_u64(data + 28);
    acknowledgement.condition_id = read_u64(data + 36);
    acknowledgement.consumer_boot_session_id = read_u32(data + 44);
    acknowledgement.ack_sequence = read_u32(data + 48);
    acknowledgement.observed_alert_age_ms = read_u32(data + 52);
    const auto validation = validate_critical_alert_ack(acknowledgement);
    return validation == AlertAckCodecError::none
               ? AlertAckDecodeResult{AlertAckCodecError::none, acknowledgement}
               : AlertAckDecodeResult{validation};
}

}  // namespace opentrail::integration
