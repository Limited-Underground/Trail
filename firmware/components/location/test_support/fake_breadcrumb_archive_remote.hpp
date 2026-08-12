#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/breadcrumb_archive_outbox.hpp"

namespace opentrail::location::test_support {

inline constexpr std::size_t kFakeBreadcrumbArchiveRemoteCapacity = 32;

class FakeBreadcrumbArchiveRemote final
    : public BreadcrumbArchiveRemoteTransport {
public:
    [[nodiscard]] BreadcrumbArchiveRemoteResult upload(
        radio::ByteView record,
        std::uint64_t now_ms) override;

    [[nodiscard]] bool push_result(BreadcrumbArchiveRemoteResult result);
    [[nodiscard]] std::size_t attempts() const;
    [[nodiscard]] const std::array<
        std::uint8_t,
        kBreadcrumbArchiveRecordBytes>*
    record_at(std::size_t index) const;
    [[nodiscard]] std::uint64_t attempted_at(std::size_t index) const;

private:
    std::array<
        std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes>,
        kFakeBreadcrumbArchiveRemoteCapacity>
        records_{};
    std::array<std::uint64_t, kFakeBreadcrumbArchiveRemoteCapacity>
        attempted_at_{};
    std::array<
        BreadcrumbArchiveRemoteResult,
        kFakeBreadcrumbArchiveRemoteCapacity>
        scripted_{};
    std::size_t attempts_{0};
    std::size_t scripted_head_{0};
    std::size_t scripted_count_{0};
};

}  // namespace opentrail::location::test_support
