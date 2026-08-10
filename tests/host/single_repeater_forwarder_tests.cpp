#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/single_repeater_forwarder.hpp"

namespace {

using namespace opentrail::delivery;

constexpr std::uint64_t kGroupContext = 0x1001;
constexpr std::uint64_t kSource = 0x2001;
constexpr std::uint64_t kRepeater = 0x3001;
constexpr std::uint64_t kDestination = 0x4001;
constexpr std::uint32_t kEpoch = 7;
const std::array<std::uint8_t, 6> frame{0x4F, 0x54, 0x01, 0xAA, 0x00, 0xFF};

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

VerifiedForwardingMetadata metadata(std::uint32_t message_id = 1) {
    return {
        kGroupContext,
        kSource,
        kDestination,
        kEpoch,
        message_id,
        true,
        true,
        true,
    };
}

SingleRepeaterPolicy policy(
    std::size_t depth = 4,
    std::uint16_t rate = 8,
    std::uint32_t window_ms = 1000,
    std::uint32_t age_ms = 100) {
    return {1, true, depth, rate, window_ms, age_ms};
}

void test_valid_frame_is_forwarded_byte_for_byte() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    const auto decision = forwarder.process(
        metadata(), {frame.data(), frame.size()}, 10);
    EXPECT(decision.queued);
    EXPECT(decision.disposition == SingleRepeaterDisposition::queued);
    EXPECT(decision.replay_state_changed);
    const auto output = forwarder.next_forward(11);
    EXPECT(output.has_frame && output.frame.size == frame.size());
    for (std::size_t index = 0; index < frame.size(); ++index) {
        EXPECT(output.frame.bytes[index] == frame[index]);
    }
}

void test_authentication_failure_does_not_poison_duplicate_state() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(2);
    input.source_authenticated = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::source_authentication_required);
    input.source_authenticated = true;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).queued);
    EXPECT(forwarder.status().authentication_dropped == 1);
}

void test_authorization_context_and_permission_precede_duplicate_state() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(3);
    input.source_authorized = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::source_unauthorized);
    input.source_authorized = true;
    input.group_context_id += 1;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).disposition ==
           SingleRepeaterDisposition::wrong_group_or_epoch);
    input.group_context_id = kGroupContext;
    input.forwarding_permitted = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 2).disposition ==
           SingleRepeaterDisposition::forwarding_not_permitted);
    input.forwarding_permitted = true;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 3).queued);
}

void test_exactly_one_authorized_repeater_is_required() {
    for (const auto count : {std::uint8_t{0}, std::uint8_t{2}}) {
        DuplicateWindow duplicates{10000};
        auto invalid = policy();
        invalid.configured_authorized_repeaters = count;
        SingleRepeaterForwarder forwarder{
            kRepeater, kGroupContext, kEpoch, invalid, duplicates};
        EXPECT(forwarder.process(
                   metadata(4), {frame.data(), frame.size()}, 0)
                   .disposition == SingleRepeaterDisposition::invalid_policy);
    }
    DuplicateWindow duplicates{10000};
    auto unauthorized = policy();
    unauthorized.local_repeater_authorized = false;
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, unauthorized, duplicates};
    EXPECT(forwarder.process(
               metadata(5), {frame.data(), frame.size()}, 0)
               .disposition == SingleRepeaterDisposition::invalid_policy);
}

void test_duplicate_and_reflection_are_forwarded_only_once() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    const auto input = metadata(6);
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).queued);
    const auto duplicate = forwarder.process(
        input, {frame.data(), frame.size()}, 1);
    EXPECT(duplicate.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(!duplicate.replay_state_changed);
    EXPECT(forwarder.status().duplicates_dropped == 1);
}

void test_self_source_and_local_destination_do_not_forward() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(7);
    input.source_alias = kRepeater;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::self_source);
    input = metadata(8);
    input.destination_alias = kRepeater;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).disposition ==
           SingleRepeaterDisposition::destination_reached);
}

void test_queue_and_rate_limits_fail_closed() {
    DuplicateWindow queue_duplicates{10000};
    SingleRepeaterForwarder queue_limited{
        kRepeater, kGroupContext, kEpoch, policy(1, 8), queue_duplicates};
    EXPECT(queue_limited.process(
               metadata(10), {frame.data(), frame.size()}, 0).queued);
    const auto queue_full = queue_limited.process(
        metadata(11), {frame.data(), frame.size()}, 1);
    EXPECT(queue_full.disposition == SingleRepeaterDisposition::queue_full);
    EXPECT(queue_full.replay_state_changed);
    EXPECT(queue_limited.next_forward(2).has_frame);
    EXPECT(queue_limited.process(
               metadata(11), {frame.data(), frame.size()}, 3).disposition ==
           SingleRepeaterDisposition::duplicate);

    DuplicateWindow rate_duplicates{10000};
    SingleRepeaterForwarder rate_limited{
        kRepeater, kGroupContext, kEpoch, policy(4, 1), rate_duplicates};
    EXPECT(rate_limited.process(
               metadata(20), {frame.data(), frame.size()}, 0).queued);
    EXPECT(rate_limited.next_forward(1).has_frame);
    const auto rate_limited_result = rate_limited.process(
        metadata(21), {frame.data(), frame.size()}, 2);
    EXPECT(rate_limited_result.disposition ==
           SingleRepeaterDisposition::rate_limited);
    EXPECT(rate_limited_result.replay_state_changed);
    EXPECT(rate_limited.process(
               metadata(21), {frame.data(), frame.size()}, 1000).disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(rate_limited.process(
               metadata(22), {frame.data(), frame.size()}, 1001).queued);
}

void test_expiry_and_clock_regression_drop_without_transmit() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(4, 8, 1000, 100), duplicates};
    EXPECT(forwarder.process(
               metadata(30), {frame.data(), frame.size()}, 1000).queued);
    const auto exact_expiry = forwarder.next_forward(1100);
    EXPECT(!exact_expiry.has_frame && exact_expiry.expired_frames_dropped == 1);
    EXPECT(forwarder.process(
               metadata(31), {frame.data(), frame.size()}, 2000).queued);
    const auto regressed = forwarder.next_forward(1999);
    EXPECT(!regressed.has_frame && regressed.expired_frames_dropped == 1);
    EXPECT(forwarder.status().expired_dropped == 2);

    DuplicateWindow process_duplicates{10000};
    SingleRepeaterForwarder process_forwarder{
        kRepeater,
        kGroupContext,
        kEpoch,
        policy(4, 8, 1000, 100),
        process_duplicates};
    EXPECT(process_forwarder.process(
               metadata(32), {frame.data(), frame.size()}, 3000).queued);
    EXPECT(process_forwarder.process(
               metadata(33), {frame.data(), frame.size()}, 2999).disposition ==
           SingleRepeaterDisposition::clock_regression);
    EXPECT(process_forwarder.process(
               metadata(33), {frame.data(), frame.size()}, 3001).queued);
    EXPECT(process_forwarder.status().clock_regression_dropped == 1);
}

void test_invalid_frame_and_configuration_are_rejected() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    EXPECT(forwarder.process(metadata(40), {nullptr, 1}, 0).disposition ==
           SingleRepeaterDisposition::invalid_argument);
    auto invalid = metadata(41);
    invalid.source_alias = 0;
    EXPECT(forwarder.process(invalid, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::invalid_argument);
}

}  // namespace

int main() {
    test_valid_frame_is_forwarded_byte_for_byte();
    test_authentication_failure_does_not_poison_duplicate_state();
    test_authorization_context_and_permission_precede_duplicate_state();
    test_exactly_one_authorized_repeater_is_required();
    test_duplicate_and_reflection_are_forwarded_only_once();
    test_self_source_and_local_destination_do_not_forward();
    test_queue_and_rate_limits_fail_closed();
    test_expiry_and_clock_regression_drop_without_transmit();
    test_invalid_frame_and_configuration_are_rejected();
    if (failures != 0) {
        std::cerr << failures
                  << " single-repeater forwarder assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 immutable single-repeater scenario groups\n";
    return EXIT_SUCCESS;
}
