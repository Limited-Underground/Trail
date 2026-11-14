#pragma once
#include "RadioLib.h"
inline std::int64_t esp_timer_get_time() { return radio_mock::now_us; }
