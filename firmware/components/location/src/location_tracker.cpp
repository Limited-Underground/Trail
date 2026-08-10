#include "opentrail/location_tracker.hpp"

namespace opentrail::location {
namespace {

constexpr std::int32_t kMinimumLatitudeE7 = -900000000;
constexpr std::int32_t kMaximumLatitudeE7 = 900000000;
constexpr std::int32_t kMinimumLongitudeE7 = -1800000000;
constexpr std::int32_t kMaximumLongitudeE7 = 1800000000;
constexpr std::uint16_t kMaximumHeadingCentidegrees = 36000;

}  // namespace

LocationTracker::LocationTracker(
    GpsProvider& provider,
    std::uint32_t stale_after_ms)
    : provider_(provider), stale_after_ms_(stale_after_ms) {}

LocationSnapshot LocationTracker::snapshot(std::uint64_t now_ms) const {
    const auto reading = provider_.latest_fix();
    if (!reading.has_fix) {
        return {};
    }

    LocationSnapshot result{};
    result.fix = reading.fix;

    const auto validation_error = validate(reading.fix);
    if (validation_error != FixError::none) {
        result.state = FixState::invalid;
        result.error = validation_error;
        return result;
    }

    if (reading.fix.received_at_ms > now_ms) {
        result.state = FixState::invalid;
        result.error = FixError::timestamp_in_future;
        return result;
    }

    result.age_ms = now_ms - reading.fix.received_at_ms;
    if (result.age_ms >= stale_after_ms_) {
        result.state = FixState::stale;
        result.error = FixError::none;
        return result;
    }

    result.state = FixState::valid;
    result.error = FixError::none;
    return result;
}

FixError LocationTracker::validate(const GpsFix& fix) {
    if (fix.latitude_e7 < kMinimumLatitudeE7 ||
        fix.latitude_e7 > kMaximumLatitudeE7) {
        return FixError::latitude_out_of_range;
    }
    if (fix.longitude_e7 < kMinimumLongitudeE7 ||
        fix.longitude_e7 > kMaximumLongitudeE7) {
        return FixError::longitude_out_of_range;
    }
    if (fix.heading_valid &&
        fix.heading_centidegrees >= kMaximumHeadingCentidegrees) {
        return FixError::heading_out_of_range;
    }
    if (fix.horizontal_accuracy_valid &&
        fix.horizontal_accuracy_cm == 0) {
        return FixError::accuracy_zero;
    }
    return FixError::none;
}

}  // namespace opentrail::location
