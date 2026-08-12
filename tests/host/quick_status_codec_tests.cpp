#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/quick_status_codec.hpp"

namespace {

using namespace opentrail::protocol;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::array<std::uint8_t, kQuickStatusPayloadBytes> encode(
    QuickStatusKind kind) {
    std::array<std::uint8_t, kQuickStatusPayloadBytes> bytes{};
    const auto result = encode_quick_status(
        {kind},
        {bytes.data(), bytes.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == bytes.size());
    return bytes;
}

void test_canonical_ok_vector() {
    const std::array<std::uint8_t, kQuickStatusPayloadBytes> expected{
        0x4F, 0x54, 0x51, 0x30, 0x00, 0x0C,
        0x01, 0x00, 0x7D, 0x69, 0xF7, 0x34,
    };
    const auto encoded = encode(QuickStatusKind::ok);
    EXPECT(encoded == expected);
    const auto decoded = decode_quick_status(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.payload.kind == QuickStatusKind::ok);
}

void test_all_four_fixed_statuses_round_trip() {
    for (const auto kind : {
             QuickStatusKind::ok,
             QuickStatusKind::need_assistance,
             QuickStatusKind::anyone_online,
             QuickStatusKind::available_to_help,
         }) {
        const auto encoded = encode(kind);
        const auto decoded = decode_quick_status(
            {encoded.data(), encoded.size()});
        EXPECT(decoded.decoded());
        EXPECT(decoded.payload.kind == kind);
    }
}

void test_each_status_has_a_distinct_canonical_vector() {
    const auto ok = encode(QuickStatusKind::ok);
    const auto assistance = encode(QuickStatusKind::need_assistance);
    const auto online = encode(QuickStatusKind::anyone_online);
    const auto help = encode(QuickStatusKind::available_to_help);
    EXPECT(ok != assistance);
    EXPECT(ok != online);
    EXPECT(ok != help);
    EXPECT(assistance[8] == 0xBE && assistance[11] == 0x1F);
    EXPECT(online[8] == 0xFF && online[11] == 0x06);
    EXPECT(help[8] == 0x38 && help[11] == 0x49);
}

void test_encode_rejects_unknown_status_without_mutating_output() {
    std::array<std::uint8_t, kQuickStatusPayloadBytes> bytes{};
    bytes.fill(0xA5);
    const auto before = bytes;
    const auto result = encode_quick_status(
        {static_cast<QuickStatusKind>(0xFF)},
        {bytes.data(), bytes.size()});
    EXPECT(result.error == QuickStatusCodecError::unknown_status);
    EXPECT(result.encoded_bytes == 0);
    EXPECT(bytes == before);
}

void test_encode_reports_argument_and_capacity_failures() {
    std::array<std::uint8_t, kQuickStatusPayloadBytes> bytes{};
    EXPECT(encode_quick_status(
               {QuickStatusKind::ok},
               {nullptr, bytes.size()}).error ==
           QuickStatusCodecError::invalid_argument);
    const auto small = encode_quick_status(
        {QuickStatusKind::ok},
        {bytes.data(), bytes.size() - 1});
    EXPECT(small.error == QuickStatusCodecError::output_too_small);
    EXPECT(small.encoded_bytes == kQuickStatusPayloadBytes);
}

void test_decode_rejects_null_and_wrong_length() {
    const auto valid = encode(QuickStatusKind::ok);
    EXPECT(decode_quick_status({nullptr, valid.size()}).error ==
           QuickStatusCodecError::invalid_argument);
    EXPECT(decode_quick_status({valid.data(), valid.size() - 1}).error ==
           QuickStatusCodecError::malformed);
}

void test_decode_rejects_magic_and_declared_length() {
    auto modified = encode(QuickStatusKind::ok);
    modified[0] = 'X';
    EXPECT(decode_quick_status({modified.data(), modified.size()}).error ==
           QuickStatusCodecError::malformed);
    modified = encode(QuickStatusKind::ok);
    modified[5] = 11;
    EXPECT(decode_quick_status({modified.data(), modified.size()}).error ==
           QuickStatusCodecError::malformed);
}

void test_decode_distinguishes_version_status_and_reserve_errors() {
    auto modified = encode(QuickStatusKind::ok);
    modified[4] = 1;
    EXPECT(decode_quick_status({modified.data(), modified.size()}).error ==
           QuickStatusCodecError::unsupported_version);
    modified = encode(QuickStatusKind::ok);
    modified[6] = 0;
    EXPECT(decode_quick_status({modified.data(), modified.size()}).error ==
           QuickStatusCodecError::unknown_status);
    modified = encode(QuickStatusKind::ok);
    modified[7] = 1;
    EXPECT(decode_quick_status({modified.data(), modified.size()}).error ==
           QuickStatusCodecError::reserved_bits_set);
}

void test_decode_rejects_corruption() {
    for (std::size_t index = 0; index < kQuickStatusPayloadBytes; ++index) {
        auto modified = encode(QuickStatusKind::available_to_help);
        modified[index] ^= 0x40;
        const auto decoded = decode_quick_status(
            {modified.data(), modified.size()});
        EXPECT(!decoded.decoded());
    }
}

void test_payload_is_fixed_and_identity_free() {
    static_assert(kQuickStatusPayloadBytes == 12);
    static_assert(sizeof(QuickStatusPayload) == 1);
    static_assert(std::is_trivially_copyable_v<QuickStatusPayload>);
    const auto encoded = encode(QuickStatusKind::anyone_online);
    EXPECT(encoded[7] == 0);
    EXPECT(quick_status_crc32(nullptr, 0) == 0);
    EXPECT(quick_status_crc32(nullptr, 1) == 0);
}

}  // namespace

int main() {
    test_canonical_ok_vector();
    test_all_four_fixed_statuses_round_trip();
    test_each_status_has_a_distinct_canonical_vector();
    test_encode_rejects_unknown_status_without_mutating_output();
    test_encode_reports_argument_and_capacity_failures();
    test_decode_rejects_null_and_wrong_length();
    test_decode_rejects_magic_and_declared_length();
    test_decode_distinguishes_version_status_and_reserve_errors();
    test_decode_rejects_corruption();
    test_payload_is_fixed_and_identity_free();

    if (failures != 0) {
        std::cerr << failures
                  << " quick-status codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 quick-status payload codec scenario groups\n";
    return EXIT_SUCCESS;
}
