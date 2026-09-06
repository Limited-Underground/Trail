#include "opentrail/oled_presentation.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <cstring>
using namespace opentrail::ui::oled_presentation;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; ++failures; } } while(false)
std::string row(const Frame& f, std::size_t i) { return {f.rows[i].data()}; }
bool has(const Frame& f, const std::string& s) { for (std::size_t i=0;i<kRows;++i) if(row(f,i).find(s)!=std::string::npos) return true; return false; }
void bounds(const Frame& f) {
    for (const auto& line:f.rows) {
        CHECK(line.back()=='\0');
        CHECK(std::strlen(line.data())<=kCharacters);
    }
    for (std::size_t page=0;page<8;++page) for(std::size_t x=0;x<128;++x) {
        CHECK((f.pixels[page*128+x]&0x80)==0);
        if (x>=125 || x%6==5) CHECK(f.pixels[page*128+x]==0);
    }
}
Snapshot normal() {
    Snapshot s; s.region_configured=true; s.region_label="US915"; s.device_name="Trail One";
    return s;
}
Snapshot pairing() {
    auto s=normal(); s.pairing={true,100,60100,{'1','3','5','7','9','0'},"TEST UNIT"};return s;
}
void priority_and_concealment() {
    for (unsigned flags=0;flags<16;++flags) {
        auto s=pairing();s.failure=(flags&1)!=0;s.reset_in_progress=(flags&2)!=0;
        s.reset_confirmation=(flags&4)!=0;s.pairing.active=(flags&8)!=0;
        auto f=render(s,1000);bounds(f);
        const auto wanted=s.failure?Surface::failure:s.reset_in_progress?Surface::reset_progress:
            s.reset_confirmation?Surface::reset_confirmation:s.pairing.active?Surface::pairing:Surface::normal;
        CHECK(f.surface==wanted);CHECK(has(f,"135790")== (wanted==Surface::pairing));
        if(wanted!=Surface::normal) CHECK(!has(f,"TRAIL ONE"));
    }
    auto s=pairing();auto before=render(s,60099);CHECK(has(before,"135790"));
    auto after=render(s,60100);CHECK(after.surface==Surface::normal);CHECK(!has(after,"135790"));
    CHECK(after.pixels==render(normal(),60100).pixels); // Entire fresh frame, no old PIN pixels.
    CHECK(render(s,99).surface==Surface::normal);
    s.pairing.deadline_ms=60101;CHECK(render(s,100).surface==Surface::normal);
    s=pairing();s.pairing.digits[2]='X';CHECK(!has(render(s,100),"135790"));CHECK(render(s,100).surface==Surface::normal);
    s=pairing();s.pairing.deadline_ms=s.pairing.start_ms;CHECK(render(s,100).surface==Surface::normal);
    s=pairing();s.pairing.start_ms=std::numeric_limits<std::uint64_t>::max()-100;
    s.pairing.deadline_ms=std::numeric_limits<std::uint64_t>::max();
    CHECK(render(s,s.pairing.start_ms).surface==Surface::pairing);
    CHECK(render(s,s.pairing.deadline_ms).surface==Surface::normal);
}
void rollback_containment() {
    PresentationOwner owner;auto s=pairing();
    CHECK(has(owner.present(s,1000),"135790"));
    CHECK(!has(owner.present(s,60100),"135790"));
    const auto rollback=owner.present(s,1000);
    CHECK(rollback.surface==Surface::failure);CHECK(!has(rollback,"135790"));
    CHECK(owner.present(s,70000).surface==Surface::failure);
    CHECK(owner.present(normal(),80000).surface==Surface::failure);
    PresentationOwner fresh;CHECK(fresh.present(normal(),0).surface==Surface::normal);
}
void region_and_unknown_authority() {
    Snapshot s;auto f=render(s,0);bounds(f);CHECK(f.surface==Surface::region_required);
    CHECK(!has(f,"READY"));s=normal();f=render(s,0);CHECK(f.surface==Surface::normal);
    CHECK(!has(f,"READY"));CHECK(has(f,"TX OFF"));CHECK(has(f,"--:--"));
    s.phone=PhoneState::ready;CHECK(has(render(s,0),"PHONE READY"));
    s.phone=static_cast<PhoneState>(255);CHECK(!has(render(s,0),"READY"));
    s.group=static_cast<GroupState>(255);s.gps_fix=static_cast<GpsFix>(255);bounds(render(s,0));
}
void metric_and_activity_edges() {
    auto s=normal();s.battery={true,100,100};s.satellites={true,99,100};
    s.gps_fix=GpsFix::fix;s.gps_sampled_at_ms=100;s.tx={true,100};s.rx={true,100};
    CHECK(has(render(s,100),"100%"));CHECK(has(render(s,100),"99"));
    CHECK(has(render(s,1099),"RADIO TX RX"));CHECK(has(render(s,1100),"RADIO --"));
    CHECK(!has(render(s,99),"100%"));CHECK(!has(render(s,99),"RADIO TX"));
    CHECK(has(render(s,30099),"100%"));CHECK(!has(render(s,30100),"100%"));
    CHECK(!has(render(s,30100),"GPS FIX"));
    s.battery.value=101;s.satellites.value=100;CHECK(!has(render(s,100),"101%"));CHECK(!has(render(s,100),"GPS:100"));
    s.battery={true,75,std::numeric_limits<std::uint64_t>::max()-10};
    CHECK(has(render(s,std::numeric_limits<std::uint64_t>::max()),"75%"));
}
void sanitization_and_pixel_bounds() {
    auto s=normal();s.device_name="abcdefghijklmnopqrstuvwxyz";
    auto f=render(s,0);CHECK(row(f,0)=="ABCDEFGHIJKLMNOPQRST~");CHECK(f.clipped);bounds(f);
    std::string hostile="ab\n";hostile.push_back(static_cast<char>(0xff));hostile+='z';s.device_name=hostile;
    f=render(s,0);CHECK(row(f,0)=="AB??Z");bounds(f);
    s.device_name="";f=render(s,0);CHECK(row(f,0)=="DEVICE");
    CHECK(f.pixels[0]==0x7f);CHECK(f.pixels[1]==0x41);CHECK(f.pixels[4]==0x1c); // Known 5x7 D glyph.
    for(unsigned n=0;n<256;++n) {
        std::string label(80,static_cast<char>(n));s.device_name=label;s.region_label=label;
        s.group=static_cast<GroupState>(n);s.phone=static_cast<PhoneState>(n);s.gps_fix=static_cast<GpsFix>(n);
        bounds(render(s,0));
    }
}
void clock_composition() {
    opentrail::time::OledClock clock;auto s=normal();s.clock=clock.observe(0);CHECK(has(render(s,0),"--:--"));
    CHECK(clock.synchronize(13*3600+34*60,opentrail::time::OledClockFormat::hour_12,10,10,true));
    s.clock=clock.observe(10);CHECK(has(render(s,10),"01:34 PM"));
    s.clock=clock.observe(86'400'010);CHECK(has(render(s,86'400'010),"--:--"));
    s.clock.valid=true;s.clock.text.fill('X');s.clock.local_second_of_day=999999;
    CHECK(has(render(s,0),"--:--"));bounds(render(s,0));
}
void write_pbm(const Frame& f,const std::string& path) {
    std::ofstream o(path);o<<"P1\n128 64\n";
    for(std::size_t y=0;y<64;++y) {for(std::size_t x=0;x<128;++x)o<<((f.pixels[(y/8)*128+x]>>(y%8))&1)<<' ';o<<'\n';}
}
}
int main(int argc,char**argv) {
    priority_and_concealment();rollback_containment();region_and_unknown_authority();metric_and_activity_edges();sanitization_and_pixel_bounds();clock_composition();
    if(failures) return EXIT_FAILURE;
    if(argc==2) {
        auto s=normal();s.phone=PhoneState::ready;s.group=GroupState::administrator;s.location_available=true;s.location_on=true;
        s.tx_available=true;s.battery={true,87,1000};s.satellites={true,12,1000};s.gps_fix=GpsFix::fix;s.gps_sampled_at_ms=1000;s.tx={true,1000};s.rx={true,1000};
        opentrail::time::OledClock clock;CHECK(clock.synchronize(13*3600+34*60,opentrail::time::OledClockFormat::hour_12,1000,1000,true));s.clock=clock.observe(1000);
        const std::string dir=argv[1];write_pbm(render(s,1000),dir+"/normal.pbm");write_pbm(render(normal(),1000),dir+"/unknown.pbm");
        s=pairing();write_pbm(render(s,1000),dir+"/pairing-synthetic.pbm");s.reset_confirmation=true;write_pbm(render(s,1000),dir+"/reset-confirm.pbm");
        s.reset_in_progress=true;write_pbm(render(s,1000),dir+"/reset-progress.pbm");s.failure=true;write_pbm(render(s,1000),dir+"/failure.pbm");
        write_pbm(render(Snapshot{},1000),dir+"/region-required.pbm");
    }
    std::cout<<"PASS: 6 OLED presentation groups (priority/expiry, authority, freshness, byte/pixel bounds, clock composition)\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
