#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/map_activation_guard.hpp"

namespace opentrail::maps {

inline constexpr std::uint8_t kMapSelectorCheckpointVersion = 0;
inline constexpr std::size_t kMapSelectorCheckpointBytes = 64;

// OTM0 contains no path, package name, geographic content, identity, key, URL,
// credential, timestamp, or free text. CRC detects accidental corruption; it
// does not authenticate the record or prevent rollback.
struct MapSelectorCheckpoint {
    std::uint8_t version{kMapSelectorCheckpointVersion};
    MapActivationState state{MapActivationState::active};
    MapActivationReason reason{MapActivationReason::none};
    MapSlot active_slot{MapSlot::slot_a};
    MapSlot previous_slot{MapSlot::none};
    std::uint8_t trial_boots{0};
    std::uint8_t maximum_trial_boots{0};
    std::uint16_t required_healthy_reads{0};
    std::uint64_t active_generation{0};
    std::uint64_t previous_generation{0};
    std::uint64_t trial_deadline_ms{0};
    std::uint64_t maximum_package_bytes{0};
    std::uint64_t record_generation{0};
};

enum class MapSelectorCheckpointError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_checkpoint,
    bad_magic,
    unsupported_version,
    noncanonical_record,
    integrity_failure,
};

struct MapSelectorCheckpointResult {
    MapSelectorCheckpointError error{
        MapSelectorCheckpointError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == MapSelectorCheckpointError::none;
    }
};

[[nodiscard]] MapSelectorCheckpointError validate_map_selector_checkpoint(
    const MapSelectorCheckpoint& checkpoint);
[[nodiscard]] MapSelectorCheckpointResult encode_map_selector_checkpoint(
    const MapSelectorCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity);
[[nodiscard]] MapSelectorCheckpointResult decode_map_selector_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    MapSelectorCheckpoint& output);

}  // namespace opentrail::maps
