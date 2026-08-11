#include <array>
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

GpsFix current_fix(std::uint64_t received_at_ms = 0) {
    GpsFix fix{};
    fix.latitude_e7 = 449775000;
    fix.longitude_e7 = -677500000;
    fix.horizontal_accuracy_cm = 250;
    fix.horizontal_accuracy_valid = true;
    fix.received_at_ms = received_at_ms;
    return fix;
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

void start_sharing(UiFixture& fixture, std::uint64_t now_ms = 0) {
    EXPECT(fixture.ui.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(
        fixture.ui.status().active_revision, 0));
    EXPECT(fixture.clock_source.enqueue_time(now_ms));
    const auto started = fixture.ui.service();
    EXPECT(started.disposition ==
           PositionSharingUiDisposition::action_applied);
    EXPECT(fixture.scheduler.status().active);
}

OutboundServiceResult service_outbound(UiFixture& fixture,
                                       std::uint64_t now_ms) {
    EXPECT(fixture.clock_source.enqueue_time(now_ms));
    return fixture.outbound.service();
}

void test_missing_fix_refreshes_waiting_state_before_input() {
    UiFixture fixture{};
    start_sharing(fixture);
    const auto input_reads = fixture.input.read_count();
    const auto outbound = service_outbound(fixture, 1);
    EXPECT(outbound.position.error ==
           PositionBroadcastScheduleError::no_current_fix);

    const auto refreshed = fixture.ui.service();
    EXPECT(refreshed.disposition ==
           PositionSharingUiDisposition::refreshed);
    EXPECT(refreshed.frame_presented);
    EXPECT(refreshed.revision == 3);
    EXPECT(refreshed.presented_notice ==
           UiNotice::position_sharing_waiting_for_fix);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::position_sharing_waiting_for_fix);
    EXPECT(fixture.input.read_count() == input_reads);
    EXPECT(fixture.ui.status().state_refreshes == 1);
    EXPECT(fixture.ui.status().presented_frames == 3);
}

void test_fresh_fix_refreshes_waiting_state_back_to_active() {
    UiFixture fixture{};
    start_sharing(fixture);
    EXPECT(service_outbound(fixture, 1).position.error ==
           PositionBroadcastScheduleError::no_current_fix);
    EXPECT(fixture.ui.service().disposition ==
           PositionSharingUiDisposition::refreshed);

    fixture.gps.set_fix(current_fix(100));
    EXPECT(service_outbound(fixture, 101).position.submitted());
    const auto recovered = fixture.ui.service();
    EXPECT(recovered.disposition ==
           PositionSharingUiDisposition::refreshed);
    EXPECT(recovered.revision == 4);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::position_sharing_active);
    EXPECT(fixture.ui.status().state_refreshes == 2);
}

void test_sink_pressure_and_failure_refresh_to_deferred() {
    constexpr std::array<PositionBroadcastSinkError, 3> errors{{
        PositionBroadcastSinkError::not_ready,
        PositionBroadcastSinkError::full,
        PositionBroadcastSinkError::failed,
    }};
    for (const auto error : errors) {
        UiFixture fixture{};
        start_sharing(fixture);
        fixture.gps.set_fix(current_fix());
        EXPECT(fixture.position_sink.enqueue_result(error));
        const auto outbound = service_outbound(fixture, 1);
        EXPECT(outbound.position.disposition ==
               PositionBroadcastScheduleDisposition::deferred);
        const auto refreshed = fixture.ui.service();
        EXPECT(refreshed.disposition ==
               PositionSharingUiDisposition::refreshed);
        EXPECT(fixture.display.latest_frame().notice ==
               UiNotice::position_sharing_deferred);
    }
}

void test_external_clock_fault_refreshes_critical_no_action_state() {
    UiFixture fixture{};
    start_sharing(fixture, 10);
    EXPECT(fixture.clock_source.enqueue_failure());
    const auto outbound = fixture.outbound.service();
    EXPECT(outbound.disposition == OutboundServiceDisposition::failed);
    EXPECT(!fixture.scheduler.status().active);

    const auto refreshed = fixture.ui.service();
    EXPECT(refreshed.disposition ==
           PositionSharingUiDisposition::refreshed);
    EXPECT(refreshed.presentation_error ==
           PositionSharingPresentationError::outbound_faulted);
    EXPECT(fixture.display.latest_frame().screen == UiScreen::system_fault);
    EXPECT(fixture.display.latest_frame().attention ==
           UiAttention::critical);
    EXPECT(fixture.display.latest_frame().action_count == 0);
}

void test_observed_refresh_updates_only_refresh_counters() {
    UiFixture fixture{};
    start_sharing(fixture);
    EXPECT(service_outbound(fixture, 1).position.error ==
           PositionBroadcastScheduleError::no_current_fix);
    const auto input_reads = fixture.input.read_count();

    EXPECT(fixture.ui.service().disposition ==
           PositionSharingUiDisposition::refreshed);
    const auto status = fixture.ui.status();
    EXPECT(status.service_calls == 3);
    EXPECT(status.presented_frames == 3);
    EXPECT(status.state_refreshes == 1);
    EXPECT(status.resolved_actions == 1);
    EXPECT(status.actions_applied == 1);
    EXPECT(status.idle_polls == 0);
    EXPECT(status.failures == 0);
    EXPECT(fixture.input.read_count() == input_reads);
}

void test_queued_old_action_waits_then_is_rejected_stale() {
    UiFixture fixture{};
    start_sharing(fixture);
    EXPECT(fixture.input.enqueue_action(2, 0));
    const auto input_reads = fixture.input.read_count();
    EXPECT(service_outbound(fixture, 1).position.error ==
           PositionBroadcastScheduleError::no_current_fix);

    EXPECT(fixture.ui.service().disposition ==
           PositionSharingUiDisposition::refreshed);
    EXPECT(fixture.input.read_count() == input_reads);
    EXPECT(fixture.input.queued_count() == 1);
    const auto stale = fixture.ui.service();
    EXPECT(stale.disposition ==
           PositionSharingUiDisposition::input_rejected);
    EXPECT(stale.action_error == ActionResolutionError::stale_frame);
    EXPECT(fixture.scheduler.status().active);
    EXPECT(fixture.outbound.status().position_command_calls == 1);
}

void test_external_refresh_display_failure_contains_and_latches() {
    UiFixture fixture{};
    start_sharing(fixture);
    EXPECT(service_outbound(fixture, 1).position.error ==
           PositionBroadcastScheduleError::no_current_fix);
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::sink_failed));

    const auto failed = fixture.ui.service();
    EXPECT(failed.disposition == PositionSharingUiDisposition::failed);
    EXPECT(failed.error ==
           PositionSharingUiError::external_refresh_failed);
    EXPECT(failed.present_error == PresentError::sink_failed);
    EXPECT(!failed.frame_presented);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.ui.status().faulted);
    EXPECT(fixture.ui.status().latched_error ==
           PositionSharingUiError::external_refresh_failed);
    EXPECT(fixture.ui.status().failures == 1);
    EXPECT(fixture.outbound.status().position_command_calls == 2);
}

void test_preexisting_fault_is_safe_initial_presentation() {
    UiFixture fixture{};
    EXPECT(fixture.clock_source.enqueue_failure());
    EXPECT(fixture.outbound.service().disposition ==
           OutboundServiceDisposition::failed);

    const auto initial = fixture.ui.service();
    EXPECT(initial.disposition == PositionSharingUiDisposition::presented);
    EXPECT(initial.presentation_error ==
           PositionSharingPresentationError::outbound_faulted);
    EXPECT(initial.revision == 1);
    EXPECT(fixture.display.latest_frame().screen == UiScreen::system_fault);
    EXPECT(fixture.display.latest_frame().action_count == 0);
    EXPECT(!fixture.ui.status().faulted);
    EXPECT(fixture.ui.status().state_refreshes == 0);
}

void test_revision_exhaustion_blocks_observed_change_and_input() {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    UiFixture fixture{maximum - 1U};
    EXPECT(fixture.scheduler.start(0) ==
           PositionBroadcastScheduleError::none);
    EXPECT(fixture.ui.service().revision == maximum - 1U);
    EXPECT(fixture.input.enqueue_action(maximum - 1U, 0));
    EXPECT(service_outbound(fixture, 1).position.error ==
           PositionBroadcastScheduleError::no_current_fix);

    const auto blocked = fixture.ui.service();
    EXPECT(blocked.error == PositionSharingUiError::revision_exhausted);
    EXPECT(!fixture.scheduler.status().active);
    EXPECT(fixture.ui.status().faulted);
    EXPECT(fixture.input.read_count() == 0);
    EXPECT(fixture.input.queued_count() == 1);
    EXPECT(fixture.display.present_count() == 1);
}

void test_nonvisible_runtime_progress_does_not_churn_revisions() {
    UiFixture fixture{};
    start_sharing(fixture);
    fixture.gps.set_fix(current_fix());
    EXPECT(service_outbound(fixture, 1).position.submitted());
    const auto after_submit = fixture.ui.service();
    EXPECT(after_submit.disposition == PositionSharingUiDisposition::idle);
    EXPECT(after_submit.action_error ==
           ActionResolutionError::input_not_ready);

    EXPECT(service_outbound(fixture, 2).position.disposition ==
           PositionBroadcastScheduleDisposition::not_due);
    const auto after_counters = fixture.ui.service();
    EXPECT(after_counters.disposition == PositionSharingUiDisposition::idle);
    EXPECT(fixture.ui.status().active_revision == 2);
    EXPECT(fixture.ui.status().next_revision == 3);
    EXPECT(fixture.ui.status().state_refreshes == 0);
    EXPECT(fixture.display.present_count() == 2);
}

static_assert(std::is_trivially_copyable_v<PositionSharingUiServiceResult>);
static_assert(
    std::is_trivially_copyable_v<PositionSharingUiCoordinatorStatus>);
static_assert(sizeof(PositionSharingUiServiceResult) <= 24);
static_assert(sizeof(PositionSharingUiCoordinatorStatus) <= 64);

}  // namespace

int main() {
    test_missing_fix_refreshes_waiting_state_before_input();
    test_fresh_fix_refreshes_waiting_state_back_to_active();
    test_sink_pressure_and_failure_refresh_to_deferred();
    test_external_clock_fault_refreshes_critical_no_action_state();
    test_observed_refresh_updates_only_refresh_counters();
    test_queued_old_action_waits_then_is_rejected_stale();
    test_external_refresh_display_failure_contains_and_latches();
    test_preexisting_fault_is_safe_initial_presentation();
    test_revision_exhaustion_blocks_observed_change_and_input();
    test_nonvisible_runtime_progress_does_not_churn_revisions();

    if (failures != 0) {
        std::cerr << failures
                  << " position sharing UI observation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position sharing UI observation scenario groups\n";
    return EXIT_SUCCESS;
}
