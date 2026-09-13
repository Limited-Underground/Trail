// OT-215: device-owned confirmation evaluation; unchanged OT-203 control and receipt framing.
#include <limits>
#include <sodium.h>
#include "nvs_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "entropy_runtime.hpp"
#include "policy_console.hpp"
#include "stage_store.hpp"
#include "input_control_loop.hpp"
#include "opentrail/evaluation_receipt_boundary.hpp"
#include "confirmation_evaluation.hpp"
using namespace opentrail;
namespace {
std::uint64_t now_us(){const auto t=esp_timer_get_time();return t<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(t);}
std::uint64_t now_ms(){const auto t=esp_timer_get_time();return t<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(t)/1000;}
}
extern "C" void app_main(){
 using namespace opentrail::security_evaluation;
 using namespace opentrail::security_diagnostics;
 if(nvs_flash_init()!=ESP_OK)return;
 SyncStageStore stage;
 if(!stage.begin())return;
 const bool installed=ot_console_install();
 if(!stage.record(Stage::install_result,installed?Error::none:Error::console_install)||!installed)return;
 if(!stage.record(Stage::waiting,Error::none))return;
 // Epoch follows both post-install persistence operations; not a shared clock.
 SynchronizingControl control(now_us());
 const bool received=receive_synchronizing_control(control,ot_policy_read,now_us,[]{vTaskDelay(1);},ot_console_healthy);
 const bool session=received&&ot_console_begin_session();
 const auto terminal=static_cast<std::uint8_t>(received&&!session?3:static_cast<unsigned>(control.reason()));
 const SyncRecord input{static_cast<std::uint16_t>(control.prologue_bytes_exact()),
  static_cast<std::uint8_t>(control.first_frame_bytes()),control.flags(),terminal,
  static_cast<std::uint8_t>(control.frame_reason()),sync_delay_bucket(control.first_read_seen(),control.first_read_delay_us())};
 // Independent commits: input with stage=waiting is a valid incomplete prefix.
 if(!stage.record_input(input))return;
 if(!stage.record(Stage::input_result,sync_stage_error_for(terminal))||!session)return;
 if(!stage.record(Stage::evaluation_enter,Error::none))return;
 static target::heltec_v4_security_eval::EntropyRuntime entropy;
 const bool started=entropy.start();
 const bool initialized=started&&sodium_init()>=0;
 const bool passed=initialized&&ot_console_healthy()&&target::heltec_v4_confirmation_eval::evaluate(entropy.random(),now_ms);
 const bool stopped=entropy.stop();
 const auto evaluation_error=!stopped?Error::entropy_stop:!started?Error::entropy_start:!initialized?Error::sodium_init:!passed?Error::evaluation_failed:Error::none;
 if(!stage.record(Stage::evaluation_return,evaluation_error))return;
 const auto result=!stopped?Result::entropy_contained:passed?Result::pass:Result::refused;
 char receipt[128]{};const auto size=receipt_with_begin(control,result,receipt,sizeof receipt);
 if(!stage.record(Stage::send_enter,Error::none))return;
 const bool sent=size!=0&&ot_policy_send(receipt,size);
 (void)stage.record(Stage::send_return,sent?Error::none:Error::receipt_send);
}
