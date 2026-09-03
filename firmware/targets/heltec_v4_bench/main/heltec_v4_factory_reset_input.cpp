#include "heltec_v4_factory_reset_input.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr gpio_num_t kFactoryResetButton =
    static_cast<gpio_num_t>(kHeltecV4FactoryResetButtonGpio);
static_assert(kHeltecV4FactoryResetButtonGpio == GPIO_NUM_0);

[[nodiscard]] bool factory_reset_button_pressed() {
    return gpio_get_level(kFactoryResetButton) ==
           kHeltecV4FactoryResetButtonPressedLevel;
}

}  // namespace

bool HeltecV4FactoryResetInput::initialize(std::uint64_t now_ms) {
    if (initialization_attempted_) {
        return initialized_;
    }
    initialization_attempted_ = true;

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kFactoryResetButton;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }

    gesture_.reset(factory_reset_button_pressed(), now_ms);
    initialized_ = true;
    return true;
}

companion::CompanionFactoryResetGestureEvent
HeltecV4FactoryResetInput::poll(std::uint64_t now_ms) {
    if (!initialized_) {
        return companion::CompanionFactoryResetGestureEvent::none;
    }
    return gesture_.observe(factory_reset_button_pressed(), now_ms);
}

companion::CompanionFactoryResetGestureEvent
HeltecV4FactoryResetInput::cancel(std::uint64_t now_ms) {
    if (!initialized_) {
        return companion::CompanionFactoryResetGestureEvent::none;
    }
    return gesture_.cancel(factory_reset_button_pressed(), now_ms);
}

bool HeltecV4FactoryResetInput::rearm_after_noncommit(
    std::uint64_t now_ms) {
    return initialized_ && gesture_.rearm_after_noncommit(
                               factory_reset_button_pressed(), now_ms);
}

}  // namespace opentrail::target::heltec_v4_bench
