#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_presentation.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveUiDisposition : std::uint8_t {
    presented = 0,
    refreshed,
    unchanged,
    snapshot_deferred,
    display_deferred,
    failed,
};

enum class BreadcrumbArchiveUiError : std::uint8_t {
    none = 0,
    invalid_initial_revision,
    presentation_unavailable,
    display_failed,
    revision_exhausted,
};

struct BreadcrumbArchiveUiServiceResult {
    BreadcrumbArchiveUiDisposition disposition{
        BreadcrumbArchiveUiDisposition::failed};
    BreadcrumbArchiveUiError error{BreadcrumbArchiveUiError::none};
    BreadcrumbArchivePresentationError presentation_error{
        BreadcrumbArchivePresentationError::none};
    ui::PresentError present_error{ui::PresentError::none};
    ui::UiNotice presented_notice{ui::UiNotice::none};
    std::uint32_t revision{0};
    bool snapshot_read{false};
    bool frame_presented{false};
    bool prior_frame_retained{false};
};

struct BreadcrumbArchiveUiCoordinatorStatus {
    bool configuration_valid{false};
    bool has_presented_frame{false};
    bool revision_limit_reached{false};
    std::uint32_t active_revision{0};
    std::uint32_t next_revision{0};
    std::uint32_t service_calls{0};
    std::uint32_t snapshot_reads{0};
    std::uint32_t snapshot_deferrals{0};
    std::uint32_t presented_frames{0};
    std::uint32_t unchanged_frames{0};
    std::uint32_t display_deferrals{0};
    std::uint32_t failures{0};
};

// Cooperative single-owner archive-status UI service. Each valid service call
// performs exactly one serialized snapshot read. This component owns only UI
// revisions and refresh decisions; it cannot start/stop capture, mutate the
// outbox, upload records, poll input, or affect base radio service.
class BreadcrumbArchiveUiCoordinator {
public:
    BreadcrumbArchiveUiCoordinator(
        BreadcrumbArchiveSnapshotSource& snapshot_source,
        ui::CheckedLocalInterface& local_interface,
        std::uint32_t initial_revision = 1);

    [[nodiscard]] BreadcrumbArchiveUiServiceResult service();
    [[nodiscard]] BreadcrumbArchiveUiCoordinatorStatus status() const;

private:
    [[nodiscard]] bool semantics_changed(
        const ui::UiFrame& candidate) const;

    BreadcrumbArchiveSnapshotSource& snapshot_source_;
    ui::CheckedLocalInterface& local_interface_;
    BreadcrumbArchiveUiCoordinatorStatus status_{};
    ui::UiFrame active_frame_{};
};

}  // namespace opentrail::integration
