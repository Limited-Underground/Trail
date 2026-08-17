#include "heltec_startup_display.hpp"

namespace opentrail::target::heltec_v4_bench {

bool StartupDisplayOwner::start() {
    if (started_) {
        return status_.available;
    }
    started_ = true;
    if (!port_.initialize() || !port_.render(StartupDisplayFrame::logo)) {
        status_.available = false;
        return false;
    }
    status_.available = true;
    status_.frame = StartupDisplayFrame::logo;
    status_.render_count = 1;
    return true;
}

bool StartupDisplayOwner::show(StartupDisplayFrame frame) {
    if (!started_ || !status_.available) {
        return false;
    }
    if (status_.frame == frame) {
        return true;
    }
    if (!port_.render(frame)) {
        status_.available = false;
        return false;
    }
    status_.frame = frame;
    ++status_.render_count;
    return true;
}

StartupDisplayFrame startup_display_frame_for_ble_phase(
    companion::CompanionBleRuntimePhase phase) {
    using companion::CompanionBleRuntimePhase;
    switch (phase) {
        case CompanionBleRuntimePhase::dormant:
        case CompanionBleRuntimePhase::waiting_for_host_sync:
            return StartupDisplayFrame::ble_starting;
        case CompanionBleRuntimePhase::advertising:
            return StartupDisplayFrame::ble_advertising;
        case CompanionBleRuntimePhase::connected:
            return StartupDisplayFrame::ble_connected;
        case CompanionBleRuntimePhase::restart_wait:
            return StartupDisplayFrame::ble_retrying;
        case CompanionBleRuntimePhase::contained:
            return StartupDisplayFrame::ble_error;
    }
    return StartupDisplayFrame::ble_error;
}

const char* startup_display_text(StartupDisplayFrame frame) {
    switch (frame) {
        case StartupDisplayFrame::logo:
            return "";
        case StartupDisplayFrame::self_check_failed:
            return "SELF CHECK FAIL";
        case StartupDisplayFrame::ble_starting:
            return "BLE STARTING";
        case StartupDisplayFrame::ble_advertising:
            return "BLE ADVERTISING";
        case StartupDisplayFrame::ble_connected:
            return "BLE CONNECTED";
        case StartupDisplayFrame::ble_retrying:
            return "BLE RETRYING";
        case StartupDisplayFrame::ble_error:
            return "BLE ERROR";
    }
    return "BLE ERROR";
}

}  // namespace opentrail::target::heltec_v4_bench
