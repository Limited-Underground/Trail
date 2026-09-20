#include <cstdlib>
#include <iostream>
#include <cstring>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "enrolled_diagnostics.hpp"
#include "enrolled_radio_driver.hpp"
#include "enrolled_nvs_backend.hpp"
using namespace opentrail::target::heltec_v4_enrolled_eval;
using Fault=EnrolledDiagnostics::Fault;
using Layer=EnrolledDiagnostics::Layer;
using Reason=EnrolledDiagnostics::Reason;
using Milestone=EnrolledDiagnostics::Milestone;
using Detail=EnrolledDiagnostics::Detail;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" " #x "\n";std::exit(1);}}while(0)
namespace {
bool get_failed=false;
unsigned clock_calls=0;
std::int64_t clock_us(){++clock_calls;return radio_mock::now_us;}
void reset(){radio_mock::reset();get_failed=false;clock_calls=0;}
std::string line(const EnrolledDiagnostics& d){std::array<char,256> out{};const auto n=d.format(out);return {out.data(),n};}
std::string trace_line(const EnrolledDiagnostics& d){std::array<char,512> out{};const auto n=d.format_trace(out);return {out.data(),n};}
void fixture(){
 reset();EnrolledDiagnostics d(clock_us);d.tick_begin(true);
 const auto begin=d.read_begin();radio_mock::now_us+=25;d.read_end(begin,true);
 d.radio_queued(100);d.radio_service(100);d.radio_started(100,1);
 radio_mock::now_us+=2500000;d.fault(Fault::tx_deadline);
 std::cout<<line(d);
}
void trace_fixture(const std::string& name){
 reset();EnrolledDiagnostics d(clock_us);
 d.milestone(Milestone::invitation_started,100,100,60100);
 d.milestone(Milestone::review_ready,2100,100,60100);
 d.milestone(Milestone::button_pressed,7100,100,60100);
 d.milestone(Milestone::button_released,8100,100,60100);
 d.milestone(Milestone::confirmation_accepted,8200,100,60100);
 radio_mock::now_us=9000000;
 if(name=="success"){
  d.milestone(Milestone::activation_ready,8300,100,60100);d.before_cleanup(false);
 }else if(name=="expiry" || name=="clock"){
  Detail detail{};detail.layer=Layer::bench_session;
  detail.reason=name=="expiry"?Reason::window_expired:Reason::context_changed;
  detail.sampled=detail.invitation_active=detail.session_active=true;
  detail.now_ms=name=="expiry"?60100:9000;detail.previous_ms=name=="expiry"?60099:8999;
  detail.issued_ms=100;detail.deadline_ms=60100;detail.session_started_ms=100;
  d.rejection(detail);radio_mock::now_us=static_cast<std::int64_t>(detail.now_ms*1000);
  d.fault(Fault::authority_clock);
 }else if(name=="storage"){
  EnrolledNvsBackend backend(&d);std::array<std::uint8_t,64> out{};get_failed=true;
  CHECK(!backend.read(1,opentrail::security_evaluation::EvaluationNamespace::boot,
      opentrail::persistence::StorageDomain::protocol_state,0,{out.data(),out.size()}).read());
 }else CHECK(false);
 d.before_cleanup(name!="success");
 std::cout<<line(d)<<trace_line(d)<<(name=="success"?"OTENROLL1 CLOSED 1\n":"OTENROLL1 REFUSED 1\n");
}
}
esp_err_t nvs_open(const char*,int,nvs_handle_t*out){*out=1;return ESP_OK;}
void nvs_close(nvs_handle_t){}
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*){
 radio_mock::now_us+=125;return get_failed?ESP_FAIL:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t){return ESP_FAIL;}
esp_err_t nvs_commit(nvs_handle_t){return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t,const char*){CHECK(false);return ESP_FAIL;}
int main(int argc,char**argv){
 if(argc==2 && std::string(argv[1])=="--emit-fixture"){
#ifdef _WIN32
  CHECK(_setmode(_fileno(stdout),_O_BINARY)!=-1);
#endif
  fixture();return 0;
 }
 if(argc==3 && std::string(argv[1])=="--emit-trace-case"){
#ifdef _WIN32
  CHECK(_setmode(_fileno(stdout),_O_BINARY)!=-1);
#endif
  trace_fixture(argv[2]);return 0;
 }
 CHECK(argc==1);unsigned groups=0;
 {
  reset();EnrolledDiagnostics d(clock_us);CHECK(line(d).empty());
  d.tick_begin(true);radio_mock::now_us+=200;d.tick_end();
  d.tick_begin(true);radio_mock::now_us+=300;d.fault(Fault::display);
  const auto frozen=line(d);CHECK(d.snapshot().fault==7&&d.snapshot().tick_last_us==300&&d.snapshot().tick_max_us==300);
  radio_mock::now_us+=1000000;d.tick_end();d.fault(Fault::storage);d.before_cleanup(true);
  d.read_end(0,false);d.radio_started(1000,16);CHECK(line(d)==frozen);++groups;
 }
 for(bool negative:{false,true}){
  reset();EnrolledDiagnostics d(clock_us);d.tick_begin(true);radio_mock::now_us=negative?-1:99999;
  d.tick_end();CHECK(d.frozen()&&d.snapshot().fault==3);++groups;
  CHECK(d.trace().detail.layer==Layer::diagnostics);
  CHECK(d.trace().detail.reason==(negative?Reason::invalid_clock:Reason::clock_regression));
 }
 {
  reset();EnrolledDiagnostics d(nullptr);d.tick_begin(true);CHECK(d.snapshot().fault==3&&d.frozen());++groups;
 }
 {
  CHECK(EnrolledDiagnostics::add(UINT64_MAX,1)==UINT64_MAX);
  CHECK(EnrolledDiagnostics::add(UINT64_MAX-2,9)==UINT64_MAX);
  CHECK(EnrolledDiagnostics::add(3,4)==7);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);EnrolledNvsBackend backend(&d);std::array<std::uint8_t,64> out{};
  using N=opentrail::security_evaluation::EvaluationNamespace;using D=opentrail::persistence::StorageDomain;
  CHECK(backend.read(1,N::boot,D::protocol_state,0,{out.data(),out.size()}).read());
  CHECK(d.snapshot().nvs_reads==1&&d.snapshot().nvs_read_us==125&&!d.frozen());
  get_failed=true;CHECK(!backend.read(1,N::boot,D::protocol_state,0,{out.data(),out.size()}).read());
  CHECK(d.frozen()&&d.snapshot().fault==5&&d.snapshot().nvs_reads==2&&d.snapshot().nvs_read_us==250);
  CHECK(d.trace().detail.layer==Layer::storage&&d.trace().detail.reason==Reason::storage_read);
  const auto frozen=line(d);backend.erase(1,N::boot,D::protocol_state,0);CHECK(line(d)==frozen);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);EnrolledRadioDriver driver(&d);CHECK(driver.start(10000,16));
  const std::uint8_t byte=1;CHECK(driver.send({&byte,1},0).accepted());
  radio_mock::now_us+=7000;d.fault(Fault::session_protocol);
  CHECK(d.snapshot().tx_queue_age_ms==7&&d.snapshot().tx_attempts==0);
  const auto frozen=line(d);CHECK(driver.stop());CHECK(line(d)==frozen);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);EnrolledRadioDriver driver(&d);CHECK(driver.start(10000,16));
  d.tick_begin(true);const std::uint8_t byte=1;CHECK(driver.send({&byte,1},0).accepted());driver.service(0);
  bool saw_frozen=false;radio_mock::callback=[&](const std::string& s){if(s=="gpio_7")saw_frozen=d.frozen();};
  radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;radio_mock::now_us+=2000000;driver.service(0);
  CHECK(saw_frozen&&d.snapshot().fault==8&&d.snapshot().tx_attempts==1&&d.snapshot().tx_completed==0);
  CHECK(d.trace().detail.layer==Layer::radio_driver&&d.trace().detail.reason==Reason::tx_expired);
  CHECK(d.trace().detail.sampled&&d.trace().detail.now_ms==2100&&d.trace().detail.deadline_ms==10000);
  CHECK(d.snapshot().tx_age_ms==2000&&d.snapshot().service_gap_ms==2000&&d.snapshot().tick_last_us==2000000);
  CHECK(radio_mock::count("finish_tx")==0);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);EnrolledRadioDriver driver(&d);CHECK(driver.start(10000,16));
  const std::uint8_t byte=1;CHECK(driver.send({&byte,1},0).accepted());driver.service(0);
  radio_mock::now_us+=1000;radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;driver.service(0);
  CHECK(d.snapshot().tx_attempts==1&&d.snapshot().tx_completed==1);
  d.before_cleanup(false);const auto frozen=line(d);CHECK(d.snapshot().fault==0);
  bool froze_before_stop=false;radio_mock::callback=[&](const std::string& s){if(s=="standby")froze_before_stop=d.frozen();};
  CHECK(driver.stop());CHECK(froze_before_stop&&line(d)==frozen);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);CHECK(trace_line(d).empty());
  d.milestone(Milestone::invitation_started,100,100,60100);
  d.milestone(Milestone::review_ready,0,100,60100);
  d.milestone(Milestone::review_ready,999,100,60100);
  d.milestone(Milestone::button_pressed,500,100,60100);
  d.milestone(Milestone::button_released,600,100,60100);
  d.milestone(Milestone::button_pressed,2000,100,60100);
  d.milestone(Milestone::button_released,3000,100,60100);
  d.milestone(Milestone::confirmation_accepted,3300,100,60100);
  CHECK(clock_calls==0&&d.trace().review_ms==0&&d.trace().button_press_count==2&&d.trace().button_release_count==2);
  CHECK(d.trace().button_press_ms==2000&&d.trace().button_release_ms==3000&&d.trace().milestone_mask==31);
  radio_mock::now_us=4000000;d.before_cleanup(false);
  CHECK(d.trace().frozen_us==4000000&&d.trace().detail.layer==Layer::none);
  const auto frozen=trace_line(d);const auto calls=clock_calls;
  d.milestone(Milestone::activation_ready,4100,100,60100);d.before_cleanup(true);
  CHECK(frozen==trace_line(d)&&clock_calls==calls);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);Detail detail{};
  detail.layer=Layer::handshake_endpoint;detail.reason=Reason::window_expired;
  detail.sampled=detail.invitation_active=true;detail.now_ms=60100;detail.previous_ms=60099;
  detail.issued_ms=100;detail.deadline_ms=60100;d.rejection(detail);
  detail.layer=Layer::bench_session;detail.now_ms=62000;d.rejection(detail);
  CHECK(clock_calls==0);radio_mock::now_us=63000000;d.fault(Fault::authority_clock);
  CHECK(d.trace().detail.layer==Layer::handshake_endpoint&&d.trace().detail.now_ms==60100&&d.trace().frozen_us==63000000);
  const auto frozen=trace_line(d);d.read_end(0,false);d.rejection(detail);d.fault(Fault::storage);d.before_cleanup(true);
  CHECK(trace_line(d)==frozen&&d.snapshot().fault==3);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);d.fault(Fault::storage);Detail detail{};
  detail.layer=Layer::bench_session;detail.reason=Reason::window_expired;d.rejection(detail);
  CHECK(d.snapshot().fault==5&&d.trace().detail.layer==Layer::target&&d.trace().detail.reason==Reason::unknown);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);d.radio_service(100);d.radio_service(99);
  CHECK(d.snapshot().fault==3&&d.trace().detail.layer==Layer::diagnostics&&d.trace().detail.reason==Reason::clock_regression);
  CHECK(d.trace().detail.sampled&&d.trace().detail.now_ms==99&&d.trace().detail.previous_ms==100);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);EnrolledRadioDriver driver(&d);CHECK(driver.start(10000,16));
  radio_mock::now_us=10000000;driver.service(0);
  CHECK(d.snapshot().fault==3&&d.trace().detail.layer==Layer::radio_driver&&d.trace().detail.reason==Reason::window_expired);
  CHECK(d.trace().detail.now_ms==10000&&d.trace().detail.deadline_ms==10000);
  const auto frozen=trace_line(d);CHECK(!driver.stop());CHECK(trace_line(d)==frozen);++groups;
 }
 {
  reset();EnrolledDiagnostics d(clock_us);Detail detail{};detail.layer=Layer::peer_traffic;
  detail.reason=Reason::record_rejected;detail.sampled=detail.invitation_active=detail.session_active=true;
  detail.now_ms=detail.previous_ms=detail.issued_ms=detail.deadline_ms=detail.session_started_ms=UINT64_MAX;
  for(unsigned i=1;i<=6;++i)d.milestone(static_cast<Milestone>(i),UINT64_MAX,UINT64_MAX,UINT64_MAX);
  d.rejection(detail);radio_mock::now_us=INT64_MAX;d.fault(Fault::session_protocol);
  const auto trace=trace_line(d);CHECK(!trace.empty()&&trace.size()<512&&trace.back()=='\n');
  CHECK(trace.find("18446744073709551615")!=std::string::npos);
  CHECK(std::count(trace.begin(),trace.end(),' ')==23);CHECK(sizeof(EnrolledDiagnostics)<=512);++groups;
 }
 std::cout<<"PASS "<<groups<<" enrolled diagnostics groups\n";
}
