#pragma once

#include <cstdint>

namespace opentrail::heltec_v4 {

struct BatteryReading {
  bool valid;
  std::uint16_t millivolts;
  std::uint8_t percent;
};

// Converts calibrated battery-terminal millivolts to a deliberately approximate
// Li-ion percentage. This is voltage-derived and is not fuel-gauge accuracy.
inline BatteryReading battery_reading_from_millivolts(
    std::int32_t millivolts) {
  constexpr std::int32_t kMinimumElectricalMillivolts = 2800;
  constexpr std::int32_t kMaximumElectricalMillivolts = 4400;
  constexpr std::int32_t kEmptyMillivolts = 3300;
  constexpr std::int32_t kFullMillivolts = 4200;

  if (millivolts < kMinimumElectricalMillivolts ||
      millivolts > kMaximumElectricalMillivolts) {
    return {false, 0, 0};
  }

  const auto bounded = millivolts < kEmptyMillivolts
                           ? kEmptyMillivolts
                           : (millivolts > kFullMillivolts
                                  ? kFullMillivolts
                                  : millivolts);
  const auto percent =
      ((bounded - kEmptyMillivolts) * 100 +
       (kFullMillivolts - kEmptyMillivolts) / 2) /
      (kFullMillivolts - kEmptyMillivolts);
  return {true, static_cast<std::uint16_t>(millivolts),
          static_cast<std::uint8_t>(percent)};
}

// Initializes the ESP32-S3 ADC1 channel used by the original HTIT-WB32LAF V4.2.
// Safe to call more than once.
bool battery_init();

// Enables the switched divider only while sampling, averages calibrated ADC
// readings, applies the nominal 4.90 divider, and fails closed on any error.
BatteryReading battery_read();

}  // namespace opentrail::heltec_v4
