// Actual OT215 target headers and app_main, with SDK-only seams and real crypto.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "nvs.h"
#include "confirmation_evaluation.hpp"
#include "opentrail/security_input_record.hpp"
using namespace opentrail;
namespace tested_target=opentrail::target::heltec_v4_confirmation_eval;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<" " #x "\n";std::exit(1);}}while(0)

namespace mock {
using Bytes=std::vector<unsigned char>;
using Blobs=std::map<std::string,Bytes>;
struct Handle{std::string name;Blobs pending;};
struct Event{std::string operation,name,key;std::uint64_t value;};
struct SecretRange{std::uintptr_t pointer;std::size_t size;bool cleared;};
std::map<std::string,Blobs> durable;
std::map<nvs_handle_t,Handle> handles;
std::vector<Event> events;
std::vector<SecretRange> ranges;
std::set<nvs_handle_t> blocked;
unsigned next_handle=1,matching=0,fail_nth=1,clock_calls=0,clock_fail_nth=0,random_calls=0,random_fail_nth=0;
std::string failure,fail_namespace,receipt;
std::uint64_t clock_failure=0,clock_base=100;
std::int64_t app_time=100000;
bool fail_init=false,fail_install=false,fail_begin=false,fail_start=false,fail_stop=false,fail_sodium=false,fail_send=false,console_good=true,revoke_on_open=false;
std::size_t input_at=0;
const std::string input="RUN SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef\n";
security::EntropyState entropy_state=security::EntropyState::ready;
void event(const std::string& operation,const std::string& name="",const std::string& key="",std::uint64_t value=0){events.push_back({operation,name,key,value});}
std::size_t count(const std::string& operation,const std::string& name=""){
 return static_cast<std::size_t>(std::count_if(events.begin(),events.end(),[&](const Event&e){return e.operation==operation&&(name.empty()||e.name==name);}));
}
std::size_t first(const std::string& operation,const std::string& name=""){
 for(std::size_t i=0;i<events.size();++i)if(events[i].operation==operation&&(name.empty()||events[i].name==name))return i;
 return events.size();
}
std::size_t mutations(){return count("set")+count("erase")+count("commit")+count("u64_set");}
bool fail(const std::string& operation,const std::string& name){
 if(failure!=operation||(!fail_namespace.empty()&&fail_namespace!=name))return false;
 return ++matching==fail_nth;
}
bool all_cleared(){return std::all_of(ranges.begin(),ranges.end(),[](const SecretRange&r){return r.cleared;});}
void reset(bool keep=false){
 CHECK(handles.empty());if(!keep)durable.clear();events.clear();ranges.clear();blocked.clear();next_handle=1;matching=0;fail_nth=1;
 clock_calls=clock_fail_nth=random_calls=random_fail_nth=0;clock_failure=0;clock_base=100;app_time=100000;
 failure.clear();fail_namespace.clear();receipt.clear();input_at=0;
 fail_init=fail_install=fail_begin=fail_start=fail_stop=fail_sodium=fail_send=revoke_on_open=false;console_good=true;entropy_state=security::EntropyState::ready;
}
Handle& handle(nvs_handle_t h){CHECK(handles.count(h));CHECK(!blocked.count(h));return handles.at(h);}
void apply(Handle& handle){for(const auto&[key,value]:handle.pending){if(value.empty())durable[handle.name].erase(key);else durable[handle.name][key]=value;}handle.pending.clear();}
std::uint64_t number(const Bytes& bytes){CHECK(bytes.size()==8);std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(bytes[i])<<(8*i);return value;}
Blobs policy(const std::string& name){const auto it=durable.find(name);return it==durable.end()?Blobs{}:it->second;}
bool no_policy_writes(){for(const auto&e:events)if((e.name=="ot215_ta"||e.name=="ot215_tb"||e.name=="ot215_ra"||e.name=="ot215_rb")&&(e.operation=="set"||e.operation=="erase"||e.operation=="commit"))return false;return true;}
class Random final:public security::SecureRandomSource{
 std::uint64_t value=0x123456789abcdef0ULL;
public:
 security::EntropyState state()const override{return entropy_state;}
 security::RandomFillResult fill(std::uint8_t* output,std::size_t size)override{
  event("rng");++random_calls;
  if(state()!=security::EntropyState::ready||random_calls==random_fail_nth){entropy_state=security::EntropyState::failed;return {security::RandomFillError::entropy_failed,0};}
  for(std::size_t i=0;i<size;++i){value^=value<<13;value^=value>>7;value^=value<<17;output[i]=static_cast<unsigned char>(value);}
  ranges.push_back({reinterpret_cast<std::uintptr_t>(output),size,false});return {security::RandomFillError::none,size};
 }
};
Random app_random;
std::uint64_t now(){++clock_calls;return clock_calls==clock_fail_nth?clock_failure:clock_base+clock_calls-1;}
}

extern "C" void __real_sodium_memzero(void*,std::size_t);
extern "C" void __wrap_sodium_memzero(void* pointer,std::size_t size){
 __real_sodium_memzero(pointer,size);
 const auto begin=reinterpret_cast<std::uintptr_t>(pointer);const auto end=begin+size;
 for(auto&r:mock::ranges)if(r.pointer>=begin&&r.pointer+r.size<=end){
  const auto* bytes=static_cast<const unsigned char*>(pointer)+(r.pointer-begin);
  CHECK(std::all_of(bytes,bytes+r.size,[](unsigned char value){return value==0;}));r.cleared=true;
 }
}
extern "C" int __wrap_sodium_init(){mock::event("sodium_init");return mock::fail_sodium?-1:0;}
extern "C" int __real_crypto_aead_chacha20poly1305_ietf_encrypt(unsigned char*,unsigned long long*,const unsigned char*,unsigned long long,const unsigned char*,unsigned long long,const unsigned char*,const unsigned char*,const unsigned char*);
extern "C" int __wrap_crypto_aead_chacha20poly1305_ietf_encrypt(unsigned char* c,unsigned long long* clen,const unsigned char* m,unsigned long long mlen,const unsigned char* ad,unsigned long long adlen,const unsigned char* nsec,const unsigned char* nonce,const unsigned char* key){
 if(adlen==148)mock::event("record_encrypt");
 return __real_crypto_aead_chacha20poly1305_ietf_encrypt(c,clen,m,mlen,ad,adlen,nsec,nonce,key);
}
extern "C" int __real_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char*,unsigned long long*,unsigned char*,const unsigned char*,unsigned long long,const unsigned char*,unsigned long long,const unsigned char*,const unsigned char*);
extern "C" int __wrap_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char* m,unsigned long long* mlen,unsigned char* nsec,const unsigned char* c,unsigned long long clen,const unsigned char* ad,unsigned long long adlen,const unsigned char* nonce,const unsigned char* key){
 if(adlen==148)mock::event("record_decrypt");
 return __real_crypto_aead_chacha20poly1305_ietf_decrypt(m,mlen,nsec,c,clen,ad,adlen,nonce,key);
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* output){
 mock::event("open",name);CHECK(mode==NVS_READONLY||mode==NVS_READWRITE);
 if(mock::fail("open",name))return ESP_FAIL;
 if(mode==NVS_READONLY&&!mock::durable.count(name))return ESP_ERR_NVS_NOT_FOUND;
 *output=mock::next_handle++;mock::handles[*output]={name,{}};
 if(mode==NVS_READWRITE)mock::durable[name];
 if(mock::revoke_on_open)mock::entropy_state=security::EntropyState::failed;
 return ESP_OK;
}
void nvs_close(nvs_handle_t h){CHECK(mock::handles.count(h));mock::event("close",mock::handles[h].name);mock::handles.erase(h);mock::blocked.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* output,std::size_t* size){
 auto&handle=mock::handle(h);const auto op=output?"data":"query";mock::event(op,handle.name,key);
 if(mock::fail(std::string(op)+"_error",handle.name)){if(output)std::memset(output,0xa5,*size);mock::blocked.insert(h);return ESP_FAIL;}
 if(mock::fail(std::string(op)+"_short",handle.name)){*size=63;mock::blocked.insert(h);return ESP_OK;}
 const auto&blobs=mock::durable[handle.name];const auto it=blobs.find(key);if(it==blobs.end())return ESP_ERR_NVS_NOT_FOUND;
 if(!output){*size=it->second.size();return ESP_OK;}
 CHECK(*size>=it->second.size());std::memcpy(output,it->second.data(),it->second.size());*size=it->second.size();
 if(mock::fail("data_corrupt",handle.name)){static_cast<unsigned char*>(output)[0]^=1;}
 return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t size){
 auto&handle=mock::handle(h);mock::event("set",handle.name,key,mock::clock_calls);CHECK(size==64);
 if(mock::fail("set",handle.name)){mock::blocked.insert(h);return ESP_FAIL;}
 const auto* bytes=static_cast<const unsigned char*>(data);handle.pending[key]=mock::Bytes(bytes,bytes+size);return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){
 auto&handle=mock::handle(h);mock::event("erase",handle.name,key);
 if(mock::fail("erase",handle.name)){mock::blocked.insert(h);return ESP_FAIL;}
 if(!mock::durable[handle.name].count(key))return ESP_ERR_NVS_NOT_FOUND;
 handle.pending[key]={};return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
 auto&handle=mock::handle(h);mock::event("commit",handle.name);
 const bool before=mock::fail("commit",handle.name),after=mock::fail("commit_applied",handle.name);
 if(!before)mock::apply(handle);
 if(before||after){mock::blocked.insert(h);return ESP_FAIL;}return ESP_OK;
}
esp_err_t nvs_set_u64(nvs_handle_t h,const char* key,std::uint64_t value){
 auto&handle=mock::handle(h);mock::event("u64_set",handle.name,key,value);mock::Bytes bytes(8);for(unsigned i=0;i<8;++i)bytes[i]=static_cast<unsigned char>(value>>(8*i));handle.pending[key]=bytes;return ESP_OK;
}
esp_err_t nvs_get_u64(nvs_handle_t h,const char* key,std::uint64_t* value){
 auto&handle=mock::handle(h);mock::event("u64_get",handle.name,key);const auto&blobs=mock::durable[handle.name];if(!blobs.count(key))return ESP_ERR_NVS_NOT_FOUND;*value=mock::number(blobs.at(key));return ESP_OK;
}
esp_err_t nvs_flash_init(){mock::event("nvs_init");return mock::fail_init?ESP_FAIL:ESP_OK;}
std::int64_t esp_timer_get_time(){const auto value=mock::app_time;mock::app_time+=1000;return value;}
void vTaskDelay(unsigned){mock::app_time+=1000;}
bool ot_console_install()noexcept{mock::event("console_install");return !mock::fail_install;}
bool ot_console_healthy()noexcept{return mock::console_good;}
bool ot_console_begin_session()noexcept{mock::event("console_begin");return !mock::fail_begin;}
bool ot_policy_read(char& output)noexcept{if(mock::input_at>=mock::input.size())return false;output=mock::input[mock::input_at++];return true;}
bool ot_policy_send(const char* data,std::size_t size)noexcept{mock::event("send");CHECK(mock::all_cleared());mock::receipt.assign(data,size);return !mock::fail_send;}
namespace invitation_target_test {
bool entropy_start(){mock::event("entropy_start");mock::entropy_state=mock::fail_start?security::EntropyState::failed:security::EntropyState::ready;return !mock::fail_start;}
bool entropy_stop(){mock::event("entropy_stop");mock::entropy_state=security::EntropyState::not_ready;return !mock::fail_stop;}
security::SecureRandomSource& entropy_random(){return mock::app_random;}
}
extern "C" void app_main();

using Backend=tested_target::NvsConfirmationBackend;using Error=Backend::Error;
constexpr auto P=persistence::kPersistentKvPartitionLabel,N=persistence::kPersistentKvCounterNamespace,K=persistence::kPersistentKvSlotAKey;
const std::array<std::string,7> names{"ot215_boot","ot215_ia","ot215_ib","ot215_ta","ot215_tb","ot215_ra","ot215_rb"};
static void clean_exit(){CHECK(mock::handles.empty());CHECK(mock::all_cleared());}
static std::uint64_t boot_generation(){const auto&blobs=mock::durable.at("ot215_boot");std::uint64_t highest=0;
 for(const auto&[key,bytes]:blobs){(void)key;CHECK(bytes.size()==64);std::uint64_t generation=0;for(unsigned i=0;i<8;++i)generation|=std::uint64_t(bytes[36+i])<<(8*i);highest=std::max(highest,generation);}return highest;
}
static void check_first_role_retired(){
 const auto before=mock::mutations();const auto& bytes=mock::durable.at("ot215_ra").begin()->second;CHECK(bytes.size()==64);
 security_eval::EvaluationReplayStore::Context context{};std::copy_n(bytes.begin()+12,context.size(),context.begin());
 {Backend backend(Backend::Store::rx_a);persistence::PersistentStorageKv storage(backend);security_eval::EvaluationReplayStore replay(storage);
  CHECK(replay.start(context,false)==security_eval::ReplayError::retired);CHECK(!replay.ready());}
 CHECK(mock::mutations()==before);clean_exit();
}

int main(){unsigned groups=0;
 // Actual target backend namespace mapping and exact caller contract.
 for(unsigned which=0;which<7;++which){mock::reset();{
  Backend backend(static_cast<Backend::Store>(which));CHECK(backend.ready());CHECK(mock::handles.begin()->second.name==names[which]);std::array<unsigned char,64> bytes{};bytes[0]=static_cast<unsigned char>(which+1);
  CHECK(backend.write_blob(P,N,K,bytes.data(),bytes.size())==Error::none);CHECK(backend.commit(P,N)==Error::none);
  std::array<unsigned char,64> output{};std::size_t size=0;CHECK(backend.read_blob(P,N,K,output.data(),output.size(),size)==Error::none&&size==64&&output==bytes);
  const auto calls=mock::events.size();CHECK(backend.write_blob("nvs",N,K,bytes.data(),64)==Error::invalid_argument);CHECK(backend.erase_key(P,N,"other")==Error::invalid_argument);CHECK(backend.commit(P,"ot215_boot")==Error::invalid_argument);CHECK(mock::events.size()==calls);}
  clean_exit();CHECK(mock::durable.size()==1);groups++;
 }
 {mock::reset();Backend invalid(static_cast<Backend::Store>(99));CHECK(!invalid.ready()&&mock::events.empty());groups++;}
 for(const auto& fault:{"query_error","query_short","data_error","data_short"}){mock::reset();mock::durable["ot215_ia"][K]=mock::Bytes(64,7);{
  Backend backend(Backend::Store::invite_a);mock::failure=fault;std::array<unsigned char,64> output{};output.fill(0x55);const auto before=output;std::size_t size=99;
  CHECK(backend.read_blob(P,N,K,output.data(),64,size)==Error::io_failure);CHECK(output==before&&size==0&&!backend.ready());const auto calls=mock::events.size();CHECK(backend.commit(P,N)==Error::io_failure&&mock::events.size()==calls);}
  clean_exit();groups++;
 }
 // Run the real evaluation and use its observed SDK sequence for interruption positions.
 mock::reset();mock::durable["unrelated"]["owner"]={1,2,3};mock::Random random;
 CHECK(tested_target::evaluate(random,mock::now));clean_exit();const auto baseline=mock::events;
 const auto commits=mock::count("commit");const auto random_fills=mock::random_calls;CHECK(commits>0&&random_fills>0);
 CHECK(mock::durable["unrelated"]["owner"]==mock::Bytes({1,2,3}));
 for(const auto& name:names)CHECK(mock::count("open",name)==1&&mock::count("close",name)==1&&!mock::durable[name].empty());
 CHECK(mock::first("set","ot215_boot")<mock::first("rng"));
 CHECK(mock::first("set","ot215_ia")<mock::first("set","ot215_ta"));CHECK(mock::first("set","ot215_ib")<mock::first("set","ot215_ta"));
 CHECK(mock::count("open")==7);for(std::size_t i=0;i<7;++i)CHECK(baseline[i].operation=="open");
 CHECK(mock::count("record_encrypt")==2&&mock::count("record_decrypt")==3);
 CHECK(mock::first("set","ot215_ra")<mock::first("record_encrypt")&&mock::first("set","ot215_rb")<mock::first("record_encrypt"));
 const auto second_confirmation_clock=static_cast<unsigned>(baseline[mock::first("set","ot215_tb")].value);CHECK(second_confirmation_clock>0);
 const auto old_generation=boot_generation();const auto old_ta=mock::policy("ot215_ta"),old_tb=mock::policy("ot215_tb"),old_ra=mock::policy("ot215_ra"),old_rb=mock::policy("ot215_rb");groups++;
 // Direct helper repeat reserves new boot context; retained fresh-only session
 // stores refuse before any TX/RX mutation. It is not an app reboot simulation.
 mock::reset(true);mock::Random repeated;CHECK(!tested_target::evaluate(repeated,mock::now));clean_exit();CHECK(boot_generation()>old_generation);CHECK(mock::no_policy_writes());
 CHECK(mock::policy("ot215_ta")==old_ta&&mock::policy("ot215_tb")==old_tb&&mock::policy("ot215_ra")==old_ra&&mock::policy("ot215_rb")==old_rb);groups++;
 // Actual target pending-offer decisions, with real durable confirmation and
 // real AEAD calls observed without replacing their implementation.
 for(unsigned kind=0;kind<5;++kind){mock::reset();mock::Random source;tested_target::SyntheticConfirmationInput decisions{};
  if(kind==0)decisions.a=tested_target::SyntheticDecision::cancel;
  if(kind==1)decisions.a=tested_target::SyntheticDecision::withhold;
  if(kind==2)decisions.b=tested_target::SyntheticDecision::cancel;
  if(kind==3)decisions.b=tested_target::SyntheticDecision::withhold;
  if(kind==4){mock::clock_fail_nth=second_confirmation_clock;mock::clock_failure=60102;}
  CHECK(!tested_target::evaluate(source,mock::now,decisions));clean_exit();
  CHECK(mock::count("record_encrypt")==0&&mock::count("record_decrypt")==0);
  if(kind<2){CHECK(mock::no_policy_writes());}
  else {CHECK(!mock::policy("ot215_ta").empty()&&!mock::policy("ot215_ra").empty());CHECK(mock::policy("ot215_tb").empty()&&mock::policy("ot215_rb").empty());CHECK(mock::count("commit","ot215_ra")==4);check_first_role_retired();}
  groups++;
 }
 for(const auto& name:names){mock::reset();mock::failure="open";mock::fail_namespace=name;mock::Random source;CHECK(!tested_target::evaluate(source,mock::now));clean_exit();CHECK(mock::mutations()==0&&mock::random_calls==0);groups++;}
 for(unsigned kind=0;kind<6;++kind){mock::reset();mock::Random source;
  if(kind==0)mock::entropy_state=security::EntropyState::not_ready;
  if(kind==1)mock::clock_base=UINT64_MAX;
  if(kind==2)mock::revoke_on_open=true;
  if(kind==3){mock::clock_fail_nth=2;mock::clock_failure=99;}
  if(kind==4){mock::clock_fail_nth=2;mock::clock_failure=UINT64_MAX;}
  CHECK(!tested_target::evaluate(source,kind==5?nullptr:mock::now));clean_exit();CHECK(mock::mutations()==0&&mock::random_calls==0);if(kind==0||kind==1||kind==5)CHECK(mock::events.empty());groups++;
 }
 for(unsigned nth=1;nth<=random_fills;++nth){mock::reset();mock::random_fail_nth=nth;mock::Random source;CHECK(!tested_target::evaluate(source,mock::now));clean_exit();CHECK(mock::no_policy_writes());groups++;}
 for(unsigned nth=1;nth<=commits;++nth)for(const auto&failure:{"commit","commit_applied"}){
  mock::reset();mock::failure=failure;mock::fail_nth=nth;mock::Random source;CHECK(!tested_target::evaluate(source,mock::now));clean_exit();CHECK(mock::matching>=nth);groups++;
 }
 for(const auto&name:names)for(const auto&failure:{"set","query_error","query_short","data_error","data_short","data_corrupt"}){
  mock::reset();mock::failure=failure;mock::fail_namespace=name;mock::Random source;CHECK(!tested_target::evaluate(source,mock::now));clean_exit();CHECK(mock::matching>0);groups++;
 }
 // Real app startup/control/entropy ordering. Only SDK facilities are substituted.
 {mock::reset();app_main();clean_exit();CHECK(mock::count("send")==1&&mock::receipt.find(" pass\n")!=std::string::npos);CHECK(mock::receipt.rfind("SEC_BEGIN1 ",0)==0);
  CHECK(mock::first("nvs_init")<mock::first("open","ot198diag"));CHECK(mock::first("u64_get","ot198diag")<mock::first("console_install"));
  CHECK(mock::first("console_begin")<mock::first("entropy_start"));CHECK(mock::first("entropy_start")<mock::first("sodium_init"));CHECK(mock::first("sodium_init")<mock::first("open","ot215_boot"));CHECK(mock::first("entropy_stop")<mock::first("send"));
  CHECK(mock::number(mock::durable["ot198diag"]["stage"])==security_diagnostics::encode(security_diagnostics::Stage::send_return,security_diagnostics::Error::none));
  const auto before=mock::durable;mock::reset(true);app_main();clean_exit();CHECK(mock::durable==before);CHECK(mock::count("console_install")==0&&mock::count("open","ot215_boot")==0&&mock::count("entropy_start")==0&&mock::count("send")==0);groups+=2;
 }
 for(unsigned kind=0;kind<9;++kind){mock::reset();if(kind==0)mock::fail_init=true;if(kind==1)mock::durable["ot198diag"]["owner"]={1};if(kind==2)mock::fail_install=true;if(kind==3)mock::console_good=false;if(kind==4)mock::fail_begin=true;if(kind==5)mock::fail_start=true;if(kind==6)mock::fail_sodium=true;if(kind==7)mock::fail_stop=true;if(kind==8)mock::random_fail_nth=1;
  app_main();clean_exit();if(kind<5){CHECK(mock::count("entropy_start")==0&&mock::count("entropy_stop")==0&&mock::count("send")==0);}
  else{CHECK(mock::count("entropy_start")==1&&mock::count("entropy_stop")==1&&mock::count("send")==1);CHECK(mock::receipt.find(kind==7?" entropy_contained\n":" refused\n")!=std::string::npos);}
  if(kind==5||kind==6){CHECK(mock::count("open","ot215_boot")==0);}
  if(kind==5){CHECK(mock::count("sodium_init")==0);}groups++;
 }
 std::cout<<"PASS "<<groups<<" actual confirmation target groups\n";
 std::cout<<"Real evaluation baseline: "<<commits<<" commits, "<<random_fills<<" RNG fills; SDK seams simulated; no physical interruption\n";
}
