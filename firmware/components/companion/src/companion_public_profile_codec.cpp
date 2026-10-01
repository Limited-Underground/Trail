#include "opentrail/companion_public_profile_codec.hpp"

namespace opentrail::companion {
namespace {
DeviceNameCodecError validate(const PublicProfilePayload& payload) {
    if (static_cast<unsigned>(payload.reason) > 5) return DeviceNameCodecError::unknown_reason;
    const bool empty = payload.name_bytes == 0;
    const bool none = payload.reason == DeviceNameReason::none;
    if (payload.name_bytes > kDeviceNameMaxUtf8Bytes ||
        (!empty && !valid_public_profile_name(payload.name.data(), payload.name_bytes))) {
        return DeviceNameCodecError::invalid_name;
    }
    bool valid = false;
    switch (payload.kind) {
    case DeviceNameKind::read:
        valid = none && empty && payload.revision == 0 && !payload.visible;
        break;
    case DeviceNameKind::write:
        valid = none && !empty && payload.revision != std::numeric_limits<std::uint64_t>::max();
        break;
    case DeviceNameKind::snapshot:
        valid = none && (empty ? (payload.revision == 0 && payload.visible) : payload.revision != 0);
        break;
    case DeviceNameKind::applied:
        valid = none && !empty && payload.revision != 0;
        break;
    case DeviceNameKind::rejected:
        valid = !none && empty && payload.revision == 0 && !payload.visible;
        break;
    case DeviceNameKind::uncertain:
        valid = payload.reason == DeviceNameReason::storage_failure && empty &&
            payload.revision == 0 && !payload.visible;
        break;
    default:
        return DeviceNameCodecError::unknown_kind;
    }
    return valid ? DeviceNameCodecError::none : DeviceNameCodecError::incoherent_fields;
}
}
bool valid_public_profile_name(const std::uint8_t* bytes, std::size_t size) {
    // Storage transport validity only. Public discovery's semantic alias policy
    // is a separate gate; persisting a preference creates no presence authority.
    return valid_bounded_name_utf8(bytes, size, 40);
}
DeviceNameEncodeResult encode_public_profile_payload(
    const PublicProfilePayload& payload, std::uint8_t* output, std::size_t capacity) {
    if (!output) return {};
    const auto error = validate(payload);
    if (error != DeviceNameCodecError::none) return {error, 0};
    const auto size = kPublicProfileHeaderBytes + payload.name_bytes;
    if (capacity < size) return {DeviceNameCodecError::output_too_small, 0};
    std::array<std::uint8_t, kPublicProfilePayloadBytes> bytes{
        'O','T','P','C',1,static_cast<std::uint8_t>(payload.kind),
        static_cast<std::uint8_t>(payload.reason),payload.name_bytes};
    for (unsigned i = 0; i < 8; ++i) {
        bytes[8+i] = static_cast<std::uint8_t>(payload.revision >> (8*i));
    }
    bytes[16] = payload.visible ? 1 : 0;
    std::copy_n(payload.name.begin(), payload.name_bytes, bytes.begin() + kPublicProfileHeaderBytes);
    std::copy_n(bytes.begin(), size, output);
    return {DeviceNameCodecError::none, size};
}
PublicProfileDecodeResult decode_public_profile_payload(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes) return {};
    if (size < kPublicProfileHeaderBytes || size > kPublicProfilePayloadBytes ||
        bytes[0]!='O' || bytes[1]!='T' || bytes[2]!='P' || bytes[3]!='C' || bytes[4]!=1 ||
        bytes[16]>1 || bytes[17] || bytes[18] || bytes[19] || size!=20U+bytes[7]) {
        return {DeviceNameCodecError::malformed, {}};
    }
    PublicProfilePayload payload;
    payload.kind = static_cast<DeviceNameKind>(bytes[5]);
    payload.reason = static_cast<DeviceNameReason>(bytes[6]);
    payload.name_bytes = bytes[7];
    payload.visible = bytes[16] == 1;
    for (unsigned i = 0; i < 8; ++i) {
        payload.revision |= static_cast<std::uint64_t>(bytes[8+i]) << (8*i);
    }
    std::copy_n(bytes + kPublicProfileHeaderBytes, payload.name_bytes, payload.name.begin());
    const auto error = validate(payload);
    return {error, error == DeviceNameCodecError::none ? payload : PublicProfilePayload{}};
}
}
