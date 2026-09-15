"""OT234 exact RadioLib source generation and compiled calibration-wait proof."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import pair_radio_driver_source as source
import radiolib_busy_source as prior

METHOD = ROOT / 'tests/host/pair_radio_driver_stubs/calibration_config.cpp'


class SourceTests(unittest.TestCase):
    def test_exact_source_and_existing_tx_derivative(self):
        raw = METHOD.read_bytes()
        result = source.bounded_calibration_method(raw)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), source.METHOD_SHA256)
        self.assertEqual(result.replace(source.AFTER.encode(), source.BEFORE.encode()), raw)
        for rejected in (raw + b'\n', result, b'untrusted'):
            with self.assertRaises(ValueError):
                source.bounded_calibration_method(rejected)
        with self.assertRaises(ValueError):
            source.generate_config(raw)
        core_method = (ROOT / 'tests/fixtures/radiolib_busy/launch.cpp').read_bytes()
        self.assertEqual(prior.bounded_method(core_method).count(prior.AFTER.encode()), 1)

    def test_actual_config_method(self):
        method = source.bounded_calibration_method(METHOD.read_bytes()).decode()
        scaffold = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using RadioLibTime_t = uint32_t;
constexpr int RADIOLIB_ERR_SPI_CMD_TIMEOUT=-705;
constexpr int RADIOLIB_SX126X_CMD_SET_PACKET_TYPE=1, RADIOLIB_SX126X_CMD_SET_RX_TX_FALLBACK_MODE=2;
constexpr int RADIOLIB_SX126X_RX_TX_FALLBACK_MODE_STDBY_XOSC=3, RADIOLIB_SX126X_RX_TX_FALLBACK_MODE_STDBY_RC=4;
constexpr int RADIOLIB_SX126X_CAD_ON_8_SYMB=5, RADIOLIB_SX126X_CAD_PARAM_DET_MIN=6, RADIOLIB_SX126X_CAD_GOTO_STDBY=7;
constexpr int RADIOLIB_SX126X_CMD_SET_CAD_PARAMS=8, RADIOLIB_SX126X_IRQ_NONE=0;
constexpr int RADIOLIB_SX126X_CALIBRATE_ALL=9, RADIOLIB_SX126X_CMD_CALIBRATE=10;
#define RADIOLIB_ASSERT(value) do{if(value)return(value);}while(0)
struct Hal {
 uint32_t offset=0,elapsed=0,release=UINT32_MAX;
 uint32_t millis(){return offset+elapsed;}
 void delay(unsigned n){elapsed+=n;}
 bool digitalRead(unsigned){return elapsed<release;}
 void yield(){++elapsed;}
};
struct Module {
 Hal* hal; struct {unsigned timeout=1000;} spiConfig{};
 unsigned checks=0,writes=0; int error=0;
 unsigned getGpio(){return 13;}
 int16_t SPIwriteStream(unsigned,const uint8_t*,unsigned,bool=true,bool=true){++writes;return error;}
 int16_t SPIcheckStream(){++checks;return 0;}
};
class SX126x {
 public: Module* mod; bool resetOnStartup=true,standbyXOSC=false; unsigned spreadingFactor=7;
 int16_t config(uint8_t);
 int16_t setBufferBaseAddress(){return 0;}
 int16_t clearIrqStatus(){return 0;}
 int16_t setDioIrqParams(unsigned,unsigned){return 0;}
};
'''
        main = r'''
int main(){
 for(uint32_t offset:{0U,0xfffffff0U}){
  Hal hal;hal.offset=offset;Module module{&hal};SX126x radio{&module};
  assert(radio.config(1)==RADIOLIB_ERR_SPI_CMD_TIMEOUT);
  assert(hal.elapsed==1000 && module.checks==0 && module.writes==4);
 }
 for(unsigned release:{0U,990U,1000U}){
  Hal hal;hal.release=release;Module module{&hal};SX126x radio{&module};
  assert(radio.config(1)==0 && module.checks==1);
  assert(hal.elapsed==(release<5?5:release));
 }
 {Hal hal;hal.release=1001;Module module{&hal};SX126x radio{&module};
  assert(radio.config(1)==RADIOLIB_ERR_SPI_CMD_TIMEOUT && module.checks==0);}
 {Hal hal;Module module{&hal};module.spiConfig.timeout=20;SX126x radio{&module};
  assert(radio.config(1)==RADIOLIB_ERR_SPI_CMD_TIMEOUT && hal.elapsed==20 && module.checks==0);}
 {Hal hal;Module module{&hal};module.error=-99;SX126x radio{&module};
  assert(radio.config(1)==-99 && hal.elapsed==0 && module.checks==0 && module.writes==1);}
 std::puts("PASS 8 actual calibration groups");
}
'''
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler, 'C++17 compiler required')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'config.cpp'
            executable = Path(directory) / ('config.exe' if os.name == 'nt' else 'config')
            cpp.write_text(scaffold + method + main)
            compiled = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                       '-fno-exceptions', '-fno-rtti', str(cpp), '-o', str(executable)],
                                      capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=5)
            self.assertEqual(result.stdout.strip(), 'PASS 8 actual calibration groups')


if __name__ == '__main__':
    unittest.main()
