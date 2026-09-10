"""Compile actual additive entropy runtime/guard/Heltec adapter against SDK seams."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[2]
STUBS={
"sdkconfig.h": "#define CONFIG_IDF_TARGET_ESP32S3 1\n#define CONFIG_BT_CONTROLLER_ENABLED 1\n#ifdef BAD_CONFIG\n#define CONFIG_BT_CTRL_MODEM_SLEEP 1\n#endif\n#ifdef BAD_PM\n#define CONFIG_PM_ENABLE 1\n#endif\n",
"esp_random.h": '#pragma once\n#include <stddef.h>\nextern "C" void esp_fill_random(void*,size_t);\n',
"esp_timer.h": '#pragma once\n#include <stdint.h>\nextern "C" int64_t esp_timer_get_time();\n',
"esp_rom_sys.h": '#pragma once\nextern "C" void esp_rom_delay_us(unsigned);\n',
"esp_bt.h": """#pragma once
#define ESP_OK 0
#define ESP_BT_MODE_BLE 1
struct esp_bt_controller_config_t {int mode;};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {1}
enum esp_bt_controller_status_t {ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED,ESP_BT_CONTROLLER_STATUS_ENABLED};
extern "C" {int esp_bt_controller_init(esp_bt_controller_config_t*);int esp_bt_controller_enable(int);int esp_bt_controller_disable();int esp_bt_controller_deinit();esp_bt_controller_status_t esp_bt_controller_get_status();}
"""}
class RuntimeTests(unittest.TestCase):
 def test_actual_runtime_scenarios(self):
  compiler=shutil.which("g++");self.assertIsNotNone(compiler,"native g++ is required")
  with tempfile.TemporaryDirectory(prefix="trail-entropy-runtime-") as name:
   d=Path(name)
   for path,raw in STUBS.items():(d/path).write_text(raw)
   common=[compiler,"-std=c++17","-Wall","-Wextra","-Werror","-UNDEBUG","-pthread","-I",str(d)]
   for path in ["firmware/targets/heltec_v4_security_eval/main","firmware/targets/heltec_v4_bench/main","firmware/components/security/include"]:common += ["-I",str(ROOT/path)]
   common += [str(ROOT/p) for p in ["firmware/targets/heltec_v4_security_eval/main/entropy_runtime.cpp","firmware/targets/heltec_v4_bench/main/heltec_v4_secure_random.cpp","firmware/components/security/src/serialized_secure_random.cpp","tests/host/entropy_runtime_tests.cpp"]]
   for bad in ["", "BAD_CONFIG", "BAD_PM"]:
    exe=d/((bad or "runtime")+".exe")
    built=subprocess.run(common+(["-D"+bad] if bad else [])+["-o",str(exe)],capture_output=True,text=True,timeout=60)
    self.assertEqual(built.returncode,0,built.stdout+built.stderr)
    cases=["config"] if bad else ["success","reentry","external","init_failure","enable_failure","status_failure","disable_failure","deinit_failure","inflight_timeout","inflight_drain","clock_regression","clock_stuck","invalid_budget"]
    for case in cases:
     with self.subTest(case=case):
      ran=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=10)
      self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=="__main__":unittest.main()
