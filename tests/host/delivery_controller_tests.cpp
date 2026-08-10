#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_radio_transport.hpp"
#include "opentrail/delivery_controller.hpp"

namespace {

using opentrail::delivery::DeliveryController;
using opentrail::delivery::DeliveryError;
using opentrail::delivery::DeliveryOutcome;
using opentrail::delivery::DeliveryPolicy;
using opentrail::delivery::MessageClass;
using opentrail::delivery::experimental_policy;
using opentrail::delivery::kTransientRejectionBackoffMs;
using opentrail::radio::RadioError;
using opentrail::radio::test_support::FakeRadioTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

const std::array<std::uint8_t, 3> frame{1, 2, 3};

void test_policies_by_message_class() {
    const auto emergency = experimental_policy(MessageClass::emergency);
    const auto critical = experimental_policy(MessageClass::critical_alert);
    const auto direct = experimental_policy(MessageClass::direct_message);
    const auto chat = experimental_policy(MessageClass::chat);
    const auto position = experimental_policy(MessageClass::position);
    const auto status = experimental_policy(MessageClass::status);
    EXPECT(emergency.requires_acknowledgement && emergency.maximum_attempts == 4);
    EXPECT(critical.requires_acknowledgement && critical.maximum_attempts == 4);
    EXPECT(direct.requires_acknowledgement && direct.maximum_attempts == 3);
    EXPECT(chat.requires_acknowledgement && chat.maximum_attempts == 3);
    EXPECT(!position.requires_acknowledgement && position.maximum_attempts == 1);
    EXPECT(!status.requires_acknowledgement && status.maximum_attempts == 1);
}

void test_acknowledged_delivery() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    EXPECT(controller.enqueue(
               1,
               MessageClass::direct_message,
               {true, 3, 100, 1000},
               {frame.data(), frame.size()},
               0)
               .accepted());
    EXPECT(!controller.acknowledge(1, 0));
    controller.service(0);
    EXPECT(controller.status().pending == 1);
    EXPECT(controller.acknowledge(1, 50));
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::confirmed);
    EXPECT(event.event.attempts == 1);
    EXPECT(controller.status().confirmed == 1);
}

void test_retry_then_attempts_exhausted() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    EXPECT(controller.enqueue(
               2,
               MessageClass::chat,
               {true, 2, 100, 1000},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    controller.service(99);
    EXPECT(controller.status().pending == 1);
    controller.service(100);
    controller.service(199);
    EXPECT(controller.status().pending == 1);
    controller.service(200);
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::attempts_exhausted);
    EXPECT(event.event.attempts == 2);
}

void test_expiry_wins_over_later_retry() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    EXPECT(controller.enqueue(
               3,
               MessageClass::emergency,
               {true, 4, 100, 150},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    controller.service(100);
    controller.service(150);
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::expired);
    EXPECT(event.event.attempts == 2);
}

void test_unacknowledged_class_sends_once() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    EXPECT(controller.enqueue(
               4,
               MessageClass::position,
               {false, 1, 0, 1000},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::sent_unconfirmed);
    EXPECT(event.event.attempts == 1);
    EXPECT(controller.status().pending == 0);
}

void test_transient_transport_rejection_does_not_burn_attempt() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    radio.fail_next_send(RadioError::busy);
    EXPECT(controller.enqueue(
               5,
               MessageClass::chat,
               {true, 2, 100, 1000},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    EXPECT(controller.status().pending == 1);
    controller.service(99);
    EXPECT(radio.status().transmit_queue_depth == 0);
    controller.service(100);
    EXPECT(radio.status().transmit_queue_depth == 1);
    EXPECT(controller.acknowledge(5, 150));
    const auto event = controller.next_event();
    EXPECT(event.event.attempts == 1);
}

void test_unacknowledged_transient_rejection_uses_bounded_backoff() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    radio.fail_next_send(RadioError::queue_full);
    EXPECT(controller.enqueue(
               55,
               MessageClass::status,
               {false, 1, 0, 1000},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    controller.service(kTransientRejectionBackoffMs - 1);
    EXPECT(radio.status().transmit_queue_depth == 0);
    controller.service(kTransientRejectionBackoffMs);
    EXPECT(radio.status().transmit_queue_depth == 1);
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::sent_unconfirmed);
    EXPECT(event.event.attempts == 1);
}

void test_permanent_transport_rejection_and_enqueue_validation() {
    FakeRadioTransport radio(3);
    DeliveryController controller(radio);
    EXPECT(controller.enqueue(
               0,
               MessageClass::status,
               {false, 1, 0, 10},
               {frame.data(), frame.size()},
               0)
               .error == DeliveryError::invalid_argument);
    EXPECT(controller.enqueue(
               1,
               MessageClass::status,
               {false, 2, 0, 10},
               {frame.data(), frame.size()},
               0)
               .error == DeliveryError::invalid_policy);

    radio.fail_next_send(RadioError::internal_failure);
    EXPECT(controller.enqueue(
               6,
               MessageClass::status,
               {false, 1, 0, 100},
               {frame.data(), frame.size()},
               0)
               .accepted());
    controller.service(0);
    const auto event = controller.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::transport_rejected);
    EXPECT(event.event.transport_error == RadioError::internal_failure);
    EXPECT(event.event.attempts == 0);
}

void test_duplicate_message_id_and_queue_capacity() {
    FakeRadioTransport radio(32);
    DeliveryController controller(radio);
    const DeliveryPolicy policy{true, 2, 100, 1000};
    EXPECT(controller.enqueue(
               10,
               MessageClass::chat,
               policy,
               {frame.data(), frame.size()},
               0)
               .accepted());
    EXPECT(controller.enqueue(
               10,
               MessageClass::chat,
               policy,
               {frame.data(), frame.size()},
               0)
               .error == DeliveryError::duplicate_message_id);
    for (std::uint32_t id = 11;
         id < 10 + DeliveryController::kPendingCapacity;
         ++id) {
        EXPECT(controller.enqueue(
                   id,
                   MessageClass::chat,
                   policy,
                   {frame.data(), frame.size()},
                   0)
                   .accepted());
    }
    EXPECT(controller.enqueue(
               99,
               MessageClass::chat,
               policy,
               {frame.data(), frame.size()},
               0)
               .error == DeliveryError::queue_full);
}

}  // namespace

int main() {
    test_policies_by_message_class();
    test_acknowledged_delivery();
    test_retry_then_attempts_exhausted();
    test_expiry_wins_over_later_retry();
    test_unacknowledged_class_sends_once();
    test_transient_transport_rejection_does_not_burn_attempt();
    test_unacknowledged_transient_rejection_uses_bounded_backoff();
    test_permanent_transport_rejection_and_enqueue_validation();
    test_duplicate_message_id_and_queue_capacity();

    if (failures != 0) {
        std::cerr << failures << " delivery controller assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 delivery controller scenarios\n";
    return EXIT_SUCCESS;
}
