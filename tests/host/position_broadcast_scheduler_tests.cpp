#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_position_broadcast_sink.hpp"
#include "opentrail/position_broadcast_scheduler.hpp"

namespace {

using namespace opentrail::location;
using namespace opentrail::location::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr PositionBroadcastSchedulePolicy policy() {
    return {1000, 100};
}

LocationSnapshot current_snapshot(
    std::int32_t latitude_e7 = 449775000) {
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

PositionDecodeResult decode_at(
    const FakePositionBroadcastSink& sink,
    std::size_t index) {
    const auto* payload = sink.at(index);
    EXPECT(payload != nullptr);
    return payload == nullptr
        ? PositionDecodeResult{}
        : decode_position({payload->data(), payload->size()});
}

void test_invalid_policy_never_starts_or_touches_sink() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler zero_cadence{sink, {0, 100}};
    PositionBroadcastScheduler zero_retry{sink, {1000, 0}};
    EXPECT(zero_cadence.start(1) ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(zero_retry.start(1) ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(zero_cadence.service(current_snapshot(), 1).disposition ==
           PositionBroadcastScheduleDisposition::failed);
    EXPECT(sink.submit_attempts() == 0);
}

void test_start_submits_current_fix_immediately() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(50) == PositionBroadcastScheduleError::none);
    const auto result = scheduler.service(current_snapshot(), 50);
    EXPECT(result.submitted());
    EXPECT(result.next_attempt_ms == 1050);
    EXPECT(sink.size() == 1);
    const auto decoded = decode_at(sink, 0);
    EXPECT(decoded.decoded());
    EXPECT(decoded.position.state == BroadcastPositionState::current);
    EXPECT(decoded.position.latitude_e7 == 449775000);
}

void test_exact_cadence_and_delayed_service_coalesce() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(100) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(1), 100).submitted());
    EXPECT(scheduler.start(500) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.status().starts == 1);
    EXPECT(scheduler.status().next_attempt_ms == 1100);
    EXPECT(scheduler.service(current_snapshot(2), 1099).disposition ==
           PositionBroadcastScheduleDisposition::not_due);
    EXPECT(scheduler.service(current_snapshot(3), 1100).submitted());
    EXPECT(scheduler.service(current_snapshot(4), 5000).submitted());
    EXPECT(scheduler.status().next_attempt_ms == 6000);
    EXPECT(sink.size() == 3);
    EXPECT(decode_at(sink, 0).position.latitude_e7 == 1);
    EXPECT(decode_at(sink, 1).position.latitude_e7 == 3);
    EXPECT(decode_at(sink, 2).position.latitude_e7 == 4);
}

void test_noncurrent_fixes_are_suppressed_and_retried() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);

    LocationSnapshot unavailable{};
    const auto first = scheduler.service(unavailable, 0);
    EXPECT(first.error == PositionBroadcastScheduleError::no_current_fix);
    EXPECT(first.next_attempt_ms == 100);
    EXPECT(scheduler.service(current_snapshot(), 99).disposition ==
           PositionBroadcastScheduleDisposition::not_due);

    auto stale = current_snapshot();
    stale.state = FixState::stale;
    EXPECT(scheduler.service(stale, 100).error ==
           PositionBroadcastScheduleError::no_current_fix);
    auto invalid = current_snapshot();
    invalid.state = FixState::invalid;
    invalid.error = FixError::latitude_out_of_range;
    EXPECT(scheduler.service(invalid, 200).error ==
           PositionBroadcastScheduleError::no_current_fix);
    EXPECT(scheduler.service(current_snapshot(), 300).submitted());
    EXPECT(sink.size() == 1);
    EXPECT(scheduler.status().suppressed == 3);
}

void test_backpressure_uses_retry_without_false_submission() {
    FakePositionBroadcastSink sink{};
    EXPECT(sink.enqueue_result(PositionBroadcastSinkError::not_ready));
    EXPECT(sink.enqueue_result(PositionBroadcastSinkError::full));
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 0).error ==
           PositionBroadcastScheduleError::sink_not_ready);
    EXPECT(scheduler.service(current_snapshot(), 99).disposition ==
           PositionBroadcastScheduleDisposition::not_due);
    EXPECT(scheduler.service(current_snapshot(), 100).error ==
           PositionBroadcastScheduleError::sink_full);
    EXPECT(scheduler.service(current_snapshot(), 200).submitted());
    EXPECT(scheduler.status().backpressured == 2);
    EXPECT(scheduler.status().submitted == 1);
    EXPECT(sink.size() == 1);
}

void test_sink_failure_and_unknown_result_are_typed() {
    FakePositionBroadcastSink sink{};
    EXPECT(sink.enqueue_result(PositionBroadcastSinkError::failed));
    EXPECT(sink.enqueue_result(
        static_cast<PositionBroadcastSinkError>(255)));
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 0).error ==
           PositionBroadcastScheduleError::sink_failed);
    EXPECT(scheduler.service(current_snapshot(), 100).error ==
           PositionBroadcastScheduleError::sink_failed);
    EXPECT(scheduler.service(current_snapshot(), 200).submitted());
    EXPECT(scheduler.status().failures == 2);
}

void test_stop_is_immediate_and_restart_is_immediate() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 0).submitted());
    scheduler.stop();
    EXPECT(scheduler.service(current_snapshot(), 1000).disposition ==
           PositionBroadcastScheduleDisposition::stopped);
    EXPECT(sink.size() == 1);
    EXPECT(scheduler.start(1500) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 1500).submitted());
    scheduler.stop();
    scheduler.stop();
    EXPECT(scheduler.status().starts == 2);
    EXPECT(scheduler.status().stops == 2);
}

void test_clock_regression_latches_scheduler_off() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(100) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), 100).submitted());
    EXPECT(scheduler.service(current_snapshot(), 99).error ==
           PositionBroadcastScheduleError::clock_regression);
    EXPECT(scheduler.status().clock_failed);
    EXPECT(!scheduler.status().active);
    EXPECT(scheduler.start(200) ==
           PositionBroadcastScheduleError::clock_regression);
    EXPECT(scheduler.service(current_snapshot(), 200).disposition ==
           PositionBroadcastScheduleDisposition::failed);
    EXPECT(sink.size() == 1);
}

void test_malformed_current_snapshot_never_reaches_sink() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    auto malformed = current_snapshot();
    malformed.error = FixError::no_fix;
    EXPECT(scheduler.service(malformed, 0).error ==
           PositionBroadcastScheduleError::encode_failed);
    EXPECT(sink.submit_attempts() == 0);
    EXPECT(scheduler.status().failures == 1);
}

void test_time_horizon_and_fake_capacity_are_bounded() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto near_maximum =
        std::numeric_limits<std::uint64_t>::max() - 500U;
    EXPECT(scheduler.start(near_maximum) ==
           PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(current_snapshot(), near_maximum).submitted());
    EXPECT(scheduler.status().time_exhausted);
    EXPECT(!scheduler.status().active);
    EXPECT(scheduler.start(near_maximum) ==
           PositionBroadcastScheduleError::time_exhausted);

    FakePositionBroadcastSink bounded_sink{};
    PositionBroadcastScheduler bounded{bounded_sink, {1, 1}};
    EXPECT(bounded.start(0) == PositionBroadcastScheduleError::none);
    for (std::size_t index = 0; index < bounded_sink.kCapacity; ++index) {
        EXPECT(bounded.service(
            current_snapshot(static_cast<std::int32_t>(index)), index)
                   .submitted());
    }
    EXPECT(bounded.service(current_snapshot(), bounded_sink.kCapacity).error ==
           PositionBroadcastScheduleError::sink_full);
    EXPECT(bounded_sink.size() == bounded_sink.kCapacity);

    FakePositionBroadcastSink scripted_sink{};
    for (std::size_t index = 0; index < scripted_sink.kCapacity; ++index) {
        EXPECT(scripted_sink.enqueue_result(
            PositionBroadcastSinkError::not_ready));
    }
    EXPECT(!scripted_sink.enqueue_result(PositionBroadcastSinkError::failed));
}

}  // namespace

int main() {
    test_invalid_policy_never_starts_or_touches_sink();
    test_start_submits_current_fix_immediately();
    test_exact_cadence_and_delayed_service_coalesce();
    test_noncurrent_fixes_are_suppressed_and_retried();
    test_backpressure_uses_retry_without_false_submission();
    test_sink_failure_and_unknown_result_are_typed();
    test_stop_is_immediate_and_restart_is_immediate();
    test_clock_regression_latches_scheduler_off();
    test_malformed_current_snapshot_never_reaches_sink();
    test_time_horizon_and_fake_capacity_are_bounded();

    if (failures != 0) {
        std::cerr << failures
                  << " position broadcast scheduler assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position broadcast scheduler scenario groups\n";
    return EXIT_SUCCESS;
}
