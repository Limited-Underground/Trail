#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/duplicate_checkpoint_codec.hpp"

namespace {

using namespace opentrail::delivery;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

DuplicateCheckpoint checkpoint() {
    DuplicateCheckpoint value{};
    value.count = 2;
    value.entries[0] = {{0x0102030405060708ULL, 9, 10}, 11000};
    value.entries[1] = {{0x1112131415161718ULL, 19, 20}, 21000};
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

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void repair_crc(
    std::array<std::uint8_t, kDuplicateCheckpointRecordBytes>& bytes) {
    constexpr std::size_t offset = kDuplicateCheckpointRecordBytes - 4;
    write_u32(bytes.data() + offset, crc32(bytes.data(), offset));
}

std::array<std::uint8_t, kDuplicateCheckpointRecordBytes> encoded(
    const DuplicateCheckpoint& value = checkpoint()) {
    std::array<std::uint8_t, kDuplicateCheckpointRecordBytes> bytes{};
    EXPECT(encode_duplicate_checkpoint(value, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

void test_round_trip_and_explicit_offsets() {
    const auto original = checkpoint();
    const auto bytes = encoded(original);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'T' &&
           bytes[2] == 'D' && bytes[3] == '0');
    EXPECT(bytes[4] == 0 && bytes[5] == 1);
    EXPECT(bytes[6] == 16 && bytes[7] == 0);
    EXPECT(bytes[8] == 2);
    EXPECT(bytes[12] == 20 && bytes[13] == 0);
    EXPECT(bytes[14] == 32 && bytes[15] == 0);
    EXPECT(bytes[16] == 0x08 && bytes[23] == 0x01);
    EXPECT(bytes[24] == 9 && bytes[28] == 10);

    DuplicateCheckpoint decoded{};
    const auto result = decode_duplicate_checkpoint(
        bytes.data(), bytes.size(), decoded);
    EXPECT(result.succeeded());
    EXPECT(result.bytes == bytes.size());
    EXPECT(decoded.version == kDuplicateCheckpointVersion);
    EXPECT(decoded.count == 2);
    EXPECT(decoded.entries[0].key == original.entries[0].key);
    EXPECT(decoded.entries[0].remaining_lifetime_ms == 11000);
    EXPECT(decoded.entries[1].key == original.entries[1].key);
}

void test_empty_checkpoint_is_canonical() {
    DuplicateCheckpoint empty{};
    const auto bytes = encoded(empty);
    EXPECT(bytes[8] == 0);
    for (std::size_t index = 16; index < 668; ++index) {
        EXPECT(bytes[index] == 0);
    }
    DuplicateCheckpoint decoded = checkpoint();
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), decoded)
               .succeeded());
    EXPECT(decoded.count == 0);
}

void test_invalid_checkpoint_and_duplicate_key_preserve_output() {
    auto invalid = checkpoint();
    invalid.version = 2;
    std::array<std::uint8_t, kDuplicateCheckpointRecordBytes> output{};
    output.fill(0xA5);
    EXPECT(encode_duplicate_checkpoint(invalid, output.data(), output.size())
               .error == DuplicateCheckpointCodecError::invalid_checkpoint);
    EXPECT(output[0] == 0xA5 && output.back() == 0xA5);
    invalid = checkpoint();
    invalid.entries[1].key = invalid.entries[0].key;
    EXPECT(validate_duplicate_checkpoint(invalid) ==
           DuplicateCheckpointCodecError::duplicate_key);
    invalid = checkpoint();
    invalid.entries[0].remaining_lifetime_ms = 0;
    EXPECT(validate_duplicate_checkpoint(invalid) ==
           DuplicateCheckpointCodecError::invalid_checkpoint);
    invalid = checkpoint();
    invalid.count = kDuplicateWindowCapacity + 1;
    EXPECT(validate_duplicate_checkpoint(invalid) ==
           DuplicateCheckpointCodecError::invalid_checkpoint);
}

void test_argument_magic_and_version_rejection_are_atomic() {
    const auto good = encoded();
    DuplicateCheckpoint unchanged = checkpoint();
    unchanged.entries[0].remaining_lifetime_ms = 777;
    EXPECT(decode_duplicate_checkpoint(nullptr, good.size(), unchanged).error ==
           DuplicateCheckpointCodecError::invalid_argument);
    EXPECT(decode_duplicate_checkpoint(
               good.data(), good.size() - 1, unchanged).error ==
           DuplicateCheckpointCodecError::invalid_argument);
    auto bytes = good;
    bytes[0] = 'X';
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error == DuplicateCheckpointCodecError::bad_magic);
    bytes = good;
    bytes[4] = 1;
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error == DuplicateCheckpointCodecError::unsupported_version);
    EXPECT(unchanged.entries[0].remaining_lifetime_ms == 777);
}

void test_noncanonical_reserved_and_unused_entries_are_rejected() {
    auto bytes = encoded();
    DuplicateCheckpoint unchanged = checkpoint();
    bytes[9] = 1;
    repair_crc(bytes);
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error ==
           DuplicateCheckpointCodecError::noncanonical_record);
    bytes = encoded();
    bytes[16 + 2 * 20] = 1;
    repair_crc(bytes);
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error ==
           DuplicateCheckpointCodecError::noncanonical_record);
    bytes = encoded();
    bytes[656] = 1;
    repair_crc(bytes);
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error ==
           DuplicateCheckpointCodecError::noncanonical_record);
}

void test_crc_and_semantic_tamper_are_rejected() {
    auto bytes = encoded();
    DuplicateCheckpoint unchanged = checkpoint();
    bytes[20] ^= 0x80U;
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error == DuplicateCheckpointCodecError::integrity_failure);
    bytes = encoded();
    bytes[24] = 0;
    bytes[25] = 0;
    bytes[26] = 0;
    bytes[27] = 0;
    repair_crc(bytes);
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), unchanged)
               .error == DuplicateCheckpointCodecError::invalid_checkpoint);
}

void test_serialized_checkpoint_restores_remaining_lifetime() {
    DuplicateWindow original{1000};
    const DuplicateKey key{7, 8, 9};
    EXPECT(original.observe(key, 100).observation ==
           DuplicateObservation::accepted);
    const auto saved = original.checkpoint(400);
    const auto bytes = encoded(saved);
    DuplicateCheckpoint decoded{};
    EXPECT(decode_duplicate_checkpoint(bytes.data(), bytes.size(), decoded)
               .succeeded());
    DuplicateWindow restored{1000};
    EXPECT(restored.restore(decoded, 50) == DuplicateError::none);
    EXPECT(restored.observe(key, 749).observation ==
           DuplicateObservation::duplicate);
    EXPECT(restored.observe(key, 750).observation ==
           DuplicateObservation::accepted);
}

}  // namespace

int main() {
    test_round_trip_and_explicit_offsets();
    test_empty_checkpoint_is_canonical();
    test_invalid_checkpoint_and_duplicate_key_preserve_output();
    test_argument_magic_and_version_rejection_are_atomic();
    test_noncanonical_reserved_and_unused_entries_are_rejected();
    test_crc_and_semantic_tamper_are_rejected();
    test_serialized_checkpoint_restores_remaining_lifetime();

    if (failures != 0) {
        std::cerr << failures
                  << " duplicate checkpoint codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 duplicate checkpoint codec scenario groups\n";
    return EXIT_SUCCESS;
}
