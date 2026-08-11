#include "opentrail/update_checkpoint.hpp"

#include <algorithm>
#include <array>

namespace opentrail::update {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'U', '0'}};
constexpr std::size_t kCrcOffset = kUpdateCheckpointRecordBytes - 4;

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

bool known_slot(ImageSlot slot) {
    return slot == ImageSlot::slot_a || slot == ImageSlot::slot_b;
}

bool known_reason(RollbackReason reason) {
    return reason == RollbackReason::boot_mismatch ||
           reason == RollbackReason::confirmation_timeout ||
           reason == RollbackReason::boot_attempt_limit ||
           reason == RollbackReason::explicit_health_failure;
}

bool persistent_state(UpdateState state) {
    return state == UpdateState::pending_reboot ||
           state == UpdateState::trial ||
           state == UpdateState::confirmed ||
           state == UpdateState::rollback_required ||
           state == UpdateState::rolled_back;
}

bool valid_health_mask(std::uint32_t mask) {
    return mask != 0 && (mask & ~kAllTrialHealthBits) == 0;
}

}  // namespace

UpdateCheckpointCodecError validate_update_checkpoint(
    const UpdateGuardCheckpoint& checkpoint) {
    if (checkpoint.version != kUpdateCheckpointVersion ||
        !persistent_state(checkpoint.state) ||
        !known_slot(checkpoint.baseline_slot) ||
        !known_slot(checkpoint.candidate_slot) ||
        checkpoint.baseline_slot == checkpoint.candidate_slot ||
        checkpoint.hardware_id == 0 ||
        checkpoint.baseline_version == 0 ||
        checkpoint.candidate_version <= checkpoint.baseline_version ||
        checkpoint.image_bytes == 0 ||
        !valid_health_mask(checkpoint.required_health_mask) ||
        checkpoint.minimum_stable_ms == 0 ||
        checkpoint.confirmation_deadline_ms <=
            checkpoint.minimum_stable_ms ||
        checkpoint.maximum_trial_boots == 0 ||
        checkpoint.trial_boots > checkpoint.maximum_trial_boots ||
        checkpoint.maximum_image_bytes == 0 ||
        checkpoint.image_bytes > checkpoint.maximum_image_bytes ||
        checkpoint.generation == 0) {
        return UpdateCheckpointCodecError::invalid_checkpoint;
    }

    const bool has_reason = known_reason(checkpoint.rollback_reason);
    if (checkpoint.state == UpdateState::pending_reboot &&
        (checkpoint.trial_boots != 0 ||
         checkpoint.rollback_reason != RollbackReason::none)) {
        return UpdateCheckpointCodecError::invalid_checkpoint;
    }
    if ((checkpoint.state == UpdateState::trial ||
         checkpoint.state == UpdateState::confirmed) &&
        (checkpoint.trial_boots == 0 ||
         checkpoint.rollback_reason != RollbackReason::none)) {
        return UpdateCheckpointCodecError::invalid_checkpoint;
    }
    if ((checkpoint.state == UpdateState::rollback_required ||
         checkpoint.state == UpdateState::rolled_back) && !has_reason) {
        return UpdateCheckpointCodecError::invalid_checkpoint;
    }
    return UpdateCheckpointCodecError::none;
}

UpdateCheckpointCodecResult encode_update_checkpoint(
    const UpdateGuardCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity < kUpdateCheckpointRecordBytes) {
        return {UpdateCheckpointCodecError::invalid_argument, 0};
    }
    const auto validation = validate_update_checkpoint(checkpoint);
    if (validation != UpdateCheckpointCodecError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = checkpoint.version;
    candidate[5] = static_cast<std::uint8_t>(checkpoint.state);
    candidate[6] = static_cast<std::uint8_t>(checkpoint.rollback_reason);
    candidate[7] = static_cast<std::uint8_t>(checkpoint.baseline_slot);
    candidate[8] = static_cast<std::uint8_t>(checkpoint.candidate_slot);
    candidate[9] = checkpoint.trial_boots;
    candidate[10] = checkpoint.maximum_trial_boots;
    write_u32(candidate.data() + 12, checkpoint.hardware_id);
    write_u32(candidate.data() + 16, checkpoint.baseline_version);
    write_u32(candidate.data() + 20, checkpoint.candidate_version);
    write_u32(candidate.data() + 24, checkpoint.image_bytes);
    write_u32(candidate.data() + 28, checkpoint.required_health_mask);
    write_u64(candidate.data() + 32, checkpoint.minimum_stable_ms);
    write_u64(candidate.data() + 40, checkpoint.confirmation_deadline_ms);
    write_u32(candidate.data() + 48, checkpoint.maximum_image_bytes);
    write_u64(candidate.data() + 52, checkpoint.generation);
    write_u32(candidate.data() + kCrcOffset,
              crc32(candidate.data(), kCrcOffset));
    std::copy(candidate.begin(), candidate.end(), output);
    return {UpdateCheckpointCodecError::none, candidate.size()};
}

UpdateCheckpointCodecResult decode_update_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    UpdateGuardCheckpoint& output) {
    if (data == nullptr || size != kUpdateCheckpointRecordBytes) {
        return {UpdateCheckpointCodecError::invalid_argument, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) {
        return {UpdateCheckpointCodecError::bad_magic, 0};
    }
    if (data[4] != kUpdateCheckpointVersion) {
        return {UpdateCheckpointCodecError::unsupported_version, 0};
    }
    if (data[11] != 0) {
        return {UpdateCheckpointCodecError::noncanonical_record, 0};
    }
    if (read_u32(data + kCrcOffset) != crc32(data, kCrcOffset)) {
        return {UpdateCheckpointCodecError::integrity_failure, 0};
    }

    UpdateGuardCheckpoint candidate{};
    candidate.version = data[4];
    candidate.state = static_cast<UpdateState>(data[5]);
    candidate.rollback_reason = static_cast<RollbackReason>(data[6]);
    candidate.baseline_slot = static_cast<ImageSlot>(data[7]);
    candidate.candidate_slot = static_cast<ImageSlot>(data[8]);
    candidate.trial_boots = data[9];
    candidate.maximum_trial_boots = data[10];
    candidate.hardware_id = read_u32(data + 12);
    candidate.baseline_version = read_u32(data + 16);
    candidate.candidate_version = read_u32(data + 20);
    candidate.image_bytes = read_u32(data + 24);
    candidate.required_health_mask = read_u32(data + 28);
    candidate.minimum_stable_ms = read_u64(data + 32);
    candidate.confirmation_deadline_ms = read_u64(data + 40);
    candidate.maximum_image_bytes = read_u32(data + 48);
    candidate.generation = read_u64(data + 52);

    const auto validation = validate_update_checkpoint(candidate);
    if (validation != UpdateCheckpointCodecError::none) {
        return {validation, 0};
    }
    output = candidate;
    return {UpdateCheckpointCodecError::none, size};
}

}  // namespace opentrail::update
