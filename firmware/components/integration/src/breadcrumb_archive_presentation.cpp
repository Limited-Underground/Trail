#include "opentrail/breadcrumb_archive_presentation.hpp"

namespace opentrail::integration {
namespace {

bool coherent_session(const location::BreadcrumbArchiveStatus& session) {
    if (session.active != session.scheduler.active ||
        session.sessions_stopped > session.sessions_started) {
        return false;
    }
    if (!session.active) {
        return session.next_sequence == 0;
    }
    return session.scheduler.policy_valid && session.sessions_started != 0 &&
           session.sessions_started == session.sessions_stopped + 1 &&
           session.next_sequence != 0;
}

bool coherent_outbox(
    const location::BreadcrumbArchiveOutboxStatus& outbox) {
    return outbox.queued <= location::kBreadcrumbArchiveOutboxCapacity &&
           (outbox.queued == 0 || outbox.has_prior_record);
}

bool coherent_retry(
    const location::BreadcrumbArchiveRetryStatus& retry,
    std::size_t queued) {
    if (retry.current_retry_ms == 0 ||
        retry.next_attempt_scheduled != (retry.next_attempt_ms != 0) ||
        (queued == 0 && retry.next_attempt_scheduled)) {
        return false;
    }
    if (retry.failed_latched) {
        return retry.latched_error !=
                   location::BreadcrumbArchiveRetryError::none &&
               !retry.next_attempt_scheduled;
    }
    return retry.latched_error ==
           location::BreadcrumbArchiveRetryError::none;
}

ui::UiFrame base_frame(
    std::uint32_t revision,
    std::size_t queued) {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::status;
    frame.status.archive_queue_count_valid = true;
    frame.status.archive_queue_count = static_cast<std::uint8_t>(queued);
    return frame;
}

ui::UiFrame failure_frame(
    std::uint32_t revision,
    std::size_t queued) {
    auto frame = base_frame(revision, queued);
    frame.attention = ui::UiAttention::warning;
    frame.notice = ui::UiNotice::archive_upload_failed;
    return frame;
}

}  // namespace

BreadcrumbArchivePresentationResult make_breadcrumb_archive_presentation(
    const location::BreadcrumbArchiveStatus& session,
    const location::BreadcrumbArchiveOutboxStatus& outbox,
    const location::BreadcrumbArchiveRetryStatus& retry,
    std::uint32_t frame_revision) {
    if (frame_revision == 0) {
        return {BreadcrumbArchivePresentationError::invalid_revision};
    }

    if (!coherent_session(session) || !coherent_outbox(outbox) ||
        !coherent_retry(retry, outbox.queued)) {
        const auto safe_queued =
            outbox.queued <= location::kBreadcrumbArchiveOutboxCapacity
                ? outbox.queued
                : 0;
        return {
            BreadcrumbArchivePresentationError::incoherent_status,
            failure_frame(frame_revision, safe_queued),
            true,
        };
    }

    auto frame = base_frame(frame_revision, outbox.queued);
    if (retry.failed_latched) {
        frame.attention = ui::UiAttention::warning;
        frame.notice = ui::UiNotice::archive_upload_failed;
    } else if (
        outbox.queued == location::kBreadcrumbArchiveOutboxCapacity ||
        session.last_record_error ==
            location::BreadcrumbArchiveRecordError::transport_full ||
        outbox.last_error == location::BreadcrumbArchiveOutboxError::full) {
        frame.attention = ui::UiAttention::warning;
        frame.notice = ui::UiNotice::archive_queue_full;
    } else if (outbox.queued != 0 && retry.next_attempt_scheduled) {
        frame.attention = ui::UiAttention::information;
        frame.notice = ui::UiNotice::archive_upload_waiting;
    } else if (outbox.queued != 0) {
        frame.attention = ui::UiAttention::information;
        frame.notice = ui::UiNotice::archive_queued;
    } else if (session.active) {
        frame.attention = ui::UiAttention::information;
        frame.notice = ui::UiNotice::archive_active;
    } else {
        frame.notice = ui::UiNotice::archive_stopped;
    }

    return {
        BreadcrumbArchivePresentationError::none,
        frame,
        true,
    };
}

}  // namespace opentrail::integration
