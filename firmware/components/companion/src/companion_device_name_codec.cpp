#include "opentrail/companion_device_name_codec.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::companion {
namespace {
constexpr std::array<std::uint8_t, 4> magic{'O', 'T', 'N', 'C'};

DeviceNameCodecError validate(const DeviceNamePayload& value) {
    if (static_cast<std::uint8_t>(value.reason) > 5) return DeviceNameCodecError::unknown_reason;
    if (value.name_bytes > kDeviceNameMaxUtf8Bytes) return DeviceNameCodecError::invalid_name;
    const bool empty = value.name_bytes == 0;
    if (!empty && !valid_device_name_utf8(value.name.data(), value.name_bytes)) {
        return DeviceNameCodecError::invalid_name;
    }
    const bool reason_none = value.reason == DeviceNameReason::none;
    bool coherent = false;
    switch (value.kind) {
        case DeviceNameKind::read:
            coherent = reason_none && empty && value.revision == 0;
            break;
        case DeviceNameKind::write:
            coherent = reason_none && !empty && value.revision != std::numeric_limits<std::uint64_t>::max();
            break;
        case DeviceNameKind::snapshot:
            coherent = reason_none && (empty == (value.revision == 0));
            break;
        case DeviceNameKind::applied:
            coherent = reason_none && !empty && value.revision > 0;
            break;
        case DeviceNameKind::rejected:
            coherent = !reason_none && empty && value.revision == 0;
            break;
        case DeviceNameKind::uncertain:
            coherent = value.reason == DeviceNameReason::storage_failure && empty && value.revision == 0;
            break;
        default:
            return DeviceNameCodecError::unknown_kind;
    }
    return coherent ? DeviceNameCodecError::none : DeviceNameCodecError::incoherent_fields;
}
} // namespace

bool valid_device_name_utf8(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0 || size > kDeviceNameMaxUtf8Bytes ||
        bytes[0] == 0x20 || bytes[size - 1] == 0x20) return false;
    std::size_t offset = 0;
    std::size_t utf16_units = 0;
    while (offset < size) {
        const auto first = bytes[offset++];
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        std::size_t continuation = 0;
        if (first <= 0x7f) { codepoint = first; }
        else if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1fU; minimum = 0x80; continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0fU; minimum = 0x800; continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07U; minimum = 0x10000; continuation = 3;
        } else return false;
        if (continuation > size - offset) return false;
        for (std::size_t index = 0; index < continuation; ++index) {
            const auto next = bytes[offset++];
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint <= 0x1f ||
            (codepoint >= 0x7f && codepoint <= 0x9f)) return false;
        utf16_units += codepoint > 0xffff ? 2 : 1;
        if (utf16_units > kDeviceNameMaxUtf16Units) return false;
    }
    return true;
}

DeviceNameDecodeResult decode_device_name_payload(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr) return {};
    if (size < kDeviceNameHeaderBytes || size > kDeviceNameMaxPayloadBytes ||
        !std::equal(magic.begin(), magic.end(), bytes)) {
        return {DeviceNameCodecError::malformed, {}};
    }
    if (bytes[4] != 1) return {DeviceNameCodecError::unsupported_version, {}};
    if (bytes[7] > kDeviceNameMaxUtf8Bytes || size != kDeviceNameHeaderBytes + bytes[7]) {
        return {DeviceNameCodecError::malformed, {}};
    }
    DeviceNamePayload value{};
    value.kind = static_cast<DeviceNameKind>(bytes[5]);
    value.reason = static_cast<DeviceNameReason>(bytes[6]);
    value.name_bytes = bytes[7];
    for (std::size_t index = 0; index < 8; ++index) {
        value.revision |= static_cast<std::uint64_t>(bytes[8 + index]) << (8U * index);
    }
    std::copy_n(bytes + kDeviceNameHeaderBytes, value.name_bytes, value.name.begin());
    const auto error = validate(value);
    if (error != DeviceNameCodecError::none) return {error, {}};
    return {DeviceNameCodecError::none, value};
}

DeviceNameEncodeResult encode_device_name_payload(
    const DeviceNamePayload& value, std::uint8_t* output, std::size_t capacity) {
    if (output == nullptr) return {};
    const auto error = validate(value);
    if (error != DeviceNameCodecError::none) return {error, 0};
    const std::size_t required = kDeviceNameHeaderBytes + value.name_bytes;
    if (capacity < required) return {DeviceNameCodecError::output_too_small, 0};
    std::array<std::uint8_t, kDeviceNameMaxPayloadBytes> encoded{};
    std::copy(magic.begin(), magic.end(), encoded.begin());
    encoded[4] = 1;
    encoded[5] = static_cast<std::uint8_t>(value.kind);
    encoded[6] = static_cast<std::uint8_t>(value.reason);
    encoded[7] = value.name_bytes;
    for (std::size_t index = 0; index < 8; ++index) {
        encoded[8 + index] = static_cast<std::uint8_t>(value.revision >> (8U * index));
    }
    std::copy_n(value.name.begin(), value.name_bytes, encoded.begin() + kDeviceNameHeaderBytes);
    std::copy_n(encoded.begin(), required, output);
    return {DeviceNameCodecError::none, required};
}
} // namespace opentrail::companion
