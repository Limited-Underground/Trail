#include "heltec_v4_oled.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "trail_startup_logo.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr char kLogTag[] = "ot_display";
constexpr std::size_t kDisplayWidth = 128;
constexpr std::size_t kDisplayHeight = 64;
constexpr std::size_t kStatusPage = 7;
constexpr std::size_t kGlyphWidth = 5;
constexpr std::size_t kGlyphAdvance = 6;
constexpr std::size_t kPairingLabelY = 5;
constexpr std::size_t kPairingDigitsY = 25;
constexpr std::size_t kPairingDigitsScale = 2;

std::array<std::uint8_t, kGlyphWidth> glyph_for(char value) {
    switch (value) {
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x7F, 0x20, 0x18, 0x20, 0x7F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x03, 0x04, 0x78, 0x04, 0x03};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        default: return {0, 0, 0, 0, 0};
    }
}

void set_pixel(std::array<std::uint8_t, kTrailStartupLogoBytes>& frame,
               std::size_t x,
               std::size_t y) {
    if (x >= kDisplayWidth || y >= kDisplayHeight) return;
    frame[(y / 8) * kDisplayWidth + x] |=
        static_cast<std::uint8_t>(1U << (y % 8));
}

void draw_scaled_text(
    std::array<std::uint8_t, kTrailStartupLogoBytes>& frame,
    const char* text,
    std::size_t length,
    std::size_t y,
    std::size_t scale) {
    if (length == 0 || scale == 0) return;
    const auto pixel_width = length * kGlyphAdvance * scale - scale;
    const auto start_x = pixel_width < kDisplayWidth
        ? (kDisplayWidth - pixel_width) / 2
        : 0;
    for (std::size_t index = 0; index < length; ++index) {
        const auto glyph = glyph_for(text[index]);
        const auto glyph_x = start_x + index * kGlyphAdvance * scale;
        for (std::size_t column = 0; column < glyph.size(); ++column) {
            for (std::size_t row = 0; row < 7; ++row) {
                if ((glyph[column] & (1U << row)) == 0) continue;
                for (std::size_t dx = 0; dx < scale; ++dx) {
                    for (std::size_t dy = 0; dy < scale; ++dy) {
                        set_pixel(frame, glyph_x + column * scale + dx,
                                  y + row * scale + dy);
                    }
                }
            }
        }
    }
}

bool pairing_digits_valid(const std::array<char, 6>& digits) {
    return std::all_of(digits.begin(), digits.end(), [](char digit) {
        return digit >= '0' && digit <= '9';
    });
}

void draw_pairing_page(
    std::array<std::uint8_t, kTrailStartupLogoBytes>& pixels,
    const PairingPinDisplayView& view) {
    pixels.fill(0);
    constexpr char kPairingLabel[] = "PAIR";
    draw_scaled_text(pixels, kPairingLabel, 4, kPairingLabelY, 1);
    draw_scaled_text(pixels, view.digits.data(), view.digits.size(),
                     kPairingDigitsY, kPairingDigitsScale);
    if (view.has_footer) {
        std::copy(view.footer.columns.begin(), view.footer.columns.end(),
                  pixels.begin() + kStatusPage * kDisplayWidth);
    }
}

void draw_status_text(std::array<std::uint8_t, kTrailStartupLogoBytes>& frame,
                      const char* text) {
    std::fill(frame.begin() + kStatusPage * kDisplayWidth, frame.end(), 0);
    const auto length = std::strlen(text);
    if (length == 0) return;
    const auto pixel_width = length * kGlyphAdvance - 1;
    const auto start_x = pixel_width < kDisplayWidth
        ? (kDisplayWidth - pixel_width) / 2
        : 0;
    for (std::size_t index = 0; index < length; ++index) {
        const auto glyph = glyph_for(text[index]);
        const auto x = start_x + index * kGlyphAdvance;
        for (std::size_t column = 0;
             column < glyph.size() && x + column < kDisplayWidth; ++column) {
            frame[kStatusPage * kDisplayWidth + x + column] = glyph[column];
        }
    }
}


void draw_frame_footer(
    std::array<std::uint8_t, kTrailStartupLogoBytes>& pixels,
    const StartupDisplayView& view) {
    if (view.has_footer) {
        std::copy(
            view.footer.columns.begin(),
            view.footer.columns.end(),
            pixels.begin() + kStatusPage * kDisplayWidth);
        return;
    }
    draw_status_text(pixels, startup_display_text(view.frame));
}

}  // namespace
bool HeltecV4Oled::record_failure(const char* step, int error_code) {
    ESP_LOGW(kLogTag, "display unavailable step=%s code=%d", step, error_code);
    initialized_ = false;
    return false;
}

bool HeltecV4Oled::initialize() {
    if (attempted_) return initialized_;
    attempted_ = true;

    gpio_config_t power_config{};
    power_config.pin_bit_mask = 1ULL << kHeltecV4VextControlGpio;
    power_config.mode = GPIO_MODE_OUTPUT;
    auto result = gpio_config(&power_config);
    if (result != ESP_OK) return record_failure("vext-config", result);
    result = gpio_set_level(
        static_cast<gpio_num_t>(kHeltecV4VextControlGpio),
        kHeltecV4VextEnableLevel);
    if (result != ESP_OK) return record_failure("vext-enable", result);
    vTaskDelay(pdMS_TO_TICKS(20));

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = -1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(kHeltecV4OledSdaGpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(kHeltecV4OledSclGpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    result = i2c_new_master_bus(&bus_config, &bus_);
    if (result != ESP_OK) return record_failure("i2c-bus", result);

    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = kHeltecV4OledAddress;
    io_config.scl_speed_hz = kHeltecV4OledClockHz;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 6;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    result = esp_lcd_new_panel_io_i2c(bus_, &io_config, &io_);
    if (result != ESP_OK) return record_failure("panel-io", result);

    esp_lcd_panel_ssd1306_config_t vendor_config{};
    vendor_config.height = kDisplayHeight;
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.bits_per_pixel = 1;
    panel_config.reset_gpio_num =
        static_cast<gpio_num_t>(kHeltecV4OledResetGpio);
    panel_config.vendor_config = &vendor_config;
    panel_config.flags.reset_active_high = false;
    result = esp_lcd_new_panel_ssd1306(io_, &panel_config, &panel_);
    if (result != ESP_OK) return record_failure("panel-create", result);
    if ((result = esp_lcd_panel_reset(panel_)) != ESP_OK) {
        return record_failure("panel-reset", result);
    }
    if ((result = esp_lcd_panel_init(panel_)) != ESP_OK) {
        return record_failure("panel-init", result);
    }
    if ((result = esp_lcd_panel_mirror(panel_, true, true)) != ESP_OK) {
        return record_failure("panel-orientation", result);
    }
    if ((result = esp_lcd_panel_invert_color(panel_, false)) != ESP_OK) {
        return record_failure("panel-color", result);
    }
    if ((result = esp_lcd_panel_disp_on_off(panel_, true)) != ESP_OK) {
        return record_failure("panel-on", result);
    }
    initialized_ = true;
    return true;
}

bool HeltecV4Oled::render(const StartupDisplayView& view) {
    if (!initialized_ || panel_ == nullptr) return false;
    auto pixels = kTrailStartupLogoSsd1306;
    draw_frame_footer(pixels, view);
    const auto result = esp_lcd_panel_draw_bitmap(
        panel_, 0, 0, kDisplayWidth, kDisplayHeight, pixels.data());
    return result == ESP_OK || record_failure("panel-draw", result);
}

bool HeltecV4Oled::render_pairing_pin(const PairingPinDisplayView& view) {
    if (!initialized_ || panel_ == nullptr ||
        !pairing_digits_valid(view.digits)) {
        return false;
    }
    std::array<std::uint8_t, kTrailStartupLogoBytes> pixels{};
    draw_pairing_page(pixels, view);
    const auto result = esp_lcd_panel_draw_bitmap(
        panel_, 0, 0, kDisplayWidth, kDisplayHeight, pixels.data());
    pixels.fill(0);
    return result == ESP_OK || record_failure("pairing-draw", result);
}

bool HeltecV4Oled::conceal() {
    bool concealed = false;
    if (panel_ != nullptr) {
        std::array<std::uint8_t, kTrailStartupLogoBytes> blank{};
        if (esp_lcd_panel_draw_bitmap(
                panel_, 0, 0, kDisplayWidth, kDisplayHeight,
                blank.data()) == ESP_OK) {
            concealed = true;
        }
        blank.fill(0);
        if (esp_lcd_panel_disp_on_off(panel_, false) == ESP_OK) {
            concealed = true;
        }
    }
    if (gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4VextControlGpio),
            1 - kHeltecV4VextEnableLevel) == ESP_OK) {
        concealed = true;
    }
    initialized_ = false;
    return concealed;
}

}  // namespace opentrail::target::heltec_v4_bench
