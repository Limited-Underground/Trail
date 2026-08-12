#include "opentrail/breadcrumb_archive_retry.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::location {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveRetryCoordinator::BreadcrumbArchiveRetryCoordinator(
    time::CheckedMonotonicClock& clock,
    BreadcrumbArchiveOutbox& outbox,
    BreadcrumbArchiveUploader& uploader,
    BreadcrumbArchiveRetryPolicy policy)
    : clock_(clock), outbox_(outbox), uploader_(uploader), policy_(policy) {
    status_.current_retry_ms = policy_.initial_retry_ms;
    if (!policy_.valid()) {
        status_.failed_latched = true;
        status_.latched_error = BreadcrumbArchiveRetryError::invalid_policy;
    }
}

BreadcrumbArchiveRetryResult BreadcrumbArchiveRetryCoordinator::latch(
    BreadcrumbArchiveRetryError error,
    bool queue_retained) {
    status_.failed_latched = true;
    status_.latched_error = error;
    status_.next_attempt_scheduled = false;
    status_.next_attempt_ms = 0;
    saturating_increment(status_.failed);
    return {
        BreadcrumbArchiveRetryDisposition::failed,
        error,
        {},
        queue_retained,
        false,
        0,
    };
}

void BreadcrumbArchiveRetryCoordinator::clear_schedule() {
    status_.next_attempt_scheduled = false;
    status_.next_attempt_ms = 0;
    status_.current_retry_ms = policy_.initial_retry_ms;
}

BreadcrumbArchiveRetryResult
BreadcrumbArchiveRetryCoordinator::schedule_retry(
    const BreadcrumbArchiveUploadResult& upload,
    std::uint64_t now_ms) {
    const auto delay = status_.current_retry_ms;
    if (delay == 0 || now_ms > std::numeric_limits<std::uint64_t>::max() - delay) {
        return latch(
            BreadcrumbArchiveRetryError::deadline_overflow,
            upload.queue_retained);
    }

    status_.next_attempt_scheduled = true;
    status_.next_attempt_ms = now_ms + delay;
    if (status_.current_retry_ms > policy_.maximum_retry_ms / 2) {
        status_.current_retry_ms = policy_.maximum_retry_ms;
    } else {
        status_.current_retry_ms = std::min(
            policy_.maximum_retry_ms, status_.current_retry_ms * 2);
    }

    return {
        BreadcrumbArchiveRetryDisposition::attempted,
        BreadcrumbArchiveRetryError::none,
        upload,
        upload.queue_retained,
        true,
        status_.next_attempt_ms,
    };
}

BreadcrumbArchiveRetryResult BreadcrumbArchiveRetryCoordinator::service() {
    if (status_.failed_latched) {
        saturating_increment(status_.failed);
        return {
            BreadcrumbArchiveRetryDisposition::failed,
            BreadcrumbArchiveRetryError::latched_failure,
            {},
            outbox_.peek().has_record,
            false,
            0,
        };
    }

    if (!outbox_.peek().has_record) {
        clear_schedule();
        saturating_increment(status_.idle);
        return {};
    }

    const auto now = clock_.now();
    if (!now.ok()) {
        if (now.error == time::MonotonicClockError::not_ready) {
            saturating_increment(status_.clock_deferred);
            return {
                BreadcrumbArchiveRetryDisposition::clock_deferred,
                BreadcrumbArchiveRetryError::clock_not_ready,
                {},
                true,
                status_.next_attempt_scheduled,
                status_.next_attempt_ms,
            };
        }
        return latch(BreadcrumbArchiveRetryError::clock_failed, true);
    }

    if (status_.next_attempt_scheduled && now.value_ms < status_.next_attempt_ms) {
        saturating_increment(status_.waiting);
        return {
            BreadcrumbArchiveRetryDisposition::waiting,
            BreadcrumbArchiveRetryError::none,
            {},
            true,
            true,
            status_.next_attempt_ms,
        };
    }

    const auto upload = uploader_.service(now.value_ms);
    saturating_increment(status_.attempted);
    switch (upload.disposition) {
        case BreadcrumbArchiveUploadDisposition::committed:
            clear_schedule();
            return {
                BreadcrumbArchiveRetryDisposition::attempted,
                BreadcrumbArchiveRetryError::none,
                upload,
                false,
                false,
                0,
            };
        case BreadcrumbArchiveUploadDisposition::idle:
            clear_schedule();
            return {
                BreadcrumbArchiveRetryDisposition::idle,
                BreadcrumbArchiveRetryError::none,
                upload,
                false,
                false,
                0,
            };
        case BreadcrumbArchiveUploadDisposition::deferred:
            return schedule_retry(upload, now.value_ms);
        case BreadcrumbArchiveUploadDisposition::rejected:
            return latch(BreadcrumbArchiveRetryError::remote_rejected, true);
        case BreadcrumbArchiveUploadDisposition::failed:
            if (upload.error == BreadcrumbArchiveUploadError::remote_failed) {
                return schedule_retry(upload, now.value_ms);
            }
            return latch(BreadcrumbArchiveRetryError::uploader_failed, true);
        default:
            return latch(BreadcrumbArchiveRetryError::uploader_failed, true);
    }
}

BreadcrumbArchiveRetryStatus BreadcrumbArchiveRetryCoordinator::status() const {
    return status_;
}

}  // namespace opentrail::location
