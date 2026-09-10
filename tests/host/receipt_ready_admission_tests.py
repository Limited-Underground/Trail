"""Compile the actual generated READY handler; inject only its external seams."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_receipt_firmware_source as source

HARNESS = r'''
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
struct Attempt { bool active=false; struct {bool armed=false;} permit;
 bool rx_deadline_active=false; struct {unsigned stage=0;} noise; } g_attempt;
struct { bool valid=false; } g_ledger;
bool g_last_attempt_valid=false,g_packet_received=false;
bool g_ready_boot_selftest=true,g_radio_ready=true;
int g_last_radio_error=0;
unsigned g_tx_attempted=0,g_tx_sent=0,g_tx_failed=0,g_rx_accepted=0,g_rx_rejected=0,
 g_lost=0,g_duplicates=0,g_corrupt=0,g_unexpected=0,g_forced_timeouts=0;
bool begin_ok=true,enabled=false;
unsigned begin_calls=0,profiles=0,statuses=0;
std::string rejected;
std::vector<std::string> events;
constexpr const char* kTag="ot153_noise_radio";
constexpr const char* kReceipt="OTNXK0";
void reject(const char* command,const char* reason) { CHECK(std::string(command)=="ready");rejected=reason; }
bool ot_console_begin_session() { ++begin_calls;events.push_back("begin");if(begin_ok)enabled=true;return begin_ok; }
void receipt(const char* tag,const char* format,const char* token,const char* challenge) {
 CHECK(enabled && begin_calls>0 && std::string(tag)==kTag && std::string(token)==kReceipt);
 CHECK(std::string(format)=="%s READY schema=OTNXREADY1 challenge=%s accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no");
 CHECK(std::string(challenge)=="0123456789abcdef0123456789abcdef");events.push_back("READY");
}
#define OT_RECEIPT_I receipt
void profile_receipt(){CHECK(enabled);++profiles;events.push_back("profile");}
void status_receipt(){CHECK(enabled);++statuses;events.push_back("status");}
const char* valid="0123456789abcdef0123456789abcdef";
void reset(){g_attempt={};g_ledger.valid=false;g_last_attempt_valid=g_packet_received=false;
 g_ready_boot_selftest=g_radio_ready=true;g_last_radio_error=0;
 g_tx_attempted=g_tx_sent=g_tx_failed=g_rx_accepted=g_rx_rejected=g_lost=g_duplicates=g_corrupt=g_unexpected=g_forced_timeouts=0;
 begin_ok=true;enabled=false;begin_calls=profiles=statuses=0;rejected.clear();events.clear();}
void blocked(const char* reason){CHECK(rejected==reason && begin_calls==0 && !enabled && events.empty() && profiles==0 && statuses==0);}
'''
CASES = r'''
int main(){
 unsigned cases=0;
 for(const char* bad : {"", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0",
                         "G123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdeF"}) {
  reset();handle_ready(bad);blocked("syntax");++cases;
 }
 bool* flags[]={&g_ready_boot_selftest,&g_radio_ready};
 for(auto flag:flags){reset();*flag=false;handle_ready(valid);blocked("not_idle");CHECK(!*flag);++cases;}
 reset();g_last_radio_error=-1;handle_ready(valid);blocked("not_idle");CHECK(g_last_radio_error==-1);++cases;
 bool* idle[]={&g_attempt.active,&g_attempt.permit.armed,&g_attempt.rx_deadline_active,
               &g_ledger.valid,&g_last_attempt_valid,&g_packet_received};
 for(auto flag:idle){reset();*flag=true;handle_ready(valid);blocked("not_idle");CHECK(*flag);++cases;}
 reset();g_attempt.noise.stage=3;handle_ready(valid);blocked("not_idle");CHECK(g_attempt.noise.stage==3);++cases;
 unsigned* counters[]={&g_tx_attempted,&g_tx_sent,&g_tx_failed,&g_rx_accepted,&g_rx_rejected,
 &g_lost,&g_duplicates,&g_corrupt,&g_unexpected,&g_forced_timeouts};
 for(auto counter:counters){reset();*counter=9;handle_ready(valid);blocked("not_idle");CHECK(*counter==9);++cases;}
 reset();begin_ok=false;handle_ready(valid);CHECK(begin_calls==1&&!enabled&&profiles==0&&statuses==0&&rejected.empty());
 CHECK(events==std::vector<std::string>{"begin"});++cases;
 reset();handle_ready(valid);CHECK(begin_calls==1&&enabled&&profiles==1&&statuses==1&&rejected.empty());
 CHECK((events==std::vector<std::string>{"begin","READY","profile","status"}));++cases;
 handle_ready(valid);CHECK(begin_calls==2&&enabled&&profiles==2&&statuses==2&&rejected.empty());
 CHECK((events==std::vector<std::string>{"begin","READY","profile","status","begin","READY","profile","status"}));++cases;
 std::printf("%u cases passed\n",cases);
}
'''

class Tests(unittest.TestCase):
    def test_actual_ready_handler(self):
        frozen = ROOT / "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp"
        generated = source.generate(frozen.read_bytes()).decode("utf-8")
        function = source.console.function(generated, "void handle_ready(const char* challenge) {")
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ required")
        with tempfile.TemporaryDirectory() as temp:
            cpp, exe = Path(temp)/"ready.cpp", Path(temp)/"ready.exe"
            cpp.write_text(HARNESS + function + CASES, encoding="utf-8")
            built = subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-DNDEBUG",
                                    str(cpp),"-o",str(exe)],capture_output=True,timeout=30)
            self.assertEqual(built.returncode,0,built.stderr.decode(errors="replace"))
            ran = subprocess.run([str(exe)],capture_output=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stderr.decode(errors="replace"))
            self.assertEqual(ran.stdout.decode().splitlines(),["28 cases passed"])

if __name__ == "__main__": unittest.main()
