#include "opentrail/map_selector_checkpoint.hpp"

#include <algorithm>
#include <array>

namespace opentrail::maps {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'M', '0'}};
constexpr std::size_t kCrcOffset = kMapSelectorCheckpointBytes - 4;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

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

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(input[0]) |
        (static_cast<std::uint16_t>(input[1]) << 8U));
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) <<
                 (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) <<
                 (index * 8U);
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

bool known_slot(MapSlot slot) {
    return slot == MapSlot::slot_a || slot == MapSlot::slot_b;
}

bool persistent_state(MapActivationState state) {
    return state == MapActivationState::active ||
           state == MapActivationState::trial ||
           state == MapActivationState::fallback_required;
}

bool fallback_reason(MapActivationReason reason) {
    return reason == MapActivationReason::trial_read_failed ||
           reason == MapActivationReason::trial_deadline_reached ||
           reason == MapActivationReason::clock_regression ||
           reason == MapActivationReason::active_media_removed ||
           reason == MapActivationReason::trial_boot_limit_reached;
}

}  // namespace

MapSelectorCheckpointError validate_map_selector_checkpoint(
    const MapSelectorCheckpoint& checkpoint) {
    if (checkpoint.version != kMapSelectorCheckpointVersion ||
        !persistent_state(checkpoint.state) ||
        !known_slot(checkpoint.active_slot) ||
        checkpoint.maximum_trial_boots == 0 ||
        checkpoint.required_healthy_reads == 0 ||
        checkpoint.active_generation == 0 ||
        checkpoint.trial_deadline_ms == 0 ||
        checkpoint.maximum_package_bytes == 0 ||
        checkpoint.record_generation == 0) {
        return MapSelectorCheckpointError::invalid_checkpoint;
    }

    const bool has_previous = checkpoint.previous_slot != MapSlot::none;
    if (has_previous) {
        if (!known_slot(checkpoint.previous_slot) ||
            checkpoint.previous_slot == checkpoint.active_slot ||
            checkpoint.previous_generation == 0) {
            return MapSelectorCheckpointError::invalid_checkpoint;
        }
    } else if (checkpoint.previous_generation != 0) {
        return MapSelectorCheckpointError::invalid_checkpoint;
    }

    if (checkpoint.state == MapActivationState::active) {
        if (checkpoint.reason != MapActivationReason::none ||
            checkpoint.trial_boots != 0) {
            return MapSelectorCheckpointError::invalid_checkpoint;
        }
    } else if (checkpoint.state == MapActivationState::trial) {
        if (!has_previous || checkpoint.reason != MapActivationReason::none ||
            checkpoint.trial_boots == 0 ||
            checkpoint.trial_boots > checkpoint.maximum_trial_boots) {
            return MapSelectorCheckpointError::invalid_checkpoint;
        }
    } else if (!has_previous || !fallback_reason(checkpoint.reason) ||
               checkpoint.trial_boots == 0 ||
               checkpoint.trial_boots > checkpoint.maximum_trial_boots) {
        return MapSelectorCheckpointError::invalid_checkpoint;
    }
    return MapSelectorCheckpointError::none;
}

MapSelectorCheckpointResult encode_map_selector_checkpoint(
    const MapSelectorCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity < kMapSelectorCheckpointBytes) {
        return {MapSelectorCheckpointError::invalid_argument, 0};
    }
    const auto validation = validate_map_selector_checkpoint(checkpoint);
    if (validation != MapSelectorCheckpointError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kMapSelectorCheckpointBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = checkpoint.version;
    candidate[5] = static_cast<std::uint8_t>(checkpoint.state);
    candidate[6] = static_cast<std::uint8_t>(checkpoint.reason);
    candidate[7] = static_cast<std::uint8_t>(checkpoint.active_slot);
    candidate[8] = static_cast<std::uint8_t>(checkpoint.previous_slot);
    candidate[9] = checkpoint.trial_boots;
    write_u16(candidate.data() + 10, checkpoint.required_healthy_reads);
    write_u64(candidate.data() + 12, checkpoint.active_generation);
    write_u64(candidate.data() + 20, checkpoint.previous_generation);
    write_u64(candidate.data() + 28, checkpoint.trial_deadline_ms);
    write_u64(candidate.data() + 36, checkpoint.maximum_package_bytes);
    write_u64(candidate.data() + 44, checkpoint.record_generation);
    candidate[52] = checkpoint.maximum_trial_boots;
    candidate[kMapSelectorCommitOffset] = kMapSelectorCommitMarker;
    write_u32(candidate.data() + kCrcOffset,
              crc32(candidate.data(), kCrcOffset));
    std::copy(candidate.begin(), candidate.end(), output);
    return {MapSelectorCheckpointError::none, candidate.size()};
}

MapSelectorCheckpointResult decode_map_selector_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    MapSelectorCheckpoint& output) {
    if (data == nullptr || size != kMapSelectorCheckpointBytes) {
        return {MapSelectorCheckpointError::invalid_argument, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) {
        return {MapSelectorCheckpointError::bad_magic, 0};
    }
    if (data[4] != kMapSelectorCheckpointVersion) {
        return {MapSelectorCheckpointError::unsupported_version, 0};
    }
    if (!std::all_of(data + 53, data + kMapSelectorCommitOffset,
                     [](std::uint8_t value) { return value == 0; })) {
        return {MapSelectorCheckpointError::noncanonical_record, 0};
    }
    if (data[kMapSelectorCommitOffset] != kMapSelectorCommitMarker) {
        return {MapSelectorCheckpointError::uncommitted_record, 0};
    }
    if (read_u32(data + kCrcOffset) != crc32(data, kCrcOffset)) {
        return {MapSelectorCheckpointError::integrity_failure, 0};
    }

    MapSelectorCheckpoint candidate{};
    candidate.version = data[4];
    candidate.state = static_cast<MapActivationState>(data[5]);
    candidate.reason = static_cast<MapActivationReason>(data[6]);
    candidate.active_slot = static_cast<MapSlot>(data[7]);
    candidate.previous_slot = static_cast<MapSlot>(data[8]);
    candidate.trial_boots = data[9];
    candidate.maximum_trial_boots = data[52];
    candidate.required_healthy_reads = read_u16(data + 10);
    candidate.active_generation = read_u64(data + 12);
    candidate.previous_generation = read_u64(data + 20);
    candidate.trial_deadline_ms = read_u64(data + 28);
    candidate.maximum_package_bytes = read_u64(data + 36);
    candidate.record_generation = read_u64(data + 44);

    const auto validation = validate_map_selector_checkpoint(candidate);
    if (validation != MapSelectorCheckpointError::none) {
        return {validation, 0};
    }
    output = candidate;
    return {MapSelectorCheckpointError::none, size};
}

}  // namespace opentrail::maps
