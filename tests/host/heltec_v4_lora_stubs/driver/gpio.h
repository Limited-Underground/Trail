#pragma once
#include <string>
#include "RadioLib.h"
using gpio_num_t = int;
constexpr int ESP_OK = 0;
constexpr int GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0;
struct gpio_config_t { std::uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
inline int gpio_config(const gpio_config_t*) { return radio_mock::op("gpio_config"); }
inline int gpio_set_level(gpio_num_t pin, int level) {
    radio_mock::gpio[static_cast<std::size_t>(pin)] = level;
    return radio_mock::op("gpio_" + std::to_string(pin));
}
inline int gpio_get_level(gpio_num_t pin) { return pin == 14 ? radio_mock::dio1() : 0; }
