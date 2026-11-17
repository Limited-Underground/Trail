#include "opentrail/group_ack.hpp"

#include <algorithm>

namespace opentrail::delivery {
namespace {

using Error = GroupReceiveStateError;

constexpr std::size_t kCounterBytes = sizeof(std::uint64_t);
constexpr std::size_t kRosterVersionBytes = sizeof(std::uint32_t);
constexpr std::size_t kDigestOffset = kCounterBytes + kRosterVersionBytes;
static_assert(kDigestOffset + kGroupDigestPrefixBytes == kGroupAckPlaintextBytes);

void put_be(std::uint8_t* out, std::size_t size, std::uint64_t value) {
    for (std::size_t i = size; i > 0; --i) {
        out[i - 1] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

std::uint64_t get_be(const std::uint8_t* in, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) value = (value << 8U) | in[i];
    return value;
}

bool valid(const GroupAckRoute& route) {
    return route.group.group_id != 0 && route.group.admin != GroupMemberId{} &&
           route.sender != GroupMemberId{} && route.recipient != GroupMemberId{} &&
           route.sender != route.recipient && route.epoch != 0 && route.transcript != GroupMemberId{};
}

bool same_route(const GroupAckRoute& a, const GroupAckRoute& b) {
    return a.group.group_id == b.group.group_id && a.group.admin == b.group.admin &&
           a.sender == b.sender && a.recipient == b.recipient && a.epoch == b.epoch &&
           a.transcript == b.transcript;
}

GroupAckRoute reversed(const GroupAckRoute& route) {
    return {route.group, route.recipient, route.sender, route.epoch, route.transcript};
}

}  // namespace

GroupAckReceiver::GroupAckReceiver(persistence::PersistentKvBackend& backend,
                                   const GroupAckRoute& expected_data)
    : expected_(expected_data), state_(backend, expected_.group, expected_.recipient, expected_.sender) {}

GroupReceiveStateError GroupAckReceiver::initialize() {
    return valid(expected_) ? state_.initialize() : Error::invalid_scope;
}

GroupReceiptAndAck GroupAckReceiver::receive(const GroupDataOperation& data, const SenderStanding& standing,
                                           const GroupView& view, GroupContent content,
                                           const std::uint8_t* plaintext, std::size_t size) {
    if (!valid(expected_) || !same_route(data.route, expected_)) {
        return {{Error::invalid_scope, std::nullopt}, std::nullopt};
    }
    if (data.message_id == 0 || data.counter == 0 || view.roster_version == 0) {
        return {{Error::invalid_frame, std::nullopt}, std::nullopt};
    }

    const auto result = state_.receive(standing, view, content, plaintext, size);
    if (result.error != Error::none || !result.disposition) return {result, std::nullopt};
    const auto outcome = result.disposition->outcome;
    if (outcome != GroupReceive::shown && outcome != GroupReceive::duplicate) {
        return {result, std::nullopt};
    }

    GroupAckReply reply{{reversed(expected_), data.message_id}, {}};
    put_be(reply.payload.data(), kCounterBytes, data.counter);
    put_be(reply.payload.data() + kCounterBytes, kRosterVersionBytes, view.roster_version);
    std::copy(view.digest_prefix.begin(), view.digest_prefix.end(), reply.payload.begin() + kDigestOffset);
    return {result, reply};
}

std::optional<GroupBoardReceipt> match_group_ack(const GroupDataOperation& pending, const GroupAckEnvelope& ack,
                                                 const std::uint8_t* plaintext, std::size_t size) {
    if (plaintext == nullptr || size != kGroupAckPlaintextBytes || pending.message_id == 0 ||
        pending.counter == 0 || !valid(pending.route) || ack.fragment_index != 0 || ack.fragment_count != 1 ||
        !same_route(ack.route, reversed(pending.route)) || ack.message_id != pending.message_id ||
        get_be(plaintext, kCounterBytes) != pending.counter) {
        return std::nullopt;
    }

    const auto roster_version = static_cast<std::uint32_t>(get_be(plaintext + kCounterBytes, kRosterVersionBytes));
    if (roster_version == 0) return std::nullopt;
    GroupBoardReceipt receipt{pending, pending.route.recipient, roster_version, {}};
    std::copy_n(plaintext + kDigestOffset, kGroupDigestPrefixBytes, receipt.digest_prefix.begin());
    return receipt;
}

}  // namespace opentrail::delivery
