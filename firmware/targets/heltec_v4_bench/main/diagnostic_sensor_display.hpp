#pragma once
#include "companion_connection_diagnostics.hpp"
#include "heltec_startup_display.hpp"

namespace opentrail::target::heltec_v4_bench {
// Diagnostic B changes only the copied display input. Acquisition, raw status,
// BLE admission, phone-ready indication, clock and security remain intact.
inline CompactStatusSnapshot diagnostic_sensor_display(CompactStatusSnapshot value) {
#if OPENTRAIL_CONNECTION_DIAGNOSTICS && !OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY
    value.battery_percent = {};
    value.gps_satellites = {};
    value.gps_fix = ui::compact_status_footer::GpsFixCode::unavailable;
#endif
    return value;
}
}  // namespace opentrail::target::heltec_v4_bench
