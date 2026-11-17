#include "heltec_oled_presentation.hpp"
#include "lora_link_test.hpp"
#include "opentrail/companion_region_catalog.hpp"

namespace opentrail::target::heltec_v4_bench {

ui::oled_presentation::Frame HeltecOledPresentation::present(
    const StartupDisplayView& view, std::uint64_t now_ms,
    std::string_view name, time::OledClockReading clock, std::uint16_t region_selection,
    ui::oled_presentation::BoardPairing board_pairing) {
    using ui::oled_presentation::PhoneState;
    ui::oled_presentation::Snapshot snapshot{};
    snapshot.device_name = name;
    snapshot.clock = clock;
    snapshot.board_pairing = board_pairing;
    using ui::compact_status_footer::Direction;
    using ui::compact_status_footer::GpsFixCode;
    using ui::compact_status_footer::ObservationState;
    const auto& battery = view.battery_percent;
    if (battery.state == ObservationState::valid && battery.value <= 100 &&
        battery.sampled_at_ms <= now_ms && view.battery_fresh_for_ms != 0 &&
        now_ms - battery.sampled_at_ms < view.battery_fresh_for_ms) {
        snapshot.battery = {true, battery.value, battery.sampled_at_ms};
    }
    const auto& satellites = view.gps_satellites;
    const bool current = satellites.state == ObservationState::valid &&
        satellites.value <= 99 && satellites.sampled_at_ms <= now_ms &&
        view.gps_fresh_for_ms != 0 &&
        now_ms - satellites.sampled_at_ms < view.gps_fresh_for_ms;
    if (current && (view.gps_fix == GpsFixCode::valid ||
                    view.gps_fix == GpsFixCode::no_fix)) {
        snapshot.satellites = {true, satellites.value, satellites.sampled_at_ms};
        snapshot.gps_sampled_at_ms = satellites.sampled_at_ms;
        if (view.gps_fix == GpsFixCode::valid)
            snapshot.gps_fix = ui::oled_presentation::GpsFix::fix;
        else if (view.gps_fix == GpsFixCode::no_fix)
            snapshot.gps_fix = ui::oled_presentation::GpsFix::no_fix;
    } else if (view.gps_fix == GpsFixCode::stale &&
               satellites.state == ObservationState::stale &&
               satellites.sampled_at_ms != 0 &&
               satellites.sampled_at_ms <= now_ms &&
               view.gps_fresh_for_ms != 0 &&
               now_ms - satellites.sampled_at_ms >= view.gps_fresh_for_ms) {
        snapshot.gps_fix = ui::oled_presentation::GpsFix::stale;
    }
    const auto* label = companion::region_selection_label(region_selection);
    snapshot.region_configured = label != nullptr;
    snapshot.region_label = label == nullptr ? "" : label;
    // The link-test radio authorizes its fixed profile from this same selection.
    snapshot.radio_running = view.activity_supported;
    snapshot.tx_available = snapshot.radio_running && lora_link_test::transmit_allowed(region_selection);
    snapshot.tx = {view.activity == Direction::tx, view.activity_at_ms};
    snapshot.rx = {view.activity == Direction::rx, view.activity_at_ms};
    snapshot.activity_visible_for_ms = view.activity_visible_for_ms;
    // The typed Ready observation comes from exact current protected Snapshot
    // delivery. Footer pixels and a raw BLE connection cannot grant Ready.
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
        case StartupDisplayFrame::ble_connected:
            snapshot.phone = view.phone_ready ? PhoneState::ready : PhoneState::unknown;
            break;
        case StartupDisplayFrame::ble_starting:
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
