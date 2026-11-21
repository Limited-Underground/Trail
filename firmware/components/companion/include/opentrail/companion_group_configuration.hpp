#pragma once
#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kConfigurationGroupCommandKind = 0x0b;
inline constexpr std::uint8_t kConfigurationGroupResultKind = 0x8f;

enum class GroupConfigurationOperation : std::uint8_t { discover = 1, create = 2, stop_trust = 3 };
enum class GroupConfigurationStatus : std::uint8_t {
    ok = 0, unavailable = 1, refused = 2, stale = 3, uncertain = 4,
};

struct GroupConfigurationScope {
    std::array<std::uint8_t, 32> admin{};
    std::uint64_t group_id{0};
    std::array<std::uint8_t, 32> roster_digest{};
    bool operator==(const GroupConfigurationScope&) const = default;
};

struct GroupConfigurationCommand {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    std::uint8_t location_rule{0};
    GroupConfigurationScope scope{};
};

struct GroupConfigurationResult {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    GroupConfigurationStatus status{GroupConfigurationStatus::unavailable};
    GroupConfigurationScope scope{};
};

[[nodiscard]] ConfigurationEncodeResult encode_group_configuration_command(
    const GroupConfigurationCommand&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<GroupConfigurationCommand> decode_group_configuration_command(
    const std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationEncodeResult encode_group_configuration_result(
    const GroupConfigurationResult&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<GroupConfigurationResult> decode_group_configuration_result(
    const std::uint8_t*, std::size_t);

} // namespace opentrail::companion
