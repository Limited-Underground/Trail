#include "entropy_runtime.hpp"
#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
namespace opentrail::target::heltec_v4_security_eval {
namespace {
constexpr bool configuration_admitted() {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_BT_CONTROLLER_ENABLED) && CONFIG_BT_CONTROLLER_ENABLED && !defined(CONFIG_BT_CTRL_MODEM_SLEEP) && !defined(CONFIG_PM_ENABLE)
    return true;
#else
    return false;
#endif
}
struct Unlock { std::atomic_flag& flag; ~Unlock(){flag.clear(std::memory_order_release);} };
}
EntropyRuntime::EntropyRuntime() : guarded_(raw_, ready_probe, nullptr) {}
bool EntropyRuntime::ready_probe(void*) noexcept {
    return configuration_admitted() && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}
bool EntropyRuntime::start() {
    if(lifecycle_.test_and_set(std::memory_order_acquire)) return false;
    Unlock unlock{lifecycle_};
    if(owns_controller_) return guarded_.state() == security::EntropyState::ready;
    if(!configuration_admitted()){error_=EntropyRuntimeError::configuration_rejected;return false;}
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE){error_=EntropyRuntimeError::controller_not_idle;return false;}
    owns_controller_=true;
    esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if(esp_bt_controller_init(&config)!=ESP_OK){error_=EntropyRuntimeError::init_failed;return false;}
    if(esp_bt_controller_enable(ESP_BT_MODE_BLE)!=ESP_OK){error_=EntropyRuntimeError::enable_failed;return false;}
    raw_.set_entropy_state(security::EntropyState::ready);
    if(!guarded_.activate()){error_=EntropyRuntimeError::readiness_failed;return false;}
    error_=EntropyRuntimeError::none;return true;
}
bool EntropyRuntime::stop(std::uint32_t budget) {
    if(lifecycle_.test_and_set(std::memory_order_acquire)) return false;
    Unlock unlock{lifecycle_};
    return stop_locked(budget);
}
bool EntropyRuntime::stop_locked(std::uint32_t budget) {
    // Even invalid budgets close admission; never silently leave requests open.
    const bool drained=guarded_.revoke();
    if(budget==0 || budget>1000000){error_=EntropyRuntimeError::invalid_budget;return false;}
    if(!drained){
        const auto begin=esp_timer_get_time();auto previous=begin;
        bool done=false;
        // Independent cap also bounds a stuck clock; each retry delays <=5us.
        for(unsigned attempt=0;attempt<200000;++attempt){
            if(guarded_.revoke()){done=true;break;}
            const auto now=esp_timer_get_time();
            if(begin<0 || now<previous || now-begin>=budget) break;
            previous=now;
            const auto remaining=budget-static_cast<std::uint32_t>(now-begin);
            esp_rom_delay_us(remaining<5?remaining:5);
        }
        if(!done){error_=EntropyRuntimeError::drain_timeout;return false;}
    }
    raw_.set_entropy_state(security::EntropyState::not_ready);
    if(!owns_controller_){error_=EntropyRuntimeError::none;return true;}
    auto status=esp_bt_controller_get_status();
    if(status==ESP_BT_CONTROLLER_STATUS_ENABLED){
        if(esp_bt_controller_disable()!=ESP_OK){error_=EntropyRuntimeError::disable_failed;return false;}
        status=esp_bt_controller_get_status();
    }
    if(status==ESP_BT_CONTROLLER_STATUS_INITED){
        if(esp_bt_controller_deinit()!=ESP_OK){error_=EntropyRuntimeError::deinit_failed;return false;}
        status=esp_bt_controller_get_status();
    }
    if(status!=ESP_BT_CONTROLLER_STATUS_IDLE){error_=EntropyRuntimeError::deinit_failed;return false;}
    owns_controller_=false;error_=EntropyRuntimeError::none;return true;
}
}
