"""Compile the whole receipt binding; model startup/ROM/USB seams only.

Each scenario gets a fresh process. Early-init invocation here verifies its
dependencies, not actual ESP-IDF constructor order; that remains an ELF gate.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

import usb_console_binding_tests as previous

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/targets/heltec_v4_noise_xk_receipts/main/opentrail_console.cpp"
STUBS = dict(previous.STUBS)
STUBS["esp_rom_sys.h"] += "void esp_rom_delay_us(unsigned);\n"
STUBS["esp_private/startup_internal.h"] = """#pragma once
#define BIT(n) (1U << (n))
#define ESP_OK 0
#define ESP_FAIL -1
using esp_err_t = int;
#define ESP_SYSTEM_INIT_FN(f, stage, cores, priority, ...) \\
 static_assert((cores)==BIT(0) && (priority)==0, "early core admission"); \\
 static esp_err_t __esp_system_init_fn_##f()
"""
HARNESS = previous.HARNESS.replace(
    "std::string startup, fifo, flight, held, delivered;",
    "void ot_receipt_log(char, const char*, const char*, ...) noexcept;\n"
    "bool nested_receipt=false, fault_during_write=false;\n"
    "std::string startup, fifo, flight, held, delivered;",
).replace(
    "check(size==1 && !busy);fifo.push_back",
    "check(size==1 && !busy);\n"
    " if(nested_receipt) { nested_receipt=false; ot_receipt_log('I',\"ot153_noise_radio\",\"NESTED\"); }\n"
    " if(fault_during_write) { fault_during_write=false; installed[0]('x'); }\n"
    " fifo.push_back",
).replace(
    "std::int64_t clock_value=0;",
    "std::int64_t clock_value=0, poll_delay_us=0, poll_ready_at=0;\n"
    "unsigned pause_calls=0;\n"
    "void esp_rom_delay_us(unsigned delay) { check(delay>0 && delay<=5); clock_value+=delay; ++pause_calls; }",
).replace(
    "if(busy && !(stall_after_write && accepted_bytes))",
    "if(busy && clock_value>=poll_ready_at && !(stall_after_write && accepted_bytes))",
).replace("busy=true;", "busy=true;poll_ready_at=clock_value+poll_delay_us;")
MAIN = r'''
static void early() {
 check(__esp_system_init_fn_ot_receipt_quarantine()==ESP_OK);
 check((hooks==std::vector<int>{1,2}));
 check(clock_value==0 && writable_calls==0 && accepted_bytes==0 && real_calls==0);
 check(!ot_console_healthy());
}
static void ready_receipt() {
 ot_receipt_log('I',"ot153_noise_radio","OTNXK0 READY accepted=%s count=%u", "yes",3U);
}
int main(int argc,char** argv) {
 check(argc==2);const std::string scenario=argv[1];_reent r;
 if(scenario=="boot") {
   check(_write_r(&r,1,"A\n",2)==2 && startup.empty() && real_calls==0);
   check(__wrap_esp_rom_output_tx_one_char('b')==7);
   check(__wrap_uart_tx_one_char('c')==8);__wrap_ets_write_char_uart('d');
   check(startup=="bcd" && !ot_console_healthy() && writable_calls==0);
 } else if(scenario=="premature_install") {
   check(!ot_console_install() && !ot_console_healthy() && hooks.empty());
 } else if(scenario=="hook_during_early") {
   hook_during_install=true;(void)__esp_system_init_fn_ot_receipt_quarantine();
   check(!ot_console_install() && !ot_console_healthy());
   check(writable_calls==0 && real_calls==0 && clock_value==0);
 } else {
   early();
   if(scenario=="quarantine_stdio") {
     check(_write_r(&r,1,"A",1)==-1 && r.error==EIO);
     check(!ot_console_install() && accepted_bytes==0 && real_calls==0);
   } else if(scenario=="quarantine_hook") {
     installed[1]('x');check(!ot_console_install() && !ot_console_healthy());
   } else if(scenario=="quarantine_wrapper") {
     check(__wrap_uart_tx_one_char('x')==1);
     check(!ot_console_install() && real_calls==0);
   } else if(scenario=="quarantine_receipt") {
     ready_receipt();check(!ot_console_install() && accepted_bytes==0);
   } else {
     check(ot_console_install() && ot_console_healthy());
     check(!ot_console_session_started());
     if(scenario!="disabled_wait" && scenario!="disabled_bypass") {
       check(ot_console_begin_session() && ot_console_session_started());
     }
     if(scenario=="disabled_wait") {
       stall_after_write=true;busy=true;
       const std::string body(512,'x');
       ot_receipt_log('I',"ot153_noise_radio",body.c_str());
       check(ot_console_healthy() && !ot_console_session_started());
       check(clock_value==0 && writable_calls==0 && flush_calls==0 && accepted_bytes==0);
       check(ot_console_begin_session() && ot_console_session_started());
       busy=false;stall_after_write=false;ready_receipt();
       check(ot_console_healthy() && !delivered.empty());
     } else if(scenario=="disabled_bypass") {
       check(__wrap_uart_tx_one_char('x')==1);
       check(!ot_console_begin_session() && !ot_console_session_started());
       check(!ot_console_healthy() && real_calls==0 && clock_value==0 && writable_calls==0);
     } else if(scenario=="repeat_begin") {
       ready_receipt();const auto previous=delivered;
       check(ot_console_begin_session() && ot_console_session_started());
       ready_receipt();check(delivered==previous+previous && ot_console_healthy());
     } else if(scenario=="begin_after_fault") {
       installed[0]('x');check(!ot_console_begin_session() && !ot_console_session_started());
       ready_receipt();check(!ot_console_healthy() && accepted_bytes==0 && writable_calls==0);
     } else if(scenario=="repeat_install") {
       check(!ot_console_install());check(hooks.size()==2 && real_calls==0);
     } else if(scenario=="absolute_deadline_gap" || scenario=="admission_clock_regression") {
       // Isolate the gap after receipt formatting: a newly started relative
       // writer budget must never renew the receipt's original deadline.
       receipt_deadline=scenario=="absolute_deadline_gap"?100:1000;
       receipt_last_admission=scenario=="absolute_deadline_gap"?95:100;
       clock_value=scenario=="absolute_deadline_gap"?100:50;
       const std::uint8_t byte='A';const auto result=writer.write(&byte,1,100);
       check(result.status!=opentrail::diagnostics::ConsoleWriteStatus::accepted);
       check(!ot_console_healthy() && writable_calls==0 && flush_calls==0 && accepted_bytes==0);
     } else if(scenario=="ready") {
       ready_receipt();
       check(delivered=="I (0) ot153_noise_radio: OTNXK0 READY accepted=yes count=3\r\n");
       check(ot_console_healthy() && real_calls==0 && !busy && held.empty());
     } else if(scenario=="large_format") {
       const std::string body(769,'x');ot_receipt_log('I',"ot153_noise_radio",body.c_str());
       check(!ot_console_healthy() && accepted_bytes==0 && flush_calls==0);
     } else if(scenario=="scheduled_poll" || scenario=="slow_poll") {
       // A scheduled host model, not a measured USB timing or RF assertion.
       poll_delay_us=scenario=="scheduled_poll"?1000:10000;
       const std::string body(512,'x');const auto started=clock_value;
       ot_receipt_log('I',"ot153_noise_radio",body.c_str());
       const auto elapsed=clock_value-started;
       if(scenario=="scheduled_poll") {
         check(ot_console_healthy());
         check(delivered=="I (0) ot153_noise_radio: "+body+"\r\n");
         check(accepted_bytes==delivered.size() && held.empty() && !busy);
         check(elapsed<20000 && pause_calls>0 && writable_calls<4096);
       } else {
         check(!ot_console_healthy() && elapsed>=20000 && elapsed<=20010);
         const auto calls=writable_calls,bytes=accepted_bytes;ready_receipt();
         check(writable_calls==calls && accepted_bytes==bytes);
       }
     } else if(scenario=="long_argument") {
       const std::string value(257,'x');ot_receipt_log('I',"ot153_noise_radio","%s",value.c_str());
       check(!ot_console_healthy() && accepted_bytes==0 && flush_calls==0);
     } else if(scenario=="unsupported_format") {
       ot_receipt_log('I',"ot153_noise_radio","%x",3U);
       check(!ot_console_healthy() && accepted_bytes==0 && flush_calls==0);
     } else if(scenario=="flush_failure" || scenario=="partial_deadline") {
       stall_after_write=scenario=="flush_failure";overrun_write=scenario=="partial_deadline";
       ready_receipt();check(!ot_console_healthy() && accepted_bytes>0);
       const auto bytes=accepted_bytes,calls=writable_calls;ready_receipt();
       check(accepted_bytes==bytes && writable_calls==calls);
     } else if(scenario=="concurrent_receipt" || scenario=="hook_during_write") {
       nested_receipt=scenario=="concurrent_receipt";fault_during_write=scenario=="hook_during_write";
       ready_receipt();check(!ot_console_healthy() && accepted_bytes==1);
       const auto calls=writable_calls;ready_receipt();
       check(accepted_bytes==1 && writable_calls==calls && real_calls==0);
     } else if(scenario=="owned_stdio") {
       check(_write_r(&r,1,"A",1)==-1 && r.error==EIO);
       check(!ot_console_healthy() && accepted_bytes==0);
     } else if(scenario=="owned_hook") {
       installed[0]('x');ready_receipt();check(!ot_console_healthy() && accepted_bytes==0);
     } else return 2;
   }
 }
 std::cout<<scenario<<" PASS\n";
}
'''


class ReceiptConsoleBindingTests(unittest.TestCase):
    def test_whole_binding_lifecycle(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ is required")
        before = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="receipt-binding-", dir=ROOT / "build") as directory:
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
            for scenario in ("boot", "premature_install", "hook_during_early", "quarantine_stdio",
                             "quarantine_hook", "quarantine_wrapper", "quarantine_receipt", "repeat_install",
                             "ready", "large_format", "long_argument", "unsupported_format", "flush_failure",
                             "partial_deadline", "concurrent_receipt", "hook_during_write", "owned_stdio", "owned_hook",
                             "scheduled_poll", "slow_poll", "absolute_deadline_gap", "admission_clock_regression",
                             "disabled_wait", "disabled_bypass", "repeat_begin", "begin_after_fault"):
                with self.subTest(scenario=scenario):
                    ran = subprocess.run([str(exe), scenario], capture_output=True, timeout=5)
                    self.assertEqual(ran.returncode, 0, ran.stderr.decode(errors="replace"))
                    self.assertEqual(ran.stdout.decode().strip(), scenario + " PASS")
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), before)


if __name__ == "__main__":
    unittest.main()
