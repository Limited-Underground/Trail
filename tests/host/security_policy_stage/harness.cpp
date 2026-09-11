// Reuse external SDK/entropy/crypto fixture behavior without changing OT-194.
#define nvs_open ot194_nvs_open
#define nvs_close ot194_nvs_close
#define nvs_commit ot194_nvs_commit
#include "ot194_external_fixture.hpp"
#undef nvs_open
#undef nvs_close
#undef nvs_commit
#include "opentrail/security_stage_record.hpp"
#ifdef ACTUAL_CONSOLE
extern "C" void lifecycle_console_send_fault();
#endif
namespace stage_mock {
constexpr nvs_handle_t handle=0x195;
bool exists=false,closed=false;
std::uint64_t pending=0,durable=0;
unsigned commits=0,sets=0,gets=0;
std::vector<std::uint64_t> records;
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){
 if(std::string(name)!="ot195diag")return ot194_nvs_open(name,mode,out);
 lifecycle::event("stage_open");
 if(mode==NVS_READONLY){if(!stage_mock::exists)return ESP_ERR_NVS_NOT_FOUND;*out=stage_mock::handle;return ESP_OK;}
 CHECK(mode==NVS_READWRITE);stage_mock::exists=true;*out=stage_mock::handle;
 return lifecycle::scenario=="create_fail"?ESP_FAIL:ESP_OK;
}
void nvs_close(nvs_handle_t h){if(h==stage_mock::handle)stage_mock::closed=true;else ot194_nvs_close(h);}
esp_err_t nvs_set_u64(nvs_handle_t h,const char* key,std::uint64_t value){CHECK(h==stage_mock::handle&&std::string(key)=="stage");++stage_mock::sets;stage_mock::pending=value;if(lifecycle::scenario=="set_fail")return ESP_FAIL;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){
 if(h!=stage_mock::handle)return ot194_nvs_commit(h);
 ++stage_mock::commits;stage_mock::durable=stage_mock::pending;
 stage_mock::records.push_back(stage_mock::durable);
 const bool failure=lifecycle::scenario=="commit_applied_fail"&&stage_mock::commits==3;
 return failure?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_get_u64(nvs_handle_t h,const char* key,std::uint64_t* out){CHECK(h==stage_mock::handle&&std::string(key)=="stage");++stage_mock::gets;*out=stage_mock::durable;if(lifecycle::scenario=="readback_corrupt")*out^=1;return lifecycle::scenario=="readback_fail"?ESP_FAIL:ESP_OK;}
int main(int argc,char** argv){
#ifdef _WIN32
 _setmode(_fileno(stdout),_O_BINARY);
#endif
 CHECK(argc==2);using namespace lifecycle;scenario=argv[1];
 const std::string challenge="0123456789abcdef0123456789abcdef";
 input="RUN SEC_EVAL1 ot187-policy-v0 "+challenge+"\n";
 if(scenario=="preexisting")stage_mock::exists=true;
 if(scenario=="no_input")input.clear();
 if(scenario=="partial")input="R";
 if(scenario=="invalid")input="RUN wrong\n";
#ifdef ACTUAL_CONSOLE
 CHECK(lifecycle_console_start());lifecycle_console_feed(input.c_str());
 if(scenario=="actual_fault")lifecycle_console_fault();
 if(scenario=="actual_input_fault")lifecycle_console_input_fault();
 if(scenario=="actual_send_fault")lifecycle_console_send_fault();
#endif
 app_main();
#ifdef ACTUAL_CONSOLE
 wire=lifecycle_console_wire();
#endif
 using namespace opentrail::security_diagnostics;
 const auto encoded=stage_mock::durable;
 const auto last=static_cast<Stage>((encoded>>32)&0xff);
 const auto error=static_cast<Error>((encoded>>40)&0xff);
 if(stage_mock::commits)CHECK(encoded==encode(last,error));
 CHECK(stage_mock::commits<=8&&stage_mock::sets<=8);
 if(scenario=="preexisting"||scenario=="nvs_fail"||scenario=="create_fail"||scenario=="set_fail")CHECK(stage_mock::commits==0&&wire.empty());
 else if(scenario=="readback_fail"||scenario=="readback_corrupt")CHECK(last==Stage::admitted&&stage_mock::commits==1&&wire.empty());
 else if(scenario=="commit_applied_fail")CHECK(last==Stage::waiting&&stage_mock::commits==3&&wire.empty());
 else if(scenario=="install_fail"||scenario=="actual_fault")CHECK(last==Stage::install_result&&error==Error::console_install&&wire.empty());
 else if(scenario=="no_input"||scenario=="partial"||scenario=="invalid"||scenario=="input_fault"||scenario=="actual_input_fault")CHECK(last==Stage::input_result&&error==Error::input_refused&&wire.empty());
 else if(scenario=="begin_fail")CHECK(last==Stage::input_result&&error==Error::session_refused&&wire.empty());
 else {
  CHECK(last==Stage::send_return&&stage_mock::commits==8);
  CHECK(error==(scenario=="send_fail"||scenario=="entropy_fault"||scenario=="actual_send_fault"?Error::receipt_send:Error::none));
  const auto evaluation=stage_mock::records.at(5);
  const auto expected=scenario=="start_fail"?Error::entropy_start:scenario=="sodium_fail"?Error::sodium_init:scenario=="evaluate_fail"||scenario=="namespace_fail"||scenario=="entropy_fault"?Error::evaluation_failed:scenario=="stop_fail"?Error::entropy_stop:Error::none;
  CHECK(static_cast<Error>((evaluation>>40)&0xff)==expected);
 }
 if(scenario=="preexisting"||scenario=="nvs_fail"||scenario=="create_fail"||scenario=="set_fail"||scenario=="readback_fail"||scenario=="readback_corrupt"||scenario=="commit_applied_fail")CHECK(std::find(events.begin(),events.end(),"entropy_start")==events.end());
 if(scenario=="success"||scenario=="actual_success")CHECK(!wire.empty()&&std::find(events.begin(),events.end(),"nvs_write")!=events.end());
 std::cout<<"PASS "<<scenario<<"\n";
 for(const auto value:stage_mock::records)std::cout<<value<<"\n";
}
