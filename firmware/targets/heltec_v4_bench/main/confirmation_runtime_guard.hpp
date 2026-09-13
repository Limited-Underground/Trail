#pragma once
#include "opentrail/companion_device_name_owner.hpp"
#include <atomic>

namespace opentrail::target::heltec_v4_bench {
// Callback-safe revocation only. Cleanup remains on the application owner.
class ConfirmationRuntimeGuard final {
public:
    void revoke() noexcept { revoked_.store(true, std::memory_order_release); }
    [[nodiscard]] bool current(bool overflow, bool host_exited, bool orphan) noexcept {
        if (overflow || host_exited || orphan) revoke();
        return !revoked_.load(std::memory_order_acquire);
    }
private:
    std::atomic<bool> revoked_{false};
};

class ConfirmationReadySource final : public companion::DeviceNameAuthoritySource {
public:
    using Probe = bool (*)();
    ConfirmationReadySource(companion::DeviceNameAuthoritySource& live, Probe ready, Probe current)
        : live_(live), ready_(ready), current_(current) {}
    companion::DeviceNameAuthority current() noexcept override {
        if (!current_ || !ready_ || !current_()) return {};
        auto result = live_.current();
        if (!current_()) return {};
        if (result.phase == companion::DeviceNamePhase::connected || result.phase == companion::DeviceNamePhase::ready)
            result.phase = ready_() ? companion::DeviceNamePhase::ready : companion::DeviceNamePhase::unavailable;
        if (!current_()) return {};
        return result;
    }
private:
    companion::DeviceNameAuthoritySource& live_;
    Probe ready_, current_;
};
} // namespace opentrail::target::heltec_v4_bench
