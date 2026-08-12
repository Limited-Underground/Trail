#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_outbox.hpp"
#include "opentrail/monotonic_clock.hpp"

namespace opentrail::location {

struct BreadcrumbArchiveRetryPolicy {
    std::uint64_t initial_retry_ms{5'000};
    std::uint64_t maximum_retry_ms{60'000};

    [[nodiscard]] constexpr bool valid() const {
        return initial_retry_ms != 0 && maximum_retry_ms >= initial_retry_ms;
    }
};

enum class BreadcrumbArchiveRetryDisposition : std::uint8_t {
    idle = 0,
    attempted,
    waiting,
    clock_deferred,
    failed,
};

enum class BreadcrumbArchiveRetryError : std::uint8_t {
    none = 0,
    invalid_policy,
    clock_not_ready,
    clock_failed,
    deadline_overflow,
    remote_rejected,
    uploader_failed,
    latched_failure,
};

struct BreadcrumbArchiveRetryResult {
    BreadcrumbArchiveRetryDisposition disposition{
        BreadcrumbArchiveRetryDisposition::idle};
    BreadcrumbArchiveRetryError error{BreadcrumbArchiveRetryError::none};
    BreadcrumbArchiveUploadResult upload{};
    bool queue_retained{false};
    bool next_attempt_scheduled{false};
    std::uint64_t next_attempt_ms{0};
};

struct BreadcrumbArchiveRetryStatus {
    bool failed_latched{false};
    BreadcrumbArchiveRetryError latched_error{
        BreadcrumbArchiveRetryError::none};
    bool next_attempt_scheduled{false};
    std::uint64_t next_attempt_ms{0};
    std::uint64_t current_retry_ms{0};
    std::uint32_t idle{0};
    std::uint32_t attempted{0};
    std::uint32_t waiting{0};
    std::uint32_t clock_deferred{0};
    std::uint32_t failed{0};
};

// Checked-time, cooperative retry owner for the optional archive uploader.
// It never reads or controls the base radio. Remote rejection, time failure,
// impossible deadline arithmetic, or uploader ambiguity latches this boot
// composition closed while retaining the FIFO for operator reconciliation.
class BreadcrumbArchiveRetryCoordinator {
public:
    BreadcrumbArchiveRetryCoordinator(
        time::CheckedMonotonicClock& clock,
        BreadcrumbArchiveOutbox& outbox,
        BreadcrumbArchiveUploader& uploader,
        BreadcrumbArchiveRetryPolicy policy);

    [[nodiscard]] BreadcrumbArchiveRetryResult service();
    [[nodiscard]] BreadcrumbArchiveRetryStatus status() const;

private:
    [[nodiscard]] BreadcrumbArchiveRetryResult latch(
        BreadcrumbArchiveRetryError error,
        bool queue_retained);
    [[nodiscard]] BreadcrumbArchiveRetryResult schedule_retry(
        const BreadcrumbArchiveUploadResult& upload,
        std::uint64_t now_ms);
    void clear_schedule();

    time::CheckedMonotonicClock& clock_;
    BreadcrumbArchiveOutbox& outbox_;
    BreadcrumbArchiveUploader& uploader_;
    BreadcrumbArchiveRetryPolicy policy_{};
    BreadcrumbArchiveRetryStatus status_{};
};

}  // namespace opentrail::location
