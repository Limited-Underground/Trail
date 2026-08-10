#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/priority_queue.hpp"

namespace {

using opentrail::delivery::MessageClass;
using opentrail::delivery::PriorityQueueError;
using opentrail::delivery::PriorityQueueEventReason;
using opentrail::delivery::PriorityQueuePolicy;
using opentrail::delivery::PriorityRateLimit;
using opentrail::delivery::PriorityTrafficQueue;
using opentrail::delivery::TrafficPriority;
using opentrail::delivery::priority_for;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

const std::array<std::uint8_t, 1> frame{0x42};

PriorityQueuePolicy policy(
    std::size_t capacity = 4,
    std::size_t reserve = 1,
    std::uint16_t rate = 20,
    std::uint32_t window = 1000) {
    return {
        capacity,
        reserve,
        {{{rate, window}, {rate, window}, {rate, window}, {rate, window}}},
    };
}

auto enqueue(
    PriorityTrafficQueue& queue,
    std::uint32_t id,
    TrafficPriority priority,
    std::uint64_t now = 0,
    std::uint32_t lifetime = 1000) {
    MessageClass message_class = MessageClass::status;
    switch (priority) {
        case TrafficPriority::emergency:
            message_class = MessageClass::emergency;
            break;
        case TrafficPriority::critical:
            message_class = MessageClass::critical_alert;
            break;
        case TrafficPriority::normal:
            message_class = MessageClass::chat;
            break;
        case TrafficPriority::background:
            message_class = MessageClass::status;
            break;
    }
    return queue.enqueue(
        id,
        message_class,
        {frame.data(), frame.size()},
        now,
        lifetime);
}

void test_reserved_capacity_blocks_normal_but_accepts_urgent() {
    PriorityTrafficQueue queue(policy(4, 1));
    EXPECT(enqueue(queue, 1, TrafficPriority::normal).accepted());
    EXPECT(enqueue(queue, 2, TrafficPriority::normal).accepted());
    EXPECT(enqueue(queue, 3, TrafficPriority::background).accepted());
    EXPECT(enqueue(queue, 4, TrafficPriority::normal).error ==
           PriorityQueueError::reserved_capacity);
    EXPECT(enqueue(queue, 5, TrafficPriority::emergency).accepted());
    EXPECT(queue.status().queued == 4);
}

void test_urgent_use_of_reserve_does_not_waste_other_capacity() {
    PriorityTrafficQueue queue(policy(4, 1));
    EXPECT(enqueue(queue, 6, TrafficPriority::emergency).accepted());
    EXPECT(enqueue(queue, 7, TrafficPriority::normal).accepted());
    EXPECT(enqueue(queue, 8, TrafficPriority::normal).accepted());
    EXPECT(enqueue(queue, 9, TrafficPriority::normal).accepted());
    EXPECT(queue.status().queued == 4);
}

void test_higher_priority_preempts_oldest_lowest_priority() {
    PriorityTrafficQueue queue(policy(3, 0));
    EXPECT(enqueue(queue, 10, TrafficPriority::background, 0).accepted());
    EXPECT(enqueue(queue, 11, TrafficPriority::background, 1).accepted());
    EXPECT(enqueue(queue, 12, TrafficPriority::normal, 2).accepted());
    const auto urgent = enqueue(queue, 13, TrafficPriority::emergency, 3);
    EXPECT(urgent.accepted());
    EXPECT(urgent.preempted);
    EXPECT(urgent.preempted_message_id == 10);
    const auto event = queue.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.message_id == 10);
    EXPECT(event.event.reason == PriorityQueueEventReason::preempted);
}

void test_equal_or_lower_priority_cannot_preempt() {
    PriorityTrafficQueue queue(policy(2, 0));
    EXPECT(enqueue(queue, 20, TrafficPriority::emergency).accepted());
    EXPECT(enqueue(queue, 21, TrafficPriority::critical).accepted());
    EXPECT(enqueue(queue, 22, TrafficPriority::critical).error ==
           PriorityQueueError::queue_full);
    EXPECT(enqueue(queue, 23, TrafficPriority::normal).error ==
           PriorityQueueError::queue_full);
}

void test_take_next_is_priority_then_fifo() {
    PriorityTrafficQueue queue(policy(6, 0));
    EXPECT(enqueue(queue, 30, TrafficPriority::normal, 0).accepted());
    EXPECT(enqueue(queue, 31, TrafficPriority::critical, 1).accepted());
    EXPECT(enqueue(queue, 32, TrafficPriority::critical, 2).accepted());
    EXPECT(enqueue(queue, 33, TrafficPriority::emergency, 3).accepted());
    EXPECT(queue.take_next(4).message.message_id == 33);
    EXPECT(queue.take_next(4).message.message_id == 31);
    EXPECT(queue.take_next(4).message.message_id == 32);
    EXPECT(queue.take_next(4).message.message_id == 30);
}

void test_rate_limit_is_visible_and_window_resets() {
    auto limits = policy(4, 0);
    limits.rate_limits[static_cast<std::size_t>(TrafficPriority::emergency)] =
        PriorityRateLimit{2, 100};
    PriorityTrafficQueue queue(limits);
    EXPECT(enqueue(queue, 40, TrafficPriority::emergency, 0).accepted());
    EXPECT(enqueue(queue, 41, TrafficPriority::emergency, 1).accepted());
    EXPECT(enqueue(queue, 42, TrafficPriority::emergency, 2).error ==
           PriorityQueueError::rate_limited);
    EXPECT(queue.status().rejected_rate_limited == 1);
    EXPECT(enqueue(queue, 43, TrafficPriority::emergency, 100).accepted());
}

void test_stale_enqueue_and_expiry_are_explicit() {
    PriorityTrafficQueue queue(policy());
    EXPECT(queue.enqueue(
               0,
               MessageClass::status,
               {frame.data(), frame.size()},
               0,
               0)
               .error == PriorityQueueError::invalid_argument);
    EXPECT(enqueue(queue, 50, TrafficPriority::normal, 0, 10).accepted());
    queue.purge_expired(10);
    EXPECT(queue.status().queued == 0);
    EXPECT(queue.status().expired == 1);
    const auto event = queue.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.message_id == 50);
    EXPECT(event.event.reason == PriorityQueueEventReason::expired);
}

void test_validation_and_duplicate_ids() {
    PriorityTrafficQueue invalid({});
    EXPECT(enqueue(invalid, 60, TrafficPriority::normal).error ==
           PriorityQueueError::invalid_policy);

    PriorityTrafficQueue queue(policy());
    EXPECT(enqueue(queue, 61, TrafficPriority::normal).accepted());
    EXPECT(enqueue(queue, 61, TrafficPriority::emergency).error ==
           PriorityQueueError::duplicate_message_id);
    EXPECT(queue.status().events_waiting == 0);
    EXPECT(priority_for(MessageClass::emergency) == TrafficPriority::emergency);
    EXPECT(priority_for(MessageClass::critical_alert) == TrafficPriority::critical);
    EXPECT(priority_for(MessageClass::chat) == TrafficPriority::normal);
    EXPECT(priority_for(MessageClass::status) == TrafficPriority::background);
}

void test_emergency_preempts_critical_before_other_emergency() {
    PriorityTrafficQueue queue(policy(3, 0));
    EXPECT(enqueue(queue, 70, TrafficPriority::emergency, 0).accepted());
    EXPECT(enqueue(queue, 71, TrafficPriority::critical, 1).accepted());
    EXPECT(enqueue(queue, 72, TrafficPriority::critical, 2).accepted());
    const auto next = enqueue(queue, 73, TrafficPriority::emergency, 3);
    EXPECT(next.accepted());
    EXPECT(next.preempted_message_id == 71);
}

}  // namespace

int main() {
    test_reserved_capacity_blocks_normal_but_accepts_urgent();
    test_urgent_use_of_reserve_does_not_waste_other_capacity();
    test_higher_priority_preempts_oldest_lowest_priority();
    test_equal_or_lower_priority_cannot_preempt();
    test_take_next_is_priority_then_fifo();
    test_rate_limit_is_visible_and_window_resets();
    test_stale_enqueue_and_expiry_are_explicit();
    test_validation_and_duplicate_ids();
    test_emergency_preempts_critical_before_other_emergency();

    if (failures != 0) {
        std::cerr << failures << " priority queue assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 priority queue scenarios\n";
    return EXIT_SUCCESS;
}
