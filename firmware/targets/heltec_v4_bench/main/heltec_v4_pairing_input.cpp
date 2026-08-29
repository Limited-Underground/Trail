#include "heltec_v4_pairing_input.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr gpio_num_t kPairingButton =
    static_cast<gpio_num_t>(kHeltecV4PairingButtonGpio);
static_assert(kHeltecV4PairingButtonGpio == GPIO_NUM_0);

[[nodiscard]] bool pairing_button_pressed() {
    return gpio_get_level(kPairingButton) ==
           kHeltecV4PairingButtonPressedLevel;
}

}  // namespace

void PairingInputGesture::reset(bool raw_pressed, std::uint64_t now_ms) {
    raw_pressed_ = raw_pressed;
    stable_pressed_ = false;
    stable_known_ = false;
    armed_ = false;
    hold_in_progress_ = false;
    raw_since_ms_ = now_ms;
    press_started_ms_ = 0;
    last_observed_ms_ = now_ms;
}

PairingInputEvent PairingInputGesture::observe(
    bool raw_pressed,
    std::uint64_t now_ms) {
    if (now_ms < last_observed_ms_) {
        reset(raw_pressed, now_ms);
        return PairingInputEvent::none;
    }
    last_observed_ms_ = now_ms;

    if (raw_pressed != raw_pressed_) {
        raw_pressed_ = raw_pressed;
        raw_since_ms_ = now_ms;
        return PairingInputEvent::none;
    }

    if (now_ms - raw_since_ms_ < kHeltecV4PairingButtonDebounceMs) {
        return PairingInputEvent::none;
    }

    if (stable_known_ && stable_pressed_ == raw_pressed_) {
        return PairingInputEvent::none;
    }

    stable_known_ = true;
    stable_pressed_ = raw_pressed_;
    if (stable_pressed_) {
        if (armed_) {
            hold_in_progress_ = true;
            press_started_ms_ = raw_since_ms_;
        }
        return PairingInputEvent::none;
    }

    if (!armed_) {
        armed_ = true;
        hold_in_progress_ = false;
        return PairingInputEvent::none;
    }

    if (!hold_in_progress_) {
        return PairingInputEvent::none;
    }

    hold_in_progress_ = false;
    const auto held_ms = raw_since_ms_ - press_started_ms_;
    if (held_ms < kHeltecV4PairingButtonHoldMs) {
        return PairingInputEvent::none;
    }
    return PairingInputEvent::long_press_released;
}

bool HeltecV4PairingInput::initialize(std::uint64_t now_ms) {
    if (initialized_) {
        return true;
    }

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kPairingButton;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }

    gesture_.reset(pairing_button_pressed(), now_ms);
    initialized_ = true;
    return true;
}

PairingInputEvent HeltecV4PairingInput::poll(std::uint64_t now_ms) {
    if (!initialized_) {
        return PairingInputEvent::none;
    }
    return gesture_.observe(pairing_button_pressed(), now_ms);
}

}  // namespace opentrail::target::heltec_v4_bench
