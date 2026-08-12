#include "opentrail/breadcrumb_archive_snapshot_adapter.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

void redact(BreadcrumbArchiveRuntimeSnapshot& output) {
    output = {};
}

}  // namespace

SerializedBreadcrumbArchiveSnapshotSource::
    SerializedBreadcrumbArchiveSnapshotSource(
        location::BreadcrumbArchiveSession& session,
        location::BreadcrumbArchiveOutbox& outbox,
        location::BreadcrumbArchiveRetryCoordinator& retry,
        BreadcrumbArchiveSnapshotLock& lock)
    : session_(session), outbox_(outbox), retry_(retry), lock_(lock) {}

BreadcrumbArchiveSnapshotState
SerializedBreadcrumbArchiveSnapshotSource::snapshot(
    BreadcrumbArchiveRuntimeSnapshot& output) {
    saturating_increment(status_.snapshot_calls);
    redact(output);
    if (status_.failed_latched) {
        saturating_increment(status_.failed);
        return BreadcrumbArchiveSnapshotState::failed;
    }

    saturating_increment(status_.lock_attempts);
    const auto acquired = lock_.acquire();
    if (acquired == BreadcrumbArchiveSnapshotLockState::not_ready) {
        saturating_increment(status_.not_ready);
        return BreadcrumbArchiveSnapshotState::not_ready;
    }
    if (acquired == BreadcrumbArchiveSnapshotLockState::failed) {
        return latch(
            BreadcrumbArchiveSnapshotAdapterError::lock_failed, output);
    }
    if (acquired != BreadcrumbArchiveSnapshotLockState::acquired) {
        return latch(
            BreadcrumbArchiveSnapshotAdapterError::invalid_lock_state,
            output);
    }
    saturating_increment(status_.locks_acquired);

    BreadcrumbArchiveRuntimeSnapshot candidate{};
    candidate.session = session_.status();
    candidate.outbox = outbox_.status();
    candidate.retry = retry_.status();

    const auto released = lock_.release();
    if (released == BreadcrumbArchiveSnapshotUnlockState::failed) {
        saturating_increment(status_.unlock_failures);
        return latch(
            BreadcrumbArchiveSnapshotAdapterError::unlock_failed, output);
    }
    if (released != BreadcrumbArchiveSnapshotUnlockState::released) {
        saturating_increment(status_.unlock_failures);
        return latch(
            BreadcrumbArchiveSnapshotAdapterError::invalid_unlock_state,
            output);
    }

    output = candidate;
    saturating_increment(status_.ready_snapshots);
    return BreadcrumbArchiveSnapshotState::ready;
}

BreadcrumbArchiveSnapshotAdapterStatus
SerializedBreadcrumbArchiveSnapshotSource::status() const {
    return status_;
}

BreadcrumbArchiveSnapshotState
SerializedBreadcrumbArchiveSnapshotSource::latch(
    BreadcrumbArchiveSnapshotAdapterError error,
    BreadcrumbArchiveRuntimeSnapshot& output) {
    redact(output);
    status_.failed_latched = true;
    status_.latched_error = error;
    saturating_increment(status_.failed);
    return BreadcrumbArchiveSnapshotState::failed;
}

}  // namespace opentrail::integration
