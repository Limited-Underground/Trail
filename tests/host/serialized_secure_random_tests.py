"""Actual unchanged Heltec adapter under a serialized lifecycle guard; no devices."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class LifecycleTests(unittest.TestCase):
    def test_actual_adapter_lifecycle(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ is required")
        sources = ["firmware/components/security/src/serialized_secure_random.cpp",
                   "firmware/targets/heltec_v4_bench/main/heltec_v4_secure_random.cpp",
                   "tests/host/serialized_secure_random_tests.cpp"]
        includes = ["firmware/components/security/include",
                    "firmware/targets/heltec_v4_bench/main",
                    "tests/host/fixtures/heltec_secure_random"]
        with tempfile.TemporaryDirectory(prefix="trail-entropy-lifecycle-") as directory:
            exe = Path(directory) / "entropy-test.exe"
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-pthread"]
            for path in includes:
                command += ["-I", str(ROOT / path)]
            command += [str(ROOT / path) for path in sources] + ["-o", str(exe)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("fault isolation PASS", ran.stdout)

if __name__ == "__main__":
    unittest.main()
