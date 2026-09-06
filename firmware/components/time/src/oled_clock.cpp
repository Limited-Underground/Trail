#include "opentrail/oled_clock.hpp"

namespace opentrail::time {

bool OledClock::advance(std::uint64_t now_ms) {
    if (observed_ && now_ms < last_ms_) {
        valid_ = false;
        return false;
    }
    observed_ = true;
    last_ms_ = now_ms;
    return true;
}

bool OledClock::synchronize(std::uint32_t local_second_of_day,
                            OledClockFormat format,
                            std::uint64_t sync_monotonic_ms,
                            std::uint64_t now_ms,
                            bool source_authorized) {
    const bool old_callback = observed_ && sync_monotonic_ms < last_ms_;
    valid_ = false;
    if (!advance(now_ms) || !source_authorized || old_callback ||
        local_second_of_day >= 86'400 || sync_monotonic_ms > now_ms ||
        (format != OledClockFormat::hour_24 && format != OledClockFormat::hour_12)) {
        return false;
    }
    // Subtract only after ordering checks; never construct an overflowing deadline.
    if (now_ms - sync_monotonic_ms >= max_sync_age_ms) return false;
    synchronized_ms_ = sync_monotonic_ms;
    synchronized_second_ = local_second_of_day;
    format_ = format;
    valid_ = true;
    return true;
}

OledClockReading OledClock::observe(std::uint64_t now_ms) {
    OledClockReading result;
    if (!advance(now_ms) || !valid_) return result;
    const auto age_ms = now_ms - synchronized_ms_;
    if (age_ms >= max_sync_age_ms) {
        valid_ = false;
        return result;
    }
    // Both addends are below one day: the sum cannot overflow uint32_t.
    const auto second = (synchronized_second_ +
                         static_cast<std::uint32_t>(age_ms / 1'000)) % 86'400;
    const auto hour_24 = second / 3'600;
    const auto minute = (second / 60) % 60;
    const auto hour = format_ == OledClockFormat::hour_24 ? hour_24
        : (hour_24 % 12 == 0 ? 12 : hour_24 % 12);
    result.valid = true;
    result.local_second_of_day = second;
    result.format = format_;
    result.text[0] = static_cast<char>('0' + hour / 10);
    result.text[1] = static_cast<char>('0' + hour % 10);
    result.text[2] = ':';
    result.text[3] = static_cast<char>('0' + minute / 10);
    result.text[4] = static_cast<char>('0' + minute % 10);
    if (format_ == OledClockFormat::hour_12) {
        result.text[5] = ' ';
        result.text[6] = hour_24 < 12 ? 'A' : 'P';
        result.text[7] = 'M';
        result.text[8] = '\0';
    }
    return result;
}

}  // namespace opentrail::time
