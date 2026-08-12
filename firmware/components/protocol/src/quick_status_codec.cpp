#include "opentrail/quick_status_codec.hpp"

#include <array>

namespace opentrail::protocol {
namespace {

constexpr std::size_t kChecksumOffset = 8;

bool known_status(QuickStatusKind kind) {
    switch (kind) {
        case QuickStatusKind::ok:
        case QuickStatusKind::need_assistance:
        case QuickStatusKind::anyone_online:
        case QuickStatusKind::available_to_help:
            return true;
    }
    return false;
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

}  // namespace

std::uint32_t quick_status_crc32(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr && size != 0) {
        return 0;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

QuickStatusEncodeResult encode_quick_status(
    const QuickStatusPayload& payload,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {QuickStatusCodecError::invalid_argument, 0};
    }
    if (output.size < kQuickStatusPayloadBytes) {
        return {
            QuickStatusCodecError::output_too_small,
            kQuickStatusPayloadBytes,
        };
    }
    if (!known_status(payload.kind)) {
        return {QuickStatusCodecError::unknown_status, 0};
    }

    std::array<std::uint8_t, kQuickStatusPayloadBytes> candidate{};
    candidate[0] = 'O';
    candidate[1] = 'T';
    candidate[2] = 'Q';
    candidate[3] = '0';
    candidate[4] = kQuickStatusPayloadVersion;
    candidate[5] = static_cast<std::uint8_t>(kQuickStatusPayloadBytes);
    candidate[6] = static_cast<std::uint8_t>(payload.kind);
    candidate[7] = 0;
    write_u32_le(
        candidate.data() + kChecksumOffset,
        quick_status_crc32(candidate.data(), kChecksumOffset));

    for (std::size_t index = 0; index < candidate.size(); ++index) {
        output.data[index] = candidate[index];
    }
    return {QuickStatusCodecError::none, kQuickStatusPayloadBytes};
}

QuickStatusDecodeResult decode_quick_status(radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {QuickStatusCodecError::invalid_argument, {}};
    }
    if (encoded.size != kQuickStatusPayloadBytes ||
        encoded.data[5] != kQuickStatusPayloadBytes) {
        return {QuickStatusCodecError::malformed, {}};
    }
    if (encoded.data[0] != 'O' || encoded.data[1] != 'T' ||
        encoded.data[2] != 'Q' || encoded.data[3] != '0') {
        return {QuickStatusCodecError::malformed, {}};
    }
    if (encoded.data[4] != kQuickStatusPayloadVersion) {
        return {QuickStatusCodecError::unsupported_version, {}};
    }
    const auto kind = static_cast<QuickStatusKind>(encoded.data[6]);
    if (!known_status(kind)) {
        return {QuickStatusCodecError::unknown_status, {}};
    }
    if (encoded.data[7] != 0) {
        return {QuickStatusCodecError::reserved_bits_set, {}};
    }
    if (read_u32_le(encoded.data + kChecksumOffset) !=
        quick_status_crc32(encoded.data, kChecksumOffset)) {
        return {QuickStatusCodecError::integrity_failure, {}};
    }
    return {QuickStatusCodecError::none, {kind}};
}

}  // namespace opentrail::protocol
