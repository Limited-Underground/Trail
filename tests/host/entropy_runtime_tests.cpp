#include "entropy_runtime.hpp"
#include "esp_bt.h"
#include <atomic>
#include <array>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>
#include <string>
using namespace opentrail::target::heltec_v4_security_eval;
static std::atomic<int> status{ESP_BT_CONTROLLER_STATUS_IDLE};
static unsigned inits,enables,disables,deinits,fills,delays;
static EntropyRuntime* reenter=nullptr;
static bool fail_init,fail_enable,fail_disable,fail_deinit,bad_status;
static std::int64_t clock_value=100;
static bool regress,stuck,release_on_delay,block,entered,released,finished;
static std::mutex mutex;static std::condition_variable cv;
extern "C" esp_bt_controller_status_t esp_bt_controller_get_status(){return static_cast<esp_bt_controller_status_t>(status.load());}
extern "C" int esp_bt_controller_init(esp_bt_controller_config_t*){++inits;if(reenter){assert(!reenter->start());assert(!reenter->stop());}if(fail_init)return -1;status=ESP_BT_CONTROLLER_STATUS_INITED;return 0;}
extern "C" int esp_bt_controller_enable(int){++enables;if(fail_enable)return -1;if(!bad_status)status=ESP_BT_CONTROLLER_STATUS_ENABLED;return 0;}
extern "C" int esp_bt_controller_disable(){++disables;if(fail_disable)return -1;status=ESP_BT_CONTROLLER_STATUS_INITED;return 0;}
extern "C" int esp_bt_controller_deinit(){++deinits;if(fail_deinit)return -1;status=ESP_BT_CONTROLLER_STATUS_IDLE;return 0;}
extern "C" std::int64_t esp_timer_get_time(){return regress?clock_value--:clock_value;}
extern "C" void esp_rom_delay_us(unsigned us){
 ++delays;if(!stuck)clock_value+=us;
 if(release_on_delay){std::unique_lock<std::mutex> lock(mutex);released=true;cv.notify_all();assert(cv.wait_for(lock,std::chrono::seconds(2),[]{return finished;}));}
}
extern "C" void esp_fill_random(void* p,size_t n){
 ++fills;assert(status==ESP_BT_CONTROLLER_STATUS_ENABLED);
 std::unique_lock<std::mutex> lock(mutex);
 if(block){entered=true;cv.notify_all();cv.wait(lock,[]{return released;});}
 for(size_t i=0;i<n;++i)static_cast<unsigned char*>(p)[i]=0x5a;
}
int main(int argc,char** argv){assert(argc==2);std::string name=argv[1];EntropyRuntime runtime;std::array<unsigned char,16> output{};
 assert(!runtime.random().fill(output.data(),output.size()).ok());
 if(name=="config"){assert(!runtime.start());assert(runtime.error()==EntropyRuntimeError::configuration_rejected);assert(inits==0);return 0;}
 if(name=="external"){status=ESP_BT_CONTROLLER_STATUS_ENABLED;assert(!runtime.start());assert(runtime.stop());assert(inits==0&&disables==0&&deinits==0);return 0;}
 if(name=="init_failure"){fail_init=true;assert(!runtime.start());assert(runtime.error()==EntropyRuntimeError::init_failed);assert(runtime.stop());assert(enables==0&&disables==0);return 0;}
 if(name=="enable_failure"||name=="status_failure"){
  fail_enable=name=="enable_failure";bad_status=name=="status_failure";assert(!runtime.start());assert(!runtime.random().fill(output.data(),16).ok());assert(runtime.stop());assert(deinits==1&&disables==0);return 0;
 }
 if(name=="reentry")reenter=&runtime;
 assert(runtime.start());assert(runtime.start());assert(inits==1&&enables==1);
 if(name=="disable_failure"||name=="deinit_failure"){
  fail_disable=name=="disable_failure";fail_deinit=name=="deinit_failure";assert(!runtime.stop());assert(!runtime.start());assert(!runtime.random().fill(output.data(),16).ok());fail_disable=false;fail_deinit=false;assert(runtime.stop());return 0;
 }
 if(name=="invalid_budget"){assert(!runtime.stop(0));assert(!runtime.random().fill(output.data(),16).ok());assert(disables==0);assert(runtime.stop());return 0;}
 if(name=="success"||name=="reentry"){
  assert(runtime.random().fill(output.data(),16).ok());assert(fills==1&&output[0]==0x5a);assert(runtime.stop());assert(runtime.stop());assert(disables==1&&deinits==1);assert(!runtime.random().fill(output.data(),16).ok());assert(runtime.start());assert(runtime.stop());return 0;
 }
 block=true;release_on_delay=name=="inflight_drain";regress=name=="clock_regression";stuck=name=="clock_stuck";
 opentrail::security::RandomFillResult result;
 std::thread worker([&]{result=runtime.random().fill(output.data(),16);std::lock_guard<std::mutex> lock(mutex);finished=true;cv.notify_all();});
 {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(2),[]{return entered;}));}
 const bool stopped=runtime.stop(20);
 if(release_on_delay){assert(stopped);assert(disables==1&&deinits==1);}
 else {assert(!stopped);assert(runtime.error()==EntropyRuntimeError::drain_timeout);assert(disables==0&&deinits==0);assert(status==ESP_BT_CONTROLLER_STATUS_ENABLED);assert(!runtime.random().fill(output.data(),16).ok());}
 {std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all();}worker.join();assert(result.ok());
 if(stuck)assert(delays==200000);
 regress=false;stuck=false;assert(runtime.stop());assert(disables==1&&deinits==1);
}
