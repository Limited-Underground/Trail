#include "fake_breadcrumb_archive_transport.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::location::test_support {

BreadcrumbArchiveTransportError FakeBreadcrumbArchiveTransport::submit(
    radio::ByteView record,
    std::uint64_t now_ms) {
    if (attempts_ != std::numeric_limits<std::uint32_t>::max()) {
        ++attempts_;
    }
    if (record.data == nullptr ||
        record.size != kBreadcrumbArchiveRecordBytes) {
        return BreadcrumbArchiveTransportError::failed;
    }
    if (result_count_ != 0) {
        const auto result = results_[result_head_];
        result_head_ = (result_head_ + 1) % results_.size();
        --result_count_;
        if (result != BreadcrumbArchiveTransportError::none) {
            return result;
        }
    }
    if (count_ == records_.size()) {
        return BreadcrumbArchiveTransportError::full;
    }
    std::copy(record.data, record.data + record.size, records_[count_].begin());
    times_[count_] = now_ms;
    ++count_;
    return BreadcrumbArchiveTransportError::none;
}

bool FakeBreadcrumbArchiveTransport::enqueue_result(
    BreadcrumbArchiveTransportError error) {
    if (result_count_ == results_.size()) {
        return false;
    }
    const auto tail = (result_head_ + result_count_) % results_.size();
    results_[tail] = error;
    ++result_count_;
    return true;
}

const std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes>*
FakeBreadcrumbArchiveTransport::at(std::size_t index) const {
    return index < count_ ? &records_[index] : nullptr;
}

std::uint64_t FakeBreadcrumbArchiveTransport::submitted_at(
    std::size_t index) const {
    return index < count_ ? times_[index] : 0;
}

std::size_t FakeBreadcrumbArchiveTransport::size() const {
    return count_;
}

std::uint32_t FakeBreadcrumbArchiveTransport::attempts() const {
    return attempts_;
}

}  // namespace opentrail::location::test_support
