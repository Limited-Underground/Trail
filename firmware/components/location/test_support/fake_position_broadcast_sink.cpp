#include "fake_position_broadcast_sink.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::location::test_support {

bool FakePositionBroadcastSink::enqueue_result(
    PositionBroadcastSinkError error) {
    if (result_count_ == results_.size()) {
        return false;
    }
    const auto tail = (result_head_ + result_count_) % results_.size();
    results_[tail] = error;
    ++result_count_;
    return true;
}

PositionBroadcastSinkError FakePositionBroadcastSink::submit(
    radio::ByteView payload) {
    if (submit_attempts_ != std::numeric_limits<std::uint32_t>::max()) {
        ++submit_attempts_;
    }
    if (payload.data == nullptr || payload.size != kPositionPayloadBytes) {
        return PositionBroadcastSinkError::failed;
    }
    if (result_count_ != 0) {
        const auto result = results_[result_head_];
        result_head_ = (result_head_ + 1U) % results_.size();
        --result_count_;
        if (result != PositionBroadcastSinkError::none) {
            return result;
        }
    }
    if (payload_count_ == payloads_.size()) {
        return PositionBroadcastSinkError::full;
    }
    std::copy_n(
        payload.data,
        kPositionPayloadBytes,
        payloads_[payload_count_].begin());
    ++payload_count_;
    return PositionBroadcastSinkError::none;
}

std::size_t FakePositionBroadcastSink::size() const {
    return payload_count_;
}

const std::array<std::uint8_t, kPositionPayloadBytes>*
FakePositionBroadcastSink::at(std::size_t index) const {
    return index < payload_count_ ? &payloads_[index] : nullptr;
}

std::uint32_t FakePositionBroadcastSink::submit_attempts() const {
    return submit_attempts_;
}

}  // namespace opentrail::location::test_support
