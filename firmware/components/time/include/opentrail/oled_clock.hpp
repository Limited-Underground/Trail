#pragma once

#include <array>
#include <cstdint>

namespace opentrail::time {

enum class OledClockFormat : std::uint8_t { hour_24, hour_12 };

struct OledClockReading {
    bool valid{false};
    std::uint32_t local_second_of_day{0};
    OledClockFormat format{OledClockFormat::hour_24};
    std::array<char, 9> text{'-', '-', ':', '-', '-', '\0'};
};

// Single-owner presentation state, with no protocol or authentication authority.
// source_authorized represents an upstream decision; this class cannot prove it.
// Sync timestamps use the same boot-local monotonic source as observe().
// Invalid synchronization invalidates the display. Rollback retains the high
// watermark, so recovery needs a fresh valid sync at or after that watermark.
class OledClock {
public:
    static constexpr std::uint64_t max_sync_age_ms = 86'400'000;

    [[nodiscard]] bool synchronize(std::uint32_t local_second_of_day,
                                   OledClockFormat format,
                                   std::uint64_t sync_monotonic_ms,
                                   std::uint64_t now_ms,
                                   bool source_authorized);
    [[nodiscard]] OledClockReading observe(std::uint64_t now_ms);

private:
    [[nodiscard]] bool advance(std::uint64_t now_ms);
    bool observed_{false};
    bool valid_{false};
    std::uint64_t last_ms_{0};
    std::uint64_t synchronized_ms_{0};
    std::uint32_t synchronized_second_{0};
    OledClockFormat format_{OledClockFormat::hour_24};
};

}  // namespace opentrail::time
