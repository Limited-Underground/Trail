#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

// Raw value for legacy AD type 0x21: the 128-bit D1 service UUID in Bluetooth
// little-endian order, followed by a versioned, correlation-only receipt.
inline constexpr std::size_t kCompanionResetReceiptPayloadBytes = 13;
inline constexpr std::size_t kCompanionResetReceiptServiceDataBytes =
    16 + kCompanionResetReceiptPayloadBytes;

struct CompanionResetReceiptServiceData {
    std::array<std::uint8_t, kCompanionResetReceiptServiceDataBytes> bytes{};
    bool valid{false};
};

[[nodiscard]] constexpr CompanionResetReceiptServiceData
encode_companion_reset_receipt_service_data(std::uint64_t receipt) {
    CompanionResetReceiptServiceData encoded{};
    if (receipt == 0) return encoded;

    constexpr std::array<std::uint8_t, 16> d1_uuid_le{
        0xd1, 0xb7, 0x43, 0x1f, 0x4f, 0x0c, 0x10, 0xa2,
        0xa3, 0x4e, 0x6b, 0x7c, 0x00, 0x2a, 0x0f, 0x5e,
    };
    for (std::size_t index = 0; index < d1_uuid_le.size(); ++index) {
        encoded.bytes[index] = d1_uuid_le[index];
    }
    encoded.bytes[16] = 'O';
    encoded.bytes[17] = 'T';
    encoded.bytes[18] = 'R';
    encoded.bytes[19] = 'R';
    encoded.bytes[20] = 0x01;
    for (std::size_t index = 0; index < sizeof(receipt); ++index) {
        encoded.bytes[21 + index] = static_cast<std::uint8_t>(
            receipt >> (index * 8U));
    }
    encoded.valid = true;
    return encoded;
}

static_assert(kCompanionResetReceiptServiceDataBytes == 29);

}  // namespace opentrail::companion
