#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_navigation_coordinator.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveParentPageMode : std::uint8_t {
    inactive = 0,
    parent,
    workflow,
    restoring_parent,
    faulted,
};

enum class BreadcrumbArchiveParentPageDisposition : std::uint8_t {
    presented = 0,
    opened,
    forwarded,
    restored,
    exit_requested,
    idle,
    input_rejected,
    display_deferred,
    failed,
};

enum class BreadcrumbArchiveParentPageError : std::uint8_t {
    none = 0,
    invalid_activation,
    display_failed,
    input_failed,
    unexpected_action,
    navigation_failed,
};

struct BreadcrumbArchiveParentPageResult {
    BreadcrumbArchiveParentPageDisposition disposition{
        BreadcrumbArchiveParentPageDisposition::failed};
    BreadcrumbArchiveParentPageError error{
        BreadcrumbArchiveParentPageError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::ActionResolutionError action_error{
        ui::ActionResolutionError::none};
    BreadcrumbArchiveNavigationResult navigation{};
    std::uint32_t revision{0};
    bool input_polled{false};
    bool frame_presented{false};
    bool workflow_called{false};
};

struct BreadcrumbArchiveParentPageStatus {
    BreadcrumbArchiveParentPageMode mode{
        BreadcrumbArchiveParentPageMode::inactive};
    BreadcrumbArchiveParentPageError latched_error{
        BreadcrumbArchiveParentPageError::none};
    std::uint32_t active_revision{0};
    std::uint32_t activations{0};
    std::uint32_t service_calls{0};
    std::uint32_t parent_presentations{0};
    std::uint32_t workflow_entries{0};
    std::uint32_t workflow_exits{0};
    std::uint32_t exit_requests{0};
    std::uint32_t input_rejections{0};
    std::uint32_t failures{0};
};

// Owns one optional archive parent page and its nested navigation handoff. A
// broader application shell explicitly activates this page and must replace it
// after exit_requested. Presenting or opening the page cannot Start capture.
class BreadcrumbArchiveParentPageCoordinator {
public:
    BreadcrumbArchiveParentPageCoordinator(
        persistence::PersistentStorage& storage,
        SerializedBreadcrumbArchiveRuntimeOwner& runtime,
        time::CheckedMonotonicClock& clock,
        ui::CheckedLocalInterface& local_interface,
        persistence::BreadcrumbArchiveSessionLeaseRequest lease_request);
    BreadcrumbArchiveParentPageCoordinator(
        const BreadcrumbArchiveParentPageCoordinator&) = delete;
    BreadcrumbArchiveParentPageCoordinator& operator=(
        const BreadcrumbArchiveParentPageCoordinator&) = delete;
    BreadcrumbArchiveParentPageCoordinator(
        BreadcrumbArchiveParentPageCoordinator&&) = delete;
    BreadcrumbArchiveParentPageCoordinator& operator=(
        BreadcrumbArchiveParentPageCoordinator&&) = delete;

    [[nodiscard]] BreadcrumbArchiveParentPageResult activate(
        std::uint32_t revision,
        const ui::UiStatusSummary& status_summary);
    [[nodiscard]] BreadcrumbArchiveParentPageResult service();
    [[nodiscard]] BreadcrumbArchiveParentPageStatus status() const;
    [[nodiscard]] BreadcrumbArchiveNavigationStatus navigation_status() const;

private:
    [[nodiscard]] ui::UiFrame parent_frame(std::uint32_t revision) const;
    [[nodiscard]] bool present_parent(
        std::uint32_t revision,
        BreadcrumbArchiveParentPageResult& result);
    void latch(BreadcrumbArchiveParentPageError error);

    ui::CheckedLocalInterface& local_interface_;
    BreadcrumbArchiveNavigationCoordinator navigation_;
    BreadcrumbArchiveParentPageStatus status_{};
    ui::UiStatusSummary parent_status_{};
    std::uint32_t pending_parent_revision_{0};
};

}  // namespace opentrail::integration
