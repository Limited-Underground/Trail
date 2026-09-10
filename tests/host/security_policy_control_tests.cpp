#include "opentrail/evaluation_control.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace opentrail::security_evaluation;
const char* command="RUN SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef\n";
void send(Control& c,const char* s,std::uint64_t now=1){for(;*s;++s)c.feed(*s,now);}
int main(){
 {Control c(0);send(c,command);assert(c.state()==State::ready);char out[128]{};assert(c.receipt(Result::pass,out,2)==0);assert(c.receipt(Result::pass,out,sizeof(out))>0);assert(std::strcmp(out,"SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef pass\n")==0);assert(c.receipt(Result::pass,out,sizeof(out))==0);assert(c.feed('x',2)==State::refused);}
 {Control c(0);assert(c.poll(59999999)==State::waiting);send(c,command,59999999);assert(c.state()==State::ready);}
 {Control c(0);assert(c.poll(60000000)==State::refused);send(c,command,60000001);assert(c.state()==State::refused);}
 {Control c(0);c.feed('R',59000000);assert(c.poll(63999999)==State::waiting);assert(c.poll(64000000)==State::refused);}
 {Control c(10);assert(c.poll(9)==State::refused);}
 {Control c(0);c.feed('R',20);assert(c.feed('U',19)==State::refused);}
 {Control c(0);for(int i=0;i<96;++i)c.feed('x',1);assert(c.state()==State::refused);}
 {Control c(0);send(c,"RUN SEC_EVAL1 ot187-policy-v0 ABCDEF01234567890123456789012345\n");assert(c.state()==State::refused);}
 {Control c(0);send(c,"RUN SEC_EVAL1 wrong 0123456789abcdef0123456789abcdef\n");assert(c.state()==State::refused);}
 {Control c(0);send(c,command);send(c,command);char out[128];assert(c.receipt(Result::pass,out,sizeof(out))==0);}
 {Control c(0);send(c,"\n");assert(c.state()==State::refused);}
 {Control c(0);send(c,command);char out[128];assert(c.receipt(static_cast<Result>(99),out,sizeof(out))==0);assert(c.receipt(Result::refused,out,sizeof(out))>0);}
 std::puts("PASS 12 security control host groups");
}
