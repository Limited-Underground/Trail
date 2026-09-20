#include <array>
#include <optional>
#include <cstdio>
#include <string_view>
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sodium.h"
#include "entropy_runtime.hpp"
#include "pair_display.hpp"
#include "enrolled_nvs_backend.hpp"
#include "enrolled_radio_driver.hpp"
#include "opentrail/enrolled_bench_session.hpp"

namespace {
using namespace opentrail;
using namespace security_evaluation;
using target::heltec_v4_pair_eval::PairDisplay;
class Authority final : public ConfirmationAuthority {
public:
    ConfirmationSample sample() override {
        const auto us=esp_timer_get_time();
        if(us<0 || static_cast<std::uint64_t>(us)<last_)good_=false;
        if(!good_)return {};
        last_=static_cast<std::uint64_t>(us);return {{1,1},last_/1000};
    }
private:std::uint64_t last_{0};bool good_{true};
};
bool usb_ready=false;
target::heltec_v4_enrolled_eval::EnrolledDiagnostics diagnostics{esp_timer_get_time};
bool diagnostics_emitted=false;
std::optional<EnrolledBenchSession> session;
bool send(std::string_view line){return usb_ready && !line.empty() && line.size()<=768 &&
    usb_serial_jtag_write_bytes(line.data(),line.size(),pdMS_TO_TICKS(100))==static_cast<int>(line.size());}
void emit_diagnostics(){
    if(diagnostics_emitted || !usb_ready || !diagnostics.frozen())return;
    diagnostics_emitted=true;std::array<char,256> line{};
    const auto bytes=diagnostics.format(line);
    if(bytes)(void)send({line.data(),bytes});
    std::array<char,512> trace{};const auto trace_bytes=diagnostics.format_trace(trace);
    if(trace_bytes)(void)send({trace.data(),trace_bytes});
}
[[noreturn]] void stopped(PairDisplay* display=nullptr,EnrolledFault fault=EnrolledFault::session_protocol){
    diagnostics.fault(fault);
    const bool cleaned=!session || session->close();
    if(display)(void)display->show_state(EndpointState::refused);
    emit_diagnostics();
    (void)send(cleaned?"OTENROLL1 REFUSED 1\n":"OTENROLL1 REFUSED 0\n");
    // No further storage, RF or protocol mutation after a failed attempt.
    for(;;)vTaskDelay(pdMS_TO_TICKS(100));
}
}
extern "C" void app_main(){
    usb_serial_jtag_driver_config_t usb{};usb.tx_buffer_size=1024;usb.rx_buffer_size=1024;
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK)stopped(nullptr,EnrolledFault::preflight);
    usb_ready=true;
    if(nvs_flash_init()!=ESP_OK)stopped(nullptr,EnrolledFault::storage); // No erase-to-repair fallback.
    static PairDisplay display;if(!display.initialize())stopped(nullptr,EnrolledFault::display);
    gpio_config_t button{};button.pin_bit_mask=1ULL<<GPIO_NUM_0;button.mode=GPIO_MODE_INPUT;
    button.pull_up_en=GPIO_PULLUP_ENABLE;button.pull_down_en=GPIO_PULLDOWN_DISABLE;button.intr_type=GPIO_INTR_DISABLE;
    if(gpio_config(&button)!=ESP_OK)stopped(&display,EnrolledFault::preflight);
    static target::heltec_v4_security_eval::EntropyRuntime entropy;
    if(!entropy.start() || sodium_init()<0)stopped(&display,EnrolledFault::entropy);
    static target::heltec_v4_enrolled_eval::EnrolledNvsBackend backend{&diagnostics};
    if(!backend.ready())stopped(&display,EnrolledFault::storage);
    static GenerationLedgerStorage ledger(backend);
    static SessionGenerationAllocator allocator(ledger,backend,backend.kMaximumGeneration);
    static std::optional<GenerationEvaluationBackend> generation_backend;
    static std::optional<EvaluationStorageBank> bank;
    static std::optional<EnrollmentEvidenceStore> evidence;
    static Authority authority;
    static target::heltec_v4_enrolled_eval::EnrolledRadioDriver radio{&diagnostics}; // Inert until explicit RADIO.
    static std::array<char,700> line{};
    std::array<char,768> output{};
    std::size_t used=0,precontrol_bytes=0;bool discarded=false,admitted=false,previous_button_pressed=false;
    const auto sample=authority.sample();if(!sample.context.transport_generation)stopped(&display,EnrolledFault::authority_clock);
    const int size=std::snprintf(output.data(),output.size(),"OTENROLL1 READY 1 %llu\n",static_cast<unsigned long long>(sample.now_ms));
    if(size<=0 || static_cast<std::size_t>(size)>=output.size() || !send({output.data(),static_cast<std::size_t>(size)}))stopped(&display,EnrolledFault::output);
    for(;;){
        // Sample edges even while a partial command arrives without idle gaps.
        // Stable levels require no durable readback for individual UART bytes.
        const bool button_pressed=gpio_get_level(GPIO_NUM_0)==0;
        const bool button_changed=button_pressed!=previous_button_pressed;
        previous_button_pressed=button_pressed;
        if(session && button_changed && !session->tick(button_pressed))stopped(&display);
        char value=0;const auto count=usb_serial_jtag_read_bytes(&value,1,pdMS_TO_TICKS(10));
        if(count<0 || count>1)stopped(&display,EnrolledFault::input);
        if(count==0){
            if(session && !session->tick(gpio_get_level(GPIO_NUM_0)==0))stopped(&display);
            vTaskDelay(1);continue;
        }
        if(!admitted && ++precontrol_bytes>4096)stopped(&display,EnrolledFault::input);
        if(value=='\n'){
            // A review tick reads durable authority. Service once per bounded
            // command or idle read, rather than multiplying work by UART bytes.
            if(session && !session->tick(gpio_get_level(GPIO_NUM_0)==0))stopped(&display);
            const auto command=std::string_view(line.data(),used);
            if(!admitted){
                if(!discarded && command=="OTENROLL1 HELLO"){
                    std::uint64_t generation=0;
                    if(!allocator.initialize() || !allocator.allocate(generation))stopped(&display);
                    generation_backend.emplace(allocator,backend,generation);
                    bank.emplace(*generation_backend);
                    evidence.emplace(*bank->get(EvaluationNamespace::enrollment));
                    session.emplace(entropy.random(),*bank,*evidence,authority,display,&radio,&diagnostics);
                    admitted=true;
                }else{
                    line.fill(0);used=0;discarded=false;
                    if(precontrol_bytes>=4096)stopped(&display,EnrolledFault::input);
                    vTaskDelay(1);continue;
                }
            }
            std::size_t bytes=0;
            if(discarded)stopped(&display,EnrolledFault::input);
            if(!session->command(command,output,bytes) || !bytes)stopped(&display);
            if(std::string_view(output.data(),bytes).find("OTENROLL1 CLOSED ")==0)emit_diagnostics();
            if(!send({output.data(),bytes}))stopped(&display,EnrolledFault::output);
            line.fill(0);used=0;discarded=false;output.fill(0);
            // Yield after each bounded admitted command, not each buffered byte.
            vTaskDelay(1);
        }else if(!discarded){
            if(value<32 || value>126 || used==line.size()){
                if(admitted)stopped(&display,EnrolledFault::input);
                line.fill(0);used=0;discarded=true;
            }else line[used++]=value;
        }
        if(!admitted && precontrol_bytes>=4096)stopped(&display,EnrolledFault::input);
        if(!admitted)vTaskDelay(1);
    }
}
