#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "opentrail/breadcrumb_archive_workflow_coordinator.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
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

static_assert(!std::is_copy_constructible_v<
              BreadcrumbArchiveWorkflowCoordinator>);
static_assert(!std::is_move_constructible_v<
              BreadcrumbArchiveWorkflowCoordinator>);

class FakeWorkflowLock final : public BreadcrumbArchiveSnapshotLock {
public:
    BreadcrumbArchiveSnapshotLockState next_acquire{
        BreadcrumbArchiveSnapshotLockState::acquired};
    BreadcrumbArchiveSnapshotUnlockState next_release{
        BreadcrumbArchiveSnapshotUnlockState::released};
    std::uint32_t acquire_calls{0};
    std::uint32_t release_calls{0};

    BreadcrumbArchiveSnapshotLockState acquire() override {
        ++acquire_calls;
        const auto result = next_acquire;
        next_acquire = BreadcrumbArchiveSnapshotLockState::acquired;
        return result;
    }

    BreadcrumbArchiveSnapshotUnlockState release() override {
        ++release_calls;
        const auto result = next_release;
        next_release = BreadcrumbArchiveSnapshotUnlockState::released;
        return result;
    }
};

DisplayCapabilities capabilities() {
    return {128, 64, 1, 2, false, true, true};
}

struct Fixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeWorkflowLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner runtime{
        clock, remote, {1'000, 100}, {10, 80}, lock};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveWorkflowCoordinator coordinator;

    Fixture(std::uint64_t initial_session_id = 1,
            std::uint64_t final_session_id =
                std::numeric_limits<std::uint64_t>::max(),
            std::uint32_t initial_revision = 1)
        : coordinator(runtime,
                      clock,
                      local,
                      initial_session_id,
                      final_session_id,
                      initial_revision) {}
};

void present_start_confirmation(Fixture& fixture) {
    const auto initial = fixture.coordinator.service();
    EXPECT(initial.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(fixture.display.latest_frame().screen ==
           UiScreen::archive_controls);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_start);
    EXPECT(fixture.input.enqueue_action(1, 0));
    const auto confirmation = fixture.coordinator.service();
    EXPECT(confirmation.disposition ==
           BreadcrumbArchiveWorkflowDisposition::confirmation_presented);
    EXPECT(confirmation.revision == 2);
    EXPECT(fixture.display.latest_frame().screen ==
           UiScreen::archive_confirmation);
}

void start_archive(Fixture& fixture, std::uint64_t now_ms = 100) {
    present_start_confirmation(fixture);
    EXPECT(fixture.clock_source.enqueue_time(now_ms));
    EXPECT(fixture.input.enqueue_action(2, 0, InputGesture::hold));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_applied);
    EXPECT(result.state_changed);
    EXPECT(result.revision == 3);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_stop);
}

void test_initial_controls_are_snapshot_bound_and_local() {
    Fixture fixture{};
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(result.snapshot_read);
    EXPECT(result.revision == 1);
    EXPECT(fixture.display.latest_frame().screen ==
           UiScreen::archive_controls);
    EXPECT(fixture.display.latest_frame().notice == UiNotice::archive_stopped);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_start);
    EXPECT(fixture.display.latest_frame().actions[1].action ==
           UiAction::cancel);
    EXPECT(fixture.input.read_count() == 0);
}

void test_start_entry_requires_exact_revision_then_hold() {
    Fixture fixture{};
    present_start_confirmation(fixture);
    EXPECT(fixture.input.enqueue_action(2, 0, InputGesture::activate));
    const auto rejected = fixture.coordinator.service();
    EXPECT(rejected.disposition ==
           BreadcrumbArchiveWorkflowDisposition::input_rejected);
    EXPECT(rejected.action_error == ActionResolutionError::hold_required);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.coordinator.status().mode ==
           BreadcrumbArchiveWorkflowMode::start_confirmation);
}

void test_cancel_returns_to_controls_without_runtime_mutation() {
    Fixture fixture{};
    present_start_confirmation(fixture);
    const auto calls = fixture.lock.acquire_calls;
    EXPECT(fixture.input.enqueue_action(2, 1));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::cancelled);
    EXPECT(result.revision == 3);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.lock.acquire_calls == calls + 1);
    EXPECT(fixture.display.latest_frame().screen ==
           UiScreen::archive_controls);
}

void test_confirmed_start_refreshes_active_controls() {
    Fixture fixture{40};
    start_archive(fixture);
    EXPECT(fixture.coordinator.consent_status().next_session_id == 41);
    EXPECT(fixture.coordinator.status().actions_applied == 1);
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.runtime.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(snapshot.session.active);
}

void test_stop_flow_is_immediate_and_clock_independent() {
    Fixture fixture{};
    start_archive(fixture);
    const auto reads = fixture.clock_source.read_count();
    EXPECT(fixture.input.enqueue_action(3, 0));
    const auto confirmation = fixture.coordinator.service();
    EXPECT(confirmation.disposition ==
           BreadcrumbArchiveWorkflowDisposition::confirmation_presented);
    EXPECT(confirmation.revision == 4);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::archive_stop_confirmation);
    EXPECT(fixture.input.enqueue_action(4, 0));
    const auto stopped = fixture.coordinator.service();
    EXPECT(stopped.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_applied);
    EXPECT(stopped.revision == 5);
    EXPECT(fixture.clock_source.read_count() == reads);
    EXPECT(fixture.display.latest_frame().notice == UiNotice::archive_stopped);
}

void test_stale_control_input_never_reaches_consent() {
    Fixture fixture{};
    EXPECT(fixture.coordinator.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(99, 0));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::input_rejected);
    EXPECT(result.action_error == ActionResolutionError::stale_frame);
    EXPECT(fixture.clock_source.read_count() == 0);
    EXPECT(fixture.coordinator.consent_status().action_calls == 0);
}

void test_snapshot_contention_defers_before_input_poll() {
    Fixture fixture{};
    EXPECT(fixture.coordinator.service().frame_presented);
    fixture.lock.next_acquire =
        BreadcrumbArchiveSnapshotLockState::not_ready;
    EXPECT(fixture.input.enqueue_action(1, 0));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::snapshot_deferred);
    EXPECT(fixture.input.read_count() == 0);
    EXPECT(fixture.input.queued_count() == 1);
}

void test_unknown_runtime_state_never_offers_start() {
    Fixture fixture{};
    fixture.lock.next_acquire = BreadcrumbArchiveSnapshotLockState::failed;
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(result.presentation_error ==
           BreadcrumbArchivePresentationError::snapshot_failed);
    EXPECT(fixture.display.latest_frame().notice ==
           UiNotice::archive_upload_failed);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_stop);
    EXPECT(fixture.display.latest_frame().actions[0].action !=
           UiAction::request_archive_start);
}

void test_live_change_refreshes_before_queued_control_input() {
    Fixture fixture{};
    EXPECT(fixture.coordinator.service().frame_presented);
    EXPECT(fixture.runtime.start_capture(9, 10).completed());
    EXPECT(fixture.input.enqueue_action(1, 0));

    const auto refreshed = fixture.coordinator.service();
    EXPECT(refreshed.disposition ==
           BreadcrumbArchiveWorkflowDisposition::refreshed);
    EXPECT(refreshed.revision == 2);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_stop);
    EXPECT(fixture.input.read_count() == 0);
    EXPECT(fixture.input.queued_count() == 1);

    const auto stale = fixture.coordinator.service();
    EXPECT(stale.disposition ==
           BreadcrumbArchiveWorkflowDisposition::input_rejected);
    EXPECT(stale.action_error == ActionResolutionError::stale_frame);
    EXPECT(fixture.coordinator.consent_status().action_calls == 0);
}

void test_start_contention_preserves_confirmation_and_session_id() {
    Fixture fixture{50};
    present_start_confirmation(fixture);
    fixture.lock.next_acquire =
        BreadcrumbArchiveSnapshotLockState::not_ready;
    EXPECT(fixture.clock_source.enqueue_time(100));
    EXPECT(fixture.input.enqueue_action(2, 0, InputGesture::hold));
    const auto deferred = fixture.coordinator.service();
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_deferred);
    EXPECT(fixture.coordinator.status().mode ==
           BreadcrumbArchiveWorkflowMode::start_confirmation);
    EXPECT(fixture.coordinator.consent_status().next_session_id == 50);

    EXPECT(fixture.clock_source.enqueue_time(101));
    EXPECT(fixture.input.enqueue_action(2, 0, InputGesture::hold));
    const auto retried = fixture.coordinator.service();
    EXPECT(retried.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_applied);
    EXPECT(fixture.coordinator.consent_status().next_session_id == 51);
}

void test_failed_post_start_refresh_stops_and_latches() {
    Fixture fixture{};
    present_start_confirmation(fixture);
    EXPECT(fixture.display.enqueue_result(DisplayWriteError::sink_failed));
    EXPECT(fixture.clock_source.enqueue_time(200));
    EXPECT(fixture.input.enqueue_action(2, 0, InputGesture::hold));
    const auto result = fixture.coordinator.service();
    EXPECT(result.disposition ==
           BreadcrumbArchiveWorkflowDisposition::failed);
    EXPECT(result.error ==
           BreadcrumbArchiveWorkflowError::post_action_refresh_failed);
    EXPECT(result.containment_attempted);
    EXPECT(result.containment.completed());
    EXPECT(fixture.coordinator.status().faulted);

    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.runtime.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(!snapshot.session.active);
}

void test_controls_cancel_exits_without_polling_again() {
    Fixture fixture{};
    EXPECT(fixture.coordinator.service().frame_presented);
    EXPECT(fixture.input.enqueue_action(1, 1));
    const auto exited = fixture.coordinator.service();
    EXPECT(exited.disposition ==
           BreadcrumbArchiveWorkflowDisposition::exit_requested);
    const auto reads = fixture.input.read_count();
    EXPECT(fixture.coordinator.service().disposition ==
           BreadcrumbArchiveWorkflowDisposition::exit_requested);
    EXPECT(fixture.input.read_count() == reads);
}

void test_local_parent_reentry_preserves_session_allocator() {
    Fixture fixture{};
    start_archive(fixture);

    EXPECT(fixture.input.enqueue_action(3, 0));
    EXPECT(fixture.coordinator.service().revision == 4);
    EXPECT(fixture.input.enqueue_action(4, 0));
    EXPECT(fixture.coordinator.service().revision == 5);
    EXPECT(fixture.coordinator.consent_status().next_session_id == 2);

    EXPECT(fixture.input.enqueue_action(5, 1));
    EXPECT(fixture.coordinator.service().disposition ==
           BreadcrumbArchiveWorkflowDisposition::exit_requested);

    UiFrame parent{};
    parent.revision = 6;
    parent.screen = UiScreen::home;
    parent.action_count = 1;
    parent.actions[0] = {UiAction::open_archive_controls, true};
    EXPECT(fixture.local.present(parent).ok());
    EXPECT(fixture.input.enqueue_action(6, 0));
    const auto entry = fixture.coordinator.enter(
        fixture.local.poll_action());
    EXPECT(entry.disposition ==
           BreadcrumbArchiveWorkflowDisposition::entered);
    EXPECT(entry.revision == 6);

    const auto reopened = fixture.coordinator.service();
    EXPECT(reopened.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(reopened.revision == 7);
    EXPECT(fixture.coordinator.consent_status().next_session_id == 2);
    EXPECT(fixture.display.latest_frame().actions[0].action ==
           UiAction::request_archive_start);
}

void test_invalid_or_exhausted_revision_fails_before_action() {
    const auto maximum_session =
        std::numeric_limits<std::uint64_t>::max();
    Fixture invalid_lease{2, 1};
    EXPECT(invalid_lease.coordinator.service().error ==
           BreadcrumbArchiveWorkflowError::invalid_initial_session_id);
    EXPECT(invalid_lease.lock.acquire_calls == 0);

    Fixture zero{1, maximum_session, 0};
    EXPECT(zero.coordinator.service().error ==
           BreadcrumbArchiveWorkflowError::invalid_initial_revision);
    EXPECT(zero.lock.acquire_calls == 0);

    Fixture maximum{
        1,
        maximum_session,
        std::numeric_limits<std::uint32_t>::max()};
    EXPECT(maximum.coordinator.service().error ==
           BreadcrumbArchiveWorkflowError::invalid_initial_revision);
    EXPECT(maximum.lock.acquire_calls == 0);

    const auto near_maximum =
        std::numeric_limits<std::uint32_t>::max() - 1;
    Fixture exhausted{1, maximum_session, near_maximum};
    EXPECT(exhausted.clock_source.enqueue_time(10));
    EXPECT(exhausted.runtime.start_capture(1, 10).completed());
    EXPECT(exhausted.coordinator.service().revision == near_maximum);
    const auto result = exhausted.coordinator.service();
    EXPECT(result.error ==
           BreadcrumbArchiveWorkflowError::revision_exhausted);
    EXPECT(result.containment_attempted);
    EXPECT(result.containment.completed());
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(exhausted.runtime.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(!snapshot.session.active);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveWorkflowResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveWorkflowStatus>);
static_assert(sizeof(BreadcrumbArchiveWorkflowResult) <= 160);
static_assert(sizeof(BreadcrumbArchiveWorkflowStatus) <= 64);

}  // namespace

int main() {
    test_initial_controls_are_snapshot_bound_and_local();
    test_start_entry_requires_exact_revision_then_hold();
    test_cancel_returns_to_controls_without_runtime_mutation();
    test_confirmed_start_refreshes_active_controls();
    test_stop_flow_is_immediate_and_clock_independent();
    test_stale_control_input_never_reaches_consent();
    test_snapshot_contention_defers_before_input_poll();
    test_unknown_runtime_state_never_offers_start();
    test_live_change_refreshes_before_queued_control_input();
    test_start_contention_preserves_confirmation_and_session_id();
    test_failed_post_start_refresh_stops_and_latches();
    test_controls_cancel_exits_without_polling_again();
    test_local_parent_reentry_preserves_session_allocator();
    test_invalid_or_exhausted_revision_fails_before_action();

    if (failures != 0) {
        std::cerr << failures
                  << " archive workflow coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 14 archive workflow coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
