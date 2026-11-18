// I-073 composed bench path: the real app permission callback, coordinator and
// HeltecV4Lora. Only session, SDK clock and SX1262 I/O are simulated.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>
#include "heltec_v4_lora.hpp"
#include "lora_link_test.hpp"
#include "esp_timer.h"
#include "opentrail/pairing_coordinator.hpp"
using namespace opentrail::protocol::pairing;
using opentrail::target::heltec_v4_bench::HeltecV4Lora;
namespace link_test = opentrail::target::heltec_v4_bench::lora_link_test;
namespace mock = radio_mock;
int failures = 0;
int groups = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while(false)
struct Session {
    int role;
    int peer;
    std::array<int,15> produced{}, consumed{};
    std::uint64_t deadline = ~std::uint64_t{0};
    static std::uint8_t value(int who, FrameType type, int instance, std::size_t i) {
        return static_cast<std::uint8_t>(who*101 + static_cast<int>(type)*17 + instance*7 + i);
    }
    bool produce(FrameType type, std::uint8_t* out) {
        const int n=++produced[static_cast<unsigned>(type)];
        for(std::size_t i=0;i<payload_bytes(type);++i) out[i]=value(role,type,n,i);
        return true;
    }
    bool consume(FrameType type, const std::uint8_t* in) {
        const int n=++consumed[static_cast<unsigned>(type)];
        for(std::size_t i=0;i<payload_bytes(type);++i) if(in[i]!=value(peer,type,n,i)) return false;
        return true;
    }
    LocalResult local(LocalStep, std::uint64_t) { return LocalResult::done; }
    bool remaining_ms(std::uint64_t now, std::uint64_t& left) const {
        left=now>=deadline?0:deadline-now;return true;
    }
};
using Coordinator = PairingCoordinator<Session>;
// Baseline compatibility only: the old coordinator exposes no launch query.
// The ACTUAL baseline app callback below does not consult this fallback at all.
template<class C, class=void> struct LaunchQuery {
    static bool allowed(C&,std::uint64_t) { return true; }
};
template<class C> struct LaunchQuery<C,std::void_t<decltype(std::declval<C&>().queued_launch_allowed(std::uint64_t{}))>> {
    static bool allowed(C& c,std::uint64_t now) { return c.queued_launch_allowed(now); }
};
struct Runtime {
    Coordinator* coordinator=nullptr;
    bool queued_launch_allowed(std::uint64_t now) {
        return coordinator && LaunchQuery<Coordinator>::allowed(*coordinator,now);
    }
};
Runtime g_board_pairing;
bool g_pairing_frame_queued=false;
unsigned g_region_id=1;
namespace opentrail::target::heltec_v4_bench {
struct TestRegion { unsigned selection_id; };
TestRegion companion_configuration_region() { return {g_region_id}; }
}
// Generated verbatim from app_main.cpp by the Python runner, not a test copy.
#include "app_pairing_permission.inc"
struct Random final : opentrail::security::SecureRandomSource {
    opentrail::security::EntropyState state() const override { return opentrail::security::EntropyState::ready; }
    opentrail::security::RandomFillResult fill(std::uint8_t* out,std::size_t n) override {
        if(!out || n!=4) return {opentrail::security::RandomFillError::invalid_argument,0};
        std::memset(out,0,n);return {opentrail::security::RandomFillError::none,n};
    }
};
FrameType frame_type(const FrameBuffer& frame) { return static_cast<FrameType>(frame[2]&0x0f); }
bool select_copy(Coordinator& a,Coordinator& b,Session& session,FrameType target,bool resend,
                 std::uint64_t floor,std::uint64_t slack,FrameBuffer& selected,std::size_t& bytes,
                 std::uint64_t& admitted) {
    CHECK(a.begin(0) && b.begin(0));
    unsigned copies=0;
    for(std::uint64_t now=0;now<20000;now+=100) {
        FrameBuffer out{};std::size_t n=0;
        if(a.service(now,out,n)) {
            if(frame_type(out)==target) {
                if(++copies==(resend?2U:1U)) {
                    a.sent(false,now);
                    session.deadline=now+floor+slack;
                    if(!a.service(now,out,n) || frame_type(out)!=target) return false;
                    a.sent(true,now);selected=out;bytes=n;admitted=now;return true;
                }
                a.sent(true,now); // Lose prior copy to make a resend necessary.
            } else { a.sent(true,now);b.on_receive(out.data(),n); }
        }
        if(b.service(now,out,n)) {
            b.sent(true,now);
            // Retain A's running budget while testing a Committed resend.
            if(target!=FrameType::committed || frame_type(out)!=FrameType::committed)
                a.on_receive(out.data(),n);
        }
    }
    return false;
}
void case_floor(FrameType target,bool version2,bool resend,unsigned backoffs,bool inside_scan,bool enough) {
    ++groups;
    Session sa{1,2},sb{2,1};Limits limits{};
    limits.peer_confirmed_completion=version2;limits.quiet_after_rx_ms=0;limits.responder_turn_delay_ms=0;
    Coordinator a(sa,true,123456,limits),b(sb,false,123456,limits);
    const auto floor=resend?limits.final_send_floor_ms:limits.final_first_send_floor_ms;
    const std::uint64_t slack=(backoffs || inside_scan)?10:0;
    FrameBuffer selected{};std::size_t bytes=0;std::uint64_t admitted=0;
    const bool ready=select_copy(a,b,sa,target,resend,floor,slack,selected,bytes,admitted);
    CHECK(ready);if(!ready)return;
    mock::reset();Random random;HeltecV4Lora radio{random};g_region_id=1;
    g_board_pairing.coordinator=&a;g_pairing_frame_queued=true;
    radio.set_transmit_permission(&lora_transmit_permitted,nullptr);CHECK(radio.start());
    CHECK(radio.send({selected.data(),bytes},admitted).accepted());
    mock::airtime_us=1000;
    for(unsigned i=0;i<backoffs;++i) {
        mock::now_us=static_cast<std::int64_t>((admitted+2*i)*1000);
        mock::cad_result=RADIOLIB_LORA_DETECTED;
        mock::callback=[](const std::string& op){if(op=="cad_result")mock::now_us+=1000;};
        radio.launch_queued(admitted+2*i);
        CHECK(mock::count("start_tx")==0 && radio.status().transmit_queue_depth==1);
    }
    const auto edge=admitted+slack;
    const auto before=inside_scan?edge-1:edge+(enough?0:1);
    mock::now_us=static_cast<std::int64_t>(before*1000);
    mock::cad_result=RADIOLIB_CHANNEL_FREE;
    mock::callback=[=](const std::string& op){
        if(op=="cad_result" && inside_scan)
            mock::now_us=static_cast<std::int64_t>((edge+(enough?0:1))*1000);
    };
    radio.launch_queued(before); // Timestamp deliberately predates CAD work.
    const bool launched=mock::count("start_tx")!=0;
    const auto drops=radio.counters().tx_prelaunch_dropped;
    unsigned refunds=0;
    if(drops) { refunds+=a.unsent()?1U:0U;CHECK(!a.unsent()); }
    CHECK(launched==enough);
    CHECK(radio.status().transmit_queue_depth==0);
    CHECK(drops==(enough?0U:1U));CHECK(refunds==(enough?0U:1U));
    radio.launch_queued(edge+2);CHECK(radio.counters().tx_prelaunch_dropped==drops);
    std::cout<<"CASE v"<<(version2?2:1)<<" type="<<static_cast<unsigned>(target)
             <<(resend?" resend":" first")<<" backoffs="<<backoffs
             <<(inside_scan?" scan":" wait")<<" enough="<<enough
             <<" launched="<<launched<<" drops="<<drops<<" refunds="<<refunds<<'\n';
    mock::callback={};radio.stop();g_pairing_frame_queued=false;g_board_pairing.coordinator=nullptr;
}
void region_cases() {
    for(unsigned region:{1U,2U}) {
        ++groups;mock::reset();Random random;HeltecV4Lora radio{random};
        g_pairing_frame_queued=false;g_region_id=region;
        radio.set_transmit_permission(&lora_transmit_permitted,nullptr);CHECK(radio.start());
        const std::array<std::uint8_t,3> beacon{1,2,3};CHECK(radio.send({beacon.data(),beacon.size()},100).accepted());
        radio.launch_queued(100);CHECK((mock::count("start_tx")!=0)==(region==1));
        CHECK(radio.counters().tx_prelaunch_dropped==(region==1?0U:1U));radio.stop();
    }
    ++groups;mock::reset();Random random;HeltecV4Lora radio{random};
    g_pairing_frame_queued=false;g_region_id=1;
    radio.set_transmit_permission(&lora_transmit_permitted,nullptr);CHECK(radio.start());
    const std::array<std::uint8_t,3> beacon{1,2,3};CHECK(radio.send({beacon.data(),beacon.size()},100).accepted());
    mock::callback=[](const std::string& op){if(op=="cad_result")g_region_id=2;};
    radio.launch_queued(100);CHECK(mock::count("start_tx")==0 && radio.counters().tx_prelaunch_dropped==1);
    mock::callback={};radio.stop();
}
void uncertain_launch() {
    ++groups;Session sa{1,2},sb{2,1};Limits limits{};limits.quiet_after_rx_ms=0;limits.responder_turn_delay_ms=0;
    Coordinator a(sa,true,123456,limits),b(sb,false,123456,limits);
    FrameBuffer selected{};std::size_t n=0;std::uint64_t now=0;
    CHECK(select_copy(a,b,sa,FrameType::control,false,limits.final_first_send_floor_ms,10,selected,n,now));
    mock::reset();Random random;HeltecV4Lora radio{random};g_region_id=1;g_pairing_frame_queued=true;g_board_pairing.coordinator=&a;
    radio.set_transmit_permission(&lora_transmit_permitted,nullptr);CHECK(radio.start());
    CHECK(radio.send({selected.data(),n},now).accepted());mock::fault="start_tx";mock::now_us=static_cast<std::int64_t>(now*1000);
    radio.launch_queued(now);CHECK(mock::count("start_tx")==1 && radio.counters().tx_prelaunch_dropped==0);
    CHECK(!radio.running());g_pairing_frame_queued=false;g_board_pairing.coordinator=nullptr;
}
int main() {
    for(const bool version2:{false,true})
      for(const auto target:{FrameType::control,FrameType::prepared,FrameType::committed}) {
        if(!version2 && target!=FrameType::control)continue;
        for(const bool resend:{false,true})for(unsigned backoffs=0;backoffs<=3;++backoffs)
          for(const bool scan:{false,true})for(const bool enough:{false,true})
            case_floor(target,version2,resend,backoffs,scan,enough);
      }
    region_cases();uncertain_launch();
    if(failures){std::cerr<<failures<<" failed checks in "<<groups<<" composed launch cases\n";return 1;}
    std::cout<<"PASS "<<groups<<" composed pairing launch groups\n";return 0;
}
