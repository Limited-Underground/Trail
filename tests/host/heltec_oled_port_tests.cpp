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

void setup_label_uses_actual_pairing_pixels_and_clears() {
    stub::reset(); HeltecV4Oled port; StartupDisplayOwner owner{port};
    EXPECT(owner.start());
    const opentrail::ui::SetupCode code{'U','V','W','X','Y','Z'};
    EXPECT(opentrail::ui::setup_advertising_name(code,true).bytes==code);
    EXPECT(owner.set_setup_code(code));
    EXPECT(owner.show(StartupDisplayFrame::ble_connected));
    const auto owned_frame=stub::state.frames.back();
    EXPECT(owner.show_pairing_pin({'1','2','3','4','5','6'}));
    const auto frame=stub::state.frames.back();
    // Decode the actual panel pixels as Trail-UVWXYZ, including all prefix glyphs.
    const std::array<std::array<std::uint8_t,5>,12> glyphs{{
        {0x01,0x01,0x7F,0x01,0x01},{0x7C,0x08,0x04,0x04,0x08},
        {0x20,0x54,0x54,0x54,0x78},{0x00,0x44,0x7D,0x40,0x00},
        {0x00,0x41,0x7F,0x40,0x00},{0x08,0x08,0x08,0x08,0x08},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}}};
    for(std::size_t i=0;i<glyphs.size();++i) for(std::size_t x=0;x<5;++x) for(std::size_t y=0;y<7;++y) {
        const auto pixel=(frame[((44+y)/8)*128+28+i*6+x] >> ((44+y)%8))&1;
        EXPECT(pixel==((glyphs[i][x]>>y)&1));
    }
    EXPECT(stub::state.bounds_valid);
    EXPECT(owner.clear_pairing_pin());
    EXPECT(stub::state.frames.back()==owned_frame);
    EXPECT(opentrail::ui::setup_advertising_name(code,false).size==0);
}

void clock_minute_redraw_survives_unchanged_transport_footer() {
    stub::reset();HeltecV4Oled port;StartupDisplayOwner owner{port};
    EXPECT(owner.start());opentrail::time::OledClock clock;
    EXPECT(clock.synchronize(36000,opentrail::time::OledClockFormat::hour_24,1000,1000,true));
    CompactStatusSnapshot snapshot{};snapshot.phone_ready=true;
    port.set_configuration("Trail Bench",clock.observe(1000),1);
    EXPECT(owner.show_compact_status(StartupDisplayFrame::ble_connected,snapshot));
    const auto initial=stub::state.frames.back();auto count=stub::state.frames.size();
    for(std::uint64_t ms=1100;ms<61000;ms+=100) {
        stub::state.now_us=ms*1000;port.set_configuration("Trail Bench",clock.observe(ms),1);
        EXPECT(owner.show_compact_status(StartupDisplayFrame::ble_connected,snapshot));
    }
    EXPECT(stub::state.frames.size()==count);
    stub::state.now_us=61000000;port.set_configuration("Trail Bench",clock.observe(61000),1);
    EXPECT(owner.show_compact_status(StartupDisplayFrame::ble_connected,snapshot));
    EXPECT(stub::state.frames.size()==count+1);
    EXPECT(stub::state.frames.back()!=initial);
    // A failed dirty redraw retains the latch and enters existing display containment.
    port.set_configuration("Changed",clock.observe(61000),1);stub::state.draw_failures=1;
    EXPECT(!owner.show_compact_status(StartupDisplayFrame::ble_connected,snapshot));
    EXPECT(port.content_changed() && !owner.status().available);
    EXPECT(black(stub::state.frames.back()));
    count=stub::state.frames.size();
    EXPECT(!owner.show_compact_status(StartupDisplayFrame::ble_connected,snapshot));
    EXPECT(stub::state.frames.size()==count);
}

void metadata_redraw_and_overlays_preserve_latest_clock() {
    stub::reset();HeltecV4Oled port;StartupDisplayOwner owner{port};
    EXPECT(owner.start());opentrail::time::OledClock clock;
    EXPECT(clock.synchronize(36000,opentrail::time::OledClockFormat::hour_24,1000,1000,true));
    CompactStatusSnapshot snapshot{};snapshot.phone_ready=true;
    StartupDisplayView view{};view.frame=StartupDisplayFrame::ble_connected;view.phone_ready=true;
    HeltecOledPresentation expected;
    auto show=[&]() { EXPECT(owner.show_compact_status(view.frame,snapshot)); };
    port.set_configuration("First",clock.observe(1000),1);show();
    auto count=stub::state.frames.size();
    port.set_configuration("Second",clock.observe(1000),1);show();
    EXPECT(stub::state.frames.size()==++count);
    EXPECT(stub::state.frames.back()==expected.present(view,1000,"Second",clock.observe(1000),1).pixels);
    port.set_configuration("Second",clock.observe(1000),2);show();
    EXPECT(stub::state.frames.size()==++count);
    EXPECT(stub::state.frames.back()==expected.present(view,1000,"Second",clock.observe(1000),2).pixels);
    EXPECT(clock.synchronize(36000,opentrail::time::OledClockFormat::hour_12,1000,1000,true));
    port.set_configuration("Second",clock.observe(1000),2);show();
    EXPECT(stub::state.frames.size()==++count);
    EXPECT(stub::state.frames.back()==expected.present(view,1000,"Second",clock.observe(1000),2).pixels);
    port.set_configuration("Second",{},2);show();
    EXPECT(stub::state.frames.size()==++count);
    EXPECT(stub::state.frames.back()==expected.present(view,1000,"Second",{},2).pixels);
    EXPECT(owner.show_pairing_pin({'1','2','3','4','5','6'}));
    count=stub::state.frames.size();const auto pin_frame=stub::state.frames.back();
    stub::state.now_us=61000000;port.set_configuration("During PIN",clock.observe(61000),1);show();
    EXPECT(stub::state.frames.size()==count && stub::state.frames.back()==pin_frame);
    EXPECT(owner.clear_pairing_pin());
    EXPECT(stub::state.frames.back()==expected.present(view,61000,"During PIN",clock.observe(61000),1).pixels);
    count=stub::state.frames.size();show();EXPECT(stub::state.frames.size()==count);
    EXPECT(owner.show_factory_reset_confirmation());count=stub::state.frames.size();
    stub::state.now_us=121000000;port.set_configuration("During reset",clock.observe(121000),0);show();
    EXPECT(stub::state.frames.size()==count);
    EXPECT(owner.clear_factory_reset_confirmation());
    EXPECT(stub::state.frames.back()==expected.present(view,121000,"During reset",clock.observe(121000),0).pixels);
    count=stub::state.frames.size();show();EXPECT(stub::state.frames.size()==count);
    EXPECT(owner.show_factory_reset_in_progress());count=stub::state.frames.size();
    stub::state.now_us=181000000;port.set_configuration("Hidden",clock.observe(181000),1);show();
    EXPECT(stub::state.frames.size()==count);
}

void unowned_setup_label_remains_after_pairing_timeout() {
    stub::reset();HeltecV4Oled port;StartupDisplayOwner owner{port};
    EXPECT(owner.start());const opentrail::ui::SetupCode code{'U','V','W','X','Y','Z'};
    EXPECT(owner.set_setup_code(code));
    opentrail::companion::CompanionV1BondOwnerStatus ownership{};
    ownership.phase=opentrail::companion::CompanionV1BondOwnerPhase::closed_unowned;
    port.set_configuration("",{},0,startup_unowned_setup_code(ownership,code));
    EXPECT(owner.show(StartupDisplayFrame::ble_advertising));
    EXPECT(owner.show_pairing_pin({'1','2','3','4','5','6'}));
    stub::state.now_us=61000000;EXPECT(owner.clear_pairing_pin());
    HeltecOledPresentation expected;StartupDisplayView view{};view.frame=StartupDisplayFrame::ble_advertising;
    EXPECT(stub::state.frames.back()==expected.present(view,61000,"Trail-UVWXYZ",{},0).pixels);
    const auto label_frame=stub::state.frames.back();auto count=stub::state.frames.size();
    for(int i=0;i<20;++i) {
        port.set_configuration("",{},0,startup_unowned_setup_code(ownership,code));
        EXPECT(owner.show(view.frame));
    }
    EXPECT(stub::state.frames.size()==count && stub::state.frames.back()==label_frame);
    opentrail::time::OledClock clock;
    EXPECT(clock.synchronize(36000,opentrail::time::OledClockFormat::hour_24,61000,61000,true));
    port.set_configuration("",clock.observe(61000),0,startup_unowned_setup_code(ownership,code));
    EXPECT(owner.show(view.frame));
    EXPECT(stub::state.frames.back()==expected.present(view,61000,"Trail-UVWXYZ",clock.observe(61000),0).pixels);
    ownership.phase=opentrail::companion::CompanionV1BondOwnerPhase::closed_owned;
    ownership.owner_present=true;
    port.set_configuration("",{},0,startup_unowned_setup_code(ownership,code));
    EXPECT(owner.show(view.frame));
    EXPECT(stub::state.frames.back()==expected.present(view,61000,"",{},0).pixels);
    EXPECT(stub::state.frames.back()!=label_frame);
    port.set_configuration("Saved Name",{},0,code);EXPECT(owner.show(view.frame));
    EXPECT(stub::state.frames.back()==expected.present(view,61000,"Saved Name",{},0).pixels);
}

void review_cells_reach_all_actual_panel_rows() {
    using opentrail::security_evaluation::EnrollmentReviewLayout;
    stub::reset(); HeltecV4Oled port; StartupDisplayOwner owner(port);
    EXPECT(owner.start()); std::uint64_t lease{}; EXPECT(owner.acquire_enrollment_review(lease));
    EnrollmentReviewLayout layout{};
    // Exact edge-cell fixture. Independent expected SSD1306 columns prove the
    // complete 21-cell row and all eight pages reach the real driver boundary.
    for (auto& row : layout.text) { row.fill('A'); row[21]=0; }
    EXPECT(owner.render_enrollment_review(lease,1,layout));
    std::array<std::uint8_t,1024> expected{};
    constexpr std::array<std::uint8_t,5> a{0x7E,0x11,0x11,0x11,0x7E};
    for(unsigned row=0;row<8;++row)for(unsigned cell=0;cell<21;++cell)
        for(unsigned x=0;x<5;++x)expected[row*128+1+cell*6+x]=a[x];
    EXPECT(stub::state.frames.back()==expected);EXPECT(stub::state.bounds_valid);
    // The group separator must not be silently rendered as a blank glyph.
    layout={};layout.text[6][0]='G';layout.text[6][1]=':';
    EXPECT(owner.render_enrollment_review(lease,2,layout));
    const auto& pixels=stub::state.frames.back();
    EXPECT(pixels[6*128+8]==0x36 && pixels[6*128+9]==0x36);
    for(unsigned row=0;row<8;++row)if(row!=6)
        for(unsigned x=0;x<128;++x)EXPECT(pixels[row*128+x]==0);
    // Four complete 16-digit fingerprint rows and the full group occupy their
    // exact cells. Fixed glyph columns independently check every visible digit.
    constexpr char digits[]="0123456789ABCDEF";
    constexpr std::array<std::array<std::uint8_t,5>,16> hex{{
        {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01}}};
    for(unsigned row=2;row<6;++row)for(unsigned cell=0;cell<16;++cell)layout.text[row][cell]=digits[cell];
    for(unsigned cell=0;cell<16;++cell)layout.text[6][cell+2]=digits[cell];
    EXPECT(owner.render_enrollment_review(lease,3,layout));
    const auto& fingerprint=stub::state.frames.back();
    for(unsigned row=2;row<7;++row)for(unsigned cell=0;cell<16;++cell)
        for(unsigned x=0;x<5;++x)EXPECT(fingerprint[row*128+1+(cell+(row==6?2:0))*6+x]==hex[cell][x]);
    EXPECT(owner.release_enrollment_review(lease));
    EXPECT(stub::state.frames.back()==kTrailStartupLogoSsd1306);
}

void review_driver_rejects_clipping_and_conceals_failures() {
    using opentrail::security_evaluation::EnrollmentReviewLayout;
    for(unsigned mode=0;mode<5;++mode) {
        stub::reset();HeltecV4Oled port;StartupDisplayOwner owner(port);std::uint64_t lease{};
        EXPECT(owner.start());EXPECT(owner.acquire_enrollment_review(lease));
        EnrollmentReviewLayout layout{};layout.text[0][0]='A';
        EXPECT(owner.render_enrollment_review(lease,1,layout));
        if(mode==0) layout.text[7].fill('A'); // Missing terminator.
        if(mode==1) layout.text[0][2]='B'; // Hidden bytes after the NUL.
        if(mode==2) stub::state.draw_failures=1;
        if(mode==3) stub::state.now_us=-1;
        if(mode==4) {stub::state.fail_all_draws=true;stub::state.fail_panel_off=true;}
        EXPECT(!owner.render_enrollment_review(lease,2,layout));
        EXPECT(!owner.status().available && owner.enrollment_review_status().lease==0);
        EXPECT(stub::state.power_off_calls>=1);
        const auto count=stub::state.frames.size();
        EXPECT(!owner.render_enrollment_review(lease,3,layout));
        EXPECT(stub::state.frames.size()==count);
    }
}

void review_reset_uses_actual_driver_and_rejects_stale_release() {
    stub::reset();HeltecV4Oled port;StartupDisplayOwner owner(port);std::uint64_t lease{};
    EXPECT(owner.start());EXPECT(owner.acquire_enrollment_review(lease));
    opentrail::security_evaluation::EnrollmentReviewLayout layout{};layout.text[0][0]='A';
    EXPECT(owner.render_enrollment_review(lease,1,layout));
    EXPECT(owner.show_factory_reset_confirmation());const auto reset=stub::state.frames.back();
    EXPECT(!owner.release_enrollment_review(lease));EXPECT(stub::state.frames.back()==reset);
    EXPECT(owner.clear_factory_reset_confirmation());EXPECT(stub::state.frames.back()==kTrailStartupLogoSsd1306);
}

int main() {
    review_cells_reach_all_actual_panel_rows();
    review_driver_rejects_clipping_and_conceals_failures();
    review_reset_uses_actual_driver_and_rejects_stale_release();
    unowned_setup_label_remains_after_pairing_timeout();
    metadata_redraw_and_overlays_preserve_latest_clock();
    clock_minute_redraw_survives_unchanged_transport_footer();
    setup_label_uses_actual_pairing_pixels_and_clears();
    real_initialization_and_logo();
    real_port_delivers_presentation_frames();
    draw_failure_conceals_and_latches_port();
    black_write_failure_still_disables_panel_and_power();
    negative_time_is_concealed();
    pairing_clear_and_failure_use_real_owner_and_port();
    rollback_cannot_be_followed_by_pairing_digits();
    if (failures) return 1;
    std::cout << "PASS actual Heltec OLED port: 14 groups\n";
    return 0;
}
