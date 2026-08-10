#include "opentrail/duplicate_checkpoint_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opentrail::delivery {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'D', 'S', '0'}};
constexpr std::size_t kHeaderBytes = 24;
constexpr std::size_t kGroupContextOffset = 16;
constexpr std::size_t kCheckpointOffset = kHeaderBytes;
constexpr std::size_t kGroupEpochOffset =
    kCheckpointOffset + kDuplicateCheckpointRecordBytes;
constexpr std::size_t kCrcOffset = kStoredDuplicateCheckpointBytes - 4;

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
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
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

bool all_zero(const std::uint8_t* data, std::size_t size) {
    return std::all_of(
        data, data + size, [](std::uint8_t byte) { return byte == 0; });
}

struct StoredCheckpoint {
    std::uint64_t generation{0};
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    DuplicateCheckpoint checkpoint{};
    std::array<std::uint8_t, kDuplicateCheckpointRecordBytes> checkpoint_bytes{};
};

enum class DecodedRecordKind : std::uint8_t {
    invalid = 0,
    current,
    legacy_unbound,
};

bool checkpoint_matches_epoch(
    const DuplicateCheckpoint& checkpoint,
    std::uint32_t group_epoch) {
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        if (checkpoint.entries[index].key.group_epoch != group_epoch) {
            return false;
        }
    }
    return true;
}

struct InspectedSlot {
    DuplicateCheckpointSlotState state{DuplicateCheckpointSlotState::empty};
    DuplicateCheckpointCodecError codec_error{
        DuplicateCheckpointCodecError::none};
    StoredCheckpoint value{};
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> bytes{};
};

bool encode_record(
    const StoredCheckpoint& value,
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& output,
    DuplicateCheckpointCodecError& codec_error) {
    if (value.generation == 0 || value.group_context_id == 0 ||
        value.group_epoch == 0 ||
        !checkpoint_matches_epoch(value.checkpoint, value.group_epoch)) {
        return false;
    }
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kStoredDuplicateCheckpointVersion;
    candidate[5] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u16(candidate.data() + 6,
              static_cast<std::uint16_t>(kDuplicateCheckpointRecordBytes));
    write_u64(candidate.data() + 8, value.generation);
    write_u64(candidate.data() + kGroupContextOffset,
              value.group_context_id);
    const auto encoded = encode_duplicate_checkpoint(
        value.checkpoint,
        candidate.data() + kCheckpointOffset,
        kDuplicateCheckpointRecordBytes);
    codec_error = encoded.error;
    if (!encoded.succeeded()) {
        return false;
    }
    write_u32(candidate.data() + kGroupEpochOffset, value.group_epoch);
    write_u32(candidate.data() + kCrcOffset,
              crc32(candidate.data(), kCrcOffset));
    output = candidate;
    return true;
}

DecodedRecordKind decode_record(
    const std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& input,
    StoredCheckpoint& output,
    DuplicateCheckpointCodecError& codec_error) {
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
        (input[4] != 0 && input[4] != kStoredDuplicateCheckpointVersion) ||
        input[5] != kHeaderBytes ||
        read_u16(input.data() + 6) != kDuplicateCheckpointRecordBytes ||
        read_u64(input.data() + 8) == 0 ||
        read_u32(input.data() + kCrcOffset) !=
            crc32(input.data(), kCrcOffset)) {
        return DecodedRecordKind::invalid;
    }

    StoredCheckpoint candidate{};
    candidate.generation = read_u64(input.data() + 8);
    const auto kind = input[4] == 0
                          ? DecodedRecordKind::legacy_unbound
                          : DecodedRecordKind::current;
    if (kind == DecodedRecordKind::legacy_unbound) {
        if (!all_zero(input.data() + kGroupContextOffset, 8) ||
            read_u32(input.data() + kGroupEpochOffset) != 0) {
            return DecodedRecordKind::invalid;
        }
    } else {
        candidate.group_context_id =
            read_u64(input.data() + kGroupContextOffset);
        candidate.group_epoch = read_u32(input.data() + kGroupEpochOffset);
        if (candidate.group_context_id == 0 || candidate.group_epoch == 0) {
            return DecodedRecordKind::invalid;
        }
    }
    std::copy(input.begin() + kCheckpointOffset,
              input.begin() + kGroupEpochOffset,
              candidate.checkpoint_bytes.begin());
    const auto decoded = decode_duplicate_checkpoint(
        candidate.checkpoint_bytes.data(),
        candidate.checkpoint_bytes.size(),
        candidate.checkpoint);
    codec_error = decoded.error;
    if (!decoded.succeeded()) {
        return DecodedRecordKind::invalid;
    }
    if (kind == DecodedRecordKind::current &&
        !checkpoint_matches_epoch(
            candidate.checkpoint, candidate.group_epoch)) {
        return DecodedRecordKind::invalid;
    }
    output = candidate;
    return kind;
}

InspectedSlot inspect_slot(
    DuplicateCheckpointStorage& storage,
    std::uint8_t slot,
    std::uint64_t expected_group_context_id,
    std::uint32_t expected_group_epoch) {
    InspectedSlot result{};
    const auto read = storage.read_slot(
        slot, result.bytes.data(), result.bytes.size());
    if (read == DuplicateCheckpointStorageError::not_found) {
        return result;
    }
    if (read != DuplicateCheckpointStorageError::none) {
        result.state = DuplicateCheckpointSlotState::io_failure;
        return result;
    }
    const auto decoded = decode_record(
        result.bytes, result.value, result.codec_error);
    if (decoded == DecodedRecordKind::invalid) {
        result.state = DuplicateCheckpointSlotState::invalid;
    } else if (decoded == DecodedRecordKind::legacy_unbound) {
        result.state = DuplicateCheckpointSlotState::legacy_unbound;
    } else if (result.value.group_context_id != expected_group_context_id ||
               result.value.group_epoch != expected_group_epoch) {
        result.state = DuplicateCheckpointSlotState::binding_mismatch;
    } else {
        result.state = DuplicateCheckpointSlotState::valid;
    }
    return result;
}

DuplicateCheckpointSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? DuplicateCheckpointSource::slot_a
                     : DuplicateCheckpointSource::slot_b;
}

bool generation_conflict(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.state == DuplicateCheckpointSlotState::valid &&
           b.state == DuplicateCheckpointSlotState::valid &&
           a.value.generation == b.value.generation && a.bytes != b.bytes;
}

const InspectedSlot* newest_valid(
    const InspectedSlot& a,
    const InspectedSlot& b,
    std::uint8_t& selected_slot) {
    if (a.state == DuplicateCheckpointSlotState::valid &&
        b.state == DuplicateCheckpointSlotState::valid) {
        if (b.value.generation > a.value.generation) {
            selected_slot = 1;
            return &b;
        }
        return &a;
    }
    if (a.state == DuplicateCheckpointSlotState::valid) {
        return &a;
    }
    if (b.state == DuplicateCheckpointSlotState::valid) {
        selected_slot = 1;
        return &b;
    }
    return nullptr;
}

}  // namespace

DuplicateCheckpointStore::DuplicateCheckpointStore(
    DuplicateCheckpointStorage& storage,
    std::uint64_t group_context_id,
    std::uint32_t group_epoch)
    : storage_(storage),
      group_context_id_(group_context_id),
      group_epoch_(group_epoch) {}

DuplicateCheckpointLoadResult DuplicateCheckpointStore::restore(
    DuplicateWindow& window,
    std::uint64_t now_ms) {
    DuplicateCheckpointLoadResult result{};
    if (group_context_id_ == 0 || group_epoch_ == 0) {
        result.error = DuplicateCheckpointStoreError::invalid_binding;
        return result;
    }
    const auto a = inspect_slot(
        storage_, 0, group_context_id_, group_epoch_);
    const auto b = inspect_slot(
        storage_, 1, group_context_id_, group_epoch_);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == DuplicateCheckpointSlotState::io_failure ||
        b.state == DuplicateCheckpointSlotState::io_failure) {
        result.error = DuplicateCheckpointStoreError::storage_failure;
        result.recovery_required = true;
        return result;
    }
    if (a.state == DuplicateCheckpointSlotState::legacy_unbound ||
        b.state == DuplicateCheckpointSlotState::legacy_unbound) {
        result.error = DuplicateCheckpointStoreError::legacy_unbound;
        result.recovery_required = true;
        return result;
    }
    if (a.state == DuplicateCheckpointSlotState::binding_mismatch ||
        b.state == DuplicateCheckpointSlotState::binding_mismatch) {
        result.error = DuplicateCheckpointStoreError::binding_mismatch;
        result.recovery_required = true;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = DuplicateCheckpointStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* selected = newest_valid(a, b, selected_slot);
    if (selected == nullptr) {
        const bool any_invalid =
            a.state == DuplicateCheckpointSlotState::invalid ||
            b.state == DuplicateCheckpointSlotState::invalid;
        result.error = any_invalid
                           ? DuplicateCheckpointStoreError::invalid_state
                           : DuplicateCheckpointStoreError::no_checkpoint;
        result.codec_error = a.codec_error != DuplicateCheckpointCodecError::none
                                 ? a.codec_error
                                 : b.codec_error;
        result.recovery_required = any_invalid;
        return result;
    }

    result.source = source_for_slot(selected_slot);
    result.generation = selected->value.generation;
    result.recovery_required =
        a.state != DuplicateCheckpointSlotState::valid ||
        b.state != DuplicateCheckpointSlotState::valid;
    result.duplicate_error = window.restore(
        selected->value.checkpoint, now_ms);
    if (result.duplicate_error != DuplicateError::none) {
        result.error = DuplicateCheckpointStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.error = DuplicateCheckpointStoreError::none;
    result.restored = true;
    return result;
}

DuplicateCheckpointSaveResult DuplicateCheckpointStore::save(
    const DuplicateWindow& window,
    std::uint64_t now_ms) {
    if (group_context_id_ == 0 || group_epoch_ == 0) {
        return {DuplicateCheckpointStoreError::invalid_binding};
    }
    const auto a = inspect_slot(
        storage_, 0, group_context_id_, group_epoch_);
    const auto b = inspect_slot(
        storage_, 1, group_context_id_, group_epoch_);
    if (a.state == DuplicateCheckpointSlotState::io_failure ||
        b.state == DuplicateCheckpointSlotState::io_failure) {
        return {DuplicateCheckpointStoreError::storage_failure};
    }
    if (a.state == DuplicateCheckpointSlotState::legacy_unbound ||
        b.state == DuplicateCheckpointSlotState::legacy_unbound) {
        return {DuplicateCheckpointStoreError::legacy_unbound};
    }
    if (a.state == DuplicateCheckpointSlotState::binding_mismatch ||
        b.state == DuplicateCheckpointSlotState::binding_mismatch) {
        return {DuplicateCheckpointStoreError::binding_mismatch};
    }
    if (generation_conflict(a, b)) {
        return {DuplicateCheckpointStoreError::generation_conflict};
    }

    std::uint8_t current_slot = 0;
    const auto* current = newest_valid(a, b, current_slot);
    const bool any_invalid =
        a.state == DuplicateCheckpointSlotState::invalid ||
        b.state == DuplicateCheckpointSlotState::invalid;
    if (current == nullptr && any_invalid) {
        return {DuplicateCheckpointStoreError::invalid_state};
    }

    StoredCheckpoint next{};
    next.checkpoint = window.checkpoint(now_ms);
    next.group_context_id = group_context_id_;
    next.group_epoch = group_epoch_;
    if (!checkpoint_matches_epoch(next.checkpoint, group_epoch_)) {
        return {DuplicateCheckpointStoreError::binding_mismatch};
    }
    if (current == nullptr) {
        next.generation = 1;
    } else {
        if (current->value.generation ==
            std::numeric_limits<std::uint64_t>::max()) {
            return {DuplicateCheckpointStoreError::generation_exhausted};
        }
        next.generation = current->value.generation + 1;
    }

    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> encoded{};
    DuplicateCheckpointCodecError codec_error{
        DuplicateCheckpointCodecError::none};
    if (!encode_record(next, encoded, codec_error)) {
        DuplicateCheckpointSaveResult result{
            DuplicateCheckpointStoreError::checkpoint_rejected};
        result.codec_error = codec_error;
        return result;
    }

    std::uint8_t target = 0;
    if (current == nullptr) {
        target = 0;
    } else if (a.state != DuplicateCheckpointSlotState::valid) {
        target = 0;
    } else if (b.state != DuplicateCheckpointSlotState::valid) {
        target = 1;
    } else {
        target = current_slot == 0 ? 1 : 0;
    }
    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        DuplicateCheckpointStorageError::none) {
        return {DuplicateCheckpointStoreError::storage_failure};
    }

    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
            DuplicateCheckpointStorageError::none ||
        verified != encoded) {
        return {DuplicateCheckpointStoreError::verification_failure};
    }
    StoredCheckpoint decoded{};
    DuplicateCheckpointCodecError verification_codec_error{
        DuplicateCheckpointCodecError::none};
    if (decode_record(
            verified, decoded, verification_codec_error) !=
            DecodedRecordKind::current ||
        decoded.generation != next.generation ||
        decoded.group_context_id != group_context_id_ ||
        decoded.group_epoch != group_epoch_ ||
        !std::equal(
            encoded.begin() + kCheckpointOffset,
            encoded.begin() + kGroupEpochOffset,
            decoded.checkpoint_bytes.begin())) {
        DuplicateCheckpointSaveResult result{
            DuplicateCheckpointStoreError::verification_failure};
        result.codec_error = verification_codec_error;
        return result;
    }

    return {
        DuplicateCheckpointStoreError::none,
        source_for_slot(target),
        DuplicateCheckpointCodecError::none,
        next.generation,
        any_invalid};
}

DuplicateCheckpointStoreError DuplicateCheckpointStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == DuplicateCheckpointStorageError::none &&
                   b == DuplicateCheckpointStorageError::none
               ? DuplicateCheckpointStoreError::none
               : DuplicateCheckpointStoreError::storage_failure;
}

}  // namespace opentrail::delivery
