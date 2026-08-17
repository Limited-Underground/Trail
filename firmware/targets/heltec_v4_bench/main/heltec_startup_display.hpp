#pragma once

#include <cstdint>

#include "opentrail/companion_ble_runtime_owner.hpp"

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

class StartupDisplayPort {
public:
    virtual ~StartupDisplayPort() = default;
    [[nodiscard]] virtual bool initialize() = 0;
    [[nodiscard]] virtual bool render(StartupDisplayFrame frame) = 0;
};

// Best-effort owner for the small target-local startup/status display. A
// display failure is latched unavailable and never controls BLE or heartbeat
// authority.
class StartupDisplayOwner {
public:
    explicit StartupDisplayOwner(StartupDisplayPort& port) : port_(port) {}

    [[nodiscard]] bool start();
    [[nodiscard]] bool show(StartupDisplayFrame frame);
    [[nodiscard]] StartupDisplayStatus status() const { return status_; }

private:
    StartupDisplayPort& port_;
    StartupDisplayStatus status_{};
    bool started_{false};
};

[[nodiscard]] StartupDisplayFrame startup_display_frame_for_ble_phase(
    companion::CompanionBleRuntimePhase phase);

[[nodiscard]] const char* startup_display_text(StartupDisplayFrame frame);

}  // namespace opentrail::target::heltec_v4_bench
