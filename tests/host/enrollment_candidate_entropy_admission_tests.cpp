// OT-0237b: actual candidate application/owners with SDK-shaped doubles.
// Each application case runs in a fresh process: application statics are not
// reset by a fixture reset. No physical entropy, reboot or USB result is claimed.
#include "candidate_runtime.hpp"
#include "candidate_store_runtime.hpp"
#include "confirmation_nonowning_entropy.hpp"
#include "entropy_admission_stub.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "heltec_v4_oled.hpp"
#include "esp_bt.h"
#include "host/ble_store.h"
#include "driver/usb_serial_jtag.h"
#include "startup_diagnostics.hpp"
#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL entropy admission %d: %s\n", __LINE__, #c); std::abort(); } } while (false)
extern "C" void app_main();
namespace admission {
namespace sdk = enrollment_candidate_target_stub;
namespace fault = enrollment_candidate_entropy_admission_stub;
namespace target = opentrail::target::heltec_v4_enrollment_candidate_eval;
namespace board = opentrail::target::heltec_v4_bench;
namespace reset_board = opentrail::targets::heltec_v4_bench;
using Entropy = opentrail::security::EntropyState;
std::jmp_buf returned;
std::string selected, input;
std::size_t consumed{}, fragment{256};
unsigned ticks{}, expected_outputs{};
unsigned fills_at_loss{};
bool app_active{}, usb_install_failure{}, crypto_init_failure{}, lost_before_begin{}, source_lost{};
bool driver_installed{}, diagnostic_write_drop{}, fifo_busy{}, fifo_partial{};
int reset_reason{1};
unsigned usb_reads{}, fifo_writes{}, fifo_flushes{};
std::vector<std::string> outputs;
std::vector<std::string> diagnostics, lowlevel, wire;
std::FILE* producer_fixture{};
void record_wire(const std::string& bytes) {
    wire.push_back(bytes);
    if(producer_fixture){CHECK(std::fwrite(bytes.data(),1,bytes.size(),producer_fixture)==bytes.size());CHECK(std::fflush(producer_fixture)==0);}
}
sdk::Disk original;

void reset() {
    CHECK(sdk::state.handles.empty() && sdk::state.live_iterators == 0);
    sdk::state = {}; fault::state = {}; heltec_oled_stub::reset();
    sdk::seed_originals(sdk::state);
    sdk::state.disk["nimble_bond"].clear();
    sdk::state.disk["nimble_bond"]["our_sec_1"] = {sdk::Bytes(sizeof(ble_store_value_sec), 0x5a), NVS_TYPE_BLOB};
    sdk::pad_originals(sdk::state);
    original = sdk::state.disk;
}
void originals_preserved() {
    for (const auto& space : original) CHECK(sdk::state.disk.at(space.first) == space.second);
    sdk::assert_no_destructive_cleanup(sdk::state);
    CHECK(sdk::state.live_iterators == 0);
}
void no_created_secret() {
    CHECK(sdk::state.sets == 0 && sdk::state.commits == 0);
    for (const auto& space : sdk::state.candidate) CHECK(space.second.empty());
    originals_preserved();
}
void incomplete_identity_only() {
    // The accepted identity owner commits a nonsecret provisioning intent before
    // asking for seed entropy. Failure preserves that exact intent, never a
    // completed identity, seed or generation allocation, and forbids restart.
    const auto& spaces = sdk::state.candidate;
    CHECK(spaces.size() == 1 && spaces.at(target::CandidateNvsStorage::kNamespace).size() == 1);
    const auto& record = spaces.at(target::CandidateNvsStorage::kNamespace).at("g00000000n7d1s0");
    CHECK(record.type == NVS_TYPE_BLOB && record.bytes.size() == 64);
    constexpr std::array<std::uint8_t, 8> intent{'O', 'T', 'I', 'D', 1, 0, 0, 0};
    CHECK(std::equal(intent.begin(), intent.end(), record.bytes.begin()));
    CHECK(std::all_of(record.bytes.begin() + 8, record.bytes.end(), [](auto byte) { return byte == 0xff; }));
    const auto sets = sdk::state.sets, commits = sdk::state.commits, fills = sdk::state.entropy_reads;
    target::CandidateNvsStorage reconstructed;
    CHECK(reconstructed.ready());
    board::ConfirmationNonowningEntropy unavailable;
    opentrail::security_evaluation::EnrollmentIdentityStore identity(
        reconstructed.external(target::CandidateNvsStorage::Store::identity), unavailable.random());
    CHECK(identity.load_existing() == decltype(identity)::LoadResult::fault && identity.failed());
    CHECK(sdk::state.sets == sets && sdk::state.commits == commits && sdk::state.entropy_reads == fills);
    originals_preserved();
}
void exact_outputs(const std::vector<std::string>& expected) { CHECK(outputs == expected); }
std::string marker(unsigned stage,unsigned phase,unsigned reason=1) {
    return "OTBOOT1 "+std::to_string(stage)+" "+std::to_string(phase)+" "+std::to_string(reason)+"\n";
}
void diagnostics_stop_at_first_reply() {
    const auto first=std::find_if(wire.begin(),wire.end(),[](const auto& line){return line.rfind("OTCAND1 ",0)==0;});
    CHECK(first!=wire.end());
    CHECK(std::none_of(first,wire.end(),[](const auto& line){return line.rfind("OTBOOT1 ",0)==0 || line.rfind("\nOTBOOT1 ",0)==0;}));
}
void arm_fill(unsigned call, fault::FillFault effect) {
    fault::state.fault_at_fill = call; fault::state.fill_fault = effect;
}
unsigned startup_stage(const std::string& name) {
    if (name == "usb-install") { usb_install_failure = true; return 1; }
    if (name == "layout") { sdk::state.named.address += 0x1000; return 8; }
    if (name == "default-nvs") { sdk::arm("init", "nvs"); return 2; }
    if (name == "reset-marker") { sdk::arm("open", "ot_reset_v1"); return 3; }
    if (name == "controller-init") { sdk::arm("nimble_init"); return 4; }
    if (name == "controller-unqualified") { fault::state.unqualified_after_controller_init = true; return 4; }
    if (name == "library-init") { crypto_init_failure = true; return 5; }
    if (name == "display") { heltec_oled_stub::state.fail_all_draws = true; return 6; }
    if (name == "input") { heltec_oled_stub::state.fail_input_config = true; return 7; }
    if (name == "candidate-storage") { sdk::arm("partition_init", "ot238_nvs"); return 8; }
    if (name == "startup-source-loss") { arm_fill(1, fault::FillFault::qualification_lost); return 9; }
    if (name == "startup-zero-context") { arm_fill(1, fault::FillFault::all_zero); return 9; }
    return 0;
}
void run_app_case() {
    reset(); const auto stage = startup_stage(selected);
    if (selected == "usb-install") {
        // Failed installation has no SDK RX object. Neither commands nor fake
        // BOOTSTATUS replies are possible; the scheduler exits the inert loop.
        input.clear();expected_outputs=0;
    } else if (stage) {
        // The refused startup parser is the actual app_main stopped() loop.
        // Its first stage stays fixed through valid, premature and oversized data.
        input = "OTCAND1 BOOTSTATUS\nOTCAND1 HELLO\nOTCAND1 BEGIN 0 1 17\n" +
            std::string(33, 'X') + "\nOTCAND1 BOOTSTATUS\n";
        expected_outputs = 5;
    } else if (selected == "public-after-source-loss") {
        input = "OTCAND1 HELLO\nOTCAND1 BEGIN 0 1 17\nOTCAND1 EXPORT\nOTCAND1 HELLO\n";
        expected_outputs = 4;
    } else if (selected == "healthy" || selected == "healthy-fragmented") {
        fragment = selected == "healthy-fragmented" ? 1 : 256;
        input = "OTCAND1 HELLO\nOTCAND1 BEGIN 0 1 17\nOTCAND1 EXPORT\n";
        expected_outputs = 3;
    } else {
        lost_before_begin = selected == "unavailable-before-begin";
        if (selected == "loss-in-begin-nonce") arm_fill(2, fault::FillFault::qualification_lost);
        if (selected == "loss-in-identity-seed") arm_fill(3, fault::FillFault::qualification_lost);
        input = "OTCAND1 HELLO\nOTCAND1 BEGIN 0 1 17\nOTCAND1 EXPORT\nOTCAND1 HELLO\n";
        expected_outputs = 4;
    }
    app_active = true;
    // Bailout happens only at the application loop's scheduler seam, after all
    // owner calls have returned. app_main has only trivial automatic objects at
    // that point; no owner operation/destructor is bypassed by the host jump.
    if (setjmp(returned) == 0) app_main();
    app_active = false;
    CHECK(consumed == input.size() && outputs.size() == expected_outputs && ticks < 4000);
    if (selected == "usb-install") {
        CHECK(!driver_installed && usb_reads==0 && outputs.empty());no_created_secret();
    } else if (stage) {
        const auto status = "OTCAND1 BOOTSTATUS " + std::to_string(stage) + "\n";
        exact_outputs({status, "OTCAND1 REFUSED\n", "OTCAND1 REFUSED\n", "OTCAND1 REFUSED\n", status});
        no_created_secret();
        CHECK(sdk::state.entropy_reads == static_cast<unsigned>(stage == 9));
        if (stage < 4 || selected == "layout") CHECK(sdk::state.controller_starts == 0);
        if ((stage >= 5 && selected != "layout") || selected == "controller-unqualified") CHECK(sdk::state.controller_stops == 1);
    } else if (selected == "healthy" || selected == "healthy-fragmented" || selected == "public-after-source-loss") {
        CHECK(outputs[0] == "OTCAND1 READY 1\n" && outputs[1] == "OTCAND1 OK BEGIN\n");
        CHECK(outputs[2].rfind("OTCAND1 CANDIDATE ", 0) == 0 && outputs[2].back() == '\n');
        CHECK(sdk::state.sets > 0 && sdk::state.commits > 0 && sdk::state.entropy_reads >= 3);
        originals_preserved();
        if (selected == "public-after-source-loss") {
            // An explicit observation, not a proposed stronger entropy contract:
            // outer public admission/export does not recheck controller entropy.
            CHECK(source_lost && outputs[3] == "OTCAND1 READY 1\n" && sdk::state.entropy_reads == fills_at_loss);
            std::printf("OBSERVED outer public readiness/export survives source qualification loss; no new entropy requested\n");
        }
    } else {
        exact_outputs({"OTCAND1 READY 1\n", "OTCAND1 REFUSED\n", "OTCAND1 REFUSED\n", "OTCAND1 REFUSED\n"});
        if (selected == "loss-in-identity-seed") incomplete_identity_only();
        else no_created_secret();
        CHECK(sdk::state.entropy_reads == (selected == "unavailable-before-begin" ? 1U : selected == "loss-in-begin-nonce" ? 2U : 3U));
    }
    if(!outputs.empty())diagnostics_stop_at_first_reply();
}
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
void stop_at_sdk_seam(unsigned stage,unsigned phase) {
    CHECK(!diagnostics.empty() && diagnostics.back()==marker(stage,phase));
    CHECK(consumed==0 || stage==0);CHECK(outputs.empty());no_created_secret();
    std::printf("PASS fixed marker before nonreturning SDK seam %u %u\n",stage,phase);
    std::fflush(stdout);
    // Fresh process ends at the deliberate blocking seam. This case proves
    // publication before entry, and makes no simulated cleanup/recovery claim.
    std::_Exit(0);
}
void run_observability_case() {
    reset();
    if(selected=="diag-reset-software")reset_reason=12;
    if(selected=="diag-reset-unknown")reset_reason=999;
    usb_install_failure=selected.rfind("diag-usb-",0)==0;
    fifo_busy=selected=="diag-usb-busy";fifo_partial=selected=="diag-usb-partial";
    diagnostic_write_drop=selected=="diag-write-drop";
    unsigned occupancy_reason=0;
    if(selected=="diag-occupancy-open"){sdk::arm("open","nimble_bond");occupancy_reason=25;}
    if(selected=="diag-occupancy-iterator"){sdk::arm("iterate","nimble_bond");occupancy_reason=26;}
    if(selected=="diag-occupancy-info"){sdk::arm("info","nimble_bond");occupancy_reason=27;}
    if(selected=="diag-occupancy-type"){sdk::state.disk["nimble_bond"]["our_sec_1"].type=NVS_TYPE_U8;occupancy_reason=28;}
    if(selected=="diag-occupancy-key"){sdk::state.disk["nimble_bond"]["unknown"]={sdk::Bytes(1,0x33),NVS_TYPE_BLOB};occupancy_reason=29;}
    if(selected=="diag-occupancy-size"){sdk::state.disk["nimble_bond"]["our_sec_1"].bytes.resize(1);occupancy_reason=30;}
    if(selected=="diag-occupancy-read"){sdk::arm("get","nimble_bond","our_sec_1",2);occupancy_reason=31;}
    if(occupancy_reason)original=sdk::state.disk;
    if(selected=="diag-runtime-fill")arm_fill(1,fault::FillFault::qualification_lost);
    if(selected=="diag-runtime-context")arm_fill(1,fault::FillFault::all_zero);
    if(selected=="diag-block-cleanup") {
        crypto_init_failure=true;sdk::state.callback=[](const auto& operation,const auto&,const auto&) {
            if(operation=="nimble_deinit") {
                CHECK(std::find(diagnostics.begin(),diagnostics.end(),marker(5,2))!=diagnostics.end());
                stop_at_sdk_seam(17,0);
            }
        };
    }
    if(selected=="diag-block-nvs" || selected=="diag-block-store" || selected=="diag-block-storage") {
        sdk::state.callback=[](const auto& operation,const auto& name,const auto&) {
            if(selected=="diag-block-nvs" && operation=="init" && name=="nvs")stop_at_sdk_seam(2,0);
            if(selected=="diag-block-store" && operation=="nimble_init")stop_at_sdk_seam(12,0);
            if(selected=="diag-block-storage" && operation=="partition_init" && name=="ot238_nvs")stop_at_sdk_seam(8,0);
        };
    }
    if(selected=="diag-block-display")heltec_oled_stub::state.on_draw=[]{stop_at_sdk_seam(6,0);};
    if(selected=="diag-block-service" || selected=="diag-block-command" ||
       selected=="diag-block-initial-dispatch" || selected=="diag-block-initial-clock" || selected=="diag-runtime-clock") {
        heltec_oled_stub::state.on_clock=[] {
            if(selected=="diag-block-initial-dispatch" && !diagnostics.empty() && diagnostics.back()==marker(23,0))stop_at_sdk_seam(23,0);
            if(selected=="diag-block-initial-clock" && !diagnostics.empty() && diagnostics.back()==marker(24,0))stop_at_sdk_seam(24,0);
            if(selected=="diag-runtime-clock" && !diagnostics.empty() && diagnostics.back()==marker(24,0))heltec_oled_stub::state.now_us=-1;
            const unsigned phase=selected=="diag-block-service"?0:6;
            if((selected=="diag-block-service" || selected=="diag-block-command") &&
               !diagnostics.empty() && diagnostics.back()==marker(0,phase))stop_at_sdk_seam(0,phase);
        };
    }
    fragment=selected=="diag-fragmented"?1:256;
    input=usb_install_failure?"":selected=="diag-partial-command"?"OTCAND1 HEL":"OTCAND1 HELLO\nOTCAND1 BOOTSTATUS\n";
    expected_outputs=usb_install_failure || selected=="diag-partial-command" || selected.rfind("diag-control-write-",0)==0?0:2;
    app_active=true;
    if(setjmp(returned)==0)app_main();
    app_active=false;
    no_created_secret();
    if(usb_install_failure) {
        CHECK(!driver_installed && usb_reads==0 && outputs.empty());
        CHECK(fifo_writes<=34 && fifo_flushes==fifo_writes);
        if(fifo_busy)CHECK(fifo_writes==0 && lowlevel.empty());
        else if(fifo_partial){CHECK(!lowlevel.empty());for(const auto& line:lowlevel)CHECK(line.size()==4);}
        else {CHECK(lowlevel.size()==34);CHECK(lowlevel.front()=="\n"+marker(1,0));
            CHECK(lowlevel[1]=="\n"+marker(1,2));for(std::size_t i=2;i<lowlevel.size();++i)CHECK(lowlevel[i]==lowlevel[1]);}
        return;
    }
    CHECK(driver_installed && usb_reads>0);
    if(selected=="diag-partial-command") {
        CHECK(outputs.empty() && consumed==input.size());
        CHECK(diagnostics.back()==marker(0,4));
        CHECK(std::find(diagnostics.begin(),diagnostics.end(),marker(0,6))==diagnostics.end());
        return;
    }
    if(selected.rfind("diag-control-write-",0)==0) {
        CHECK(outputs.empty() && std::count(diagnostics.begin(),diagnostics.end(),"\n"+marker(1,2))==1);
        CHECK(diagnostics.back()=="\n"+marker(1,2));return;
    }
    if(occupancy_reason || selected.rfind("diag-runtime-",0)==0) {
        const unsigned stage=occupancy_reason?4:9;
        exact_outputs({"OTCAND1 REFUSED\n","OTCAND1 BOOTSTATUS "+std::to_string(stage)+"\n"});
        const unsigned reason=occupancy_reason?occupancy_reason:selected=="diag-runtime-marker"?18:
            selected=="diag-runtime-budget"?19:selected=="diag-runtime-restore"?20:selected=="diag-runtime-clock"?24:21;
        CHECK(std::find(diagnostics.begin(),diagnostics.end(),marker(reason,2))!=diagnostics.end());
        if(occupancy_reason)CHECK(sdk::state.controller_starts==0);
        diagnostics_stop_at_first_reply();return;
    }
    CHECK(consumed==input.size());exact_outputs({"OTCAND1 READY 1\n","OTCAND1 BOOTSTATUS 0\n"});
    diagnostics_stop_at_first_reply();
    if(diagnostic_write_drop){CHECK(diagnostics.empty());return;}
    const unsigned reason=reset_reason==999?0:static_cast<unsigned>(reset_reason);
    std::vector<std::string> expected;
    for(const unsigned stage:{1U,8U,2U,3U,4U,5U,6U,7U,8U,9U}) {
        if(stage!=1)expected.push_back(marker(stage,0,reason));
        if(stage==4)for(unsigned part=10;part<=16;++part){expected.push_back(marker(part,0,reason));expected.push_back(marker(part,1,reason));}
        if(stage==9) {
            for(unsigned part=18;part<=21;++part){expected.push_back(marker(part,0,reason));expected.push_back(marker(part,1,reason));}
            expected.push_back(marker(22,0,reason));
            for(unsigned part=23;part<=24;++part){expected.push_back(marker(part,0,reason));expected.push_back(marker(part,1,reason));}
            expected.push_back(marker(22,1,reason));
        }
        expected.push_back(marker(stage,1,reason));
    }
    expected.push_back(marker(0,0,reason));expected.push_back(marker(0,3,reason));
    if(selected=="diag-delayed-hello")for(unsigned i=0;i<32;++i)expected.push_back(marker(0,3,reason));
    expected.push_back(marker(0,4,reason));expected.push_back(marker(0,6,reason));expected.push_back(marker(0,5,reason));
    CHECK(diagnostics==expected);
    CHECK(diagnostics.size()+lowlevel.size()<=96);
    CHECK(lowlevel==std::vector<std::string>{"\n"+marker(1,0,reason)});
}
#endif
void guarded_source_case() {
    reset(); board::ConfirmationNonowningEntropy source;
    std::array<std::uint8_t, 32> out{}; out.fill(0xa7); const auto untouched = out;
    CHECK(!source.activate() && source.random().state() == Entropy::not_ready);
    CHECK(!source.random().fill(out.data(), out.size()).ok() && out == untouched && sdk::state.entropy_reads == 0);
    sdk::state.controller_status = ESP_BT_CONTROLLER_STATUS_ENABLED;
    CHECK(source.activate() && source.random().state() == Entropy::ready);
    arm_fill(1, fault::FillFault::qualification_lost);
    const auto failed = source.random().fill(out.data(), out.size());
    CHECK(!failed.ok() && failed.bytes_written == 0 && out == untouched && source.random().state() == Entropy::not_ready);
    sdk::state.controller_status = ESP_BT_CONTROLLER_STATUS_ENABLED;
    CHECK(!source.random().fill(out.data(), out.size()).ok() && out == untouched && sdk::state.entropy_reads == 1);
    CHECK(source.revoke() && source.random().state() == Entropy::not_ready);
    originals_preserved();
}
void reconstructed_owner_case() {
    reset(); reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;
    target::CandidateStoreRuntime old(bonds); CHECK(old.start());
    std::array<std::uint8_t, 8> bytes{}; CHECK(old.random().fill(bytes.data(), bytes.size()).ok());
    CHECK(old.stop() && old.random().state() == Entropy::not_ready);
    CHECK(!old.start()); // Same attempted owner cannot silently rearms itself.
    fault::state.unqualified_after_controller_init = true;
    target::CandidateStoreRuntime restarted(bonds); CHECK(!restarted.start());
    bytes.fill(0xa7); const auto untouched = bytes;
    CHECK(!restarted.random().fill(bytes.data(), bytes.size()).ok() && bytes == untouched);
    CHECK(sdk::state.entropy_reads == 1 && restarted.stop());
    fault::state.unqualified_after_controller_init = false;
    target::CandidateStoreRuntime fresh(bonds); CHECK(fresh.start());
    CHECK(old.random().state() == Entropy::not_ready && restarted.random().state() == Entropy::not_ready);
    CHECK(fresh.random().fill(bytes.data(), bytes.size()).ok() && fresh.stop());
    no_created_secret();
}
constexpr const char* cases[] = {
    "healthy", "healthy-fragmented", "usb-install", "layout", "default-nvs", "reset-marker",
    "controller-init", "controller-unqualified", "library-init", "display", "input", "candidate-storage",
    "startup-source-loss", "startup-zero-context", "unavailable-before-begin", "loss-in-begin-nonce",
    "loss-in-identity-seed", "public-after-source-loss", "guarded-source", "reconstructed-owner"
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
    ,"diag-healthy","diag-fragmented","diag-delayed-hello","diag-partial-command",
    "diag-usb-failed","diag-usb-busy","diag-usb-partial","diag-write-drop",
    "diag-reset-software","diag-reset-unknown","diag-block-nvs","diag-block-store",
    "diag-block-display","diag-block-storage","diag-block-service","diag-block-command"
    ,"diag-control-write-failed","diag-control-write-partial","diag-block-cleanup",
    "diag-occupancy-open","diag-occupancy-iterator","diag-occupancy-info","diag-occupancy-type",
    "diag-occupancy-key","diag-occupancy-size","diag-occupancy-read","diag-runtime-marker",
    "diag-runtime-budget","diag-runtime-restore","diag-runtime-fill","diag-runtime-context",
    "diag-runtime-clock","diag-block-initial-dispatch","diag-block-initial-clock"
#endif
};
int child(const char* executable, const char* scenario) {
    const char* args[]{executable, scenario, nullptr};
#ifdef _WIN32
    return static_cast<int>(_spawnv(_P_WAIT, executable, args));
#else
    const auto pid = fork(); CHECK(pid >= 0);
    if (pid == 0) { execv(executable, const_cast<char* const*>(args)); _exit(127); }
    int status{}; CHECK(waitpid(pid, &status, 0) == pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
}
}
extern "C" int __wrap_sodium_init() {
    // The existing scalar host corpus does not link every initialization-only
    // library backend. Model this startup return seam exactly as prior actual-
    // app suites do; real SDK library initialization remains build/physical.
    return admission::crypto_init_failure ? -1 : 0;
}
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t* config) {
    CHECK(config && config->tx_buffer_size == 4096 && config->rx_buffer_size == 4096);
    admission::driver_installed=!admission::usb_install_failure;
    return admission::driver_installed?ESP_OK:ESP_FAIL;
}
int usb_serial_jtag_read_bytes(void* out, std::size_t size, std::uint32_t) {
    using namespace admission;
    CHECK(driver_installed && out && size > 0 && size <= 256);++usb_reads;
    if(selected=="diag-delayed-hello" && usb_reads<=40)return 0;
    if (!source_lost && ((lost_before_begin && consumed >= std::strlen("OTCAND1 HELLO\n")) ||
        (selected == "public-after-source-loss" && outputs.size() >= 2))) {
        sdk::state.controller_status = ESP_BT_CONTROLLER_STATUS_IDLE; source_lost = true;
        fills_at_loss = sdk::state.entropy_reads;
    }
    // Do not read the next command in the same chunk when a between-command
    // failure is requested. This is a controlled I/O seam, not peer authority.
    const auto limit = lost_before_begin || selected == "public-after-source-loss" ? 1U : fragment;
    const auto count = std::min({size, limit, input.size() - consumed});
    if (count) std::memcpy(out, input.data() + consumed, count);
    consumed += count; return static_cast<int>(count);
}
int usb_serial_jtag_write_bytes(const void* data, std::size_t size, std::uint32_t wait) {
    CHECK(data && size > 0 && size <= admission::target::CandidateRuntime::Output{}.size());
    CHECK(admission::driver_installed);
    const std::string line(static_cast<const char*>(data),size);
    if(line.rfind("OTBOOT1 ",0)==0 || line.rfind("\nOTBOOT1 ",0)==0) {
        CHECK(wait==0 && size<32);
        if(admission::diagnostic_write_drop)return 0;
        admission::diagnostics.push_back(line);admission::record_wire(line);
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
        if(admission::selected=="diag-runtime-marker" && line==admission::marker(18,0))admission::sdk::arm("get","ot_reset_v1");
        if(admission::selected=="diag-runtime-budget" && line==admission::marker(19,0))admission::sdk::state.total_entries=admission::sdk::used(admission::sdk::state);
        if(admission::selected=="diag-runtime-restore" && line==admission::marker(20,0))admission::sdk::arm("get",admission::target::CandidateNvsStorage::kNamespace);
#endif
        return static_cast<int>(size);
    }
    if(admission::selected.rfind("diag-control-write-",0)==0) {
        if(admission::selected=="diag-control-write-partial"){admission::record_wire(line.substr(0,2));return 2;}
        return 0;
    }
    admission::record_wire(line);
    admission::outputs.emplace_back(static_cast<const char*>(data), size);
    CHECK(admission::outputs.size() <= admission::expected_outputs);
    return static_cast<int>(size);
}
void ot237_app_tick(std::uint32_t ticks) {
    CHECK(ticks > 0);
    if (!admission::app_active) return;
    CHECK(++admission::ticks < 4000);
    heltec_oled_stub::state.now_us+=1'000'000;
    if(admission::selected.rfind("diag-usb-",0)==0 && admission::ticks==40)longjmp(admission::returned,1);
    if(admission::selected=="diag-partial-command" && admission::ticks==40)longjmp(admission::returned,1);
    if(admission::selected.rfind("diag-control-write-",0)==0 && admission::ticks==40)longjmp(admission::returned,1);
    if(admission::selected=="usb-install" && admission::ticks==4)longjmp(admission::returned,1);
    if (admission::selected.rfind("diag-usb-",0)!=0 && admission::selected!="diag-partial-command" &&
        admission::selected.rfind("diag-control-write-",0)!=0 &&
        admission::consumed == admission::input.size() && admission::outputs.size() == admission::expected_outputs)
        longjmp(admission::returned, 1);
}
int esp_rom_get_reset_reason(int cpu) {CHECK(cpu==0);return admission::reset_reason;}
int usb_serial_jtag_ll_txfifo_writable() {CHECK(!admission::driver_installed);return admission::fifo_busy?0:1;}
std::uint32_t usb_serial_jtag_ll_write_txfifo(const std::uint8_t* data,std::uint32_t size) {
    CHECK(!admission::driver_installed && data && size<32 && !admission::fifo_busy);
    ++admission::fifo_writes;const auto count=admission::fifo_partial?std::min(size,4U):size;
    admission::lowlevel.emplace_back(reinterpret_cast<const char*>(data),count);
    admission::record_wire(admission::lowlevel.back());return count;
}
void usb_serial_jtag_ll_txfifo_flush() {CHECK(!admission::driver_installed);++admission::fifo_flushes;}
int main(int argc, char** argv) {
    if (argc == 1) {
        for (const auto* scenario : admission::cases) CHECK(admission::child(argv[0], scenario) == 0);
        std::printf("PASS %zu actual enrollment candidate entropy admission groups\n", std::size(admission::cases));
        return 0;
    }
    CHECK((argc == 2 || argc == 3) && std::find(std::begin(admission::cases), std::end(admission::cases), std::string(argv[1])) != std::end(admission::cases));
    admission::selected = argv[1];
    if(argc==3){CHECK(admission::selected.rfind("diag-",0)==0);admission::producer_fixture=std::fopen(argv[2],"wb");CHECK(admission::producer_fixture);}
    if (admission::selected == "guarded-source") admission::guarded_source_case();
    else if (admission::selected == "reconstructed-owner") admission::reconstructed_owner_case();
#if OT_CANDIDATE_STARTUP_DIAGNOSTICS
    else if(admission::selected.rfind("diag-",0)==0)admission::run_observability_case();
#endif
    else admission::run_app_case();
    std::printf("PASS 1 entropy admission case %s\n", argv[1]);
    if(admission::producer_fixture)CHECK(std::fclose(admission::producer_fixture)==0);
    return 0;
}
