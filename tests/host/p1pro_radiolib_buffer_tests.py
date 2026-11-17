"""I-011 bounds regression against a local, unmodified RadioLib 7.1.2 tree."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BACKEND_SHA256 = 'b8537be52a92cef3e94135a44b6cfbfb30732f08408c5389b4bd310573bd8ade'
PINS = {
    "Module.cpp": "92d4156216e5935b86411d56b2e409c86e012e9c60204f1b5a33f75169a75170",
    "modules/SX126x/SX126x.cpp": "13e88359397c3b64dfd52e8f6a5aa475e4304711915e6614fb106d39c0e24b4a",
    "modules/SX126x/SX1262.cpp": "dcab1fd9eb495503a0a3de21029ee3d6f66066e57342eb69ee1a964ad7d422d2",
    "BuildOpt.h": "4dc41d4ea37fa4203b61767082b0e12af723f72cb643975ef5bd632e8dc18615",
}


TARGET_CONFIG_SHA256 = '6eda648d5edf22ca3cdd37bad7b407b2d42fa73dd3a1eab5ccda916cb1d8a76d'

def verify_source(library):
    if json.loads((library / "library.json").read_text())["version"] != "7.1.2":
        raise ValueError("requires pinned RadioLib 7.1.2")
    for name, expected in PINS.items():
        actual = hashlib.sha256((library / "src" / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"unreviewed RadioLib source: {name}")

    sx126x = (library / "src/modules/SX126x/SX126x.cpp").read_text()
    setup = sx126x.split("int16_t SX126x::modSetup(", 1)[1].split("if(!SX126x::findChip", 1)[0]
    for field, value in (("ADDR", "16"), ("CMD", "8")):
        if f"widths[RADIOLIB_MODULE_SPI_WIDTH_{field}] = Module::BITS_{value};" not in setup:
            raise ValueError(f"header width must be configured before the first SPI call: {field}")
    config = ROOT / "firmware/targets/sensecap_solar_p1_pro_smoke/platformio.ini"
    if hashlib.sha256(config.read_bytes()).hexdigest() != TARGET_CONFIG_SHA256:
        raise ValueError("P1-Pro build configuration changed; renew the I-011 bounds review")
    port = ROOT / "firmware/targets/sensecap_solar_p1_pro_smoke/src/p1pro_sx1262_radio_port.cpp"
    source = port.read_text()
    # Bind the proven call graph. Changed platform code needs a new bounds review.
    if hashlib.sha256(port.read_bytes()).hexdigest() != BACKEND_SHA256:
        raise ValueError("P1-Pro backend call graph changed; renew the I-011 bounds review")
    if re.search(r"\bmodule\.spiConfig\b", source):
        raise ValueError("Repeater code must not override the SX1262 SPI widths")
    if "std::uint8_t data[2]{};" not in source or "module.SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS,data,sizeof(data))" not in source:
        raise ValueError("the direct IRQ response must remain bound to its destination size")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--radiolib-root", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    verify_source(args.radiolib_root)
    # Prove an altered invariant is rejected without mutating either source tree.
    original_pin = PINS["Module.cpp"]
    try:
        PINS["Module.cpp"] = "0" * 64
        try:
            verify_source(args.radiolib_root)
        except ValueError:
            pass
        else:
            raise AssertionError("mutated vendor pin was accepted")
    finally:
        PINS["Module.cpp"] = original_pin
    args.output_dir.mkdir()
    exe = args.output_dir / ("buffer-test.exe" if os.name == "nt" else "buffer-test")
    src = args.radiolib_root / "src"
    command = [
        str(args.compiler), "-std=c++17", "-Wall", "-Wextra", "-O2",
        "-fstack-protector-all", "-I", str(src),
        str(ROOT / "tests/host/p1pro_radiolib_buffers.cpp"),
        str(src / "Module.cpp"), str(src / "Hal.cpp"), "-o", str(exe),
    ]
    environment = os.environ.copy()
    environment["PATH"] = str(args.compiler.parent) + os.pathsep + environment["PATH"]
    compile_result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=60)
    result = {"compile_argv": command, "compile_exit": compile_result.returncode,
              "compile_stdout": compile_result.stdout, "compile_stderr": compile_result.stderr,
              "vendor_source_pins": PINS, "backend_sha256": BACKEND_SHA256}
    if compile_result.returncode == 0:
        trial = subprocess.run([str(exe)], capture_output=True, text=True, env=environment, timeout=10)
        result.update(run_exit=trial.returncode, stdout=trial.stdout, stderr=trial.stderr)
    with (args.output_dir / "RESULT.json").open("x", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")
    if result.get("run_exit") != 0 or "PASS: 7 pinned RadioLib command-header paths" not in result.get("stdout", ""):
        raise SystemExit(json.dumps(result, indent=2))
    print("PASS: source-width custody, mutated-pin refusal, 7 actual RadioLib buffer paths")


if __name__ == "__main__":
    main()
