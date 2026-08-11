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

DisplayCapabilities button_capabilities() {
    return {128, 64, 1, 2, false, true, true};
}

PriorityQueuePolicy queue_policy() {
    return {
        8,
        0,
        {{{20, 1000}, {20, 1000}, {20, 1000}, {20, 1000}}},
    };
}

struct FaultFixture {
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

void expect_fault_frame(const PositionSharingPresentationResult& result,
                        std::uint32_t revision) {
    EXPECT(result.presentable());
    EXPECT(result.frame.revision == revision);
    EXPECT(result.frame.screen == UiScreen::system_fault);
    EXPECT(result.frame.attention == UiAttention::critical);
    EXPECT(result.frame.notice == UiNotice::position_sharing_failed);
    EXPECT(result.frame.action_count == 0);
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{
        display, input, button_capabilities()};
    EXPECT(interface.present(result.frame).ok());
}

void test_healthy_stopped_runtime_preserves_start_frame() {
    FaultFixture fixture{};
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 1);
    EXPECT(result.mapped());
    EXPECT(result.frame.notice == UiNotice::position_sharing_stopped);
    EXPECT(result.frame.action_count == 1);
    EXPECT(result.frame.actions[0].action ==
           UiAction::start_position_sharing);
}

void test_healthy_active_runtime_preserves_stop_action() {
    FaultFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 2);
    EXPECT(result.mapped());
    EXPECT(result.frame.actions[0].action ==
           UiAction::stop_position_sharing);
    const auto applied = apply_position_sharing_action(
        fixture.scheduler,
        fixture.runtime.status(),
        UiAction::stop_position_sharing,
        1);
    EXPECT(applied.applied());
    EXPECT(applied.state_changed);
}

void test_real_source_failure_overrides_stopped_presentation() {
    FaultFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(fixture.runtime.service().clock_error ==
           MonotonicClockError::source_failed);
    EXPECT(!fixture.scheduler.status().active);
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 3);
    EXPECT(result.error == PositionSharingPresentationError::outbound_faulted);
    expect_fault_frame(result, 3);
}

void test_real_rollback_overrides_stopped_presentation() {
    FaultFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_time(100));
    EXPECT(fixture.clock_source.enqueue_time(99));
    EXPECT(fixture.runtime.service().serviced());
    EXPECT(fixture.runtime.service().clock_error ==
           MonotonicClockError::rollback_detected);
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 4);
    EXPECT(result.error == PositionSharingPresentationError::outbound_faulted);
    expect_fault_frame(result, 4);
}

void test_previously_resolved_start_is_rejected_after_fault() {
    FaultFixture fixture{};
    const auto healthy = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 5);
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{
        display, input, button_capabilities()};
    EXPECT(interface.present(healthy.frame).ok());
    EXPECT(input.enqueue_action(5, 0));
    const auto resolved = interface.poll_action();
    EXPECT(resolved.ok());

    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(!fixture.runtime.service().serviced());
    const auto before = fixture.scheduler.status().start_attempts;
    const auto rejected = apply_position_sharing_action(
        fixture.scheduler,
        fixture.runtime.status(),
        resolved.action,
        10);
    EXPECT(rejected.error == PositionSharingControlError::outbound_faulted);
    EXPECT(!rejected.state_changed);
    EXPECT(fixture.scheduler.status().start_attempts == before);
}

void test_presented_fault_revision_rejects_old_input() {
    FaultFixture fixture{};
    const auto healthy = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 6);
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{
        display, input, button_capabilities()};
    EXPECT(interface.present(healthy.frame).ok());
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(!fixture.runtime.service().serviced());
    const auto fault = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 7);
    EXPECT(interface.present(fault.frame).ok());
    EXPECT(input.enqueue_action(6, 0));
    EXPECT(interface.poll_action().error == ActionResolutionError::stale_frame);
}

void test_direct_start_is_rejected_without_scheduler_mutation() {
    FaultFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(!fixture.runtime.service().serviced());
    const auto before = fixture.scheduler.status();
    const auto result = apply_position_sharing_action(
        fixture.scheduler,
        fixture.runtime.status(),
        UiAction::start_position_sharing,
        100);
    EXPECT(result.error == PositionSharingControlError::outbound_faulted);
    EXPECT(!result.state_changed);
    EXPECT(fixture.scheduler.status().start_attempts == before.start_attempts);
}

void test_stop_remains_safe_and_idempotent_after_fault() {
    FaultFixture fixture{};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(!fixture.runtime.service().serviced());
    const auto result = apply_position_sharing_action(
        fixture.scheduler,
        fixture.runtime.status(),
        UiAction::stop_position_sharing,
        100);
    EXPECT(result.applied());
    EXPECT(!result.state_changed);
    EXPECT(!fixture.scheduler.status().active);
}

void test_incoherent_runtime_status_fails_closed() {
    FaultFixture fixture{};
    auto invalid = fixture.runtime.status();
    invalid.faulted = true;
    invalid.latched_clock_error = MonotonicClockError::none;
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), invalid, 8);
    EXPECT(result.error ==
           PositionSharingPresentationError::invalid_outbound_status);
    expect_fault_frame(result, 8);
    EXPECT(apply_position_sharing_action(
               fixture.scheduler,
               invalid,
               UiAction::start_position_sharing,
               0)
               .error == PositionSharingControlError::invalid_outbound_status);

    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    const auto stopped = apply_position_sharing_action(
        fixture.scheduler,
        invalid,
        UiAction::stop_position_sharing,
        0);
    EXPECT(stopped.applied());
    EXPECT(stopped.state_changed);
    EXPECT(!fixture.scheduler.status().active);
}

void test_invalid_revision_and_unknown_clock_state_fail_closed() {
    FaultFixture fixture{};
    const auto revision = make_position_sharing_presentation(
        fixture.scheduler.status(), fixture.runtime.status(), 0);
    EXPECT(revision.error ==
           PositionSharingPresentationError::invalid_revision);
    EXPECT(!revision.presentable());

    auto unknown = fixture.runtime.status();
    unknown.latched_clock_error = static_cast<MonotonicClockError>(255);
    const auto result = make_position_sharing_presentation(
        fixture.scheduler.status(), unknown, 9);
    EXPECT(result.error ==
           PositionSharingPresentationError::invalid_outbound_status);
    expect_fault_frame(result, 9);
}

static_assert(std::is_trivially_copyable_v<PositionSharingPresentationResult>);
static_assert(std::is_trivially_copyable_v<PositionSharingControlResult>);

}  // namespace

int main() {
    test_healthy_stopped_runtime_preserves_start_frame();
    test_healthy_active_runtime_preserves_stop_action();
    test_real_source_failure_overrides_stopped_presentation();
    test_real_rollback_overrides_stopped_presentation();
    test_previously_resolved_start_is_rejected_after_fault();
    test_presented_fault_revision_rejects_old_input();
    test_direct_start_is_rejected_without_scheduler_mutation();
    test_stop_remains_safe_and_idempotent_after_fault();
    test_incoherent_runtime_status_fails_closed();
    test_invalid_revision_and_unknown_clock_state_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " outbound position safety assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 outbound position safety scenario groups\n";
    return EXIT_SUCCESS;
}
