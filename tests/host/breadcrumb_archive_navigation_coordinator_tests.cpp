#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "memory_persistent_storage.hpp"
#include "opentrail/breadcrumb_archive_navigation_coordinator.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location::test_support;
using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;
using namespace opentrail::ui;
using namespace opentrail::ui::test_support;

constexpr std::uint64_t kInitialSession = 500;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeNavigationLock final : public BreadcrumbArchiveSnapshotLock {
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

UiFrame parent_frame(std::uint32_t revision) {
    UiFrame frame{};
    frame.revision = revision;
    frame.screen = UiScreen::home;
    frame.action_count = 1;
    frame.actions[0] = {UiAction::open_archive_controls, true};
    return frame;
}

struct Harness {
    MemoryPersistentStorage storage{};
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeNavigationLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner runtime{
        clock, remote, {1'000, 100}, {10, 80}, lock};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveNavigationCoordinator navigation;

    explicit Harness(
        BreadcrumbArchiveSessionLeaseRequest request = {
            kInitialSession, 4})
        : navigation(storage, runtime, clock, local, request) {}

    ResolvedAction resolved_open(std::uint32_t revision) {
        EXPECT(local.present(parent_frame(revision)).ok());
        EXPECT(input.enqueue_action(revision, 0));
        return local.poll_action();
    }
};

void test_idle_parent_mode_has_no_optional_path_side_effects() {
    Harness harness{};
    const auto result = harness.navigation.service();
    EXPECT(result.disposition == BreadcrumbArchiveNavigationDisposition::idle);
    EXPECT(!result.workflow_called && !result.lease_initialized);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::parent);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.present_count() == 0);
}

void test_invalid_or_stale_open_never_allocates_a_lease() {
    Harness harness{};
    EXPECT(harness.local.present(parent_frame(10)).ok());
    ResolvedAction stale{};
    stale.error = ActionResolutionError::none;
    stale.action = UiAction::open_archive_controls;
    stale.frame_revision = 9;
    const auto result = harness.navigation.open(stale);
    EXPECT(result.disposition ==
           BreadcrumbArchiveNavigationDisposition::input_rejected);
    EXPECT(result.error ==
           BreadcrumbArchiveNavigationError::invalid_open_action);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.navigation.status().input_rejections == 1);
}

void test_first_local_open_commits_before_presenting_controls() {
    Harness harness{};
    const auto opened = harness.navigation.open(harness.resolved_open(10));
    EXPECT(opened.disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    EXPECT(opened.lease_initialized && !opened.workflow_called);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::workflow);
    EXPECT(harness.navigation.bootstrap_status().first_session_id ==
           kInitialSession);
    EXPECT(harness.navigation.bootstrap_status().final_session_id ==
           kInitialSession + 3);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).writes == 2);
    EXPECT(harness.display.latest_frame().revision == 10);
    EXPECT(harness.lock.acquire_calls == 0);

    const auto controls = harness.navigation.service();
    EXPECT(controls.disposition ==
           BreadcrumbArchiveNavigationDisposition::forwarded);
    EXPECT(controls.workflow_called);
    EXPECT(controls.bootstrap.workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(harness.display.latest_frame().screen ==
           UiScreen::archive_controls);
    EXPECT(harness.display.latest_frame().revision == 11);
    EXPECT(harness.lock.acquire_calls == 1);
}

void test_open_while_workflow_active_is_rejected_without_new_lease() {
    Harness harness{};
    const auto action = harness.resolved_open(1);
    EXPECT(harness.navigation.open(action).disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    const auto before = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    const auto repeated = harness.navigation.open(action);
    const auto after = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(repeated.error ==
           BreadcrumbArchiveNavigationError::invalid_open_action);
    EXPECT(after.reads == before.reads && after.writes == before.writes);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::workflow);
}

void test_cancel_handoff_and_exact_parent_reentry_reuse_boot_lease() {
    Harness harness{};
    EXPECT(harness.navigation.open(harness.resolved_open(1)).disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    EXPECT(harness.navigation.service().bootstrap.workflow.revision == 2);
    const auto lease_before = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);

    EXPECT(harness.input.enqueue_action(2, 1));
    const auto exited = harness.navigation.service();
    EXPECT(exited.disposition ==
           BreadcrumbArchiveNavigationDisposition::exit_requested);
    EXPECT(exited.minimum_parent_revision == 3);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::parent);

    const auto reopened = harness.navigation.open(
        harness.resolved_open(exited.minimum_parent_revision));
    EXPECT(reopened.disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    EXPECT(reopened.workflow_called && !reopened.lease_initialized);
    const auto lease_after = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(lease_after.reads == lease_before.reads &&
           lease_after.writes == lease_before.writes);
    const auto controls = harness.navigation.service();
    EXPECT(controls.bootstrap.workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(harness.display.latest_frame().revision == 4);
}

void test_parent_may_reenter_at_a_later_exact_active_revision() {
    Harness harness{};
    EXPECT(harness.navigation.open(harness.resolved_open(3)).disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    EXPECT(harness.navigation.service().bootstrap.workflow.revision == 4);
    EXPECT(harness.input.enqueue_action(4, 1));
    EXPECT(harness.navigation.service().minimum_parent_revision == 5);

    const auto later = harness.navigation.open(harness.resolved_open(20));
    EXPECT(later.disposition ==
           BreadcrumbArchiveNavigationDisposition::opened);
    EXPECT(harness.navigation.service().bootstrap.workflow.revision == 21);
}

void test_lease_failure_latches_before_archive_runtime_or_controls() {
    Harness harness{};
    harness.storage.arm_power_loss_after(0);
    const auto opened = harness.navigation.open(harness.resolved_open(1));
    EXPECT(opened.disposition ==
           BreadcrumbArchiveNavigationDisposition::failed);
    EXPECT(opened.error ==
           BreadcrumbArchiveNavigationError::bootstrap_failed);
    EXPECT(opened.bootstrap.lease_error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::faulted);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.latest_frame().screen == UiScreen::home);

    harness.storage.clear_fault();
    const auto before = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(harness.navigation.service().error ==
           BreadcrumbArchiveNavigationError::bootstrap_failed);
    const auto after = harness.storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(after.reads == before.reads && after.erases == before.erases);
}

void test_exhausted_parent_revision_is_rejected_before_storage() {
    Harness harness{};
    const auto unsafe_revision =
        std::numeric_limits<std::uint32_t>::max() - 1;
    const auto result = harness.navigation.open(
        harness.resolved_open(unsafe_revision));
    EXPECT(result.disposition ==
           BreadcrumbArchiveNavigationDisposition::input_rejected);
    EXPECT(result.error ==
           BreadcrumbArchiveNavigationError::invalid_parent_revision);
    EXPECT(harness.storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.navigation.status().mode ==
           BreadcrumbArchiveNavigationMode::parent);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveNavigationResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveNavigationStatus>);
static_assert(!std::is_copy_constructible_v<
              BreadcrumbArchiveNavigationCoordinator>);
static_assert(!std::is_move_constructible_v<
              BreadcrumbArchiveNavigationCoordinator>);
static_assert(sizeof(BreadcrumbArchiveNavigationStatus) <= 64);

}  // namespace

int main() {
    test_idle_parent_mode_has_no_optional_path_side_effects();
    test_invalid_or_stale_open_never_allocates_a_lease();
    test_first_local_open_commits_before_presenting_controls();
    test_open_while_workflow_active_is_rejected_without_new_lease();
    test_cancel_handoff_and_exact_parent_reentry_reuse_boot_lease();
    test_parent_may_reenter_at_a_later_exact_active_revision();
    test_lease_failure_latches_before_archive_runtime_or_controls();
    test_exhausted_parent_revision_is_rejected_before_storage();

    if (failures != 0) {
        std::cerr << failures
                  << " archive navigation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 archive navigation scenario groups\n";
    return EXIT_SUCCESS;
}
