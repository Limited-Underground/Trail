#pragma once
#include "esp_stub.hpp"
// The actual app and OLED owner call this task-local scheduler seam. Delay
// callbacks are counted without claiming physical timing; no product source
// is changed. Bailout requires completed app input/output, never startup work.
void ot237_app_tick(std::uint32_t);
#define vTaskDelay ot237_app_tick
