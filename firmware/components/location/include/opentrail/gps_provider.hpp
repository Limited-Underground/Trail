#pragma once

#include <cstdint>

namespace opentrail::location {

// Coordinates and optional measurements use integer units so the interface is
// deterministic on host and ESP32 targets without requiring floating point.
struct GpsFix {
    std::int32_t latitude_e7{0};
    std::int32_t longitude_e7{0};
    std::int32_t altitude_cm{0};
    std::uint32_t horizontal_accuracy_cm{0};
    std::uint32_t speed_cm_per_second{0};
    std::uint16_t heading_centidegrees{0};
    std::uint64_t received_at_ms{0};
    std::uint64_t utc_seconds{0};
    bool altitude_valid{false};
    bool horizontal_accuracy_valid{false};
    bool speed_valid{false};
    bool heading_valid{false};
    bool utc_valid{false};
};

struct GpsReadResult {
    bool has_fix{false};
    GpsFix fix{};
};

// Implementations expose their latest observation. They do not decide whether
// its fields are valid or whether its monotonic age is acceptable.
class GpsProvider {
public:
    virtual ~GpsProvider() = default;

    [[nodiscard]] virtual GpsReadResult latest_fix() const = 0;
};

}  // namespace opentrail::location
