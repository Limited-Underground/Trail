#include "opentrail/breadcrumb_archive_workflow_bootstrap.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveWorkflowBootstrap::BreadcrumbArchiveWorkflowBootstrap(
    persistence::PersistentStorage& storage,
    SerializedBreadcrumbArchiveRuntimeOwner& runtime,
    time::CheckedMonotonicClock& clock,
    ui::CheckedLocalInterface& local_interface,
    persistence::BreadcrumbArchiveSessionLeaseRequest lease_request)
    : lease_store_(storage),
      runtime_(runtime),
      clock_(clock),
      local_interface_(local_interface),
      lease_request_(lease_request) {}

BreadcrumbArchiveWorkflowBootstrapResult
BreadcrumbArchiveWorkflowBootstrap::initialize(
    std::uint32_t initial_revision) {
    saturating_increment(status_.initialize_calls);
    if (status_.state == BreadcrumbArchiveWorkflowBootstrapState::ready) {
        BreadcrumbArchiveWorkflowBootstrapResult result{};
        result.disposition =
            BreadcrumbArchiveWorkflowBootstrapDisposition::already_ready;
        return result;
    }
    if (status_.state == BreadcrumbArchiveWorkflowBootstrapState::failed) {
        return unavailable();
    }
    if (lease_request_.initial_session_id == 0 ||
        lease_request_.lease_size == 0 || initial_revision == 0 ||
        initial_revision == std::numeric_limits<std::uint32_t>::max()) {
        latch(BreadcrumbArchiveWorkflowBootstrapError::invalid_configuration);
        return unavailable();
    }

    BreadcrumbArchiveWorkflowBootstrapResult result{};
    result.lease_attempted = true;
    saturating_increment(status_.lease_attempts);
    const auto allocation = lease_store_.allocate(lease_request_);
    result.lease_error = allocation.error;
    status_.lease_error = allocation.error;
    if (!allocation.allocated()) {
        latch(
            BreadcrumbArchiveWorkflowBootstrapError::lease_allocation_failed);
        result.disposition =
            BreadcrumbArchiveWorkflowBootstrapDisposition::failed;
        result.error = status_.latched_error;
        return result;
    }

    workflow_.emplace(
        runtime_,
        clock_,
        local_interface_,
        allocation.first_session_id,
        allocation.final_session_id,
        initial_revision);
    if (!workflow_->status().configuration_valid) {
        workflow_.reset();
        latch(
            BreadcrumbArchiveWorkflowBootstrapError::workflow_configuration_failed);
        result.disposition =
            BreadcrumbArchiveWorkflowBootstrapDisposition::failed;
        result.error = status_.latched_error;
        return result;
    }

    status_.state = BreadcrumbArchiveWorkflowBootstrapState::ready;
    status_.lease_generation = allocation.generation;
    status_.first_session_id = allocation.first_session_id;
    status_.final_session_id = allocation.final_session_id;
    result.disposition =
        BreadcrumbArchiveWorkflowBootstrapDisposition::initialized;
    return result;
}

BreadcrumbArchiveWorkflowBootstrapResult
BreadcrumbArchiveWorkflowBootstrap::service() {
    if (!workflow_.has_value() ||
        status_.state != BreadcrumbArchiveWorkflowBootstrapState::ready) {
        return unavailable();
    }
    BreadcrumbArchiveWorkflowBootstrapResult result{};
    result.disposition =
        BreadcrumbArchiveWorkflowBootstrapDisposition::forwarded;
    result.workflow_called = true;
    saturating_increment(status_.workflow_calls);
    result.workflow = workflow_->service();
    return result;
}

BreadcrumbArchiveWorkflowBootstrapResult
BreadcrumbArchiveWorkflowBootstrap::enter(
    const ui::ResolvedAction& action) {
    if (!workflow_.has_value() ||
        status_.state != BreadcrumbArchiveWorkflowBootstrapState::ready) {
        return unavailable();
    }
    BreadcrumbArchiveWorkflowBootstrapResult result{};
    result.disposition =
        BreadcrumbArchiveWorkflowBootstrapDisposition::forwarded;
    result.workflow_called = true;
    saturating_increment(status_.workflow_calls);
    result.workflow = workflow_->enter(action);
    return result;
}

BreadcrumbArchiveWorkflowBootstrapStatus
BreadcrumbArchiveWorkflowBootstrap::status() const {
    return status_;
}

BreadcrumbArchiveWorkflowBootstrapResult
BreadcrumbArchiveWorkflowBootstrap::unavailable() const {
    BreadcrumbArchiveWorkflowBootstrapResult result{};
    result.disposition =
        status_.state == BreadcrumbArchiveWorkflowBootstrapState::failed
            ? BreadcrumbArchiveWorkflowBootstrapDisposition::failed
            : BreadcrumbArchiveWorkflowBootstrapDisposition::rejected;
    result.error =
        status_.state == BreadcrumbArchiveWorkflowBootstrapState::failed
            ? status_.latched_error
            : BreadcrumbArchiveWorkflowBootstrapError::not_initialized;
    result.lease_error = status_.lease_error;
    return result;
}

void BreadcrumbArchiveWorkflowBootstrap::latch(
    BreadcrumbArchiveWorkflowBootstrapError error) {
    status_.state = BreadcrumbArchiveWorkflowBootstrapState::failed;
    status_.latched_error = error;
}

}  // namespace opentrail::integration
