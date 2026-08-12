#include "opentrail/breadcrumb_archive_ui_coordinator.hpp"

#include <cstddef>
#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool same_status(const ui::UiStatusSummary& left,
                 const ui::UiStatusSummary& right) {
    return left.radio == right.radio &&
           left.position == right.position &&
           left.power == right.power &&
           left.peer_count_valid == right.peer_count_valid &&
           left.peer_count == right.peer_count &&
           left.unread_messages == right.unread_messages &&
           left.archive_queue_count_valid ==
               right.archive_queue_count_valid &&
           left.archive_queue_count == right.archive_queue_count;
}

bool same_semantics(const ui::UiFrame& left, const ui::UiFrame& right) {
    if (left.screen != right.screen ||
        left.attention != right.attention ||
        left.notice != right.notice ||
        !same_status(left.status, right.status) ||
        left.action_count != right.action_count) {
        return false;
    }
    for (std::size_t index = 0; index < ui::kMaxUiActions; ++index) {
        if (left.actions[index].action != right.actions[index].action ||
            left.actions[index].enabled != right.actions[index].enabled) {
            return false;
        }
    }
    return true;
}

}  // namespace

BreadcrumbArchiveUiCoordinator::BreadcrumbArchiveUiCoordinator(
    BreadcrumbArchiveSnapshotSource& snapshot_source,
    ui::CheckedLocalInterface& local_interface,
    std::uint32_t initial_revision)
    : snapshot_source_(snapshot_source), local_interface_(local_interface) {
    status_.configuration_valid = initial_revision != 0;
    status_.next_revision = initial_revision;
}

BreadcrumbArchiveUiServiceResult
BreadcrumbArchiveUiCoordinator::service() {
    saturating_increment(status_.service_calls);
    BreadcrumbArchiveUiServiceResult result{};
    if (!status_.configuration_valid) {
        result.error = BreadcrumbArchiveUiError::invalid_initial_revision;
        saturating_increment(status_.failures);
        return result;
    }

    const auto presentation = capture_breadcrumb_archive_presentation(
        snapshot_source_, status_.next_revision);
    result.snapshot_read = true;
    result.presentation_error = presentation.error;
    saturating_increment(status_.snapshot_reads);

    if (!presentation.presentable()) {
        result.prior_frame_retained = status_.has_presented_frame;
        if (presentation.error ==
            BreadcrumbArchivePresentationError::snapshot_not_ready) {
            result.disposition =
                BreadcrumbArchiveUiDisposition::snapshot_deferred;
            result.revision = status_.active_revision;
            saturating_increment(status_.snapshot_deferrals);
            return result;
        }
        result.error = BreadcrumbArchiveUiError::presentation_unavailable;
        saturating_increment(status_.failures);
        return result;
    }

    if (status_.has_presented_frame &&
        !semantics_changed(presentation.frame)) {
        result.disposition = BreadcrumbArchiveUiDisposition::unchanged;
        result.revision = status_.active_revision;
        result.presented_notice = active_frame_.notice;
        result.prior_frame_retained = true;
        saturating_increment(status_.unchanged_frames);
        return result;
    }

    if (status_.revision_limit_reached) {
        result.error = BreadcrumbArchiveUiError::revision_exhausted;
        result.revision = status_.active_revision;
        result.prior_frame_retained = status_.has_presented_frame;
        saturating_increment(status_.failures);
        return result;
    }

    result.revision = status_.next_revision;
    const auto presented = local_interface_.present(presentation.frame);
    result.present_error = presented.error;
    if (!presented.ok()) {
        result.prior_frame_retained = status_.has_presented_frame;
        if (presented.error == ui::PresentError::sink_not_ready) {
            result.disposition =
                BreadcrumbArchiveUiDisposition::display_deferred;
            saturating_increment(status_.display_deferrals);
            return result;
        }
        result.error = BreadcrumbArchiveUiError::display_failed;
        saturating_increment(status_.failures);
        return result;
    }

    const bool refreshed = status_.has_presented_frame;
    active_frame_ = presentation.frame;
    status_.has_presented_frame = true;
    status_.active_revision = status_.next_revision;
    if (status_.next_revision ==
        std::numeric_limits<std::uint32_t>::max()) {
        status_.revision_limit_reached = true;
    } else {
        ++status_.next_revision;
    }
    saturating_increment(status_.presented_frames);

    result.disposition = refreshed
                             ? BreadcrumbArchiveUiDisposition::refreshed
                             : BreadcrumbArchiveUiDisposition::presented;
    result.frame_presented = true;
    result.presented_notice = presentation.frame.notice;
    return result;
}

BreadcrumbArchiveUiCoordinatorStatus
BreadcrumbArchiveUiCoordinator::status() const {
    return status_;
}

bool BreadcrumbArchiveUiCoordinator::semantics_changed(
    const ui::UiFrame& candidate) const {
    return !same_semantics(active_frame_, candidate);
}

}  // namespace opentrail::integration
