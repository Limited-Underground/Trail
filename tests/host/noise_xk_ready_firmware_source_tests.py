"""Generator integrity and compiled behavior of the actual generated handler."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_ready_firmware_source as source

FROZEN = ROOT / "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp"


class SourceTests(unittest.TestCase):
    def test_exact_source_and_deterministic_overlay(self):
        original = FROZEN.read_bytes()
        first = source.generate(original)
        self.assertEqual(first, source.generate(original))
        self.assertEqual(FROZEN.read_bytes(), original)
        self.assertEqual(hashlib.sha256(original).hexdigest(), source.FROZEN_SHA256)
        self.assertNotIn(b"\r", first)
        text = first.decode()
        # Reverse the overlay: no unrelated radio/protocol code may have changed.
        for before, after in reversed(source.REPLACEMENTS):
            self.assertEqual(text.count(after), 1)
            text = text.replace(after, before, 1)
        self.assertEqual(text, original.decode().replace("\r\n", "\n"))

    def test_tampered_frozen_input_rejected(self):
        original = FROZEN.read_bytes()
        for raw in (original + b"\n", original[:-1], b"", "not bytes"):
            with self.assertRaises(source.SourceError):
                source.generate(raw)

    def test_missing_and_repeated_anchor_rejected(self):
        for value in ("missing", "anchor anchor"):
            with self.assertRaises(source.SourceError):
                source.replace_once(value, "anchor", "replacement")

    def test_cli_cannot_overwrite_input(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/noise_xk_ready_firmware_source.py"),
                                 "--source", str(FROZEN), "--output", str(FROZEN)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"output_overwrites_source", result.stderr)

    def test_actual_handler_state_and_receipt_order(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.fail("g++ is required for the firmware handler behavioral gate")
        generated = source.generate(FROZEN.read_bytes()).decode()
        handler = generated[generated.index("void handle_ready("):generated.index("void cli_task(")]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
struct Attempt { bool active=false; struct {bool armed=false;} permit;
    bool rx_deadline_active=false; struct {int stage=0;} noise; } g_attempt;
struct {bool valid=false;} g_ledger;
bool g_ready_boot_selftest=true, g_radio_ready=true;
bool g_last_attempt_valid=false, g_packet_received=false;
int g_last_radio_error=0;
uint32_t g_tx_attempted=0,g_tx_sent=0,g_tx_failed=0,g_rx_accepted=0,g_rx_rejected=0;
uint32_t g_lost=0,g_duplicates=0,g_corrupt=0,g_unexpected=0,g_forced_timeouts=0;
const char* kTag="test"; const char* kReceipt="OT153";
std::vector<std::string> records;
template<class... Args> void log(const char*,const char* format,Args... args) {
    char output[512]; std::snprintf(output,sizeof(output),format,args...); records.emplace_back(output);
}
#define ESP_LOGI log
void reject(const char*,const char* reason) { records.emplace_back(std::string("reject:")+reason); }
void profile_receipt() { records.emplace_back("PROFILE"); }
void status_receipt() { records.emplace_back("STATUS"); }
'''
        assertions = r'''
int main() {
    const char* challenge="0123456789abcdef0123456789abcdef";
    auto accept=[&]() {
        records.clear(); handle_ready(challenge);
        assert(records.size()==3);
        assert(records[0]==std::string("OT153 READY schema=OTNXREADY1 challenge=")+challenge+
            " accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no");
        assert(records[1]=="PROFILE" && records[2]=="STATUS");
    };
    accept(); accept(); // Readiness is repeatable without consuming state.
    for (const char* bad : {"", "0123456789abcdef", "0123456789abcdef0123456789abcdeF",
                           "0123456789abcdef0123456789abcdef0", "0123456789abcdef0123456789abcdeg"}) {
        records.clear(); handle_ready(bad); assert(records.size()==1 && records[0]=="reject:syntax");
    }
    auto denied=[&]() { records.clear(); handle_ready(challenge);
        assert(records.size()==1 && records[0]=="reject:not_idle"); };
    for (bool* flag : {&g_attempt.active,&g_attempt.permit.armed,&g_attempt.rx_deadline_active,
                      &g_ledger.valid,&g_last_attempt_valid,&g_packet_received}) {
        *flag=true; denied(); assert(*flag); *flag=false;
    }
    for (uint32_t* counter : {&g_tx_attempted,&g_tx_sent,&g_tx_failed,&g_rx_accepted,&g_rx_rejected,
                             &g_lost,&g_duplicates,&g_corrupt,&g_unexpected,&g_forced_timeouts}) {
        *counter=1; denied(); assert(*counter==1); *counter=0;
    }
    g_attempt.noise.stage=1; denied(); assert(g_attempt.noise.stage==1); g_attempt.noise.stage=0;
    g_last_radio_error=-1; denied(); assert(g_last_radio_error==-1); g_last_radio_error=0;
    g_ready_boot_selftest=false; denied(); assert(!g_ready_boot_selftest); g_ready_boot_selftest=true;
    g_radio_ready=false; denied(); assert(!g_radio_ready); g_radio_ready=true;
    accept();
}
'''
        with tempfile.TemporaryDirectory(prefix="ready-handler-") as directory:
            cpp = Path(directory) / "handler.cpp"
            binary = Path(directory) / "handler.exe"
            cpp.write_text(harness + handler + assertions, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    str(cpp), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_generated_command_uses_existing_lock_and_actual_startup_result(self):
        text = source.generate(FROZEN.read_bytes()).decode()
        cli = text[text.index("void cli_task("):text.index("void handle_received_payload(")]
        self.assertLess(cli.index("xSemaphoreTake(g_radio_mutex"), cli.index("handle_ready(tokens[1])"))
        self.assertIn('count == 2U && std::strcmp(tokens[0], "ready") == 0', cli)
        main = text[text.index('extern "C" void app_main()'):]
        self.assertLess(main.index("g_ready_boot_selftest = stale_selftest_passed"), main.index("xTaskCreate(cli_task"))


if __name__ == "__main__":
    unittest.main()
