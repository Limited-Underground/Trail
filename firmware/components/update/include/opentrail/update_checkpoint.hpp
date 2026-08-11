#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/update_boot_guard.hpp"

namespace opentrail::update {

inline constexpr std::uint8_t kUpdateCheckpointVersion = 0;
inline constexpr std::size_t kUpdateCheckpointRecordBytes = 64;

struct UpdateGuardCheckpoint {
    std::uint8_t version{kUpdateCheckpointVersion};
    UpdateState state{UpdateState::idle};
    RollbackReason rollback_reason{RollbackReason::none};
    ImageSlot baseline_slot{ImageSlot::slot_a};
    ImageSlot candidate_slot{ImageSlot::slot_b};
    std::uint8_t trial_boots{0};
    std::uint8_t maximum_trial_boots{0};
    std::uint32_t hardware_id{0};
    std::uint32_t baseline_version{0};
    std::uint32_t candidate_version{0};
    std::uint32_t image_bytes{0};
    std::uint32_t required_health_mask{0};
    std::uint64_t minimum_stable_ms{0};
    std::uint64_t confirmation_deadline_ms{0};
    std::uint32_t maximum_image_bytes{0};
    std::uint64_t generation{0};
};

enum class UpdateCheckpointCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_checkpoint,
    bad_magic,
    unsupported_version,
    noncanonical_record,
    integrity_failure,
};

struct UpdateCheckpointCodecResult {
    UpdateCheckpointCodecError error{
        UpdateCheckpointCodecError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == UpdateCheckpointCodecError::none;
    }
};

[[nodiscard]] UpdateCheckpointCodecError validate_update_checkpoint(
    const UpdateGuardCheckpoint& checkpoint);
[[nodiscard]] UpdateCheckpointCodecResult encode_update_checkpoint(
    const UpdateGuardCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity);
[[nodiscard]] UpdateCheckpointCodecResult decode_update_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    UpdateGuardCheckpoint& output);

}  // namespace opentrail::update
