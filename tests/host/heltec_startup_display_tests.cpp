#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "heltec_startup_display.hpp"

namespace {

using opentrail::companion::CompanionBleRuntimePhase;
using opentrail::target::heltec_v4_bench::StartupDisplayFrame;
using opentrail::target::heltec_v4_bench::StartupDisplayOwner;
using opentrail::target::heltec_v4_bench::StartupDisplayPort;
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

    bool render(StartupDisplayFrame frame) override {
        frames.push_back(frame);
        return frames.size() != failing_render_call;
    }

    bool initialize_succeeds{true};
    std::size_t failing_render_call{0};
    std::size_t initialize_calls{0};
    std::vector<StartupDisplayFrame> frames{};
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
    require(port.frames.size() == 3, "duplicate frame suppressed");
    require(port.frames[0] == StartupDisplayFrame::logo, "logo first");
    require(port.frames[1] == StartupDisplayFrame::ble_starting,
            "starting second");
    require(port.frames[2] == StartupDisplayFrame::ble_advertising,
            "advertising only after explicit update");
    require(status.available && status.render_count == 3,
            "successful display status");
}

void test_initialization_and_render_fail_closed_locally() {
    FakeDisplayPort init_failure;
    init_failure.initialize_succeeds = false;
    StartupDisplayOwner unavailable{init_failure};
    require(!unavailable.start(), "init failure returned");
    require(init_failure.frames.empty(), "no render after init failure");
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
    require(render_failure.frames.size() == 2,
            "failed display does not retry implicitly");
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
}  // namespace

int main() {
    test_success_and_duplicate_suppression();
    test_initialization_and_render_fail_closed_locally();
    test_ble_phase_and_copy_mapping();
    test_compact_footer_placeholder_mapping();
    std::cout << "4 Heltec startup display groups passed.\n";
    return 0;
}
