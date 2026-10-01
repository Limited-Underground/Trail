#include "heltec_v4_factory_reset_input.hpp"
#include "driver/gpio.h"
#include "esp_err.h"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr gpio_num_t kFactoryResetButton =
    static_cast<gpio_num_t>(kHeltecV4FactoryResetButtonGpio);
static_assert(kHeltecV4FactoryResetButtonGpio == GPIO_NUM_0);
}
bool HeltecV4FactoryResetInput::initialize() {
    if (initialization_attempted_) return initialized_;
    initialization_attempted_ = true;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kFactoryResetButton;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    initialized_ = gpio_config(&config) == ESP_OK;
    return initialized_;
}
bool HeltecV4FactoryResetInput::sample(bool& pressed) {
    if (!initialized_) return false;
    const auto level = gpio_get_level(kFactoryResetButton);
    if (level != 0 && level != 1) return false;
    pressed = level == kHeltecV4FactoryResetButtonPressedLevel;
    return true;
}
} // namespace opentrail::target::heltec_v4_bench
