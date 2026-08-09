#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_radio_transport.hpp"
#include "opentrail/delivery_controller.hpp"
#include "opentrail/priority_queue.hpp"

namespace {

using opentrail::delivery::DeliveryController;
using opentrail::delivery::DeliveryOutcome;
using opentrail::delivery::MessageClass;
using opentrail::delivery::PriorityTrafficQueue;
using opentrail::delivery::TrafficPriority;
using opentrail::radio::test_support::FakeRadioTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_emergency_reaches_delivery_before_normal_backlog() {
    const auto policy = opentrail::delivery::experimental_priority_policy();
    PriorityTrafficQueue priority_queue(policy);
    FakeRadioTransport radio(32);
    DeliveryController delivery(radio);
    const std::array<std::uint8_t, 1> normal_frame{0x10};
    const std::array<std::uint8_t, 1> emergency_frame{0xEE};

    EXPECT(priority_queue.enqueue(
               1,
               MessageClass::chat,
               {normal_frame.data(), normal_frame.size()},
               0,
               1000)
               .accepted());
    EXPECT(priority_queue.enqueue(
               2,
               MessageClass::emergency,
               {emergency_frame.data(), emergency_frame.size()},
               1,
               1000)
               .accepted());

    const auto selected = priority_queue.take_next(2);
    EXPECT(selected.has_message);
    EXPECT(selected.message.message_id == 2);
    EXPECT(selected.message.frame[0] == 0xEE);
    EXPECT(delivery.enqueue(
               selected.message.message_id,
               selected.message.message_class,
               {true, 2, 100, 500},
               {selected.message.frame.data(), selected.message.frame_size},
               2)
               .accepted());
    delivery.service(2);
    EXPECT(delivery.acknowledge(2, 10));
    const auto event = delivery.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::confirmed);
    EXPECT(priority_queue.take_next(10).message.message_id == 1);
}

}  // namespace

int main() {
    test_emergency_reaches_delivery_before_normal_backlog();
    if (failures != 0) {
        std::cerr << failures << " priority/delivery integration assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: emergency priority/delivery integration scenario\n";
    return EXIT_SUCCESS;
}
