#include "opentrail/breadcrumb_archive_parent_page_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveParentPageCoordinator::
BreadcrumbArchiveParentPageCoordinator(
    persistence::PersistentStorage& storage,
    SerializedBreadcrumbArchiveRuntimeOwner& runtime,
    time::CheckedMonotonicClock& clock,
    ui::CheckedLocalInterface& local_interface,
    persistence::BreadcrumbArchiveSessionLeaseRequest lease_request)
    : local_interface_(local_interface),
      navigation_(storage, runtime, clock, local_interface, lease_request) {}

BreadcrumbArchiveParentPageResult
BreadcrumbArchiveParentPageCoordinator::activate(
    std::uint32_t revision,
    const ui::UiStatusSummary& status_summary) {
    saturating_increment(status_.activations);
    BreadcrumbArchiveParentPageResult result{};
    if (status_.mode == BreadcrumbArchiveParentPageMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    const auto local = local_interface_.status();
    if (status_.mode != BreadcrumbArchiveParentPageMode::inactive ||
        revision == 0 ||
        (local.has_active_frame && revision <= local.active_revision)) {
        result.error = BreadcrumbArchiveParentPageError::invalid_activation;
        return result;
    }

    parent_status_ = status_summary;
    if (!present_parent(revision, result)) {
        return result;
    }
    status_.mode = BreadcrumbArchiveParentPageMode::parent;
    result.disposition = BreadcrumbArchiveParentPageDisposition::presented;
    return result;
}

BreadcrumbArchiveParentPageResult
BreadcrumbArchiveParentPageCoordinator::service() {
    saturating_increment(status_.service_calls);
    BreadcrumbArchiveParentPageResult result{};
    if (status_.mode == BreadcrumbArchiveParentPageMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    if (status_.mode == BreadcrumbArchiveParentPageMode::inactive) {
        result.disposition = BreadcrumbArchiveParentPageDisposition::idle;
        return result;
    }
    if (status_.mode ==
        BreadcrumbArchiveParentPageMode::restoring_parent) {
        if (!present_parent(pending_parent_revision_, result)) {
            return result;
        }
        pending_parent_revision_ = 0;
        status_.mode = BreadcrumbArchiveParentPageMode::parent;
        saturating_increment(status_.workflow_exits);
        result.disposition =
            BreadcrumbArchiveParentPageDisposition::restored;
        return result;
    }
    if (status_.mode == BreadcrumbArchiveParentPageMode::workflow) {
        result.navigation = navigation_.service();
        result.workflow_called = result.navigation.workflow_called;
        if (result.navigation.disposition ==
            BreadcrumbArchiveNavigationDisposition::exit_requested) {
            pending_parent_revision_ =
                result.navigation.minimum_parent_revision;
            status_.mode =
                BreadcrumbArchiveParentPageMode::restoring_parent;
            if (!present_parent(pending_parent_revision_, result)) {
                return result;
            }
            pending_parent_revision_ = 0;
            status_.mode = BreadcrumbArchiveParentPageMode::parent;
            saturating_increment(status_.workflow_exits);
            result.disposition =
                BreadcrumbArchiveParentPageDisposition::restored;
            return result;
        }
        if (result.navigation.error !=
                BreadcrumbArchiveNavigationError::none ||
            navigation_.status().mode ==
                BreadcrumbArchiveNavigationMode::faulted) {
            result.error =
                BreadcrumbArchiveParentPageError::navigation_failed;
            latch(result.error);
            return result;
        }
        result.disposition =
            BreadcrumbArchiveParentPageDisposition::forwarded;
        return result;
    }

    result.input_polled = true;
    const auto action = local_interface_.poll_action();
    result.action_error = action.error;
    if (action.error == ui::ActionResolutionError::input_not_ready) {
        result.disposition = BreadcrumbArchiveParentPageDisposition::idle;
        return result;
    }
    if (!action.ok()) {
        if (action.error == ui::ActionResolutionError::input_failed) {
            result.error = BreadcrumbArchiveParentPageError::input_failed;
            latch(result.error);
        } else {
            result.disposition =
                BreadcrumbArchiveParentPageDisposition::input_rejected;
            saturating_increment(status_.input_rejections);
        }
        return result;
    }

    if (action.action == ui::UiAction::cancel) {
        status_.mode = BreadcrumbArchiveParentPageMode::inactive;
        status_.active_revision = action.frame_revision;
        saturating_increment(status_.exit_requests);
        result.disposition =
            BreadcrumbArchiveParentPageDisposition::exit_requested;
        result.revision = action.frame_revision;
        return result;
    }
    if (action.action != ui::UiAction::open_archive_controls) {
        result.disposition =
            BreadcrumbArchiveParentPageDisposition::input_rejected;
        result.error = BreadcrumbArchiveParentPageError::unexpected_action;
        saturating_increment(status_.input_rejections);
        return result;
    }

    result.navigation = navigation_.open(action);
    if (result.navigation.disposition !=
        BreadcrumbArchiveNavigationDisposition::opened) {
        result.error = BreadcrumbArchiveParentPageError::navigation_failed;
        latch(result.error);
        return result;
    }
    status_.mode = BreadcrumbArchiveParentPageMode::workflow;
    saturating_increment(status_.workflow_entries);
    result.disposition = BreadcrumbArchiveParentPageDisposition::opened;
    return result;
}

BreadcrumbArchiveParentPageStatus
BreadcrumbArchiveParentPageCoordinator::status() const {
    return status_;
}

BreadcrumbArchiveNavigationStatus
BreadcrumbArchiveParentPageCoordinator::navigation_status() const {
    return navigation_.status();
}

ui::UiFrame BreadcrumbArchiveParentPageCoordinator::parent_frame(
    std::uint32_t revision) const {
    ui::UiFrame frame{};
    frame.revision = revision;
    frame.screen = ui::UiScreen::status;
    frame.attention = ui::UiAttention::information;
    frame.status = parent_status_;
    frame.action_count = 2;
    frame.actions[0] = {ui::UiAction::open_archive_controls, true};
    frame.actions[1] = {ui::UiAction::cancel, true};
    return frame;
}

bool BreadcrumbArchiveParentPageCoordinator::present_parent(
    std::uint32_t revision,
    BreadcrumbArchiveParentPageResult& result) {
    result.revision = revision;
    const auto presented = local_interface_.present(parent_frame(revision));
    result.present_error = presented.error;
    if (!presented.ok()) {
        result.disposition =
            presented.error == ui::PresentError::sink_not_ready
                ? BreadcrumbArchiveParentPageDisposition::display_deferred
                : BreadcrumbArchiveParentPageDisposition::failed;
        if (result.disposition ==
            BreadcrumbArchiveParentPageDisposition::failed) {
            result.error = BreadcrumbArchiveParentPageError::display_failed;
            latch(result.error);
        }
        return false;
    }
    status_.active_revision = revision;
    saturating_increment(status_.parent_presentations);
    result.frame_presented = true;
    return true;
}

void BreadcrumbArchiveParentPageCoordinator::latch(
    BreadcrumbArchiveParentPageError error) {
    status_.mode = BreadcrumbArchiveParentPageMode::faulted;
    status_.latched_error = error;
    saturating_increment(status_.failures);
}

}  // namespace opentrail::integration
