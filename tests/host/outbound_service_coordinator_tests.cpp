#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_gps_provider.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "fake_radio_transport.hpp"
#include "opentrail/outbound_service_coordinator.hpp"
#include "opentrail/packet_codec.hpp"
#include "opentrail/position_codec.hpp"
#include "opentrail/position_packet_admission.hpp"

namespace {

using namespace opentrail::delivery;
using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
using namespace opentrail::protocol;
using namespace opentrail::radio;
using namespace opentrail::radio::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr std::array<std::uint8_t, 1> kSmallFrame{{0x42}};

PriorityQueuePolicy queue_policy() {
    return {
        8,
        0,
        {{{20, 1000}, {20, 1000}, {20, 1000}, {20, 1000}}},
    };
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

GpsFix current_fix(std::uint64_t received_at_ms = 0) {
    GpsFix fix{};
    fix.latitude_e7 = 449775000;
    fix.longitude_e7 = -677500000;
    fix.horizontal_accuracy_cm = 250;
    fix.horizontal_accuracy_valid = true;
    fix.received_at_ms = received_at_ms;
    return fix;
}

struct RuntimeFixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeGpsProvider gps{};
    LocationTracker location{gps, 1000};
    PriorityTrafficQueue queue{queue_policy()};
    OneMetadata metadata{800};
    PositionPacketAdmissionSink packet_sink{queue, metadata, {38, 1000}};
    PositionBroadcastScheduler scheduler{packet_sink, {1000, 100}};
    FakeRadioTransport sender{64};
    FakeRadioTransport receiver{64};
    DeliveryController delivery{sender};
    PriorityDeliveryHandoff handoff{queue, delivery};
    OutboundServiceCoordinator runtime{
        clock, location, scheduler, handoff, delivery, sender};

    RuntimeFixture() {
        sender.connect(receiver);
        receiver.connect(sender);
    }
};

bool receive_one(FakeRadioTransport& receiver,
                 std::array<std::uint8_t, 64>& bytes,
                 std::size_t& size) {
    const auto received = receiver.receive({bytes.data(), bytes.size()});
    size = received.received_bytes;
    return received.has_frame();
}

void test_not_ready_touches_no_downstream_component() {
    RuntimeFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_not_ready());
    const auto result = fixture.runtime.service();
    EXPECT(result.disposition == OutboundServiceDisposition::deferred);
    EXPECT(result.clock_error == MonotonicClockError::not_ready);
    EXPECT(!result.delivery_serviced);
    EXPECT(!result.radio_serviced);
    EXPECT(fixture.gps.read_count() == 0);
    EXPECT(fixture.scheduler.status().service_calls == 0);
    EXPECT(fixture.handoff.status().service_calls == 0);
    EXPECT(fixture.runtime.status().clock_deferred == 1);
}

void test_stopped_position_sharing_does_not_read_gps() {
    RuntimeFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(10));
    const auto result = fixture.runtime.service();
    EXPECT(result.serviced());
    EXPECT(result.now_ms == 10);
    EXPECT(result.position.disposition ==
           PositionBroadcastScheduleDisposition::stopped);
    EXPECT(result.handoff.disposition ==
           PriorityDeliveryHandoffDisposition::idle);
    EXPECT(result.delivery_serviced);
    EXPECT(result.radio_serviced);
    EXPECT(fixture.gps.read_count() == 0);
}

void test_one_cycle_reaches_fake_peer_in_fixed_order() {
    RuntimeFixture fixture{};
    fixture.gps.set_fix(current_fix());
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_time(0));
    const auto result = fixture.runtime.service();
    EXPECT(result.serviced());
    EXPECT(result.position.submitted());
    EXPECT(result.handoff.transferred());
    EXPECT(result.handoff.message_id == 800);
    EXPECT(fixture.queue.status().queued == 0);
    EXPECT(fixture.gps.read_count() == 1);
    EXPECT(fixture.clock_source.read_count() == 1);

    std::array<std::uint8_t, 64> bytes{};
    std::size_t size = 0;
    EXPECT(receive_one(fixture.receiver, bytes, size));
    const auto packet = decode_packet({bytes.data(), size});
    EXPECT(packet.decoded());
    EXPECT(packet.packet.header.message_id == 800);
    const auto position = decode_position(packet.packet.payload);
    EXPECT(position.decoded());
    EXPECT(position.position.latitude_e7 == 449775000);
    const auto event = fixture.delivery.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::sent_unconfirmed);
}

void test_equal_checked_time_is_valid_without_early_resubmit() {
    RuntimeFixture fixture{};
    fixture.gps.set_fix(current_fix());
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_time(0));
    EXPECT(fixture.clock_source.enqueue_time(0));
    EXPECT(fixture.runtime.service().position.submitted());
    const auto equal = fixture.runtime.service();
    EXPECT(equal.serviced());
    EXPECT(equal.position.disposition ==
           PositionBroadcastScheduleDisposition::not_due);
    EXPECT(fixture.sender.status().frames_sent == 1);
    EXPECT(fixture.gps.read_count() == 2);
}

void test_clock_rollback_stops_position_and_latches_runtime() {
    RuntimeFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_time(100));
    EXPECT(fixture.clock_source.enqueue_time(99));
    EXPECT(fixture.clock_source.enqueue_time(200));
    EXPECT(fixture.runtime.service().serviced());
    EXPECT(fixture.scheduler.status().active);
    EXPECT(fixture.queue.enqueue(
               900,
               MessageClass::chat,
               {kSmallFrame.data(), kSmallFrame.size()},
               100,
               1000)
               .accepted());

    const auto rollback = fixture.runtime.service();
    EXPECT(rollback.disposition == OutboundServiceDisposition::failed);
    EXPECT(rollback.clock_error == MonotonicClockError::rollback_detected);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.queue.status().queued == 1);
    EXPECT(fixture.delivery.status().pending == 0);

    const auto latched = fixture.runtime.service();
    EXPECT(latched.clock_error == MonotonicClockError::fault_latched);
    EXPECT(fixture.clock_source.read_count() == 2);
    EXPECT(fixture.runtime.status().latched_clock_error ==
           MonotonicClockError::rollback_detected);
    EXPECT(fixture.runtime.status().latched_refusals == 1);
}

void test_clock_source_failure_latches_without_downstream_work() {
    RuntimeFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_failure());
    const auto result = fixture.runtime.service();
    EXPECT(result.disposition == OutboundServiceDisposition::failed);
    EXPECT(result.clock_error == MonotonicClockError::source_failed);
    EXPECT(fixture.gps.read_count() == 0);
    EXPECT(fixture.scheduler.status().service_calls == 0);
    EXPECT(fixture.handoff.status().service_calls == 0);
    EXPECT(fixture.runtime.status().faulted);
    EXPECT(fixture.runtime.status().clock_failures == 1);
}

void test_missing_fix_does_not_block_existing_queued_traffic() {
    RuntimeFixture fixture{};
    fixture.gps.set_unavailable();
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.queue.enqueue(
               910,
               MessageClass::chat,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    EXPECT(fixture.clock_source.enqueue_time(0));
    const auto result = fixture.runtime.service();
    EXPECT(result.position.error ==
           PositionBroadcastScheduleError::no_current_fix);
    EXPECT(result.handoff.transferred());
    EXPECT(fixture.queue.status().queued == 0);
    std::array<std::uint8_t, 64> bytes{};
    std::size_t size = 0;
    EXPECT(receive_one(fixture.receiver, bytes, size));
    EXPECT(size == 1);
}

void test_invalid_position_policy_does_not_block_queue_delivery() {
    FakeMonotonicCounterSource source{};
    CheckedMonotonicClock clock{source};
    FakeGpsProvider gps{};
    LocationTracker location{gps, 1000};
    PriorityTrafficQueue queue{queue_policy()};
    OneMetadata metadata{920};
    PositionPacketAdmissionSink sink{queue, metadata, {38, 1000}};
    PositionBroadcastScheduler scheduler{sink, {0, 100}};
    FakeRadioTransport sender{64};
    FakeRadioTransport receiver{64};
    sender.connect(receiver);
    DeliveryController delivery{sender};
    PriorityDeliveryHandoff handoff{queue, delivery};
    OutboundServiceCoordinator runtime{
        clock, location, scheduler, handoff, delivery, sender};
    EXPECT(queue.enqueue(
               921,
               MessageClass::chat,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    EXPECT(source.enqueue_time(0));
    const auto result = runtime.service();
    EXPECT(result.position.disposition ==
           PositionBroadcastScheduleDisposition::failed);
    EXPECT(result.position.error ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(result.handoff.transferred());
    std::array<std::uint8_t, 64> bytes{};
    std::size_t size = 0;
    EXPECT(receive_one(receiver, bytes, size));
}

void test_handoff_rejection_does_not_block_existing_delivery() {
    RuntimeFixture fixture{};
    EXPECT(fixture.delivery.enqueue(
               930,
               MessageClass::position,
               experimental_policy(MessageClass::position),
               {kSmallFrame.data(), kSmallFrame.size()},
               0)
               .accepted());
    EXPECT(fixture.queue.enqueue(
               930,
               MessageClass::position,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    EXPECT(fixture.clock_source.enqueue_time(0));
    const auto result = fixture.runtime.service();
    EXPECT(result.handoff.error ==
           PriorityDeliveryHandoffError::delivery_rejected);
    EXPECT(result.handoff.delivery_error ==
           DeliveryError::duplicate_message_id);
    EXPECT(result.handoff.queue_retained);
    EXPECT(fixture.queue.status().queued == 1);
    std::array<std::uint8_t, 64> bytes{};
    std::size_t size = 0;
    EXPECT(receive_one(fixture.receiver, bytes, size));
}

void test_full_delivery_defers_then_frees_capacity_for_next_cycle() {
    RuntimeFixture fixture{};
    for (std::uint32_t id = 940;
         id < 940 + DeliveryController::kPendingCapacity;
         ++id) {
        EXPECT(fixture.delivery.enqueue(
                   id,
                   MessageClass::position,
                   experimental_policy(MessageClass::position),
                   {kSmallFrame.data(), kSmallFrame.size()},
                   0)
                   .accepted());
    }
    EXPECT(fixture.queue.enqueue(
               960,
               MessageClass::position,
               {kSmallFrame.data(), kSmallFrame.size()},
               0,
               1000)
               .accepted());
    EXPECT(fixture.clock_source.enqueue_time(0));
    EXPECT(fixture.clock_source.enqueue_time(1));
    const auto first = fixture.runtime.service();
    EXPECT(first.handoff.disposition ==
           PriorityDeliveryHandoffDisposition::deferred);
    EXPECT(first.handoff.queue_retained);
    EXPECT(fixture.queue.status().queued == 1);
    EXPECT(fixture.delivery.status().pending == 4);

    const auto second = fixture.runtime.service();
    EXPECT(second.handoff.transferred());
    EXPECT(second.handoff.message_id == 960);
    EXPECT(fixture.queue.status().queued == 0);
    EXPECT(fixture.runtime.status().serviced_cycles == 2);
}

static_assert(std::is_trivially_copyable_v<OutboundServiceResult>);
static_assert(std::is_trivially_copyable_v<OutboundServiceStatus>);
static_assert(sizeof(OutboundServiceResult) <= 64);
static_assert(sizeof(OutboundServiceStatus) <= 40);

}  // namespace

int main() {
    test_not_ready_touches_no_downstream_component();
    test_stopped_position_sharing_does_not_read_gps();
    test_one_cycle_reaches_fake_peer_in_fixed_order();
    test_equal_checked_time_is_valid_without_early_resubmit();
    test_clock_rollback_stops_position_and_latches_runtime();
    test_clock_source_failure_latches_without_downstream_work();
    test_missing_fix_does_not_block_existing_queued_traffic();
    test_invalid_position_policy_does_not_block_queue_delivery();
    test_handoff_rejection_does_not_block_existing_delivery();
    test_full_delivery_defers_then_frees_capacity_for_next_cycle();

    if (failures != 0) {
        std::cerr << failures
                  << " outbound service coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 outbound service coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
