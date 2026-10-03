#pragma once
#include "driver/usb_serial_jtag.h"
#include "startup_diagnostic_port.hpp"
#include <array>
#include <cstdio>
#include <cstdint>
#ifndef OT_CANDIDATE_STARTUP_DIAGNOSTICS
#define OT_CANDIDATE_STARTUP_DIAGNOSTICS 0
#endif
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/usb_serial_jtag_ll.h"
#endif

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
// Opt-in evaluation diagnostics, never a control reply or readiness authority.
// Stage: 0 application loop, 1 USB, 2 default NVS, 3 reset marker, 4 store,
// 5 sodium, 6 display, 7 input, 8 layout/storage, 9 runtime initialization.
// 10..17 store startup/cleanup, 18..24 runtime initialization,
// 25..31 fixed occupancy failure categories; see the exact emitting call sites.
// Phase: 0 enter, 1 returned/passed the emitting check (stage 13 return only),
// 2 stopped, 3 first service returned,
// 4 first command byte, 5 first reply attempt, 6 first line dispatch entered.
// No private bytes, new RTC/NVS writes, tasks, retries or blocking diagnostic IO.
class StartupDiagnostics final {
public:
    StartupDiagnostics() {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        // This ROM API reads the SoC register. esp_reset_reason() would pull in
        // the SDK hint-clearing constructor and alter RTC persistence behavior.
        reset_=normalize_reset(static_cast<unsigned>(esp_rom_get_reset_reason(0)));
#endif
    }
    void driver_installed() noexcept {driver_ready_=true;}
    bool driver_ready() const noexcept {return driver_ready_;}
    StartupDiagnosticPort port() noexcept {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        return {this,[](void* context,unsigned stage,unsigned phase){static_cast<StartupDiagnostics*>(context)->mark(stage,phase);}};
#else
        return {};
#endif
    }
    void mark(unsigned stage,unsigned phase) {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(stage==stage_ && phase==phase_)return;
        stage_=stage;phase_=phase;
        if(!input_seen_)publish(stage,phase);
#else
        (void)stage;(void)phase;
#endif
    }
    void stopped(unsigned stage) {mark(stage,2);arm_replay();}
    void transport_failed() {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(!reply_completed_ && !transport_failure_seen_){transport_failure_seen_=true;stage_=1;phase_=2;publish(1,2,input_seen_);}
#endif
    }
    void reply_completed() noexcept {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        reply_completed_=true;
#endif
    }
    void loop_enter() {mark(0,0);}
    void loop_returned() {mark(0,3);arm_replay();}
    void command_byte(char byte) {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(!input_seen_){input_seen_=true;publish(0,4);}
        if(byte=='\n' && !line_seen_ && !reply_seen_){line_seen_=true;publish(0,6);}
#else
        (void)byte;
#endif
    }
    void reply_attempt() {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(!reply_seen_){reply_seen_=true;publish(0,5);}
#endif
    }
    void replay() {
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(input_seen_ || replays_==32 || next_us_<=0)return;
        const auto now=esp_timer_get_time();
        if(now<next_us_ || now>INT64_MAX-1'000'000)return;
        ++replays_;publish(stage_,phase_);next_us_=now+1'000'000;
#endif
    }
private:
    bool driver_ready_{};
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
    unsigned reset_{},stage_{10},phase_{7},replays_{};
    bool input_seen_{},line_seen_{},reply_seen_{},reply_completed_{},transport_failure_seen_{};
    std::int64_t next_us_{};
    static unsigned normalize_reset(unsigned value) noexcept {
        switch(value) {
        case 1:case 3:case 5:case 7:case 8:case 9:case 11:case 12:case 13:
        case 15:case 16:case 17:case 18:case 19:case 20:case 21:case 22:case 23:return value;
        default:return 0;
        }
    }
    void arm_replay() {
        if(next_us_ || input_seen_)return;
        const auto now=esp_timer_get_time();
        if(now>0 && now<=INT64_MAX-1'000'000)next_us_=now+1'000'000;
    }
    void publish(unsigned stage,unsigned phase,bool resync=false) {
        if(reply_completed_)return;
        std::array<char,32> record{};
        // The driverless path starts a fresh line after a possibly partial FIFO
        // write. FIFO room is not a full-record guarantee: loss stays unknown.
        const unsigned prefix=driver_ready_ && !resync?0:1;if(prefix)record[0]='\n';
        const int n=std::snprintf(record.data()+prefix,record.size()-prefix,
            "OTBOOT1 %u %u %u\n",stage,phase,reset_);
        if(n<=0 || static_cast<std::size_t>(n)>=record.size()-prefix)return;
        const auto size=static_cast<std::size_t>(n)+prefix;
        if(driver_ready_){(void)usb_serial_jtag_write_bytes(record.data(),size,0);return;}
        if(!usb_serial_jtag_ll_txfifo_writable())return;
        const auto written=usb_serial_jtag_ll_write_txfifo(
            reinterpret_cast<const std::uint8_t*>(record.data()),static_cast<std::uint32_t>(size));
        if(written)usb_serial_jtag_ll_txfifo_flush();
    }
#else
    void arm_replay() noexcept {}
#endif
};
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
