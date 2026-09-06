#include "heltec_oled_presentation.hpp"

namespace opentrail::target::heltec_v4_bench {

ui::oled_presentation::Frame HeltecOledPresentation::present(
    const StartupDisplayView& view, std::uint64_t now_ms) {
    using ui::oled_presentation::PhoneState;
    ui::oled_presentation::Snapshot snapshot{};
    // No configured region or authenticated Ready observation is supplied by
    // StartupDisplayView. Its existing footer is raster data, not typed telemetry.
    switch (view.frame) {
        case StartupDisplayFrame::self_check_failed:
        case StartupDisplayFrame::ble_error:
            snapshot.failure = true;
            break;
        case StartupDisplayFrame::factory_reset_confirmation:
            snapshot.reset_confirmation = true;
            break;
        case StartupDisplayFrame::factory_resetting:
            snapshot.reset_in_progress = true;
            break;
        case StartupDisplayFrame::ble_starting:
        case StartupDisplayFrame::ble_connected:
            snapshot.phone = PhoneState::unknown;
            break;
        case StartupDisplayFrame::ble_retrying:
            snapshot.phone = PhoneState::reconnecting;
            break;
        case StartupDisplayFrame::ble_advertising:
            snapshot.phone = PhoneState::disconnected;
            break;
        case StartupDisplayFrame::logo:
        default:
            snapshot.failure = true;
            break;
    }
    return owner_.present(snapshot, now_ms);
}

}  // namespace opentrail::target::heltec_v4_bench
