#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "memory_persistent_storage.hpp"
#include "opentrail/breadcrumb_archive_parent_page_coordinator.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location::test_support;
using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;
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

class FakeParentPageLock final : public BreadcrumbArchiveSnapshotLock {
public:
    BreadcrumbArchiveSnapshotLockState acquire() override {
        ++acquire_calls;
        return BreadcrumbArchiveSnapshotLockState::acquired;
    }
    BreadcrumbArchiveSnapshotUnlockState release() override {
        ++release_calls;
        return BreadcrumbArchiveSnapshotUnlockState::released;
    }
    std::uint32_t acquire_calls{0};
    std::uint32_t release_calls{0};
};

DisplayCapabilities capabilities() {
    return {128, 64, 1, 2, false, true, true};
}

UiStatusSummary summary() {
    UiStatusSummary result{};
    result.radio = UiIndicatorState::normal;
    result.position = UiIndicatorState::normal;
    result.power = UiIndicatorState::warning;
    result.peer_count_valid = true;
    result.peer_count = 3;
    return result;
}

struct Harness {
    MemoryPersistentStorage storage{};
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeParentPageLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner runtime{
        clock, remote, {1'000, 100}, {10, 80}, lock};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveParentPageCoordinator page;

    explicit Harness(
        BreadcrumbArchiveSessionLeaseRequest request = {700, 4})
        : page(storage, runtime, clock, local, request) {}
};

void activate(Harness& harness, std::uint32_t revision = 1) {
    const auto result = harness.page.activate(revision, summary());
    EXPECT(result.disposition ==
           BreadcrumbArchiveParentPageDisposition::presented);
    EXPECT(result.frame_presented && result.revision == revision);
}

void open_controls(Harness& harness, std::uint32_t parent_revision = 1) {
    EXPECT(harness.input.enqueue_action(parent_revision, 0));
    const auto opened = harness.page.service();
    EXPECT(opened.disposition ==
           BreadcrumbArchiveParentPageDisposition::opened);
    const auto controls = harness.page.service();
    EXPECT(controls.disposition ==
           BreadcrumbArchiveParentPageDisposition::forwarded);
    EXPECT(harness.display.latest_frame().screen ==
           UiScreen::archive_controls);
}

void test_activation_presents_only_the_optional_parent_page() {
    Harness harness{};
    activate(harness, 5);
    const auto frame = harness.display.latest_frame();
    EXPECT(frame.screen == UiScreen::status);
    EXPECT(frame.attention == UiAttention::information);
    EXPECT(frame.status.peer_count_valid && frame.status.peer_count == 3);
    EXPECT(frame.action_count == 2);
    EXPECT(frame.actions[0].action == UiAction::open_archive_controls);
    EXPECT(frame.actions[1].action == UiAction::cancel);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.lock.acquire_calls == 0);
}

void test_parent_idle_polls_once_without_optional_side_effects() {
    Harness harness{};
    activate(harness);
    const auto result = harness.page.service();
    EXPECT(result.disposition == BreadcrumbArchiveParentPageDisposition::idle);
    EXPECT(result.input_polled);
    EXPECT(harness.input.read_count() == 1);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.lock.acquire_calls == 0);
}

void test_open_commits_lease_before_nested_controls() {
    Harness harness{};
    activate(harness);
    EXPECT(harness.input.enqueue_action(1, 0));
    const auto opened = harness.page.service();
    EXPECT(opened.disposition ==
           BreadcrumbArchiveParentPageDisposition::opened);
    EXPECT(opened.navigation.lease_initialized);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).writes == 2);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.latest_frame().screen == UiScreen::status);

    const auto controls = harness.page.service();
    EXPECT(controls.workflow_called);
    EXPECT(harness.display.latest_frame().screen ==
           UiScreen::archive_controls);
    EXPECT(harness.display.latest_frame().revision == 2);
    EXPECT(harness.lock.acquire_calls == 1);
}

void test_nested_cancel_restores_parent_without_new_lease() {
    Harness harness{};
    activate(harness);
    open_controls(harness);
    const auto before = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(harness.input.enqueue_action(2, 1));
    const auto restored = harness.page.service();
    EXPECT(restored.disposition ==
           BreadcrumbArchiveParentPageDisposition::restored);
    EXPECT(restored.frame_presented && restored.revision == 3);
    EXPECT(harness.display.latest_frame().screen == UiScreen::status);
    EXPECT(harness.page.status().mode ==
           BreadcrumbArchiveParentPageMode::parent);
    const auto after = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(after.reads == before.reads && after.writes == before.writes);
}

void test_parent_cancel_exits_for_the_broader_application_shell() {
    Harness harness{};
    activate(harness, 10);
    EXPECT(harness.input.enqueue_action(10, 1));
    const auto exited = harness.page.service();
    EXPECT(exited.disposition ==
           BreadcrumbArchiveParentPageDisposition::exit_requested);
    EXPECT(exited.revision == 10);
    EXPECT(harness.page.status().mode ==
           BreadcrumbArchiveParentPageMode::inactive);
    const auto reads = harness.input.read_count();
    EXPECT(harness.page.service().disposition ==
           BreadcrumbArchiveParentPageDisposition::idle);
    EXPECT(harness.input.read_count() == reads);

    EXPECT(harness.page.activate(11, summary()).disposition ==
           BreadcrumbArchiveParentPageDisposition::presented);
}

void test_deferred_parent_presentation_is_explicitly_retryable() {
    Harness harness{};
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    const auto deferred = harness.page.activate(1, summary());
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveParentPageDisposition::display_deferred);
    EXPECT(!deferred.frame_presented);
    EXPECT(harness.page.status().mode ==
           BreadcrumbArchiveParentPageMode::inactive);
    EXPECT(harness.page.activate(1, summary()).disposition ==
           BreadcrumbArchiveParentPageDisposition::presented);
}

void test_deferred_restore_retries_without_reentering_navigation() {
    Harness harness{};
    activate(harness);
    open_controls(harness);
    EXPECT(harness.display.enqueue_result(DisplayWriteError::not_ready));
    EXPECT(harness.input.enqueue_action(2, 1));
    const auto deferred = harness.page.service();
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveParentPageDisposition::display_deferred);
    EXPECT(harness.page.status().mode ==
           BreadcrumbArchiveParentPageMode::restoring_parent);
    const auto lease_before = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    const auto restored = harness.page.service();
    EXPECT(restored.disposition ==
           BreadcrumbArchiveParentPageDisposition::restored);
    EXPECT(harness.display.latest_frame().revision == 3);
    const auto lease_after = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(lease_after.reads == lease_before.reads &&
           lease_after.writes == lease_before.writes);
}

void test_lease_failure_latches_after_parent_but_before_runtime() {
    Harness harness{};
    activate(harness);
    harness.storage.arm_power_loss_after(0);
    EXPECT(harness.input.enqueue_action(1, 0));
    const auto failed = harness.page.service();
    EXPECT(failed.disposition ==
           BreadcrumbArchiveParentPageDisposition::failed);
    EXPECT(failed.error ==
           BreadcrumbArchiveParentPageError::navigation_failed);
    EXPECT(harness.page.status().mode ==
           BreadcrumbArchiveParentPageMode::faulted);
    EXPECT(harness.display.latest_frame().screen == UiScreen::status);
    EXPECT(harness.lock.acquire_calls == 0);
}

void test_invalid_summary_and_revision_fail_without_archive_mutation() {
    Harness harness{};
    auto invalid = summary();
    invalid.peer_count_valid = false;
    const auto bad_summary = harness.page.activate(1, invalid);
    EXPECT(bad_summary.error ==
           BreadcrumbArchiveParentPageError::display_failed);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);

    Harness revision{};
    EXPECT(revision.page.activate(0, summary()).error ==
           BreadcrumbArchiveParentPageError::invalid_activation);
    EXPECT(revision.display.present_count() == 0);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveParentPageResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveParentPageStatus>);
static_assert(!std::is_copy_constructible_v<
              BreadcrumbArchiveParentPageCoordinator>);
static_assert(!std::is_move_constructible_v<
              BreadcrumbArchiveParentPageCoordinator>);
static_assert(sizeof(BreadcrumbArchiveParentPageStatus) <= 64);

}  // namespace

int main() {
    test_activation_presents_only_the_optional_parent_page();
    test_parent_idle_polls_once_without_optional_side_effects();
    test_open_commits_lease_before_nested_controls();
    test_nested_cancel_restores_parent_without_new_lease();
    test_parent_cancel_exits_for_the_broader_application_shell();
    test_deferred_parent_presentation_is_explicitly_retryable();
    test_deferred_restore_retries_without_reentering_navigation();
    test_lease_failure_latches_after_parent_but_before_runtime();
    test_invalid_summary_and_revision_fail_without_archive_mutation();

    if (failures != 0) {
        std::cerr << failures
                  << " archive parent-page assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 archive parent-page scenario groups\n";
    return EXIT_SUCCESS;
}
