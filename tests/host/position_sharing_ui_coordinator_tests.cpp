#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_gps_provider.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "fake_position_broadcast_sink.hpp"
#include "fake_radio_transport.hpp"
#include "opentrail/position_sharing_ui_coordinator.hpp"

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
    return {128, 64, 1, 2, false, true, false};
}

struct UiFixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeGpsProvider gps{};
    LocationTracker location{gps, 1000};
    FakePositionBroadcastSink position_sink{};
    PositionBroadcastScheduler scheduler{position_sink, {1000, 100}};
    PriorityTrafficQueue queue{queue_policy()};
    FakeRadioTransport radio{64};
    DeliveryController delivery{radio};
    PriorityDeliveryHandoff handoff{queue, delivery};
    OutboundServiceCoordinator outbound{
        clock, location, scheduler, handoff, delivery, radio};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{
        display, input, button_capabilities()};
    PositionSharingUiCoordinator ui;

    explicit UiFixture(std::uint32_t initial_revision = 1)
        : ui(outbound, local, initial_revision) {}
};

void test_first_service_publishes_owned_stopped_revision() {
    UiFixture fixture{};
    const auto result = fixture.ui.service();
    EXPECT(result.disposition == PositionSharingUiDisposition::presented);
    EXPECT(result.error == PositionSharingUiError::none);
    EXPECT(result.frame_presented);
    EXPECT(result.revision == 1);
    EXPECT(fixture.display.latest_frame().revision == 1);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::position_sharing_stopped);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::start_position_sharing);
    EXPECT(fixture.ui.status().next_revision == 2);
}

void test_idle_poll_does_not_refresh_or_touch_runtime() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    const auto idle = fixture.ui.service();
    EXPECT(idle.disposition == PositionSharingUiDisposition::idle);
    EXPECT(idle.action_error == ActionResolutionError::input_not_ready);
    EXPECT(!idle.frame_presented);
    EXPECT(fixture.display.present_count() == 1);
    EXPECT(fixture.outbound.status().position_command_calls == 0);
    EXPECT(fixture.ui.status().idle_polls == 1);
}

void test_start_uses_checked_time_and_publishes_active_revision() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_time(40));
    const auto started = fixture.ui.service();
    EXPECT(started.disposition ==
           PositionSharingUiDisposition::action_applied);
    EXPECT(started.state_changed);
    EXPECT(started.frame_presented);
    EXPECT(started.revision == 2);
    EXPECT(fixture.scheduler.status().active);
    EXPECT(fixture.scheduler.status().last_now_ms == 40);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::position_sharing_active);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::stop_position_sharing);
    EXPECT(fixture.position_sink.submit_attempts() == 0);
}

void test_stop_is_clock_independent_and_publishes_stopped_revision() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_time(50));
    EXPECT(fixture.ui.service().state_changed);
    const auto reads_before = fixture.clock_source.read_count();
    EXPECT(fixture.input.enqueue_action(2, 0));
    const auto stopped = fixture.ui.service();
    EXPECT(stopped.disposition ==
           PositionSharingUiDisposition::action_applied);
    EXPECT(stopped.state_changed);
    EXPECT(stopped.revision == 3);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.clock_source.read_count() == reads_before);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::position_sharing_stopped);
}

void test_not_ready_keeps_start_frame_for_fresh_retry() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_not_ready());
    const auto deferred = fixture.ui.service();
    EXPECT(deferred.disposition ==
           PositionSharingUiDisposition::action_deferred);
    EXPECT(deferred.control_error ==
           PositionSharingControlError::outbound_not_ready);
    EXPECT(!deferred.frame_presented);
    EXPECT(fixture.ui.status().active_revision == 1);
    EXPECT(fixture.display.present_count() == 1);
    EXPECT(fixture.scheduler.status().start_attempts == 0);

    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_time(60));
    const auto retried = fixture.ui.service();
    EXPECT(retried.disposition ==
           PositionSharingUiDisposition::action_applied);
    EXPECT(retried.revision == 2);
    EXPECT(fixture.scheduler.status().active);
    EXPECT(fixture.ui.status().actions_deferred == 1);
}

void test_permanent_clock_fault_publishes_no_action_frame() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_failure());
    const auto rejected = fixture.ui.service();
    EXPECT(rejected.disposition ==
           PositionSharingUiDisposition::action_rejected);
    EXPECT(rejected.control_error ==
           PositionSharingControlError::outbound_faulted);
    EXPECT(rejected.presentation_error ==
           PositionSharingPresentationError::outbound_faulted);
    EXPECT(rejected.frame_presented);
    EXPECT(fixture.display.latest_frame().screen == UiScreen::system_fault);
    EXPECT(fixture.display.latest_frame().action_count == 0);
    EXPECT(fixture.outbound.status().faulted);
    EXPECT(!fixture.ui.status().faulted);
}

void test_stale_and_failed_input_never_mutate_position() {
    UiFixture fixture{};
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(2, 0));
    const auto stale = fixture.ui.service();
    EXPECT(stale.disposition ==
           PositionSharingUiDisposition::input_rejected);
    EXPECT(stale.action_error == ActionResolutionError::stale_frame);
    EXPECT(fixture.outbound.status().position_command_calls == 0);
    EXPECT(fixture.ui.status().active_revision == 1);

    EXPECT(fixture.input.enqueue_failure());
    const auto failed = fixture.ui.service();
    EXPECT(failed.disposition == PositionSharingUiDisposition::failed);
    EXPECT(failed.error == PositionSharingUiError::input_failed);
    EXPECT(!fixture.ui.status().faulted);
    EXPECT(fixture.outbound.status().position_command_calls == 0);
}

void test_initial_display_failure_retries_same_revision() {
    UiFixture fixture{};
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::not_ready));
    const auto failed = fixture.ui.service();
    EXPECT(failed.disposition == PositionSharingUiDisposition::failed);
    EXPECT(failed.error == PositionSharingUiError::display_failed);
    EXPECT(failed.present_error == PresentError::sink_not_ready);
    EXPECT(!fixture.ui.status().faulted);
    EXPECT(!fixture.ui.status().has_presented_frame);
    EXPECT(fixture.ui.status().next_revision == 1);

    const auto retried = fixture.ui.service();
    EXPECT(retried.disposition == PositionSharingUiDisposition::presented);
    EXPECT(retried.revision == 1);
    EXPECT(fixture.local.status().active_revision == 1);
}

void test_post_action_display_failure_stops_and_latches() {
    UiFixture fixture{};
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::none));
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::sink_failed));
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 0));
    EXPECT(fixture.clock_source.enqueue_time(70));
    const auto failed = fixture.ui.service();
    EXPECT(failed.disposition == PositionSharingUiDisposition::failed);
    EXPECT(failed.error ==
           PositionSharingUiError::post_action_refresh_failed);
    EXPECT(failed.present_error == PresentError::sink_failed);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.ui.status().faulted);
    EXPECT(fixture.ui.status().latched_error ==
           PositionSharingUiError::post_action_refresh_failed);
    EXPECT(fixture.outbound.status().position_command_calls == 2);
    const auto blocked = fixture.ui.service();
    EXPECT(blocked.error ==
           PositionSharingUiError::post_action_refresh_failed);
    EXPECT(fixture.input.read_count() == 1);
}

void test_revision_exhaustion_and_invalid_seed_fail_closed() {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    UiFixture exhausted{maximum - 1U};
    EXPECT(exhausted.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(exhausted.ui.service().revision == maximum - 1U);
    EXPECT(exhausted.input.enqueue_action(maximum - 1U, 0));
    const auto blocked = exhausted.ui.service();
    EXPECT(blocked.error == PositionSharingUiError::revision_exhausted);
    EXPECT(exhausted.ui.status().faulted);
    EXPECT(!exhausted.scheduler.status().active);
    EXPECT(exhausted.input.read_count() == 0);
    EXPECT(exhausted.input.queued_count() == 1);

    UiFixture invalid{maximum};
    const auto invalid_result = invalid.ui.service();
    EXPECT(invalid_result.error ==
           PositionSharingUiError::invalid_initial_revision);
    EXPECT(invalid.display.present_count() == 0);
    EXPECT(invalid.input.read_count() == 0);
}

static_assert(std::is_trivially_copyable_v<PositionSharingUiServiceResult>);
static_assert(
    std::is_trivially_copyable_v<PositionSharingUiCoordinatorStatus>);
static_assert(sizeof(PositionSharingUiServiceResult) <= 24);
static_assert(sizeof(PositionSharingUiCoordinatorStatus) <= 56);

}  // namespace

int main() {
    test_first_service_publishes_owned_stopped_revision();
    test_idle_poll_does_not_refresh_or_touch_runtime();
    test_start_uses_checked_time_and_publishes_active_revision();
    test_stop_is_clock_independent_and_publishes_stopped_revision();
    test_not_ready_keeps_start_frame_for_fresh_retry();
    test_permanent_clock_fault_publishes_no_action_frame();
    test_stale_and_failed_input_never_mutate_position();
    test_initial_display_failure_retries_same_revision();
    test_post_action_display_failure_stops_and_latches();
    test_revision_exhaustion_and_invalid_seed_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " position sharing UI coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position sharing UI coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
