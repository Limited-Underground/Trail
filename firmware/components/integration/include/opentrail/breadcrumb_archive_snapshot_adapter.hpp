#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_presentation.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveSnapshotLockState : std::uint8_t {
    acquired = 0,
    not_ready,
    failed,
};

enum class BreadcrumbArchiveSnapshotUnlockState : std::uint8_t {
    released = 0,
    failed,
};

// A target adapter may bind this boundary to one mutex, task-owned critical
// section, or equivalent nonblocking serialization primitive. All archive
// session/outbox/retry mutations must use that same target serialization
// domain; this interface alone cannot enforce external writer discipline.
class BreadcrumbArchiveSnapshotLock {
public:
    virtual ~BreadcrumbArchiveSnapshotLock() = default;

    [[nodiscard]] virtual BreadcrumbArchiveSnapshotLockState acquire() = 0;
    [[nodiscard]] virtual BreadcrumbArchiveSnapshotUnlockState release() = 0;
};

enum class BreadcrumbArchiveSnapshotAdapterError : std::uint8_t {
    none = 0,
    lock_failed,
    unlock_failed,
    invalid_lock_state,
    invalid_unlock_state,
    latched_failure,
};

struct BreadcrumbArchiveSnapshotAdapterStatus {
    bool failed_latched{false};
    BreadcrumbArchiveSnapshotAdapterError latched_error{
        BreadcrumbArchiveSnapshotAdapterError::none};
    std::uint32_t snapshot_calls{0};
    std::uint32_t lock_attempts{0};
    std::uint32_t locks_acquired{0};
    std::uint32_t ready_snapshots{0};
    std::uint32_t not_ready{0};
    std::uint32_t failed{0};
    std::uint32_t unlock_failures{0};
};

// Target-shaped composition of the three concrete archive status owners. A
// successful call copies all statuses while one injected lock is held, then
// releases the lock before publishing the complete tuple to the caller.
class SerializedBreadcrumbArchiveSnapshotSource final
    : public BreadcrumbArchiveSnapshotSource {
public:
    SerializedBreadcrumbArchiveSnapshotSource(
        location::BreadcrumbArchiveSession& session,
        location::BreadcrumbArchiveOutbox& outbox,
        location::BreadcrumbArchiveRetryCoordinator& retry,
        BreadcrumbArchiveSnapshotLock& lock);

    [[nodiscard]] BreadcrumbArchiveSnapshotState snapshot(
        BreadcrumbArchiveRuntimeSnapshot& output) override;
    [[nodiscard]] BreadcrumbArchiveSnapshotAdapterStatus status() const;

private:
    [[nodiscard]] BreadcrumbArchiveSnapshotState latch(
        BreadcrumbArchiveSnapshotAdapterError error,
        BreadcrumbArchiveRuntimeSnapshot& output);

    location::BreadcrumbArchiveSession& session_;
    location::BreadcrumbArchiveOutbox& outbox_;
    location::BreadcrumbArchiveRetryCoordinator& retry_;
    BreadcrumbArchiveSnapshotLock& lock_;
    BreadcrumbArchiveSnapshotAdapterStatus status_{};
};

}  // namespace opentrail::integration
