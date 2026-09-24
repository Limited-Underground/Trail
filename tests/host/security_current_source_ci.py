"""Current-source CI regression matrix; never an OT206 historical-proof replay.

Acquires the admitted public dependency into fresh output and shares its scalar
objects only within this invocation. SDK/entropy/storage seams are simulated.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import sys
from types import SimpleNamespace
import security_policy_invitation_lifecycle_tests as frozen
from security_policy_lifecycle import dependencies

ROOT = Path(__file__).resolve().parents[2]


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def pin(path):
    return {"bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def run(output):
    need(sys.dont_write_bytecode and sys.flags.utf8_mode and not sys.flags.optimize,
         "use_python_-X_utf8_-B_without_optimization")
    need(output.is_absolute() and ".." not in output.parts and
         any(output.is_relative_to(ROOT / part) for part in (".private", "build")),
         "fresh_absolute_output_inside_worktree_required")
    for path in (output, *output.parents):
        need(not path.is_symlink() and not getattr(path, "is_junction", lambda: False)(),
             "indirect_output_refused")
    output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "OT220-CURRENT-SOURCE-CI-1", "result": "failed",
              "hardware": False, "network": True, "historical_proof_replay": False,
              "suites": {}, "limits": ["SDK, entropy and storage seams simulated"]}
    commands = frozen.Commands(output)
    try:
        configured = os.environ.get("OPENTRAIL_MSYS2_ROOT")
        compiler_dir = Path(configured) / "ucrt64/bin" if configured else None
        def compiler(name):
            if compiler_dir is not None:
                path = compiler_dir / (name + ".exe")
            else:
                path = Path(shutil.which(name) or "missing-" + name)
            need(path.is_file(), "native_compiler_missing: " + str(path))
            return path.resolve()
        cc, cxx = compiler("gcc"), compiler("g++")
        env = dict(os.environ)
        env.pop("PYTHONOPTIMIZE", None)
        env["PATH"] = str(cc.parent) + os.pathsep + str(cxx.parent) + os.pathsep + env.get("PATH", "")
        result["compilers"] = {str(p): {**pin(p), "version": commands.check_output(
            [p, "--version"], env=env).splitlines()[0]} for p in (cc, cxx)}
        # Pin the current source closure before execution, without treating it as
        # equal to any historical machine/compiler proof.
        roots = [ROOT / "firmware/components", ROOT / "tests/host"]
        target_names = ("heltec_v4_security_eval", "heltec_v4_invitation_eval",
                        "heltec_v4_confirmation_eval", "heltec_v4_bench",
                        "heltec_v4_security_receipt_sync", "heltec_v4_security_policy_eval",
                        "heltec_v4_pair_eval", "heltec_v4_pair_radio_eval", "heltec_v4_enrolled_eval")
        roots += [ROOT / "firmware/targets" / name / "main" for name in target_names]
        inputs = {p for directory in roots for p in directory.rglob("*")
                  if p.is_file() and p.suffix in (".py", ".cpp", ".c", ".hpp", ".h", ".json")}
        helper = ROOT / "tools/noise_xk_independent_interop.py"
        inputs.add(helper)
        inputs.update(ROOT / "tools" / name for name in (
            "pair_bench_bridge.py", "pair_confirmation_trial.py", "pair_trial_operator.py", "pair_radio_driver_source.py",
            "enrolled_pair_bridge.py", "enrolled_trace_schema.py", "enrolled_confirmation_trial.py", "enrolled_trial_operator.py"))
        before = {str(p): pin(p) for p in inputs}
        component = dependencies.acquire(output / "managed-component")
        result["dependency"] = {"archive_sha256": dependencies.ARCHIVE_SHA,
                                "checksum_sha256": dependencies.CHECKSUM_SHA,
                                "manifest_sha256": dependencies.MANIFEST_SHA,
                                "verified_files": 733}
        spec = importlib.util.spec_from_file_location("current_ci_crypto", helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.COMPONENT, module.SOURCE = component, component / "libsodium"
        module.SUCCESSOR = ROOT / "firmware/targets/heltec_v4_security_eval/main/noise_adapter"
        module.SUCCESSOR_SHA = "b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b"
        build = output / "scalar-build"
        native_subprocess = dependencies.subprocess
        def logged(command, **kwargs):
            kwargs.setdefault("env", env)
            return commands.run(command, **kwargs)
        try:
            dependencies.subprocess = SimpleNamespace(run=logged)
            dependencies.native_probe(module, build, cc)
        finally:
            dependencies.subprocess = native_subprocess
        result["scalar_control"] = {"groups": 26, "result": "passed",
            "binary": pin(build / "independent-probe.exe")}
        flags = ["-O2", "-Wall", "-Wextra", "-DSODIUM_STATIC", "-DCONFIGURED=1",
                 "-DNATIVE_LITTLE_ENDIAN=1", "-fno-asynchronous-unwind-tables",
                 "-fno-unwind-tables", "-ffunction-sections", "-fdata-sections"]
        shared_includes = [build / "include", module.SOURCE / "src/libsodium/include",
                           module.SOURCE / "src/libsodium/include/sodium", module.SUCCESSOR]
        def includes(paths):
            return [item for path in paths for item in ("-I", str(path))]
        def compile_source(source, obj, more=(), c=False, defines=()):
            language = ["-std=c11"] if c else ["-std=c++17", "-fno-exceptions", "-fno-rtti", "-Werror"]
            commands.run([cc if c else cxx, *language, *flags, *defines,
                *includes([*shared_includes, *more]), "-MMD", "-MF", obj.with_suffix(".d"),
                "-c", source, "-o", obj], env=env, check=True)
        crypto_objects = [build / f"source-{i}.o" for i in range(1, 17)]
        for i, name in enumerate(frozen.EXTRA):
            obj = output / f"signing-{i}.o"
            compile_source(module.SOURCE / "src/libsodium" / name, obj, c=True)
            crypto_objects.append(obj)
        base = [ROOT / "firmware/components" / part for part in
                ("security_evaluation/include", "security/include", "security/test_support",
                 "persistence/include", "persistence/test_support")]
        def suite(name, sources, extra_includes, wraps, marker, cases=None, defines=()):
            directory = output / name
            directory.mkdir()
            objects = list(crypto_objects)
            for i, source in enumerate(sources):
                obj = directory / f"unit-{i}.o"
                compile_source(source, obj, extra_includes, defines=defines)
                objects.append(obj)
            exe = directory / (name + ".exe")
            commands.run([cxx, "-Wl,--gc-sections", *["-Wl,--wrap=" + value for value in wraps],
                          *objects, "-o", exe], env=env, check=True)
            outputs = []
            for case in cases or [None]:
                completed = commands.run([exe, *([case] if case else [])], env=env, check=True, timeout=60)
                need(completed.stdout.startswith("PASS ") and marker in completed.stdout,
                     "missing_suite_pass: " + name)
                outputs.append(completed.stdout.strip())
            summary = "PASS " + str(len(cases)) + marker if cases else outputs[0]
            result["suites"][name] = {"output": summary, "binary": pin(exe)}
            if cases:
                result["suites"][name]["cases"] = dict(zip(cases, outputs))
            print(summary, flush=True)
            return exe
        common = [ROOT / name for name in frozen.COMMON]
        for name in frozen.SUITES:
            suite(name, [*common, ROOT / "tests/host" / (name + ".cpp")], base, (), " groups\n")
        suite("security_confirmation_owner_tests", [*common, ROOT / "tests/host/security_confirmation_owner_tests.cpp"],
              base, (), " actual confirmation owner groups")
        suite("security_handshake_endpoint_tests", [*common, ROOT / "tests/host/security_handshake_endpoint_tests.cpp"],
              base, (), " independent handshake endpoint groups")
        suite("security_independent_invitation_tests", [*common, ROOT / "tests/host/security_independent_invitation_tests.cpp"],
              base, (), " independent invitation groups")
        suite("security_independent_endpoint_tests", [*common, ROOT / "tests/host/security_independent_endpoint_tests.cpp"],
              base, (), " independent provisioned endpoint groups")
        suite("peer_activation_store_tests", [*common, ROOT / "tests/host/peer_activation_store_tests.cpp"],
              base, (), " peer activation store groups")
        suite("peer_membership_store_tests", [*common, ROOT / "tests/host/peer_membership_store_tests.cpp"],
              base, (), " peer membership store groups")
        suite("enrolled_peer_endpoint_tests", [*common, ROOT / "tests/host/enrolled_peer_endpoint_tests.cpp"],
              base, (), " enrolled peer endpoint groups")
        suite("enrollment_identity_binding_tests", [*common, ROOT / "tests/host/enrollment_identity_binding_tests.cpp"],
              base, (), " enrollment identity binding groups")
        suite("enrollment_fingerprint_review_tests", [*common, ROOT / "tests/host/enrollment_fingerprint_review_tests.cpp"],
              base, (), " enrollment fingerprint review groups")
        suite("enrollment_review_layout_tests", [*common, ROOT / "tests/host/enrollment_review_layout_tests.cpp"],
              base, (), " enrollment review layout groups")
        suite("enrollment_review_device_port_tests", [*common, ROOT / "tests/host/enrollment_review_device_port_tests.cpp"],
              base, (), " groups passed")
        bench = ROOT / "firmware/targets/heltec_v4_bench/main"
        suite("heltec_enrollment_input_arbiter_tests", [*common,
              *[bench / name for name in ("heltec_enrollment_input_arbiter.cpp", "heltec_v4_factory_reset_input.cpp",
                "heltec_startup_display.cpp", "heltec_v4_oled.cpp", "heltec_oled_presentation.cpp")],
              *[ROOT / "firmware/components" / name for name in ("companion/src/companion_factory_reset_gesture.cpp",
                "ui/src/compact_status_footer.cpp", "ui/src/oled_presentation.cpp", "time/src/oled_clock.cpp")],
              ROOT / "tests/host/heltec_enrollment_input_arbiter_tests.cpp"],
              [*base, bench, ROOT / "tests/host/fixtures/heltec_oled",
               *[ROOT / "firmware/components" / name / "include" for name in ("companion", "ui", "time", "protocol", "radio")]],
              (), " actual enrollment input arbiter groups")
        suite("enrollment_commit_coordinator_tests", [*common, ROOT / "tests/host/enrollment_commit_coordinator_tests.cpp"],
              base, (), " enrollment commit journal groups")
        suite("enrollment_identity_store_tests", [*common, ROOT / "tests/host/enrollment_identity_store_tests.cpp"],
              base, (), " identity store groups")
        suite("heltec_enrollment_identity_owner_tests", [*common,
              ROOT / "tests/host/heltec_enrollment_identity_owner_tests.cpp",
              *[ROOT / "firmware/targets/heltec_v4_bench/main" / name for name in
                ("heltec_enrollment_identity_owner.cpp", "enrollment_identity_nvs_storage.cpp", "heltec_v4_factory_reset_storage.cpp")]],
              [*base, ROOT / "tests/host/fixtures/identity_nvs", ROOT / "firmware/targets/heltec_v4_bench/main",
               *[ROOT / "firmware/components" / name / "include" for name in ("companion", "radio", "protocol", "location", "time")]],
              (), " actual retained identity owner groups")
        suite("enrollment_possession_proof_tests", [*common, ROOT / "tests/host/enrollment_possession_proof_tests.cpp"],
              base, (), " enrollment possession:")
        suite("product_enrollment_activation_tests", [*common, ROOT / "tests/host/product_enrollment_activation_tests.cpp"],
              base, (), " product enrollment activation groups")
        suite("enrollment_binding_store_tests", [*common, ROOT / "tests/host/enrollment_binding_store_tests.cpp"],
              base, (), " enrollment binding store groups")
        suite("enrollment_retained_state_tests", [*common, ROOT / "tests/host/enrollment_retained_state_tests.cpp"],
              base, (), " retained state groups")
        suite("product_enrollment_rekey_tests", [*common, ROOT / "tests/host/product_enrollment_rekey_tests.cpp"],
              base, (), " product enrollment rekey groups")
        suite("product_enrollment_authority_tests", [*common, ROOT / "tests/host/product_enrollment_authority_tests.cpp"],
              base, (), " product enrollment authority groups")
        suite("enrollment_evidence_store_tests", [*common, ROOT / "tests/host/enrollment_evidence_store_tests.cpp"],
              base, (), " enrollment evidence store groups")
        suite("provisioned_peer_endpoint_tests", [*common, ROOT / "tests/host/provisioned_peer_endpoint_tests.cpp"],
              base, (), " provisioned peer endpoint groups")
        suite("evaluation_storage_bank_tests", [ROOT / "tests/host/evaluation_storage_bank_tests.cpp"],
              base, (), " evaluation storage bank groups")
        enrolled_includes = [*base, ROOT / "firmware/components/companion/include",
            ROOT / "firmware/components/protocol/include", ROOT / "firmware/components/radio/include",
            ROOT / "firmware/components/radio/test_support"]
        radio_sources = [ROOT / "firmware/components/protocol/src/packet_codec.cpp",
                         ROOT / "firmware/components/radio/test_support/fake_radio_transport.cpp"]
        suite("session_generation_storage_tests", [*common, ROOT / "tests/host/session_generation_storage_tests.cpp"],
              base, (), " session generation storage groups")
        suite("enrolled_peer_transport_tests", [*common, *radio_sources, ROOT / "tests/host/enrolled_peer_transport_tests.cpp"],
              enrolled_includes, (), " enrolled peer transport groups")
        nvs_includes = [*base, ROOT / "tests/host/fixtures/security_eval_nvs",
                        ROOT / "firmware/targets/heltec_v4_enrolled_eval/main"]
        suite("enrolled_nvs_backend_tests", [*common, ROOT / "tests/host/enrolled_nvs_backend_tests.cpp"],
              nvs_includes, (), " enrolled NVS backend groups")
        suite("enrolled_nvs_session_tests", [*common, ROOT / "tests/host/enrolled_nvs_session_tests.cpp"],
              nvs_includes, (), " enrolled NVS session groups")
        suite("enrolled_radio_driver_tests", [ROOT / "tests/host/enrolled_radio_driver_tests.cpp",
              ROOT / "firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.cpp"],
              [ROOT / "tests/host/pair_radio_driver_stubs", *nvs_includes,
               ROOT / "firmware/components/radio/include"], (), " enrolled radio driver groups")
        diagnostic_exe = suite("enrolled_diagnostics_tests", [ROOT / "tests/host/enrolled_diagnostics_tests.cpp",
              ROOT / "firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.cpp"],
              [ROOT / "tests/host/pair_radio_driver_stubs", *nvs_includes,
               ROOT / "firmware/components/radio/include"], (), " enrolled diagnostics groups")
        suite("companion_status_bridge_tests", [*common, ROOT / "tests/host/companion_status_bridge_tests.cpp"],
              enrolled_includes, (), " companion status bridge groups")
        enrolled_exe = suite("enrolled_bench_session_tests", [*common, *radio_sources,
              ROOT / "tests/host/enrolled_bench_session_tests.cpp"], enrolled_includes,
              (), " enrolled bench session groups")
        suite("enrolled_completion_composed_tests", [*common, *radio_sources,
              ROOT / "tests/host/enrolled_completion_composed_tests.cpp",
              ROOT / "firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.cpp"],
              [ROOT / "tests/host/pair_radio_driver_stubs", *nvs_includes, *enrolled_includes],
              (), " enrolled completion composition groups")
        suite("security_peer_traffic_tests", [*common, ROOT / "tests/host/security_peer_traffic_tests.cpp"],
              base, (), " peer traffic groups")
        suite("security_endpoint_record_tests", [*common, ROOT / "tests/host/security_endpoint_record_tests.cpp"],
              base, (), " guarded endpoint record groups")
        suite("security_independent_transport_tests",
              [*common, ROOT / "tests/host/security_independent_transport_tests.cpp",
               ROOT / "firmware/components/protocol/src/packet_codec.cpp",
               ROOT / "firmware/components/radio/test_support/fake_radio_transport.cpp"],
              [*base, ROOT / "firmware/components/radio/include",
               ROOT / "firmware/components/radio/test_support", ROOT / "firmware/components/protocol/include"],
              (), " independent transport groups")
        suite("pair_radio_session_tests",
              [*common, ROOT / "tests/host/pair_radio_session_tests.cpp",
               ROOT / "firmware/components/protocol/src/packet_codec.cpp",
               ROOT / "firmware/components/radio/test_support/fake_radio_transport.cpp"],
              [*base, ROOT / "firmware/components/radio/include",
               ROOT / "firmware/components/radio/test_support", ROOT / "firmware/components/protocol/include"],
              (), " radio pair session groups")
        suite("pair_radio_driver_tests",
              [ROOT / "tests/host/pair_radio_driver_tests.cpp",
               ROOT / "firmware/targets/heltec_v4_pair_radio_eval/main/pair_radio_driver.cpp"],
              [ROOT / "tests/host/pair_radio_driver_stubs",
               ROOT / "firmware/targets/heltec_v4_pair_radio_eval/main",
               ROOT / "firmware/components/radio/include", *base],
              (), " radio driver groups")
        pair_exe = suite("pair_bench_session_tests", [*common, ROOT / "tests/host/pair_bench_session_tests.cpp"],
                         base, (), " pair bench session groups")
        pair_target = ROOT / "firmware/targets/heltec_v4_pair_eval/main"
        entropy_target = ROOT / "firmware/targets/heltec_v4_security_eval/main"
        bench_target = ROOT / "firmware/targets/heltec_v4_bench/main"
        startup_sources = [*common, pair_target / "app_main.cpp", entropy_target / "entropy_runtime.cpp",
            bench_target / "heltec_v4_secure_random.cpp",
            ROOT / "firmware/components/security/src/serialized_secure_random.cpp",
            ROOT / "firmware/components/persistence/src/persistent_storage_kv.cpp",
            ROOT / "tests/host/pair_target_startup_tests.cpp"]
        startup_cases = ("happy usb_install nvs_init display_init button_init boot_open role_open tx_open rx_open "
            "tx_blank_read tx_blank_retained rx_blank_read rx_blank_retained entropy_not_idle entropy_init "
            "entropy_enable entropy_readiness sodium_init authority ready_send session_tick usb_read "
            "session_command invalid_control line_overflow prehello_noise prehello_init prehello_budget prehello_diag_budget prehello_boundary").split()
        suite("pair_target_startup_tests", startup_sources,
              [ROOT / "tests/host/pair_target_startup_stubs", pair_target, entropy_target, bench_target, *base],
              ["sodium_init"], " actual pair startup groups", startup_cases)
        suite("pair_radio_target_startup_tests",
              [*startup_sources, ROOT / "firmware/components/protocol/src/packet_codec.cpp"],
              [ROOT / "tests/host/pair_radio_startup_stubs", ROOT / "tests/host/pair_target_startup_stubs",
               pair_target, entropy_target, bench_target, ROOT / "firmware/components/radio/include",
               ROOT / "firmware/components/protocol/include", *base],
              ["sodium_init"], " actual pair startup groups", startup_cases, ["-DOT_PAIR_RADIO_EVAL=1"])
        for name, extra in (("enrolled_pair_bridge_tests", ["--node-exe", enrolled_exe, "--diagnostic-exe", diagnostic_exe]),
                            ("enrolled_trace_capture_tests", ["--diagnostic-exe", diagnostic_exe]),
                            ("enrolled_confirmation_trial_tests", []),
                            ("enrolled_trial_operator_tests", []),
                            ("enrolled_target_pacing_tests", []),
                            ("pair_bench_bridge_tests", ["--node-exe", pair_exe]),
                            ("pair_confirmation_trial_tests", []),
                            ("pair_trial_operator_tests", []), ("pair_radio_driver_source_tests", [])):
            completed = commands.run([sys.executable, "-X", "utf8", "-B",
                ROOT / "tests/host" / (name + ".py"), *extra], env=env, check=True, timeout=90)
            result["suites"][name] = {"output": (completed.stdout + completed.stderr).strip(),
                                      "result": "passed"}
            print(name + " passed", flush=True)
        stubs = ROOT / "tests/host/security_invitation_target_stubs"
        target_common = [ROOT / "firmware/components" / name for name in
            ("persistence/src/persistent_storage_kv.cpp", "persistence/src/outbound_counter_lease_store.cpp", "security/src/aead_nonce.cpp")]
        for kind in ("invitation", "confirmation"):
            target = ROOT / "firmware/targets" / ("heltec_v4_" + kind + "_eval") / "main"
            more = [stubs, target, ROOT / "firmware/targets/heltec_v4_security_receipt_sync/main",
                    ROOT / "firmware/targets/heltec_v4_security_policy_eval/main", *base,
                    ROOT / "firmware/components/security_diagnostics/include"]
            wraps = ["sodium_init", "sodium_memzero"]
            if kind == "confirmation":
                wraps += ["crypto_aead_chacha20poly1305_ietf_encrypt", "crypto_aead_chacha20poly1305_ietf_decrypt"]
            name = "security_" + kind + "_target_tests"
            suite(name, [*target_common, target / "app_main.cpp", ROOT / "tests/host" / (name + ".cpp")],
                  more, wraps, " actual " + kind + " target groups")
        target = ROOT / "firmware/targets/heltec_v4_bench/main"
        backend = [target / name for name in ("confirmation_evaluation_backend.cpp", "confirmation_nonowning_entropy.cpp", "heltec_v4_secure_random.cpp")]
        backend += [ROOT / "firmware/components" / name for name in ("security/src/serialized_secure_random.cpp", "persistence/src/persistent_storage_kv.cpp", "companion/src/companion_device_name_owner.cpp")]
        suite("security_confirmation_ble_backend_tests",
              [*common, *backend, ROOT / "tests/host/security_confirmation_ble_backend_tests.cpp"],
              [ROOT / "tests/host/security_confirmation_ble_stubs", stubs, target, *base,
               ROOT / "firmware/components/companion/include", ROOT / "firmware/components/time/include"],
              ["sodium_init", "ot_noise_xk_split", "crypto_aead_chacha20poly1305_ietf_encrypt", "crypto_aead_chacha20poly1305_ietf_decrypt"],
              " actual protected confirmation backend groups")
        need(before == {name: pin(Path(name)) for name in before}, "source_changed_during_test")
        result["source_pins"] = {Path(name).relative_to(ROOT).as_posix(): value for name, value in before.items()}
        result["result"] = "passed"
    except Exception as error:
        result["error"] = type(error).__name__ + ": " + str(error)
        raise
    finally:
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    run(args.output_root)
