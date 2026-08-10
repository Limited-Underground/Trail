#pragma once

#include <cstdint>

#include "opentrail/gps_provider.hpp"

namespace opentrail::location {

enum class FixState : std::uint8_t {
    unavailable = 0,
    valid,
    stale,
    invalid,
};

enum class FixError : std::uint8_t {
    none = 0,
    no_fix,
    latitude_out_of_range,
    longitude_out_of_range,
    heading_out_of_range,
    accuracy_zero,
    timestamp_in_future,
};

struct LocationSnapshot {
    FixState state{FixState::unavailable};
    FixError error{FixError::no_fix};
    GpsFix fix{};
    std::uint64_t age_ms{0};

    [[nodiscard]] constexpr bool usable() const {
        return state == FixState::valid;
    }
};

class LocationTracker {
public:
    LocationTracker(GpsProvider& provider, std::uint32_t stale_after_ms);

    [[nodiscard]] LocationSnapshot snapshot(std::uint64_t now_ms) const;

private:
    [[nodiscard]] static FixError validate(const GpsFix& fix);

    GpsProvider& provider_;
    std::uint32_t stale_after_ms_{0};
};

}  // namespace opentrail::location
