#pragma once
#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kConfigurationGroupCommandKind = 0x0b;
inline constexpr std::uint8_t kConfigurationGroupResultKind = 0x8f;

inline constexpr std::size_t kConfigurationGroupSlots = 4;
inline constexpr std::size_t kConfigurationRosterFragmentBytes = 42;
inline constexpr std::size_t kConfigurationGroupRosterBytes = 439;

enum class GroupConfigurationOperation : std::uint8_t {
    discover = 1, create = 2, stop_trust = 3, read_roster = 4,
};
enum class GroupConfigurationStatus : std::uint8_t {
    ok = 0, unavailable = 1, refused = 2, stale = 3, uncertain = 4, missing = 5,
};
enum class GroupConfigurationRosterState : std::uint8_t {
    none = 0, active = 1, distrusted = 2,
};

struct GroupConfigurationScope {
    std::array<std::uint8_t, 32> admin{};
    std::uint64_t group_id{0};
    std::array<std::uint8_t, 32> roster_digest{};
    bool operator==(const GroupConfigurationScope& other) const {
        return admin == other.admin && group_id == other.group_id && roster_digest == other.roster_digest;
    }
    bool operator!=(const GroupConfigurationScope& other) const {
        return !(*this == other);
    }
};

struct GroupConfigurationReadRequest {
    std::uint8_t slot{0};
    GroupConfigurationRosterState state{GroupConfigurationRosterState::none};
    std::uint16_t offset{0};
};

struct GroupConfigurationRosterFragment {
    std::uint8_t slot{0};
    GroupConfigurationRosterState state{GroupConfigurationRosterState::none};
    std::uint16_t offset{0};
    std::uint16_t total_bytes{0};
    std::uint8_t size{0};
    std::array<std::uint8_t, kConfigurationRosterFragmentBytes> bytes{};
};

struct GroupConfigurationCommand {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    std::uint8_t location_rule{0};
    GroupConfigurationScope scope{};
    // Offset zero opens a read by slot. Continuations pin scope and state.
    GroupConfigurationReadRequest read{};
};

struct GroupConfigurationResult {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    GroupConfigurationStatus status{GroupConfigurationStatus::unavailable};
    GroupConfigurationScope scope{};
    GroupConfigurationRosterFragment fragment{};
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
