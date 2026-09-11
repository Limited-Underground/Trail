#pragma once
#include "opentrail/evaluation_control.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace opentrail::security_evaluation {
enum class InputReason : std::uint8_t { none=0,idle_timeout=9,assembly_timeout=10,clock_regression=11,buffer_limit=12,invalid_length=13,invalid_prefix=14,invalid_hex=15,loop_limit=16,console_fault=17,unexpected_state=18 };
// Single-owner, nonblocking parser. A ready transition consumes the sole request.
class DiagnosticControl {
public:
 explicit DiagnosticControl(std::uint64_t start) noexcept : start_(start),last_(start) {}
 State poll(std::uint64_t now) noexcept {
  if(state_==State::waiting && (now<last_ || now-start_>=(size_?5000000:60000000))) {
   note(now<last_?InputReason::clock_regression:size_?InputReason::assembly_timeout:InputReason::idle_timeout);state_=State::refused;
  }
  last_=now; return state_;
 }
 State feed(char byte,std::uint64_t now) noexcept {
  if(state_!=State::waiting){note(InputReason::unexpected_state);state_=State::refused;return state_;}
  if(poll(now)!=State::waiting)return state_;
  if(size_==sizeof(input_)-1){note(InputReason::buffer_limit);state_=State::refused;return state_;}
  if(size_==0)start_=now;
  input_[size_++]=byte;
  if(byte!='\n')return state_;
  constexpr char prefix[]="RUN SEC_EVAL1 ot187-policy-v0 ";
  constexpr std::size_t n=sizeof(prefix)-1;
  if(size_!=n+33){note(InputReason::invalid_length);state_=State::refused;return state_;}
  if(std::memcmp(input_,prefix,n)!=0){note(InputReason::invalid_prefix);state_=State::refused;return state_;}
  for(std::size_t i=0;i<32;++i){char c=input_[n+i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){note(InputReason::invalid_hex);state_=State::refused;return state_;}}
  std::memcpy(challenge_,input_+n,32);state_=State::ready;return state_;
 }
 InputReason reason()const noexcept{return reason_;}
 // First failure wins; receive-loop classification never changes parser state.
 void note(InputReason reason)noexcept{if(reason_==InputReason::none)reason_=reason;}
 State state()const noexcept{return state_;}
 const char* challenge()const noexcept{return challenge_;}
 // One terminal receipt, no partial output. Caller submits through bounded FIFO.
 std::size_t receipt(Result result,char* output,std::size_t capacity) noexcept {
  if(state_!=State::ready || emitted_ || !output)return 0;
  const char* name=nullptr;
  switch(result){case Result::pass:name="pass";break;case Result::refused:name="refused";break;case Result::entropy_contained:name="entropy_contained";break;case Result::nvs_unavailable:name="nvs_unavailable";break;default:return 0;}
  constexpr char prefix[]="SEC_EVAL1 ot187-policy-v0 ";
  const auto n=sizeof(prefix)-1;const auto len=std::strlen(name);const auto total=n+32+1+len+1;
  if(capacity<total+1)return 0;
  std::memcpy(output,prefix,n);std::memcpy(output+n,challenge_,32);output[n+32]=' ';
  std::memcpy(output+n+33,name,len);output[total-1]='\n';output[total]='\0';emitted_=true;return total;
 }
private:
 InputReason reason_{InputReason::none};std::uint64_t start_,last_;State state_{State::waiting};char input_[96]{};char challenge_[33]{};std::size_t size_{0};bool emitted_{false};
};
}
