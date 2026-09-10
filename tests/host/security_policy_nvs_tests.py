"""Compile and exercise the actual four-namespace NVS backend and durable stores."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run():
    compiler = shutil.which("g++") or shutil.which("c++")
    if compiler is None:
        raise RuntimeError("a native C++17 compiler is required on PATH")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="policy-nvs-", dir=build) as directory:
        executable = Path(directory) / "policy-nvs-tests.exe"
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DNDEBUG", "-fno-exceptions", "-fno-rtti"]
        for include in ("firmware/components/security_evaluation/include",
                        "firmware/components/persistence/include",
                        "tests/host/fixtures/security_eval_nvs",
                        "firmware/targets/heltec_v4_security_policy_eval/main"):
            command.extend(["-I", str(ROOT / include)])
        command.extend(str(ROOT / source) for source in (
            "tests/host/security_policy_nvs_tests.cpp",
            "firmware/components/persistence/src/persistent_storage_kv.cpp",
            "firmware/components/persistence/src/outbound_counter_lease_store.cpp"))
        command.extend(["-o", str(executable)])
        subprocess.run(command, check=True, timeout=90)
        result = subprocess.run([str(executable)], check=True, capture_output=True,
                                text=True, timeout=20)
        if result.stdout.strip() != "PASS 23 actual policy NVS backend host groups":
            raise RuntimeError("unexpected replay test result: " + result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    run()
