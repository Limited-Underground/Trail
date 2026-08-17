#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "heltec_startup_display.hpp"

namespace {

using opentrail::companion::CompanionBleRuntimePhase;
using opentrail::target::heltec_v4_bench::StartupDisplayFrame;
using opentrail::target::heltec_v4_bench::StartupDisplayOwner;
using opentrail::target::heltec_v4_bench::StartupDisplayPort;
using opentrail::target::heltec_v4_bench::startup_display_frame_for_ble_phase;
using opentrail::target::heltec_v4_bench::startup_display_text;

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

}  // namespace

int main() {
    test_success_and_duplicate_suppression();
    test_initialization_and_render_fail_closed_locally();
    test_ble_phase_and_copy_mapping();
    std::cout << "3 Heltec startup display groups passed.\n";
    return 0;
}
