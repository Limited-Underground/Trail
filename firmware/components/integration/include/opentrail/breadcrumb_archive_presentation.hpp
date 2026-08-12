#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive.hpp"
#include "opentrail/breadcrumb_archive_outbox.hpp"
#include "opentrail/breadcrumb_archive_retry.hpp"
#include "opentrail/local_interface.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchivePresentationError : std::uint8_t {
    none = 0,
    invalid_revision,
    incoherent_status,
    snapshot_not_ready,
    snapshot_failed,
    invalid_snapshot_state,
};

struct BreadcrumbArchiveRuntimeSnapshot {
    location::BreadcrumbArchiveStatus session{};
    location::BreadcrumbArchiveOutboxStatus outbox{};
    location::BreadcrumbArchiveRetryStatus retry{};
};

enum class BreadcrumbArchiveSnapshotState : std::uint8_t {
    ready = 0,
    not_ready,
    failed,
};

// A target implementation must copy all three status owners under one
// serialized read boundary. It must not return a mixture from different
// cooperative cycles. Common code performs exactly one call per capture.
class BreadcrumbArchiveSnapshotSource {
public:
    virtual ~BreadcrumbArchiveSnapshotSource() = default;

    [[nodiscard]] virtual BreadcrumbArchiveSnapshotState snapshot(
        BreadcrumbArchiveRuntimeSnapshot& output) = 0;
};

struct BreadcrumbArchivePresentationResult {
    BreadcrumbArchivePresentationError error{
        BreadcrumbArchivePresentationError::incoherent_status};
    ui::UiFrame frame{};
    bool has_safe_frame{false};

    [[nodiscard]] constexpr bool presentable() const {
        return has_safe_frame;
    }
};

// Maps private archive runtime state into one coordinate-free semantic frame.
// The frame grants no capture, upload, discard, export, or deletion authority.
// Invalid copied state fails visibly without implying that base radio service
// is unavailable.
[[nodiscard]] BreadcrumbArchivePresentationResult
make_breadcrumb_archive_presentation(
    const location::BreadcrumbArchiveStatus& session,
    const location::BreadcrumbArchiveOutboxStatus& outbox,
    const location::BreadcrumbArchiveRetryStatus& retry,
    std::uint32_t frame_revision);

// Reads one serialized status tuple and immediately reduces it to the
// coordinate-free semantic frame. not_ready returns no frame so a caller may
// retain its prior truthful presentation. Failed or unknown source state
// produces a generic action-free archive warning and ignores partial output.
[[nodiscard]] BreadcrumbArchivePresentationResult
capture_breadcrumb_archive_presentation(
    BreadcrumbArchiveSnapshotSource& source,
    std::uint32_t frame_revision);

}  // namespace opentrail::integration
