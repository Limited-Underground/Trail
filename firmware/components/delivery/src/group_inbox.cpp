#include "opentrail/group_inbox.hpp"

#include <algorithm>

namespace opentrail::delivery {
namespace {

// FNV-1a 64 over the content type and body. Not cryptographic (see the header).
std::uint64_t checksum(GroupContent content, const std::uint8_t* body, std::size_t size) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&h](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001B3ULL;
    };
    mix(static_cast<std::uint8_t>(content));
    for (std::size_t i = 0; i < size; ++i) mix(body[i]);
    return h;
}

std::size_t popcount(std::uint32_t v) {
    std::size_t n = 0;
    for (; v != 0; v &= v - 1) ++n;
    return n;
}

}  // namespace

// ------------------------------------------------------------------ GroupInbox

GroupInbox::Sender* GroupInbox::find(const GroupMemberId& id) {
    for (Sender& s : senders_) {
        if (s.used && s.id == id) return &s;
    }
    return nullptr;
}

const GroupInbox::Sender* GroupInbox::find(const GroupMemberId& id) const {
    for (const Sender& s : senders_) {
        if (s.used && s.id == id) return &s;
    }
    return nullptr;
}

GroupReceiveResult GroupInbox::admit(const SenderStanding& standing, const GroupView& view,
                                    GroupContent content, const DecodedGroupFrame& frame) {
    GroupReceiveResult result{};
    if (!standing.on_current_roster) { result.outcome = GroupReceive::not_member; return result; }
    if (!standing.pair_active) { result.outcome = GroupReceive::no_pair; return result; }
    const GroupFrameHeader& header = frame.header;
    if (header.roster_version > view.roster_version) {
        result.outcome = GroupReceive::roster_newer;
        return result;
    }
    if (header.roster_version == view.roster_version && header.digest_prefix != view.digest_prefix) {
        result.outcome = GroupReceive::roster_differs;
        return result;
    }
    result.sender_roster_older = header.roster_version < view.roster_version;
    result.outcome = content == GroupContent::position && !view.location_allowed
                         ? GroupReceive::position_refused
                         : GroupReceive::shown;
    return result;
}

void GroupInbox::observe(Window& window, GroupContent content, const DecodedGroupFrame& frame,
                         GroupReceiveResult& result) {
    const std::uint32_t number = frame.header.message_number;
    const std::uint64_t sum = checksum(content, frame.body, frame.body_size);
    if (window.first == 0) {
        window.first = number;
        window.highest = number;
        window.seen = 1;
        window.sums[0] = sum;
        return;
    }
    if (number > window.highest) {
        const std::uint32_t shift = number - window.highest;
        result.missing_before = shift - 1;
        if (shift >= kGroupInboxWindow) {
            window.seen = 0;
            window.sums = {};
        } else {
            window.seen <<= shift;
            for (std::uint32_t i = kGroupInboxWindow; i-- > shift;) window.sums[i] = window.sums[i - shift];
            for (std::uint32_t i = 0; i < shift; ++i) window.sums[i] = 0;
        }
        window.highest = number;
        window.seen |= 1U;
        window.sums[0] = sum;
        return;
    }
    const std::uint32_t back = window.highest - number;
    if (back >= kGroupInboxWindow) {
        result.outcome = GroupReceive::too_old;
        return;
    }
    const std::uint32_t bit = 1U << back;
    if ((window.seen & bit) != 0) {
        result.outcome = window.sums[back] == sum ? GroupReceive::duplicate : GroupReceive::conflict;
        return;
    }
    window.seen |= bit;
    window.sums[back] = sum;
    if (number < window.first) window.first = number;
}

std::uint32_t GroupInbox::count_missing(const Window& window) {
    if (window.first == 0) return 0;
    const std::uint32_t span = std::min(kGroupInboxWindow, window.highest - window.first + 1);
    const std::uint32_t mask = span >= kGroupInboxWindow ? 0xFFFFFFFFU : ((1U << span) - 1U);
    return static_cast<std::uint32_t>(span - popcount(window.seen & mask));
}

GroupReceiveResult GroupInbox::receive(const GroupMemberId& sender, const SenderStanding& standing,
                                      const GroupView& view, GroupContent content,
                                      const DecodedGroupFrame& frame) {
    auto result = admit(standing, view, content, frame);
    if (result.outcome != GroupReceive::shown) return result;

    Sender* slot = find(sender);
    if (slot == nullptr) {
        const auto free = std::find_if(senders_.begin(), senders_.end(), [](const Sender& candidate) {
            return !candidate.used;
        });
        if (free == senders_.end()) { result.outcome = GroupReceive::capacity; return result; }
        *free = Sender{true, sender, {}};
        slot = &*free;
    }
    observe(slot->window, content, frame, result);
    return result;
}

std::uint32_t GroupInbox::missing(const GroupMemberId& sender) const {
    const Sender* slot = find(sender);
    return slot != nullptr ? count_missing(slot->window) : 0;
}

void GroupInbox::forget(const GroupMemberId& sender) {
    if (Sender* s = find(sender)) *s = Sender{};
}

void GroupInbox::clear() { senders_ = {}; }

// ------------------------------------------------------------------ GroupOutbox

GroupOutbox::Message* GroupOutbox::find(std::uint32_t number) {
    for (Message& m : messages_) {
        if (m.used && m.number == number) return &m;
    }
    return nullptr;
}

const GroupOutbox::Message* GroupOutbox::find(std::uint32_t number) const {
    for (const Message& m : messages_) {
        if (m.used && m.number == number) return &m;
    }
    return nullptr;
}

GroupOutboxError GroupOutbox::track(std::uint32_t number, const GroupMemberId* recipients, std::size_t count,
                                    std::optional<std::uint32_t>& evicted) {
    evicted.reset();
    if (number == 0 || recipients == nullptr || count == 0 || count > kGroupOtherMembers) {
        return GroupOutboxError::invalid_argument;
    }
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (recipients[i] == recipients[j]) return GroupOutboxError::invalid_argument;
        }
    }
    if (find(number) != nullptr) return GroupOutboxError::duplicate_number;
    Message* slot = nullptr;
    for (Message& m : messages_) {
        if (!m.used) { slot = &m; break; }
    }
    if (slot == nullptr) {
        // Oldest fully delivered message first, otherwise the oldest.
        Message* done = nullptr;
        Message* oldest = nullptr;
        for (Message& m : messages_) {
            const bool complete = m.delivered == static_cast<std::uint8_t>((1U << m.count) - 1U);
            if (complete && (done == nullptr || m.number < done->number)) done = &m;
            if (oldest == nullptr || m.number < oldest->number) oldest = &m;
        }
        slot = done != nullptr ? done : oldest;
        evicted = slot->number;
    }
    *slot = Message{};
    slot->used = true;
    slot->number = number;
    slot->count = count;
    std::copy(recipients, recipients + count, slot->recipients.begin());
    return GroupOutboxError::none;
}

GroupOutboxError GroupOutbox::delivered(std::uint32_t number, const GroupMemberId& member) {
    Message* m = find(number);
    if (m == nullptr) return GroupOutboxError::unknown_message;
    for (std::size_t i = 0; i < m->count; ++i) {
        if (m->recipients[i] == member) {
            m->delivered = static_cast<std::uint8_t>(m->delivered | (1U << i));
            return GroupOutboxError::none;
        }
    }
    return GroupOutboxError::not_recipient;
}

std::optional<GroupDeliveryStatus> GroupOutbox::status(std::uint32_t number) const {
    const Message* m = find(number);
    if (m == nullptr) return std::nullopt;
    return GroupDeliveryStatus{popcount(m->delivered), m->count};
}

void GroupOutbox::drop_member(const GroupMemberId& member) {
    for (Message& m : messages_) {
        if (!m.used) continue;
        for (std::size_t i = 0; i < m.count; ++i) {
            if (m.recipients[i] != member) continue;
            // Close the gap in both the recipient list and the delivered bits.
            const std::uint8_t low = static_cast<std::uint8_t>(m.delivered & ((1U << i) - 1U));
            const std::uint8_t high = static_cast<std::uint8_t>((m.delivered >> (i + 1)) << i);
            m.delivered = static_cast<std::uint8_t>(low | high);
            for (std::size_t k = i; k + 1 < m.count; ++k) m.recipients[k] = m.recipients[k + 1];
            m.recipients[m.count - 1] = GroupMemberId{};
            --m.count;
            break;
        }
    }
}

void GroupOutbox::clear() { messages_ = {}; }

}  // namespace opentrail::delivery
