#include "companion_connection_diagnostics.hpp"
#include "diagnostic_sensor_display.hpp"
#include "heltec_oled_presentation.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace opentrail::target::heltec_v4_bench;
namespace diag = connection_diagnostics;
namespace footer = opentrail::ui::compact_status_footer;
void require(bool value) { if (!value) std::exit(1); }

class Display final : public StartupDisplayPort {
public:
    std::uint64_t now = 1000;
    unsigned calls = 0, changes = 0, pins = 0;
    bool succeeds = true, have = false;
    HeltecOledPresentation presentation;
    std::array<std::uint8_t,1024> pixels{};
    bool initialize() override { return true; }
    bool render(const StartupDisplayView& view) override {
        if (view.frame == StartupDisplayFrame::logo) return true;
        ++calls;
        if (!succeeds) return false;
        const auto frame = presentation.present(view, now, "Bench", {}, 1);
        if (!have || frame.pixels != pixels) ++changes;
        pixels = frame.pixels; have = true;
        return true;
    }
    bool render_pairing_pin(const PairingPinDisplayView&) override { ++pins; return succeeds; }
    bool conceal() override { return true; }
};

void ring_boundaries() {
    diag::Events events;
    diag::Event out{};
    require(!events.pop(out));
    for (std::uint32_t i=0;i<65;++i) events.push({diag::Kind::protocol_enter,i,0,0});
    require(events.dropped()==1);
    for (std::uint32_t i=0;i<64;++i) { require(events.pop(out)); require(out.uptime_ms==i); }
    require(!events.pop(out));
    for (std::uint32_t i=0;i<10000;++i) {
        events.push({diag::Kind::protocol_exit,i,5,20});
        require(events.pop(out) && out.uptime_ms==i && out.first==5 && out.second==20);
    }
}

void concurrent_writers() {
    diag::Events events;
    std::atomic<unsigned> finished{0};
    auto producer=[&](unsigned actor) {
        for (unsigned i=0;i<10000;++i)
            events.push({diag::Kind::gap_enter,i,actor,i^0x5a5aU});
        finished.fetch_add(1,std::memory_order_release);
    };
    std::thread first(producer,1),second(producer,2);
    unsigned received=0;
    diag::Event out{};
    auto consume=[&] { require(out.first==1 || out.first==2); require(out.second==(out.uptime_ms^0x5a5aU)); ++received; };
    while(finished.load(std::memory_order_acquire)!=2) {
        if(events.pop(out)) consume(); else std::this_thread::yield();
    }
    first.join();second.join();
    while(events.pop(out)) consume();
    require(received+events.dropped()==20000);
}

void display_isolation() {
    Display port; StartupDisplayOwner owner(port); require(owner.start());
    CompactStatusSnapshot source{};
    source.battery_percent={footer::ObservationState::valid,33,1000};
    source.gps_satellites={footer::ObservationState::valid,11,1000};
    source.gps_fix=footer::GpsFixCode::valid; source.freshness={60000,5000};
    for(std::uint64_t now=1000;now<=5000;now+=1000) {
        source.render_now_ms=port.now=now; source.gps_satellites.sampled_at_ms=now;
        const auto shown=diagnostic_sensor_display(source);
        require(source.gps_satellites.value==11 && source.battery_percent.value==33);
        require(shown.render_now_ms==source.render_now_ms && shown.freshness.gps_fresh_for_ms==5000);
        require(owner.show_compact_status(StartupDisplayFrame::ble_connected,shown));
    }
    require(port.changes==1);
    require(port.calls==(OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY ? 5U : 1U));
    source.phone_ready=true;
    require(owner.show_compact_status(StartupDisplayFrame::ble_connected,diagnostic_sensor_display(source)));
    require(port.changes==2);
    require(owner.show_pairing_pin({'1','2','3','4','5','6'}));
    const auto before=port.calls;
    source.phone_ready=false;
    require(owner.show_compact_status(StartupDisplayFrame::ble_connected,diagnostic_sensor_display(source)));
    require(port.calls==before && port.pins==1);
    require(owner.clear_pairing_pin() && port.calls==before+1);
    require(owner.show_factory_reset_confirmation());
    const auto reset=port.calls;
    source.phone_ready=true;
    require(owner.show_compact_status(StartupDisplayFrame::ble_connected,diagnostic_sensor_display(source)));
    require(port.calls==reset);
    require(owner.clear_factory_reset_confirmation());
    port.succeeds=false; source.phone_ready=false;
    require(!owner.show_compact_status(StartupDisplayFrame::ble_connected,diagnostic_sensor_display(source)));
    require(!owner.status().available);
}
int main() {
    ring_boundaries(); concurrent_writers(); display_isolation();
    std::cout << "PASS ring capacity/order/reuse, concurrent producers, sensor isolation, phone readiness, overlays and render failure; sensor_display=" << OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY << '\n';
}
