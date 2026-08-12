#include "opentrail/breadcrumb_archive_navigation_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveNavigationCoordinator::
BreadcrumbArchiveNavigationCoordinator(
    persistence::PersistentStorage& storage,
    SerializedBreadcrumbArchiveRuntimeOwner& runtime,
    time::CheckedMonotonicClock& clock,
    ui::CheckedLocalInterface& local_interface,
    persistence::BreadcrumbArchiveSessionLeaseRequest lease_request)
    : local_interface_(local_interface),
      bootstrap_(storage, runtime, clock, local_interface, lease_request) {}

BreadcrumbArchiveNavigationResult
BreadcrumbArchiveNavigationCoordinator::open(
    const ui::ResolvedAction& action) {
    saturating_increment(status_.open_calls);
    BreadcrumbArchiveNavigationResult result{};
    if (status_.mode == BreadcrumbArchiveNavigationMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }

    const auto local = local_interface_.status();
    if (status_.mode != BreadcrumbArchiveNavigationMode::parent ||
        !action.ok() ||
        action.action != ui::UiAction::open_archive_controls ||
        !local.has_active_frame ||
        action.frame_revision != local.active_revision) {
        result.disposition =
            BreadcrumbArchiveNavigationDisposition::input_rejected;
        result.error =
            BreadcrumbArchiveNavigationError::invalid_open_action;
        saturating_increment(status_.input_rejections);
        return result;
    }
    if (action.frame_revision >=
        std::numeric_limits<std::uint32_t>::max() - 1) {
        result.disposition =
            BreadcrumbArchiveNavigationDisposition::input_rejected;
        result.error =
            BreadcrumbArchiveNavigationError::invalid_parent_revision;
        saturating_increment(status_.input_rejections);
        return result;
    }

    const auto bootstrap_status = bootstrap_.status();
    if (bootstrap_status.state ==
        BreadcrumbArchiveWorkflowBootstrapState::dormant) {
        result.bootstrap = bootstrap_.initialize(action.frame_revision + 1);
        result.lease_initialized =
            result.bootstrap.disposition ==
            BreadcrumbArchiveWorkflowBootstrapDisposition::initialized;
        if (!result.lease_initialized) {
            result.error = BreadcrumbArchiveNavigationError::bootstrap_failed;
            latch(result.error);
            return result;
        }
    } else if (bootstrap_status.state ==
               BreadcrumbArchiveWorkflowBootstrapState::ready) {
        result.bootstrap = bootstrap_.enter(action);
        result.workflow_called = result.bootstrap.workflow_called;
        if (!result.bootstrap.workflow_called ||
            result.bootstrap.workflow.disposition !=
                BreadcrumbArchiveWorkflowDisposition::entered) {
            result.error =
                BreadcrumbArchiveNavigationError::workflow_entry_failed;
            latch(result.error);
            return result;
        }
    } else {
        result.error = BreadcrumbArchiveNavigationError::bootstrap_failed;
        latch(result.error);
        return result;
    }

    status_.mode = BreadcrumbArchiveNavigationMode::workflow;
    status_.minimum_parent_revision = 0;
    saturating_increment(status_.successful_entries);
    result.disposition = BreadcrumbArchiveNavigationDisposition::opened;
    return result;
}

BreadcrumbArchiveNavigationResult
BreadcrumbArchiveNavigationCoordinator::service() {
    saturating_increment(status_.service_calls);
    BreadcrumbArchiveNavigationResult result{};
    if (status_.mode == BreadcrumbArchiveNavigationMode::faulted) {
        result.error = status_.latched_error;
        return result;
    }
    if (status_.mode == BreadcrumbArchiveNavigationMode::parent) {
        result.disposition = BreadcrumbArchiveNavigationDisposition::idle;
        return result;
    }

    result.bootstrap = bootstrap_.service();
    result.workflow_called = result.bootstrap.workflow_called;
    if (!result.bootstrap.workflow_called) {
        result.error = BreadcrumbArchiveNavigationError::bootstrap_failed;
        latch(result.error);
        return result;
    }
    if (result.bootstrap.workflow.disposition ==
        BreadcrumbArchiveWorkflowDisposition::exit_requested) {
        if (result.bootstrap.workflow.revision ==
            std::numeric_limits<std::uint32_t>::max()) {
            result.error =
                BreadcrumbArchiveNavigationError::invalid_parent_revision;
            latch(result.error);
            return result;
        }
        status_.mode = BreadcrumbArchiveNavigationMode::parent;
        status_.minimum_parent_revision =
            result.bootstrap.workflow.revision + 1;
        saturating_increment(status_.exit_requests);
        result.minimum_parent_revision = status_.minimum_parent_revision;
        result.disposition =
            BreadcrumbArchiveNavigationDisposition::exit_requested;
        return result;
    }

    result.disposition = BreadcrumbArchiveNavigationDisposition::forwarded;
    return result;
}

BreadcrumbArchiveNavigationStatus
BreadcrumbArchiveNavigationCoordinator::status() const {
    return status_;
}

BreadcrumbArchiveWorkflowBootstrapStatus
BreadcrumbArchiveNavigationCoordinator::bootstrap_status() const {
    return bootstrap_.status();
}

void BreadcrumbArchiveNavigationCoordinator::latch(
    BreadcrumbArchiveNavigationError error) {
    status_.mode = BreadcrumbArchiveNavigationMode::faulted;
    status_.latched_error = error;
    saturating_increment(status_.failures);
}

}  // namespace opentrail::integration
