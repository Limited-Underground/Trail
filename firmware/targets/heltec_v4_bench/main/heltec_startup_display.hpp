#pragma once

#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"
#include "opentrail/compact_status_footer.hpp"

namespace opentrail::target::heltec_v4_bench {

enum class StartupDisplayFrame : std::uint8_t {
    logo,
    self_check_failed,
    ble_starting,
    ble_advertising,
    ble_connected,
    ble_retrying,
    ble_error,
};

struct StartupDisplayStatus {
    bool available{false};
    StartupDisplayFrame frame{StartupDisplayFrame::logo};
    std::uint32_t render_count{0};
};

struct StartupDisplayView {
    StartupDisplayFrame frame{StartupDisplayFrame::logo};
    bool has_footer{false};
    ui::compact_status_footer::Page footer{};
};

// Current target observations used to build the fixed compact footer. The BLE
// observation remains the StartupDisplayFrame argument so the visible frame,
// footer code, and runtime-owner phase cannot disagree.
struct CompactStatusSnapshot {
    ui::compact_status_footer::Metric battery_percent{};
    ui::compact_status_footer::Metric gps_satellites{};
    ui::compact_status_footer::Freshness freshness{};
    bool activity_supported{false};
    ui::compact_status_footer::Direction activity{
        ui::compact_status_footer::Direction::none};
    std::uint64_t activity_at_ms{0};
    std::uint64_t activity_visible_for_ms{0};
    std::uint64_t render_now_ms{0};
};

class StartupDisplayPort {
public:
    virtual ~StartupDisplayPort() = default;
    [[nodiscard]] virtual bool initialize() = 0;
    [[nodiscard]] virtual bool render(const StartupDisplayView& view) = 0;
};

// Best-effort owner for the small target-local startup/status display. A
// display failure is latched unavailable and never controls BLE or heartbeat
// authority.
class StartupDisplayOwner {
public:
    explicit StartupDisplayOwner(StartupDisplayPort& port) : port_(port) {}

    [[nodiscard]] bool start();
    [[nodiscard]] bool show(StartupDisplayFrame frame);
    [[nodiscard]] bool show_compact_status(
        StartupDisplayFrame frame,
        const CompactStatusSnapshot& snapshot);
    [[nodiscard]] bool show_footer(
        StartupDisplayFrame frame,
        const ui::compact_status_footer::Page& footer);
    [[nodiscard]] StartupDisplayStatus status() const { return status_; }

private:
    [[nodiscard]] bool show_view(const StartupDisplayView& view);

    StartupDisplayPort& port_;
    StartupDisplayStatus status_{};
    StartupDisplayView view_{};
    bool started_{false};
    bool has_view_{false};
};

[[nodiscard]] StartupDisplayFrame startup_display_frame_for_ble_phase(
    companion::CompanionBleRuntimePhase phase);

// Compatibility path for callers that have no live observations yet. Battery,
// GPS, and activity remain fail-closed placeholders.
[[nodiscard]] bool startup_display_compact_footer_page(
    StartupDisplayFrame frame,
    ui::compact_status_footer::Page& page);

// Produces the compact page for BLE frames only from caller-owned current
// observations. Invalid, stale, future, out-of-range, or unsupported inputs
// retain the compact footer's fail-closed placeholders.
[[nodiscard]] bool startup_display_compact_footer_page(
    StartupDisplayFrame frame,
    const CompactStatusSnapshot& snapshot,
    ui::compact_status_footer::Page& page);


[[nodiscard]] const char* startup_display_text(StartupDisplayFrame frame);

}  // namespace opentrail::target::heltec_v4_bench
