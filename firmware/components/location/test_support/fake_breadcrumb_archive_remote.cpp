#include "fake_breadcrumb_archive_remote.hpp"

#include <algorithm>

namespace opentrail::location::test_support {

BreadcrumbArchiveRemoteResult FakeBreadcrumbArchiveRemote::upload(
    radio::ByteView record,
    std::uint64_t now_ms) {
    if (record.data == nullptr ||
        record.size != kBreadcrumbArchiveRecordBytes ||
        attempts_ == records_.size()) {
        return BreadcrumbArchiveRemoteResult::failed;
    }
    std::copy(
        record.data, record.data + record.size, records_[attempts_].begin());
    attempted_at_[attempts_] = now_ms;
    ++attempts_;

    if (scripted_count_ == 0) {
        return BreadcrumbArchiveRemoteResult::durable_ack;
    }
    const auto result = scripted_[scripted_head_];
    scripted_head_ = (scripted_head_ + 1) % scripted_.size();
    --scripted_count_;
    return result;
}

bool FakeBreadcrumbArchiveRemote::push_result(
    BreadcrumbArchiveRemoteResult result) {
    if (scripted_count_ == scripted_.size()) {
        return false;
    }
    const auto tail = (scripted_head_ + scripted_count_) % scripted_.size();
    scripted_[tail] = result;
    ++scripted_count_;
    return true;
}

std::size_t FakeBreadcrumbArchiveRemote::attempts() const {
    return attempts_;
}

const std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes>*
FakeBreadcrumbArchiveRemote::record_at(std::size_t index) const {
    return index < attempts_ ? &records_[index] : nullptr;
}

std::uint64_t FakeBreadcrumbArchiveRemote::attempted_at(
    std::size_t index) const {
    return index < attempts_ ? attempted_at_[index] : 0;
}

}  // namespace opentrail::location::test_support
