#pragma once

#include <cstdint>

#include "opentrail/companion_factory_reset_gesture.hpp"

namespace opentrail::target::heltec_v4_bench {

inline constexpr int kHeltecV4FactoryResetButtonGpio = 0;
inline constexpr int kHeltecV4FactoryResetButtonPressedLevel = 0;

// Thin target adapter for the active-low BOOT/USER key. It owns GPIO
// configuration and sampling only; the target-neutral gesture owns debounce
// and timing. It cannot open pairing or mutate storage.
class HeltecV4FactoryResetInput {
public:
    [[nodiscard]] bool initialize(std::uint64_t now_ms);
    [[nodiscard]] companion::CompanionFactoryResetGestureEvent poll(
        std::uint64_t now_ms);
    [[nodiscard]] companion::CompanionFactoryResetGestureEvent cancel(
        std::uint64_t now_ms);
    [[nodiscard]] bool rearm_after_noncommit(std::uint64_t now_ms);
    [[nodiscard]] companion::CompanionFactoryResetGestureStatus status() const {
        return gesture_.status();
    }

    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    companion::CompanionFactoryResetGesture gesture_{};
    bool initialization_attempted_{false};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
