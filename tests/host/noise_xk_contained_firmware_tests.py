"""Actual generated send-tail behavior with controlled radio, clock and logs."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_contained_firmware_source as source

FROZEN = ROOT / "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp"


class ContainedFirmwareTests(unittest.TestCase):
    def test_only_exact_deterministic_overlay(self):
        raw = FROZEN.read_bytes()
        generated = source.generate(raw)
        self.assertEqual(generated, source.generate(raw))
        self.assertEqual(FROZEN.read_bytes(), raw)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), source.ready.FROZEN_SHA256)
        text = generated.decode()
        self.assertNotIn("\r", text)
        for before, after in reversed(source.REPLACEMENTS):
            self.assertEqual(text.count(after), 1)
            text = text.replace(after, before, 1)
        self.assertEqual(text.encode(), source.ready.generate(raw))

    def test_tampered_input_and_anchor_fail_closed(self):
        for raw in (b"", FROZEN.read_bytes() + b"\n", "not bytes"):
            with self.assertRaises(source.SourceError):
                source.generate(raw)
        for raw in ("none", "anchor anchor"):
            with self.assertRaises(source.SourceError):
                source.ready.replace_once(raw, "anchor", "replacement")

    def test_cli_refuses_input_overwrite(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/noise_xk_contained_firmware_source.py"),
                                 "--source", str(FROZEN), "--output", str(FROZEN)],
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"output_overwrites_source", result.stderr)

    def test_actual_generated_send_tail(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for compiled firmware behavior")
        generated = source.generate(FROZEN.read_bytes()).decode()
        start = generated.index("    ++g_tx_attempted;", generated.index("void handle_send("))
        tail = generated[start:generated.index("void handle_abort(", start)]
        arm = generated[generated.index("int16_t arm_receive() {"):generated.index("int16_t configure_radio() {")]
        rx_start = generated.index("    std::array<uint8_t, kMaxRadioBytes> payload{};", generated.index('extern "C" void app_main()'))
        rx_loop = "void receive_loop() {\n" + generated[rx_start:]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
constexpr int RADIOLIB_ERR_SPI_CMD_TIMEOUT=-705, RADIOLIB_ERR_CHIP_NOT_FOUND=-2;
enum class Message {m1,m2,m3};
struct {int role=0,scenario=0;} g_attempt;
const char* role_token(int) {return "I";}
const char* scenario_token(int) {return "baseline";}
const char* message_token(Message m) {return m==Message::m1?"m1":m==Message::m2?"m2":"m3";}
const char* kTag="test"; const char* kReceipt="OT153";
constexpr int kMessage2DeadlineMs=1000,kMessage3DeadlineMs=1000;
bool g_radio_ready=true,g_packet_received=true;
int g_last_radio_error=0,g_tx_attempted=0,g_tx_sent=0,g_tx_failed=0;
int tx_result=0,rx_result=0,tx_calls=0,rx_calls=0,wipes=0,zeroes=0,expected=0;
int64_t clock_us=100;
std::vector<std::string> logs,events;
int64_t esp_timer_get_time() {return clock_us+=10;}
template<class... Args> void log(const char*,const char* format,Args... args) {
 char output[1024]; std::snprintf(output,sizeof(output),format,args...);
 logs.emplace_back(output); events.emplace_back(output);
}
#define ESP_LOGI log
int g_rx_rejected=0,length_calls=0,read_calls=0,timeout_checks=0,locks=0,unlocks=0;
bool locked=false,inject_containment=false;
constexpr size_t kMaxRadioBytes=64;
constexpr int RADIOLIB_ERR_PACKET_TOO_LONG=-4,portMAX_DELAY=0;
int g_radio_mutex=1;
struct StopLoop {};
void xSemaphoreTake(int,int) {assert(!locked);locked=true;++locks;
 if(inject_containment) {g_radio_ready=false;inject_containment=false;}}
void xSemaphoreGive(int) {assert(locked);locked=false;++unlocks;}
int pdMS_TO_TICKS(int x) {return x;}
void vTaskDelay(int) {assert(!locked);throw StopLoop{};}
void check_rx_timeout() {assert(locked);++timeout_checks;}
void handle_received_payload(uint8_t*,size_t,int64_t) {assert(locked);}
#define ESP_LOGW log
struct Radio {
 int16_t transmit(uint8_t*,size_t) {++tx_calls; events.emplace_back("TX_CALL");return tx_result;}
 int16_t startReceive() {++rx_calls;events.emplace_back("RX_CALL");return rx_result;}
 size_t getPacketLength() {assert(locked);++length_calls;return 16;}
 int16_t readData(uint8_t*,size_t) {assert(locked);++read_calls;return 0;}
} g_radio;
'''
        harness += arm + r'''
void sodium_memzero(void* p,size_t n) {++zeroes;std::memset(p,0,n);}
void wipe_attempt() {++wipes;}
void start_expected_rx(Message,int64_t when,int) {assert(when==120);++expected;}
void send(Message message) {
 std::array<uint8_t,64> payload{}; size_t written=64;
 char payload_hash[16]="SECRET_PAYLOAD",session_hash[16]="SECRET_SESSION",attempt_hash[16]="SECRET_ATTEMPT";
'''
        assertions = r'''
void reset(int tx,int rx,bool ready=true) {
 tx_result=tx;rx_result=rx;g_radio_ready=ready;g_packet_received=true;clock_us=100;
 g_last_radio_error=g_tx_attempted=g_tx_sent=g_tx_failed=0;
 tx_calls=rx_calls=wipes=zeroes=expected=0;logs.clear();events.clear();
 length_calls=read_calls=timeout_checks=locks=unlocks=0;locked=false;inject_containment=false;
}
void check(int tx,int rx,bool attempted) {
 assert(logs.size()==4 && zeroes==2);
 assert(logs[0].find("OT153 TX_START ")==0);
 assert(logs[1]=="OT153 TX_RETURN schema=OTNXTXDIAG1 role=I scenario=baseline message=m1 result="+
   std::to_string(tx)+" start_us=110 done_us=120 measured_us=10");
 assert(logs[2]=="OT153 RX_REARM_RETURN schema=OTNXTXDIAG1 role=I scenario=baseline message=m1 result="+
   std::to_string(rx)+" attempted="+(attempted?"yes":"no")+" start_us=130 done_us=140 measured_us=10");
 assert(logs[1].find("SECRET")==std::string::npos && logs[2].find("SECRET")==std::string::npos);
 assert(logs[3].find("OT153 TX_DONE ")==0);
 assert(logs[3].find("start_us=110 done_us=120 measured_us=10")!=std::string::npos);
 assert(logs[3].find("rx_restart="+std::to_string(rx)+" permit_consumed=yes")!=std::string::npos);
 assert(g_last_radio_error==(attempted?rx:tx) && g_tx_attempted==1 && g_tx_sent==(tx==0) && g_tx_failed==(tx!=0));
}
int main() {
 reset(0,0);send(Message::m1);check(0,0,true);
 assert(tx_calls==1 && rx_calls==1 && wipes==0 && expected==1 && g_radio_ready);
 assert(events.size()==6 && events[1]=="TX_CALL" && events[3]=="RX_CALL");
 reset(-705,0);send(Message::m1);check(-705,-705,false);
 assert(tx_calls==1 && rx_calls==0 && wipes==1 && expected==0 && !g_radio_ready);
 // Even an attempted subsequent send cannot call the contained radio or rearm it.
 logs.clear();events.clear();send(Message::m3);
 assert(tx_calls==1 && rx_calls==0 && !g_radio_ready && wipes==2);
 reset(-5,0);send(Message::m1);check(-5,0,true);
 assert(tx_calls==1 && rx_calls==1 && wipes==1 && expected==0 && g_radio_ready);
 reset(0,-6);send(Message::m1);check(0,-6,true);
 assert(wipes==1 && expected==0);
 reset(0,-705);send(Message::m1);check(0,-705,true);
 assert(wipes==1 && expected==0 && !g_radio_ready && rx_calls==1);
 send(Message::m3);assert(tx_calls==1 && rx_calls==1 && wipes==2);
 reset(0,0,false);send(Message::m1);check(-2,-2,false);
 assert(tx_calls==0 && rx_calls==0 && wipes==1);
 // Simulate the CLI latching unavailable before the main task acquires its lock.
 reset(0,0);inject_containment=true;
 try {receive_loop();assert(false);} catch(const StopLoop&) {}
 assert(!g_radio_ready && length_calls==0 && read_calls==0 && rx_calls==0);
 assert(timeout_checks==1 && locks==1 && unlocks==1 && !locked);
 // Receive-task rearm failure also contains; the next iteration checks timeouts only.
 reset(0,-705);
 try {receive_loop();assert(false);} catch(const StopLoop&) {}
 assert(!g_radio_ready && length_calls==1 && read_calls==1 && rx_calls==1);
 assert(g_last_radio_error==-705 && timeout_checks==1 && locks==2 && unlocks==2 && !locked);
 // Ordinary idle behavior still releases the lock before delaying.
 reset(0,0);g_packet_received=false;
 try {receive_loop();assert(false);} catch(const StopLoop&) {}
 assert(timeout_checks==1 && locks==1 && unlocks==1 && length_calls==0 && !locked);
}
'''
        with tempfile.TemporaryDirectory(prefix="nxk-contained-") as directory:
            path = Path(directory)
            cpp = path / "generated.cpp"
            executable = path / "generated.exe"
            cpp.write_text(harness + tail + rx_loop + assertions, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    str(cpp), "-o", str(executable)], capture_output=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr.decode(errors="replace"))
            ran = subprocess.run([str(executable)], capture_output=True, timeout=5)
            self.assertEqual(ran.returncode, 0, ran.stderr.decode(errors="replace"))


if __name__ == "__main__":
    unittest.main()
