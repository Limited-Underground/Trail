#include "opentrail/evaluation_control_synchronizing.hpp"
#include "opentrail/security_sync_record.hpp"
#include "input_control_loop.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace opentrail::security_evaluation;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static const std::string prefix=SynchronizingControl::kPrefix;
static const std::string a="0123456789abcdef0123456789abcdef";
static const std::string b="fedcba9876543210fedcba9876543210";
static const std::string command=prefix+a+"\n";
static unsigned cases=0;
static void encodable(const SynchronizingControl& c){
 using namespace opentrail::security_diagnostics;
 const SyncRecord record{static_cast<std::uint16_t>(c.prologue_bytes_exact()),
  static_cast<std::uint8_t>(c.first_frame_bytes()),c.flags(),static_cast<std::uint8_t>(c.reason()),
  static_cast<std::uint8_t>(c.frame_reason()),sync_delay_bucket(c.first_read_seen(),c.first_read_delay_us())};
 CHECK(valid_sync(record)&&encode_sync(record)!=0);
}
static void feed(SynchronizingControl& control,const std::string& bytes,std::uint64_t now=1){
 for(char byte:bytes){
  if(control.state()!=State::waiting)break;
  control.feed(byte,now);
  CHECK(control.accounting_balanced());CHECK(control.consumed()<=SynchronizingControl::kConsumedMax);
 }
 if(control.state()!=State::waiting)encodable(control);
}
static void accepted(const std::string& bytes,std::size_t discarded){
 SynchronizingControl c(0);feed(c,bytes);++cases;
 CHECK(c.state()==State::ready&&std::string(c.challenge())==a);
 CHECK(c.prologue_bytes_exact()==discarded);
 CHECK(bool(c.flags()&SynchronizingControl::kAcceptedAfterPrologue)==(discarded!=0));
 char receipt[128]{};const auto size=c.receipt(Result::pass,receipt,sizeof receipt);
 CHECK(size!=0&&std::string(receipt)=="SEC_EVAL1 ot187-policy-v0 "+a+" pass\n");
 CHECK(c.receipt(Result::pass,receipt,sizeof receipt)==0);
}
int main(){
 CHECK(prefix.size()==30&&command.size()==63);
 CHECK(SynchronizingControl::kConsumedMax==288&&SynchronizingControl::kSessionUs==20000000);
 // Prefix's failure function is zero: no proper nonempty prefix is a suffix.
 for(std::size_t n=1;n<=prefix.size();++n)for(std::size_t k=1;k<n;++k)
  CHECK(prefix.substr(0,k)!=prefix.substr(n-k,k));
 accepted(command,0);
 accepted(std::string("\xc0\0\n",3)+command,3);
 accepted("R"+command,1);
 for(std::size_t n=1;n<prefix.size();++n)accepted(prefix.substr(0,n)+"x"+command,n+1);
 for(std::size_t n:{0u,1u,31u,64u,190u,191u,192u})accepted(std::string(n,'x')+command,n);
 for(std::size_t length:{31u,32u,33u,62u,64u,94u,95u}){
  const auto malformed=prefix+std::string(length-31,'z')+"\n";
  SynchronizingControl c(0);feed(c,malformed);++cases;
  CHECK(c.state()==State::waiting&&c.frame_reason()==SyncReason::invalid_length);
  CHECK(c.first_frame_bytes()==length&&(c.flags()&SynchronizingControl::kPrologueFrame));
  CHECK(!(c.flags()&SynchronizingControl::kAcceptedAfterPrologue));
  feed(c,command);CHECK(c.state()==State::ready&&c.first_frame_bytes()==length);
  CHECK(c.flags()&SynchronizingControl::kAcceptedAfterPrologue);
 }
 {
  SynchronizingControl c(0);feed(c,"x"+prefix+"zz\n");++cases;
  CHECK(c.state()==State::waiting&&!(c.flags()&SynchronizingControl::kAcceptedAfterPrologue));
  c.poll(SynchronizingControl::kSessionUs);
  CHECK(c.reason()==SyncReason::idle_timeout&&c.frame_reason()==SyncReason::invalid_length);
  CHECK(!(c.flags()&SynchronizingControl::kAcceptedAfterPrologue));
 }
 {
  SynchronizingControl c(0);feed(c,prefix+std::string(31,'0')+"G\n"+command);++cases;
  CHECK(c.state()==State::ready&&c.frame_reason()==SyncReason::invalid_hex&&c.first_frame_bytes()==63);
 }
 // Already-read overflow trigger remains accounted, including terminal budget cases.
 for(char trigger:{'R','x','\n','\0',static_cast<char>(0xc0)}){
  const auto bytes=prefix+std::string(65,'x')+std::string(1,trigger)+command;
  SynchronizingControl c(0);feed(c,bytes);++cases;
  CHECK(c.state()==State::ready&&c.frame_reason()==SyncReason::buffer_limit);
  CHECK(c.first_frame_bytes()==95&&c.prologue_bytes_exact()==96);
 }
 {
  SynchronizingControl c(0);feed(c,std::string(191,'x')+"RUx");++cases;
  CHECK(c.state()==State::refused&&c.reason()==SyncReason::sync_limit);
  CHECK(c.consumed()==194&&c.prologue_bytes_exact()==194&&c.accounting_balanced());
 }
 {
  SynchronizingControl c(0);feed(c,std::string(192,'x')+prefix+std::string(65,'x')+"x");++cases;
  CHECK(c.state()==State::refused&&c.reason()==SyncReason::sync_limit);
  CHECK(c.frame_reason()==SyncReason::buffer_limit&&c.prologue_bytes_exact()==288&&c.consumed()==288);
 }
 {
  SynchronizingControl c(0);feed(c,"x"+command);++cases;
  const auto flags=c.flags();const auto consumed=c.consumed();
  c.feed('x',2);CHECK(c.state()==State::refused&&c.reason()==SyncReason::unexpected_state);
  CHECK(c.flags()==flags&&c.consumed()==consumed&&std::string(c.challenge())==a&&c.accounting_balanced());
  encodable(c);
 }
 {
  SynchronizingControl c(100);CHECK(!c.first_read_seen()&&c.first_read_delay_us()==UINT64_MAX);feed(c,command,100);++cases;
  CHECK(c.first_read_seen()&&c.first_read_delay_us()==0);
  SynchronizingControl delayed(100);feed(delayed,command,120);
  CHECK(delayed.first_read_seen()&&delayed.first_read_delay_us()==20);
  SynchronizingControl backwards(100);feed(backwards,"x",99);
  CHECK(backwards.reason()==SyncReason::clock_regression&&backwards.accounting_balanced());
  CHECK(backwards.first_read_seen()&&backwards.first_read_delay_us()==UINT64_MAX);
  CHECK(opentrail::security_diagnostics::sync_delay_bucket(backwards.first_read_seen(),backwards.first_read_delay_us())==7);
 }
 {
  SynchronizingControl c(0);feed(c,"R",1);c.poll(5000001);++cases;
  CHECK(c.state()==State::waiting&&c.prologue_bytes_exact()==1&&c.accounting_balanced());
  feed(c,command,5000002);CHECK(c.state()==State::ready);
  SynchronizingControl anchored(0);feed(anchored,prefix,1);anchored.poll(5000001);
  CHECK(anchored.state()==State::refused&&anchored.reason()==SyncReason::assembly_timeout);
 }
 // Actual target receive loop stops at first complete frame, even if it is stale.
 for(bool fail_health:{false,true}){
  SynchronizingControl c(0);const auto stream=prefix+b+"\n"+command;
  std::size_t index=0;std::uint64_t now=1;bool health=true;
  auto read=[&](char& byte){if(index==stream.size())return false;byte=stream[index++];if(fail_health&&byte=='\n')health=false;return true;};
  const bool received=receive_synchronizing_control(c,read,[&]{return now;},[&]{now+=10000;},[&]{return health;});++cases;
  CHECK(received==!fail_health&&index==63&&std::string(c.challenge())==b);
  CHECK(c.reason()==(fail_health?SyncReason::console_fault:SyncReason::none));
  encodable(c);
 }
 {
  SynchronizingControl c(0);std::uint64_t now=0;unsigned pauses=0;
  const bool received=receive_synchronizing_control(c,[](char&){return false;},[&]{return now;},[&]{++pauses;now+=10000;},[]{return true;});++cases;
  CHECK(!received&&c.reason()==SyncReason::idle_timeout&&now==20000000&&pauses==2000&&!c.first_read_seen());
  encodable(c);
  SynchronizingControl capped(0);now=0;pauses=0;
  CHECK(!receive_synchronizing_control(capped,[](char&){return false;},[&]{return now;},[&]{++pauses;++now;},[]{return true;}));
  CHECK(capped.reason()==SyncReason::loop_limit&&pauses==7000);
  encodable(capped);
 }
 std::printf("PASS %u synchronizing control groups; actual target receive loop, no hardware\n",cases);
}
