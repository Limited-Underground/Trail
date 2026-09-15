// Compile real app_main/PairNvsBackend/PersistentStorageKv/EntropyRuntime.
// Only SDK calls and OLED presentation are replaced; no production test hooks.
// Link with -Wl,--wrap=sodium_init and the existing real libsodium/noise objects.
// sodium_init is an initializer seam (success/failure), not real initialization
// coverage; subsequent Noise/key operations retain the real scalar crypto code.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "pair_display.hpp"
#include "pair_diagnostics.hpp"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_bt.h"
#include "nvs.h"
#include "freertos/task.h"
using namespace opentrail::target::heltec_v4_pair_eval;
namespace {
std::string scenario,input,output;
std::size_t cursor=0;
unsigned idle=0,nvs_inits=0,opens=0,bt_inits=0,bt_enables=0,random_fills=0,sodium_calls=0,writes=0,commits=0,erases=0,clock_calls=0;
unsigned failed_writes=0,diagnostic_queries=0;
std::int64_t now_us=1000000;
esp_bt_controller_status_t bt_status=ESP_BT_CONTROLLER_STATUS_IDLE;
std::map<nvs_handle_t,std::string> handles;
std::map<std::string,std::vector<std::uint8_t>> blobs;
std::vector<std::pair<unsigned,unsigned>> displays;
bool injected=false;
unsigned expected_stage=0,expected_error=0;
unsigned frozen_writes=0,frozen_fills=0,frozen_opens=0,frozen_bt=0;
bool checked_prehello=false;
std::size_t first_failure_cursor=0;
bool happy_case(){return scenario=="happy" || scenario=="prehello_noise" || scenario=="prehello_init" || scenario=="prehello_boundary";}
void record_freeze(){frozen_writes=writes;frozen_fills=random_fills;frozen_opens=opens;frozen_bt=bt_inits;}
std::string key(nvs_handle_t h,const char* k){return handles.at(h)+":"+k;}
[[noreturn]] void finish(){
    if(happy_case()) {
        assert(output.find("OTPAIR1 READY 1 ")!=std::string::npos);
        assert(output.find("OTPAIR1 ID ")!=std::string::npos);
        assert(output.find("OTPAIR1 DIAG 1 0 0 0\n")!=std::string::npos);
        assert(opens==4 && bt_inits==1 && bt_enables==1 && sodium_calls==1);
        assert(random_fills>0 && writes>0);
        if(scenario!="happy") assert(checked_prehello);
    } else if(scenario=="usb_install") {
        assert(nvs_inits==0 && opens==0 && bt_inits==0 && writes==0);
    } else {
        const std::string prefix="OTPAIR1 DIAG 1 "+std::to_string(expected_stage)+" "+std::to_string(expected_error)+" ";
        auto first=output.find(prefix);assert(first!=std::string::npos);
        auto end=output.find('\n',first);assert(end!=std::string::npos);
        const auto receipt=output.substr(first,end-first+1);
        assert(output.find(receipt,end+1)!=std::string::npos); // INIT cannot overwrite first cause.
        assert(output.find("OTPAIR1 ID ")==std::string::npos);
        assert(output.find("OTPAIR1 REFUSED\n")!=std::string::npos);
        assert(writes==frozen_writes && random_fills==frozen_fills && opens==frozen_opens && bt_inits==frozen_bt);
        assert(writes==0 && commits==0 && erases==0); // No INIT ledger work in fatal diagnostics.
        if(expected_stage<11) assert(bt_inits==0 && sodium_calls==0 && random_fills==0);
        if(expected_stage<5) assert(opens==0);
        if(expected_stage==11) assert(sodium_calls==0);
        if(expected_stage==12) assert(random_fills==0);
        if(expected_stage==21) assert(first_failure_cursor==4096);
    }
    std::printf("PASS 1 actual pair startup groups (%s)\n",scenario.c_str());std::fflush(stdout);std::_Exit(0);
}
}
extern "C" void app_main();
void vTaskDelay(TickType_t ticks){now_us+=static_cast<std::int64_t>(ticks)*1000;if(++idle>20000)finish();}
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*){return scenario=="usb_install"?-1:0;}
int usb_serial_jtag_write_bytes(const void* p,std::size_t n,std::uint32_t){
    std::string text(static_cast<const char*>(p),n);
    if(scenario=="ready_send" && text.find("OTPAIR1 READY ")==0 && failed_writes++==0)return 0;
    output+=text;return static_cast<int>(n);
}
int usb_serial_jtag_read_bytes(void* p,std::uint32_t n,std::uint32_t){
    assert(n==1);
    if(scenario=="usb_read" && !injected){injected=true;return -1;}
    if(cursor>=input.size()){if(++idle>50)finish();return 0;}
    if((scenario=="prehello_noise" || scenario=="prehello_init" || scenario=="prehello_boundary") && input.compare(cursor,13,"OTPAIR1 HELLO")==0) {
        assert(writes==0 && commits==0 && erases==0 && random_fills==0);
        assert(output.find("OTPAIR1 DIAG 1 0 0 0\n")!=std::string::npos);
        checked_prehello=true;
    }
    // Snapshot side effects immediately before the first DIAG enters the app.
    if(input.compare(cursor,12,"OTPAIR1 DIAG")==0 && diagnostic_queries++==0)record_freeze();
    *static_cast<char*>(p)=input[cursor++];return 1;
}
int gpio_config(const gpio_config_t*){return scenario=="button_init"?-1:0;}
int gpio_get_level(int){return 1;}
esp_err_t nvs_flash_init(){++nvs_inits;return scenario=="nvs_init"?-1:0;}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* h){
    assert(mode==NVS_READWRITE);++opens;
    if((scenario=="boot_open"&&opens==1)||(scenario=="role_open"&&opens==2)||(scenario=="tx_open"&&opens==3)||(scenario=="rx_open"&&opens==4))return -1;
    *h=opens;handles[*h]=name;return 0;
}
void nvs_close(nvs_handle_t){}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* k,void* p,std::size_t* n){
    const auto name=handles.at(h);
    if((scenario=="tx_blank_read"&&name=="ot230_tx")||(scenario=="rx_blank_read"&&name=="ot230_rx"))return -1;
    if((scenario=="tx_blank_retained"&&name=="ot230_tx")||(scenario=="rx_blank_retained"&&name=="ot230_rx")){
        if(p) { std::memset(p,0,*n); }
        *n=64;return 0;
    }
    auto it=blobs.find(key(h,k));if(it==blobs.end())return ESP_ERR_NVS_NOT_FOUND;
    if(p){assert(*n>=it->second.size());std::copy(it->second.begin(),it->second.end(),static_cast<std::uint8_t*>(p));}
    *n=it->second.size();return 0;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* k,const void* p,std::size_t n){++writes;const auto* b=static_cast<const std::uint8_t*>(p);blobs[key(h,k)]={b,b+n};return 0;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* k){++erases;return blobs.erase(key(h,k))?0:ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_commit(nvs_handle_t){++commits;return 0;}
extern "C" esp_bt_controller_status_t esp_bt_controller_get_status(){
    if(scenario=="entropy_not_idle"&&bt_inits==0)return ESP_BT_CONTROLLER_STATUS_ENABLED;
    return bt_status;
}
extern "C" int esp_bt_controller_init(esp_bt_controller_config_t*){++bt_inits;if(scenario=="entropy_init")return -1;bt_status=ESP_BT_CONTROLLER_STATUS_INITED;return 0;}
extern "C" int esp_bt_controller_enable(int){++bt_enables;if(scenario=="entropy_enable")return -1;if(scenario!="entropy_readiness")bt_status=ESP_BT_CONTROLLER_STATUS_ENABLED;return 0;}
extern "C" int esp_bt_controller_disable(){bt_status=ESP_BT_CONTROLLER_STATUS_INITED;return 0;}
extern "C" int esp_bt_controller_deinit(){bt_status=ESP_BT_CONTROLLER_STATUS_IDLE;return 0;}
extern "C" void esp_rom_delay_us(unsigned us){now_us+=us;}
extern "C" std::int64_t esp_timer_get_time(){++clock_calls;if(scenario=="authority")return -1;if(scenario=="session_tick"&&clock_calls>1)return -1;return ++now_us;}
extern "C" void esp_fill_random(void* p,std::size_t n){++random_fills;std::memset(p,0x5a,n);}
extern "C" int __wrap_sodium_init(){++sodium_calls;return scenario=="sodium_init"?-1:0;}
namespace opentrail::target::heltec_v4_pair_eval {
bool PairDisplay::initialize(){return scenario!="display_init";}
bool PairDisplay::show_state(opentrail::security_evaluation::EndpointState){return true;}
bool PairDisplay::show_review(opentrail::security_evaluation::InvitationRole,const std::array<std::uint8_t,32>&){return true;}
bool PairDisplay::show_failure(PairStage stage,PairError error){if(displays.empty())first_failure_cursor=cursor;displays.emplace_back(static_cast<unsigned>(stage),static_cast<unsigned>(error));return true;}
}
int main(int argc,char** argv){
    assert(argc==1 || argc==2);scenario=argc==2?argv[1]:"happy";
    const std::map<std::string,std::pair<unsigned,unsigned>> cases{
        {"usb_install",{1,1}},{"nvs_init",{2,1}},{"display_init",{3,1}},{"button_init",{4,1}},
        {"boot_open",{5,1}},{"role_open",{6,1}},{"tx_open",{7,1}},{"rx_open",{8,1}},
        {"tx_blank_read",{9,2}},{"tx_blank_retained",{9,3}},{"rx_blank_read",{10,2}},{"rx_blank_retained",{10,3}},
        {"entropy_not_idle",{11,5}},{"entropy_init",{11,6}},{"entropy_enable",{11,7}},{"entropy_readiness",{11,8}},
        {"sodium_init",{12,10}},{"authority",{13,11}},{"ready_send",{14,13}},{"session_tick",{15,14}},
        {"usb_read",{16,2}},{"session_command",{17,14}},{"invalid_control",{19,15}},{"line_overflow",{20,16}},
        {"prehello_budget",{21,17}},{"prehello_diag_budget",{21,17}}};
    if(!happy_case()){assert(cases.count(scenario));expected_stage=cases.at(scenario).first;expected_error=cases.at(scenario).second;}
    const auto init="OTPAIR1 INIT 1 "+std::string(64,'1')+"\n";
    input="OTPAIR1 DIAG\n"+init+"OTPAIR1 DIAG\n";
    if(scenario=="happy")input="OTPAIR1 HELLO\n"+init+"OTPAIR1 DIAG\n";
    if(scenario=="session_tick")input="OTPAIR1 DIAG\nOTPAIR1 HELLO\nOTPAIR1 DIAG\n";
    if(scenario=="session_command")input="OTPAIR1 HELLO\nOTPAIR1 BAD\n"+input;
    if(scenario=="invalid_control")input="OTPAIR1 HELLO\n"+std::string(1,'\1')+"\n"+input;
    if(scenario=="line_overflow")input="OTPAIR1 HELLO\n"+std::string(701,'X')+"\n"+input;
    if(scenario=="prehello_noise")input=std::string("\0\xC0\xFF",3)+"ROMFRAGMENT\nOTPAIR1 DIAG\nOTPAIR1 HELLO\n"+init+"OTPAIR1 DIAG\n";
    if(scenario=="prehello_init")input=init+"OTPAIR1 DIAG\nOTPAIR1 HELLO\n"+init+"OTPAIR1 DIAG\n";
    if(scenario=="prehello_budget")input=std::string(4096,'X')+"\n"+input;
    if(scenario=="prehello_boundary")input="OTPAIR1 DIAG\n"+std::string(4069,'\n')+"OTPAIR1 HELLO\n"+init+"OTPAIR1 DIAG\n";
    if(scenario=="prehello_diag_budget") {
        std::string prefix;
        for(unsigned i=0;i<320;++i)prefix+="OTPAIR1 DIAG\n";
        input=prefix+"\n"+input;
    }
    app_main();assert(false);
}
