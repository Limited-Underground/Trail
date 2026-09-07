#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "heltec_startup_display.hpp"
#include "heltec_oled_presentation.hpp"

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
    [[nodiscard]] bool render_pairing_pin(
        const PairingPinDisplayView& view) override;
    [[nodiscard]] bool conceal() override;
    // App-owner task only; copies bounded readback metadata, never performs I/O.
    void set_configuration(std::string_view name, time::OledClockReading clock,
                           std::uint16_t region_selection = 0) {
        configuration_name_ = {};
        configuration_name_bytes_ = name.size() <= configuration_name_.size() ? name.size() : 0;
        for (std::size_t i = 0; i < configuration_name_bytes_; ++i) configuration_name_[i] = name[i];
        configuration_clock_ = clock;
        configuration_region_ = region_selection;
    }

private:
    [[nodiscard]] bool record_failure(const char* step, int error_code);
    [[nodiscard]] bool admit_display_time(std::uint64_t& now_ms);

    HeltecOledPresentation presentation_{};
    std::array<char, 96> configuration_name_{};
    std::size_t configuration_name_bytes_{0};
    time::OledClockReading configuration_clock_{};
    std::uint16_t configuration_region_{0};
    i2c_master_bus_handle_t bus_{nullptr};
    esp_lcd_panel_io_handle_t io_{nullptr};
    esp_lcd_panel_handle_t panel_{nullptr};
    bool time_observed_{false};
    std::uint64_t last_display_us_{0};
    bool attempted_{false};
    bool initialized_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
