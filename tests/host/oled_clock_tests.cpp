#include "opentrail/oled_clock.hpp"

#include <cstring>
#include <iostream>
#include <limits>

namespace {
using opentrail::time::OledClock;
using opentrail::time::OledClockFormat;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

void unknown_and_midnight_noon() {
    OledClock clock;
    EXPECT(!clock.observe(0).valid);
    EXPECT(std::strcmp(clock.observe(0).text.data(), "--:--") == 0);
    EXPECT(clock.synchronize(0, OledClockFormat::hour_12, 0, 0, true));
    EXPECT(std::strcmp(clock.observe(0).text.data(), "12:00 AM") == 0);
    EXPECT(clock.synchronize(43'200, OledClockFormat::hour_12, 1, 1, true));
    EXPECT(std::strcmp(clock.observe(1).text.data(), "12:00 PM") == 0);
    EXPECT(clock.synchronize(43'200, OledClockFormat::hour_24, 2, 2, true));
    EXPECT(std::strcmp(clock.observe(2).text.data(), "12:00") == 0);
}

void rollover_and_disconnected_progress() {
    OledClock clock;
    EXPECT(clock.synchronize(86'399, OledClockFormat::hour_24, 100, 100, true));
    EXPECT(std::strcmp(clock.observe(1'099).text.data(), "23:59") == 0);
    EXPECT(clock.observe(1'100).local_second_of_day == 0);
    EXPECT(std::strcmp(clock.observe(1'100).text.data(), "00:00") == 0);
    // No phone callback or connection state is required to keep running.
    EXPECT(std::strcmp(clock.observe(61'100).text.data(), "00:01") == 0);
}

void exact_staleness_and_resync() {
    OledClock clock;
    EXPECT(clock.synchronize(0, OledClockFormat::hour_24, 0, 0, true));
    EXPECT(clock.observe(OledClock::max_sync_age_ms - 1).valid);
    EXPECT(!clock.observe(OledClock::max_sync_age_ms).valid);
    EXPECT(!clock.observe(OledClock::max_sync_age_ms + 1).valid);
    EXPECT(clock.synchronize(3'600, OledClockFormat::hour_12,
        OledClock::max_sync_age_ms + 2, OledClock::max_sync_age_ms + 2, true));
    EXPECT(std::strcmp(clock.observe(OledClock::max_sync_age_ms + 2).text.data(), "01:00 AM") == 0);
}

void invalid_sync_and_timestamp_ordering() {
    OledClock clock;
    EXPECT(!clock.synchronize(86'400, OledClockFormat::hour_24, 0, 0, true));
    EXPECT(!clock.synchronize(1, static_cast<OledClockFormat>(9), 0, 0, true));
    EXPECT(!clock.synchronize(1, OledClockFormat::hour_24, 0, 0, false));
    EXPECT(!clock.synchronize(1, OledClockFormat::hour_24, 2, 1, true));
    EXPECT(!clock.synchronize(1, OledClockFormat::hour_24, 0, 2, true));
    EXPECT(!clock.observe(2).valid);
    EXPECT(clock.synchronize(10, OledClockFormat::hour_24, 2, 2, true));
    EXPECT(clock.observe(3).valid);
    EXPECT(!clock.synchronize(20, OledClockFormat::hour_24, 2, 4, true));
    EXPECT(!clock.observe(4).valid);
    OledClock expired;
    EXPECT(!expired.synchronize(0, OledClockFormat::hour_24, 0,
        OledClock::max_sync_age_ms, true));
}

void rollback_invalidates_until_fresh_sync() {
    OledClock clock;
    EXPECT(clock.synchronize(70'000, OledClockFormat::hour_24, 100, 100, true));
    EXPECT(clock.observe(200).valid);
    EXPECT(!clock.observe(199).valid);
    EXPECT(!clock.observe(200).valid);
    EXPECT(!clock.synchronize(0, OledClockFormat::hour_24, 199, 199, true));
    // Civil time may move backwards; the monotonic source may not.
    EXPECT(clock.synchronize(0, OledClockFormat::hour_24, 200, 200, true));
    EXPECT(std::strcmp(clock.observe(200).text.data(), "00:00") == 0);
}

void integer_limits_and_fixed_buffer() {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    OledClock clock;
    EXPECT(clock.synchronize(86'399, OledClockFormat::hour_12,
        maximum - 1'000, maximum - 1'000, true));
    const auto reading = clock.observe(maximum);
    EXPECT(reading.valid);
    EXPECT(reading.local_second_of_day == 0);
    EXPECT(reading.text.size() == 9);
    EXPECT(reading.text.back() == '\0');
    EXPECT(std::strcmp(reading.text.data(), "12:00 AM") == 0);
    EXPECT(!clock.observe(0).valid);
    EXPECT(!clock.synchronize(std::numeric_limits<std::uint32_t>::max(),
        OledClockFormat::hour_24, maximum, maximum, true));
    OledClock huge_age;
    EXPECT(!huge_age.synchronize(0, OledClockFormat::hour_24, 0, maximum, true));
}
}  // namespace

int main() {
    unknown_and_midnight_noon();
    rollover_and_disconnected_progress();
    exact_staleness_and_resync();
    invalid_sync_and_timestamp_ordering();
    rollback_invalidates_until_fresh_sync();
    integer_limits_and_fixed_buffer();
    if (failures != 0) return 1;
    std::cout << "PASS OLED clock: 6 groups\n";
    return 0;
}
