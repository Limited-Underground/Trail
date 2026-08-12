#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/breadcrumb_archive.hpp"

namespace opentrail::location::test_support {

class FakeBreadcrumbArchiveTransport final
    : public BreadcrumbArchiveTransport {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] BreadcrumbArchiveTransportError submit(
        radio::ByteView record,
        std::uint64_t now_ms) override;

    [[nodiscard]] bool enqueue_result(
        BreadcrumbArchiveTransportError error);
    [[nodiscard]] const std::array<
        std::uint8_t, kBreadcrumbArchiveRecordBytes>* at(
        std::size_t index) const;
    [[nodiscard]] std::uint64_t submitted_at(std::size_t index) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint32_t attempts() const;

private:
    std::array<std::array<
        std::uint8_t, kBreadcrumbArchiveRecordBytes>, kCapacity> records_{};
    std::array<std::uint64_t, kCapacity> times_{};
    std::array<BreadcrumbArchiveTransportError, kCapacity> results_{};
    std::size_t count_{0};
    std::size_t result_head_{0};
    std::size_t result_count_{0};
    std::uint32_t attempts_{0};
};

}  // namespace opentrail::location::test_support
