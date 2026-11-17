#pragma once

#include "opentrail/group_receive_state.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kGroupAckPlaintextBytes = 16;
using GroupAckPlaintext = std::array<std::uint8_t, kGroupAckPlaintextBytes>;

// Supplied by the current Packet-v1 owner after authenticating the packet in
// the selected group pair context. The group name includes the full admin ID.
struct GroupAckRoute {
    identity::roster::GroupName group{};
    GroupMemberId sender{};
    GroupMemberId recipient{};
    std::uint32_t epoch{0};
    GroupMemberId transcript{};
};

struct GroupDataOperation {
    GroupAckRoute route{};
    std::uint32_t message_id{0};  // authenticated Packet-v1 header field
    std::uint64_t counter{0};      // DATA frame counter
};

struct GroupAckEnvelope {
    GroupAckRoute route{};
    std::uint32_t message_id{0};
    std::uint8_t fragment_index{0};
    std::uint8_t fragment_count{1};
};

struct GroupAckReply {
    GroupAckEnvelope envelope{};
    GroupAckPlaintext payload{};
};

struct GroupReceiptAndAck {
    DurableGroupReceiveResult receive;
    std::optional<GroupAckReply> ack;
};

struct GroupBoardReceipt {
    GroupDataOperation operation;
    GroupMemberId board;
    std::uint32_t roster_version;
    GroupDigestPrefix digest_prefix;  // discovery hint; signed roster validation still applies
};

// Accepts DATA plaintext from the current authenticated group-pair owner after
// its packet replay gate. The caller serializes this owner with roster updates
// and binds the standing and view to the current durable roster. A reply
// attests board receipt after durable history admission. The caller owns the
// Packet-v1 ACK counter, sealing, and launch, and the phone presents independently.
class GroupAckReceiver {
public:
    GroupAckReceiver(persistence::PersistentKvBackend& backend, const GroupAckRoute& expected_data);
    GroupAckReceiver(const GroupAckReceiver&) = delete;
    GroupAckReceiver& operator=(const GroupAckReceiver&) = delete;

    // Same first-admission-only provisioning contract as GroupReceiveState.
    [[nodiscard]] GroupReceiveStateError initialize();
    [[nodiscard]] GroupReceiptAndAck receive(const GroupDataOperation& authenticated_data,
                                             const SenderStanding& standing, const GroupView& view,
                                             GroupContent content, const std::uint8_t* plaintext,
                                             std::size_t size);

private:
    const GroupAckRoute expected_;
    GroupReceiveState state_;
};

// Call only with an authenticated, replay-admitted ACK and the current pending
// DATA operation. Matches the full reversed route, epoch/transcript and both
// operation fields. It returns evidence for the peer board only.
[[nodiscard]] std::optional<GroupBoardReceipt> match_group_ack(const GroupDataOperation& pending,
                                                               const GroupAckEnvelope& authenticated_ack,
                                                               const std::uint8_t* plaintext,
                                                               std::size_t size);

}  // namespace opentrail::delivery
