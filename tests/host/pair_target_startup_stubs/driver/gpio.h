#pragma once
#include <cstdint>
#define GPIO_NUM_0 0
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
struct gpio_config_t {std::uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;};
int gpio_config(const gpio_config_t*);
int gpio_get_level(int);
