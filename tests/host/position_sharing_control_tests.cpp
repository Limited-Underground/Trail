#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_local_interface.hpp"
#include "fake_position_broadcast_sink.hpp"
#include "opentrail/position_sharing_control.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
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

constexpr PositionBroadcastSchedulePolicy policy() {
    return {1000, 100};
}

DisplayCapabilities button_capabilities() {
    return {128, 64, 1, 2, false, true, false};
}

DisplayCapabilities touch_capabilities() {
    return {466, 466, 16, 2, true, false, false};
}

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

void expect_presentable(const PositionSharingPresentationResult& result,
                        const DisplayCapabilities& capabilities) {
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, capabilities};
    EXPECT(result.presentable());
    EXPECT(interface.present(result.frame).ok());
}

void test_stopped_frame_offers_only_start() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto result =
        make_position_sharing_presentation(scheduler.status(), 1);
    EXPECT(result.mapped());
    EXPECT(result.frame.screen == UiScreen::status);
    EXPECT(result.frame.attention == UiAttention::information);
    EXPECT(result.frame.notice == UiNotice::position_sharing_stopped);
    EXPECT(result.frame.action_count == 1);
    EXPECT(result.frame.actions[0].action == UiAction::start_position_sharing);
    expect_presentable(result, button_capabilities());
    expect_presentable(result, touch_capabilities());
}

void test_checked_start_arms_without_transmitting() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto frame =
        make_position_sharing_presentation(scheduler.status(), 2).frame;
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, button_capabilities()};
    EXPECT(interface.present(frame).ok());
    EXPECT(input.enqueue_action(2, 0));
    const auto resolved = interface.poll_action();
    EXPECT(resolved.ok());
    const auto applied =
        apply_position_sharing_action(scheduler, resolved.action, 50);
    EXPECT(applied.applied());
    EXPECT(applied.state_changed);
    EXPECT(scheduler.status().active);
    EXPECT(sink.submit_attempts() == 0);
}

void test_active_frame_stops_immediately_through_checked_ui() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    const auto result =
        make_position_sharing_presentation(scheduler.status(), 3);
    EXPECT(result.frame.notice == UiNotice::position_sharing_active);
    EXPECT(result.frame.actions[0].action == UiAction::stop_position_sharing);

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, touch_capabilities()};
    EXPECT(interface.present(result.frame).ok());
    EXPECT(input.enqueue_action(3, 0));
    const auto resolved = interface.poll_action();
    EXPECT(resolved.ok());
    const auto applied =
        apply_position_sharing_action(scheduler, resolved.action, 1);
    EXPECT(applied.applied());
    EXPECT(applied.state_changed);
    EXPECT(!scheduler.status().active);
    EXPECT(sink.submit_attempts() == 0);
}

void test_missing_fix_is_visible_and_stoppable() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(scheduler.service(LocationSnapshot{}, 0).error ==
           PositionBroadcastScheduleError::no_current_fix);
    const auto result =
        make_position_sharing_presentation(scheduler.status(), 4);
    EXPECT(result.frame.attention == UiAttention::warning);
    EXPECT(result.frame.notice ==
           UiNotice::position_sharing_waiting_for_fix);
    EXPECT(result.frame.actions[0].action == UiAction::stop_position_sharing);
    expect_presentable(result, button_capabilities());
}

void test_sink_pressure_is_deferred_without_false_success() {
    for (const auto sink_error : {PositionBroadcastSinkError::not_ready,
                                  PositionBroadcastSinkError::full}) {
        FakePositionBroadcastSink sink{};
        EXPECT(sink.enqueue_result(sink_error));
        PositionBroadcastScheduler scheduler{sink, policy()};
        EXPECT(scheduler.start(0) == PositionBroadcastScheduleError::none);
        EXPECT(!scheduler.service(current_snapshot(), 0).submitted());
        const auto result =
            make_position_sharing_presentation(scheduler.status(), 5);
        EXPECT(result.frame.notice == UiNotice::position_sharing_deferred);
        EXPECT(result.frame.actions[0].action ==
               UiAction::stop_position_sharing);
        EXPECT(scheduler.status().submitted == 0);
        expect_presentable(result, touch_capabilities());
    }
}

void test_encode_and_sink_failures_remain_stoppable_retries() {
    FakePositionBroadcastSink encode_sink{};
    PositionBroadcastScheduler encode_scheduler{encode_sink, policy()};
    EXPECT(encode_scheduler.start(0) == PositionBroadcastScheduleError::none);
    auto malformed = current_snapshot();
    malformed.error = FixError::no_fix;
    EXPECT(encode_scheduler.service(malformed, 0).error ==
           PositionBroadcastScheduleError::encode_failed);
    const auto encode_result = make_position_sharing_presentation(
        encode_scheduler.status(), 6);
    EXPECT(encode_result.frame.notice == UiNotice::position_sharing_deferred);
    EXPECT(encode_result.frame.action_count == 1);

    FakePositionBroadcastSink failed_sink{};
    EXPECT(failed_sink.enqueue_result(PositionBroadcastSinkError::failed));
    PositionBroadcastScheduler failed_scheduler{failed_sink, policy()};
    EXPECT(failed_scheduler.start(0) == PositionBroadcastScheduleError::none);
    EXPECT(failed_scheduler.service(current_snapshot(), 0).error ==
           PositionBroadcastScheduleError::sink_failed);
    const auto failed_result = make_position_sharing_presentation(
        failed_scheduler.status(), 7);
    EXPECT(failed_result.frame.notice == UiNotice::position_sharing_deferred);
    EXPECT(failed_result.frame.actions[0].action ==
           UiAction::stop_position_sharing);
    EXPECT(apply_position_sharing_action(
               failed_scheduler, UiAction::stop_position_sharing, 1)
               .applied());
    const auto stopped_after_failure = make_position_sharing_presentation(
        failed_scheduler.status(), 8);
    EXPECT(stopped_after_failure.mapped());
    EXPECT(stopped_after_failure.frame.notice ==
           UiNotice::position_sharing_stopped);
}

void test_terminal_scheduler_faults_expose_no_execution_action() {
    FakePositionBroadcastSink invalid_sink{};
    PositionBroadcastScheduler invalid{invalid_sink, {0, 100}};
    const auto invalid_result =
        make_position_sharing_presentation(invalid.status(), 8);
    EXPECT(invalid_result.error ==
           PositionSharingPresentationError::invalid_scheduler_status);
    EXPECT(invalid_result.frame.screen == UiScreen::system_fault);
    EXPECT(invalid_result.frame.notice == UiNotice::position_sharing_failed);
    EXPECT(invalid_result.frame.action_count == 0);
    expect_presentable(invalid_result, button_capabilities());

    FakePositionBroadcastSink clock_sink{};
    PositionBroadcastScheduler clock{clock_sink, policy()};
    EXPECT(clock.start(100) == PositionBroadcastScheduleError::none);
    EXPECT(clock.service(current_snapshot(), 99).error ==
           PositionBroadcastScheduleError::clock_regression);
    const auto clock_result =
        make_position_sharing_presentation(clock.status(), 9);
    EXPECT(clock_result.frame.notice == UiNotice::position_sharing_failed);
    EXPECT(clock_result.frame.action_count == 0);

    FakePositionBroadcastSink horizon_sink{};
    PositionBroadcastScheduler horizon{horizon_sink, policy()};
    const auto near_max = std::numeric_limits<std::uint64_t>::max() - 500U;
    EXPECT(horizon.start(near_max) == PositionBroadcastScheduleError::none);
    EXPECT(horizon.service(current_snapshot(), near_max).submitted());
    const auto horizon_result =
        make_position_sharing_presentation(horizon.status(), 10);
    EXPECT(horizon_result.frame.notice == UiNotice::position_sharing_failed);
    EXPECT(horizon_result.frame.action_count == 0);

    auto unknown = horizon.status();
    unknown.policy_valid = true;
    unknown.active = true;
    unknown.clock_failed = false;
    unknown.time_exhausted = false;
    unknown.last_error = static_cast<PositionBroadcastScheduleError>(255);
    const auto unknown_result =
        make_position_sharing_presentation(unknown, 11);
    EXPECT(unknown_result.error ==
           PositionSharingPresentationError::invalid_scheduler_status);
    EXPECT(unknown_result.frame.notice == UiNotice::position_sharing_failed);
    EXPECT(unknown_result.frame.action_count == 0);
}

void test_revision_guards_prevent_stale_privacy_actions() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto invalid =
        make_position_sharing_presentation(scheduler.status(), 0);
    EXPECT(invalid.error == PositionSharingPresentationError::invalid_revision);
    EXPECT(!invalid.presentable());

    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface interface{display, input, button_capabilities()};
    const auto stopped =
        make_position_sharing_presentation(scheduler.status(), 20).frame;
    EXPECT(interface.present(stopped).ok());
    EXPECT(apply_position_sharing_action(
               scheduler, UiAction::start_position_sharing, 0)
               .applied());
    const auto active =
        make_position_sharing_presentation(scheduler.status(), 21).frame;
    EXPECT(interface.present(active).ok());
    EXPECT(input.enqueue_action(20, 0));
    const auto stale = interface.poll_action();
    EXPECT(stale.error == ActionResolutionError::stale_frame);
    EXPECT(scheduler.status().active);
}

void test_unrelated_and_unknown_actions_cannot_mutate_scheduler() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto before = scheduler.status();
    for (const auto action : {UiAction::show_status,
                              UiAction::acknowledge_notice,
                              static_cast<UiAction>(255)}) {
        const auto result =
            apply_position_sharing_action(scheduler, action, 100);
        EXPECT(result.error == PositionSharingControlError::invalid_action);
        EXPECT(!result.state_changed);
    }
    const auto after = scheduler.status();
    EXPECT(after.start_attempts == before.start_attempts);
    EXPECT(after.stops == before.stops);
    EXPECT(sink.submit_attempts() == 0);
}

void test_repeated_actions_are_idempotent_and_failures_are_typed() {
    FakePositionBroadcastSink sink{};
    PositionBroadcastScheduler scheduler{sink, policy()};
    const auto first = apply_position_sharing_action(
        scheduler, UiAction::start_position_sharing, 0);
    const auto second = apply_position_sharing_action(
        scheduler, UiAction::start_position_sharing, 10);
    EXPECT(first.applied() && first.state_changed);
    EXPECT(second.applied() && !second.state_changed);
    const auto stopped = apply_position_sharing_action(
        scheduler, UiAction::stop_position_sharing, 20);
    const auto stopped_again = apply_position_sharing_action(
        scheduler, UiAction::stop_position_sharing, 30);
    EXPECT(stopped.applied() && stopped.state_changed);
    EXPECT(stopped_again.applied() && !stopped_again.state_changed);

    FakePositionBroadcastSink invalid_sink{};
    PositionBroadcastScheduler invalid{invalid_sink, {0, 100}};
    const auto rejected = apply_position_sharing_action(
        invalid, UiAction::start_position_sharing, 0);
    EXPECT(rejected.error == PositionSharingControlError::scheduler_rejected);
    EXPECT(rejected.scheduler_error ==
           PositionBroadcastScheduleError::invalid_policy);
    EXPECT(!rejected.state_changed);
}

static_assert(std::is_trivially_copyable_v<PositionSharingPresentationResult>);
static_assert(std::is_trivially_copyable_v<PositionSharingControlResult>);
static_assert(sizeof(PositionSharingPresentationResult) <= 64);
static_assert(sizeof(PositionSharingControlResult) <= 4);

}  // namespace

int main() {
    test_stopped_frame_offers_only_start();
    test_checked_start_arms_without_transmitting();
    test_active_frame_stops_immediately_through_checked_ui();
    test_missing_fix_is_visible_and_stoppable();
    test_sink_pressure_is_deferred_without_false_success();
    test_encode_and_sink_failures_remain_stoppable_retries();
    test_terminal_scheduler_faults_expose_no_execution_action();
    test_revision_guards_prevent_stale_privacy_actions();
    test_unrelated_and_unknown_actions_cannot_mutate_scheduler();
    test_repeated_actions_are_idempotent_and_failures_are_typed();

    if (failures != 0) {
        std::cerr << failures
                  << " position sharing control assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 position sharing control scenario groups\n";
    return EXIT_SUCCESS;
}
