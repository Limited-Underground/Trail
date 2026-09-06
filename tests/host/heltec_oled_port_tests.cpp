#include "esp_stub.hpp"
#include "heltec_v4_oled.hpp"
#include "heltec_oled_presentation.hpp"
#include "trail_startup_logo.hpp"

#include <iostream>

namespace {
using namespace opentrail::target::heltec_v4_bench;
namespace stub = heltec_oled_stub;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) { ++failures; std::cerr << "FAIL line " << line << ": " << expression << '\n'; }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)
bool black(const std::array<std::uint8_t, 1024>& frame) {
    return std::all_of(frame.begin(), frame.end(), [](auto b) { return b == 0; });
}

void real_initialization_and_logo() {
    stub::reset(); HeltecV4Oled port;
    EXPECT(port.initialize()); EXPECT(port.initialize());
    EXPECT(stub::state.sda == 17 && stub::state.scl == 18);
    EXPECT(stub::state.address == 0x3c && stub::state.clock_hz == 400000);
    EXPECT(stub::state.reset_pin == 21 && stub::state.height == 64);
    EXPECT(stub::state.mirrored && stub::state.panel_on_calls == 1);
    EXPECT(port.render(StartupDisplayView{}));
    EXPECT(stub::state.frames.back() == kTrailStartupLogoSsd1306);
    EXPECT(stub::state.bounds_valid);
}

void real_port_delivers_presentation_frames() {
    stub::reset(); HeltecV4Oled port; HeltecOledPresentation expected;
    EXPECT(port.initialize());
    for (const auto kind : {StartupDisplayFrame::ble_advertising,
                           StartupDisplayFrame::factory_reset_confirmation,
                           StartupDisplayFrame::factory_resetting,
                           StartupDisplayFrame::self_check_failed}) {
        StartupDisplayView view{}; view.frame = kind;
        EXPECT(port.render(view));
        EXPECT(stub::state.frames.back() == expected.present(view, 1000).pixels);
        EXPECT(stub::state.frames.back() != kTrailStartupLogoSsd1306);
        EXPECT(!black(stub::state.frames.back()));
    }
    EXPECT(stub::state.bounds_valid);
}

void draw_failure_conceals_and_latches_port() {
    stub::reset(); HeltecV4Oled port; EXPECT(port.initialize());
    stub::state.draw_failures = 1;
    StartupDisplayView view{}; view.frame = StartupDisplayFrame::ble_advertising;
    EXPECT(!port.render(view));
    EXPECT(stub::state.frames.size() == 2);
    EXPECT(black(stub::state.frames.back()));
    EXPECT(stub::state.panel_off_calls == 1 && stub::state.power_off_calls == 1);
    const auto count = stub::state.frames.size();
    EXPECT(!port.render(view)); EXPECT(!port.initialize());
    EXPECT(stub::state.frames.size() == count);
}

void black_write_failure_still_disables_panel_and_power() {
    stub::reset(); HeltecV4Oled port; EXPECT(port.initialize());
    stub::state.fail_all_draws = true;
    stub::state.fail_panel_off = true;
    StartupDisplayView view{}; view.frame = StartupDisplayFrame::self_check_failed;
    EXPECT(!port.render(view));
    EXPECT(black(stub::state.frames.back()));
    EXPECT(stub::state.panel_off_calls == 1 && stub::state.power_off_calls == 1);
}

void negative_time_is_concealed() {
    stub::reset(); HeltecV4Oled port; EXPECT(port.initialize());
    stub::state.now_us = -1;
    StartupDisplayView view{}; view.frame = StartupDisplayFrame::ble_connected;
    EXPECT(!port.render(view));
    EXPECT(stub::state.frames.size() == 1 && black(stub::state.frames.back()));
    EXPECT(stub::state.panel_off_calls == 1 && stub::state.power_off_calls == 1);
}

void pairing_clear_and_failure_use_real_owner_and_port() {
    stub::reset(); HeltecV4Oled port; StartupDisplayOwner owner(port);
    EXPECT(owner.start());
    EXPECT(owner.show(StartupDisplayFrame::ble_advertising));
    const auto normal = stub::state.frames.back();
    // Fixed synthetic fixture only. No physical pairing material enters the test.
    EXPECT(owner.show_pairing_pin({'1','2','3','4','5','6'}));
    EXPECT(stub::state.frames.back() != normal);
    EXPECT(owner.clear_pairing_pin());
    EXPECT(stub::state.frames.back() == normal);
    stub::state.draw_failures = 1;
    EXPECT(!owner.show_pairing_pin({'6','5','4','3','2','1'}));
    EXPECT(!owner.status().available);
    EXPECT(black(stub::state.frames.back()));
    EXPECT(stub::state.panel_off_calls >= 1 && stub::state.power_off_calls >= 1);
    const auto count = stub::state.frames.size();
    EXPECT(!owner.show_pairing_pin({'1','1','1','1','1','1'}));
    EXPECT(stub::state.frames.size() == count);
}

void rollback_cannot_be_followed_by_pairing_digits() {
    stub::reset(); HeltecV4Oled port; StartupDisplayOwner owner(port);
    EXPECT(owner.start());
    EXPECT(owner.show(StartupDisplayFrame::ble_advertising));
    stub::state.now_us = 999'000;  // Prior normal frame was at 1000 ms.
    (void)owner.show(StartupDisplayFrame::ble_connected);
    const auto count = stub::state.frames.size();
    EXPECT(!owner.show_pairing_pin({'1','2','3','4','5','6'}));
    // Containment may redraw blank/failure, but must not emit a pairing frame.
    for (auto index = count; index < stub::state.frames.size(); ++index) {
        EXPECT(black(stub::state.frames[index]));
    }
}
}

int main() {
    real_initialization_and_logo();
    real_port_delivers_presentation_frames();
    draw_failure_conceals_and_latches_port();
    black_write_failure_still_disables_panel_and_power();
    negative_time_is_concealed();
    pairing_clear_and_failure_use_real_owner_and_port();
    rollback_cannot_be_followed_by_pairing_digits();
    if (failures) return 1;
    std::cout << "PASS actual Heltec OLED port: 7 groups\n";
    return 0;
}
