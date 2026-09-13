"""Actual OT-200 application and console integration; no device I/O.

Uses repository SDK/NVS seams and a checksum-pinned temporary libsodium
tree acquired from the admitted public archive. SDK delays model observation cutoffs, not reset or torn-write behavior.
Every run requires a fresh output directory and retains commands and failures.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from security_policy_lifecycle import dependencies

ROOT = Path(__file__).resolve().parents[2]
BIN = Path("C:/msys64/ucrt64/bin")
PINNED_UCRT = os.name == "nt" and (BIN / "g++.exe").is_file() and (BIN / "gcc.exe").is_file()
CPP = BIN / "g++.exe" if PINNED_UCRT else Path(shutil.which("g++") or "missing-g++")
CC = BIN / "gcc.exe" if PINNED_UCRT else Path(shutil.which("gcc") or "missing-gcc")
ADAPTER = ROOT / "firmware/targets/heltec_v4_security_eval/main/noise_adapter"
TARGET = ROOT / "firmware/targets/heltec_v4_security_sync_diag/main"
POLICY = ROOT / "firmware/targets/heltec_v4_security_policy_eval/main"
CHECKSUM_SHA = "5e3983c5496a3cffba3d013c70991b18cda6c345655fff883fb0e14dfa09e582"
ADAPTER_SHA = "b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b"
HEADER_SHA = "b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45"
PRIMITIVES = """crypto_hash/sha256/hash_sha256.c crypto_hash/sha256/cp/hash_sha256_cp.c
crypto_auth/hmacsha256/auth_hmacsha256.c crypto_kdf/hkdf/kdf_hkdf_sha256.c
crypto_aead/chacha20poly1305/aead_chacha20poly1305.c crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
crypto_onetimeauth/poly1305/donna/poly1305_donna.c crypto_stream/chacha20/stream_chacha20.c
crypto_stream/chacha20/ref/chacha20_ref.c crypto_verify/verify.c
crypto_scalarmult/curve25519/scalarmult_curve25519.c crypto_scalarmult/curve25519/ref10/x25519_ref10.c
crypto_core/ed25519/ref10/ed25519_ref10.c sodium/utils.c sodium/core.c
crypto_sign/crypto_sign.c crypto_sign/ed25519/sign_ed25519.c crypto_sign/ed25519/ref10/keypair.c
crypto_sign/ed25519/ref10/sign.c crypto_sign/ed25519/ref10/open.c
crypto_hash/sha512/hash_sha512.c crypto_hash/sha512/cp/hash_sha512_cp.c""".split()
SCENARIOS = (
    "normal", "prologue", "malformed_then_valid", "hex_then_valid", "budget193", "bulk288",
    "malformed_only", "invalid_hex_only", "no_input", "partial", "frame_timeout",
    "ready_health_failure", "input_fault", "begin_fail", "duplicate", "stale_then_fresh", "first_read_before_epoch",
    "startup_costs", "slow_startup", "slow_waiting", "slow_detail_commit", "slow_detail_verify",
    "slow_input", "slow_verify", "slow_send_return", "poll_cap", "late_first",
    "preexisting", "nvs_fail", "open_fail", "create_fail", "install_fail",
    "start_fail", "sodium_fail", "evaluate_fail", "namespace_fail", "stop_fail",
    "entropy_fault", "send_fail",
)
OPERATIONS = ("set", "commit_before", "commit_applied", "get", "get_corrupt")


def need(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    need(sys.dont_write_bytecode, "Run Python with -B")
    need(sys.flags.optimize == 0, "optimized_python_refused")
    need(args.output_root.is_absolute(), "output_root_must_be_absolute")
    output = args.output_root.resolve()
    allowed = (ROOT / ".private/ot200-integration/lifecycle").resolve()
    need(output.is_relative_to(allowed) and output != allowed, "output_root_outside_lifecycle")
    need(not output.exists(), "fresh_output_root_required")
    need(CPP.is_file() and CC.is_file(), "pinned_compiler_missing")
    output.mkdir(parents=True)
    env = dict(os.environ)
    env.pop("PYTHONOPTIMIZE", None)
    env["PATH"] = str(CPP.resolve().parent) + os.pathsep + str(CC.resolve().parent) + os.pathsep + env.get("PATH", "")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    result = {"status": "running", "hardware": False, "network": False,
              "source_root": str(ROOT), "output_root": str(output),
              "commands": [], "cases": [], "capture_checks": [], "source_hashes": {},
              "limits": ["SDK/clock/NVS simulation, not physical reset or torn flash",
                         "host cutoff does not prove persistence completed",
                         "crypto initialization and entropy use controlled seams"]}

    def save():
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    def run(command, label, timeout=120):
        row = {"label": label, "argv": list(map(str, command)), "cwd": str(output),
               "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
        result["commands"].append(row)
        row["log"] = f"{len(result['commands']):03d}-{label}.log"
        save()
        started = time.monotonic()
        try:
            completed = subprocess.run(row["argv"], cwd=output, env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       timeout=timeout, check=False)
            (output / row["log"]).write_bytes(completed.stdout)
            row["exit_code"] = completed.returncode
            row["duration_seconds"] = round(time.monotonic() - started, 6)
            save()
            need(completed.returncode == 0, "command_failed: " + label)
            return completed.stdout
        except subprocess.TimeoutExpired as error:
            (output / row["log"]).write_bytes(error.stdout or b"")
            row["failure"] = "timeout"
            row["duration_seconds"] = round(time.monotonic() - started, 6)
            save()
            raise
        except OSError as error:
            row["failure"] = "execution_environment: " + str(error)
            row["duration_seconds"] = round(time.monotonic() - started, 6)
            save()
            raise

    def pin(path):
        result["source_hashes"][str(path)] = digest(path)

    try:
        result["network"] = True
        component = dependencies.acquire(output / "managed-component")
        source_root = component / "libsodium/src/libsodium"
        pin(Path(dependencies.__file__))
        for compiler in (CC, CPP):
            version = run([compiler, "--version"], compiler.stem + "-version").decode("utf-8").splitlines()[0]
            if PINNED_UCRT:
                need("16.1.0" in version, "pinned_compiler_version_changed")
            result[compiler.name] = {"sha256": digest(compiler), "version": version}
        inventory = component / "CHECKSUMS.json"
        need(digest(inventory) == CHECKSUM_SHA, "source_inventory_changed")
        files = json.loads(inventory.read_bytes())["files"]
        need(len(files) == 731, "source_inventory_count")
        seen = set()
        for entry in files:
            path = (component / entry["path"]).resolve()
            need(path.is_relative_to(component.resolve()) and path not in seen, "source_inventory_path")
            seen.add(path)
            need(path.stat().st_size == entry["size"] and digest(path) == entry["hash"], "local_libsodium_source_changed")
        need(digest(ADAPTER / "noise_xk_libsodium.c") == ADAPTER_SHA, "adapter_changed")
        need(digest(ADAPTER / "noise_xk_libsodium.h") == HEADER_SHA, "adapter_header_changed")
        result["verified_local_dependency_files"] = len(files)
        stubs = output / "stubs"
        stubs.mkdir()
        previous = ROOT / "tests/host/security_policy_lifecycle"
        inherited = (previous / "harness.cpp").read_bytes()
        marker = b"int main(int argc,char** argv)"
        need(inherited.count(marker) == 1, "external_fixture_boundary_changed")
        external_fixture = inherited.split(marker)[0]
        event_seam = b"void event(const char* name){events.emplace_back(name);}"
        need(external_fixture.count(event_seam) == 1, "external_event_fixture_changed")
        external_fixture = b'extern "C" void lifecycle_sdk_event(const char*);\n' + external_fixture.replace(
            event_seam, b"void event(const char* name){events.emplace_back(name);lifecycle_sdk_event(name);}")
        stub_data = {
            "external_fixture.hpp": external_fixture,
            "entropy_runtime.hpp": (previous / "entropy_runtime.hpp").read_bytes(),
            "nvs.h": (ROOT / "tests/host/fixtures/security_eval_nvs/nvs.h").read_bytes() +
                     b"\nconstexpr int NVS_READONLY=0;\nesp_err_t nvs_set_u64(nvs_handle_t,const char*,std::uint64_t);\nesp_err_t nvs_get_u64(nvs_handle_t,const char*,std::uint64_t*);\n",
            "nvs_flash.h": b'#pragma once\n#include "nvs.h"\nint nvs_flash_init();\n',
            "esp_timer.h": b'#pragma once\n#include <cstdint>\nstd::int64_t esp_timer_get_time();\n',
            "freertos/FreeRTOS.h": b"#pragma once\n",
            "freertos/task.h": b"#pragma once\nvoid vTaskDelay(unsigned);\n",
        }
        for name, raw in stub_data.items():
            path = stubs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        generated = output / "include/sodium"
        generated.mkdir(parents=True)
        version = (source_root / "include/sodium/version.h.in").read_text()
        for key, value in {"@VERSION@": "1.0.22", "@SODIUM_LIBRARY_VERSION_MAJOR@": "26",
                           "@SODIUM_LIBRARY_VERSION_MINOR@": "4", "@SODIUM_LIBRARY_MINIMAL_DEF@": "#define SODIUM_LIBRARY_MINIMAL 1"}.items():
            version = version.replace(key, value)
        need("@" not in version, "version_template_changed")
        (generated / "version.h").write_text(version, encoding="utf-8")
        includes = [output, stubs, output / "include", source_root / "include", source_root / "include/sodium", ADAPTER,
                    TARGET, POLICY, ROOT / "firmware/components/security_diagnostics/include",
                    ROOT / "firmware/components/security_evaluation/include",
                    ROOT / "firmware/components/security/include", ROOT / "firmware/components/persistence/include"]
        flags = ["-O2", "-Wall", "-Wextra", "-DSODIUM_STATIC", "-DCONFIGURED=1", "-DNATIVE_LITTLE_ENDIAN=1",
                 "-ffunction-sections", "-fdata-sections"]
        for path in includes:
            flags.extend(["-I", str(path)])
        objects = []
        inputs = [ADAPTER / "noise_xk_libsodium.c"] + [source_root / name for name in PRIMITIVES]
        for i, source in enumerate(inputs):
            obj = output / f"crypto-{i}.o"
            pin(source)
            # Match the proven native base-C recipe: PE unwind roots otherwise
            # retain unused sodium_init paths despite the explicit wrapper seam.
            native_unwind = ["-fno-asynchronous-unwind-tables", "-fno-unwind-tables"] if i <= 15 else []
            run([CC, "-std=c11", *flags, *native_unwind, *(["-Werror"] if i == 0 else []), "-c", source, "-o", obj], f"compile-crypto-{i}")
            objects.append(obj)
        common = [ROOT / "firmware/components/persistence/src/persistent_storage_kv.cpp",
                  ROOT / "firmware/components/persistence/src/outbound_counter_lease_store.cpp",
                  ROOT / "firmware/components/security/src/aead_nonce.cpp"]
        target = TARGET / "app_main.cpp"
        harness = ROOT / "tests/host/security_policy_sync_lifecycle_tests.cpp"
        for source in [target, POLICY / "app_main.cpp", POLICY / "policy_console.cpp",
                       TARGET / "stage_store.hpp", TARGET / "input_control_loop.hpp",
                       ROOT / "firmware/components/security_diagnostics/include/opentrail/security_sync_record.hpp",
                       ROOT / "firmware/components/security_evaluation/include/opentrail/evaluation_control_synchronizing.hpp",
                       previous / "harness.cpp", previous / "entropy_runtime.hpp", harness, Path(__file__), *common]:
            pin(source)
        exe = output / "actual-app.exe"
        link = [CPP, "-std=c++17", "-Werror", *flags, "-Wl,--gc-sections", "-Wl,--wrap=sodium_init"]
        platform_libraries = ["-ladvapi32"] if os.name == "nt" else []
        run([*link, target, harness, *common, *objects, *platform_libraries, "-o", exe], "compile-actual-app")

        # Inspected pure injected-byte capture primitive; no port/runtime import.
        sys.path.insert(0, str(ROOT / "tools"))
        from security_policy_capture import CaptureError, capture
        pin(ROOT / "tools/security_policy_capture.py")

        def crosscheck(wire, arrival, label, expected=None, extra=None):
            pending = [wire] + ([] if extra is None else [extra])
            clock = [0.0]

            def read(size, remaining):
                need(size == 129 and remaining > 0, "capture_read_contract")
                if pending:
                    clock[0] = max(clock[0] + 0.000001, arrival)
                    return pending.pop(0)
                clock[0] = 30.0
                return b""

            observed = "accepted"
            try:
                captured = capture(read, lambda: clock[0], "0123456789abcdef0123456789abcdef", 30.0)
                need(captured == wire.replace(b"\r\n", b"\n"), "capture_changed_receipt")
            except CaptureError as error:
                observed = str(error)
            need(observed == (expected or "accepted"), "capture_crosscheck_failed: " + label + ": " + observed)
            result["capture_checks"].append({"label": label, "arrival_seconds": arrival, "result": observed})

        def check_case(executable, case, prefix):
            raw = run([executable, *case], prefix + "-" + "-".join(case), timeout=20)
            lines = raw.decode("ascii").splitlines()
            need(lines and lines[0].startswith("PASS sync lifecycle "), "missing_case_pass_marker")
            result["cases"].append({"binary": prefix, "argv": case, "output": lines})
            wire = raw.partition(b"\n")[2]
            if wire:
                values = dict(word.split("=", 1) for word in lines[0].split() if "=" in word)
                arrival = int(values["receipt_us"]) / 1000000
                expected = "receipt_invalid" if case[0] == "stale_then_fresh" else "late_output" if arrival >= 30 else None
                crosscheck(wire, arrival, prefix + "-" + "-".join(case), expected)
                if case[0] == "actual_normal":
                    crosscheck(wire, arrival, "duplicate-receipt", "trailing_output", wire)
                    crosscheck(wire, arrival, "trailing-receipt-bytes", "trailing_output", b"extra")
                    crosscheck(wire.replace(b"0123456789abcdef0123456789abcdef", b"0" * 32), arrival, "wrong-challenge", "receipt_invalid")
                    crosscheck(wire, 30.0, "exact-host-deadline", "late_output")
            return lines

        for case in [[name] for name in SCENARIOS] + [["failure", str(stage), operation]
                  for stage in range(1, 10) for operation in OPERATIONS]:
            lines = check_case(exe, case, "app")
            if case[0] == "stale_then_fresh":
                # Exact host comparison keeps the current challenge distinct from
                # device parser success for a valid but stale queued command.
                wire = lines[-1]
                need(wire == "SEC_EVAL1 ot187-policy-v0 fedcba9876543210fedcba9876543210 pass", "stale_receipt_missing")
                need(wire != "SEC_EVAL1 ot187-policy-v0 0123456789abcdef0123456789abcdef pass", "host_challenge_must_remain_distinct")

        # This inspected existing helper generates SDK seams and directly includes
        # the existing policy_console.cpp; importing it launches no tests/devices.
        import security_policy_console_lifecycle_tests as console
        for path in (Path(console.__file__), ROOT / "tests/host/receipt_console_binding_tests.py",
                     ROOT / "tests/host/usb_console_binding_tests.py"):
            pin(path)
        sdk = output / "console-sdk"
        for name, raw in console.console_stubs().items():
            path = sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(raw, encoding="utf-8")
        translation = console.console_translation_unit()
        clock_definition = "std::int64_t esp_timer_get_time() { return ++clock_value; }"
        need(translation.count(clock_definition) == 1, "console_clock_fixture_changed")
        translation = translation.replace(clock_definition,
            'extern "C" std::uint64_t lifecycle_shared_time();\n'
            'std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(lifecycle_shared_time()); }')
        renames = "".join(f"#define {name} baseline_{name}\n" for name in
            ("ot_console_install", "ot_console_healthy", "ot_console_session_started", "ot_console_begin_session", "ot_policy_read", "ot_policy_send", "esp_timer_get_time"))
        translation = renames + translation + '\nextern "C" void lifecycle_console_send_fault(){fault_write=true;}\n' + \
            'extern "C" std::size_t lifecycle_console_remaining(){return rx.size();}\n'
        translation_path = output / "actual-console.cpp"
        translation_path.write_text(translation, encoding="utf-8")
        composed = output / "actual-console.exe"
        run([*link, "-DACTUAL_CONSOLE", "-I", sdk,
             "-I", ROOT / "firmware/components/diagnostics/include", target, harness, translation_path,
             *common, *objects, *platform_libraries, "-o", composed], "compile-actual-console")
        for scenario in ("actual_normal", "actual_prologue", "actual_malformed_then_valid",
                         "actual_duplicate", "actual_fault", "actual_input_fault", "actual_send_fault"):
            check_case(composed, [scenario], "console")
        result["executables"] = [{"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path)}
                                 for path in (exe, composed)]
        # Freeze source identity throughout this focused suite.
        for path, sha in result["source_hashes"].items():
            need(digest(Path(path)) == sha, "source_changed_during_validation: " + path)
        result["status"] = "passed"
        result["case_count"] = len(result["cases"])
        save()
        print(f"PASS {result['case_count']} actual sync lifecycle scenarios and {len(result['capture_checks'])} strict capture checks; SDK/clock/NVS simulated; hardware untested")
        return 0
    except Exception as error:
        result["status"] = "failed"
        result["failure"] = type(error).__name__ + ": " + str(error)
        save()
        print(result["failure"], file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
