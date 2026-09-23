"""Compile actual enrolled target input loop against a 100 Hz SDK scheduler seam.
Compiles the exact last for-loop extracted from the current app_main.
Only dependencies are mocked; production loop framing/yield statements are intact.
This isolates scheduler pacing, not crypto/NVS execution time or physical root cause.
"""
from pathlib import Path
import subprocess,os,tempfile,shutil,unittest
root=Path(__file__).resolve().parents[2]
prefix=r'''
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cassert>
#define pdMS_TO_TICKS(ms) ((ms)/10)
constexpr int GPIO_NUM_0=0;
struct Done{};struct Halt{};
struct Record{std::string command;unsigned elapsed;};
std::string input;std::size_t cursor=0;unsigned now_us=9999,start_us=0,idle_reads=0,delay_calls=0,bytes_since_delay=0,max_bytes_without_delay=0;
std::vector<Record> records;
unsigned tick_calls=0;
bool button_mode=false;unsigned button_start=0;
std::vector<std::pair<unsigned,bool>> button_ticks;
void vTaskDelay(unsigned ticks){assert(ticks>0);now_us=(now_us/10000+ticks)*10000;++delay_calls;max_bytes_without_delay=std::max(max_bytes_without_delay,bytes_since_delay);bytes_since_delay=0;}
int gpio_get_level(int){return button_mode && now_us-button_start<50000?0:1;}
int usb_serial_jtag_read_bytes(void*out,unsigned size,unsigned timeout){assert(size==1&&timeout==1);if(cursor==input.size()){now_us+=timeout*10000;if(++idle_reads==3)throw Done{};return 0;}if(cursor==0||input[cursor-1]=='\n')start_us=now_us;if(button_mode)now_us+=9000;*static_cast<char*>(out)=input[cursor++];++bytes_since_delay;return 1;}
struct Display{};Display display;
enum class EnrolledFault{input,output};
struct Diagnostics{};Diagnostics diagnostics;
bool cleanup_done=false,diag_sent=false;
void emit_diagnostics(){assert(cleanup_done);assert(!diag_sent);diag_sent=true;}
[[noreturn]] void stopped(Display*,EnrolledFault=EnrolledFault::input){throw Halt{};}
bool send(std::string_view value){if(value.find("OTENROLL1 CLOSED ")==0)assert(diag_sent);return true;}
struct Session{bool tick(bool pressed){++tick_calls;if(button_mode)button_ticks.push_back({now_us-button_start,pressed});return true;}bool command(std::string_view command,std::array<char,768>&out,std::size_t&count){records.push_back({std::string(command),(now_us-start_us)/1000});if(command=="OTENROLL1 CLOSE"){cleanup_done=true;const std::string closed="OTENROLL1 CLOSED 1\n";std::copy(closed.begin(),closed.end(),out.begin());count=closed.size();return true;}out[0]='O';out[1]='K';out[2]='\n';count=3;return true;}};
template<class T>struct Optional{T value{};bool present=false;explicit operator bool()const{return present;}T*operator->(){return &value;}T&operator*(){return value;}template<class...A>void emplace(A&&...){present=true;}};
struct Dummy{};struct Bank{int storage=0;int*get(int){return &storage;}};
struct Allocator{bool initialize(){return true;}bool allocate(std::uint64_t&out){out=1;return true;}};
struct Entropy{int random(){return 0;}};
namespace EvaluationNamespace{constexpr int enrollment=0;}
void run(){
 Optional<Session>session;Allocator allocator;Dummy backend,authority,radio;Entropy entropy;Optional<Dummy>generation_backend,evidence;Optional<Bank>bank;
 std::array<char,700>line{};std::array<char,768>output{};std::size_t used=0,precontrol_bytes=0;bool discarded=false,admitted=false,previous_button_pressed=false;
'''
suffix=r'''
}
int main(int argc,char**argv){assert(argc==2);const bool corrected=std::string(argv[1])=="corrected";
 input="OTENROLL1 HELLO\nOTENROLL1 BEGIN "+std::string(504,'A')+"\n"+std::string(700,'X')+"\n";
 for(unsigned i=0;i<100;++i)input+="OTENROLL1 STATUS\n";
 try{run();}catch(const Done&){}catch(const Halt&){std::puts("unexpected halt");return 1;}
 assert(records.size()==103);assert(records[1].command.size()==520);assert(idle_reads==3);
 const auto begin_ms=records[1].elapsed;const auto longest_ms=records[2].elapsed;
 if(corrected){assert(begin_ms<5000);assert(longest_ms<5000);assert(max_bytes_without_delay<=701);assert(delay_calls>=103+2);assert(tick_calls==104);}
 else {assert(begin_ms>=5190);assert(longest_ms>=6990);}
 std::printf("%s BEGIN=%ums MAX_LINE=%ums max_bytes_without_delay=%u idle_reads=%u yields=%u\n",corrected?"PASS corrected":"EXPECTED-FAIL original",begin_ms,longest_ms,max_bytes_without_delay,idle_reads,delay_calls);
 if(corrected){
  // Admit first, then drip bytes every9ms: no idle read can sample release.
  input="OTENROLL1 HELLO\n"+std::string(70,'X')+"\n";cursor=0;idle_reads=0;records.clear();
  button_mode=true;button_start=now_us+400000; // Press after HELLO admission.
  try{run();}catch(const Done&){}catch(const Halt&){assert(false);}
  bool pressed=false,released=false;
  for(const auto& event:button_ticks){if(event.second)pressed=true;else if(pressed){assert(event.first<70000);released=true;break;}}
  assert(pressed && released);assert(records.size()==2);
  input="OTENROLL1 HELLO\nOTENROLL1 CLOSE\n";cursor=0;idle_reads=0;button_mode=false;
  try{run();}catch(const Done&){}catch(const Halt&){assert(false);}
  assert(cleanup_done&&diag_sent);
 }
}
'''
def main():
    env=os.environ.copy()
    compiler=env.get('CXX') or shutil.which('g++')
    if not compiler and os.name=='nt':
        compiler=str(Path(env.get('OPENTRAIL_MSYS2_ROOT','C:/msys64'))/'ucrt64/bin/g++.exe')
    if not compiler: raise RuntimeError('C++ compiler unavailable')
    env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
    source=root/'firmware/targets/heltec_v4_enrolled_eval/main/app_main.cpp'
    text=source.read_text();start=text.rindex('    for(;;){');loop=text[start:text.rindex('}')]
    with tempfile.TemporaryDirectory(prefix='ot-enrolled-pacing-') as folder:
        here=Path(folder);cpp=here/'actual-loop.cpp';exe=here/'actual-loop.exe'
        cpp.write_text(prefix+loop+suffix)
        cmd=[compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
        compiled=subprocess.run(cmd,cwd=root,env=env,text=True,capture_output=True,timeout=120)
        if compiled.returncode: raise RuntimeError(compiled.stdout+compiled.stderr)
        result=subprocess.run([str(exe),'corrected'],cwd=root,env=env,text=True,capture_output=True,timeout=10)
        if result.returncode: raise RuntimeError(result.stdout+result.stderr)
        print(result.stdout,end='')
    print('PASS 1 actual enrolled target pacing regression')
class TargetPacingTests(unittest.TestCase):
    def test_command_deadline_and_bounded_idle_yields(self):
        main()
    def test_failure_snapshot_cleanup_and_terminal_order(self):
        text=(root/'firmware/targets/heltec_v4_enrolled_eval/main/app_main.cpp').read_text()
        functions=text[text.index('void emit_diagnostics()'):text.index('\n}\nextern "C"')]
        shim=r'''
#include <array>
#include <string>
#include <vector>
#include <optional>
#include <cassert>
#include "enrolled_diagnostics.hpp"
using EnrolledFault=opentrail::security_evaluation::EnrolledFault;
using opentrail::target::heltec_v4_enrolled_eval::EnrolledDiagnostics;
std::int64_t clock_us(){return 100;}
EnrolledDiagnostics diagnostics(clock_us);
bool diagnostics_emitted=false,usb_ready=true;
std::vector<std::string> events;
enum class EndpointState{refused};
struct PairDisplay{bool show_state(EndpointState){events.push_back("display");return true;}};
struct Session{bool close(){assert(diagnostics.frozen());events.push_back("cleanup");return false;}};
std::optional<Session> session;
bool send(std::string_view line){events.emplace_back(line);return true;}
struct Done{};
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(unsigned){throw Done{};}
'''
        checks=r'''
int main(int argc,char**){
 assert(argc==2);session.emplace();PairDisplay display;
 try{stopped(&display,EnrolledFault::display);}catch(const Done&){}
 assert(events.size()==5&&events[0]=="cleanup"&&events[1]=="display");
 assert(events[2].find("OTENROLL1 DIAG 1 7 ")==0);
 assert(events[3].find("OTENROLL1 TRACE 1 7 ")==0);
 assert(events[4]=="OTENROLL1 REFUSED 0\n");
 emit_diagnostics();assert(events.size()==5);
 assert(diagnostics.snapshot().fault==7);
}
'''
        compiler=os.environ.get('CXX') or shutil.which('g++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='ot-enrolled-terminal-') as folder:
            cpp=Path(folder)/'terminal.cpp';exe=Path(folder)/'terminal.exe';cpp.write_text(shim+functions+checks)
            command=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'firmware/targets/heltec_v4_enrolled_eval/main'),'-I'+str(root/'firmware/components/security_evaluation/include'),str(cpp),'-o',str(exe)]
            built=subprocess.run(command,env=env,text=True,capture_output=True,timeout=30)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe),'failure'],env=env,text=True,capture_output=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
