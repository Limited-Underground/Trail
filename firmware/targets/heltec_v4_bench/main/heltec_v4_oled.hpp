#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "heltec_startup_display.hpp"

namespace opentrail::target::heltec_v4_bench {

inline constexpr int kHeltecV4OledSdaGpio = 17;
inline constexpr int kHeltecV4OledSclGpio = 18;
inline constexpr int kHeltecV4OledResetGpio = 21;
inline constexpr int kHeltecV4VextControlGpio = 36;
inline constexpr int kHeltecV4VextEnableLevel = 0;
inline constexpr std::uint8_t kHeltecV4OledAddress = 0x3C;
inline constexpr std::uint32_t kHeltecV4OledClockHz = 400000;

// Physically accepted OT-DEV-001 binding for the documented Heltec V4-family
// OLED interface. Exact received-board revision and controller die remain unresolved.
class HeltecV4Oled final : public StartupDisplayPort {
public:
    [[nodiscard]] bool initialize() override;
    [[nodiscard]] bool render(const StartupDisplayView& view) override;

private:
    [[nodiscard]] bool record_failure(const char* step, int error_code);

    i2c_master_bus_handle_t bus_{nullptr};
    esp_lcd_panel_io_handle_t io_{nullptr};
    esp_lcd_panel_handle_t panel_{nullptr};
    bool attempted_{false};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
