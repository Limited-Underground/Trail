"""Compile actual generated handlers with injected console/radio boundaries."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_console_firmware_source as source
FROZEN = ROOT / "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp"


class Tests(unittest.TestCase):
    def test_exact_input_and_anchors_only(self):
        raw = FROZEN.read_bytes()
        self.assertEqual(source.generate(raw), source.generate(raw))
        self.assertEqual(FROZEN.read_bytes(), raw)
        for bad in (b"", raw + b"\n", "text"):
            with self.assertRaises(source.SourceError): source.generate(bad)
        generated = source.contained.generate(raw)
        for bad in (generated.replace(b"void handle_send(", b"void changed_send("),
                    generated + b"\nvoid handle_send(char* const* tokens) {\n}\n"):
            with patch.object(source.contained, "generate", return_value=bad):
                with self.assertRaises(source.SourceError): source.generate(raw)

    def test_install_and_lock_boundaries(self):
        text = source.generate(FROZEN.read_bytes()).decode()
        main = source.function(text, 'extern "C" void app_main() {')
        self.assertLess(main.index("ot_console_install()"), main.index("ESP_ERROR_CHECK"))
        self.assertIn("if (!console_guard() || !g_radio_ready || !g_packet_received)", main)
        cli = source.function(text, "void cli_task(void*) {")
        self.assertLess(cli.index("if (!console_guard())"), cli.index("handle_prepare(tokens.data())"))
        self.assertIn("if (!console_guard()) { xSemaphoreGive(g_radio_mutex); continue; }", cli)

    def test_cli_refuses_frozen_overwrite(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/noise_xk_console_firmware_source.py"),
                                 "--source", str(FROZEN), "--output", str(FROZEN)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"output_overwrites_source", result.stderr)

    def test_actual_generated_handlers(self):
        text = source.generate(FROZEN.read_bytes()).decode()
        functions = "\n".join(source.function(text, sig) for sig in (
            "bool console_guard() {", "int16_t arm_receive() {",
            "void rx_start_receipt(Message message, int64_t start_us, uint32_t policy_ms) {",
            "void start_expected_rx(Message message, int64_t start_us, uint32_t policy_ms) {",
            "void handle_prepare(char* const* tokens) {", "void handle_arm_tx(char* const* tokens) {",
            "void handle_send(char* const* tokens) {"))
        harness = r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
enum class Role {initiator,responder}; enum class Scenario {baseline,retry_withheld}; enum class Message {m1,m2,m3};
constexpr size_t kDigestBytes=8,kShortDigestChars=8,OT_NOISE_XK_MESSAGE_3_BYTES=64;
constexpr int kPermitLifetimeMs=30000,kMessage2DeadlineMs=1000,kMessage3DeadlineMs=1000;
constexpr int RADIOLIB_ERR_CHIP_NOT_FOUND=-2,RADIOLIB_ERR_SPI_CMD_TIMEOUT=-705;
struct Permit {bool armed=false;Message message{};uint32_t deadline_ms=0;};
struct Noise {int stage=0;};
struct Attempt {bool active=false;Role role{};Scenario scenario{};std::array<uint8_t,8> session_digest{},attempt_digest{};
 Permit permit{};Noise noise{};bool rx_deadline_active=false;Message rx_deadline_message{};
 int64_t rx_deadline_start_us=0,rx_deadline_us=0;uint32_t rx_deadline_policy_ms=0;} g_attempt;
struct {bool valid=false;std::array<uint8_t,8> session_digest{};} g_ledger;
std::array<uint8_t,8> g_last_attempt_digest{};bool g_last_attempt_valid=false;
bool healthy=true,g_radio_ready=true,g_packet_received=false,locked=true;
int g_last_radio_error=0,g_tx_attempted=0,g_tx_sent=0,g_tx_failed=0,tx=0,rx=0,wipes=0,crypto=0;
std::string fault;std::vector<std::string> logs;
const char* kTag="test";const char* kReceipt="OT153";
bool ot_console_healthy(){CHECK(locked);return healthy;}
void wipe_attempt(){CHECK(locked);++wipes;g_attempt={};}
void sodium_memzero(void* p,size_t n){std::memset(p,0,n);}
struct {uint32_t millis(){return 1;}} g_hal;
int64_t esp_timer_get_time(){return 100;}
struct {int16_t startReceive(){CHECK(locked);++rx;return 0;}int16_t transmit(uint8_t*,size_t){CHECK(locked);++tx;return 0;}}g_radio;
const char* role_token(Role r){return r==Role::initiator?"I":"R";}
const char* scenario_token(Scenario){return "baseline";}
const char* message_token(Message m){return m==Message::m1?"m1":m==Message::m2?"m2":"m3";}
void short_digest_hex(const std::array<uint8_t,8>&,char* out){std::strcpy(out,"digest");}
bool parse_hex_identity(const char*,uint64_t& value){value=1;return true;}
bool parse_role(const char* s,Role& r){r=*s=='R'?Role::responder:Role::initiator;return true;}
bool parse_scenario(const char*,Scenario& s){s=Scenario::baseline;return true;}
bool parse_message(const char*,Message& m){m=Message::m1;return true;}
void digest_identity(uint64_t,std::array<uint8_t,8>& d){d.fill(1);}
bool digest_equal(const std::array<uint8_t,8>& a,const std::array<uint8_t,8>& b){return a==b;}
bool scenario_available(Role,Scenario,const char*&){return true;}
void consume_scenario(Role,Scenario){}
bool initialize_attempt_noise(Role,uint64_t,uint64_t){++crypto;return true;}
bool command_identity(const char*,const char*,const char*&){return g_attempt.active;}
bool expected_write(Message){return true;}
bool permit_live(){return g_attempt.permit.armed;}
size_t message_bytes(Message){return 64;}
int ot_noise_xk_write_message(Noise*,uint8_t*,size_t,size_t* n){++crypto;*n=64;return 0;}
void payload_digest_hex(const uint8_t*,size_t,char* out){std::strcpy(out,"payload");}
template<class... A> void log(const char*,const char* fmt,A... args){
 char b[1500];std::snprintf(b,sizeof b,fmt,args...);logs.emplace_back(b);
 if(!fault.empty() && logs.back().find(fault)!=std::string::npos)healthy=false;
}
#define ESP_LOGI log
void reject(const char*,const char*,bool=false){}
'''
        cases = r'''
void reset(const char* fail="") {healthy=true;g_radio_ready=true;g_attempt={};g_ledger={};g_last_attempt_valid=false;
 tx=rx=wipes=crypto=g_tx_attempted=g_tx_sent=g_tx_failed=0;logs.clear();fault=fail;}
char t0[]="prepare",t1[]="one",t2[]="two",t3[]="R",t4[]="baseline";
char* tokens[]={t0,t1,t2,t3,t4};
bool saw(const char* marker){for(auto& s:logs)if(s.find(marker)!=std::string::npos)return true;return false;}
void queued(){auto n=logs.size();int c=crypto;handle_prepare(tokens);handle_arm_tx(tokens);handle_send(tokens);
 CHECK(logs.size()==n&&crypto==c&&tx==0&&rx==0&&!g_attempt.active&&!g_attempt.permit.armed&&!g_radio_ready);}
int main(){
 reset();handle_prepare(tokens);CHECK(g_attempt.active&&saw("PREPARED")&&saw("RX_START")&&tx==0);
 reset("PREPARED");handle_prepare(tokens);CHECK(!saw("RX_START"));queued();
 reset("RX_START");handle_prepare(tokens);CHECK(saw("RX_START"));queued();
 reset();handle_prepare(tokens);fault="TX_ARM";handle_arm_tx(tokens);CHECK(saw("TX_ARM"));queued();
 reset();handle_prepare(tokens);handle_arm_tx(tokens);fault="TX_START";handle_send(tokens);
 CHECK(saw("TX_START")&&!saw("TX_RETURN")&&tx==0);queued();
 reset();handle_prepare(tokens);handle_arm_tx(tokens);fault="TX_RETURN";handle_send(tokens);
 CHECK(tx==1&&rx==0&&!g_radio_ready&&!g_attempt.active&&!saw("RX_REARM_RETURN"));
 reset();handle_prepare(tokens);handle_arm_tx(tokens);fault="RX_REARM_RETURN";handle_send(tokens);
 CHECK(tx==1&&rx==1&&!g_radio_ready&&!saw("TX_DONE"));
 reset();handle_prepare(tokens);handle_arm_tx(tokens);handle_send(tokens);
 CHECK(tx==1&&rx==1&&g_radio_ready&&saw("TX_DONE"));
 reset();healthy=false;CHECK(arm_receive()==RADIOLIB_ERR_CHIP_NOT_FOUND&&rx==0&&!g_radio_ready);
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ required")
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/"probe.cpp";exe=Path(temp)/"probe.exe"
            cpp.write_bytes((harness+functions+cases).encode())
            built=subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-DNDEBUG",str(cpp),"-o",str(exe)],capture_output=True)
            self.assertEqual(built.returncode,0,built.stderr.decode(errors="replace"))
            ran=subprocess.run([str(exe)],capture_output=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stderr.decode(errors="replace"))


if __name__ == "__main__": unittest.main()
