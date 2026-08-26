#include <cassert>
#include <cstdint>

#include "../../firmware/targets/heltec_v4_bench/main/heltec_v4_battery.hpp"

using opentrail::heltec_v4::battery_reading_from_millivolts;

int main() {
  const auto absent = battery_reading_from_millivolts(0);
  assert(!absent.valid);
  assert(absent.millivolts == 0);
  assert(absent.percent == 0);

  const auto below_electrical_range = battery_reading_from_millivolts(2799);
  assert(!below_electrical_range.valid);
  const auto above_electrical_range = battery_reading_from_millivolts(4401);
  assert(!above_electrical_range.valid);

  const auto low = battery_reading_from_millivolts(2800);
  assert(low.valid);
  assert(low.millivolts == 2800);
  assert(low.percent == 0);

  const auto empty = battery_reading_from_millivolts(3300);
  assert(empty.valid);
  assert(empty.percent == 0);

  const auto midpoint = battery_reading_from_millivolts(3750);
  assert(midpoint.valid);
  assert(midpoint.millivolts == 3750);
  assert(midpoint.percent == 50);

  const auto rounded = battery_reading_from_millivolts(3755);
  assert(rounded.valid);
  assert(rounded.percent == 51);

  const auto full = battery_reading_from_millivolts(4200);
  assert(full.valid);
  assert(full.percent == 100);

  const auto high = battery_reading_from_millivolts(4400);
  assert(high.valid);
  assert(high.millivolts == 4400);
  assert(high.percent == 100);

  return 0;
}
