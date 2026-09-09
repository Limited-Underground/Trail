"""Extract exact installed driver methods; fake only HAL/peripheral dependencies."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/radiolib-busy-repro"
OUT.mkdir(parents=True, exist_ok=True)
LIB = ROOT / 'tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/managed_components/jgromes__radiolib/src'


def digest(raw): return hashlib.sha256(raw).hexdigest()


def method(relative, signature):
    path = LIB / relative
    raw = path.read_bytes()
    pins = {'modules/SX126x/SX126x.cpp': '57c7256bae24b9fa2d1ad2cd94e40d6f4d0c6a3c3dc290f3bfa858304ea1deb7',
            'protocols/PhysicalLayer/PhysicalLayer.cpp': '118449d38a4c2b4bb586b7092a6094b87b6d667890f1f6450b4ad8d9a9bf339b'}
    if digest(raw) != pins[relative]:
        raise RuntimeError('radiolib_source_mismatch')
    source = raw.decode('utf-8')
    assert source.count(signature) == 1
    start = source.index(signature)
    opened = source.index('{', start)
    depth = 1
    end = opened + 1
    while depth:
        if source[end] == '{': depth += 1
        if source[end] == '}': depth -= 1
        end += 1
    body = source[start:end]
    evidence.append({'path': str(path.relative_to(ROOT)).replace('\\', '/'),
                     'file_sha256': digest(raw), 'method': signature,
                     'method_sha256': digest(body.encode()), 'method_bytes': len(body.encode())})
    return body + '\n'


evidence = []
methods = method('modules/SX126x/SX126x.cpp', 'int16_t SX126x::transmit(const uint8_t* data, size_t len, uint8_t addr)')
methods += method('protocols/PhysicalLayer/PhysicalLayer.cpp', 'int16_t PhysicalLayer::startTransmit(const uint8_t* data, size_t len, uint8_t addr)')
methods += method('modules/SX126x/SX126x.cpp', 'int16_t SX126x::launchMode()')

declarations = r'''
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
using RadioLibTime_t = uint64_t;
// Branch constants and successful peripheral stubs are harness scaffolding.
// All transmit/startTransmit/launchMode control flow is extracted unchanged.
constexpr int RADIOLIB_SX126X_LORA_CR_4_8=4;
constexpr int RADIOLIB_SX126X_LORA_CRC_ON=1;
constexpr int RADIOLIB_SX126X_MAX_PACKET_LENGTH=255;
constexpr int RADIOLIB_ERR_PACKET_TOO_SHORT=-1, RADIOLIB_ERR_PACKET_TOO_LONG=-2;
constexpr int RADIOLIB_ERR_TX_TIMEOUT=-5, RADIOLIB_ERR_UNSUPPORTED=-3;
constexpr int RADIOLIB_SX126X_PACKET_TYPE_LR_FHSS=3, RADIOLIB_SX126X_IRQ_TX_DONE=1;
constexpr int RADIOLIB_RADIO_MODE_RX=1, RADIOLIB_RADIO_MODE_TX=2, RADIOLIB_RADIO_MODE_NONE=0;
constexpr int RADIOLIB_SX126X_TX_TIMEOUT_NONE=0;
#define RADIOLIB_ASSERT(state) do { if ((state) != 0) return (state); } while(false)
#define RADIOLIB_DEBUG_BASIC_PRINTLN(...) ((void)0)
struct HostWatchdog {};
struct Hal {
    bool busy=false, irq=false; unsigned yields=0, millis_reads=0;
    uint64_t elapsed=0;
    void yield() { elapsed += 10; if (++yields==100) throw HostWatchdog{}; }
    uint64_t millis() { ++millis_reads; return elapsed; }
    bool digitalRead(int pin) { return pin==13 ? busy : irq; }
};
struct Module {
    static constexpr int MODE_RX=1;
    Hal* hal;
    int getGpio() {return 13;} int getIrq() {return 14;}
    void setRfSwitchState(int) {}
};
struct RadioModeConfig_t { struct {const uint8_t* data; size_t len; uint8_t addr;} transmit; };
struct PhysicalLayer {
    virtual int16_t stageMode(int, RadioModeConfig_t*)=0;
    virtual int16_t launchMode()=0;
    int16_t startTransmit(const uint8_t*,size_t,uint8_t);
};
struct SX126x : PhysicalLayer {
    Module* mod;
    int codingRate=1, crcTypeLoRa=1, stagedMode=0, txMode=2, rxTimeout=0;
    unsigned packet_type_calls=0, finish_calls=0, set_tx_calls=0;
    explicit SX126x(Module* value):mod(value){}
    int16_t standby() {return 0;}
    uint64_t getTimeOnAir(size_t len) {assert(len==64);return 118016;}
    int16_t stageMode(int mode, RadioModeConfig_t*) override {stagedMode=mode;return 0;}
    int16_t launchMode() override;
    int16_t transmit(const uint8_t*,size_t,uint8_t);
    int getPacketType() {++packet_type_calls;return 1;}
    int16_t finishTransmit() {++finish_calls;return 0;}
    int getIrqFlags() {return 1;} void hopLRFHSS() {}
    int16_t setRx(int) {return 0;} int16_t setTx(int) {++set_tx_calls;return 0;}
};
'''
main = r'''
int main() {
    uint8_t payload[64]{};
    {
        Hal hal;hal.busy=true;Module module{&hal};SX126x radio{&module};bool interrupted=false;
        try {radio.transmit(payload,64,0);} catch(const HostWatchdog&) {interrupted=true;}
        assert(interrupted && hal.elapsed==1000 && hal.millis_reads==0);
        assert(radio.set_tx_calls==1 && radio.packet_type_calls==0 && radio.finish_calls==0);
        std::puts("stuck_busy: interrupted_after_1000_simulated_ms timeout_clock_reads=0 finish_calls=0");
    }
    {
        Hal hal;Module module{&hal};SX126x radio{&module};
        assert(radio.transmit(payload,64,0)==RADIOLIB_ERR_TX_TIMEOUT);
        assert(hal.elapsed==600 && radio.finish_calls==1 && radio.packet_type_calls==1);
        std::puts("busy_low_irq_low: driver_timeout_after_600_simulated_ms finish_calls=1");
    }
    {
        Hal hal;hal.irq=true;Module module{&hal};SX126x radio{&module};
        assert(radio.transmit(payload,64,0)==0 && radio.finish_calls==1);
        std::puts("busy_low_irq_high: completed finish_calls=1");
    }
}
'''
cpp = OUT / 'actual_methods.cpp'
cpp.write_text(declarations + methods + main, encoding='utf-8', newline='\n')
compiler = shutil.which('g++')
assert compiler
binary = OUT / 'actual_methods.exe'
command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)]
subprocess.run(command, check=True, timeout=30)
result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=5)
record = {'scope': 'Constructed HAL faults, not evidence of physical BUSY state.',
          'driver_methods': evidence, 'harness_sha256': digest(cpp.read_bytes()),
          'compiler': subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],
          'results': result.stdout.splitlines(), 'exit_code': result.returncode}
(OUT / 'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record,indent=2))
