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

}  // namespace opentrail::integration
