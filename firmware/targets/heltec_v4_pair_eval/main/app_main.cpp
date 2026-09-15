#include <array>
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
#include "pair_nvs_backend.hpp"
#include "startup_ingress.hpp"
#ifdef OT_PAIR_RADIO_EVAL
#include "opentrail/pair_radio_channel.hpp"
#include "pair_radio_driver.hpp"
#endif

namespace {
using namespace opentrail;
using namespace opentrail::security_evaluation;
using namespace opentrail::target::heltec_v4_pair_eval;
class LocalAuthority final : public ConfirmationAuthority {
public:
    ConfirmationSample sample() override {
        const auto us=esp_timer_get_time();
        if (us<0 || static_cast<std::uint64_t>(us)<last_us_) good_=false;
        if (!good_) return {};
        last_us_=static_cast<std::uint64_t>(us);
        // Evaluation USB owner context only; not protected BLE authentication.
        return {{1,1},last_us_/1000};
    }
private:
    std::uint64_t last_us_{0};
    bool good_{true};
};
bool usb_ready=false;
PairDiagnostics diagnostics;
bool send(const char* bytes,std::size_t size) {
    return usb_ready && size>0 && size<=768 &&
        usb_serial_jtag_write_bytes(bytes,size,pdMS_TO_TICKS(100))==static_cast<int>(size);
}
bool send_refused() {
    constexpr char failure[]="OTPAIR1 REFUSED\n";
    return send(failure,sizeof(failure)-1);
}
bool send_diagnostics() {
    std::array<char,96> output{};
    std::size_t size=0;
    return diagnostics.format(output,size) && send(output.data(),size);
}
void record_failure(PairStage stage,PairError error,PairDisplay* display,
                    PairBenchSession* session=nullptr,bool close_session=true) {
    const bool first=diagnostics.latch(stage,error,
        session?PairCleanup::uncertain:PairCleanup::not_attempted);
    if(session) {
        const bool closed=!close_session || session->close();
        const bool cleared=closed && session->cleanup_ok() && session->secrets_cleared();
        if(first) diagnostics.finish_cleanup(cleared?PairCleanup::confirmed_cleared:PairCleanup::uncertain);
    }
    if(display) (void)display->show_failure(diagnostics.stage(),diagnostics.error());
}
[[noreturn]] void stopped(PairStage stage,PairError error,PairDisplay* display=nullptr,
                         PairBenchSession* session=nullptr,bool discard_initial=false) {
    // Latch before cleanup or presentation; neither may replace the cause.
    record_failure(stage,error,display,session);
    (void)send_refused();
    std::array<char,700> line{};
    std::size_t used=0;
    bool discarded=discard_initial;
    for (;;) {
        if(!usb_ready) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        char value=0;
        const auto count=usb_serial_jtag_read_bytes(&value,1,pdMS_TO_TICKS(10));
        if(count<=0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if(value=='\n') {
            const bool query=!discarded && std::string_view(line.data(),used)=="OTPAIR1 DIAG";
            // Only the exact diagnostic query is admitted after a fatal error.
            // No startup operation, protocol session, reset or storage write is retried.
            if(query) (void)send_diagnostics(); else (void)send_refused();
            line.fill(0); used=0; discarded=false;
        } else if(!discarded) {
            if(value<32 || value>126 || used==line.size()) {
                line.fill(0); used=0; discarded=true;
            } else line[used++]=value;
        }
        // A continuously readable unterminated/noisy stream must still let
        // the idle/watchdog task run, including while discarding an overflow.
        vTaskDelay(1);
    }
}
enum class BlankCheck { blank, read_failed, retained };
BlankCheck blank(persistence::PersistentStorage& storage) {
    std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
    for(std::size_t slot=0;slot<persistence::kPersistentSlotCount;++slot) {
        const auto read=storage.read_slot(persistence::StorageDomain::outbound_counter_state,
                                        slot,{bytes.data(),bytes.size()});
        if(!read.read() || read.bytes_read!=bytes.size()) return BlankCheck::read_failed;
        for(auto byte:bytes) if(byte!=0xff) return BlankCheck::retained;
    }
    return BlankCheck::blank;
}
PairError entropy_error(target::heltec_v4_security_eval::EntropyRuntimeError error) {
    using Error=target::heltec_v4_security_eval::EntropyRuntimeError;
    switch(error) {
        case Error::configuration_rejected:return PairError::entropy_configuration;
        case Error::controller_not_idle:return PairError::entropy_not_idle;
        case Error::init_failed:return PairError::entropy_init;
        case Error::enable_failed:return PairError::entropy_enable;
        case Error::readiness_failed:return PairError::entropy_readiness;
        default:return PairError::entropy_other;
    }
}
}
extern "C" void app_main() {
    usb_serial_jtag_driver_config_t usb{};
    usb.tx_buffer_size=1024; usb.rx_buffer_size=1024;
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK) stopped(PairStage::usb_install,PairError::operation_failed);
    usb_ready=true;
    // Existing NVS failures are never repaired through an erase fallback.
    if(nvs_flash_init()!=ESP_OK) stopped(PairStage::nvs_init,PairError::operation_failed);
    static PairDisplay display;
    if(!display.initialize()) stopped(PairStage::display_init,PairError::operation_failed,&display);
    gpio_config_t button{};
    button.pin_bit_mask=1ULL<<GPIO_NUM_0;
    button.mode=GPIO_MODE_INPUT;
    button.pull_up_en=GPIO_PULLUP_ENABLE;
    button.pull_down_en=GPIO_PULLDOWN_DISABLE;
    button.intr_type=GPIO_INTR_DISABLE;
    if(gpio_config(&button)!=ESP_OK) stopped(PairStage::button_init,PairError::operation_failed,&display);
    static PairNvsBackend boot_backend(PairNvsBackend::Store::boot);
    if(!boot_backend.ready()) stopped(PairStage::boot_open,PairError::operation_failed,&display);
    static PairNvsBackend role_backend(PairNvsBackend::Store::role);
    if(!role_backend.ready()) stopped(PairStage::role_open,PairError::operation_failed,&display);
    static PairNvsBackend tx_backend(PairNvsBackend::Store::tx);
    if(!tx_backend.ready()) stopped(PairStage::tx_open,PairError::operation_failed,&display);
    static PairNvsBackend rx_backend(PairNvsBackend::Store::rx);
    if(!rx_backend.ready()) stopped(PairStage::rx_open,PairError::operation_failed,&display);
    static persistence::PersistentStorageKv boot(boot_backend),role(role_backend),tx(tx_backend),rx(rx_backend);
    const auto tx_blank=blank(tx);
    if(tx_blank!=BlankCheck::blank) stopped(PairStage::tx_blank,
        tx_blank==BlankCheck::read_failed?PairError::read_failed:PairError::retained_state,&display);
    const auto rx_blank=blank(rx);
    if(rx_blank!=BlankCheck::blank) stopped(PairStage::rx_blank,
        rx_blank==BlankCheck::read_failed?PairError::read_failed:PairError::retained_state,&display);
    static opentrail::target::heltec_v4_security_eval::EntropyRuntime entropy;
    if(!entropy.start()) stopped(PairStage::entropy_start,entropy_error(entropy.error()),&display);
    if(sodium_init()<0) stopped(PairStage::sodium_init,PairError::sodium_failed,&display);
    static LocalAuthority authority;
#ifdef OT_PAIR_RADIO_EVAL
    static opentrail::target::heltec_v4_pair_radio_eval::HeltecPairRadioDriver radio;
    static PairRadioChannel radio_channel(radio);
    static PairBenchSession session(entropy.random(),boot,role,tx,rx,authority,display,&radio_channel);
#else
    static PairBenchSession session(entropy.random(),boot,role,tx,rx,authority,display);
#endif
    const auto now=authority.sample();
    if(now.context.transport_generation==0) stopped(PairStage::authority,PairError::authority_invalid,&display,&session);
    std::array<char,768> output{};
    const auto length=std::snprintf(output.data(),output.size(),"OTPAIR1 READY 1 %llu\n",
                                  static_cast<unsigned long long>(now.now_ms));
    if(length<=0 || static_cast<std::size_t>(length)>=output.size())
        stopped(PairStage::ready_send,PairError::output_invalid,&display,&session);
    if(!send(output.data(),static_cast<std::size_t>(length)))
        stopped(PairStage::ready_send,PairError::output_short,&display,&session);
    std::array<char,700> line{};
    std::size_t used=0;
    StartupIngress startup_ingress;
    bool control_admitted=false;
    for(;;) {
        // The session owns release/press/hold/release and its signed deadline.
        // Never manufacture a physical decision from a host command.
        if(!session.tick(gpio_get_level(GPIO_NUM_0)==0))
            record_failure(PairStage::session_tick,PairError::session_refused,&display,&session,false);
        if(!control_admitted && startup_ingress.at_limit())
            stopped(PairStage::precontrol_budget,PairError::precontrol_budget,&display,&session);
        char value=0;
        const auto count=usb_serial_jtag_read_bytes(&value,1,pdMS_TO_TICKS(10));
        if(count<0) stopped(PairStage::usb_read,PairError::read_failed,&display,&session,
                            used!=0 || startup_ingress.pending_line());
        if(count==0) continue;
        bool admitting_control=false;
        if(!control_admitted) {
            const auto result=startup_ingress.feed(value);
            if(result==StartupIngress::Result::budget_exhausted)
                stopped(PairStage::precontrol_budget,PairError::precontrol_budget,
                        &display,&session,value!='\n');
            if(result==StartupIngress::Result::ignored) {
                // Includes empty, unknown, binary and overlong startup lines.
                // No such bytes reach session admission or allocate identity.
                vTaskDelay(1);
                continue;
            }
            admitting_control=result==StartupIngress::Result::hello;
            const std::string_view admitted=admitting_control?"OTPAIR1 HELLO":"OTPAIR1 DIAG";
            for(std::size_t i=0;i<admitted.size();++i) line[i]=admitted[i];
            used=admitted.size();
        }
        if(value=='\n') {
            std::size_t size=0;
            const auto command=std::string_view(line.data(),used);
            if(command=="OTPAIR1 DIAG") {
                // Observation bypasses command admission without renewing the
                // session or mutating its state; tick still enforces expiry.
                if(!diagnostics.format(output,size))
                    stopped(PairStage::response_send,PairError::output_invalid,&display,&session);
            } else if(!session.command(command,output,size)) {
                record_failure(PairStage::session_command,PairError::session_refused,&display,&session,false);
            } else if(admitting_control) control_admitted=true;
            line.fill(0); used=0;
            if(size==0) {
                if(!send_refused()) stopped(PairStage::response_send,PairError::output_short,&display,&session);
            } else if(!send(output.data(),size))
                stopped(PairStage::response_send,PairError::output_short,&display,&session);
            output.fill(0);
            // Give the idle/watchdog task time even under continuous valid input.
            vTaskDelay(1);
        } else {
            // LF-only framing is deliberate. Binary/control data or an overlong
            // record permanently closes this candidate; no partial command runs.
            if(value<32 || value>126)
                stopped(PairStage::invalid_control,PairError::invalid_control,&display,&session,true);
            if(used==line.size())
                stopped(PairStage::line_overflow,PairError::line_overflow,&display,&session,true);
            line[used++]=value;
        }
    }
}
