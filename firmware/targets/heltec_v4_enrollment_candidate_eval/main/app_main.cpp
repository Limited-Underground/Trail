#include "candidate_runtime.hpp"
#include "candidate_store_runtime.hpp"
#include "heltec_v4_oled.hpp"
#include "startup_diagnostics.hpp"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <optional>
#include <cstdio>

namespace {
using namespace opentrail;
using namespace target::heltec_v4_enrollment_candidate_eval;
bool send(const CandidateRuntime::Output& output,std::size_t size,StartupDiagnostics& diagnostics) {
    diagnostics.reply_attempt();
    const bool ok=size && size<=output.size() && usb_serial_jtag_write_bytes(output.data(),size,pdMS_TO_TICKS(100))==static_cast<int>(size);
    if(ok)diagnostics.reply_completed();else diagnostics.transport_failed();return ok;
}
[[noreturn]] void stopped(unsigned stage,StartupDiagnostics& diagnostics) {
    constexpr char refused[]="OTCAND1 REFUSED\n";
    // Fixed first-failure stage, retained at the failed check. No driver retry,
    // private bytes, post-failure clock sample or historical cause inference.
    diagnostics.stopped(stage);
    std::array<char,32> line{};std::size_t used=0;bool malformed=false;
    for(;;) {
        diagnostics.replay();
        // SDK read_bytes dereferences its installed driver object. A failed
        // installation must never enter it; diagnostics may use the FIFO only.
        if(!diagnostics.driver_ready()){vTaskDelay(1);continue;}
        std::array<char,256> chunk{};
        const int count=usb_serial_jtag_read_bytes(chunk.data(),chunk.size(),1);
        for(int index=0;index<count && static_cast<std::size_t>(index)<chunk.size();++index) {
            const char byte=chunk[static_cast<std::size_t>(index)];diagnostics.command_byte(byte);
            if(byte=='\n') {
                if(!malformed && std::string_view(line.data(),used)=="OTCAND1 BOOTSTATUS") {
                    std::array<char,48> response{};const int size=std::snprintf(response.data(),response.size(),"OTCAND1 BOOTSTATUS %u\n",stage);
                    if(size>0 && static_cast<std::size_t>(size)<response.size()){diagnostics.reply_attempt();const int written=usb_serial_jtag_write_bytes(response.data(),size,pdMS_TO_TICKS(100));if(written==size)diagnostics.reply_completed();else diagnostics.transport_failed();}
                } else {diagnostics.reply_attempt();const int written=usb_serial_jtag_write_bytes(refused,sizeof(refused)-1,pdMS_TO_TICKS(100));if(written==static_cast<int>(sizeof(refused)-1))diagnostics.reply_completed();else diagnostics.transport_failed();}
                used=0;malformed=false;
            } else {
                if(byte<32 || byte>126 || used==line.size())malformed=true;
                else if(!malformed)line[used++]=byte;
            }
        }
        vTaskDelay(1);
    }
}
}
extern "C" void app_main() {
    static StartupDiagnostics diagnostics;
    diagnostics.mark(1,0);
    usb_serial_jtag_driver_config_t config{};config.tx_buffer_size=4096;config.rx_buffer_size=4096;
    if(usb_serial_jtag_driver_install(&config)!=ESP_OK)stopped(1,diagnostics);
    diagnostics.driver_installed();diagnostics.mark(1,1);
    diagnostics.mark(8,0);
    if(!CandidateNvsStorage::layout_ok())stopped(8,diagnostics);
    diagnostics.mark(8,1);diagnostics.mark(2,0);
    if(nvs_flash_init()!=ESP_OK)stopped(2,diagnostics);
    diagnostics.mark(2,1);diagnostics.mark(3,0);
    // Marker first: even creating the candidate RW namespace is prohibited on
    // pending/ambiguous reset. No automatic cleanup or original runtime starts.
    static targets::heltec_v4_bench::HeltecV4FactoryResetMarkerStorage marker;
    const auto initial=marker.load();
    if(initial.error!=companion::DeviceFactoryResetPortError::none || initial.state!=companion::DeviceFactoryResetMarkerState::absent || initial.reset_receipt)stopped(3,diagnostics);
    diagnostics.mark(3,1);diagnostics.mark(4,0);
    static targets::heltec_v4_bench::HeltecV4FactoryResetUserDomainStorage original_user;
    static targets::heltec_v4_bench::HeltecV4FactoryResetNimbleBondStorage bonds;
    static CandidateStoreRuntime store(bonds,diagnostics.port());
    if(!store.start()){diagnostics.stopped(4);(void)store.stop();stopped(4,diagnostics);}
    diagnostics.mark(4,1);diagnostics.mark(5,0);
    if(sodium_init()<0){diagnostics.stopped(5);(void)store.stop();stopped(5,diagnostics);}
    diagnostics.mark(5,1);diagnostics.mark(6,0);
    static target::heltec_v4_bench::HeltecV4Oled oled;
    static target::heltec_v4_bench::StartupDisplayOwner display(oled);
    static target::heltec_v4_bench::HeltecEnrollmentInputArbiter input(display);
    if(!display.start()){diagnostics.stopped(6);(void)store.stop();stopped(6,diagnostics);}
    diagnostics.mark(6,1);diagnostics.mark(7,0);
    if(!input.initialize()){diagnostics.stopped(7);(void)store.stop();stopped(7,diagnostics);}
    diagnostics.mark(7,1);diagnostics.mark(8,0);
    static CandidateNvsStorage storage;
    if(!storage.ready()){diagnostics.stopped(8);(void)store.stop();stopped(8,diagnostics);}
    diagnostics.mark(8,1);diagnostics.mark(9,0);
    static CandidateRuntime runtime(display,input,store.random(),storage,marker,original_user,bonds,diagnostics.port());
    if(!runtime.initialize()){diagnostics.stopped(9);(void)runtime.close();(void)store.stop();stopped(9,diagnostics);}
    diagnostics.mark(9,1);
    static CandidateRuntime::Output output{};
    // HELLO owns the only READY response; an opt-in diagnostic notice
    // cannot masquerade as a fresh command acknowledgement.
    diagnostics.loop_enter();bool first_service=true;
    for(;;) {
        (void)runtime.service(); // Terminal enrollment still services real reset.
        if(first_service){diagnostics.loop_returned();first_service=false;}
        diagnostics.replay();
        std::array<char,256> chunk{};
        const int read=usb_serial_jtag_read_bytes(chunk.data(),chunk.size(),1);
        if(read<0){diagnostics.transport_failed();diagnostics.stopped(1);(void)runtime.close();(void)store.stop();stopped(1,diagnostics);}
        if(static_cast<std::size_t>(read)>chunk.size()){diagnostics.transport_failed();diagnostics.stopped(1);(void)runtime.close();(void)store.stop();stopped(1,diagnostics);}
        for(int index=0;index<read;++index) {
            const char byte=chunk[static_cast<std::size_t>(index)];diagnostics.command_byte(byte);
            std::size_t size=0;(void)runtime.receive(byte,output,size);
            if(size && !send(output,size,diagnostics)){diagnostics.stopped(1);(void)runtime.close();(void)store.stop();stopped(1,diagnostics);}
        }
        // At 100 Hz pdMS_TO_TICKS(1) is zero. Bound the chunk and yield at
        // least one actual tick, rather than adding a tick for every proof byte.
        vTaskDelay(1);
    }
}
