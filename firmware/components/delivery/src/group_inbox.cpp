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

GroupReceiveResult GroupInbox::receive(const GroupMemberId& sender, const SenderStanding& standing,
                                       const GroupView& view, GroupContent content,
                                       const DecodedGroupFrame& frame) {
    GroupReceiveResult r{};
    if (!standing.on_current_roster) { r.outcome = GroupReceive::not_member; return r; }
    if (!standing.pair_active) { r.outcome = GroupReceive::no_pair; return r; }
    const GroupFrameHeader& h = frame.header;
    if (h.roster_version > view.roster_version) { r.outcome = GroupReceive::roster_newer; return r; }
    if (h.roster_version == view.roster_version && h.digest_prefix != view.digest_prefix) {
        r.outcome = GroupReceive::roster_differs;
        return r;
    }
    r.sender_roster_older = h.roster_version < view.roster_version;
    if (content == GroupContent::position && !view.location_allowed) {
        r.outcome = GroupReceive::position_refused;
        return r;
    }
    const std::uint64_t sum = checksum(content, frame.body, frame.body_size);
    const std::uint32_t n = h.message_number;
    Sender* s = find(sender);
    if (s == nullptr) {
        for (Sender& free : senders_) {
            if (!free.used) { s = &free; break; }
        }
        if (s == nullptr) { r.outcome = GroupReceive::capacity; return r; }
        *s = Sender{};
        s->used = true;
        s->id = sender;
        s->first = n;
        s->highest = n;
        s->seen = 1;
        s->sums[0] = sum;
        r.outcome = GroupReceive::shown;
        return r;
    }
    if (n > s->highest) {
        const std::uint32_t shift = n - s->highest;
        r.missing_before = shift - 1;
        if (shift >= kGroupInboxWindow) {
            s->seen = 0;
            s->sums = {};
        } else {
            s->seen <<= shift;
            for (std::uint32_t i = kGroupInboxWindow; i-- > shift;) s->sums[i] = s->sums[i - shift];
            for (std::uint32_t i = 0; i < shift; ++i) s->sums[i] = 0;
        }
        s->highest = n;
        s->seen |= 1U;
        s->sums[0] = sum;
        r.outcome = GroupReceive::shown;
        return r;
    }
    const std::uint32_t back = s->highest - n;
    if (back >= kGroupInboxWindow) {
        r.outcome = GroupReceive::too_old;
        return r;
    }
    const std::uint32_t bit = 1U << back;
    if ((s->seen & bit) != 0) {
        r.outcome = s->sums[back] == sum ? GroupReceive::duplicate : GroupReceive::conflict;
        return r;
    }
    s->seen |= bit;
    s->sums[back] = sum;
    if (n < s->first) s->first = n;  // arrived out of order before the first one seen
    r.outcome = GroupReceive::shown;  // a late arrival fills its gap
    return r;
}

std::uint32_t GroupInbox::missing(const GroupMemberId& sender) const {
    const Sender* s = find(sender);
    if (s == nullptr) return 0;
    const std::uint32_t span = std::min<std::uint32_t>(kGroupInboxWindow, s->highest - s->first + 1);
    const std::uint32_t mask = span >= 32 ? 0xFFFFFFFFU : ((1U << span) - 1U);
    return static_cast<std::uint32_t>(span - popcount(s->seen & mask));
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
