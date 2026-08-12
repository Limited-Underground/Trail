#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_snapshot_adapter.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveRuntimeOperation : std::uint8_t {
    start_capture = 0,
    stop_capture,
    capture_position,
    service_upload,
};

enum class BreadcrumbArchiveRuntimeDisposition : std::uint8_t {
    completed = 0,
    deferred,
    failed,
};

enum class BreadcrumbArchiveRuntimeError : std::uint8_t {
    none = 0,
    lock_not_ready,
    lock_failed,
    unlock_failed,
    invalid_lock_state,
    invalid_unlock_state,
    snapshot_failed,
    latched_failure,
};

struct BreadcrumbArchiveRuntimeResult {
    BreadcrumbArchiveRuntimeOperation operation{
        BreadcrumbArchiveRuntimeOperation::start_capture};
    BreadcrumbArchiveRuntimeDisposition disposition{
        BreadcrumbArchiveRuntimeDisposition::failed};
    BreadcrumbArchiveRuntimeError error{
        BreadcrumbArchiveRuntimeError::none};
    location::BreadcrumbArchiveStartResult start{};
    location::PositionBroadcastScheduleResult capture{};
    location::BreadcrumbArchiveRetryResult upload{};
    bool operation_attempted{false};
    bool outcome_uncertain{false};

    [[nodiscard]] constexpr bool completed() const {
        return disposition == BreadcrumbArchiveRuntimeDisposition::completed;
    }
};

struct BreadcrumbArchiveRuntimeOwnerStatus {
    bool failed_latched{false};
    BreadcrumbArchiveRuntimeError latched_error{
        BreadcrumbArchiveRuntimeError::none};
    BreadcrumbArchiveSnapshotAdapterError snapshot_adapter_error{
        BreadcrumbArchiveSnapshotAdapterError::none};
    std::uint32_t operation_calls{0};
    std::uint32_t snapshot_calls{0};
    std::uint32_t lock_attempts{0};
    std::uint32_t locks_acquired{0};
    std::uint32_t completed{0};
    std::uint32_t deferred{0};
    std::uint32_t failed{0};
};

// Target-shaped optional archive owner. Concrete capture, outbox, uploader,
// retry, and snapshot objects are private so target composition cannot mutate
// them without passing through this owner's one injected lock boundary.
class SerializedBreadcrumbArchiveRuntimeOwner final
    : public BreadcrumbArchiveSnapshotSource {
public:
    SerializedBreadcrumbArchiveRuntimeOwner(
        time::CheckedMonotonicClock& clock,
        location::BreadcrumbArchiveRemoteTransport& remote,
        location::PositionBroadcastSchedulePolicy capture_policy,
        location::BreadcrumbArchiveRetryPolicy retry_policy,
        BreadcrumbArchiveSnapshotLock& lock);

    [[nodiscard]] BreadcrumbArchiveRuntimeResult start_capture(
        std::uint64_t session_id,
        std::uint64_t now_ms);
    [[nodiscard]] BreadcrumbArchiveRuntimeResult stop_capture();
    [[nodiscard]] BreadcrumbArchiveRuntimeResult capture_position(
        const location::LocationSnapshot& snapshot,
        std::uint64_t now_ms);
    [[nodiscard]] BreadcrumbArchiveRuntimeResult service_upload();

    [[nodiscard]] BreadcrumbArchiveSnapshotState snapshot(
        BreadcrumbArchiveRuntimeSnapshot& output) override;
    [[nodiscard]] BreadcrumbArchiveRuntimeOwnerStatus status() const;

private:
    [[nodiscard]] bool begin(
        BreadcrumbArchiveRuntimeResult& result);
    void finish(BreadcrumbArchiveRuntimeResult& result);
    void latch(BreadcrumbArchiveRuntimeError error);
    [[nodiscard]] bool synchronize_snapshot_failure();

    BreadcrumbArchiveSnapshotLock& lock_;
    location::BreadcrumbArchiveOutbox outbox_{};
    location::BreadcrumbArchiveSession session_;
    location::BreadcrumbArchiveUploader uploader_;
    location::BreadcrumbArchiveRetryCoordinator retry_;
    SerializedBreadcrumbArchiveSnapshotSource snapshot_source_;
    BreadcrumbArchiveRuntimeOwnerStatus status_{};
};

}  // namespace opentrail::integration
