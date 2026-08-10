#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/location_tracker.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::location {

inline constexpr std::uint8_t kPositionPayloadVersion = 0;
inline constexpr std::size_t kPositionPayloadBytes = 16;

enum class BroadcastPositionState : std::uint8_t {
    unavailable = 0,
    current = 1,
    stale = 2,
    invalid = 3,
};

enum class PositionCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unsupported_state,
    reserved_bits_set,
    invalid_coordinate,
    invalid_accuracy,
    inconsistent_fields,
};

struct PositionBroadcast {
    BroadcastPositionState state{BroadcastPositionState::unavailable};
    std::int32_t latitude_e7{0};
    std::int32_t longitude_e7{0};
    std::uint16_t age_seconds{0};
    std::uint16_t horizontal_accuracy_m{0};
    bool age_saturated{false};
    bool horizontal_accuracy_valid{false};
    bool horizontal_accuracy_saturated{false};

    [[nodiscard]] constexpr bool has_coordinates() const {
        return state == BroadcastPositionState::current ||
               state == BroadcastPositionState::stale;
    }
};

struct PositionEncodeResult {
    PositionCodecError error{PositionCodecError::none};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == PositionCodecError::none;
    }
};

struct PositionDecodeResult {
    PositionCodecError error{PositionCodecError::malformed};
    PositionBroadcast position{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == PositionCodecError::none;
    }
};

[[nodiscard]] PositionEncodeResult encode_position(
    const LocationSnapshot& snapshot,
    radio::MutableByteView output);
[[nodiscard]] PositionDecodeResult decode_position(radio::ByteView encoded);

}  // namespace opentrail::location
