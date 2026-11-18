"""Build the two device parameters host suites from fresh local sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "firmware/components/device_parameters"
STRICT = [
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-fno-exceptions", "-fno-rtti",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fresh_output(path):
    if not path.is_absolute() or ".." in path.parts or path.drive.upper() != "D:":
        raise ValueError("Use an absolute output directory on D: below this worktree's build folder")
    for ancestor in (path, *path.parents):
        try:
            info = ancestor.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("Indirect output directory refused")
    target = path.resolve()
    if not target.is_relative_to(ROOT / "build") or target == ROOT / "build":
        raise ValueError("Output must be below this worktree's build folder")
    target.mkdir(parents=True, exist_ok=False)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--cxx", type=Path, default=Path(r"C:\msys64\ucrt64\bin\g++.exe"))
    args = parser.parse_args()
    if not args.cxx.is_file():
        parser.error("The selected native C++ compiler does not exist")
    output = fresh_output(args.output_root)
    compiler = args.cxx.resolve()
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    env["TEMP"] = env["TMP"] = str(output)
    sources = {
        "codec": [
            COMPONENT / "src/device_parameters_codec.cpp",
            ROOT / "tests/host/device_parameters_codec_tests.cpp",
        ],
        "collector": [
            COMPONENT / "src/device_parameters_codec.cpp",
            COMPONENT / "src/device_parameters_collector.cpp",
            ROOT / "tests/host/device_parameters_collector_tests.cpp",
        ],
    }
    closure = {COMPONENT / "include/opentrail/device_parameters.hpp", Path(__file__).resolve()}
    closure.update(path for files in sources.values() for path in files)
    pins = {str(path.relative_to(ROOT)): digest(path) for path in sorted(closure)}
    report = {
        "schema": "MESHTRAIL-DEVICE-PARAMETERS-HOST-1",
        "result": "failed",
        "source_sha256": pins,
        "compiler": str(compiler),
        "compiler_sha256": digest(compiler),
        "commands": [],
        "suites": {},
    }
    log_path = output / "commands.log"
    report_path = output / "result.json"

    def run(argv):
        start = time.monotonic()
        result = subprocess.run(
            [str(value) for value in argv], cwd=ROOT, env=env,
            capture_output=True, text=True, encoding="utf-8", errors="strict",
            timeout=180,
        )
        record = {
            "argv": [str(value) for value in argv],
            "cwd": str(ROOT),
            "exit_code": result.returncode,
            "elapsed_seconds": round(time.monotonic() - start, 3),
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
        report["commands"].append(record)
        with log_path.open("a", encoding="utf-8", newline="\n") as log:
            log.write(json.dumps(record, ensure_ascii=True) + "\n")
        if result.returncode != 0:
            raise RuntimeError("Command failed, see " + str(log_path))
        return result

    try:
        version = run([compiler, "--version"])
        report["compiler_version"] = version.stdout.splitlines()[0]
        for name, files in sources.items():
            exe = output / ("device_parameters_" + name + "_tests.exe")
            run([compiler, *STRICT, "-I", COMPONENT / "include", *files, "-o", exe])
            result = run([exe])
            if "PASS" not in result.stdout:
                raise RuntimeError("Missing test success for " + name)
            report["suites"][name] = {
                "result": "passed", "binary_sha256": digest(exe),
                "stdout": result.stdout, "stderr": result.stderr,
            }
            print(result.stdout.strip(), flush=True)
        for path in closure:
            if digest(path) != pins[str(path.relative_to(ROOT))]:
                raise RuntimeError("Source changed during verification")
        report["result"] = "passed"
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
