#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "nvs.h"
#include "policy_console.hpp"
#define CHECK(x) do{if(!(x)){std::cerr<<"CHECK "<<__LINE__<<"\n";std::exit(1);}}while(0)
extern "C" void app_main();
namespace lifecycle {
std::string scenario,input,wire;
std::vector<std::string> events;
std::uint64_t clock=0,available_at=0;
bool healthy=true,session=false;
unsigned pauses=0,sends=0;
void event(const char* name){events.emplace_back(name);}
bool start(){event("entropy_start");if(scenario=="entropy_fault")healthy=false;return scenario!="start_fail";}
bool stop(){event("entropy_stop");if(scenario=="delayed_stop")clock+=31000000;return scenario!="stop_fail";}
bool random_fail(){return scenario=="evaluate_fail";}
}
#ifndef ACTUAL_CONSOLE
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(lifecycle::clock);}
#endif
void vTaskDelay(unsigned){++lifecycle::pauses;lifecycle::clock+=lifecycle::scenario=="poll_cap"?1:10000;}
int nvs_flash_init(){lifecycle::event("nvs_init");return lifecycle::scenario=="nvs_fail"?ESP_FAIL:ESP_OK;}
extern "C" void randombytes_buf(void*,std::size_t){std::abort();}
extern "C" int __wrap_sodium_init(){lifecycle::event("sodium_init");return lifecycle::scenario=="sodium_fail"?-1:0;}
#ifndef ACTUAL_CONSOLE
bool ot_console_install()noexcept{lifecycle::event("install");return lifecycle::scenario!="install_fail";}
bool ot_console_healthy()noexcept{return lifecycle::healthy;}
bool ot_console_session_started()noexcept{return lifecycle::session;}
bool ot_console_begin_session()noexcept{lifecycle::event("begin");lifecycle::session=lifecycle::scenario!="begin_fail";return lifecycle::session;}
bool ot_policy_read(char& byte)noexcept{
 if(lifecycle::clock<lifecycle::available_at||lifecycle::input.empty())return false;
 byte=lifecycle::input.front();lifecycle::input.erase(0,1);lifecycle::event("input");
 if(lifecycle::scenario=="input_fault")lifecycle::healthy=false;
 return true;
}
bool ot_policy_send(const char* p,std::size_t n)noexcept{lifecycle::event("send");++lifecycle::sends;if(lifecycle::scenario=="send_fail"||!lifecycle::healthy)return false;lifecycle::wire.assign(p,n);return true;}
#endif
namespace nv {
using Blobs=std::map<std::string,std::vector<unsigned char>>;
struct Handle{std::string name;Blobs pending;};
std::map<std::string,Blobs> durable;std::map<unsigned,Handle> handles;unsigned next=1;
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){lifecycle::event("nvs_open");CHECK(mode==NVS_READWRITE);if(lifecycle::scenario=="namespace_fail")return ESP_FAIL;*out=nv::next++;nv::handles[*out]={name,{}};return ESP_OK;}
void nvs_close(nvs_handle_t h){nv::handles.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* size){auto& blobs=nv::durable[nv::handles.at(h).name];auto it=blobs.find(key);if(it==blobs.end())return ESP_ERR_NVS_NOT_FOUND;if(out){CHECK(*size>=it->second.size());std::memcpy(out,it->second.data(),it->second.size());}*size=it->second.size();return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* p,std::size_t n){lifecycle::event("nvs_write");auto* b=static_cast<const unsigned char*>(p);nv::handles.at(h).pending[key]={b,b+n};return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){nv::handles.at(h).pending[key]={};return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){auto& handle=nv::handles.at(h);for(auto& [key,value]:handle.pending){if(value.empty())nv::durable[handle.name].erase(key);else nv::durable[handle.name][key]=value;}handle.pending.clear();return ESP_OK;}
#ifdef ACTUAL_CONSOLE
extern "C" bool lifecycle_console_start();
extern "C" void lifecycle_console_feed(const char*);
extern "C" const char* lifecycle_console_wire();
extern "C" void lifecycle_console_fault();
extern "C" void lifecycle_console_input_fault();
extern "C" void lifecycle_console_time(std::uint64_t);
#endif
int main(int argc,char** argv){
 #ifdef _WIN32
 _setmode(_fileno(stdout),_O_BINARY);
 #endif
 CHECK(argc==2);using namespace lifecycle;scenario=argv[1];
 const std::string challenge="0123456789abcdef0123456789abcdef";
 input="RUN SEC_EVAL1 ot187-policy-v0 "+challenge+"\n";
 if(scenario=="no_input"||scenario=="poll_cap")input.clear();
 if(scenario=="partial")input="R";
 if(scenario=="invalid")input="RUN wrong\n";
 if(scenario=="late_first")available_at=60000000;
 if(scenario=="last_first")available_at=59990000;
#ifdef ACTUAL_CONSOLE
 CHECK(lifecycle_console_start());lifecycle_console_feed(input.c_str());
 if(scenario=="actual_fault")lifecycle_console_fault();
 if(scenario=="actual_input_fault")lifecycle_console_input_fault();
#endif
 app_main();
#ifdef ACTUAL_CONSOLE
 wire=lifecycle_console_wire();
 if(scenario=="actual_fault"||scenario=="actual_input_fault"){CHECK(wire.empty());CHECK(events.empty());}
 else {CHECK(wire=="SEC_EVAL1 ot187-policy-v0 "+challenge+" pass\r\n");CHECK(std::find(events.begin(),events.end(),"nvs_write")!=events.end());}
#else
 auto count=[&](const char* name){return std::count(events.begin(),events.end(),name);};
 const bool silent=scenario=="install_fail"||scenario=="begin_fail"||scenario=="input_fault"||scenario=="no_input"||scenario=="partial"||scenario=="invalid"||scenario=="late_first"||scenario=="poll_cap";
 if(silent){CHECK(wire.empty()&&sends==0&&count("nvs_init")==0&&count("entropy_start")==0);}
 else {
  CHECK(count("nvs_init")==1&&count("send")==1);
  CHECK(std::find(events.begin(),events.end(),"begin")<std::find(events.begin(),events.end(),"nvs_init"));
  if(scenario=="nvs_fail")CHECK(count("entropy_start")==0);
  else {CHECK(count("entropy_start")==1&&count("entropy_stop")==1);CHECK(std::find(events.begin(),events.end(),"entropy_stop")<std::find(events.begin(),events.end(),"send"));}
  std::string result=scenario=="nvs_fail"?"nvs_unavailable":scenario=="stop_fail"?"entropy_contained":scenario=="start_fail"||scenario=="sodium_fail"||scenario=="evaluate_fail"||scenario=="namespace_fail"?"refused":"pass";
  if(scenario=="send_fail"||scenario=="entropy_fault")CHECK(wire.empty());
  else CHECK(wire=="SEC_EVAL1 ot187-policy-v0 "+challenge+" "+result+"\n");
  if(scenario=="success")CHECK(count("nvs_write")>0&&count("random_fill")>0);
 }
 if(scenario=="poll_cap")CHECK(pauses==7000);
 if(scenario=="no_input"||scenario=="poll_cap")CHECK(count("input")==0);
 // At the exact first-byte deadline the real loop reads one byte, then Control refuses it.
 if(scenario=="late_first")CHECK(count("input")==1);
 if(scenario=="start_fail")CHECK(count("sodium_init")==0&&count("nvs_open")==0);
 if(scenario=="delayed_stop")CHECK(lifecycle::clock>=31000000);
#endif
 std::cout<<"PASS "<<scenario<<"\n";
 if(!wire.empty())std::cout<<wire;
}
