#include "opentrail/evaluation_control_diagnostic.hpp"
#include "firmware/targets/heltec_v4_security_policy_eval/main/policy_control_loop.hpp"
#include "firmware/targets/heltec_v4_security_input_diag/main/input_control_loop.hpp"
#include "firmware/components/security_diagnostics/include/opentrail/security_input_record.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace opentrail::security_evaluation;
const std::string command="RUN SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef\n";
void equal(Control& old,DiagnosticControl& current){
 assert(old.state()==current.state());assert(std::string(old.challenge())==current.challenge());
}
void feed(Control& old,DiagnosticControl& current,const std::string& input,std::uint64_t t){
 for(char c:input){assert(old.feed(c,t)==current.feed(c,t));equal(old,current);}
}
void reason(const std::string& input,InputReason expected){Control old(0);DiagnosticControl current(0);feed(old,current,input,1);assert(current.reason()==expected);}
struct Trace {
 std::vector<std::string> calls;std::uint64_t now=0;unsigned h=0,reads=0;std::size_t pos=0;int scenario;
 std::string input;
 explicit Trace(int s):scenario(s),input(command){if(s==1||s==5||s==6||s==7)input.clear();if(s==2)input="R";if(s==3)input="bad\n";if(s==9)input+=command;}
 bool read(char& out){calls.push_back("read");++reads;if(scenario==8&&reads==1)return false;if(pos==input.size())return false;out=input[pos++];return true;}
 std::uint64_t clock(){calls.push_back("clock");return now;}
 void pause(){calls.push_back("pause");if(scenario!=6)now+=10000;if(scenario==7)now=0;}
 bool healthy(){calls.push_back("healthy");++h;return !((scenario==4&&h==2)||(scenario==5));}
};
void trace(int scenario,InputReason expected){
 Trace a(scenario),b(scenario);Control old(scenario==7?1:0);DiagnosticControl current(scenario==7?1:0);
 auto runold=receive_control(old,[&](char& c){return a.read(c);},[&]{return a.clock();},[&]{a.pause();},[&]{return a.healthy();});
 auto runnew=receive_diagnostic_control(current,[&](char& c){return b.read(c);},[&]{return b.clock();},[&]{b.pause();},[&]{return b.healthy();});
 assert(runold==runnew);equal(old,current);assert(a.calls==b.calls);assert(a.pos==b.pos);assert(current.reason()==expected);
 char x[128]{},y[128]{};assert(old.receipt(Result::pass,x,sizeof x)==current.receipt(Result::pass,y,sizeof y));assert(std::string(x)==y);
}
int main(){
 // Fourteen parser/state groups, all compared to the frozen original.
 {Control a(0);DiagnosticControl b(0);feed(a,b,command,1);assert(b.reason()==InputReason::none);for(auto r:{Result::pass,Result::refused}){char x[128]{},y[128]{};assert(a.receipt(r,x,sizeof x)==b.receipt(r,y,sizeof y));assert(std::string(x)==y);}}
 {Control a(0);DiagnosticControl b(0);assert(a.poll(59999999)==b.poll(59999999));feed(a,b,command,59999999);assert(b.state()==State::ready);}
 {Control a(0);DiagnosticControl b(0);assert(a.poll(60000000)==b.poll(60000000));assert(b.reason()==InputReason::idle_timeout);feed(a,b,command,60000001);assert(b.reason()==InputReason::idle_timeout);}
 {Control a(0);DiagnosticControl b(0);feed(a,b,"R",59000000);assert(a.poll(63999999)==b.poll(63999999));assert(a.poll(64000000)==b.poll(64000000));assert(b.reason()==InputReason::assembly_timeout);}
 {Control a(10);DiagnosticControl b(10);assert(a.poll(9)==b.poll(9));assert(b.reason()==InputReason::clock_regression);}
 {Control a(0);DiagnosticControl b(0);feed(a,b,"R",20);feed(a,b,"U",19);assert(b.reason()==InputReason::clock_regression);}
 reason(std::string(96,'x'),InputReason::buffer_limit);
 reason("\n",InputReason::invalid_length);
 {auto s=command;s[0]='X';reason(s,InputReason::invalid_prefix);}
 {auto s=command;s[s.size()-2]='G';reason(s,InputReason::invalid_hex);}
 reason(command.substr(0,command.size()-1)+"\r\n",InputReason::invalid_length);
 {Control a(0);DiagnosticControl b(0);feed(a,b,command,1);feed(a,b,"x",2);assert(b.reason()==InputReason::unexpected_state);}
 {Control a(0);DiagnosticControl b(0);feed(a,b,command,1);char x[128]{},y[128]{};for(auto r:{static_cast<Result>(99),Result::entropy_contained}){assert(a.receipt(r,x,2)==b.receipt(r,y,2));assert(a.receipt(r,x,sizeof x)==b.receipt(r,y,sizeof y));assert(std::string(x)==y);}}
 {unsigned seed=12345;for(unsigned test=0;test<500;++test){Control a(10);DiagnosticControl b(10);std::uint64_t t=10;for(unsigned i=0;i<128;++i){seed=seed*1664525u+1013904223u;t+=(seed%1000001);if(seed%31==0)t=0;if(seed%3==0)assert(a.poll(t)==b.poll(t));else {char c=static_cast<char>(seed>>24);assert(a.feed(c,t)==b.feed(c,t));}equal(a,b);}}}
 const InputReason expected[]={InputReason::none,InputReason::idle_timeout,InputReason::assembly_timeout,InputReason::invalid_length,InputReason::console_fault,InputReason::console_fault,InputReason::loop_limit,InputReason::clock_regression,InputReason::none,InputReason::none};
 for(int i=0;i<10;++i)trace(i,expected[i]);
 std::puts("PASS 24 input diagnostic host groups");
 using namespace opentrail::security_diagnostics;
 for(unsigned s=0;s<10;++s)for(unsigned e=0;e<256;++e){
  auto stage=static_cast<Stage>(s);auto error=static_cast<Error>(e);const auto value=encode(stage,error);
  assert((value!=0)==valid(stage,error));
  if(value)std::printf("RECORD %u %u %llu\n",s,e,static_cast<unsigned long long>(value));
 }
}
