#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/packet_codec.hpp"
#include "opentrail/position_codec.hpp"
#include "opentrail/position_packet_admission.hpp"

namespace {

using namespace opentrail::delivery;
using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::protocol;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class ScriptedMetadataSource final : public PositionPacketMetadataSource {
public:
    static constexpr std::size_t kCapacity = 16;

    bool enqueue(PositionPacketMetadata metadata) {
        if (size_ == entries_.size()) {
            return false;
        }
        entries_[size_++] = metadata;
        return true;
    }

    PositionPacketMetadata next() override {
        ++calls_;
        if (head_ == size_) {
            return {};
        }
        return entries_[head_++];
    }

    std::uint32_t calls() const {
        return calls_;
    }

private:
    std::array<PositionPacketMetadata, kCapacity> entries_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::uint32_t calls_{0};
};

PriorityQueuePolicy queue_policy(
    std::size_t capacity = 4,
    std::size_t reserve = 1,
    std::uint16_t background_rate = 20,
    std::uint32_t window_ms = 1000) {
    return {
        capacity,
        reserve,
        {{{20, window_ms},
          {20, window_ms},
          {20, window_ms},
          {background_rate, window_ms}}},
    };
}

PositionPacketAdmissionPolicy admission_policy(
    std::size_t maximum_frame_bytes = 38,
    std::uint32_t lifetime_ms = 1000) {
    return {maximum_frame_bytes, lifetime_ms};
}

PositionPacketMetadata metadata(
    std::uint32_t message_id,
    PositionPacketMetadataError error = PositionPacketMetadataError::none) {
    return {error, 0xA001, 0xB001, message_id};
}

LocationSnapshot current_snapshot(std::int32_t latitude_e7 = 449775000) {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = latitude_e7;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.age_ms = 25;
    return snapshot;
}

std::array<std::uint8_t, kPositionPayloadBytes> current_payload() {
    std::array<std::uint8_t, kPositionPayloadBytes> payload{};
    const auto encoded = encode_position(
        current_snapshot(), {payload.data(), payload.size()});
    EXPECT(encoded.encoded());
    return payload;
}

void test_policy_requires_exact_frame_capacity_and_lifetime() {
    auto payload = current_payload();
    PriorityTrafficQueue queue{queue_policy()};
    ScriptedMetadataSource source{};

    PositionPacketAdmissionSink short_mtu{
        queue, source, admission_policy(37, 1000)};
    EXPECT(short_mtu.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::failed);
    EXPECT(!short_mtu.status().policy_valid);

    PositionPacketAdmissionSink zero_lifetime{
        queue, source, admission_policy(38, 0)};
    EXPECT(zero_lifetime.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::failed);
    EXPECT(!zero_lifetime.status().policy_valid);
    EXPECT(source.calls() == 0);
    EXPECT(queue.status().queued == 0);
}

void test_current_position_becomes_exact_background_packet() {
    auto payload = current_payload();
    PriorityTrafficQueue queue{queue_policy()};
    ScriptedMetadataSource source{};
    EXPECT(source.enqueue(metadata(42)));
    PositionPacketAdmissionSink sink{
        queue, source, admission_policy(38, 1000)};

    EXPECT(sink.submit({payload.data(), payload.size()}, 50) ==
           PositionBroadcastSinkError::none);
    const auto queued = queue.take_next(50);
    EXPECT(queued.has_message);
    EXPECT(queued.message.message_id == 42);
    EXPECT(queued.message.message_class == MessageClass::position);
    EXPECT(queued.message.priority == TrafficPriority::background);
    EXPECT(queued.message.frame_size == 38);
    EXPECT(queued.message.created_at_ms == 50);
    EXPECT(queued.message.expires_at_ms == 1050);

    const auto packet = decode_packet(
        {queued.message.frame.data(), queued.message.frame_size});
    EXPECT(packet.decoded());
    EXPECT(packet.packet.header.type == PacketType::position);
    EXPECT(packet.packet.header.source_node_id == 0xA001);
    EXPECT(packet.packet.header.network_id == 0xB001);
    EXPECT(packet.packet.header.message_id == 42);
    const auto position = decode_position(packet.packet.payload);
    EXPECT(position.decoded());
    EXPECT(position.position.state == BroadcastPositionState::current);
    EXPECT(position.position.latitude_e7 == 449775000);
    EXPECT(sink.status().admitted == 1);
}

void test_scheduler_passes_actual_attempt_time_into_admission() {
    PriorityTrafficQueue queue{queue_policy()};
    ScriptedMetadataSource source{};
    EXPECT(source.enqueue(metadata(43)));
    PositionPacketAdmissionSink sink{
        queue, source, admission_policy(38, 1000)};
    PositionBroadcastScheduler scheduler{sink, {1000, 100}};

    EXPECT(scheduler.start(75) == PositionBroadcastScheduleError::none);
    const auto submitted = scheduler.service(current_snapshot(100), 80);
    EXPECT(submitted.submitted());
    EXPECT(submitted.next_attempt_ms == 1080);
    const auto queued = queue.take_next(80);
    EXPECT(queued.has_message);
    EXPECT(queued.message.created_at_ms == 80);
    EXPECT(queued.message.expires_at_ms == 1080);
    const auto packet = decode_packet(
        {queued.message.frame.data(), queued.message.frame_size});
    EXPECT(packet.decoded());
    EXPECT(decode_position(packet.packet.payload).position.latitude_e7 == 100);
}

void test_invalid_or_noncurrent_payload_never_consumes_metadata() {
    PriorityTrafficQueue queue{queue_policy()};
    ScriptedMetadataSource source{};
    EXPECT(source.enqueue(metadata(44)));
    PositionPacketAdmissionSink sink{
        queue, source, admission_policy()};
    auto payload = current_payload();

    EXPECT(sink.submit({}, 0) == PositionBroadcastSinkError::failed);
    EXPECT(sink.submit({payload.data(), payload.size() - 1}, 0) ==
           PositionBroadcastSinkError::failed);
    payload[3] = 1;
    EXPECT(sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::failed);

    std::array<std::uint8_t, kPositionPayloadBytes> stale{};
    auto stale_snapshot = current_snapshot();
    stale_snapshot.state = FixState::stale;
    EXPECT(encode_position(
               stale_snapshot, {stale.data(), stale.size()})
               .encoded());
    EXPECT(sink.submit({stale.data(), stale.size()}, 0) ==
           PositionBroadcastSinkError::failed);
    EXPECT(source.calls() == 0);
    EXPECT(queue.status().queued == 0);
    EXPECT(sink.status().failures == 4);
}

void test_metadata_not_ready_is_retryable_backpressure() {
    auto payload = current_payload();
    PriorityTrafficQueue queue{queue_policy()};
    ScriptedMetadataSource source{};
    PositionPacketAdmissionSink sink{queue, source, admission_policy()};
    EXPECT(sink.submit({payload.data(), payload.size()}, 5) ==
           PositionBroadcastSinkError::not_ready);
    EXPECT(sink.status().last_error ==
           PositionPacketAdmissionError::metadata_not_ready);
    EXPECT(sink.status().backpressured == 1);
    EXPECT(sink.status().failures == 0);
    EXPECT(queue.status().queued == 0);
}

void test_metadata_exhaustion_failure_and_invalid_values_fail_closed() {
    auto payload = current_payload();
    for (const auto entry : {
             metadata(0, PositionPacketMetadataError::exhausted),
             metadata(0, PositionPacketMetadataError::failed),
             metadata(0, static_cast<PositionPacketMetadataError>(255)),
             PositionPacketMetadata{
                 PositionPacketMetadataError::none, 0, 0xB001, 45}}) {
        PriorityTrafficQueue queue{queue_policy()};
        ScriptedMetadataSource source{};
        EXPECT(source.enqueue(entry));
        PositionPacketAdmissionSink sink{queue, source, admission_policy()};
        EXPECT(sink.submit({payload.data(), payload.size()}, 0) ==
               PositionBroadcastSinkError::failed);
        EXPECT(sink.status().failures == 1);
        EXPECT(queue.status().queued == 0);
    }
}

void test_background_rate_limit_retries_at_exact_window() {
    auto payload = current_payload();
    PriorityTrafficQueue queue{queue_policy(4, 0, 1, 100)};
    ScriptedMetadataSource source{};
    EXPECT(source.enqueue(metadata(50)));
    EXPECT(source.enqueue(metadata(51)));
    EXPECT(source.enqueue(metadata(52)));
    PositionPacketAdmissionSink sink{queue, source, admission_policy()};

    EXPECT(sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::none);
    EXPECT(queue.take_next(0).has_message);
    EXPECT(sink.submit({payload.data(), payload.size()}, 1) ==
           PositionBroadcastSinkError::not_ready);
    EXPECT(sink.status().last_error ==
           PositionPacketAdmissionError::queue_rate_limited);
    EXPECT(sink.submit({payload.data(), payload.size()}, 100) ==
           PositionBroadcastSinkError::none);
    EXPECT(sink.status().admitted == 2);
    EXPECT(sink.status().backpressured == 1);
}

void test_reserved_and_full_capacity_map_to_full() {
    auto payload = current_payload();
    const std::array<std::uint8_t, 1> other{{0xAA}};

    PriorityTrafficQueue reserved{queue_policy(2, 1)};
    EXPECT(reserved.enqueue(
               60, MessageClass::chat, {other.data(), other.size()}, 0, 1000)
               .accepted());
    ScriptedMetadataSource reserved_source{};
    EXPECT(reserved_source.enqueue(metadata(61)));
    PositionPacketAdmissionSink reserved_sink{
        reserved, reserved_source, admission_policy()};
    EXPECT(reserved_sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::full);
    EXPECT(reserved_sink.status().last_error ==
           PositionPacketAdmissionError::queue_capacity);
    EXPECT(reserved.status().rejected_reserved == 1);

    PriorityTrafficQueue full{queue_policy(1, 0)};
    EXPECT(full.enqueue(
               62, MessageClass::status, {other.data(), other.size()}, 0, 1000)
               .accepted());
    ScriptedMetadataSource full_source{};
    EXPECT(full_source.enqueue(metadata(63)));
    PositionPacketAdmissionSink full_sink{
        full, full_source, admission_policy()};
    EXPECT(full_sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::full);
    EXPECT(full.status().rejected_full == 1);
}

void test_invalid_queue_and_duplicate_metadata_are_failures() {
    auto payload = current_payload();
    PriorityTrafficQueue invalid_queue{{}};
    ScriptedMetadataSource invalid_source{};
    EXPECT(invalid_source.enqueue(metadata(70)));
    PositionPacketAdmissionSink invalid_sink{
        invalid_queue, invalid_source, admission_policy()};
    EXPECT(invalid_sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::failed);
    EXPECT(invalid_sink.status().last_error ==
           PositionPacketAdmissionError::queue_rejected);

    PriorityTrafficQueue duplicate_queue{queue_policy(4, 0)};
    ScriptedMetadataSource duplicate_source{};
    EXPECT(duplicate_source.enqueue(metadata(71)));
    EXPECT(duplicate_source.enqueue(metadata(71)));
    PositionPacketAdmissionSink duplicate_sink{
        duplicate_queue, duplicate_source, admission_policy()};
    EXPECT(duplicate_sink.submit({payload.data(), payload.size()}, 0) ==
           PositionBroadcastSinkError::none);
    EXPECT(duplicate_sink.submit({payload.data(), payload.size()}, 1) ==
           PositionBroadcastSinkError::failed);
    EXPECT(duplicate_sink.status().admitted == 1);
    EXPECT(duplicate_sink.status().failures == 1);
    EXPECT(duplicate_queue.status().queued == 1);
}

void test_position_stays_below_critical_and_expires_visibly() {
    auto payload = current_payload();
    const std::array<std::uint8_t, 1> critical{{0xCC}};
    PriorityTrafficQueue queue{queue_policy(3, 0)};
    EXPECT(queue.enqueue(
               80,
               MessageClass::critical_alert,
               {critical.data(), critical.size()},
               0,
               100)
               .accepted());
    ScriptedMetadataSource source{};
    EXPECT(source.enqueue(metadata(81)));
    PositionPacketAdmissionSink sink{
        queue, source, admission_policy(38, 10)};
    EXPECT(sink.submit({payload.data(), payload.size()}, 1) ==
           PositionBroadcastSinkError::none);

    const auto first = queue.take_next(1);
    EXPECT(first.has_message);
    EXPECT(first.message.message_id == 80);
    EXPECT(first.message.priority == TrafficPriority::critical);
    queue.purge_expired(11);
    EXPECT(queue.status().queued == 0);
    EXPECT(queue.status().expired == 1);
    const auto event = queue.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.message_id == 81);
    EXPECT(event.event.message_class == MessageClass::position);
    EXPECT(event.event.reason == PriorityQueueEventReason::expired);
}

static_assert(std::is_trivially_copyable_v<PositionPacketMetadata>);
static_assert(std::is_trivially_copyable_v<PositionPacketAdmissionStatus>);
static_assert(sizeof(PositionPacketAdmissionStatus) <= 20);

}  // namespace

int main() {
    test_policy_requires_exact_frame_capacity_and_lifetime();
    test_current_position_becomes_exact_background_packet();
    test_scheduler_passes_actual_attempt_time_into_admission();
    test_invalid_or_noncurrent_payload_never_consumes_metadata();
    test_metadata_not_ready_is_retryable_backpressure();
    test_metadata_exhaustion_failure_and_invalid_values_fail_closed();
    test_background_rate_limit_retries_at_exact_window();
    test_reserved_and_full_capacity_map_to_full();
    test_invalid_queue_and_duplicate_metadata_are_failures();
    test_position_stays_below_critical_and_expires_visibly();

    if (failures != 0) {
        std::cerr << failures
                  << " position packet admission assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position packet admission scenario groups\n";
    return EXIT_SUCCESS;
}
