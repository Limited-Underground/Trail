#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_workflow_bootstrap.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveNavigationMode : std::uint8_t {
    parent = 0,
    workflow,
    faulted,
};

enum class BreadcrumbArchiveNavigationDisposition : std::uint8_t {
    opened = 0,
    forwarded,
    exit_requested,
    idle,
    input_rejected,
    failed,
};

enum class BreadcrumbArchiveNavigationError : std::uint8_t {
    none = 0,
    invalid_open_action,
    invalid_parent_revision,
    bootstrap_failed,
    workflow_entry_failed,
};

struct BreadcrumbArchiveNavigationResult {
    BreadcrumbArchiveNavigationDisposition disposition{
        BreadcrumbArchiveNavigationDisposition::failed};
    BreadcrumbArchiveNavigationError error{
        BreadcrumbArchiveNavigationError::none};
    BreadcrumbArchiveWorkflowBootstrapResult bootstrap{};
    std::uint32_t minimum_parent_revision{0};
    bool lease_initialized{false};
    bool workflow_called{false};
};

struct BreadcrumbArchiveNavigationStatus {
    BreadcrumbArchiveNavigationMode mode{
        BreadcrumbArchiveNavigationMode::parent};
    BreadcrumbArchiveNavigationError latched_error{
        BreadcrumbArchiveNavigationError::none};
    std::uint32_t minimum_parent_revision{0};
    std::uint32_t open_calls{0};
    std::uint32_t service_calls{0};
    std::uint32_t successful_entries{0};
    std::uint32_t exit_requests{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Exact-revision handoff between an external parent shell and the optional
// archive workflow. The first valid local open action initializes the durable
// lease bootstrap; later valid parent actions re-enter the same boot workflow.
// This owner never creates/polls a parent frame and has no archive Start,
// radio, server, or base-client authority.
class BreadcrumbArchiveNavigationCoordinator {
public:
    BreadcrumbArchiveNavigationCoordinator(
        persistence::PersistentStorage& storage,
        SerializedBreadcrumbArchiveRuntimeOwner& runtime,
        time::CheckedMonotonicClock& clock,
        ui::CheckedLocalInterface& local_interface,
        persistence::BreadcrumbArchiveSessionLeaseRequest lease_request);
    BreadcrumbArchiveNavigationCoordinator(
        const BreadcrumbArchiveNavigationCoordinator&) = delete;
    BreadcrumbArchiveNavigationCoordinator& operator=(
        const BreadcrumbArchiveNavigationCoordinator&) = delete;
    BreadcrumbArchiveNavigationCoordinator(
        BreadcrumbArchiveNavigationCoordinator&&) = delete;
    BreadcrumbArchiveNavigationCoordinator& operator=(
        BreadcrumbArchiveNavigationCoordinator&&) = delete;

    [[nodiscard]] BreadcrumbArchiveNavigationResult open(
        const ui::ResolvedAction& action);
    [[nodiscard]] BreadcrumbArchiveNavigationResult service();
    [[nodiscard]] BreadcrumbArchiveNavigationStatus status() const;
    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapStatus
    bootstrap_status() const;

private:
    void latch(BreadcrumbArchiveNavigationError error);

    ui::CheckedLocalInterface& local_interface_;
    BreadcrumbArchiveWorkflowBootstrap bootstrap_;
    BreadcrumbArchiveNavigationStatus status_{};
};

}  // namespace opentrail::integration
