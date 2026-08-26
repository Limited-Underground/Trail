#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "heltec_startup_display.hpp"

namespace {

using opentrail::companion::CompanionBleRuntimePhase;
using opentrail::target::heltec_v4_bench::CompactStatusSnapshot;
using opentrail::target::heltec_v4_bench::StartupDisplayFrame;
using opentrail::target::heltec_v4_bench::StartupDisplayOwner;
using opentrail::target::heltec_v4_bench::StartupDisplayPort;
using opentrail::target::heltec_v4_bench::StartupDisplayView;
using opentrail::target::heltec_v4_bench::startup_display_frame_for_ble_phase;
using opentrail::target::heltec_v4_bench::startup_display_text;

using opentrail::target::heltec_v4_bench::startup_display_compact_footer_page;
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakeDisplayPort final : public StartupDisplayPort {
public:
    bool initialize() override {
        ++initialize_calls;
        return initialize_succeeds;
    }

    bool render(const StartupDisplayView& view) override {
        views.push_back(view);
        return views.size() != failing_render_call;
    }

    bool initialize_succeeds{true};
    std::size_t failing_render_call{0};
    std::size_t initialize_calls{0};
    std::vector<StartupDisplayView> views{};
};

void test_success_and_duplicate_suppression() {
    FakeDisplayPort port;
    StartupDisplayOwner owner{port};
    require(owner.start(), "display start");
    require(owner.start(), "display start idempotence");
    require(owner.show(StartupDisplayFrame::ble_starting), "show starting");
    require(owner.show(StartupDisplayFrame::ble_starting),
            "duplicate starting accepted");
    require(owner.show(StartupDisplayFrame::ble_advertising),
            "show advertising");
    const auto status = owner.status();
    require(port.initialize_calls == 1, "initialize exactly once");
    require(port.views.size() == 3, "duplicate frame suppressed");
    require(port.views[0].frame == StartupDisplayFrame::logo, "logo first");
    require(port.views[1].frame == StartupDisplayFrame::ble_starting,
            "starting second");
    require(port.views[2].frame == StartupDisplayFrame::ble_advertising,
            "advertising only after explicit update");
    require(status.available && status.render_count == 3,
            "successful display status");
}

void test_initialization_and_render_fail_closed_locally() {
    FakeDisplayPort init_failure;
    init_failure.initialize_succeeds = false;
    StartupDisplayOwner unavailable{init_failure};
    require(!unavailable.start(), "init failure returned");
    require(init_failure.views.empty(), "no render after init failure");
    require(!unavailable.show(StartupDisplayFrame::ble_advertising),
            "unavailable display stays local-only");

    FakeDisplayPort render_failure;
    render_failure.failing_render_call = 2;
    StartupDisplayOwner owner{render_failure};
    require(owner.start(), "render-failure setup");
    require(!owner.show(StartupDisplayFrame::ble_starting),
            "render failure returned");
    require(!owner.status().available, "render failure latches unavailable");
    require(!owner.show(StartupDisplayFrame::ble_advertising),
            "no later display work after failure");
    require(render_failure.views.size() == 2,
            "failed display does not retry implicitly");
}

void test_content_aware_footer_suppression_and_redraw() {
    using Page = opentrail::ui::compact_status_footer::Page;

    FakeDisplayPort port;
    StartupDisplayOwner owner{port};
    require(owner.start(), "content-aware display start");

    Page first{};
    first.columns[25] = 0x01;
    require(owner.show_footer(StartupDisplayFrame::ble_connected, first),
            "first connected footer accepted");
    require(port.views.size() == 2, "first footer rendered");
    require(port.views.back().frame == StartupDisplayFrame::ble_connected,
            "footer retains BLE frame");
    require(port.views.back().has_footer, "footer presence retained");
    require(port.views.back().footer.columns == first.columns,
            "exact footer retained");

    require(owner.show_footer(StartupDisplayFrame::ble_connected, first),
            "identical connected footer accepted");
    require(port.views.size() == 2, "identical footer suppressed");

    auto changed = first;
    changed.columns[121] = 0x7F;
    require(owner.show_footer(StartupDisplayFrame::ble_connected, changed),
            "changed connected footer accepted");
    require(port.views.size() == 3,
            "changed footer redraws under unchanged BLE frame");
    require(port.views.back().footer.columns == changed.columns,
            "changed exact footer rendered");

    require(owner.show(StartupDisplayFrame::ble_connected),
            "plain connected footer accepted");
    require(port.views.size() == 4,
            "plain footer redraws after different explicit content");
    require(port.views.back().has_footer,
            "plain BLE frame carries generated footer");
    require(port.views.back().footer.columns != changed.columns,
            "plain BLE frame replaces explicit footer content");

    const auto status = owner.status();
    require(status.available &&
                status.frame == StartupDisplayFrame::ble_connected &&
                status.render_count == 4,
            "content-aware redraw updates status");
}

void test_footer_rejected_for_non_footer_frames() {
    using Page = opentrail::ui::compact_status_footer::Page;

    FakeDisplayPort port;
    StartupDisplayOwner owner{port};
    require(owner.start(), "footer rejection display start");

    Page page{};
    page.columns.fill(0xFF);
    require(!owner.show_footer(StartupDisplayFrame::logo, page),
            "logo footer rejected");
    require(!owner.show_footer(StartupDisplayFrame::self_check_failed, page),
            "self-check footer rejected");
    require(!owner.show_footer(static_cast<StartupDisplayFrame>(0xFF), page),
            "invalid frame footer rejected");
    require(port.views.size() == 1,
            "rejected footer requests do not render");
    require(owner.status().available && owner.status().render_count == 1,
            "rejected footer requests do not contain display");

    require(owner.show(StartupDisplayFrame::self_check_failed),
            "plain self-check frame remains available");
    require(port.views.size() == 2 &&
                port.views.back().frame ==
                    StartupDisplayFrame::self_check_failed &&
                !port.views.back().has_footer,
            "plain self-check frame retains full-screen path");
}
void test_ble_phase_and_copy_mapping() {
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::dormant) ==
                StartupDisplayFrame::ble_starting,
            "dormant mapping");
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::waiting_for_host_sync) ==
                StartupDisplayFrame::ble_starting,
            "host-sync mapping");
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::advertising) ==
                StartupDisplayFrame::ble_advertising,
            "advertising mapping");
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::connected) ==
                StartupDisplayFrame::ble_connected,
            "connected mapping");
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::restart_wait) ==
                StartupDisplayFrame::ble_retrying,
            "retry mapping");
    require(startup_display_frame_for_ble_phase(
                CompanionBleRuntimePhase::contained) ==
                StartupDisplayFrame::ble_error,
            "contained mapping");

    require(std::string(startup_display_text(
                StartupDisplayFrame::ble_advertising)) == "BLE ADVERTISING",
            "advertising copy");
    require(std::string(startup_display_text(
                StartupDisplayFrame::self_check_failed)) == "SELF CHECK FAIL",
            "self-check copy");
    require(std::string(startup_display_text(
                StartupDisplayFrame::logo)).empty(), "logo has no false status");
}


void test_compact_footer_placeholder_mapping() {
    using Page = opentrail::ui::compact_status_footer::Page;
    using Glyph = std::array<std::uint8_t, 5>;
    const Glyph dash{0x08, 0x08, 0x08, 0x08, 0x08};
    const Glyph percent{0x63, 0x13, 0x08, 0x64, 0x63};
    const Glyph blank{0x00, 0x00, 0x00, 0x00, 0x00};
    const std::array<std::pair<StartupDisplayFrame, Glyph>, 5> cases{{
        {StartupDisplayFrame::ble_starting,
         {0x46, 0x49, 0x49, 0x49, 0x31}},
        {StartupDisplayFrame::ble_advertising,
         {0x7E, 0x11, 0x11, 0x11, 0x7E}},
        {StartupDisplayFrame::ble_connected,
         {0x3E, 0x41, 0x41, 0x41, 0x22}},
        {StartupDisplayFrame::ble_retrying,
         {0x7F, 0x09, 0x19, 0x29, 0x46}},
        {StartupDisplayFrame::ble_error,
         {0x7F, 0x49, 0x49, 0x49, 0x41}},
    }};

    const auto glyph_at = [](const Page& page, std::size_t x) {
        Glyph result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = page.columns[x + index];
        }
        return result;
    };

    const auto page_is_blank = [](const Page& page) {
        for (const auto column : page.columns) {
            if (column != 0) return false;
        }
        return true;
    };

    for (const auto& item : cases) {
        Page page{};
        require(startup_display_compact_footer_page(item.first, page),
                "BLE frame uses compact footer");
        require(glyph_at(page, 25) == dash && glyph_at(page, 31) == dash &&
                    glyph_at(page, 37) == percent,
                "battery placeholder only");
        require(glyph_at(page, 75) == dash && glyph_at(page, 81) == dash,
                "GPS placeholder only");
        require(glyph_at(page, 113) == item.second, "BLE short-code glyph");
        require(glyph_at(page, 121) == blank, "unsupported activity blank");
        require(page.columns[0] == 0 && page.columns[126] == 0 &&
                    page.columns[127] == 0,
                "footer edge columns blank");
    }

    Page page{};
    page.columns.fill(0xFF);
    require(!startup_display_compact_footer_page(
                StartupDisplayFrame::logo, page),
            "logo keeps blank footer path");
    require(page_is_blank(page), "logo request clears a prefilled page");
    page.columns.fill(0xFF);
    require(!startup_display_compact_footer_page(
                StartupDisplayFrame::self_check_failed, page),
            "self-check failure keeps explicit text path");
    require(page_is_blank(page),
            "self-check request clears a prefilled page");
}

void test_dynamic_compact_footer_observations_and_redraw() {
    using Direction = opentrail::ui::compact_status_footer::Direction;
    using Glyph = std::array<std::uint8_t, 5>;
    using ObservationState =
        opentrail::ui::compact_status_footer::ObservationState;

    const Glyph zero{0x3E, 0x51, 0x49, 0x45, 0x3E};
    const Glyph one{0x00, 0x42, 0x7F, 0x40, 0x00};
    const Glyph two{0x42, 0x61, 0x51, 0x49, 0x46};
    const Glyph percent{0x63, 0x13, 0x08, 0x64, 0x63};
    const Glyph connected{0x3E, 0x41, 0x41, 0x41, 0x22};
    const Glyph tx{0x04, 0x02, 0x7F, 0x02, 0x04};

    const auto glyph_at = [](const auto& page, std::size_t x) {
        Glyph result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = page.columns[x + index];
        }
        return result;
    };

    CompactStatusSnapshot snapshot{};
    snapshot.battery_percent = {ObservationState::valid, 100, 950};
    snapshot.gps_satellites = {ObservationState::valid, 12, 950};
    snapshot.freshness = {100, 100};
    snapshot.activity_supported = true;
    snapshot.activity = Direction::tx;
    snapshot.activity_at_ms = 980;
    snapshot.activity_visible_for_ms = 100;
    snapshot.render_now_ms = 1000;

    FakeDisplayPort port;
    StartupDisplayOwner owner{port};
    require(owner.start(), "dynamic footer display start");
    require(owner.show_compact_status(
                StartupDisplayFrame::ble_connected, snapshot),
            "dynamic connected footer accepted");
    require(port.views.size() == 2, "dynamic footer rendered");
    const auto& page = port.views.back().footer;
    require(glyph_at(page, 25) == one && glyph_at(page, 31) == zero &&
                glyph_at(page, 37) == zero &&
                glyph_at(page, 43) == percent,
            "three-digit battery observation rendered");
    require(glyph_at(page, 75) == one && glyph_at(page, 81) == two,
            "two-digit GPS observation rendered");
    require(glyph_at(page, 113) == connected,
            "BLE frame remains footer authority");
    require(glyph_at(page, 121) == tx,
            "accepted current TX observation rendered");

    require(owner.show_compact_status(
                StartupDisplayFrame::ble_connected, snapshot),
            "identical dynamic footer accepted");
    require(port.views.size() == 2,
            "identical dynamic footer suppressed");

    snapshot.battery_percent.value = 99;
    require(owner.show_compact_status(
                StartupDisplayFrame::ble_connected, snapshot),
            "changed battery observation accepted");
    require(port.views.size() == 3,
            "changed observation redraws unchanged BLE frame");
}

void test_dynamic_compact_footer_fail_closed_boundaries() {
    using Direction = opentrail::ui::compact_status_footer::Direction;
    using Glyph = std::array<std::uint8_t, 5>;
    using ObservationState =
        opentrail::ui::compact_status_footer::ObservationState;
    using Page = opentrail::ui::compact_status_footer::Page;

    const Glyph dash{0x08, 0x08, 0x08, 0x08, 0x08};
    const Glyph percent{0x63, 0x13, 0x08, 0x64, 0x63};
    const Glyph blank{0x00, 0x00, 0x00, 0x00, 0x00};
    const Glyph advertising{0x7E, 0x11, 0x11, 0x11, 0x7E};

    const auto glyph_at = [](const Page& page, std::size_t x) {
        Glyph result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = page.columns[x + index];
        }
        return result;
    };
    const auto page_is_blank = [](const Page& page) {
        for (const auto column : page.columns) {
            if (column != 0) return false;
        }
        return true;
    };

    CompactStatusSnapshot snapshot{};
    snapshot.battery_percent = {ObservationState::valid, 101, 999};
    snapshot.gps_satellites = {ObservationState::valid, 12, 1001};
    snapshot.freshness = {100, 100};
    snapshot.activity_supported = true;
    snapshot.activity = Direction::rx;
    snapshot.activity_at_ms = 900;
    snapshot.activity_visible_for_ms = 100;
    snapshot.render_now_ms = 1000;

    Page page{};
    require(startup_display_compact_footer_page(
                StartupDisplayFrame::ble_advertising, snapshot, page),
            "dynamic advertising footer accepted");
    require(glyph_at(page, 25) == dash && glyph_at(page, 31) == dash &&
                glyph_at(page, 37) == percent,
            "out-of-range battery fails closed");
    require(glyph_at(page, 75) == dash && glyph_at(page, 81) == dash,
            "future GPS observation fails closed");
    require(glyph_at(page, 113) == advertising,
            "advertising frame maps independently of invalid metrics");
    require(glyph_at(page, 121) == blank,
            "expired activity observation fails closed");

    page.columns.fill(0xFF);
    require(!startup_display_compact_footer_page(
                StartupDisplayFrame::logo, snapshot, page),
            "dynamic logo footer rejected");
    require(page_is_blank(page), "dynamic rejected frame clears page");

    FakeDisplayPort port;
    StartupDisplayOwner owner{port};
    require(owner.start(), "dynamic rejection display start");
    require(!owner.show_compact_status(
                StartupDisplayFrame::self_check_failed, snapshot),
            "dynamic self-check footer rejected");
    require(port.views.size() == 1,
            "rejected dynamic footer does not replace boot view");
}
}  // namespace

int main() {
    test_success_and_duplicate_suppression();
    test_initialization_and_render_fail_closed_locally();
    test_content_aware_footer_suppression_and_redraw();
    test_footer_rejected_for_non_footer_frames();
    test_ble_phase_and_copy_mapping();
    test_compact_footer_placeholder_mapping();
    test_dynamic_compact_footer_observations_and_redraw();
    test_dynamic_compact_footer_fail_closed_boundaries();
    std::cout << "8 Heltec startup display groups passed.\n";
    return 0;
}
