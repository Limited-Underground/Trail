#pragma once
#include "opentrail/evaluation_control.hpp"
namespace opentrail::security_evaluation {
// Call once per app invocation. All callbacks must be bounded/nonblocking except
// pause, which must use a bounded scheduler delay. Stops at the first complete
// command; queued later bytes are not executed. No absence-of-future-input claim.
template<class Read,class Clock,class Pause,class Healthy>
bool receive_control(Control& control,Read read,Clock clock,Pause pause,Healthy healthy) {
 if(control.state()!=State::waiting)return false;
 for(unsigned attempts=0;attempts<7000 && control.state()==State::waiting && healthy();++attempts){
  char byte=0;
  for(unsigned bytes=0;bytes<96 && read(byte);++bytes){
   if(control.feed(byte,clock())!=State::waiting)break;
  }
  control.poll(clock());
  if(control.state()==State::waiting)pause();
 }
 return control.state()==State::ready && healthy();
}
}
