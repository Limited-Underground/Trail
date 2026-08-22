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
    StartupDisplayView view{};
    view.frame = frame;
    view.has_footer = startup_display_compact_footer_page(frame, view.footer);
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

bool StartupDisplayOwner::show_view(const StartupDisplayView& view) {
    if (!started_ || !status_.available) {
        return false;
    }
    if (has_view_ && view_.frame == view.frame &&
        view_.has_footer == view.has_footer &&
        (!view.has_footer || view_.footer.columns == view.footer.columns)) {
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
    using ui::compact_status_footer::ActivityOwner;
    using ui::compact_status_footer::BleCode;
    using ui::compact_status_footer::Freshness;
    using ui::compact_status_footer::Snapshot;

    Snapshot snapshot{};
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
        default:
            page = {};
            return false;
    }

    constexpr std::uint64_t kRenderNowMs = 0;
    const auto fields = ui::compact_status_footer::format(
        snapshot, Freshness{0, 0}, kRenderNowMs);
    const ActivityOwner unsupported_activity{false, 0};
    page = ui::compact_status_footer::render(
        fields, unsupported_activity, kRenderNowMs);
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
    }
    return "BLE ERROR";
}

}  // namespace opentrail::target::heltec_v4_bench
