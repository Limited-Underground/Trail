#include "candidate_runtime.hpp"
#include "candidate_store_runtime.hpp"
#include "heltec_v4_oled.hpp"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <optional>
#include <cstdio>

namespace {
using namespace opentrail;
using namespace target::heltec_v4_enrollment_candidate_eval;
bool send(const CandidateRuntime::Output& output,std::size_t size) {
    return size && size<=output.size() && usb_serial_jtag_write_bytes(output.data(),size,pdMS_TO_TICKS(100))==static_cast<int>(size);
}
[[noreturn]] void stopped(unsigned stage) {
    constexpr char refused[]="OTCAND1 REFUSED\n";
    // Fixed first-failure stage, retained at the failed check. No driver retry,
    // private bytes, post-failure clock sample or historical cause inference.
    std::array<char,32> line{};std::size_t used=0;bool malformed=false;
    for(;;) {
        char byte{};const int read=usb_serial_jtag_read_bytes(&byte,1,pdMS_TO_TICKS(10));
        if(read==1 && byte=='\n') {
            if(!malformed && std::string_view(line.data(),used)=="OTCAND1 BOOTSTATUS") {
                std::array<char,48> response{};const int size=std::snprintf(response.data(),response.size(),"OTCAND1 BOOTSTATUS %u\n",stage);
                if(size>0 && static_cast<std::size_t>(size)<response.size())(void)usb_serial_jtag_write_bytes(response.data(),size,pdMS_TO_TICKS(100));
            } else (void)usb_serial_jtag_write_bytes(refused,sizeof(refused)-1,pdMS_TO_TICKS(100));
            used=0;malformed=false;
        } else if(read==1) {
            if(byte<32 || byte>126 || used==line.size())malformed=true;
            else if(!malformed)line[used++]=byte;
        }
        vTaskDelay(1);
    }
}
}
extern "C" void app_main() {
    usb_serial_jtag_driver_config_t config{};config.tx_buffer_size=4096;config.rx_buffer_size=4096;
    if(usb_serial_jtag_driver_install(&config)!=ESP_OK)stopped(1);
    if(!CandidateNvsStorage::layout_ok())stopped(8);
    if(nvs_flash_init()!=ESP_OK)stopped(2);
    // Marker first: even creating the candidate RW namespace is prohibited on
    // pending/ambiguous reset. No automatic cleanup or original runtime starts.
    static targets::heltec_v4_bench::HeltecV4FactoryResetMarkerStorage marker;
    const auto initial=marker.load();
    if(initial.error!=companion::DeviceFactoryResetPortError::none || initial.state!=companion::DeviceFactoryResetMarkerState::absent || initial.reset_receipt)stopped(3);
    static targets::heltec_v4_bench::HeltecV4FactoryResetUserDomainStorage original_user;
    static targets::heltec_v4_bench::HeltecV4FactoryResetNimbleBondStorage bonds;
    static CandidateStoreRuntime store(bonds);
    if(!store.start()){(void)store.stop();stopped(4);}
    if(sodium_init()<0){(void)store.stop();stopped(5);}
    static target::heltec_v4_bench::HeltecV4Oled oled;
    static target::heltec_v4_bench::StartupDisplayOwner display(oled);
    static target::heltec_v4_bench::HeltecEnrollmentInputArbiter input(display);
    if(!display.start()){(void)store.stop();stopped(6);}
    if(!input.initialize()){(void)store.stop();stopped(7);}
    static CandidateNvsStorage storage;
    if(!storage.ready()){(void)store.stop();stopped(8);}
    static CandidateRuntime runtime(display,input,store.random(),storage,marker,original_user,bonds);
    if(!runtime.initialize()){(void)runtime.close();(void)store.stop();stopped(9);}
    static CandidateRuntime::Output output{};
    // Quiet startup. HELLO owns the only READY response; a queued boot notice
    // cannot masquerade as a fresh command acknowledgement.
    for(;;) {
        (void)runtime.service(); // Terminal enrollment still services real reset.
        std::array<char,256> chunk{};
        const int read=usb_serial_jtag_read_bytes(chunk.data(),chunk.size(),1);
        if(read<0){(void)runtime.close();(void)store.stop();stopped(1);}
        if(static_cast<std::size_t>(read)>chunk.size()){(void)runtime.close();(void)store.stop();stopped(1);}
        for(int index=0;index<read;++index) {
            std::size_t size=0;(void)runtime.receive(chunk[static_cast<std::size_t>(index)],output,size);
            if(size && !send(output,size)){(void)runtime.close();(void)store.stop();stopped(1);}
        }
        // At 100 Hz pdMS_TO_TICKS(1) is zero. Bound the chunk and yield at
        // least one actual tick, rather than adding a tick for every proof byte.
        vTaskDelay(1);
    }
}
