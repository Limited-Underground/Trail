#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_radio_transport.hpp"
#include "opentrail/packet_codec.hpp"
#include "opentrail/position_codec.hpp"
#include "opentrail/position_packet_admission.hpp"
#include "opentrail/priority_delivery_handoff.hpp"

namespace {

using namespace opentrail::delivery;
using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::protocol;
using namespace opentrail::radio::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr std::array<std::uint8_t, 1> kSmallFrame{{0x42}};

PriorityQueuePolicy queue_policy(
    std::size_t capacity = 8,
    std::size_t reserve = 0) {
    return {
        capacity,
        reserve,
        {{{20, 1000}, {20, 1000}, {20, 1000}, {20, 1000}}},
    };
}

std::array<std::uint8_t, 38> position_frame(
    std::uint32_t message_id = 1) {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = 100;
    snapshot.fix.longitude_e7 = -200;
    snapshot.fix.horizontal_accuracy_cm = 100;
    snapshot.fix.horizontal_accuracy_valid = true;
    std::array<std::uint8_t, kPositionPayloadBytes> payload{};
    EXPECT(encode_position(snapshot, {payload.data(), payload.size()}).encoded());

    std::array<std::uint8_t, 38> frame{};
    const PacketView packet{
        {kExperimentalPacketVersion,
         PacketType::position,
         0,
         0xA001,
         0xB001,
         message_id},
        {payload.data(), payload.size()},
    };
    EXPECT(encode_packet(packet, {frame.data(), frame.size()}).encoded());
    return frame;
}

class OneMetadata final : public PositionPacketMetadataSource {
public:
    explicit OneMetadata(std::uint32_t message_id) : message_id_(message_id) {}

    PositionPacketMetadata next() override {
        if (used_) {
            return {};
        }
        used_ = true;
        return {
            PositionPacketMetadataError::none,
            0xA001,
            0xB001,
            message_id_,
        };
    }

private:
    std::uint32_t message_id_{0};
    bool used_{false};
};

LocationSnapshot current_snapshot() {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = 449775000;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.age_ms = 25;
    return snapshot;
}

void test_peek_and_commit_require_the_selected_message() {
    PriorityTrafficQueue queue{queue_policy()};
    EXPECT(queue.enqueue(
               1,
               MessageClass::chat,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    EXPECT(queue.enqueue(
               2,
               MessageClass::critical_alert,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    const auto selected = queue.peek_next(0);
    EXPECT(selected.has_message);
    EXPECT(selected.message.message_id == 2);
    EXPECT(!queue.commit_next(0, 0));
    EXPECT(!queue.commit_next(1, 0));
    EXPECT(queue.status().queued == 2);
    EXPECT(queue.commit_next(2, 0));
    EXPECT(queue.status().queued == 1);
    EXPECT(queue.peek_next(0).message.message_id == 1);
}

void test_empty_queue_is_idle_without_delivery_mutation() {
    PriorityTrafficQueue queue{queue_policy()};
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto result = handoff.service(0);
    EXPECT(result.disposition == PriorityDeliveryHandoffDisposition::idle);
    EXPECT(result.message_id == 0);
    EXPECT(delivery.status().pending == 0);
    EXPECT(handoff.status().idle == 1);
}

void test_handoff_preserves_priority_selection() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(10);
    EXPECT(queue.enqueue(
               10,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               1000)
               .accepted());
    EXPECT(queue.enqueue(
               11,
               MessageClass::critical_alert,
               {kSmallFrame.data(), kSmallFrame.size()},
               1,
               1000)
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto transferred = handoff.service(2);
    EXPECT(transferred.transferred());
    EXPECT(transferred.message_id == 11);
    EXPECT(!transferred.queue_retained);
    EXPECT(delivery.status().pending == 1);
    EXPECT(queue.status().queued == 1);
    EXPECT(queue.peek_next(2).message.message_id == 10);
}

void test_full_delivery_defers_without_losing_queue_entry() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(200);
    EXPECT(queue.enqueue(
               200,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               2000)
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    for (std::uint32_t id = 100;
         id < 100 + DeliveryController::kPendingCapacity;
         ++id) {
        EXPECT(delivery.enqueue(
                   id,
                   MessageClass::chat,
                   {true, 1, 100, 500},
                   {kSmallFrame.data(), kSmallFrame.size()},
                   0)
                   .accepted());
    }
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto deferred = handoff.service(0);
    EXPECT(deferred.disposition == PriorityDeliveryHandoffDisposition::deferred);
    EXPECT(deferred.error ==
           PriorityDeliveryHandoffError::delivery_queue_full);
    EXPECT(deferred.queue_retained);
    EXPECT(queue.status().queued == 1);

    delivery.service(500);
    EXPECT(delivery.status().pending == 0);
    const auto retried = handoff.service(500);
    EXPECT(retried.transferred());
    EXPECT(queue.status().queued == 0);
    EXPECT(delivery.status().pending == 1);
    EXPECT(handoff.status().deferred == 1);
    EXPECT(handoff.status().transferred == 1);
}

void test_oversized_for_transport_fails_and_retains() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(300);
    EXPECT(queue.enqueue(
               300,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               1000)
               .accepted());
    FakeRadioTransport radio{16};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto result = handoff.service(0);
    EXPECT(result.disposition == PriorityDeliveryHandoffDisposition::failed);
    EXPECT(result.error == PriorityDeliveryHandoffError::delivery_rejected);
    EXPECT(result.delivery_error == DeliveryError::payload_too_large);
    EXPECT(result.queue_retained);
    EXPECT(queue.status().queued == 1);
    EXPECT(delivery.status().pending == 0);
}

void test_duplicate_delivery_id_fails_and_retains() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(400);
    EXPECT(queue.enqueue(
               400,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               1000)
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    EXPECT(delivery.enqueue(
               400,
               MessageClass::position,
               experimental_policy(MessageClass::position),
               {position.data(), position.size()},
               0)
               .accepted());
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto result = handoff.service(0);
    EXPECT(result.error == PriorityDeliveryHandoffError::delivery_rejected);
    EXPECT(result.delivery_error == DeliveryError::duplicate_message_id);
    EXPECT(result.queue_retained);
    EXPECT(queue.status().queued == 1);
    EXPECT(delivery.status().pending == 1);
}

void test_handoff_never_extends_original_expiry() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(500);
    EXPECT(queue.enqueue(
               500,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               10)
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    EXPECT(handoff.service(9).transferred());
    delivery.service(10);
    const auto event = delivery.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.message_id == 500);
    EXPECT(event.event.outcome == DeliveryOutcome::expired);
    EXPECT(event.event.attempts == 0);
    EXPECT(radio.status().transmit_queue_depth == 0);
}

void test_queue_expiry_wins_before_handoff() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(600);
    EXPECT(queue.enqueue(
               600,
               MessageClass::position,
               {position.data(), position.size()},
               0,
               10)
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    EXPECT(handoff.service(10).disposition ==
           PriorityDeliveryHandoffDisposition::idle);
    EXPECT(delivery.status().pending == 0);
    EXPECT(queue.status().expired == 1);
    const auto event = queue.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.message_id == 600);
    EXPECT(event.event.reason == PriorityQueueEventReason::expired);
}

void test_clock_rollback_horizon_fails_without_narrowing() {
    PriorityTrafficQueue queue{queue_policy()};
    const auto position = position_frame(700);
    EXPECT(queue.enqueue(
               700,
               MessageClass::position,
               {position.data(), position.size()},
               100,
               std::numeric_limits<std::uint32_t>::max())
               .accepted());
    FakeRadioTransport radio{};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    const auto result = handoff.service(0);
    EXPECT(result.error == PriorityDeliveryHandoffError::invalid_time);
    EXPECT(result.queue_retained);
    EXPECT(queue.status().queued == 1);
    EXPECT(delivery.status().pending == 0);
}

void test_scheduler_packet_queue_delivery_reaches_fake_peer_once() {
    PriorityTrafficQueue queue{queue_policy()};
    OneMetadata metadata{800};
    PositionPacketAdmissionSink packet_sink{queue, metadata, {38, 1000}};
    PositionBroadcastScheduler scheduler{packet_sink, {1000, 100}};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 0).submitted());

    FakeRadioTransport sender{64};
    FakeRadioTransport receiver{64};
    sender.connect(receiver);
    receiver.connect(sender);
    DeliveryController delivery{sender};
    PriorityDeliveryHandoff handoff{queue, delivery};
    EXPECT(handoff.service(0).transferred());
    EXPECT(queue.status().queued == 0);
    delivery.service(0);
    const auto delivery_event = delivery.next_event();
    EXPECT(delivery_event.has_event);
    EXPECT(delivery_event.event.message_id == 800);
    EXPECT(delivery_event.event.outcome == DeliveryOutcome::sent_unconfirmed);
    EXPECT(delivery_event.event.attempts == 1);

    sender.service(0);
    std::array<std::uint8_t, 64> received{};
    const auto radio_result =
        receiver.receive({received.data(), received.size()});
    EXPECT(radio_result.has_frame());
    const auto packet =
        decode_packet({received.data(), radio_result.received_bytes});
    EXPECT(packet.decoded());
    EXPECT(packet.packet.header.message_id == 800);
    const auto position = decode_position(packet.packet.payload);
    EXPECT(position.decoded());
    EXPECT(position.position.latitude_e7 == 449775000);
    EXPECT(sender.status().frames_sent == 1);
    EXPECT(receiver.status().frames_received == 1);
}

static_assert(std::is_trivially_copyable_v<PriorityDeliveryHandoffResult>);
static_assert(std::is_trivially_copyable_v<PriorityDeliveryHandoffStatus>);
static_assert(sizeof(PriorityDeliveryHandoffResult) <= 16);
static_assert(sizeof(PriorityDeliveryHandoffStatus) <= 24);

}  // namespace

int main() {
    test_peek_and_commit_require_the_selected_message();
    test_empty_queue_is_idle_without_delivery_mutation();
    test_handoff_preserves_priority_selection();
    test_full_delivery_defers_without_losing_queue_entry();
    test_oversized_for_transport_fails_and_retains();
    test_duplicate_delivery_id_fails_and_retains();
    test_handoff_never_extends_original_expiry();
    test_queue_expiry_wins_before_handoff();
    test_clock_rollback_horizon_fails_without_narrowing();
    test_scheduler_packet_queue_delivery_reaches_fake_peer_once();

    if (failures != 0) {
        std::cerr << failures
                  << " priority delivery handoff assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 priority delivery handoff scenario groups\n";
    return EXIT_SUCCESS;
}
