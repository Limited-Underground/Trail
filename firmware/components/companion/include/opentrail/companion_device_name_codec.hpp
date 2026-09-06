#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

// Candidate payload only. No live GATT frame or advertised capability is allocated.
inline constexpr std::size_t kDeviceNameHeaderBytes = 16;
inline constexpr std::size_t kDeviceNameMaxUtf8Bytes = 96;
inline constexpr std::size_t kDeviceNameMaxUtf16Units = 32;
inline constexpr std::size_t kDeviceNameMaxPayloadBytes = 112;

enum class DeviceNameKind : std::uint8_t {
    read = 1, write = 2, snapshot = 0x81, applied = 0x82,
    rejected = 0x83, uncertain = 0x84,
};

enum class DeviceNameReason : std::uint8_t {
    none = 0, unauthorized = 1, stale_revision = 2,
    invalid_name = 3, storage_failure = 4, unsupported = 5,
};

enum class DeviceNameCodecError : std::uint8_t {
    none, invalid_argument, malformed, unsupported_version, unknown_kind,
    unknown_reason, invalid_name, incoherent_fields, output_too_small,
};

struct DeviceNamePayload {
    DeviceNameKind kind{DeviceNameKind::read};
    DeviceNameReason reason{DeviceNameReason::none};
    std::uint64_t revision{0};
    std::uint8_t name_bytes{0};
    std::array<std::uint8_t, kDeviceNameMaxUtf8Bytes> name{};
};

struct DeviceNameDecodeResult {
    DeviceNameCodecError error{DeviceNameCodecError::invalid_argument};
    DeviceNamePayload value{};
    [[nodiscard]] bool decoded() const { return error == DeviceNameCodecError::none; }
};

struct DeviceNameEncodeResult {
    DeviceNameCodecError error{DeviceNameCodecError::invalid_argument};
    std::size_t encoded_bytes{0};
    [[nodiscard]] bool encoded() const { return error == DeviceNameCodecError::none; }
};

[[nodiscard]] bool valid_device_name_utf8(const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] DeviceNameDecodeResult decode_device_name_payload(const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] DeviceNameEncodeResult encode_device_name_payload(
    const DeviceNamePayload& value, std::uint8_t* output, std::size_t capacity);

} // namespace opentrail::companion
