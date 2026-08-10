#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_window.hpp"

namespace opentrail::delivery {

inline constexpr std::uint8_t kDuplicateCheckpointEnvelopeVersion = 0;
inline constexpr std::size_t kDuplicateCheckpointRecordBytes = 672;

enum class DuplicateCheckpointCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_checkpoint,
    duplicate_key,
    bad_magic,
    unsupported_version,
    noncanonical_record,
    integrity_failure,
};

struct DuplicateCheckpointCodecResult {
    DuplicateCheckpointCodecError error{
        DuplicateCheckpointCodecError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == DuplicateCheckpointCodecError::none;
    }
};

[[nodiscard]] DuplicateCheckpointCodecError validate_duplicate_checkpoint(
    const DuplicateCheckpoint& checkpoint);
[[nodiscard]] DuplicateCheckpointCodecResult encode_duplicate_checkpoint(
    const DuplicateCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity);
[[nodiscard]] DuplicateCheckpointCodecResult decode_duplicate_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    DuplicateCheckpoint& output);

}  // namespace opentrail::delivery
