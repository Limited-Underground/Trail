#pragma once
#include "opentrail/companion_device_name_codec.hpp"
#include <algorithm>
#include <limits>
namespace opentrail::companion {
inline constexpr std::uint8_t kPublicProfileMinor = 5;
inline constexpr std::size_t kPublicProfileHeaderBytes = 20;
inline constexpr std::size_t kPublicProfilePayloadBytes = 116;
struct PublicProfilePayload {
    DeviceNameKind kind{DeviceNameKind::read};
    DeviceNameReason reason{DeviceNameReason::none};
    std::uint64_t revision{0};
    std::uint8_t name_bytes{0};
    std::array<std::uint8_t, kDeviceNameMaxUtf8Bytes> name{};
    bool visible{false};
};
struct PublicProfileDecodeResult {
    DeviceNameCodecError error{DeviceNameCodecError::invalid_argument};
    PublicProfilePayload value{};
    bool decoded() const {return error==DeviceNameCodecError::none;}
};
[[nodiscard]] bool valid_public_profile_name(const std::uint8_t*,std::size_t);
[[nodiscard]] DeviceNameEncodeResult encode_public_profile_payload(const PublicProfilePayload&,std::uint8_t*,std::size_t);
[[nodiscard]] PublicProfileDecodeResult decode_public_profile_payload(const std::uint8_t*,std::size_t);
inline PublicProfilePayload absent_public_profile() {PublicProfilePayload p;p.kind=DeviceNameKind::snapshot;p.visible=true;return p;}
inline bool same_public_profile(const PublicProfilePayload& a,const PublicProfilePayload& b) {
    return a.revision==b.revision && a.visible==b.visible && a.name_bytes==b.name_bytes && a.name==b.name;
}
}
