#include "fake_monotonic_counter_source.hpp"

#include <limits>

namespace opentrail::time::test_support {

bool FakeMonotonicCounterSource::enqueue_time(std::uint64_t value_ms) {
    return enqueue({RawClockReadError::none, value_ms});
}

bool FakeMonotonicCounterSource::enqueue_not_ready() {
    return enqueue({RawClockReadError::not_ready, 0});
}

bool FakeMonotonicCounterSource::enqueue_failure() {
    return enqueue({RawClockReadError::source_failed, 0});
}

RawClockSample FakeMonotonicCounterSource::read() {
    if (read_count_ != std::numeric_limits<std::uint32_t>::max()) {
        ++read_count_;
    }
    if (size_ == 0) {
        return {RawClockReadError::not_ready, 0};
    }

    const auto sample = samples_[head_];
    head_ = (head_ + 1) % samples_.size();
    --size_;
    return sample;
}

std::size_t FakeMonotonicCounterSource::queued_count() const {
    return size_;
}

std::uint32_t FakeMonotonicCounterSource::read_count() const {
    return read_count_;
}

bool FakeMonotonicCounterSource::enqueue(const RawClockSample& sample) {
    if (size_ == samples_.size()) {
        return false;
    }
    const auto tail = (head_ + size_) % samples_.size();
    samples_[tail] = sample;
    ++size_;
    return true;
}

}  // namespace opentrail::time::test_support
