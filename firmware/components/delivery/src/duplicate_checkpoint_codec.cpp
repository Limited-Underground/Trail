#include "opentrail/duplicate_checkpoint_codec.hpp"

#include <algorithm>
#include <array>

namespace opentrail::delivery {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'D', '0'}};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kEntryBytes = 20;
constexpr std::size_t kEntriesOffset = kHeaderBytes;
constexpr std::size_t kTailReservedOffset =
    kEntriesOffset + kDuplicateWindowCapacity * kEntryBytes;
constexpr std::size_t kCrcOffset = kDuplicateCheckpointRecordBytes - 4;

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

bool valid_key(const DuplicateKey& key) {
    return key.source_alias != 0 && key.group_epoch != 0 &&
           key.message_id != 0;
}

}  // namespace

DuplicateCheckpointCodecError validate_duplicate_checkpoint(
    const DuplicateCheckpoint& checkpoint) {
    if (checkpoint.version != kDuplicateCheckpointVersion ||
        checkpoint.count > kDuplicateWindowCapacity) {
        return DuplicateCheckpointCodecError::invalid_checkpoint;
    }
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        const auto& entry = checkpoint.entries[index];
        if (!valid_key(entry.key) || entry.remaining_lifetime_ms == 0) {
            return DuplicateCheckpointCodecError::invalid_checkpoint;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (checkpoint.entries[prior].key == entry.key) {
                return DuplicateCheckpointCodecError::duplicate_key;
            }
        }
    }
    return DuplicateCheckpointCodecError::none;
}

DuplicateCheckpointCodecResult encode_duplicate_checkpoint(
    const DuplicateCheckpoint& checkpoint,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity < kDuplicateCheckpointRecordBytes) {
        return {DuplicateCheckpointCodecError::invalid_argument, 0};
    }
    const auto validation = validate_duplicate_checkpoint(checkpoint);
    if (validation != DuplicateCheckpointCodecError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kDuplicateCheckpointRecordBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kDuplicateCheckpointEnvelopeVersion;
    candidate[5] = kDuplicateCheckpointVersion;
    write_u16(candidate.data() + 6,
              static_cast<std::uint16_t>(kHeaderBytes));
    candidate[8] = static_cast<std::uint8_t>(checkpoint.count);
    write_u16(candidate.data() + 12,
              static_cast<std::uint16_t>(kEntryBytes));
    write_u16(candidate.data() + 14,
              static_cast<std::uint16_t>(kDuplicateWindowCapacity));
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        const auto& entry = checkpoint.entries[index];
        auto* target = candidate.data() + kEntriesOffset + index * kEntryBytes;
        write_u64(target, entry.key.source_alias);
        write_u32(target + 8, entry.key.group_epoch);
        write_u32(target + 12, entry.key.message_id);
        write_u32(target + 16, entry.remaining_lifetime_ms);
    }
    write_u32(candidate.data() + kCrcOffset,
              crc32(candidate.data(), kCrcOffset));
    std::copy(candidate.begin(), candidate.end(), output);
    return {DuplicateCheckpointCodecError::none, candidate.size()};
}

DuplicateCheckpointCodecResult decode_duplicate_checkpoint(
    const std::uint8_t* data,
    std::size_t size,
    DuplicateCheckpoint& output) {
    if (data == nullptr || size != kDuplicateCheckpointRecordBytes) {
        return {DuplicateCheckpointCodecError::invalid_argument, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) {
        return {DuplicateCheckpointCodecError::bad_magic, 0};
    }
    if (data[4] != kDuplicateCheckpointEnvelopeVersion ||
        data[5] != kDuplicateCheckpointVersion) {
        return {DuplicateCheckpointCodecError::unsupported_version, 0};
    }
    if (read_u16(data + 6) != kHeaderBytes || data[9] != 0 ||
        data[10] != 0 || data[11] != 0 ||
        read_u16(data + 12) != kEntryBytes ||
        read_u16(data + 14) != kDuplicateWindowCapacity ||
        !all_zero(data + kTailReservedOffset,
                  kCrcOffset - kTailReservedOffset)) {
        return {DuplicateCheckpointCodecError::noncanonical_record, 0};
    }
    if (read_u32(data + kCrcOffset) != crc32(data, kCrcOffset)) {
        return {DuplicateCheckpointCodecError::integrity_failure, 0};
    }
    const auto count = static_cast<std::size_t>(data[8]);
    if (count > kDuplicateWindowCapacity) {
        return {DuplicateCheckpointCodecError::invalid_checkpoint, 0};
    }

    DuplicateCheckpoint candidate{};
    candidate.count = count;
    for (std::size_t index = 0; index < kDuplicateWindowCapacity; ++index) {
        const auto* source = data + kEntriesOffset + index * kEntryBytes;
        if (index >= count) {
            if (!all_zero(source, kEntryBytes)) {
                return {
                    DuplicateCheckpointCodecError::noncanonical_record, 0};
            }
            continue;
        }
        candidate.entries[index] = {
            {read_u64(source), read_u32(source + 8), read_u32(source + 12)},
            read_u32(source + 16)};
    }
    const auto validation = validate_duplicate_checkpoint(candidate);
    if (validation != DuplicateCheckpointCodecError::none) {
        return {validation, 0};
    }
    output = candidate;
    return {DuplicateCheckpointCodecError::none, size};
}

}  // namespace opentrail::delivery
