#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_public_link_info.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr std::array<std::uint8_t, kCompanionPublicLinkInfoBytes>
    kExpectedBytes{
        0x4F, 0x54, 0x42, 0x30, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x18, 0x00, 0x01, 0x01, 0x00, 0x00,
    };

std::array<std::uint8_t, kCompanionPublicLinkInfoBytes> encoded() {
    std::array<std::uint8_t, kCompanionPublicLinkInfoBytes> bytes{};
    const auto result = encode_companion_public_link_info(
        {bytes.data(), bytes.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == bytes.size());
    return bytes;
}

void test_exact_fixed_otb0_vector() {
    const auto bytes = encoded();
    EXPECT(bytes == kExpectedBytes);
    EXPECT(decode_companion_public_link_info(
               {bytes.data(), bytes.size()}).decoded());

    const auto decoded = decode_companion_protocol_info(
        {bytes.data(), bytes.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.info.role == CompanionDeviceRole::screenless_client);
    EXPECT(decoded.info.capabilities == 0);
    EXPECT(decoded.info.max_fragment_payload_bytes == 1);
    EXPECT(decoded.info.minimum_att_mtu == 24);
    EXPECT(decoded.info.max_fragment_count == 1);
    EXPECT(decoded.info.max_active_controllers == 1);
}

void test_encode_is_bounded_and_atomic() {
    std::array<std::uint8_t, kCompanionPublicLinkInfoBytes> bytes{};
    bytes.fill(0xA5U);
    const auto before = bytes;

    const auto null_output = encode_companion_public_link_info(
        {nullptr, bytes.size()});
    EXPECT(null_output.error == CompanionCodecError::invalid_argument);
    EXPECT(null_output.encoded_bytes == 0);

    const auto small = encode_companion_public_link_info(
        {bytes.data(), bytes.size() - 1});
    EXPECT(small.error == CompanionCodecError::output_too_small);
    EXPECT(small.encoded_bytes == kCompanionPublicLinkInfoBytes);
    EXPECT(bytes == before);
}

void test_decode_rejects_null_and_every_wrong_length() {
    const auto bytes = encoded();
    EXPECT(decode_companion_public_link_info(
               {nullptr, bytes.size()}).error ==
           CompanionCodecError::invalid_argument);

    for (std::size_t size = 0; size < bytes.size(); ++size) {
        EXPECT(decode_companion_public_link_info(
                   {bytes.data(), size}).error ==
               CompanionCodecError::malformed);
    }

    std::array<std::uint8_t, kCompanionPublicLinkInfoBytes + 1> long_bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        long_bytes[index] = bytes[index];
    }
    EXPECT(decode_companion_public_link_info(
               {long_bytes.data(), long_bytes.size()}).error ==
           CompanionCodecError::malformed);
}

void test_decode_rejects_every_noncanonical_byte() {
    const auto bytes = encoded();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        auto changed = bytes;
        changed[index] ^= 0x01U;
        EXPECT(!decode_companion_public_link_info(
                    {changed.data(), changed.size()}).decoded());
    }

    auto known_capability = bytes;
    known_capability[7] =
        static_cast<std::uint8_t>(CompanionCapability::quick_status);
    EXPECT(decode_companion_public_link_info(
               {known_capability.data(), known_capability.size()}).error ==
           CompanionCodecError::malformed);

    auto unknown_capability = bytes;
    unknown_capability[7] = 0x80U;
    EXPECT(decode_companion_public_link_info(
               {unknown_capability.data(), unknown_capability.size()}).error ==
           CompanionCodecError::unknown_capability);
}

void test_fixed_record_contains_no_variable_or_private_payload() {
    static_assert(kCompanionPublicLinkInfoBytes == 16);
    static_assert(std::is_trivially_copyable_v<
                  CompanionPublicLinkInfoDecodeResult>);
    static_assert(sizeof(CompanionPublicLinkInfoDecodeResult) == 1);

    const auto bytes = encoded();
    EXPECT(bytes[7] == 0);
    EXPECT(bytes[14] == 0 && bytes[15] == 0);
    for (std::size_t repeat = 0; repeat < 100; ++repeat) {
        EXPECT(encoded() == bytes);
        EXPECT(decode_companion_public_link_info(
                   {bytes.data(), bytes.size()}).decoded());
    }
}

}  // namespace

int main() {
    test_exact_fixed_otb0_vector();
    test_encode_is_bounded_and_atomic();
    test_decode_rejects_null_and_every_wrong_length();
    test_decode_rejects_every_noncanonical_byte();
    test_fixed_record_contains_no_variable_or_private_payload();

    if (failures != 0) {
        std::cerr << failures
                  << " companion public link-info assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 companion public link-info scenario groups\n";
    return EXIT_SUCCESS;
}
