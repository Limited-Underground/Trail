// Compile the actual OT-200 app as a separate translation unit. Only external
// SDK/NVS/clock/entropy services are simulated; no physical interruption claim.
#define nvs_open baseline_nvs_open
#define nvs_close baseline_nvs_close
#define nvs_commit baseline_nvs_commit
#define esp_timer_get_time baseline_esp_timer_get_time
#define ot_policy_read baseline_ot_policy_read
#define ot_policy_send baseline_ot_policy_send
#define ot_console_install baseline_ot_console_install
#define ot_console_healthy baseline_ot_console_healthy
#define ot_console_session_started baseline_ot_console_session_started
#define ot_console_begin_session baseline_ot_console_begin_session
#include "external_fixture.hpp"
#undef nvs_open
#undef nvs_close
#undef nvs_commit
#undef esp_timer_get_time
#undef ot_policy_read
#undef ot_policy_send
#undef ot_console_install
#undef ot_console_healthy
#undef ot_console_session_started
#undef ot_console_begin_session
#include "opentrail/security_sync_record.hpp"
#ifdef ACTUAL_CONSOLE
extern "C" void lifecycle_console_send_fault();
extern "C" std::size_t lifecycle_console_remaining();
#endif

namespace ordering {
constexpr nvs_handle_t handle=0x3200;
constexpr std::uint64_t horizon=30000000;
// Slot 9 is the detail key written between stages 3 and 4.
struct Event { std::string name; unsigned slot; std::uint64_t at; };
std::vector<Event> trace;
std::vector<unsigned> set_slots;
std::map<std::string,std::uint64_t> durable;
std::string mode="none",pending_key;
unsigned fail_slot=0,sets=0,commits=0,gets=0,read_bytes=0;
std::uint64_t pending=0,installed_at=0,first_clock=0,first_read=0;
bool exists=false,closed=false,clock_seen=false,read_seen=false;
std::array<std::uint64_t,10> commit_cost{},get_cost{};
unsigned stage_of(std::uint64_t value){return static_cast<unsigned>((value>>32)&255);}
unsigned slot_of(const std::string& key,std::uint64_t value){return key=="input"?9:stage_of(value);}
void event(const char* name,unsigned slot=0){trace.push_back({name,slot,lifecycle::clock});}
bool failing(unsigned slot,const char* operation){return fail_slot==slot&&mode==operation;}
std::size_t index(const char* name,unsigned slot=0){
 for(std::size_t i=0;i<trace.size();++i)if(trace[i].name==name&&trace[i].slot==slot)return i;
 return trace.size();
}
unsigned durable_before(std::uint64_t cutoff){
 unsigned result=0;
 for(const auto& e:trace)if(e.name=="durable"&&e.slot!=9&&e.at<cutoff)result=e.slot;
 return result;
}
bool detail_before(std::uint64_t cutoff){
 for(const auto& e:trace)if(e.name=="durable"&&e.slot==9&&e.at<cutoff)return true;
 return false;
}
unsigned count(const char* name){return static_cast<unsigned>(std::count(lifecycle::events.begin(),lifecycle::events.end(),name));}
unsigned stage(){const auto it=durable.find("stage");return it==durable.end()?0:stage_of(it->second);}
void before(const char* first,unsigned a,const char* second,unsigned b){
 CHECK(index(first,a)<trace.size()&&index(second,b)<trace.size());
 CHECK(index(first,a)<index(second,b));
}
}

extern "C" void lifecycle_sdk_event(const char* name){ordering::event(name);}
extern "C" std::uint64_t lifecycle_shared_time(){return ++lifecycle::clock;}
std::int64_t esp_timer_get_time(){
 if(!ordering::clock_seen){ordering::clock_seen=true;ordering::first_clock=lifecycle::clock;ordering::event("control_epoch");}
 return static_cast<std::int64_t>(lifecycle::clock);
}
bool ot_console_install()noexcept{
 ordering::installed_at=lifecycle::clock;ordering::event("install");
 return baseline_ot_console_install();
}
bool ot_console_healthy()noexcept{return baseline_ot_console_healthy();}
bool ot_console_begin_session()noexcept{
 ordering::event("session");return baseline_ot_console_begin_session();
}
bool ot_policy_read(char& byte)noexcept{
 const bool read=baseline_ot_policy_read(byte);
 if(read){
  // Simulate the first SDK clock sample regressing after parser construction.
  // The already-read byte must be accounted for, refused, and stored as an
  // unknown/high delay bucket rather than a plausible zero-delay observation.
  if(!ordering::read_seen&&lifecycle::scenario=="first_read_before_epoch"){
   CHECK(ordering::clock_seen&&ordering::first_clock>0);
   lifecycle::clock=ordering::first_clock-1;
  }
  ++ordering::read_bytes;
  if(!ordering::read_seen){ordering::read_seen=true;ordering::first_read=lifecycle::clock;ordering::event("first_read");}
 }
 if(read&&byte=='\n'&&lifecycle::scenario=="ready_health_failure")lifecycle::healthy=false;
 return read;
}
bool ot_policy_send(const char* bytes,std::size_t size)noexcept{
 ordering::event("send");return baseline_ot_policy_send(bytes,size);
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){
 if(std::string(name)!="ot198diag")return baseline_nvs_open(name,mode,out);
 ordering::event("stage_open");
 if(mode==NVS_READONLY){
  if(lifecycle::scenario=="open_fail")return ESP_FAIL;
  if(!ordering::exists)return ESP_ERR_NVS_NOT_FOUND;
  *out=ordering::handle;return ESP_OK;
 }
 CHECK(mode==NVS_READWRITE);ordering::exists=true;*out=ordering::handle;
 return lifecycle::scenario=="create_fail"?ESP_FAIL:ESP_OK;
}
void nvs_close(nvs_handle_t h){
 if(h==ordering::handle){ordering::closed=true;ordering::event("close");}
 else baseline_nvs_close(h);
}
esp_err_t nvs_set_u64(nvs_handle_t h,const char* key,std::uint64_t value){
 CHECK(h==ordering::handle&&(std::string(key)=="stage"||std::string(key)=="input"));
 ++ordering::sets;ordering::pending=value;ordering::pending_key=key;
 const auto slot=ordering::slot_of(key,value);ordering::event("set",slot);ordering::set_slots.push_back(slot);
 return ordering::failing(slot,"set")?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
 if(h!=ordering::handle)return baseline_nvs_commit(h);
 ++ordering::commits;const auto slot=ordering::slot_of(ordering::pending_key,ordering::pending);
 ordering::event("commit_begin",slot);lifecycle::clock+=ordering::commit_cost.at(slot);
 if(ordering::failing(slot,"commit_before")){ordering::event("commit_error",slot);return ESP_FAIL;}
 ordering::durable[ordering::pending_key]=ordering::pending;ordering::event("durable",slot);
 if(ordering::failing(slot,"commit_applied")){ordering::event("commit_error",slot);return ESP_FAIL;}
 ordering::event("commit_return",slot);return ESP_OK;
}
esp_err_t nvs_get_u64(nvs_handle_t h,const char* key,std::uint64_t* out){
 CHECK(h==ordering::handle&&(std::string(key)=="stage"||std::string(key)=="input"));
 ++ordering::gets;CHECK(ordering::durable.count(key)==1);
 const auto slot=ordering::slot_of(key,ordering::durable.at(key));
 ordering::event("verify_begin",slot);lifecycle::clock+=ordering::get_cost.at(slot);
 *out=ordering::durable.at(key);
 if(ordering::failing(slot,"get_corrupt"))*out^=1;
 if(ordering::failing(slot,"get")){ordering::event("verify_error",slot);return ESP_FAIL;}
 ordering::event("verify_return",slot);return ESP_OK;
}

// Independent extraction checks actual persisted bytes against scenario facts.
// The product decoder is an additional check, not the expected-value oracle.
void detail_is(unsigned discarded,unsigned first_bytes,unsigned flags,unsigned terminal,unsigned first_reason,unsigned delay){
 CHECK(ordering::durable.count("input")==1);
 const auto raw=ordering::durable.at("input");
 CHECK((raw&4095)==0xa20);
 CHECK(((raw>>12)&511)==discarded&&((raw>>21)&127)==first_bytes);
 CHECK(((raw>>28)&127)==flags&&((raw>>35)&31)==terminal);
 CHECK(((raw>>40)&31)==first_reason&&((raw>>45)&7)==delay);
 unsigned crc=65535;
 for(unsigned byte=0;byte<6;++byte){
  crc^=static_cast<unsigned>((raw>>(8*byte))&255)<<8;
  for(unsigned bit=0;bit<8;++bit)crc=((crc&32768)?(crc<<1)^4129:crc<<1)&65535;
 }
 CHECK((raw>>48)==crc);
 opentrail::security_diagnostics::SyncRecord decoded;
 CHECK(opentrail::security_diagnostics::decode_sync(raw,decoded));
}

int main(int argc,char** argv){
#ifdef _WIN32
 _setmode(_fileno(stdout),_O_BINARY);
#endif
 CHECK(argc==2||argc==4);
 const std::string name=argv[1];lifecycle::scenario=name;
 const std::string challenge="0123456789abcdef0123456789abcdef";
 const std::string stale="fedcba9876543210fedcba9876543210";
 const std::string prefix="RUN SEC_EVAL1 ot187-policy-v0 ";
 const std::string command=prefix+challenge+"\n";
 const std::string malformed=prefix+"x\n";
 std::string invalid_hex=command;invalid_hex[command.size()-2]='G';
 lifecycle::input=command;ordering::commit_cost.fill(100);
 const bool actual=name.rfind("actual_",0)==0;
 const std::string kind=actual?name.substr(7):name;
 if(name=="failure"){
  CHECK(argc==4);ordering::fail_slot=static_cast<unsigned>(std::stoul(argv[2]));ordering::mode=argv[3];
  CHECK(ordering::fail_slot>=1&&ordering::fail_slot<=9);
  CHECK(ordering::mode=="set"||ordering::mode=="commit_before"||ordering::mode=="commit_applied"||ordering::mode=="get"||ordering::mode=="get_corrupt");
 }else CHECK(argc==2);
 if(kind=="prologue")lifecycle::input=std::string("\0\xc0\nx",4)+command;
 if(kind=="malformed_then_valid")lifecycle::input=malformed+command;
 if(name=="hex_then_valid")lifecycle::input=invalid_hex+command;
 if(name=="budget193")lifecycle::input=std::string(193,'x')+command;
 if(name=="bulk288")lifecycle::input=std::string(192,'x')+prefix+std::string(65,'x')+"y"+command;
 if(name=="malformed_only")lifecycle::input=malformed;
 if(name=="invalid_hex_only")lifecycle::input=invalid_hex;
 if(name=="no_input"||name=="poll_cap")lifecycle::input.clear();
 if(name=="partial")lifecycle::input="R";
 if(name=="frame_timeout")lifecycle::input=prefix+"a";
 if(name=="ready_health_failure")lifecycle::input="x"+command;
 if(kind=="duplicate")lifecycle::input=command+command;
 if(name=="stale_then_fresh")lifecycle::input=prefix+stale+"\n"+command;
 if(name=="late_first")lifecycle::available_at=20000000+300;
 if(name=="preexisting")ordering::exists=true;
 if(name=="startup_costs"){ordering::commit_cost[1]=5000000;ordering::commit_cost[2]=7000000;ordering::commit_cost[3]=11000000;}
 if(name=="slow_startup")ordering::commit_cost[1]=31000000;
 if(name=="slow_waiting")ordering::commit_cost[3]=31000000;
 if(name=="slow_detail_commit")ordering::commit_cost[9]=31000000;
 if(name=="slow_detail_verify")ordering::get_cost[9]=31000000;
 if(name=="slow_input")ordering::commit_cost[4]=31000000;
 if(name=="slow_verify")ordering::get_cost[4]=31000000;
 if(name=="slow_send_return")ordering::commit_cost[8]=31000000;
#ifdef ACTUAL_CONSOLE
 CHECK(actual);CHECK(lifecycle_console_start());
 // The fixture takes a C string; NUL is delivered separately through the mock
 // console scenarios, while real-console composition uses equivalent text noise.
 if(kind=="prologue")lifecycle::input="noise\n"+command;
 lifecycle_console_feed(lifecycle::input.c_str());
 if(kind=="fault")lifecycle_console_fault();
 if(kind=="input_fault")lifecycle_console_input_fault();
 if(kind=="send_fault")lifecycle_console_send_fault();
#else
 CHECK(!actual);
#endif
 app_main(); // Direct actual source; no substitute parser/evaluator implementation.
#ifdef ACTUAL_CONSOLE
 lifecycle::wire=lifecycle_console_wire();
#endif
 using namespace opentrail::security_diagnostics;
 const unsigned last=ordering::stage();
 const auto error=ordering::durable.count("stage")?static_cast<unsigned>((ordering::durable.at("stage")>>40)&255):0;
 if(last)CHECK(ordering::durable.at("stage")==encode(static_cast<Stage>(last),static_cast<Error>(error)));
 const bool startup_stop=name=="preexisting"||name=="nvs_fail"||name=="open_fail"||name=="create_fail";
 if(startup_stop){
  CHECK(last==0&&ordering::sets==0&&lifecycle::wire.empty());
  CHECK(!ordering::clock_seen&&ordering::count("entropy_start")==0);
 }else{
  CHECK(ordering::closed);
  if(name=="failure"){
   const auto slot=ordering::fail_slot;
   const unsigned attempted=slot==9?4:slot+(slot>=4?1:0);
   CHECK(ordering::sets==attempted);
   CHECK(ordering::commits==attempted-(ordering::mode=="set"?1u:0u));
   CHECK(ordering::gets==attempted-((ordering::mode=="set"||ordering::mode=="commit_before"||ordering::mode=="commit_applied")?1u:0u));
   const bool not_applied=ordering::mode=="set"||ordering::mode=="commit_before";
   CHECK(last==(slot==9?3:slot-(not_applied?1:0)));
   CHECK(ordering::durable.count("input")==static_cast<unsigned>(slot==9?!not_applied:slot>=4));
   CHECK(ordering::count("entropy_start")==static_cast<unsigned>(slot>=6&&slot<=8));
   CHECK(ordering::count("entropy_stop")==static_cast<unsigned>(slot>=6&&slot<=8));
   CHECK(lifecycle::sends==static_cast<unsigned>(slot==8));
   CHECK(lifecycle::wire.empty()==(slot!=8));
   CHECK(ordering::set_slots.back()==slot);
   if(slot<=3)CHECK(!ordering::clock_seen);
  }else if(name=="install_fail"||name=="actual_fault"){
   CHECK(last==2&&error==1&&ordering::sets==2&&lifecycle::wire.empty());
   CHECK(!ordering::clock_seen&&ordering::durable.count("input")==0);
  }else{
   const bool refused=name=="budget193"||name=="bulk288"||name=="malformed_only"||name=="invalid_hex_only"||
    name=="no_input"||name=="partial"||name=="frame_timeout"||name=="ready_health_failure"||
    kind=="input_fault"||name=="begin_fail"||name=="poll_cap"||name=="late_first"||name=="first_read_before_epoch";
   CHECK(last==(refused?4u:8u));
   CHECK(ordering::sets==(refused?5u:9u)&&ordering::commits==ordering::sets&&ordering::gets==ordering::sets);
   CHECK(ordering::count("entropy_start")==static_cast<unsigned>(!refused));
   CHECK(ordering::count("entropy_stop")==static_cast<unsigned>(!refused));
   if(refused)CHECK(lifecycle::wire.empty());
   unsigned discarded=0,first_bytes=0,flags=0,terminal=0,first_reason=0,delay=0;
   if(kind=="prologue"){discarded=actual?6:4;flags=actual?67:111;}
   if(kind=="malformed_then_valid"||name=="malformed_only"){
    discarded=32;first_bytes=32;first_reason=13;flags=name=="malformed_only"?19:83;
   }
   if(name=="hex_then_valid"||name=="invalid_hex_only"){
    discarded=63;first_bytes=63;first_reason=15;flags=name=="invalid_hex_only"?19:83;
   }
   if(name=="budget193"){discarded=193;flags=1;terminal=19;CHECK(ordering::read_bytes==193);}
   if(name=="bulk288"){discarded=288;first_bytes=95;flags=17;terminal=19;first_reason=12;CHECK(ordering::read_bytes==288);}
   if(name=="no_input"||name=="partial"||name=="malformed_only"||name=="invalid_hex_only")terminal=9;
   if(name=="no_input"||name=="poll_cap")delay=7;
   if(name=="partial"){discarded=1;flags=1;}
   if(name=="frame_timeout")terminal=10;
   if(name=="ready_health_failure"){terminal=17;discarded=1;flags=65;}
   if(kind=="input_fault")terminal=17;
   if(name=="begin_fail")terminal=3;
   if(name=="poll_cap"){terminal=16;CHECK(lifecycle::pauses==7000);}
   if(name=="late_first"){terminal=9;discarded=1;flags=1;delay=6;CHECK(ordering::read_bytes==1);}
   if(name=="first_read_before_epoch"){
    terminal=11;discarded=1;flags=1;delay=7;
    CHECK(ordering::read_bytes==1&&lifecycle::input==command.substr(1));
    CHECK(ordering::first_read+1==ordering::first_clock);
   }
   detail_is(discarded,first_bytes,flags,terminal,first_reason,delay);
   if(refused)CHECK(error==(terminal==19?16:terminal));
   if(kind=="duplicate"||name=="stale_then_fresh"){
    CHECK(ordering::read_bytes==63);
#ifdef ACTUAL_CONSOLE
    CHECK(lifecycle_console_remaining()==63);
#else
    CHECK(lifecycle::input==command);
#endif
   }
   const bool send_fault=name=="send_fail"||name=="entropy_fault"||name=="actual_send_fault";
   if(!refused){
    CHECK(error==(send_fault?8u:0u));
    std::string result=name=="stop_fail"?"entropy_contained":
      name=="start_fail"||name=="sodium_fail"||name=="evaluate_fail"||name=="namespace_fail"?"refused":"pass";
    if(send_fault)CHECK(lifecycle::wire.empty());
    else CHECK(lifecycle::wire=="SEC_EVAL1 ot187-policy-v0 "+(name=="stale_then_fresh"?stale:challenge)+" "+result+(actual?"\r\n":"\n"));
    if(name=="normal"||name=="actual_normal")CHECK(ordering::count("nvs_write")>0&&ordering::count("random_fill")>0);
   }
  }
 }
 // All key writes form exactly one prefix of the production ordering.
 const std::vector<unsigned> expected{1,2,3,9,4,5,6,7,8};
 CHECK(ordering::set_slots.size()<=expected.size());
 CHECK(std::equal(ordering::set_slots.begin(),ordering::set_slots.end(),expected.begin()));
 if(ordering::index("set",4)<ordering::trace.size()){
  ordering::before("durable",9,"verify_return",9);
  ordering::before("verify_return",9,"set",4);
 }
 if(ordering::count("entropy_start")){
  ordering::before("verify_return",9,"entropy_start",0);
  ordering::before("verify_return",5,"entropy_start",0);
  ordering::before("entropy_stop",0,"set",6);
 }
 if(ordering::clock_seen){
  ordering::before("verify_return",3,"control_epoch",0);
  CHECK(ordering::first_clock==ordering::commit_cost[1]+ordering::commit_cost[2]+ordering::commit_cost[3]);
  CHECK(ordering::first_clock-ordering::installed_at==ordering::commit_cost[2]+ordering::commit_cost[3]);
 }
 if(ordering::read_seen&&name!="late_first"&&name!="first_read_before_epoch")CHECK(ordering::first_read==ordering::first_clock);
 if(ordering::index("send")<ordering::trace.size()){
  ordering::before("verify_return",7,"send",0);
  ordering::before("send",0,"set",8);
 }
 if(name=="startup_costs")CHECK(ordering::installed_at==5000000&&ordering::first_clock==23000000);
 if(name=="slow_startup")CHECK(ordering::first_clock>ordering::horizon&&ordering::durable_before(ordering::horizon)==0);
 if(name=="slow_waiting")CHECK(ordering::first_clock>ordering::horizon&&ordering::durable_before(ordering::horizon)==2);
 if(name=="slow_detail_commit")CHECK(ordering::durable_before(ordering::horizon)==3&&!ordering::detail_before(ordering::horizon));
 if(name=="slow_detail_verify"){
  CHECK(ordering::durable_before(ordering::horizon)==3&&ordering::detail_before(ordering::horizon));
  CHECK(ordering::trace.at(ordering::index("verify_return",9)).at>ordering::horizon);
 }
 if(name=="slow_input")CHECK(ordering::first_read<ordering::horizon&&ordering::durable_before(ordering::horizon)==3&&ordering::detail_before(ordering::horizon));
 if(name=="slow_verify"){
  CHECK(ordering::durable_before(ordering::horizon)==4);
  CHECK(ordering::trace.at(ordering::index("verify_return",4)).at>ordering::horizon);
 }
 if(name=="slow_send_return"){
  CHECK(ordering::trace.at(ordering::index("send")).at<ordering::horizon);
  CHECK(ordering::durable_before(ordering::horizon)==7);
 }
 std::cout<<"PASS sync lifecycle "<<name;
 if(name=="failure")std::cout<<" slot="<<ordering::fail_slot<<" operation="<<ordering::mode;
 std::cout<<" durable_stage="<<last<<" first_clock_us="<<ordering::first_clock
          <<" installed_us="<<ordering::installed_at<<" final_clock_us="<<lifecycle::clock
          <<" durable_prefix_before_30s="<<ordering::durable_before(ordering::horizon)
          <<" detail_before_30s="<<ordering::detail_before(ordering::horizon)
          <<" receipt_us="<<(ordering::index("send")<ordering::trace.size()?ordering::trace.at(ordering::index("send")).at:0)<<"\n";
 if(!lifecycle::wire.empty())std::cout<<lifecycle::wire;
}
