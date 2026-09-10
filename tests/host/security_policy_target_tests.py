"""Compile actual policy console with SDK seams; no target source substitution."""
from pathlib import Path
import shutil,subprocess,tempfile
import receipt_console_binding_tests as receipt
import usb_console_binding_tests as base
ROOT=Path(__file__).resolve().parents[2]
def run():
    compiler=shutil.which("g++")
    if compiler is None:raise RuntimeError("native g++ required")
    with tempfile.TemporaryDirectory(prefix="policy-console-") as d:
        work=Path(d)
        stubs=dict(receipt.STUBS)
        stubs["hal/usb_serial_jtag_ll.h"]+="bool usb_serial_jtag_ll_rxfifo_data_available();\nunsigned usb_serial_jtag_ll_read_rxfifo(std::uint8_t*,unsigned);\n"
        for name,text in stubs.items():
            p=work/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        harness=base.HARNESS+"\nvoid esp_rom_delay_us(unsigned n){check(n>0&&n<=5);clock_value+=n;}\nstd::string rx;bool usb_serial_jtag_ll_rxfifo_data_available(){return !rx.empty();}\nunsigned usb_serial_jtag_ll_read_rxfifo(std::uint8_t* p,unsigned n){check(n==1&&!rx.empty());*p=rx[0];rx.erase(0,1);return 1;}\n"
        source=ROOT/"firmware/targets/heltec_v4_security_policy_eval/main/policy_console.cpp"
        cpp=work/"actual.cpp";exe=work/"actual.exe"
        cpp.write_text(harness+'\n#include "'+source.as_posix()+'"\n'+(ROOT/"tests/host/security_policy_target_tests.cpp").read_text())
        subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-I",str(work),"-I",str(ROOT/"firmware/components/diagnostics/include"),"-I",str(ROOT/"firmware/components/security_evaluation/include"),"-I",str(ROOT/"firmware/targets/heltec_v4_security_policy_eval/main"),str(cpp),"-o",str(exe)],check=True,timeout=90)
        for scenario in ["disabled","read","success","stalled","late","bypass","loop","loop_cap","loop_invalid","stdio"]:
            result=subprocess.run([str(exe),scenario],check=True,capture_output=True,text=True,timeout=10)
            if result.stdout.strip()!=f"PASS {scenario}":raise RuntimeError("unexpected output")
        print("PASS 10 actual policy console/control groups")
if __name__=="__main__":run()
