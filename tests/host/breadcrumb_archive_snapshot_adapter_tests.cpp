#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "fake_breadcrumb_archive_remote.hpp"
#include "fake_local_interface.hpp"
#include "fake_monotonic_counter_source.hpp"
#include "opentrail/breadcrumb_archive_snapshot_adapter.hpp"
#include "opentrail/breadcrumb_archive_ui_coordinator.hpp"

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

class FakeSnapshotLock final : public BreadcrumbArchiveSnapshotLock {
public:
    static constexpr std::size_t kCapacity = 16;

    bool enqueue_acquire(BreadcrumbArchiveSnapshotLockState state) {
        if (acquire_size_ == acquire_results_.size()) {
            return false;
        }
        const auto tail = (acquire_head_ + acquire_size_) %
                          acquire_results_.size();
        acquire_results_[tail] = state;
        ++acquire_size_;
        return true;
    }

    bool enqueue_release(BreadcrumbArchiveSnapshotUnlockState state) {
        if (release_size_ == release_results_.size()) {
            return false;
        }
        const auto tail = (release_head_ + release_size_) %
                          release_results_.size();
        release_results_[tail] = state;
        ++release_size_;
        return true;
    }

    BreadcrumbArchiveSnapshotLockState acquire() override {
        ++acquire_calls_;
        auto result = BreadcrumbArchiveSnapshotLockState::acquired;
        if (acquire_size_ != 0) {
            result = acquire_results_[acquire_head_];
            acquire_head_ = (acquire_head_ + 1) % acquire_results_.size();
            --acquire_size_;
        }
        held_ = result == BreadcrumbArchiveSnapshotLockState::acquired;
        return result;
    }

    BreadcrumbArchiveSnapshotUnlockState release() override {
        ++release_calls_;
        auto result = BreadcrumbArchiveSnapshotUnlockState::released;
        if (release_size_ != 0) {
            result = release_results_[release_head_];
            release_head_ = (release_head_ + 1) % release_results_.size();
            --release_size_;
        }
        if (result == BreadcrumbArchiveSnapshotUnlockState::released) {
            held_ = false;
        }
        return result;
    }

    bool held() const { return held_; }
    std::uint32_t acquire_calls() const { return acquire_calls_; }
    std::uint32_t release_calls() const { return release_calls_; }

private:
    std::array<BreadcrumbArchiveSnapshotLockState, kCapacity>
        acquire_results_{};
    std::array<BreadcrumbArchiveSnapshotUnlockState, kCapacity>
        release_results_{};
    std::size_t acquire_head_{0};
    std::size_t acquire_size_{0};
    std::size_t release_head_{0};
    std::size_t release_size_{0};
    bool held_{false};
    std::uint32_t acquire_calls_{0};
    std::uint32_t release_calls_{0};
};

DisplayCapabilities capabilities() {
    return {128, 64, 1, 2, false, true, false};
}

struct Fixture {
    BreadcrumbArchiveOutbox outbox{};
    BreadcrumbArchiveSession session{outbox, {1'000, 100}};
    FakeBreadcrumbArchiveRemote remote{};
    BreadcrumbArchiveUploader uploader{outbox, remote};
    FakeMonotonicCounterSource clock_source{};
    CheckedMonotonicClock clock{clock_source};
    BreadcrumbArchiveRetryCoordinator retry{
        clock, outbox, uploader, {10, 80}};
    FakeSnapshotLock lock{};
    SerializedBreadcrumbArchiveSnapshotSource source{
        session, outbox, retry, lock};
};

bool redacted(const BreadcrumbArchiveRuntimeSnapshot& snapshot) {
    return !snapshot.session.active &&
           snapshot.session.sessions_started == 0 &&
           snapshot.outbox.queued == 0 &&
           snapshot.retry.current_retry_ms == 0;
}

BreadcrumbArchiveRuntimeSnapshot sentinel() {
    BreadcrumbArchiveRuntimeSnapshot value{};
    value.session.active = true;
    value.session.sessions_started = 99;
    value.outbox.queued = 15;
    value.retry.current_retry_ms = 999;
    return value;
}

void test_ready_copy_uses_one_balanced_lock_boundary() {
    Fixture fixture{};
    BreadcrumbArchiveRuntimeSnapshot output{};
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(fixture.lock.acquire_calls() == 1);
    EXPECT(fixture.lock.release_calls() == 1);
    EXPECT(!fixture.lock.held());
    EXPECT(output.session.scheduler.policy_valid);
    EXPECT(output.retry.current_retry_ms == 10);
    EXPECT(fixture.source.status().ready_snapshots == 1);
}

void test_concrete_owner_state_is_copied_as_one_tuple() {
    Fixture fixture{};
    EXPECT(fixture.session.start(7, 20).started());
    BreadcrumbArchiveRuntimeSnapshot output{};
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(output.session.active);
    EXPECT(output.session.sessions_started == 1);
    EXPECT(output.session.scheduler.active);
    EXPECT(output.outbox.queued == 0);
    EXPECT(output.retry.current_retry_ms == 10);
}

void test_lock_contention_is_temporary_and_redacts_output() {
    Fixture fixture{};
    EXPECT(fixture.lock.enqueue_acquire(
        BreadcrumbArchiveSnapshotLockState::not_ready));
    auto output = sentinel();
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::not_ready);
    EXPECT(redacted(output));
    EXPECT(fixture.lock.acquire_calls() == 1);
    EXPECT(fixture.lock.release_calls() == 0);
    EXPECT(!fixture.source.status().failed_latched);

    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(fixture.lock.acquire_calls() == 2);
    EXPECT(fixture.lock.release_calls() == 1);
}

void test_lock_failure_latches_and_blocks_later_access() {
    Fixture fixture{};
    EXPECT(fixture.lock.enqueue_acquire(
        BreadcrumbArchiveSnapshotLockState::failed));
    auto output = sentinel();
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(redacted(output));
    EXPECT(fixture.source.status().latched_error ==
           BreadcrumbArchiveSnapshotAdapterError::lock_failed);
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(fixture.lock.acquire_calls() == 1);
    EXPECT(fixture.lock.release_calls() == 0);
}

void test_unlock_failure_never_publishes_copied_tuple() {
    Fixture fixture{};
    EXPECT(fixture.session.start(8, 30).started());
    EXPECT(fixture.lock.enqueue_release(
        BreadcrumbArchiveSnapshotUnlockState::failed));
    auto output = sentinel();
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(redacted(output));
    EXPECT(fixture.lock.acquire_calls() == 1);
    EXPECT(fixture.lock.release_calls() == 1);
    EXPECT(fixture.source.status().unlock_failures == 1);
    EXPECT(fixture.source.status().latched_error ==
           BreadcrumbArchiveSnapshotAdapterError::unlock_failed);
}

void test_unknown_lock_state_fails_closed() {
    Fixture fixture{};
    EXPECT(fixture.lock.enqueue_acquire(
        static_cast<BreadcrumbArchiveSnapshotLockState>(99)));
    BreadcrumbArchiveRuntimeSnapshot output{};
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(fixture.source.status().latched_error ==
           BreadcrumbArchiveSnapshotAdapterError::invalid_lock_state);
    EXPECT(fixture.lock.release_calls() == 0);
}

void test_unknown_unlock_state_fails_closed_and_redacts() {
    Fixture fixture{};
    EXPECT(fixture.lock.enqueue_release(
        static_cast<BreadcrumbArchiveSnapshotUnlockState>(99)));
    auto output = sentinel();
    EXPECT(fixture.source.snapshot(output) ==
           BreadcrumbArchiveSnapshotState::failed);
    EXPECT(redacted(output));
    EXPECT(fixture.source.status().latched_error ==
           BreadcrumbArchiveSnapshotAdapterError::invalid_unlock_state);
    EXPECT(fixture.source.status().unlock_failures == 1);
}

void test_retry_status_changes_are_observed_on_next_snapshot() {
    Fixture fixture{};
    BreadcrumbArchiveRuntimeSnapshot first{};
    EXPECT(fixture.source.snapshot(first) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(first.retry.idle == 0);
    EXPECT(fixture.retry.service().disposition ==
           BreadcrumbArchiveRetryDisposition::idle);
    BreadcrumbArchiveRuntimeSnapshot second{};
    EXPECT(fixture.source.snapshot(second) ==
           BreadcrumbArchiveSnapshotState::ready);
    EXPECT(second.retry.idle == 1);
    EXPECT(fixture.lock.acquire_calls() == 2);
    EXPECT(fixture.lock.release_calls() == 2);
}

void test_adapter_composes_with_privacy_safe_ui_owner() {
    Fixture fixture{};
    FakeDisplaySink display{};
    FakeLocalInputSource input{};
    CheckedLocalInterface local{display, input, capabilities()};
    BreadcrumbArchiveUiCoordinator coordinator{fixture.source, local};
    const auto result = coordinator.service();
    EXPECT(result.frame_presented);
    EXPECT(result.presented_notice == UiNotice::archive_stopped);
    EXPECT(display.latest_frame().action_count == 0);
    EXPECT(input.read_count() == 0);
    EXPECT(fixture.lock.acquire_calls() == 1);
    EXPECT(fixture.lock.release_calls() == 1);
}

void test_fake_lock_capacity_is_fixed_and_bounded() {
    FakeSnapshotLock lock{};
    for (std::size_t index = 0; index < FakeSnapshotLock::kCapacity; ++index) {
        EXPECT(lock.enqueue_acquire(
            BreadcrumbArchiveSnapshotLockState::not_ready));
        EXPECT(lock.enqueue_release(
            BreadcrumbArchiveSnapshotUnlockState::released));
    }
    EXPECT(!lock.enqueue_acquire(
        BreadcrumbArchiveSnapshotLockState::not_ready));
    EXPECT(!lock.enqueue_release(
        BreadcrumbArchiveSnapshotUnlockState::released));
}

static_assert(std::is_trivially_copyable_v<
              BreadcrumbArchiveSnapshotAdapterStatus>);
static_assert(sizeof(BreadcrumbArchiveSnapshotAdapterStatus) <= 32);

}  // namespace

int main() {
    test_ready_copy_uses_one_balanced_lock_boundary();
    test_concrete_owner_state_is_copied_as_one_tuple();
    test_lock_contention_is_temporary_and_redacts_output();
    test_lock_failure_latches_and_blocks_later_access();
    test_unlock_failure_never_publishes_copied_tuple();
    test_unknown_lock_state_fails_closed();
    test_unknown_unlock_state_fails_closed_and_redacts();
    test_retry_status_changes_are_observed_on_next_snapshot();
    test_adapter_composes_with_privacy_safe_ui_owner();
    test_fake_lock_capacity_is_fixed_and_bounded();

    if (failures != 0) {
        std::cerr << failures
                  << " breadcrumb archive snapshot adapter assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 breadcrumb archive snapshot adapter scenario groups\n";
    return EXIT_SUCCESS;
}
