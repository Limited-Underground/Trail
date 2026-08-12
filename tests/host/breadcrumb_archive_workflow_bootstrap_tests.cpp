#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "memory_persistent_storage.hpp"
#include "opentrail/breadcrumb_archive_workflow_bootstrap.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location::test_support;
using namespace opentrail::persistence;
using namespace opentrail::persistence::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;
using namespace opentrail::ui;
using namespace opentrail::ui::test_support;

constexpr std::uint64_t kInitialSession = 100;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeBootstrapLock final : public BreadcrumbArchiveSnapshotLock {
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

struct Harness {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeBootstrapLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner runtime{
        clock, remote, {1'000, 100}, {10, 80}, lock};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveWorkflowBootstrap bootstrap;

    Harness(
        MemoryPersistentStorage& storage,
        BreadcrumbArchiveSessionLeaseRequest request = {
            kInitialSession, 4})
        : bootstrap(
              storage,
              runtime,
              clock,
              local,
              request) {}
};

void test_dormant_bootstrap_blocks_every_workflow_call() {
    MemoryPersistentStorage storage{};
    Harness harness{storage};
    const auto service = harness.bootstrap.service();
    EXPECT(service.disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::rejected);
    EXPECT(service.error ==
           BreadcrumbArchiveWorkflowBootstrapError::not_initialized);
    EXPECT(!service.workflow_called);

    const auto entry = harness.bootstrap.enter({});
    EXPECT(entry.error ==
           BreadcrumbArchiveWorkflowBootstrapError::not_initialized);
    EXPECT(harness.bootstrap.status().state ==
           BreadcrumbArchiveWorkflowBootstrapState::dormant);
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.present_count() == 0);
}

void test_invalid_configuration_fails_before_storage_or_runtime() {
    MemoryPersistentStorage storage{};
    Harness invalid_request{storage, {0, 4}};
    const auto request_result = invalid_request.bootstrap.initialize();
    EXPECT(request_result.error ==
           BreadcrumbArchiveWorkflowBootstrapError::invalid_configuration);
    EXPECT(!request_result.lease_attempted);

    Harness invalid_revision{storage, {kInitialSession, 4}};
    const auto revision_result = invalid_revision.bootstrap.initialize(
        std::numeric_limits<std::uint32_t>::max());
    EXPECT(revision_result.error ==
           BreadcrumbArchiveWorkflowBootstrapError::invalid_configuration);
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).reads == 0);
    EXPECT(invalid_request.lock.acquire_calls == 0);
    EXPECT(invalid_revision.lock.acquire_calls == 0);
}

void test_committed_lease_precedes_workflow_construction_and_service() {
    MemoryPersistentStorage storage{};
    Harness harness{storage, {kInitialSession, 3}};
    const auto initialized = harness.bootstrap.initialize();
    EXPECT(initialized.disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    EXPECT(initialized.lease_attempted && !initialized.workflow_called);
    const auto ready = harness.bootstrap.status();
    EXPECT(ready.state == BreadcrumbArchiveWorkflowBootstrapState::ready);
    EXPECT(ready.lease_generation == 1);
    EXPECT(ready.first_session_id == kInitialSession);
    EXPECT(ready.final_session_id == kInitialSession + 2);
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).writes == 2);
    EXPECT(storage.counters(
               StorageDomain::breadcrumb_archive_state).syncs == 2);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.present_count() == 0);

    const auto presented = harness.bootstrap.service();
    EXPECT(presented.disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::forwarded);
    EXPECT(presented.workflow_called);
    EXPECT(presented.workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(harness.lock.acquire_calls == 1 &&
           harness.lock.release_calls == 1);
    EXPECT(harness.display.latest_frame().screen ==
           UiScreen::archive_controls);
}

void test_repeated_initialize_reuses_one_boot_lease() {
    MemoryPersistentStorage storage{};
    Harness harness{storage};
    EXPECT(harness.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    const auto before = storage.counters(
        StorageDomain::breadcrumb_archive_state);
    const auto repeated = harness.bootstrap.initialize();
    const auto after = storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(repeated.disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::already_ready);
    EXPECT(!repeated.lease_attempted);
    EXPECT(after.reads == before.reads && after.erases == before.erases &&
           after.writes == before.writes && after.syncs == before.syncs);
    EXPECT(harness.bootstrap.status().initialize_calls == 2);
    EXPECT(harness.bootstrap.status().lease_attempts == 1);
}

void test_restart_bootstrap_reserves_the_next_nonoverlapping_range() {
    MemoryPersistentStorage storage{};
    Harness first{storage, {kInitialSession, 2}};
    EXPECT(first.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    const auto first_status = first.bootstrap.status();

    Harness second{
        storage,
        {std::numeric_limits<std::uint64_t>::max(), 3}};
    EXPECT(second.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    const auto second_status = second.bootstrap.status();
    EXPECT(second_status.lease_generation == 2);
    EXPECT(second_status.first_session_id ==
           first_status.final_session_id + 1);
    EXPECT(second_status.final_session_id ==
           second_status.first_session_id + 2);
}

void test_failed_or_uncertain_lease_latches_without_workflow_calls() {
    MemoryPersistentStorage storage{};
    Harness harness{storage};
    storage.arm_power_loss_after(0);
    const auto failed = harness.bootstrap.initialize();
    EXPECT(failed.disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::failed);
    EXPECT(failed.error ==
           BreadcrumbArchiveWorkflowBootstrapError::lease_allocation_failed);
    EXPECT(failed.lease_error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(failed.lease_attempted);
    const auto before = storage.counters(
        StorageDomain::breadcrumb_archive_state);

    storage.clear_fault();
    const auto retried = harness.bootstrap.initialize();
    const auto after = storage.counters(
        StorageDomain::breadcrumb_archive_state);
    EXPECT(retried.error ==
           BreadcrumbArchiveWorkflowBootstrapError::lease_allocation_failed);
    EXPECT(!retried.lease_attempted);
    EXPECT(after.reads == before.reads && after.erases == before.erases);
    EXPECT(harness.bootstrap.service().error ==
           BreadcrumbArchiveWorkflowBootstrapError::lease_allocation_failed);
    EXPECT(harness.lock.acquire_calls == 0);
    EXPECT(harness.display.present_count() == 0);
}

void test_applied_but_uncertain_range_is_skipped_after_restart() {
    MemoryPersistentStorage storage{};
    Harness first{storage, {kInitialSession, 2}};
    EXPECT(first.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    const auto first_final = first.bootstrap.status().final_session_id;

    Harness uncertain{storage, {kInitialSession, 2}};
    storage.arm_power_loss_after(4);
    const auto interrupted = uncertain.bootstrap.initialize();
    EXPECT(interrupted.error ==
           BreadcrumbArchiveWorkflowBootstrapError::lease_allocation_failed);
    storage.clear_fault();

    Harness recovered{storage, {kInitialSession, 2}};
    EXPECT(recovered.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    EXPECT(recovered.bootstrap.status().lease_generation == 3);
    EXPECT(recovered.bootstrap.status().first_session_id == first_final + 3);
}

void test_single_id_lease_exhausts_without_crossing_range() {
    MemoryPersistentStorage storage{};
    Harness harness{storage, {kInitialSession, 1}};
    EXPECT(harness.bootstrap.initialize().disposition ==
           BreadcrumbArchiveWorkflowBootstrapDisposition::initialized);
    EXPECT(harness.bootstrap.service().workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::presented);
    EXPECT(harness.input.enqueue_action(1, 0));
    EXPECT(harness.bootstrap.service().workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::confirmation_presented);
    EXPECT(harness.clock_source.enqueue_time(10));
    EXPECT(harness.input.enqueue_action(2, 0, InputGesture::hold));
    const auto started = harness.bootstrap.service();
    EXPECT(started.workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_applied);

    EXPECT(harness.input.enqueue_action(3, 0));
    EXPECT(harness.bootstrap.service().workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::confirmation_presented);
    EXPECT(harness.input.enqueue_action(4, 0));
    EXPECT(harness.bootstrap.service().workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::action_applied);

    EXPECT(harness.input.enqueue_action(5, 0));
    EXPECT(harness.bootstrap.service().workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::confirmation_presented);
    const auto clock_reads = harness.clock_source.read_count();
    EXPECT(harness.input.enqueue_action(6, 0, InputGesture::hold));
    const auto exhausted = harness.bootstrap.service();
    EXPECT(exhausted.workflow.disposition ==
           BreadcrumbArchiveWorkflowDisposition::failed);
    EXPECT(exhausted.workflow.consent_error ==
           BreadcrumbArchiveConsentError::session_id_exhausted);
    EXPECT(harness.clock_source.read_count() == clock_reads);
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveWorkflowBootstrapResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveWorkflowBootstrapStatus>);
static_assert(!std::is_copy_constructible_v<
              BreadcrumbArchiveWorkflowBootstrap>);
static_assert(!std::is_move_constructible_v<
              BreadcrumbArchiveWorkflowBootstrap>);
static_assert(sizeof(BreadcrumbArchiveWorkflowBootstrapStatus) <= 64);

}  // namespace

int main() {
    test_dormant_bootstrap_blocks_every_workflow_call();
    test_invalid_configuration_fails_before_storage_or_runtime();
    test_committed_lease_precedes_workflow_construction_and_service();
    test_repeated_initialize_reuses_one_boot_lease();
    test_restart_bootstrap_reserves_the_next_nonoverlapping_range();
    test_failed_or_uncertain_lease_latches_without_workflow_calls();
    test_applied_but_uncertain_range_is_skipped_after_restart();
    test_single_id_lease_exhausts_without_crossing_range();

    if (failures != 0) {
        std::cerr << failures
                  << " archive workflow bootstrap assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 archive workflow bootstrap scenario groups\n";
    return EXIT_SUCCESS;
}
