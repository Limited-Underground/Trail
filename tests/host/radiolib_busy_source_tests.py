"""Portable actual-driver regression for bounded TX-start BUSY containment."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from radiolib_busy_source import bounded_method, generate, SourceError, BEFORE, AFTER
FIXTURE = ROOT / "tests/fixtures/radiolib_busy"


class RadioBusyTests(unittest.TestCase):
    def test_fixture_pins_and_single_correction(self):
        manifest = json.loads((FIXTURE / "manifest.json").read_text())
        for name, record in manifest.items():
            self.assertEqual(hashlib.sha256((FIXTURE / (name + ".cpp")).read_bytes()).hexdigest(), record["method_sha256"])
        raw = (FIXTURE / "launch.cpp").read_bytes()
        corrected = bounded_method(raw)
        self.assertEqual(corrected.replace(AFTER.encode(), BEFORE.encode()), raw)
        with self.assertRaises(SourceError): bounded_method(raw + b"\n")
        with self.assertRaises(SourceError): bounded_method(corrected)
        with self.assertRaises(SourceError): generate(raw)
        with self.assertRaises(SourceError): generate(b"untrusted source")

    def test_actual_driver_faults(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "C++20 GCC is required for actual-driver regression")
        methods = (FIXTURE / "transmit.cpp").read_bytes() + b"\n" + (FIXTURE / "start_transmit.cpp").read_bytes() + b"\n" + bounded_method((FIXTURE / "launch.cpp").read_bytes())
        main = r"""
int main() {
    uint8_t payload[64]{};
    // Held BUSY returns the existing SPI timeout, clears staging and does no
    // finishTransmit/packet query/retry operation after the uncertain setTx.
    for (uint32_t offset : {uint32_t(0), uint32_t(0xfffffff0)}) {
        Hal hal;hal.busy=true;hal.offset=offset;Module module{&hal};SX126x radio{&module};
        assert(radio.transmit(payload,64,0)==RADIOLIB_ERR_SPI_CMD_TIMEOUT);
        assert(hal.elapsed==1000 && radio.stagedMode==RADIOLIB_RADIO_MODE_NONE);
        assert(radio.set_tx_calls==1 && radio.packet_type_calls==0 && radio.finish_calls==0);
        assert(radio.launchMode()==RADIOLIB_ERR_UNSUPPORTED && radio.set_tx_calls==1);
    }
    // Healthy and finite late BUSY clearance preserve existing transmit logic.
    for(unsigned release : {0U, 990U, 1000U}) {
        Hal hal;hal.busy=release!=0;hal.clear_after=release;hal.irq=true;
        Module module{&hal};SX126x radio{&module};
        assert(radio.transmit(payload,64,0)==0);
        assert(hal.elapsed==release+10 && radio.finish_calls==1 && radio.set_tx_calls==1);
    }
    { Hal hal;Module module{&hal};SX126x radio{&module};
      assert(radio.transmit(payload,64,0)==RADIOLIB_ERR_TX_TIMEOUT);
      assert(hal.elapsed==600 && radio.finish_calls==1); }
    { Hal hal;hal.busy=true;Module module{&hal};module.spiConfig.timeout=20;SX126x radio{&module};
      assert(radio.transmit(payload,64,0)==RADIOLIB_ERR_SPI_CMD_TIMEOUT && hal.elapsed==20); }
    { Hal hal;Module module{&hal};SX126x radio{&module};radio.set_tx_result=-77;
      assert(radio.transmit(payload,64,0)==-77 && hal.elapsed==0 && radio.finish_calls==0); }
    { Hal hal;hal.busy=true;Module module{&hal};SX126x radio{&module};radio.stagedMode=RADIOLIB_RADIO_MODE_RX;
      assert(radio.launchMode()==0 && radio.stagedMode==RADIOLIB_RADIO_MODE_NONE && hal.elapsed==0); }
    std::puts("actual driver: held BUSY, wraparound, late release, IRQ timeout, healthy, configurable deadline, command error, RX passed");
}
"""
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/"actual.cpp"
            cpp.write_bytes(b"#include <initializer_list>\n" + (FIXTURE / "scaffolding.hpp").read_bytes() + methods + main.encode())
            binary=Path(directory)/("actual.exe" if sys.platform=="win32" else "actual")
            subprocess.run([compiler,"-std=c++20","-Wall","-Wextra","-Werror",str(cpp),"-o",str(binary)],check=True,timeout=30)
            result=subprocess.run([str(binary)],check=True,timeout=5,capture_output=True,text=True)
            self.assertIn("passed",result.stdout)


if __name__ == "__main__":
    unittest.main()
