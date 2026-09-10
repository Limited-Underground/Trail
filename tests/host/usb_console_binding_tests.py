"""Compile the actual board console binding against bounded TX-only SDK seams.

Each scenario runs in a fresh process: sticky globals are never reset by tests.
No RX API, USB driver installer, radio or device is supplied by these stubs.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/targets/heltec_v4_noise_xk_console/main/opentrail_console.cpp"
STUBS = {
    "reent.h": "#pragma once\nstruct _reent { int error = 0; };\n#define __errno_r(r) ((r)->error)\n",
    "esp_attr.h": "#pragma once\n#define IRAM_ATTR\n#define DRAM_ATTR\n",
    "esp_rom_sys.h": "#pragma once\nvoid esp_rom_install_channel_putc(int, void (*)(char));\n",
    "esp_timer.h": "#pragma once\n#include <cstdint>\nstd::int64_t esp_timer_get_time();\n",
    "hal/usb_serial_jtag_ll.h": """#pragma once
#include <cstdint>
int usb_serial_jtag_ll_txfifo_writable();
std::uint32_t usb_serial_jtag_ll_write_txfifo(const std::uint8_t*, std::uint32_t);
void usb_serial_jtag_ll_txfifo_flush();
""",
}
HARNESS = r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
static void check(bool value) { if(!value)std::abort(); }
bool ot_console_healthy() noexcept;
bool ot_console_install() noexcept;
std::string startup, fifo, flight, held, delivered;
std::vector<int> hooks;
void (*installed[2])(char) = {nullptr,nullptr};
std::int64_t clock_value=0;
bool busy=false, stall_after_write=false, overrun_write=false, hook_during_install=false;
bool install_during_startup=false;
unsigned accepted_bytes=0, flush_calls=0, writable_calls=0, real_calls=0;
void esp_rom_install_channel_putc(int channel,void(*callback)(char)) {
 check(!ot_console_healthy()); check(channel==1 || channel==2);
 hooks.push_back(channel); installed[channel-1]=callback;
 if(hook_during_install && channel==2)callback('x');
}
std::int64_t esp_timer_get_time() { return ++clock_value; }
int usb_serial_jtag_ll_txfifo_writable() {
 ++writable_calls;
 if(busy && !(stall_after_write && accepted_bytes)) {
   held+=flight;
   if(flight.size()<64) {delivered+=held;held.clear();}
   flight.clear();busy=false;
 }
 return busy?0:1;
}
void usb_serial_jtag_ll_txfifo_flush() {
 ++flush_calls; check(!busy); flight=fifo;fifo.clear();busy=true;
}
std::uint32_t usb_serial_jtag_ll_write_txfifo(const std::uint8_t* data,std::uint32_t size) {
 check(size==1 && !busy);fifo.push_back(static_cast<char>(*data));++accepted_bytes;
 if(fifo.size()==64) {flight=fifo;fifo.clear();busy=true;}
 if(overrun_write)clock_value+=30000;
 return 1;
}
extern "C" int __real_esp_rom_output_tx_one_char(std::uint8_t byte) {
 ++real_calls;startup.push_back(char(byte));
 if(install_during_startup) {
   install_during_startup=false;check(!ot_console_install());check(hooks.empty());
 }
 return 7;
}
extern "C" int __real_uart_tx_one_char(std::uint8_t byte) {++real_calls;startup.push_back(char(byte));return 8;}
extern "C" void __real_ets_write_char_uart(char byte) {++real_calls;startup.push_back(byte);}
'''
MAIN = r'''
int main(int argc,char** argv) {
 check(argc==2);std::string scenario=argv[1];_reent r;
 if(scenario=="startup") {
   check(_write_r(&r,1,"A\n",2)==2 && startup=="A\r\n");
   check(__wrap_esp_rom_output_tx_one_char('b')==7);
   check(__wrap_uart_tx_one_char('c')==8);__wrap_ets_write_char_uart('d');
   check(startup=="A\r\nbcd" && hooks.empty() && !ot_console_healthy() && writable_calls==0);
 } else if(scenario=="startup_install_race") {
   install_during_startup=true;check(_write_r(&r,1,"A",1)==1);
   check(startup=="A" && hooks.empty() && !ot_console_healthy());
   check(ot_console_install() && ot_console_healthy() && (hooks==std::vector<int>{1,2}));
 } else if(scenario=="install_race") {
   hook_during_install=true;check(!ot_console_install());check(!ot_console_healthy());
   check(!ot_console_install());check(_write_r(&r,1,"A",1)==-1 && r.error==EIO && accepted_bytes==0);
 } else {
   check(ot_console_install());check((hooks==std::vector<int>{1,2}) && ot_console_healthy());
   check(!ot_console_install());
   if(scenario=="success") {
     check(_write_r(&r,1,"A\n",2)==2);check(_write_r(&r,2,"B",1)==1);
     check(delivered=="A\r\nB" && accepted_bytes==4 && real_calls==0 && ot_console_healthy());
   } else if(scenario=="full_packet") {
     std::string input(64,'x');check(_write_r(&r,1,input.data(),input.size())==64);
     check(delivered==input && held.empty() && !busy && ot_console_healthy());
   } else if(scenario=="flush_failure" || scenario=="partial_deadline") {
     stall_after_write=scenario=="flush_failure";overrun_write=scenario=="partial_deadline";
     check(_write_r(&r,1,"A",1)==-1 && accepted_bytes==1 && r.error==EIO);
     check(!ot_console_healthy());auto calls=writable_calls;
     check(_write_r(&r,1,"A",1)==-1 && writable_calls==calls && accepted_bytes==1);
   } else if(scenario=="invalid_fd") {
     check(_write_r(&r,9,"A",1)==-1 && r.error==EBADF && ot_console_healthy());
     check(accepted_bytes==0 && writable_calls==0);
   } else if(scenario=="zero") {
     check(_write_r(&r,1,nullptr,0)==0 && accepted_bytes==0 && flush_calls==0 && ot_console_healthy());
   } else if(scenario=="bypass_output" || scenario=="bypass_uart" || scenario=="bypass_ets"
             || scenario=="hook1" || scenario=="hook2") {
     if(scenario=="bypass_output")check(__wrap_esp_rom_output_tx_one_char('x')==1);
     if(scenario=="bypass_uart")check(__wrap_uart_tx_one_char('x')==1);
     if(scenario=="bypass_ets")__wrap_ets_write_char_uart('x');
     if(scenario=="hook1")installed[0]('x');
     if(scenario=="hook2")installed[1]('x');
     check(!ot_console_healthy() && real_calls==0 && accepted_bytes==0);
     check(_write_r(&r,1,"A",1)==-1 && r.error==EIO && writable_calls==0);
   } else return 2;
 }
 std::cout<<scenario<<" PASS\n";
}
'''


class UsbConsoleBindingTests(unittest.TestCase):
    def test_actual_binding_scenarios(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ is required")
        before = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="usb-console-binding-", dir=ROOT / "build") as directory:
            work = Path(directory)
            for name, content in STUBS.items():
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            source, exe = work / "actual.cpp", work / "actual.exe"
            source.write_text(HARNESS + '\n#include "' + SOURCE.as_posix() + '"\n' + MAIN, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DNDEBUG",
                                    "-I", str(work), "-I", str(ROOT / "firmware/components/diagnostics/include"),
                                    str(source), "-o", str(exe)], capture_output=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr.decode(errors="replace"))
            for scenario in ("startup", "startup_install_race", "install_race", "success", "full_packet", "flush_failure", "partial_deadline",
                             "invalid_fd", "zero", "bypass_output", "bypass_uart", "bypass_ets", "hook1", "hook2"):
                with self.subTest(scenario=scenario):
                    ran = subprocess.run([str(exe), scenario], capture_output=True, timeout=5)
                    self.assertEqual(ran.returncode, 0, ran.stderr.decode(errors="replace"))
                    self.assertEqual(ran.stdout.decode().strip(), scenario + " PASS")
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), before)


if __name__ == "__main__":
    unittest.main()
