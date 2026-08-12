#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::protocol {

inline constexpr std::uint8_t kQuickStatusPayloadVersion = 0;
inline constexpr std::size_t kQuickStatusPayloadBytes = 12;

// These values are stable wire semantics. A renderer chooses localized labels.
enum class QuickStatusKind : std::uint8_t {
    ok = 1,
    need_assistance = 2,
    anyone_online = 3,
    available_to_help = 4,
};

enum class QuickStatusCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unknown_status,
    reserved_bits_set,
    integrity_failure,
};

struct QuickStatusPayload {
    QuickStatusKind kind{QuickStatusKind::ok};
};

struct QuickStatusEncodeResult {
    QuickStatusCodecError error{QuickStatusCodecError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == QuickStatusCodecError::none;
    }
};

struct QuickStatusDecodeResult {
    QuickStatusCodecError error{QuickStatusCodecError::malformed};
    QuickStatusPayload payload{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == QuickStatusCodecError::none;
    }
};

[[nodiscard]] std::uint32_t quick_status_crc32(
    const std::uint8_t* data,
    std::size_t size);
[[nodiscard]] QuickStatusEncodeResult encode_quick_status(
    const QuickStatusPayload& payload,
    radio::MutableByteView output);
[[nodiscard]] QuickStatusDecodeResult decode_quick_status(
    radio::ByteView encoded);

}  // namespace opentrail::protocol
