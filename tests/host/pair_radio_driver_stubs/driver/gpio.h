#pragma once
#include "RadioLib.h"
using gpio_num_t = int;
constexpr int ESP_OK = 0;
constexpr int GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0;
struct gpio_config_t { std::uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
inline int gpio_config(const gpio_config_t* config) {
    if (config->pin_bit_mask != ((1ULL << 7) | (1ULL << 2) | (1ULL << 46)) ||
        config->mode != GPIO_MODE_OUTPUT || config->intr_type != GPIO_INTR_DISABLE) return -1;
    return radio_mock::op("gpio_config");
}
inline int gpio_set_level(gpio_num_t pin, int level) {
    const auto result = radio_mock::op("gpio_" + std::to_string(pin));
    if (!result) radio_mock::gpio[static_cast<std::size_t>(pin)] = level;
    return result;
}
inline int gpio_get_level(gpio_num_t pin) {
    (void)radio_mock::op("dio");
    return pin == 14 ? radio_mock::irq != 0 : radio_mock::gpio[static_cast<std::size_t>(pin)];
}
