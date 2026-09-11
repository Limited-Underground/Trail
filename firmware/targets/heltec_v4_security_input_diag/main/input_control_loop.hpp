#pragma once
#include "opentrail/evaluation_control_diagnostic.hpp"
namespace opentrail::security_evaluation {
// Same callback order and stop conditions as OT-187. No diagnostic I/O.
// First parser failure wins; a health failure after ready is console_fault.
template<class Read,class Clock,class Pause,class Healthy>
bool receive_diagnostic_control(DiagnosticControl& control,Read read,Clock clock,Pause pause,Healthy healthy) {
 if(control.state()!=State::waiting){control.note(InputReason::unexpected_state);return false;}
 unsigned attempts=0;bool health=true;
 for(;attempts<7000 && control.state()==State::waiting && (health=healthy());++attempts){
  char byte=0;
  for(unsigned bytes=0;bytes<96 && read(byte);++bytes){
   if(control.feed(byte,clock())!=State::waiting)break;
  }
  control.poll(clock());
  if(control.state()==State::waiting)pause();
 }
 if(control.state()==State::ready){
  const bool ready_health=healthy();
  if(!ready_health)control.note(InputReason::console_fault);
  return ready_health;
 }
 if(control.state()==State::waiting)control.note(attempts==7000?InputReason::loop_limit:!health?InputReason::console_fault:InputReason::unexpected_state);
 return false;
}
}
