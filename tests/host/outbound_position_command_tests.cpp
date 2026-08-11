#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_gps_provider.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "fake_position_broadcast_sink.hpp"
#include "fake_radio_transport.hpp"
#include "opentrail/outbound_service_coordinator.hpp"
#include "opentrail/position_sharing_control.hpp"

namespace {

using namespace opentrail::delivery;
using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
using namespace opentrail::radio::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;
using namespace opentrail::ui;
using namespace opentrail::ui::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

PriorityQueuePolicy queue_policy() {
    return {
        8,
        0,
        {{{20, 1000}, {20, 1000}, {20, 1000}, {20, 1000}}},
    };
}

DisplayCapabilities button_capabilities() {
    return {128, 64, 1, 2, false, true, true};
}

struct CommandFixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeGpsProvider gps{};
    LocationTracker location{gps, 1000};
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, {1000, 100}};
    PriorityTrafficQueue queue{queue_policy()};
    FakeRadioTransport radio{64};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    OutboundServiceCoordinator runtime{
        clock, location, scheduler, handoff, delivery, radio};
};

struct InvalidPolicyFixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeGpsProvider gps{};
    LocationTracker location{gps, 1000};
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, {0, 100}};
    PriorityTrafficQueue queue{queue_policy()};
    FakeRadioTransport radio{64};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    OutboundServiceCoordinator runtime{
        clock, location, scheduler, handoff, delivery, radio};
};

void test_start_owns_checked_time_without_submitting() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(42));
    const auto result = fixture.runtime.start_position_sharing();
    EXPECT(result.applied());
    EXPECT(result.state_changed);
    EXPECT(result.clock_error == MonotonicClockError::none);
    EXPECT(fixture.clock_source.read_count() == 1);
    EXPECT(fixture.scheduler.status().active);
    EXPECT(fixture.scheduler.status().last_now_ms == 42);
    EXPECT(fixture.runtime.status().last_now_ms == 42);
    EXPECT(fixture.sink.submit_attempts() == 0);
    EXPECT(fixture.gps.read_count() == 0);
}

void test_not_ready_defers_then_a_fresh_sample_starts() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_not_ready());
    EXPECT(fixture.clock_source.enqueue_time(50));
    const auto deferred = fixture.runtime.start_position_sharing();
    EXPECT(deferred.error == OutboundPositionCommandError::clock_not_ready);
    EXPECT(deferred.clock_error == MonotonicClockError::not_ready);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.scheduler.status().start_attempts == 0);

    const auto started = fixture.runtime.start_position_sharing();
    EXPECT(started.applied());
    EXPECT(started.state_changed);
    const auto status = fixture.runtime.status();
    EXPECT(status.position_command_calls == 2);
    EXPECT(status.position_commands_deferred == 1);
    EXPECT(status.position_commands_applied == 1);
    EXPECT(status.clock_deferred == 1);
}

void test_source_failure_latches_and_stops_active_sharing() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(10));
    EXPECT(fixture.runtime.start_position_sharing().applied());
    EXPECT(fixture.clock_source.enqueue_failure());
    const auto failed = fixture.runtime.start_position_sharing();
    EXPECT(failed.error == OutboundPositionCommandError::clock_faulted);
    EXPECT(failed.clock_error == MonotonicClockError::source_failed);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.runtime.status().faulted);
    EXPECT(fixture.runtime.status().clock_failures == 1);
    EXPECT(fixture.runtime.status().position_command_failures == 1);
}

void test_rollback_during_start_latches_and_stops() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(100));
    EXPECT(fixture.runtime.start_position_sharing().applied());
    EXPECT(fixture.clock_source.enqueue_time(99));
    const auto failed = fixture.runtime.start_position_sharing();
    EXPECT(failed.error == OutboundPositionCommandError::clock_faulted);
    EXPECT(failed.clock_error == MonotonicClockError::rollback_detected);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.runtime.status().latched_clock_error ==
           MonotonicClockError::rollback_detected);
}

void test_start_after_latch_consumes_no_more_clock_samples() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(fixture.runtime.start_position_sharing().error ==
           OutboundPositionCommandError::clock_faulted);
    EXPECT(fixture.clock_source.enqueue_time(200));
    const auto refused = fixture.runtime.start_position_sharing();
    EXPECT(refused.error == OutboundPositionCommandError::clock_faulted);
    EXPECT(refused.clock_error == MonotonicClockError::fault_latched);
    EXPECT(fixture.clock_source.read_count() == 1);
    EXPECT(fixture.clock_source.queued_count() == 1);
    EXPECT(fixture.scheduler.status().start_attempts == 0);
}

void test_stop_is_immediate_and_clock_independent() {
    CommandFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_time(999));
    const auto stopped = fixture.runtime.stop_position_sharing();
    EXPECT(stopped.applied());
    EXPECT(stopped.state_changed);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.clock_source.queued_count() == 1);
}

void test_repeated_commands_are_typed_and_idempotent() {
    CommandFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(7));
    EXPECT(fixture.clock_source.enqueue_time(7));
    const auto first_start = fixture.runtime.start_position_sharing();
    const auto second_start = fixture.runtime.start_position_sharing();
    const auto first_stop = fixture.runtime.stop_position_sharing();
    const auto second_stop = fixture.runtime.stop_position_sharing();
    EXPECT(first_start.state_changed);
    EXPECT(second_start.applied() && !second_start.state_changed);
    EXPECT(first_stop.state_changed);
    EXPECT(second_stop.applied() && !second_stop.state_changed);
    const auto status = fixture.runtime.status();
    EXPECT(status.position_command_calls == 4);
    EXPECT(status.position_commands_applied == 4);
    EXPECT(status.position_commands_deferred == 0);
    EXPECT(status.position_command_failures == 0);
}

void test_invalid_scheduler_policy_is_a_typed_rejection() {
    InvalidPolicyFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_time(1));
    const auto result = fixture.runtime.start_position_sharing();
    EXPECT(result.error ==
           OutboundPositionCommandError::scheduler_rejected);
    EXPECT(result.clock_error == MonotonicClockError::none);
    EXPECT(result.scheduler_error ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(!fixture.runtime.status().faulted);
    EXPECT(fixture.runtime.status().has_time);
    EXPECT(fixture.runtime.status().position_command_failures == 1);
}

void test_target_adapter_maps_retry_fault_and_success() {
    CommandFixture retry{};
    EXPECT(retry.clock_source.enqueue_not_ready());
    const auto deferred = apply_position_sharing_action(
        retry.runtime, UiAction::start_position_sharing);
    EXPECT(deferred.error == PositionSharingControlError::outbound_not_ready);
    EXPECT(retry.scheduler.status().start_attempts == 0);

    CommandFixture fault{};
    EXPECT(fault.clock_source.enqueue_failure());
    const auto failed = apply_position_sharing_action(
        fault.runtime, UiAction::start_position_sharing);
    EXPECT(failed.error == PositionSharingControlError::outbound_faulted);

    CommandFixture healthy{};
    EXPECT(healthy.clock_source.enqueue_time(60));
    const auto applied = apply_position_sharing_action(
        healthy.runtime, UiAction::start_position_sharing);
    EXPECT(applied.applied());
    EXPECT(applied.state_changed);
}

void test_resolved_ui_start_uses_action_time_sample() {
    CommandFixture fixture{};
    const auto presentation = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 22);
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{
        display, input, button_capabilities()};
    EXPECT(interface.present(presentation.frame).ok());
    EXPECT(input.enqueue_action(22, 0));
    const auto action = interface.poll_action();
    EXPECT(action.ok());

    EXPECT(fixture.clock_source.enqueue_time(800));
    const auto applied = apply_position_sharing_action(
        fixture.runtime, action.action);
    EXPECT(applied.applied());
    EXPECT(fixture.scheduler.status().last_now_ms == 800);
    EXPECT(fixture.runtime.status().last_now_ms == 800);
    EXPECT(fixture.clock_source.read_count() == 1);
    EXPECT(fixture.sink.submit_attempts() == 0);
}

static_assert(std::is_trivially_copyable_v<OutboundPositionCommandResult>);
static_assert(sizeof(OutboundPositionCommandResult) <= 16);
static_assert(sizeof(OutboundServiceStatus) <= 56);

}  // namespace

int main() {
    test_start_owns_checked_time_without_submitting();
    test_not_ready_defers_then_a_fresh_sample_starts();
    test_source_failure_latches_and_stops_active_sharing();
    test_rollback_during_start_latches_and_stops();
    test_start_after_latch_consumes_no_more_clock_samples();
    test_stop_is_immediate_and_clock_independent();
    test_repeated_commands_are_typed_and_idempotent();
    test_invalid_scheduler_policy_is_a_typed_rejection();
    test_target_adapter_maps_retry_fault_and_success();
    test_resolved_ui_start_uses_action_time_sample();

    if (failures != 0) {
        std::cerr << failures
                  << " outbound position command assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 outbound position command scenario groups\n";
    return EXIT_SUCCESS;
}
