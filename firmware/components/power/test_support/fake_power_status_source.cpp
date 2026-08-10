#include "fake_power_status_source.hpp"

#include <limits>

namespace opentrail::power::test_support {

bool FakePowerStatusSource::enqueue(const RawPowerObservation& observation) {
    if (size_ == observations_.size()) {
        return false;
    }
    const auto tail = (head_ + size_) % observations_.size();
    observations_[tail] = observation;
    ++size_;
    return true;
}

bool FakePowerStatusSource::enqueue_not_ready() {
    return enqueue({});
}

bool FakePowerStatusSource::enqueue_failure() {
    RawPowerObservation observation{};
    observation.error = PowerReadError::source_failed;
    return enqueue(observation);
}

RawPowerObservation FakePowerStatusSource::read() {
    if (read_count_ != std::numeric_limits<std::uint32_t>::max()) {
        ++read_count_;
    }
    if (size_ == 0) {
        return {};
    }

    const auto observation = observations_[head_];
    head_ = (head_ + 1) % observations_.size();
    --size_;
    return observation;
}

std::size_t FakePowerStatusSource::queued_count() const {
    return size_;
}

std::uint32_t FakePowerStatusSource::read_count() const {
    return read_count_;
}

}  // namespace opentrail::power::test_support
