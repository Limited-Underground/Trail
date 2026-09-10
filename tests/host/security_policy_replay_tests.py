"""Compile and exercise the actual replay store with persistent fault injection."""
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
    with tempfile.TemporaryDirectory(prefix="replay-store-", dir=build) as directory:
        executable = Path(directory) / "replay-tests.exe"
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DNDEBUG", "-fno-exceptions", "-fno-rtti"]
        for include in ("firmware/components/security_evaluation/include",
                        "firmware/components/persistence/include",
                        "firmware/components/persistence/test_support"):
            command.extend(["-I", str(ROOT / include)])
        command.extend(str(ROOT / source) for source in (
            "tests/host/security_policy_replay_tests.cpp",
            "firmware/components/persistence/test_support/memory_persistent_storage.cpp"))
        command.extend(["-o", str(executable)])
        subprocess.run(command, check=True, timeout=90)
        result = subprocess.run([str(executable)], check=True, capture_output=True,
                                text=True, timeout=20)
        if result.stdout.strip() != "PASS 15 durable replay store groups":
            raise RuntimeError("unexpected replay test result: " + result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    run()
