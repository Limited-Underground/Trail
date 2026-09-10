#include "policy_control_loop.hpp"
int main(int argc,char**argv){
 check(argc==2);std::string scenario=argv[1];
 check(__esp_system_init_fn_ot_receipt_quarantine()==ESP_OK);check(ot_console_install());
 if(scenario=="loop" || scenario=="loop_cap" || scenario=="loop_invalid"){
  using namespace opentrail::security_evaluation;
  Control c(0);unsigned reads=0,pauses=0;std::uint64_t time=1;
  const std::string command="RUN SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef\n";
  std::string input=scenario=="loop"?command+command:scenario=="loop_invalid"?"bad\n": "";
  auto read=[&](char& b){++reads;if(input.empty())return false;b=input[0];input.erase(0,1);return true;};
  auto clock=[&]{return time;};auto pause=[&]{++pauses;};auto healthy=[]{return true;};
  bool ok=receive_control(c,read,clock,pause,healthy);
  if(scenario=="loop"){
   check(ok&&input==command&&reads==command.size());
   check(!receive_control(c,read,clock,pause,healthy)&&input==command&&reads==command.size());
  }else if(scenario=="loop_cap")check(!ok&&pauses==7000&&reads==7000);
  else check(!ok&&c.state()==State::refused&&reads==4);
  std::cout<<"PASS "<<scenario<<"\n";return 0;
 }
 const std::string receipt="SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef pass\n";
 if(scenario=="disabled"){check(!ot_policy_send(receipt.data(),receipt.size()));check(accepted_bytes==0);}
 else if(scenario=="read"){rx="ABC";char byte=0;check(ot_policy_read(byte)&&byte=='A');check(ot_policy_read(byte)&&byte=='B');check(ot_policy_read(byte)&&byte=='C');check(!ot_policy_read(byte));}
 else{
 check(ot_console_begin_session());
 check(!ot_console_begin_session());
 if(scenario=="stalled")stall_after_write=true;
 if(scenario=="late")overrun_write=true;
 if(scenario=="bypass")installed[0]('x');
 if(scenario=="stdio"){_reent r;check(__wrap__write_r(&r,1,"x",1)==-1&&r.error==EIO);}
 const bool ok=ot_policy_send(receipt.data(),receipt.size());
 if(scenario=="success"){
 check(ok&&delivered==receipt.substr(0,receipt.size()-1)+"\r\n");
 check(!ot_console_begin_session());
 check(!ot_policy_send(receipt.data(),receipt.size()));
 }else{check(!ok&&!ot_console_healthy());auto count=accepted_bytes;check(!ot_policy_send(receipt.data(),receipt.size())&&accepted_bytes==count);}
 }
 std::cout<<"PASS "<<scenario<<"\n";
}
