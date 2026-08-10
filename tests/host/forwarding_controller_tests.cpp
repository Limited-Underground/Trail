#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/forwarding_controller.hpp"

namespace {

using opentrail::delivery::DuplicateWindow;
using opentrail::delivery::ForwardingController;
using opentrail::delivery::ForwardingDisposition;
using opentrail::delivery::ForwardingPolicy;
using opentrail::delivery::RoutingMetadata;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr std::uint64_t kNodeA = 0xA001;
constexpr std::uint64_t kRepeater = 0xB001;
constexpr std::uint64_t kNodeC = 0xC001;
constexpr std::uint64_t kGroup = 0xDD01;
constexpr std::uint32_t kEpoch = 1;
const std::array<std::uint8_t, 4> frame{0x10, 0x20, 0x30, 0x40};

RoutingMetadata route(
    std::uint32_t message_id,
    std::uint64_t destination,
    std::uint8_t hops_remaining = 1,
    bool permitted = true) {
    return {
        kNodeA,
        destination,
        kGroup,
        kEpoch,
        message_id,
        hops_remaining,
        permitted,
    };
}

ForwardingPolicy repeater_policy(
    std::size_t queue_depth = 4,
    std::uint16_t rate = 8,
    std::uint32_t window_ms = 1000) {
    return {true, queue_depth, rate, window_ms};
}

void test_three_node_unicast_and_ttl_decrement() {
    DuplicateWindow repeater_duplicates(10000);
    DuplicateWindow destination_duplicates(10000);
    ForwardingController repeater(
        kRepeater, kGroup, kEpoch, repeater_policy(), repeater_duplicates);
    ForwardingController destination(
        kNodeC, kGroup, kEpoch, {false, 0, 0, 0}, destination_duplicates);

    const auto at_repeater = repeater.process(
        route(1, kNodeC), {frame.data(), frame.size()}, 0);
    EXPECT(!at_repeater.deliver_local);
    EXPECT(at_repeater.queued_for_forward);
    EXPECT(at_repeater.disposition == ForwardingDisposition::queued);

    const auto forwarded = repeater.next_forward();
    EXPECT(forwarded.has_frame);
    EXPECT(forwarded.frame.metadata.hops_remaining == 0);
    EXPECT(forwarded.frame.size == frame.size());
    EXPECT(forwarded.frame.bytes[0] == frame[0]);
    EXPECT(forwarded.frame.bytes[3] == frame[3]);

    const auto at_destination = destination.process(
        forwarded.frame.metadata,
        {forwarded.frame.bytes.data(), forwarded.frame.size},
        10);
    EXPECT(at_destination.deliver_local);
    EXPECT(!at_destination.queued_for_forward);
    EXPECT(at_destination.disposition == ForwardingDisposition::destination_reached);
}

void test_reflected_loop_is_suppressed_as_duplicate() {
    DuplicateWindow duplicates(10000);
    ForwardingController repeater(
        kRepeater, kGroup, kEpoch, repeater_policy(), duplicates);
    const auto metadata = route(2, kNodeC);
    EXPECT(repeater.process(metadata, {frame.data(), frame.size()}, 0).queued_for_forward);
    EXPECT(repeater.process(metadata, {frame.data(), frame.size()}, 10).disposition ==
           ForwardingDisposition::duplicate);
    EXPECT(repeater.status().duplicates_dropped == 1);
}

void test_source_seeds_duplicate_window_and_rejects_untracked_self_alias() {
    DuplicateWindow source_duplicates(10000);
    ForwardingController source(
        kNodeA, kGroup, kEpoch, {false, 0, 0, 0}, source_duplicates);
    EXPECT(source.record_originated(30, 0) ==
           opentrail::delivery::DuplicateError::none);
    EXPECT(source.process(
               route(30, 0, 1), {frame.data(), frame.size()}, 10)
               .disposition == ForwardingDisposition::duplicate);

    auto untracked = route(31, 0, 1);
    untracked.source_alias = kNodeA;
    EXPECT(source.process(untracked, {frame.data(), frame.size()}, 20).disposition ==
           ForwardingDisposition::self_source_untracked);
}

void test_ttl_and_forward_permission_are_enforced() {
    DuplicateWindow duplicates(10000);
    ForwardingController repeater(
        kRepeater, kGroup, kEpoch, repeater_policy(), duplicates);
    EXPECT(repeater.process(
               route(3, kNodeC, 0), {frame.data(), frame.size()}, 0)
               .disposition == ForwardingDisposition::ttl_exhausted);
    EXPECT(repeater.process(
               route(4, kNodeC, 2, false), {frame.data(), frame.size()}, 0)
               .disposition == ForwardingDisposition::forwarding_not_permitted);
    EXPECT(repeater.status().queue_depth == 0);
}

void test_client_role_does_not_forward() {
    DuplicateWindow duplicates(10000);
    ForwardingController client(
        kRepeater, kGroup, kEpoch, {false, 0, 0, 0}, duplicates);
    const auto decision = client.process(
        route(5, kNodeC), {frame.data(), frame.size()}, 0);
    EXPECT(!decision.deliver_local);
    EXPECT(!decision.queued_for_forward);
    EXPECT(decision.disposition == ForwardingDisposition::forwarding_disabled);
}

void test_wrong_group_is_rejected_without_poisoning_duplicate_window() {
    DuplicateWindow duplicates(10000);
    ForwardingController repeater(
        kRepeater, kGroup, kEpoch, repeater_policy(), duplicates);
    auto wrong = route(6, kNodeC);
    wrong.group_id = 0x9999;
    EXPECT(repeater.process(wrong, {frame.data(), frame.size()}, 0).disposition ==
           ForwardingDisposition::wrong_group_or_epoch);
    EXPECT(repeater.process(
               route(6, kNodeC), {frame.data(), frame.size()}, 1)
               .queued_for_forward);
}

void test_broadcast_is_delivered_and_forwarded_once() {
    DuplicateWindow duplicates(10000);
    ForwardingController repeater(
        kRepeater, kGroup, kEpoch, repeater_policy(), duplicates);
    const auto decision = repeater.process(
        route(7, 0, 1), {frame.data(), frame.size()}, 0);
    EXPECT(decision.deliver_local);
    EXPECT(decision.queued_for_forward);
    const auto forwarded = repeater.next_forward();
    EXPECT(forwarded.frame.metadata.destination_alias == 0);
    EXPECT(forwarded.frame.metadata.hops_remaining == 0);
}

void test_queue_and_rate_limits_bound_congestion() {
    DuplicateWindow queue_duplicates(10000);
    ForwardingController queue_limited(
        kRepeater, kGroup, kEpoch, repeater_policy(1, 10, 1000), queue_duplicates);
    EXPECT(queue_limited.process(
               route(10, kNodeC), {frame.data(), frame.size()}, 0)
               .queued_for_forward);
    EXPECT(queue_limited.process(
               route(11, kNodeC), {frame.data(), frame.size()}, 1)
               .disposition == ForwardingDisposition::queue_full);
    EXPECT(queue_limited.status().congestion_dropped == 1);

    DuplicateWindow rate_duplicates(10000);
    ForwardingController rate_limited(
        kRepeater, kGroup, kEpoch, repeater_policy(4, 2, 1000), rate_duplicates);
    EXPECT(rate_limited.process(
               route(20, kNodeC), {frame.data(), frame.size()}, 0)
               .queued_for_forward);
    EXPECT(rate_limited.next_forward().has_frame);
    EXPECT(rate_limited.process(
               route(21, kNodeC), {frame.data(), frame.size()}, 1)
               .queued_for_forward);
    EXPECT(rate_limited.next_forward().has_frame);
    EXPECT(rate_limited.process(
               route(22, kNodeC), {frame.data(), frame.size()}, 2)
               .disposition == ForwardingDisposition::rate_limited);
    EXPECT(rate_limited.process(
               route(23, kNodeC), {frame.data(), frame.size()}, 1000)
               .queued_for_forward);
}

}  // namespace

int main() {
    test_three_node_unicast_and_ttl_decrement();
    test_reflected_loop_is_suppressed_as_duplicate();
    test_source_seeds_duplicate_window_and_rejects_untracked_self_alias();
    test_ttl_and_forward_permission_are_enforced();
    test_client_role_does_not_forward();
    test_wrong_group_is_rejected_without_poisoning_duplicate_window();
    test_broadcast_is_delivered_and_forwarded_once();
    test_queue_and_rate_limits_bound_congestion();

    if (failures != 0) {
        std::cerr << failures << " forwarding controller assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 controlled-forwarding scenarios\n";
    return EXIT_SUCCESS;
}
