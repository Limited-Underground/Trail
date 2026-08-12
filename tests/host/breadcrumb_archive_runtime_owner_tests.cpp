#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "opentrail/breadcrumb_archive_runtime_owner.hpp"

namespace {

using namespace opentrail::integration;
using namespace opentrail::location;
using namespace opentrail::location::test_support;
using namespace opentrail::time;
using namespace opentrail::time::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeRuntimeLock final : public BreadcrumbArchiveSnapshotLock {
public:
    static constexpr std::size_t kCapacity = 16;

    bool push_acquire(BreadcrumbArchiveSnapshotLockState state) {
        if (acquire_size_ == acquire_.size()) {
            return false;
        }
        acquire_[(acquire_head_ + acquire_size_) % acquire_.size()] = state;
        ++acquire_size_;
        return true;
    }

    bool push_release(BreadcrumbArchiveSnapshotUnlockState state) {
        if (release_size_ == release_.size()) {
            return false;
        }
        release_[(release_head_ + release_size_) % release_.size()] = state;
        ++release_size_;
        return true;
    }

    BreadcrumbArchiveSnapshotLockState acquire() override {
        ++acquires_;
        if (acquire_size_ == 0) {
            return BreadcrumbArchiveSnapshotLockState::acquired;
        }
        const auto value = acquire_[acquire_head_];
        acquire_head_ = (acquire_head_ + 1) % acquire_.size();
        --acquire_size_;
        return value;
    }

    BreadcrumbArchiveSnapshotUnlockState release() override {
        ++releases_;
        if (release_size_ == 0) {
            return BreadcrumbArchiveSnapshotUnlockState::released;
        }
        const auto value = release_[release_head_];
        release_head_ = (release_head_ + 1) % release_.size();
        --release_size_;
        return value;
    }

    std::uint32_t acquires() const { return acquires_; }
    std::uint32_t releases() const { return releases_; }

private:
    std::array<BreadcrumbArchiveSnapshotLockState, kCapacity> acquire_{};
    std::array<BreadcrumbArchiveSnapshotUnlockState, kCapacity> release_{};
    std::size_t acquire_head_{0};
    std::size_t acquire_size_{0};
    std::size_t release_head_{0};
    std::size_t release_size_{0};
    std::uint32_t acquires_{0};
    std::uint32_t releases_{0};
};

LocationSnapshot current() {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = 449775000;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.age_ms = 25;
    return snapshot;
}

struct Fixture {
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    FakeBreadcrumbArchiveRemote remote{};
    FakeRuntimeLock lock{};
    SerializedBreadcrumbArchiveRuntimeOwner owner{
        clock, remote, {1'000, 100}, {10, 80}, lock};
};

void test_start_and_snapshot_share_one_lock_domain() {
    Fixture fixture{};
    const auto started = fixture.owner.start_capture(10, 0);
    EXPECT(started.completed());
    EXPECT(started.start.started());
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(snapshot.session.active);
    EXPECT(fixture.lock.acquires() == 2);
    EXPECT(fixture.lock.releases() == 2);
}

void test_contention_defers_without_attempting_mutation() {
    Fixture fixture{};
    EXPECT(fixture.lock.push_acquire(
        BreadcrumbArchiveSnapshotLockState::not_ready));
    const auto deferred = fixture.owner.start_capture(11, 0);
    EXPECT(deferred.disposition ==
           BreadcrumbArchiveRuntimeDisposition::deferred);
    EXPECT(deferred.error == BreadcrumbArchiveRuntimeError::lock_not_ready);
    EXPECT(!deferred.operation_attempted);
    EXPECT(fixture.lock.releases() == 0);
    EXPECT(fixture.owner.start_capture(11, 0).start.started());
}

void test_capture_position_enqueues_under_owner_boundary() {
    Fixture fixture{};
    EXPECT(fixture.owner.start_capture(12, 100).start.started());
    const auto captured = fixture.owner.capture_position(current(), 100);
    EXPECT(captured.completed());
    EXPECT(captured.capture.submitted());
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(snapshot.outbox.queued == 1);
    EXPECT(snapshot.session.records_submitted == 1);
}

void test_upload_service_commits_only_after_durable_ack() {
    Fixture fixture{};
    EXPECT(fixture.owner.start_capture(13, 200).start.started());
    EXPECT(fixture.owner.capture_position(current(), 200).capture.submitted());
    EXPECT(fixture.remote.push_result(
        BreadcrumbArchiveRemoteResult::durable_ack));
    EXPECT(fixture.clock_source.enqueue_time(200));
    const auto uploaded = fixture.owner.service_upload();
    EXPECT(uploaded.completed());
    EXPECT(uploaded.upload.upload.committed());
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(snapshot.outbox.queued == 0);
}

void test_stop_is_serialized_and_visible_in_next_snapshot() {
    Fixture fixture{};
    EXPECT(fixture.owner.start_capture(14, 0).start.started());
    const auto stopped = fixture.owner.stop_capture();
    EXPECT(stopped.completed());
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(!snapshot.session.active);
    EXPECT(snapshot.session.sessions_stopped == 1);
}

void test_component_rejection_is_completed_not_lock_failure() {
    Fixture fixture{};
    const auto rejected = fixture.owner.start_capture(0, 0);
    EXPECT(rejected.completed());
    EXPECT(rejected.start.error ==
           BreadcrumbArchiveSessionError::invalid_session);
    EXPECT(!fixture.owner.status().failed_latched);
    EXPECT(fixture.lock.acquires() == 1);
    EXPECT(fixture.lock.releases() == 1);
}

void test_writer_lock_failure_latches_and_blocks_snapshot() {
    Fixture fixture{};
    EXPECT(fixture.lock.push_acquire(
        BreadcrumbArchiveSnapshotLockState::failed));
    const auto failed = fixture.owner.start_capture(15, 0);
    EXPECT(failed.error == BreadcrumbArchiveRuntimeError::lock_failed);
    EXPECT(fixture.owner.status().failed_latched);
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(fixture.lock.acquires() == 1);
}

void test_writer_unlock_failure_marks_outcome_uncertain() {
    Fixture fixture{};
    EXPECT(fixture.lock.push_release(
        BreadcrumbArchiveSnapshotUnlockState::failed));
    const auto failed = fixture.owner.start_capture(16, 0);
    EXPECT(failed.error == BreadcrumbArchiveRuntimeError::unlock_failed);
    EXPECT(failed.operation_attempted);
    EXPECT(failed.outcome_uncertain);
    EXPECT(fixture.owner.status().failed_latched);
    const auto blocked = fixture.owner.stop_capture();
    EXPECT(blocked.error == BreadcrumbArchiveRuntimeError::latched_failure);
    EXPECT(!blocked.operation_attempted);
}

void test_snapshot_lock_failure_propagates_to_writer_gate() {
    Fixture fixture{};
    EXPECT(fixture.lock.push_acquire(
        BreadcrumbArchiveSnapshotLockState::failed));
    BreadcrumbArchiveRuntimeSnapshot snapshot{};
    EXPECT(fixture.owner.snapshot(snapshot) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(fixture.owner.status().latched_error ==
           BreadcrumbArchiveRuntimeError::snapshot_failed);
    EXPECT(fixture.owner.status().snapshot_adapter_error ==
           BreadcrumbArchiveSnapshotAdapterError::lock_failed);
    const auto blocked = fixture.owner.start_capture(17, 0);
    EXPECT(blocked.error == BreadcrumbArchiveRuntimeError::latched_failure);
    EXPECT(fixture.lock.acquires() == 1);
}

void test_unknown_writer_lock_states_fail_closed() {
    Fixture acquire_unknown{};
    EXPECT(acquire_unknown.lock.push_acquire(
        static_cast<BreadcrumbArchiveSnapshotLockState>(99)));
    EXPECT(acquire_unknown.owner.start_capture(18, 0).error ==
           BreadcrumbArchiveRuntimeError::invalid_lock_state);

    Fixture release_unknown{};
    EXPECT(release_unknown.lock.push_release(
        static_cast<BreadcrumbArchiveSnapshotUnlockState>(99)));
    const auto failed = release_unknown.owner.start_capture(19, 0);
    EXPECT(failed.error ==
           BreadcrumbArchiveRuntimeError::invalid_unlock_state);
    EXPECT(failed.outcome_uncertain);
}

static_assert(std::is_trivially_copyable_v<BreadcrumbArchiveRuntimeResult>);
static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveRuntimeOwnerStatus>);
static_assert(sizeof(BreadcrumbArchiveRuntimeResult) <= 104);
static_assert(sizeof(BreadcrumbArchiveRuntimeOwnerStatus) <= 40);

}  // namespace

int main() {
    test_start_and_snapshot_share_one_lock_domain();
    test_contention_defers_without_attempting_mutation();
    test_capture_position_enqueues_under_owner_boundary();
    test_upload_service_commits_only_after_durable_ack();
    test_stop_is_serialized_and_visible_in_next_snapshot();
    test_component_rejection_is_completed_not_lock_failure();
    test_writer_lock_failure_latches_and_blocks_snapshot();
    test_writer_unlock_failure_marks_outcome_uncertain();
    test_snapshot_lock_failure_propagates_to_writer_gate();
    test_unknown_writer_lock_states_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive runtime owner assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive runtime owner scenario groups\n";
    return EXIT_SUCCESS;
}
