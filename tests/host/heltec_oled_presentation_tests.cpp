#include "heltec_oled_presentation.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace opentrail::target::heltec_v4_bench;
using opentrail::ui::oled_presentation::Frame;
using opentrail::ui::oled_presentation::Surface;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string row(const Frame& frame, std::size_t index) {
    return frame.rows[index].data();
}

void unused_rows_blank(const Frame& frame, std::size_t start) {
    for (std::size_t index = start; index < frame.rows.size(); ++index) {
        for (char value : frame.rows[index]) require(value == '\0', "unused text row not blank");
        for (std::size_t x = 0; x < 128; ++x)
            require(frame.pixels[index * 128 + x] == 0, "unused pixel row not blank");
    }
}

void region_placeholders(const Frame& frame) {
    require(row(frame, 3) == "DEVICE", "unconfirmed name invented");
    require(row(frame, 7) == "TIME --:--", "unconfirmed clock invented");
    for (const auto index : {2U, 4U, 5U, 6U}) {
        require(row(frame, index).empty(), "unused region row not blank");
        for (std::size_t x=0;x<128;++x)
            require(frame.pixels[index*128+x]==0, "unused region pixels not blank");
    }
}

void every_frame_and_invalid_fail_closed() {
    constexpr std::array<Surface, 9> expected{
        Surface::failure, Surface::failure, Surface::region_required,
        Surface::region_required, Surface::region_required, Surface::region_required,
        Surface::failure, Surface::reset_confirmation, Surface::reset_progress,
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        HeltecOledPresentation mapper;
        StartupDisplayView view{};
        view.frame = static_cast<StartupDisplayFrame>(i);
        require(mapper.present(view, 100).surface == expected[i], "frame mapping differs");
    }
    // Exercise the entire remaining uint8 enum representation, not only one sentinel.
    for (unsigned i = expected.size(); i <= 255; ++i) {
        HeltecOledPresentation mapper;
        StartupDisplayView view{};
        view.frame = static_cast<StartupDisplayFrame>(i);
        const auto frame = mapper.present(view, 100);
        require(frame.surface == Surface::failure, "invalid enum admitted");
        unused_rows_blank(frame, 1);
    }
}

void raw_connection_and_footer_cannot_grant_status() {
    constexpr std::array<StartupDisplayFrame, 4> ordinary{
        StartupDisplayFrame::ble_starting, StartupDisplayFrame::ble_advertising,
        StartupDisplayFrame::ble_connected, StartupDisplayFrame::ble_retrying,
    };
    for (const auto state : ordinary) {
        HeltecOledPresentation clean;
        HeltecOledPresentation hostile;
        StartupDisplayView view{};
        view.frame = state;
        const auto expected = clean.present(view, 1'000);
        view.has_footer = true;
        view.footer.columns.fill(0xff);
        const auto actual = hostile.present(view, 1'000);
        require(actual.surface == Surface::region_required, "missing region hidden");
        require(row(actual, 0) == "REGION REQUIRED", "region message missing");
        require(row(actual, 1) == "RADIO TX DISABLED", "TX denial missing");
        require(actual.rows == expected.rows && actual.pixels == expected.pixels, "footer inferred as telemetry");
        for (std::size_t i = 0; i < actual.rows.size(); ++i) {
            require(row(actual, i).find("READY") == std::string::npos, "raw link promoted to Ready");
        }
        region_placeholders(actual);
    }
}

void safety_surfaces_and_transition_clear_lower_content() {
    HeltecOledPresentation mapper;
    StartupDisplayView view{};
    view.frame = StartupDisplayFrame::ble_advertising;
    require(mapper.present(view, 0).surface == Surface::region_required, "ordinary baseline");
    view.frame = StartupDisplayFrame::factory_reset_confirmation;
    auto frame = mapper.present(view, 1);
    require(frame.surface == Surface::reset_confirmation, "confirmation priority");
    require(row(frame, 0) == "ERASE ALL TRAIL DATA?", "confirmation text");
    require(row(frame, 1) == "RELEASE THEN CONFIRM", "confirmation direction");
    unused_rows_blank(frame, 2);
    view.frame = StartupDisplayFrame::factory_resetting;
    frame = mapper.present(view, 2);
    require(row(frame, 0) == "RESETTING", "progress must not claim completion");
    require(row(frame, 1) == "DO NOT REMOVE POWER", "progress direction");
    unused_rows_blank(frame, 2);
    view.frame = StartupDisplayFrame::ble_error;
    frame = mapper.present(view, 3);
    require(row(frame, 0) == "SELF CHECK FAIL", "failure surface");
    unused_rows_blank(frame, 1);
    view.frame = StartupDisplayFrame::ble_connected;
    frame = mapper.present(view, 4);
    require(frame.surface == Surface::region_required, "later normal still has no region");
    region_placeholders(frame);
}

void typed_name_and_clock_survive_region_warning_only() {
    HeltecOledPresentation mapper;
    StartupDisplayView view{};view.frame=StartupDisplayFrame::ble_connected;
    opentrail::time::OledClock clock;
    require(clock.synchronize(3600,opentrail::time::OledClockFormat::hour_24,10,10,true), "clock fixture");
    auto frame=mapper.present(view,10,"Bench One",clock.observe(10));
    require(frame.surface==Surface::region_required && row(frame,1)=="RADIO TX DISABLED", "typed settings granted region");
    require(row(frame,3)=="BENCH ONE" && row(frame,7)=="TIME 01:00", "typed independent settings missing");
    view.frame=StartupDisplayFrame::factory_reset_confirmation;
    frame=mapper.present(view,11,"Bench One",clock.observe(11));
    unused_rows_blank(frame,2);
}

void rollback_contains_all_later_views() {
    HeltecOledPresentation mapper;
    StartupDisplayView view{};
    view.frame = StartupDisplayFrame::ble_advertising;
    require(mapper.present(view, 100).surface == Surface::region_required, "initial time");
    require(mapper.present(view, 100).surface == Surface::region_required, "equal time permitted");
    view.frame = StartupDisplayFrame::factory_reset_confirmation;
    require(mapper.present(view, 99).surface == Surface::failure, "rollback not contained");
    view.frame = StartupDisplayFrame::factory_resetting;
    const auto frame = mapper.present(view, std::numeric_limits<std::uint64_t>::max());
    require(frame.surface == Surface::failure, "rollback containment was reset");
    unused_rows_blank(frame, 1);
}
}  // namespace

int main() {
    every_frame_and_invalid_fail_closed();
    raw_connection_and_footer_cannot_grant_status();
    safety_surfaces_and_transition_clear_lower_content();
    rollback_contains_all_later_views();
    typed_name_and_clock_survive_region_warning_only();
    std::cout << "PASS: 5 Heltec OLED presentation scenario groups\n";
    return 0;
}
