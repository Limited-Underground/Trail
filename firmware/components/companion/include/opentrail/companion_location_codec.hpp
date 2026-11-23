#pragma once

#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kConfigurationLocationCommandKind = 0x0a;
inline constexpr std::uint8_t kConfigurationLocationResultKind = 0x8c;
inline constexpr std::size_t kConfigurationLocationCommandBytes = 112;
inline constexpr std::size_t kConfigurationLocationResultBytes = 28;
inline constexpr std::uint32_t kConfigurationLocationFixLifetimeMs = 15000;
inline constexpr std::uint64_t kConfigurationLocationCommandLifetimeMs = 5000;
static_assert(kConfigurationLocationCommandBytes <= kConfigurationPayloadBytes);

enum class ConfigurationLocationKind : std::uint8_t { group = 1, direct = 2 };
enum class ConfigurationLocationOperation : std::uint8_t { consent = 1, start, renew, stop, withdraw };
enum class ConfigurationLocationStatus : std::uint8_t { ok = 1, denied, unavailable };

struct ConfigurationLocationScope {
    ConfigurationLocationKind kind{};
    std::uint8_t slot{};
    std::uint64_t id{}, generation{};
    std::array<std::uint8_t, 32> admin{}, digest{};

    bool operator==(const ConfigurationLocationScope& other) const {
        return kind == other.kind && slot == other.slot && id == other.id &&
            generation == other.generation && admin == other.admin && digest == other.digest;
    }
};

struct ConfigurationLocationCommand {
    ConfigurationLocationScope scope{};
    ConfigurationLocationOperation operation{};
    std::uint64_t lease{}, sample{};
    std::uint32_t age_ms{};
    std::int32_t latitude_e4{}, longitude_e4{};
};

struct ConfigurationLocationResult {
    ConfigurationLocationOperation operation{};
    ConfigurationLocationStatus status{};
    std::uint64_t id{}, generation{}, lease{};
};

// Profile 5 uses the existing authenticated Configuration frame. The body is
// versioned, exact length and big-endian. Group requests pin both roster hashes.
// The reply confirms command/lease state and has no coordinate fields.
[[nodiscard]] ConfigurationEncodeResult encode_configuration_location_command(
    const ConfigurationLocationCommand&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<ConfigurationLocationCommand> decode_configuration_location_command(
    const std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationEncodeResult encode_configuration_location_result(
    const ConfigurationLocationResult&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<ConfigurationLocationResult> decode_configuration_location_result(
    const std::uint8_t*, std::size_t);

}  // namespace opentrail::companion
