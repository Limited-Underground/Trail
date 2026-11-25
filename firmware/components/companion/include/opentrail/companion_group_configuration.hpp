#pragma once
#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kConfigurationGroupCommandKind = 0x0b;
inline constexpr std::uint8_t kConfigurationGroupResultKind = 0x8f;

inline constexpr std::size_t kConfigurationGroupSlots = 4;
inline constexpr std::size_t kConfigurationRosterFragmentBytes = 42;
inline constexpr std::size_t kConfigurationGroupRosterBytes = 439;
inline constexpr std::size_t kConfigurationInviteIdBytes = 16;
// qr::kDescriptorBaseBytes, and that plus the longest label (qr::kNameBytes).
inline constexpr std::size_t kConfigurationInviteMinimumBytes = 167;
inline constexpr std::size_t kConfigurationInviteBytes = 287;
inline constexpr std::size_t kConfigurationInviteFragmentBytes = 96;
// The Invite command carries its label after the scope in the 128-byte body.
inline constexpr std::size_t kConfigurationInviteLabelBytes = 47;

enum class GroupConfigurationOperation : std::uint8_t {
    discover = 1, create = 2, stop_trust = 3, read_roster = 4,
    // F8 live Invite and Join, QR route. Invite issues a boot-scoped signed
    // invite on the admin board, Read Invite pages it out, and Join pages a
    // scanned invite into the joiner board, which verifies and claims it once.
    invite = 5, read_invite = 6, join = 7,
    // F8 step 2e: what the board's QR join is doing now, for the app to show.
    join_progress = 8,
};
// The step a board's QR group join is at. The app tells the person what to do;
// it never shows the comparison values, which stay on the boards' screens.
enum class GroupJoinStep : std::uint8_t {
    none = 0,          // no join on this board
    waiting = 1,       // looking for the other device over the radio
    review_ids = 2,    // this device's screen shows MY ID and PEER ID
    review_code = 3,   // this device's screen shows the 8-character code
    waiting_peer = 4,  // confirmed here; waiting for the other device's person
    saving = 5,        // keeping the decision on this device
    joined = 6,        // done: committed on this device
    cancelled = 7,     // ended by a person before it was kept
    failed = 8,        // ended without a decision (time limit, radio, storage)
};
enum class GroupJoinSide : std::uint8_t { none = 0, joiner = 1, admin = 2 };
struct GroupJoinProgress {
    GroupJoinStep step{GroupJoinStep::none};
    GroupJoinSide side{GroupJoinSide::none};
    std::uint64_t group_id{0};
    bool operator==(const GroupJoinProgress& other) const {
        return step == other.step && side == other.side && group_id == other.group_id;
    }
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

// Invite paging. Read Invite names the invite ID and offset; Join carries
// offset, total and the bytes; both results echo offset and total. Pages are
// kConfigurationInviteFragmentBytes, except a shorter last page.
struct GroupConfigurationInvitePage {
    std::array<std::uint8_t, kConfigurationInviteIdBytes> invite_id{};
    std::uint16_t offset{0};
    std::uint16_t total_bytes{0};
    std::uint8_t size{0};
    std::array<std::uint8_t, kConfigurationInviteFragmentBytes> bytes{};
};

struct GroupConfigurationLabel {
    std::uint8_t size{0};
    std::array<std::uint8_t, kConfigurationInviteLabelBytes> bytes{};
    bool operator==(const GroupConfigurationLabel& other) const {
        return size == other.size && bytes == other.bytes;
    }
};

struct GroupConfigurationCommand {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    std::uint8_t location_rule{0};
    GroupConfigurationScope scope{};
    // Offset zero opens a read by slot. Continuations pin scope and state.
    GroupConfigurationReadRequest read{};
    // Invite: label shown to the joiner. Read Invite and Join: page.
    GroupConfigurationLabel label{};
    GroupConfigurationInvitePage invite{};
};

struct GroupConfigurationResult {
    GroupConfigurationOperation operation{GroupConfigurationOperation::discover};
    GroupConfigurationStatus status{GroupConfigurationStatus::unavailable};
    GroupConfigurationScope scope{};
    GroupConfigurationRosterFragment fragment{};
    // Invite: ID and total. Read Invite: page. Join: offset and total, plus
    // the invite ID and scope once the last page is accepted.
    GroupConfigurationInvitePage invite{};
    // Join Progress only.
    GroupJoinProgress progress{};
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
