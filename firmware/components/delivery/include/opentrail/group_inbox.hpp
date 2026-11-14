#pragma once

// Receiving and sending state for Trail V1 group messages (docs/security/GROUP_PAIRWISE_V1.md,
// "Group messages"; implementation order step 3). Host-only logic; durable storage of this
// state is a later part of step 3.
//
// Receiving (GroupInbox, one per group):
// - accept only from members on this board's newest roster with an active pair;
// - a frame naming a newer roster is held back (fetch it first); the same version with a
//   different digest prefix is a roster disagreement (fetch and offer it);
// - group positions are refused while the location rule is off;
// - each message is shown once, by sender and message number; numbers skipped since the
//   last one are reported as missing, and a late arrival fills its gap;
// - the same sender and number with different content is a conflict and is shown as one.
//   Content is compared by a 64-bit checksum, which is not cryptographic: Packet-v1 already
//   authenticates each frame, so this only catches a sender reusing a number.
// Each sender keeps a window of its last 32 numbers; anything older cannot be told apart
// and is not shown.
//
// Sending (GroupOutbox, one per group): delivery per member for recent messages
// ("delivered to 4 of 5"). There is no relaying.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/group_frame.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kGroupMemberIdBytes = 32;
using GroupMemberId = std::array<std::uint8_t, kGroupMemberIdBytes>;  // member fingerprint

inline constexpr std::size_t kGroupOtherMembers = 5;     // a group has at most 6 members
inline constexpr std::uint32_t kGroupInboxWindow = 32;   // numbers remembered per sender
inline constexpr std::size_t kGroupOutboxMessages = 16;  // recent sent messages tracked

// This board's view of the group when a frame arrives.
struct GroupView {
    std::uint32_t roster_version{0};
    GroupDigestPrefix digest_prefix{};
    bool location_allowed{false};  // the roster's location rule is not off
};

// The sender as this board's newest roster and pair records see it.
struct SenderStanding {
    bool on_current_roster{false};
    bool pair_active{false};
};

enum class GroupReceive : std::uint8_t {
    shown = 0,         // new message: show it
    conflict,          // same sender and number, different content: show as a conflict
    duplicate,         // already shown
    too_old,           // older than the sender's window: not shown
    not_member,        // sender not on this board's newest roster
    no_pair,           // no active pair with the sender
    roster_newer,      // sender used a newer roster: fetch it, not shown yet
    roster_differs,    // same version, different digest: fetch and offer it, not shown
    position_refused,  // location rule is off
    capacity,          // more senders than a group can have
};

struct GroupReceiveResult {
    GroupReceive outcome{GroupReceive::not_member};
    bool sender_roster_older{false};  // sender should be offered this board's roster
    std::uint32_t missing_before{0};  // numbers skipped since this sender's previous highest
};

class GroupInbox {
public:
    [[nodiscard]] GroupReceiveResult receive(const GroupMemberId& sender, const SenderStanding& standing,
                                             const GroupView& view, GroupContent content,
                                             const DecodedGroupFrame& frame);
    // Numbers within the sender's window, since the first one seen, that have not arrived.
    [[nodiscard]] std::uint32_t missing(const GroupMemberId& sender) const;
    // A member removed from the roster (or a pair deleted): its state is dropped.
    void forget(const GroupMemberId& sender);
    void clear();

private:
    struct Sender {
        bool used{false};
        GroupMemberId id{};
        std::uint32_t first{0};    // first number seen (nothing before it counts as missing)
        std::uint32_t highest{0};
        std::uint32_t seen{0};     // bit i: number highest - i has arrived
        std::array<std::uint64_t, kGroupInboxWindow> sums{};  // content checksum per bit
    };
    Sender* find(const GroupMemberId& id);
    const Sender* find(const GroupMemberId& id) const;

    std::array<Sender, kGroupOtherMembers> senders_{};
};

enum class GroupOutboxError : std::uint8_t {
    none = 0,
    invalid_argument,  // number 0, no recipients, too many, or repeated recipients
    duplicate_number,  // already tracked (numbers never repeat)
    unknown_message,
    not_recipient,
};

struct GroupDeliveryStatus {
    std::size_t delivered{0};
    std::size_t total{0};
};

class GroupOutbox {
public:
    // Starts tracking a sent message (up to kGroupOutboxMessages at once). When full, the
    // lowest-numbered complete message is dropped first (delivered to every remaining
    // recipient, or with none left), otherwise the lowest-numbered one; its number is
    // returned in `evicted`. This keeps pending messages longer than the 16 newest.
    [[nodiscard]] GroupOutboxError track(std::uint32_t number, const GroupMemberId* recipients, std::size_t count,
                                         std::optional<std::uint32_t>& evicted);
    // An acknowledgement from `member` for `number` (repeats are harmless).
    [[nodiscard]] GroupOutboxError delivered(std::uint32_t number, const GroupMemberId& member);
    [[nodiscard]] std::optional<GroupDeliveryStatus> status(std::uint32_t number) const;
    // A member removed from the roster no longer counts toward any message.
    void drop_member(const GroupMemberId& member);
    void clear();

private:
    struct Message {
        bool used{false};
        std::uint32_t number{0};
        std::array<GroupMemberId, kGroupOtherMembers> recipients{};
        std::size_t count{0};
        std::uint8_t delivered{0};  // bit per recipient
    };
    Message* find(std::uint32_t number);
    const Message* find(std::uint32_t number) const;

    std::array<Message, kGroupOutboxMessages> messages_{};
};

}  // namespace opentrail::delivery
