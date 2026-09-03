#pragma once

#include <array>
#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"
#include "opentrail/companion_pairing_window.hpp"
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
    factory_reset_confirmation,
    factory_resetting,
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

// Ephemeral render-only view. The owner never retains this object or exposes
// its digits through StartupDisplayStatus.
struct PairingPinDisplayView {
    std::array<char, 6> digits{};
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
    [[nodiscard]] virtual bool render_pairing_pin(
        const PairingPinDisplayView& view) = 0;
    // Emergency best-effort concealment that remains callable after an
    // ordinary render failure. Implementations must not depend on the normal
    // display-owner availability latch.
    [[nodiscard]] virtual bool conceal() = 0;
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
    [[nodiscard]] bool show_pairing_pin(const std::array<char, 6>& digits);
    [[nodiscard]] bool clear_pairing_pin();
    // The confirmation and in-progress pages are transient full-screen
    // overlays. Ordinary status updates continue to replace the retained
    // normal view without redrawing over either overlay. Cancellation restores
    // that latest normal view; the in-progress page cannot be cancelled.
    [[nodiscard]] bool show_factory_reset_confirmation();
    [[nodiscard]] bool clear_factory_reset_confirmation();
    [[nodiscard]] bool show_factory_reset_in_progress();
    [[nodiscard]] StartupDisplayStatus status() const { return status_; }

private:
    enum class FactoryResetOverlay : std::uint8_t {
        none = 0,
        confirmation,
        resetting,
    };

    [[nodiscard]] bool show_view(const StartupDisplayView& view);

    StartupDisplayPort& port_;
    StartupDisplayStatus status_{};
    StartupDisplayView view_{};
    bool started_{false};
    bool has_view_{false};
    bool pairing_pin_visible_{false};
    FactoryResetOverlay factory_reset_overlay_{FactoryResetOverlay::none};
};

// Narrow target adapter for the pairing-window component. It retains no PIN;
// StartupDisplayOwner forwards digits directly to the physical display.
class PairingPinDisplayPortAdapter final
    : public companion::CompanionPairingPinDisplayPort {
public:
    explicit PairingPinDisplayPortAdapter(StartupDisplayOwner& display)
        : display_(display) {}

    [[nodiscard]] bool show_pairing_pin(
        const std::array<char, 6>& digits) override;
    [[nodiscard]] bool clear_pairing_pin() override;

private:
    StartupDisplayOwner& display_;
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
