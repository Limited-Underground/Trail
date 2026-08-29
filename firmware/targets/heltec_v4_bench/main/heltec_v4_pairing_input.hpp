#pragma once

#include <cstdint>

namespace opentrail::target::heltec_v4_bench {

inline constexpr int kHeltecV4PairingButtonGpio = 0;
inline constexpr int kHeltecV4PairingButtonPressedLevel = 0;
inline constexpr std::uint64_t kHeltecV4PairingButtonDebounceMs = 40;
inline constexpr std::uint64_t kHeltecV4PairingButtonHoldMs = 3'000;

enum class PairingInputEvent : std::uint8_t {
    none = 0,
    long_press_released,
};

// Deterministic, allocation-free gesture recognizer. It does not arm until a
// release has remained stable after boot, so a button held while resetting the
// ESP32-S3 cannot open a pairing window. A clock regression also fails closed
// by returning to that boot-release state.
class PairingInputGesture {
public:
    void reset(bool raw_pressed, std::uint64_t now_ms);

    [[nodiscard]] PairingInputEvent observe(
        bool raw_pressed,
        std::uint64_t now_ms);

    [[nodiscard]] bool armed() const { return armed_; }

private:
    bool raw_pressed_{false};
    bool stable_pressed_{false};
    bool stable_known_{false};
    bool armed_{false};
    bool hold_in_progress_{false};
    std::uint64_t raw_since_ms_{0};
    std::uint64_t press_started_ms_{0};
    std::uint64_t last_observed_ms_{0};
};

// Thin polling adapter for the active-low BOOT/USER key on GPIO0. GPIO0 is an
// ESP32-S3 strapping pin, so callers must never treat a reset-time press as an
// application gesture. No interrupt or task is created; app_main owns polling.
class HeltecV4PairingInput {
public:
    [[nodiscard]] bool initialize(std::uint64_t now_ms);
    [[nodiscard]] PairingInputEvent poll(std::uint64_t now_ms);

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] bool armed() const { return gesture_.armed(); }

private:
    PairingInputGesture gesture_{};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
