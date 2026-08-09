#include "opentrail/position_codec.hpp"

#include <limits>

namespace opentrail::location {
namespace {

constexpr std::uint8_t kAccuracyPresent = 0x01;
constexpr std::uint8_t kAgeSaturated = 0x02;
constexpr std::uint8_t kAccuracySaturated = 0x04;
constexpr std::uint8_t kKnownFlags =
    kAccuracyPresent | kAgeSaturated | kAccuracySaturated;
constexpr std::int32_t kMinimumLatitudeE7 = -900000000;
constexpr std::int32_t kMaximumLatitudeE7 = 900000000;
constexpr std::int32_t kMinimumLongitudeE7 = -1800000000;
constexpr std::int32_t kMaximumLongitudeE7 = 1800000000;

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1] << 8U);
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::int32_t read_i32_le(const std::uint8_t* source) {
    const auto value = read_u32_le(source);
    if (value <= static_cast<std::uint32_t>(
                     std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int32_t>(value);
    }
    const auto signed_value = static_cast<std::int64_t>(value) -
                              (static_cast<std::int64_t>(1) << 32U);
    return static_cast<std::int32_t>(signed_value);
}

bool valid_coordinate(std::int32_t latitude_e7, std::int32_t longitude_e7) {
    return latitude_e7 >= kMinimumLatitudeE7 &&
           latitude_e7 <= kMaximumLatitudeE7 &&
           longitude_e7 >= kMinimumLongitudeE7 &&
           longitude_e7 <= kMaximumLongitudeE7;
}

bool known_state(BroadcastPositionState state) {
    switch (state) {
        case BroadcastPositionState::unavailable:
        case BroadcastPositionState::current:
        case BroadcastPositionState::stale:
        case BroadcastPositionState::invalid:
            return true;
    }
    return false;
}

BroadcastPositionState broadcast_state(FixState state) {
    switch (state) {
        case FixState::unavailable:
            return BroadcastPositionState::unavailable;
        case FixState::valid:
            return BroadcastPositionState::current;
        case FixState::stale:
            return BroadcastPositionState::stale;
        case FixState::invalid:
            return BroadcastPositionState::invalid;
    }
    return static_cast<BroadcastPositionState>(0xFF);
}

}  // namespace

PositionEncodeResult encode_position(
    const LocationSnapshot& snapshot,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {PositionCodecError::invalid_argument, 0};
    }
    if (output.size < kPositionPayloadBytes) {
        return {PositionCodecError::output_too_small, kPositionPayloadBytes};
    }

    const auto state = broadcast_state(snapshot.state);
    if (!known_state(state)) {
        return {PositionCodecError::unsupported_state, 0};
    }

    const bool has_coordinates =
        state == BroadcastPositionState::current ||
        state == BroadcastPositionState::stale;
    if (has_coordinates && snapshot.error != FixError::none) {
        return {PositionCodecError::inconsistent_fields, 0};
    }
    if (has_coordinates &&
        !valid_coordinate(snapshot.fix.latitude_e7, snapshot.fix.longitude_e7)) {
        return {PositionCodecError::invalid_coordinate, 0};
    }
    if (has_coordinates && snapshot.fix.horizontal_accuracy_valid &&
        snapshot.fix.horizontal_accuracy_cm == 0) {
        return {PositionCodecError::invalid_accuracy, 0};
    }

    std::uint8_t flags = 0;
    std::uint16_t age_seconds = 0;
    std::uint16_t accuracy_m = 0;
    std::int32_t latitude_e7 = 0;
    std::int32_t longitude_e7 = 0;

    if (has_coordinates) {
        latitude_e7 = snapshot.fix.latitude_e7;
        longitude_e7 = snapshot.fix.longitude_e7;

        const auto rounded_age = snapshot.age_ms / 1000U +
                                 (snapshot.age_ms % 1000U == 0 ? 0U : 1U);
        if (rounded_age > std::numeric_limits<std::uint16_t>::max()) {
            age_seconds = std::numeric_limits<std::uint16_t>::max();
            flags |= kAgeSaturated;
        } else {
            age_seconds = static_cast<std::uint16_t>(rounded_age);
        }

        if (snapshot.fix.horizontal_accuracy_valid) {
            flags |= kAccuracyPresent;
            const auto rounded_accuracy =
                static_cast<std::uint64_t>(
                    snapshot.fix.horizontal_accuracy_cm) /
                    100U +
                (snapshot.fix.horizontal_accuracy_cm % 100U == 0 ? 0U : 1U);
            if (rounded_accuracy >
                std::numeric_limits<std::uint16_t>::max()) {
                accuracy_m = std::numeric_limits<std::uint16_t>::max();
                flags |= kAccuracySaturated;
            } else {
                accuracy_m = static_cast<std::uint16_t>(rounded_accuracy);
            }
        }
    }

    output.data[0] = kPositionPayloadVersion;
    output.data[1] = static_cast<std::uint8_t>(state);
    output.data[2] = flags;
    output.data[3] = 0;
    write_u16_le(output.data + 4, age_seconds);
    write_u32_le(output.data + 6, static_cast<std::uint32_t>(latitude_e7));
    write_u32_le(output.data + 10, static_cast<std::uint32_t>(longitude_e7));
    write_u16_le(output.data + 14, accuracy_m);
    return {PositionCodecError::none, kPositionPayloadBytes};
}

PositionDecodeResult decode_position(radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {PositionCodecError::invalid_argument, {}};
    }
    if (encoded.size != kPositionPayloadBytes) {
        return {PositionCodecError::malformed, {}};
    }
    if (encoded.data[0] != kPositionPayloadVersion) {
        return {PositionCodecError::unsupported_version, {}};
    }

    const auto state = static_cast<BroadcastPositionState>(encoded.data[1]);
    if (!known_state(state)) {
        return {PositionCodecError::unsupported_state, {}};
    }
    const auto flags = encoded.data[2];
    if ((flags & static_cast<std::uint8_t>(~kKnownFlags)) != 0 ||
        encoded.data[3] != 0) {
        return {PositionCodecError::reserved_bits_set, {}};
    }

    PositionBroadcast result{};
    result.state = state;
    result.age_saturated = (flags & kAgeSaturated) != 0;
    result.horizontal_accuracy_valid = (flags & kAccuracyPresent) != 0;
    result.horizontal_accuracy_saturated =
        (flags & kAccuracySaturated) != 0;
    result.age_seconds = read_u16_le(encoded.data + 4);
    result.latitude_e7 = read_i32_le(encoded.data + 6);
    result.longitude_e7 = read_i32_le(encoded.data + 10);
    result.horizontal_accuracy_m = read_u16_le(encoded.data + 14);

    if (!result.has_coordinates()) {
        if (flags != 0 || result.age_seconds != 0 ||
            result.latitude_e7 != 0 || result.longitude_e7 != 0 ||
            result.horizontal_accuracy_m != 0) {
            return {PositionCodecError::inconsistent_fields, {}};
        }
        return {PositionCodecError::none, result};
    }

    if (!valid_coordinate(result.latitude_e7, result.longitude_e7)) {
        return {PositionCodecError::invalid_coordinate, {}};
    }
    if (result.age_saturated &&
        result.age_seconds != std::numeric_limits<std::uint16_t>::max()) {
        return {PositionCodecError::inconsistent_fields, {}};
    }
    if (!result.horizontal_accuracy_valid &&
        (result.horizontal_accuracy_m != 0 ||
         result.horizontal_accuracy_saturated)) {
        return {PositionCodecError::inconsistent_fields, {}};
    }
    if (result.horizontal_accuracy_valid &&
        result.horizontal_accuracy_m == 0) {
        return {PositionCodecError::invalid_accuracy, {}};
    }
    if (result.horizontal_accuracy_saturated &&
        result.horizontal_accuracy_m !=
            std::numeric_limits<std::uint16_t>::max()) {
        return {PositionCodecError::inconsistent_fields, {}};
    }
    return {PositionCodecError::none, result};
}

}  // namespace opentrail::location
