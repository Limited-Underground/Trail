#include "opentrail/monotonic_clock.hpp"

#include <limits>

namespace opentrail::time {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

CheckedMonotonicClock::CheckedMonotonicClock(MonotonicCounterSource& source)
    : source_(source) {}

MonotonicTime CheckedMonotonicClock::now() {
    saturating_increment(status_.read_attempts);
    if (status_.fault_latched) {
        saturating_increment(status_.latched_refusals);
        return {MonotonicClockError::fault_latched, 0};
    }

    const auto sample = source_.read();
    switch (sample.error) {
        case RawClockReadError::not_ready:
            saturating_increment(status_.not_ready_reads);
            return {MonotonicClockError::not_ready, 0};
        case RawClockReadError::source_failed:
            status_.fault_latched = true;
            status_.latched_error = MonotonicClockError::source_failed;
            saturating_increment(status_.source_failures);
            return {MonotonicClockError::source_failed, 0};
        case RawClockReadError::none:
            break;
        default:
            status_.fault_latched = true;
            status_.latched_error = MonotonicClockError::source_failed;
            saturating_increment(status_.source_failures);
            return {MonotonicClockError::source_failed, 0};
    }

    if (status_.initialized && sample.value_ms < status_.last_value_ms) {
        status_.fault_latched = true;
        status_.latched_error = MonotonicClockError::rollback_detected;
        saturating_increment(status_.rollback_failures);
        return {MonotonicClockError::rollback_detected, 0};
    }

    status_.initialized = true;
    status_.last_value_ms = sample.value_ms;
    saturating_increment(status_.successful_reads);
    return {MonotonicClockError::none, sample.value_ms};
}

MonotonicClockStatus CheckedMonotonicClock::status() const {
    return status_;
}

}  // namespace opentrail::time
