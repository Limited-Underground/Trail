#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {
inline constexpr std::size_t kConfigurationInfoBytes = 16;
inline constexpr std::size_t kConfigurationHeaderBytes = 20;
inline constexpr std::size_t kConfigurationPayloadBytes = 128;
inline constexpr std::size_t kConfigurationRecordBytes = 148;
inline constexpr std::size_t kConfigurationTimeBytes = 24;
inline constexpr std::uint8_t kConfigurationNameCapability = 0x40;
inline constexpr std::uint8_t kConfigurationTimeCapability = 0x80;
enum class ConfigurationCodecError { none, invalid_argument, malformed, output_too_small };
struct ConfigurationInfo { std::uint8_t capabilities{0}; };
struct ConfigurationFrame {
    std::uint8_t kind{1};
    std::uint32_t session_nonce{0}, exchange_id{0};
    std::uint16_t payload_bytes{0};
    std::array<std::uint8_t, kConfigurationPayloadBytes> payload{};
};
struct ConfigurationTimePayload {
    std::uint8_t kind{1}, code{0}, format{0};
    std::uint64_t challenge_id{0};
    std::uint32_t local_second_of_day{0};
};
struct ConfigurationEncodeResult {
    ConfigurationCodecError error{ConfigurationCodecError::invalid_argument};
    std::size_t encoded_bytes{0};
    [[nodiscard]] bool encoded() const { return error == ConfigurationCodecError::none; }
};
template<class T> struct ConfigurationDecodeResult {
    ConfigurationCodecError error{ConfigurationCodecError::invalid_argument};
    T value{};
    [[nodiscard]] bool decoded() const { return error == ConfigurationCodecError::none; }
};
// Separate strict candidate codec: never advertised, dispatched or used as authority.
// Fixed profile fields are not caller-configurable. Failure leaves output unchanged.
[[nodiscard]] ConfigurationEncodeResult encode_configuration_info(const ConfigurationInfo&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<ConfigurationInfo> decode_configuration_info(const std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationEncodeResult encode_configuration_frame(const ConfigurationFrame&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<ConfigurationFrame> decode_configuration_frame(const std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationEncodeResult encode_configuration_time_payload(const ConfigurationTimePayload&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<ConfigurationTimePayload> decode_configuration_time_payload(const std::uint8_t*, std::size_t);
// Transport compatibility only; encrypted ownership, Ready, session correlation and
// negotiated operation selection remain separate mandatory runtime checks.
[[nodiscard]] bool configuration_transport_compatible(const ConfigurationInfo&, std::uint8_t requested_capability,
    std::uint16_t actual_mtu, std::size_t send_capacity, std::size_t receive_capacity, bool indications);
} // namespace opentrail::companion
