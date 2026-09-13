#pragma once
#include "opentrail/evaluation_control_synchronizing.hpp"
namespace opentrail::security_evaluation {
// Same callback order and bounds as the input diagnostic receive loop. The
// generic control seam lets host tests exercise the actual target loop.
template<class Control,class Read,class Clock,class Pause,class Healthy>
bool receive_synchronizing_control(Control& control,Read read,Clock clock,Pause pause,Healthy healthy) {
 using Reason=typename Control::Reason;
 if(control.state()!=State::waiting){control.note(Reason::unexpected_state);return false;}
 unsigned attempts=0;bool health=true;
 for(;attempts<7000&&control.state()==State::waiting&&(health=healthy());++attempts){
  char byte=0;
  for(unsigned bytes=0;bytes<96&&read(byte);++bytes){
   if(control.feed(byte,clock())!=State::waiting)break;
  }
  control.poll(clock());
  if(control.state()==State::waiting)pause();
 }
 if(control.state()==State::ready){
  const bool ready_health=healthy();
  if(!ready_health)control.note(Reason::console_fault);
  return ready_health;
 }
 if(control.state()==State::waiting)control.note(attempts==7000?Reason::loop_limit:!health?Reason::console_fault:Reason::unexpected_state);
 return false;
}
}
