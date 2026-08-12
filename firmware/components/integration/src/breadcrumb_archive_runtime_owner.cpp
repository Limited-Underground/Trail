#include "opentrail/breadcrumb_archive_runtime_owner.hpp"

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

SerializedBreadcrumbArchiveRuntimeOwner::
    SerializedBreadcrumbArchiveRuntimeOwner(
        time::CheckedMonotonicClock& clock,
        location::BreadcrumbArchiveRemoteTransport& remote,
        location::PositionBroadcastSchedulePolicy capture_policy,
        location::BreadcrumbArchiveRetryPolicy retry_policy,
        BreadcrumbArchiveSnapshotLock& lock)
    : lock_(lock),
      session_(outbox_, capture_policy),
      uploader_(outbox_, remote),
      retry_(clock, outbox_, uploader_, retry_policy),
      snapshot_source_(session_, outbox_, retry_, lock_) {}

BreadcrumbArchiveRuntimeResult
SerializedBreadcrumbArchiveRuntimeOwner::start_capture(
    std::uint64_t session_id,
    std::uint64_t now_ms) {
    BreadcrumbArchiveRuntimeResult result{};
    result.operation = BreadcrumbArchiveRuntimeOperation::start_capture;
    if (!begin(result)) {
        return result;
    }
    result.operation_attempted = true;
    result.start = session_.start(session_id, now_ms);
    finish(result);
    return result;
}

BreadcrumbArchiveRuntimeResult
SerializedBreadcrumbArchiveRuntimeOwner::stop_capture() {
    BreadcrumbArchiveRuntimeResult result{};
    result.operation = BreadcrumbArchiveRuntimeOperation::stop_capture;
    if (!begin(result)) {
        return result;
    }
    result.operation_attempted = true;
    session_.stop();
    finish(result);
    return result;
}

BreadcrumbArchiveRuntimeResult
SerializedBreadcrumbArchiveRuntimeOwner::capture_position(
    const location::LocationSnapshot& snapshot,
    std::uint64_t now_ms) {
    BreadcrumbArchiveRuntimeResult result{};
    result.operation = BreadcrumbArchiveRuntimeOperation::capture_position;
    if (!begin(result)) {
        return result;
    }
    result.operation_attempted = true;
    result.capture = session_.service(snapshot, now_ms);
    finish(result);
    return result;
}

BreadcrumbArchiveRuntimeResult
SerializedBreadcrumbArchiveRuntimeOwner::service_upload() {
    BreadcrumbArchiveRuntimeResult result{};
    result.operation = BreadcrumbArchiveRuntimeOperation::service_upload;
    if (!begin(result)) {
        return result;
    }
    result.operation_attempted = true;
    result.upload = retry_.service();
    finish(result);
    return result;
}

BreadcrumbArchiveSnapshotState
SerializedBreadcrumbArchiveRuntimeOwner::snapshot(
    BreadcrumbArchiveRuntimeSnapshot& output) {
    saturating_increment(status_.snapshot_calls);
    if (!synchronize_snapshot_failure() || status_.failed_latched) {
        redact(output);
        saturating_increment(status_.failed);
        return BreadcrumbArchiveSnapshotState::failed;
    }

    const auto state = snapshot_source_.snapshot(output);
    const auto adapter = snapshot_source_.status();
    status_.snapshot_adapter_error = adapter.latched_error;
    if (state == BreadcrumbArchiveSnapshotState::failed &&
        adapter.failed_latched) {
        latch(BreadcrumbArchiveRuntimeError::snapshot_failed);
        saturating_increment(status_.failed);
    } else if (state == BreadcrumbArchiveSnapshotState::not_ready) {
        saturating_increment(status_.deferred);
    } else if (state == BreadcrumbArchiveSnapshotState::ready) {
        saturating_increment(status_.completed);
    }
    return state;
}

BreadcrumbArchiveRuntimeOwnerStatus
SerializedBreadcrumbArchiveRuntimeOwner::status() const {
    return status_;
}

bool SerializedBreadcrumbArchiveRuntimeOwner::begin(
    BreadcrumbArchiveRuntimeResult& result) {
    saturating_increment(status_.operation_calls);
    if (!synchronize_snapshot_failure() || status_.failed_latched) {
        result.error = BreadcrumbArchiveRuntimeError::latched_failure;
        saturating_increment(status_.failed);
        return false;
    }

    saturating_increment(status_.lock_attempts);
    const auto acquired = lock_.acquire();
    if (acquired == BreadcrumbArchiveSnapshotLockState::not_ready) {
        result.disposition = BreadcrumbArchiveRuntimeDisposition::deferred;
        result.error = BreadcrumbArchiveRuntimeError::lock_not_ready;
        saturating_increment(status_.deferred);
        return false;
    }
    if (acquired == BreadcrumbArchiveSnapshotLockState::failed) {
        result.error = BreadcrumbArchiveRuntimeError::lock_failed;
        latch(result.error);
        saturating_increment(status_.failed);
        return false;
    }
    if (acquired != BreadcrumbArchiveSnapshotLockState::acquired) {
        result.error = BreadcrumbArchiveRuntimeError::invalid_lock_state;
        latch(result.error);
        saturating_increment(status_.failed);
        return false;
    }
    saturating_increment(status_.locks_acquired);
    return true;
}

void SerializedBreadcrumbArchiveRuntimeOwner::finish(
    BreadcrumbArchiveRuntimeResult& result) {
    const auto released = lock_.release();
    if (released == BreadcrumbArchiveSnapshotUnlockState::failed) {
        result.disposition = BreadcrumbArchiveRuntimeDisposition::failed;
        result.error = BreadcrumbArchiveRuntimeError::unlock_failed;
        result.outcome_uncertain = result.operation_attempted;
        latch(result.error);
        saturating_increment(status_.failed);
        return;
    }
    if (released != BreadcrumbArchiveSnapshotUnlockState::released) {
        result.disposition = BreadcrumbArchiveRuntimeDisposition::failed;
        result.error = BreadcrumbArchiveRuntimeError::invalid_unlock_state;
        result.outcome_uncertain = result.operation_attempted;
        latch(result.error);
        saturating_increment(status_.failed);
        return;
    }
    result.disposition = BreadcrumbArchiveRuntimeDisposition::completed;
    saturating_increment(status_.completed);
}

void SerializedBreadcrumbArchiveRuntimeOwner::latch(
    BreadcrumbArchiveRuntimeError error) {
    status_.failed_latched = true;
    status_.latched_error = error;
}

bool SerializedBreadcrumbArchiveRuntimeOwner::
    synchronize_snapshot_failure() {
    const auto adapter = snapshot_source_.status();
    status_.snapshot_adapter_error = adapter.latched_error;
    if (adapter.failed_latched && !status_.failed_latched) {
        latch(BreadcrumbArchiveRuntimeError::snapshot_failed);
    }
    return !adapter.failed_latched;
}

}  // namespace opentrail::integration
