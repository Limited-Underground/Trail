#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using esp_err_t = int;
using gpio_num_t = int;
using i2c_master_bus_handle_t = void*;
using esp_lcd_panel_io_handle_t = void*;
using esp_lcd_panel_handle_t = void*;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_FAIL = -1;
inline constexpr int ESP_ERR_INVALID_STATE = 0x103;
inline constexpr int GPIO_MODE_OUTPUT = 1;
inline constexpr int I2C_CLK_SRC_DEFAULT = 0;
struct gpio_config_t { std::uint64_t pin_bit_mask{}; int mode{}; };
struct i2c_master_bus_config_t {
    int i2c_port{}, sda_io_num{}, scl_io_num{}, clk_source{}, glitch_ignore_cnt{};
    struct { bool enable_internal_pullup{}; } flags;
};
struct esp_lcd_panel_io_i2c_config_t {
    int dev_addr{}, scl_speed_hz{}, control_phase_bytes{}, dc_bit_offset{}, lcd_cmd_bits{}, lcd_param_bits{};
};
struct esp_lcd_panel_ssd1306_config_t { int height{}; };
struct esp_lcd_panel_dev_config_t {
    int bits_per_pixel{}, reset_gpio_num{};
    void* vendor_config{};
    struct { bool reset_active_high{}; } flags;
};
namespace heltec_oled_stub {
struct State {
    std::int64_t now_us{1'000'000};
    int draw_failures{}, power_off_calls{}, panel_off_calls{}, panel_on_calls{};
    bool fail_all_draws{false}, fail_panel_off{false}, fail_power_off{false};
    bool bounds_valid{true}, mirrored{false};
    int sda{}, scl{}, address{}, clock_hz{}, reset_pin{}, height{};
    std::vector<std::array<std::uint8_t, 1024>> frames;
};
inline State state;
inline void reset() { state = State{}; }
template<typename... Args> inline void log(const char*, const char*, Args...) {}
}
inline int gpio_config(const gpio_config_t*) { return ESP_OK; }
inline int gpio_set_level(gpio_num_t gpio, int level) {
    if (gpio == 36 && level == 1) {
        ++heltec_oled_stub::state.power_off_calls;
        if (heltec_oled_stub::state.fail_power_off) return ESP_FAIL;
    }
    return ESP_OK;
}
inline int i2c_new_master_bus(const i2c_master_bus_config_t* c, i2c_master_bus_handle_t* out) {
    heltec_oled_stub::state.sda = c->sda_io_num;
    heltec_oled_stub::state.scl = c->scl_io_num;
    *out = &heltec_oled_stub::state;
    return ESP_OK;
}
inline int esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, const esp_lcd_panel_io_i2c_config_t* c,
                                   esp_lcd_panel_io_handle_t* out) {
    heltec_oled_stub::state.address = c->dev_addr;
    heltec_oled_stub::state.clock_hz = c->scl_speed_hz;
    *out = &heltec_oled_stub::state;
    return ESP_OK;
}
inline int esp_lcd_new_panel_ssd1306(esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t* c,
                                    esp_lcd_panel_handle_t* out) {
    heltec_oled_stub::state.reset_pin = c->reset_gpio_num;
    heltec_oled_stub::state.height = static_cast<esp_lcd_panel_ssd1306_config_t*>(c->vendor_config)->height;
    *out = &heltec_oled_stub::state;
    return ESP_OK;
}
inline int esp_lcd_panel_reset(esp_lcd_panel_handle_t) { return ESP_OK; }
inline int esp_lcd_panel_init(esp_lcd_panel_handle_t) { return ESP_OK; }
inline int esp_lcd_panel_mirror(esp_lcd_panel_handle_t, bool x, bool y) {
    heltec_oled_stub::state.mirrored = x && y; return ESP_OK;
}
inline int esp_lcd_panel_invert_color(esp_lcd_panel_handle_t, bool) { return ESP_OK; }
inline int esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool on) {
    if (on) ++heltec_oled_stub::state.panel_on_calls;
    else {
        ++heltec_oled_stub::state.panel_off_calls;
        if (heltec_oled_stub::state.fail_panel_off) return ESP_FAIL;
    }
    return ESP_OK;
}
inline int esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int x0, int y0, int x1, int y1, const void* data) {
    auto& s = heltec_oled_stub::state;
    s.bounds_valid = s.bounds_valid && x0 == 0 && y0 == 0 && x1 == 128 && y1 == 64;
    std::array<std::uint8_t, 1024> frame{};
    std::copy_n(static_cast<const std::uint8_t*>(data), frame.size(), frame.begin());
    s.frames.push_back(frame);
    if (s.fail_all_draws) return ESP_FAIL;
    if (s.draw_failures > 0) { --s.draw_failures; return ESP_FAIL; }
    return ESP_OK;
}
inline std::int64_t esp_timer_get_time() { return heltec_oled_stub::state.now_us; }
inline std::uint32_t pdMS_TO_TICKS(std::uint32_t ms) { return ms; }
inline void vTaskDelay(std::uint32_t) {}
#define ESP_LOGW(...) heltec_oled_stub::log(__VA_ARGS__)
