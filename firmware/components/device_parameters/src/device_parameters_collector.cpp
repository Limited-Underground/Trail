#include "opentrail/device_parameters.hpp"

#include <limits>

namespace opentrail::device_parameters {
namespace {

constexpr auto kMaximumAge = std::numeric_limits<std::uint32_t>::max();

std::uint32_t bounded_age(std::uint64_t age_ms) {
    return age_ms > kMaximumAge ? kMaximumAge : static_cast<std::uint32_t>(age_ms);
}

template <class T>
bool sample(const Observation<T>& input, std::uint64_t now_ms, Sample<T>& output) {
    if (!input.available) return true;
    if (input.observed_at_ms > now_ms) return false;
    output = {true, bounded_age(now_ms - input.observed_at_ms), input.value};
    return true;
}

}  // namespace

std::uint32_t effective_age_ms(
    std::uint32_t source_age_ms, std::uint32_t snapshot_age_ms) {
    return bounded_age(static_cast<std::uint64_t>(source_age_ms) + snapshot_age_ms);
}

Error serve(const Snapshot& snapshot, std::uint64_t now_ms, Snapshot& output) {
    if (!valid(snapshot)) return Error::invalid_value;
    if (now_ms < snapshot.sampled_at_ms) return Error::clock_regression;

    const auto age_ms = bounded_age(now_ms - snapshot.sampled_at_ms);
    if (age_ms < snapshot.age_ms) return Error::clock_regression;

    Snapshot response = snapshot;
    response.age_ms = age_ms;
    output = response;
    return Error::none;
}

Error Collector::collect(const Inputs& inputs, std::uint64_t now_ms, Snapshot& output) {
    if (next_ == 0 || exhausted_) return Error::sequence_exhausted;
    if (has_previous_ && now_ms < previous_ms_) return Error::clock_regression;

    Snapshot candidate{};
    candidate.sequence = next_;
    candidate.sampled_at_ms = now_ms;
    if (!sample(inputs.battery, now_ms, candidate.battery) ||
        !sample(inputs.power, now_ms, candidate.power) ||
        !sample(inputs.packet, now_ms, candidate.packet) ||
        !sample(inputs.radio, now_ms, candidate.radio) ||
        !sample(inputs.backoffs, now_ms, candidate.backoffs) ||
        !sample(inputs.gnss, now_ms, candidate.gnss) ||
        !sample(inputs.reset, now_ms, candidate.reset) ||
        !sample(inputs.heap, now_ms, candidate.heap) ||
        !sample(inputs.main_stack, now_ms, candidate.main_stack) ||
        !sample(inputs.ble_stack, now_ms, candidate.ble_stack) ||
        !sample(inputs.storage, now_ms, candidate.storage) ||
        !sample(inputs.firmware, now_ms, candidate.firmware)) {
        return Error::clock_regression;
    }
    if (!valid(candidate)) return Error::invalid_value;

    output = candidate;
    previous_ms_ = now_ms;
    has_previous_ = true;
    if (next_ == kMaximumAge) {
        exhausted_ = true;
    } else {
        ++next_;
    }
    return Error::none;
}

}  // namespace opentrail::device_parameters
