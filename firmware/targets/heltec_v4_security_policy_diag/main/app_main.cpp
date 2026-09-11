// Diagnostic successor: exact predecessor evaluation/control implementation.
#define app_main ot187_reference_app_main
#include "../../heltec_v4_security_policy_eval/main/app_main.cpp"
#undef app_main
#include "stage_store.hpp"
extern "C" void app_main(){
 using namespace opentrail::security_evaluation;
 using opentrail::security_diagnostics::Stage;
 using opentrail::security_diagnostics::Error;
 // Deliberate diagnostic perturbation: NVS before console installation/input.
 if(nvs_flash_init()!=ESP_OK)return;
 opentrail::security_diagnostics::StageStore stage;
 if(!stage.begin())return;
 const bool installed=ot_console_install();
 if(!stage.record(Stage::install_result,installed?Error::none:Error::console_install)||!installed)return;
 if(!stage.record(Stage::waiting,Error::none))return;
 Control control(now_us());
 const bool received=receive_control(control,ot_policy_read,now_us,[]{vTaskDelay(1);},ot_console_healthy);
 const bool session=received&&ot_console_begin_session();
 const auto input_error=!received?Error::input_refused:!session?Error::session_refused:Error::none;
 if(!stage.record(Stage::input_result,input_error)||!session)return;
 if(!stage.record(Stage::evaluation_enter,Error::none))return;
 static target::heltec_v4_security_eval::EntropyRuntime entropy;
 const bool started=entropy.start();
 const bool initialized=started&&sodium_init()>=0;
 const bool passed=initialized&&ot_console_healthy()&&evaluate(entropy.random());
 const bool stopped=entropy.stop();
 const auto evaluation_error=!stopped?Error::entropy_stop:!started?Error::entropy_start:!initialized?Error::sodium_init:!passed?Error::evaluation_failed:Error::none;
 if(!stage.record(Stage::evaluation_return,evaluation_error))return;
 const auto result=!stopped?Result::entropy_contained:passed?Result::pass:Result::refused;
 char receipt[128]{};const auto size=control.receipt(result,receipt,sizeof receipt);
 if(!stage.record(Stage::send_enter,Error::none))return;
 const bool sent=size!=0&&ot_policy_send(receipt,size);
 (void)stage.record(Stage::send_return,sent?Error::none:Error::receipt_send);
}
