#include "opentrail/priority_queue.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {

PriorityTrafficQueue::PriorityTrafficQueue(PriorityQueuePolicy policy)
    : policy_(policy) {}

std::size_t PriorityTrafficQueue::priority_index(TrafficPriority priority) {
    return static_cast<std::size_t>(priority);
}

bool PriorityTrafficQueue::urgent(TrafficPriority priority) {
    return priority == TrafficPriority::emergency ||
           priority == TrafficPriority::critical;
}

bool PriorityTrafficQueue::valid_message_class(MessageClass message_class) {
    switch (message_class) {
        case MessageClass::emergency:
        case MessageClass::critical_alert:
        case MessageClass::direct_message:
        case MessageClass::chat:
        case MessageClass::position:
        case MessageClass::status:
            return true;
    }
    return false;
}

bool PriorityTrafficQueue::higher_priority(
    TrafficPriority left,
    TrafficPriority right) {
    return priority_index(left) < priority_index(right);
}

std::uint64_t PriorityTrafficQueue::saturating_add(
    std::uint64_t value,
    std::uint32_t increment) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum - increment ? maximum : value + increment;
}

bool PriorityTrafficQueue::valid_policy() const {
    if (policy_.capacity == 0 ||
        policy_.capacity > kPriorityQueueMaximumCapacity ||
        policy_.reserved_urgent_slots > policy_.capacity) {
        return false;
    }
    return std::all_of(
        policy_.rate_limits.begin(),
        policy_.rate_limits.end(),
        [](const PriorityRateLimit& limit) {
            return limit.maximum_accepted > 0 && limit.window_ms > 0;
        });
}

bool PriorityTrafficQueue::rate_available(
    TrafficPriority priority,
    std::uint64_t now_ms) {
    const auto index = priority_index(priority);
    if (index >= rates_.size()) {
        return false;
    }
    auto& state = rates_[index];
    const auto& limit = policy_.rate_limits[index];
    if (!state.initialized || now_ms < state.window_started_ms ||
        now_ms - state.window_started_ms >= limit.window_ms) {
        state.window_started_ms = now_ms;
        state.accepted = 0;
        state.initialized = true;
    }
    return state.accepted < limit.maximum_accepted;
}

void PriorityTrafficQueue::record_rate(TrafficPriority priority) {
    ++rates_[priority_index(priority)].accepted;
}

void PriorityTrafficQueue::push_event(const PriorityQueueEvent& event) {
    if (event_count_ == kPriorityQueueEventCapacity) {
        ++counters_.events_dropped;
        return;
    }
    events_[event_tail_] = event;
    event_tail_ = (event_tail_ + 1) % kPriorityQueueEventCapacity;
    ++event_count_;
}

void PriorityTrafficQueue::remove_entry(std::size_t index) {
    entries_[index] = {};
    --queue_count_;
}

void PriorityTrafficQueue::purge_expired(std::uint64_t now_ms) {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (!entry.used || now_ms < entry.traffic.expires_at_ms) {
            continue;
        }
        push_event({
            entry.traffic.message_id,
            entry.traffic.message_class,
            entry.traffic.priority,
            PriorityQueueEventReason::expired,
        });
        ++counters_.expired;
        remove_entry(index);
    }
}

PriorityEnqueueResult PriorityTrafficQueue::enqueue(
    std::uint32_t message_id,
    MessageClass message_class,
    radio::ByteView frame,
    std::uint64_t now_ms,
    std::uint32_t lifetime_ms) {
    if (!valid_policy()) {
        return {PriorityQueueError::invalid_policy, false, 0};
    }
    if (message_id == 0 || !valid_message_class(message_class) ||
        frame.data == nullptr || frame.size == 0 || lifetime_ms == 0) {
        return {PriorityQueueError::invalid_argument, false, 0};
    }
    const auto priority = priority_for(message_class);
    if (frame.size > radio::kMaximumFrameBytes) {
        return {PriorityQueueError::payload_too_large, false, 0};
    }

    purge_expired(now_ms);
    for (const auto& entry : entries_) {
        if (entry.used && entry.traffic.message_id == message_id) {
            return {PriorityQueueError::duplicate_message_id, false, 0};
        }
    }
    if (!rate_available(priority, now_ms)) {
        ++counters_.rejected_rate_limited;
        return {PriorityQueueError::rate_limited, false, 0};
    }

    std::size_t preempt_index = entries_.size();
    if (queue_count_ >= policy_.capacity) {
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            const auto& entry = entries_[index];
            if (!entry.used || !higher_priority(priority, entry.traffic.priority)) {
                continue;
            }
            if (preempt_index == entries_.size() ||
                priority_index(entry.traffic.priority) >
                    priority_index(entries_[preempt_index].traffic.priority) ||
                (entry.traffic.priority ==
                     entries_[preempt_index].traffic.priority &&
                 entry.sequence < entries_[preempt_index].sequence)) {
                preempt_index = index;
            }
        }
        if (preempt_index == entries_.size()) {
            ++counters_.rejected_full;
            return {PriorityQueueError::queue_full, false, 0};
        }
    }

    if (!urgent(priority) && policy_.reserved_urgent_slots > 0) {
        std::size_t nonurgent_count = 0;
        for (const auto& entry : entries_) {
            if (entry.used && !urgent(entry.traffic.priority)) {
                ++nonurgent_count;
            }
        }
        std::size_t projected_nonurgent = nonurgent_count + 1;
        if (preempt_index != entries_.size() &&
            !urgent(entries_[preempt_index].traffic.priority)) {
            --projected_nonurgent;
        }
        if (projected_nonurgent >
            policy_.capacity - policy_.reserved_urgent_slots) {
            ++counters_.rejected_reserved;
            return {PriorityQueueError::reserved_capacity, false, 0};
        }
    }

    PriorityEnqueueResult result{};
    if (preempt_index != entries_.size()) {
        const auto preempted = entries_[preempt_index].traffic;
        result.preempted = true;
        result.preempted_message_id = preempted.message_id;
        push_event({
            preempted.message_id,
            preempted.message_class,
            preempted.priority,
            PriorityQueueEventReason::preempted,
        });
        ++counters_.preempted;
        remove_entry(preempt_index);
    }

    for (auto& entry : entries_) {
        if (entry.used) {
            continue;
        }
        entry.traffic.message_id = message_id;
        entry.traffic.message_class = message_class;
        entry.traffic.priority = priority;
        std::copy_n(frame.data, frame.size, entry.traffic.frame.begin());
        entry.traffic.frame_size = frame.size;
        entry.traffic.created_at_ms = now_ms;
        entry.traffic.expires_at_ms = saturating_add(now_ms, lifetime_ms);
        entry.sequence = next_sequence_++;
        entry.used = true;
        ++queue_count_;
        ++counters_.accepted;
        record_rate(priority);
        return result;
    }

    ++counters_.rejected_full;
    return {PriorityQueueError::queue_full, result.preempted,
            result.preempted_message_id};
}

QueuedTrafficResult PriorityTrafficQueue::take_next(std::uint64_t now_ms) {
    purge_expired(now_ms);
    std::size_t selected = entries_.size();
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (!entry.used) {
            continue;
        }
        if (selected == entries_.size() ||
            higher_priority(
                entry.traffic.priority,
                entries_[selected].traffic.priority) ||
            (entry.traffic.priority == entries_[selected].traffic.priority &&
             entry.sequence < entries_[selected].sequence)) {
            selected = index;
        }
    }
    if (selected == entries_.size()) {
        return {};
    }
    const auto traffic = entries_[selected].traffic;
    remove_entry(selected);
    return {true, traffic};
}

PriorityQueueEventResult PriorityTrafficQueue::next_event() {
    if (event_count_ == 0) {
        return {};
    }
    const auto event = events_[event_head_];
    events_[event_head_] = {};
    event_head_ = (event_head_ + 1) % kPriorityQueueEventCapacity;
    --event_count_;
    return {true, event};
}

PriorityQueueStatus PriorityTrafficQueue::status() const {
    auto status = counters_;
    status.queued = queue_count_;
    status.events_waiting = event_count_;
    return status;
}

}  // namespace opentrail::delivery
