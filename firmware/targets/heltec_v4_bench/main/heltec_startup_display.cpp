#include "heltec_startup_display.hpp"

namespace opentrail::target::heltec_v4_bench {

bool StartupDisplayOwner::start() {
    if (started_) {
        return status_.available;
    }
    started_ = true;
    const StartupDisplayView view{};
    if (!port_.initialize() || !port_.render(view)) {
        status_.available = false;
        return false;
    }
    status_.available = true;
    status_.frame = StartupDisplayFrame::logo;
    status_.render_count = 1;
    view_ = view;
    has_view_ = true;
    return true;
}

bool StartupDisplayOwner::show(StartupDisplayFrame frame) {
    if (frame == StartupDisplayFrame::factory_reset_confirmation ||
        frame == StartupDisplayFrame::factory_resetting) {
        return false;
    }
    StartupDisplayView view{};
    view.frame = frame;
    view.has_footer = startup_display_compact_footer_page(frame, view.footer);
    return show_view(view);
}

bool StartupDisplayOwner::show_compact_status(
    StartupDisplayFrame frame,
    const CompactStatusSnapshot& snapshot) {
    StartupDisplayView view{};
    view.frame = frame;
    if (!startup_display_compact_footer_page(frame, snapshot, view.footer)) {
        return false;
    }
    view.has_footer = true;
    return show_view(view);
}

bool StartupDisplayOwner::show_footer(
    StartupDisplayFrame frame,
    const ui::compact_status_footer::Page& footer) {
    ui::compact_status_footer::Page generated{};
    if (!startup_display_compact_footer_page(frame, generated)) {
        return false;
    }
    StartupDisplayView view{};
    view.frame = frame;
    view.has_footer = true;
    view.footer = footer;
    return show_view(view);
}

bool StartupDisplayOwner::show_pairing_pin(
    const std::array<char, 6>& digits) {
    if (!started_ || !status_.available ||
        factory_reset_overlay_ != FactoryResetOverlay::none) {
        return false;
    }
    for (const auto digit : digits) {
        if (digit < '0' || digit > '9') {
            return false;
        }
    }

    PairingPinDisplayView pairing_view{};
    pairing_view.digits = digits;
    if (has_view_ && view_.has_footer) {
        pairing_view.has_footer = true;
        pairing_view.footer = view_.footer;
    }
    const auto rendered = port_.render_pairing_pin(pairing_view);
    pairing_view.digits.fill('\0');
    if (!rendered) {
        (void)port_.conceal();
        pairing_pin_visible_ = false;
        status_.available = false;
        return false;
    }
    pairing_pin_visible_ = true;
    ++status_.render_count;
    return true;
}

bool StartupDisplayOwner::clear_pairing_pin() {
    if (!started_ || !status_.available) {
        return false;
    }
    if (!pairing_pin_visible_) {
        return true;
    }
    if (!has_view_ || !port_.render(view_)) {
        (void)port_.conceal();
        pairing_pin_visible_ = false;
        status_.available = false;
        return false;
    }
    pairing_pin_visible_ = false;
    ++status_.render_count;
    return true;
}

bool StartupDisplayOwner::show_factory_reset_confirmation() {
    if (!started_ || !status_.available || pairing_pin_visible_) {
        return false;
    }
    if (factory_reset_overlay_ == FactoryResetOverlay::confirmation) {
        return true;
    }
    if (factory_reset_overlay_ != FactoryResetOverlay::none) {
        return false;
    }

    StartupDisplayView overlay{};
    overlay.frame = StartupDisplayFrame::factory_reset_confirmation;
    if (!port_.render(overlay)) {
        (void)port_.conceal();
        factory_reset_overlay_ = FactoryResetOverlay::none;
        status_.available = false;
        return false;
    }
    factory_reset_overlay_ = FactoryResetOverlay::confirmation;
    ++status_.render_count;
    return true;
}

bool StartupDisplayOwner::clear_factory_reset_confirmation() {
    if (!started_ || !status_.available) {
        return false;
    }
    if (factory_reset_overlay_ == FactoryResetOverlay::none) {
        return true;
    }
    if (factory_reset_overlay_ != FactoryResetOverlay::none &&
        factory_reset_overlay_ != FactoryResetOverlay::confirmation) {
        return false;
    }
    if (!has_view_ || !port_.render(view_)) {
        (void)port_.conceal();
        factory_reset_overlay_ = FactoryResetOverlay::none;
        status_.available = false;
        return false;
    }
    factory_reset_overlay_ = FactoryResetOverlay::none;
    ++status_.render_count;
    return true;
}

bool StartupDisplayOwner::show_factory_reset_in_progress() {
    if (!started_ || !status_.available || pairing_pin_visible_) {
        return false;
    }
    if (factory_reset_overlay_ == FactoryResetOverlay::resetting) {
        return true;
    }
    if (factory_reset_overlay_ != FactoryResetOverlay::none &&
        factory_reset_overlay_ != FactoryResetOverlay::confirmation) {
        return false;
    }

    StartupDisplayView overlay{};
    overlay.frame = StartupDisplayFrame::factory_resetting;
    if (!port_.render(overlay)) {
        (void)port_.conceal();
        factory_reset_overlay_ = FactoryResetOverlay::none;
        status_.available = false;
        return false;
    }
    factory_reset_overlay_ = FactoryResetOverlay::resetting;
    ++status_.render_count;
    return true;
}

bool StartupDisplayOwner::show_view(const StartupDisplayView& view) {
    if (!started_ || !status_.available) {
        return false;
    }
    if (has_view_ && view_.frame == view.frame &&
        view_.has_footer == view.has_footer &&
        (!view.has_footer || view_.footer.columns == view.footer.columns)) {
        return true;
    }
    if (pairing_pin_visible_ ||
        factory_reset_overlay_ != FactoryResetOverlay::none) {
        view_ = view;
        has_view_ = true;
        status_.frame = view.frame;
        return true;
    }
    if (!port_.render(view)) {
        status_.available = false;
        return false;
    }
    view_ = view;
    has_view_ = true;
    status_.frame = view.frame;
    ++status_.render_count;
    return true;
}

bool PairingPinDisplayPortAdapter::show_pairing_pin(
    const std::array<char, 6>& digits) {
    return display_.show_pairing_pin(digits);
}

bool PairingPinDisplayPortAdapter::clear_pairing_pin() {
    return display_.clear_pairing_pin();
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

bool startup_display_compact_footer_page(
    StartupDisplayFrame frame,
    ui::compact_status_footer::Page& page) {
    return startup_display_compact_footer_page(
        frame, CompactStatusSnapshot{}, page);
}

bool startup_display_compact_footer_page(
    StartupDisplayFrame frame,
    const CompactStatusSnapshot& status,
    ui::compact_status_footer::Page& page) {
    using ui::compact_status_footer::ActivityOwner;
    using ui::compact_status_footer::BleCode;
    using ui::compact_status_footer::Snapshot;

    Snapshot snapshot{};
    snapshot.battery_percent = status.battery_percent;
    snapshot.gps_satellites = status.gps_satellites;
    switch (frame) {
        case StartupDisplayFrame::ble_starting:
            snapshot.ble = BleCode::starting;
            break;
        case StartupDisplayFrame::ble_advertising:
            snapshot.ble = BleCode::advertising;
            break;
        case StartupDisplayFrame::ble_connected:
            snapshot.ble = BleCode::connected;
            break;
        case StartupDisplayFrame::ble_retrying:
            snapshot.ble = BleCode::retrying;
            break;
        case StartupDisplayFrame::ble_error:
            snapshot.ble = BleCode::error;
            break;
        case StartupDisplayFrame::logo:
        case StartupDisplayFrame::self_check_failed:
        case StartupDisplayFrame::factory_reset_confirmation:
        case StartupDisplayFrame::factory_resetting:
        default:
            page = {};
            return false;
    }

    const auto fields = ui::compact_status_footer::format(
        snapshot, status.freshness, status.render_now_ms);
    ActivityOwner activity{
        status.activity_supported, status.activity_visible_for_ms};
    if (status.activity != ui::compact_status_footer::Direction::none) {
        (void)activity.observe_accepted_transport_event(
            status.activity, status.activity_at_ms);
    }
    page = ui::compact_status_footer::render(
        fields, activity, status.render_now_ms);
    return true;
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
        case StartupDisplayFrame::factory_reset_confirmation:
            return "ERASE ALL TRAIL DATA?";
        case StartupDisplayFrame::factory_resetting:
            return "RESETTING";
    }
    return "BLE ERROR";
}

}  // namespace opentrail::target::heltec_v4_bench
