"""Actual frozen policy console lifecycle with SDK/FIFO seams; no device I/O.

Each fault executes in a fresh process. Shared fixtures are read-only. Host timing
and startup callbacks do not prove physical timing or ESP-IDF startup order.
"""
from pathlib import Path
import shutil, subprocess, tempfile
import receipt_console_binding_tests as receipt
import usb_console_binding_tests as base
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/"firmware/targets/heltec_v4_security_policy_eval/main/policy_console.cpp"

def console_stubs():
    stubs=dict(receipt.STUBS)
    stubs["hal/usb_serial_jtag_ll.h"]+="bool usb_serial_jtag_ll_rxfifo_data_available();\nunsigned usb_serial_jtag_ll_read_rxfifo(std::uint8_t*,unsigned);\n"
    return stubs

def console_translation_unit():
    harness=base.HARNESS.replace('bool busy=false,',
        'bool force_block=false, zero_write=false, fault_write=false, terminal_stall=false;\nunsigned rx_reads=0; bool input_fault=false;\nbool busy=false,')
    harness=harness.replace('++writable_calls;', '++writable_calls; if(force_block)return 0;')
    harness=harness.replace('++flush_calls; check(!busy);', '++flush_calls; if(terminal_stall && accepted_bytes)stall_after_write=true; check(!busy);')
    harness=harness.replace('check(size==1 && !busy);fifo.push_back',
        "check(size==1 && !busy);if(zero_write)return 0;if(fault_write)installed[0]('x');fifo.push_back")
    return harness+r'''
void esp_rom_delay_us(unsigned n){check(n>0&&n<=5);clock_value+=n;}
std::string rx;
bool usb_serial_jtag_ll_rxfifo_data_available(){return !rx.empty();}
unsigned usb_serial_jtag_ll_read_rxfifo(std::uint8_t* p,unsigned n){
 check(n==1&&!rx.empty());++rx_reads;*p=rx[0];rx.erase(0,1);
 if(input_fault)installed[0]('x');
 return 1;
}
'''+ '\n#include "'+SOURCE.as_posix()+'"\n'+r'''
extern "C" bool lifecycle_console_start(){return __esp_system_init_fn_ot_receipt_quarantine()==ESP_OK;}
extern "C" void lifecycle_console_feed(const char* bytes){rx=bytes;}
extern "C" const char* lifecycle_console_wire(){return delivered.c_str();}
extern "C" void lifecycle_console_fault(){installed[0]('x');}
extern "C" void lifecycle_console_time(std::uint64_t us){clock_value=static_cast<std::int64_t>(us);}
extern "C" void lifecycle_console_input_fault(){input_fault=true;}
'''

MAIN=r'''
#include "policy_control_loop.hpp"
int main(int argc,char**argv){
 check(argc==2);const std::string scenario=argv[1];
 enum class Stage{boot,quarantine,installed,input,session,send,accepted};
 Stage last=Stage::boot;_reent r;
 const std::string command="RUN SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef\n";
 const std::string record="SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef pass\n";
 if(scenario=="boot"){
  check(__wrap__write_r(&r,1,"x",1)==1 && real_calls==0);
  check(!ot_console_install()&&!ot_console_healthy()&&writable_calls==0);
 }else{
  hook_during_install=scenario=="quarantine_race";
  const bool early=lifecycle_console_start();last=Stage::quarantine;
  if(scenario=="quarantine_race")check(!early);
  else check(early);
  if(scenario=="quarantine_stdio")check(__wrap__write_r(&r,1,"x",1)==-1&&r.error==EIO);
  if(scenario=="quarantine_rom")check(__wrap_uart_tx_one_char('x')==1);
  if(scenario=="quarantine_race"||scenario=="quarantine_stdio"||scenario=="quarantine_rom"){
   check(!ot_console_install()&&!ot_console_begin_session()&&accepted_bytes==0&&writable_calls==0);
   check(real_calls==0&&last==Stage::quarantine);
  }else{
   check(ot_console_install());last=Stage::installed;
   if(scenario=="input_fault"){
    using namespace opentrail::security_evaluation;
    lifecycle_console_feed(command.c_str());input_fault=true;Control control(0);
    last=Stage::input;
    check(!receive_control(control,ot_policy_read,[]{return std::uint64_t(clock_value);},[]{++clock_value;},ot_console_healthy));
    check(rx_reads==1&&!ot_console_healthy()&&!ot_console_begin_session());
    check(!ot_policy_send(record.data(),record.size())&&accepted_bytes==0&&last==Stage::input);
   }else{
    check(ot_console_begin_session());last=Stage::session;
    if(scenario=="fifo_unavailable")force_block=true;
    if(scenario=="zero_write")zero_write=true;
    if(scenario=="write_fault")fault_write=true;
    if(scenario=="deadline")overrun_write=true;
    if(scenario=="terminal_flush")terminal_stall=true;
    if(scenario=="reentry")receipt_active.test_and_set();
    last=Stage::send;bool ok=ot_policy_send(record.data(),record.size());
    if(ok)last=Stage::accepted;
    if(scenario=="success")check(ok&&delivered==record.substr(0,record.size()-1)+"\r\n"&&last==Stage::accepted);
    else{
     check(!ok&&!ot_console_healthy()&&last==Stage::send);
     if(scenario=="fifo_unavailable"||scenario=="zero_write"||scenario=="reentry")check(accepted_bytes==0);
     if(scenario=="terminal_flush")check(accepted_bytes==record.size()+1&&delivered.empty());
     if(scenario=="deadline"||scenario=="write_fault")check(accepted_bytes==1);
     check(clock_value<21000 || scenario=="deadline");
    }
    auto count=accepted_bytes;auto calls=writable_calls;
    check(!ot_console_begin_session()&&!ot_policy_send(record.data(),record.size()));
    check(count==accepted_bytes&&calls==writable_calls);
   }
  }
 }
 std::cout<<"PASS "<<scenario<<"\n";
}
'''
SCENARIOS=("boot","quarantine_race","quarantine_stdio","quarantine_rom","input_fault",
           "success","fifo_unavailable","zero_write","write_fault","deadline","terminal_flush","reentry")
def run():
    compiler=shutil.which("g++")
    if compiler is None:raise RuntimeError("native g++ required")
    with tempfile.TemporaryDirectory(prefix="policy-console-lifecycle-") as d:
        work=Path(d)
        for name,text in console_stubs().items():
            p=work/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding="utf-8")
        cpp=work/"actual.cpp";exe=work/"actual.exe"
        cpp.write_text(console_translation_unit()+MAIN,encoding="utf-8")
        subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-I",str(work),
            "-I",str(ROOT/"firmware/components/diagnostics/include"),
            "-I",str(ROOT/"firmware/components/security_evaluation/include"),
            "-I",str(SOURCE.parent),str(cpp),"-o",str(exe)],check=True,timeout=90)
        for scenario in SCENARIOS:
            result=subprocess.run([str(exe),scenario],check=True,capture_output=True,text=True,timeout=10)
            if result.stdout.strip()!=f"PASS {scenario}":raise RuntimeError("unexpected lifecycle output")
        print(f"PASS {len(SCENARIOS)} actual policy console lifecycle groups")
if __name__=="__main__":run()
