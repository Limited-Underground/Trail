"""Focused inactive Packet/driver/owner-channel composition; no device access."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_RESULT_SHA256 = "1f0e3a6c2143fb8d67f343b6349f7afe67da7c9afaf56b6d323d406057522371"
ACCEPTED_REUSE_SHA256 = "6f8b0a9edf77cc10acb8dc15a126d3dd43d8f766ce87863e9eae88384a129a15"
TEST = "tests/host/companion_packet_service_tests.cpp"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finish_verification(result, verifier):
    try:
        verifier()
    except Exception as error:
        result.update(result="VERIFICATION_REFUSED", exit=2,
                      verification_error=type(error).__name__, verification_reason=str(error))
        return
    result.update(result="PASS" if result["child_exit"] == 0 else "TEST_FAIL",
                  exit=result["child_exit"])


def write_result(output, result):
    result["finished_unix"] = time.time()
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def reporting_controls(output):
    output.mkdir(parents=True, exist_ok=False)
    observed = {}
    for tamper in (False, True):
        case = output / ("post-run-tamper" if tamper else "unchanged")
        case.mkdir()
        artifact = case / "source-fixture.txt"
        artifact.write_bytes(b"before child")
        original = digest(artifact)
        result = {"result": "VERIFY_PENDING", "exit": 2, "child_exit": 0,
                  "evidence_layer": "synthetic reporting control; no native child"}
        if tamper:
            artifact.write_bytes(b"changed after child")

        def verify():
            if digest(artifact) != original:
                raise ValueError("post-run source identity mismatch")

        finish_verification(result, verify)
        write_result(case, result)
        saved = json.loads((case / "RESULT.json").read_text(encoding="utf-8"))
        wanted = "VERIFICATION_REFUSED" if tamper else "PASS"
        if saved["result"] != wanted or saved["exit"] != (2 if tamper else 0):
            raise AssertionError("untruthful final reporting artifact")
        observed[case.name] = {"result": saved["result"], "exit": saved["exit"]}
    (output / "RESULT.json").write_text(json.dumps(observed, indent=2) + "\n", encoding="utf-8")
    return observed


def verify_reuse(readback, replacements=None):
    # replacements is used only by the filesystem-only refusal controls below;
    # the build entrypoint supplies none and never refreshes accepted identities.
    replacements = replacements or {}

    def read(path):
        path = Path(path).resolve()
        return Path(replacements.get(str(path), path)).read_bytes()

    def checked(path, wanted):
        raw = read(path)
        if hashlib.sha256(raw).hexdigest() != wanted:
            raise ValueError(f"accepted reuse pin mismatch: {Path(path).name}")
        return raw

    binding = json.loads(checked(readback, ACCEPTED_REUSE_SHA256))
    checked(binding["accepted_admission"]["path"], binding["accepted_admission"]["sha256"])
    checked(binding["accepted_result"]["path"], ACCEPTED_RESULT_SHA256)
    cache_pin = binding["accepted_object_cache"]
    cache = json.loads(checked(cache_pin["path"], cache_pin["sha256"]))
    checked(binding["compiler"]["path"], binding["compiler"]["sha256"])
    for path, pin in binding["generated_headers"].items():
        if len(checked(path, pin["sha256"])) != pin["bytes"]:
            raise ValueError("generated header length mismatch")
    wanted_names = {f"common-{n:02}" for n in (0, 1, 2, 3, 4, 5, 7)} | {
        f"primitive-{n:02}" for n in range(23)}
    if set(binding["verified_reused_objects"]) != wanted_names:
        raise ValueError("accepted object selection mismatch")
    build = Path(binding["accepted_result"]["path"]).parent.resolve()
    dependencies, objects = {}, {}
    for name, pin in binding["verified_reused_objects"].items():
        if Path(pin["path"]).resolve() != build / (name + ".o"):
            raise ValueError("object path binding mismatch")
        entry = pin["cache_entry"]
        if cache.get(name) != entry or entry["object"] != pin["sha256"]:
            raise ValueError("accepted cache entry mismatch")
        checked(pin["path"], pin["sha256"])
        objects[str(Path(pin["path"]).resolve())] = pin["sha256"]
        for path, wanted in entry["dependencies"].items():
            if path in dependencies and dependencies[path] != wanted:
                raise ValueError("conflicting dependency pin")
            dependencies[path] = wanted
    for path, wanted in dependencies.items():
        checked(path, wanted)
    return binding, objects, dependencies


def reuse_controls(readback, output):
    binding, _, _ = verify_reuse(readback)
    output.mkdir(parents=True, exist_ok=False)
    first = binding["verified_reused_objects"]["common-07"]
    targets = {
        "object": first["path"],
        "dependency": next(iter(first["cache_entry"]["dependencies"])),
        "cache": binding["accepted_object_cache"]["path"],
        "generated-header": next(iter(binding["generated_headers"])),
        "compiler": binding["compiler"]["path"],
        "readback": str(readback),
    }
    results = {"unchanged": "PASS"}
    for name, original in targets.items():
        original = str(Path(original).resolve())
        copy = output / (name + ".isolated")
        raw = Path(original).read_bytes()
        changed = bytes([raw[0] ^ 1]) + raw[1:]
        copy.write_bytes(changed)
        try:
            verify_reuse(readback, {original: copy})
        except ValueError:
            results[name + "-changed-before-run"] = "REFUSED"
        else:
            raise AssertionError("preexisting changed input accepted: " + name)
        if Path(original).read_bytes() != raw:
            raise AssertionError("original altered by refusal fixture")
    missing = output / "absent"
    for name in ("object", "dependency", "cache"):
        original = str(Path(targets[name]).resolve())
        try:
            verify_reuse(readback, {original: missing})
        except FileNotFoundError:
            results[name + "-absent-before-run"] = "REFUSED"
        else:
            raise AssertionError("absent input accepted: " + name)
    verify_reuse(readback)
    (output / "RESULT.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return results


def run(accepted_build, output, readback):
    accepted_build, output = accepted_build.resolve(), output.resolve()
    record = accepted_build / "RESULT.json"
    if digest(record) != ACCEPTED_RESULT_SHA256:
        raise ValueError("accepted build result identity mismatch")
    prior = json.loads(record.read_text(encoding="utf-8-sig"))
    if prior["result"] != "PASS":
        raise ValueError("accepted build did not pass")
    recipe = next(c for c in prior["compiled_commands"]
                  if "-c" in c and c[c.index("-c") + 1].endswith("packet_v1_product_binding_tests.cpp"))
    compiler = Path(recipe[0])
    if digest(compiler) != prior["compiler_sha256"]:
        raise ValueError("compiler identity mismatch")
    old_root = Path(recipe[recipe.index("-c") + 1]).parents[2]
    # Reuse only the thirty independently pinned primitive, Packet, delivery,
    # counter/storage/random and actual driver objects. Compile this new test
    # and its concrete GATT/controller dependencies; never link old test objects.
    binding, reused, dependencies = verify_reuse(readback)
    if record != Path(binding["accepted_result"]["path"]).resolve():
        raise ValueError("accepted build path mismatch")
    if compiler.resolve() != Path(binding["compiler"]["path"]).resolve():
        raise ValueError("compiler path mismatch")
    objects = list(map(Path, reused))
    current_dependency_pins = {}
    for path, pin in dependencies.items():
        old = Path(path)
        try:
            current = ROOT / old.relative_to(old_root)
        except ValueError:
            current = old
        if digest(current) != pin:
            raise ValueError("current topic reused dependency mismatch: " + str(current))
        current_dependency_pins[str(current.resolve())] = pin
    scope_path = Path(readback).parent / "packet-driver-export-20261109/SOURCE_BINDINGS.json"
    if digest(scope_path) != "553e8f0eac88acfc40497ce5106538613ed02bea0b37412e8ca8bfb5ff76ec3f":
        raise ValueError("released source scope mismatch")
    scope = json.loads(scope_path.read_text(encoding="utf-8"))
    fresh_names = ("companion_protocol", "companion_semantics", "companion_request_coordinator",
                   "companion_gatt_session", "companion_authorization", "companion_authorization_persistence",
                   "companion_authorization_protected_store", "companion_authorization_wire",
                   "companion_gatt_authorization", "companion_gatt_authorization_adapter",
                   "companion_gatt_authority_composition")
    fresh_sources = [ROOT / TEST] + [ROOT / ("firmware/components/companion/src/" + n + ".cpp") for n in fresh_names]
    source_paths = list(dict.fromkeys([ROOT / x["path"] for x in scope["files"]] + fresh_sources))
    source_hashes = {str(p): digest(p) for p in source_paths}
    output.mkdir(parents=True, exist_ok=False)
    exe = output / "packet-service.exe"
    command = []
    for token in recipe:
        if token.lower().startswith(str(old_root).lower() + os.sep):
            token = str(ROOT / Path(token).relative_to(old_root))
        command.append(token)
    command += ["-I", str(ROOT / "firmware/components/companion/include"),
                "-I", str(ROOT / "tests/host/pair_radio_driver_stubs"),
                "-I", str(ROOT / "firmware/targets/heltec_v4_pair_radio_eval/main")]
    commands = []
    for i, source in enumerate(fresh_sources):
        compiled, dep = output / ("fresh-%02d.o" % i), output / ("fresh-%02d.d" % i)
        one = list(command)
        one[one.index("-c") + 1] = str(source)
        one[one.index("-o") + 1] = str(compiled)
        one[one.index("-MF") + 1] = str(dep)
        commands.append(one)
        objects.append(compiled)
    link = [str(compiler), "-Wl,--gc-sections", *map(str, objects), "-ladvapi32", "-o", str(exe)]
    commands.append(link)
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    started = time.time()
    result = {"task": "OT-0005i/#75", "started_unix": started, "result": "STARTED", "exit": 2,
              "accepted_result_sha256": digest(record), "source_hashes": source_hashes,
              "accepted_reuse_sha256": digest(readback), "dependency_pins": dependencies,
              "current_dependency_pins": current_dependency_pins,
              "reused_objects": reused, "compiler_sha256": digest(compiler),
              "commands": commands, "physical": False}
    try:
        compiled_dependencies = {}
        for n, cmd in enumerate(commands[:-1]):
            scan = list(cmd)
            out_index = scan.index("-o")
            del scan[out_index:out_index + 2]
            scan[scan.index("-c")] = "-MM"
            if "-MMD" in scan:
                scan.remove("-MMD")
            scan[scan.index("-MF") + 1] = str(output / ("scan-%02d.d" % n))
            p = subprocess.run(scan, env=env, capture_output=True, text=True, timeout=120)
            (output / ("scan-%02d.stderr.log" % n)).write_text(p.stderr, encoding="utf-8")
            if p.returncode:
                result.update(result="SOURCE_SCAN_FAIL", exit=p.returncode)
                return result
            raw = (output / ("scan-%02d.d" % n)).read_text(encoding="utf-8")
            lines = raw.replace("\\\n", " ").splitlines()
            for line in lines:
                if ": " in line:
                    line = line.split(": ", 1)[1]
                elif line.startswith("object:"):
                    line = line[7:]
                for token in line.split():
                    if token == "\\" or token.endswith(":"):
                        continue
                    path = Path(token)
                    if not path.is_file():
                        raise ValueError("missing compiled dependency: " + str(path))
                    compiled_dependencies[str(path.resolve())] = digest(path)
        result["compiled_source_dependencies"] = compiled_dependencies
        for n, cmd in enumerate(commands):
            p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
            (output / f"build-{n}.stdout.log").write_text(p.stdout, encoding="utf-8")
            (output / f"build-{n}.stderr.log").write_text(p.stderr, encoding="utf-8")
            if p.returncode:
                result.update(result="BUILD_FAIL", exit=p.returncode)
                return result
        test_command = [str(exe)]
        result["test_command"] = test_command
        p = subprocess.run(test_command, env=env, capture_output=True, text=True, timeout=60)
        (output / "tests.stdout.log").write_text(p.stdout, encoding="utf-8")
        (output / "tests.stderr.log").write_text(p.stderr, encoding="utf-8")
        result.update(result="VERIFY_PENDING", child_exit=p.returncode,
                      stdout=p.stdout, stderr=p.stderr, binary_sha256=digest(exe))

        def post_run_checks():
            verify_reuse(readback)
            if current_dependency_pins != {p: digest(p) for p in current_dependency_pins}:
                raise ValueError("current topic reused dependency changed during validation")
            if compiled_dependencies != {p: digest(p) for p in compiled_dependencies}:
                raise ValueError("compiled dependency changed during validation")
            if reused != {p: digest(p) for p in reused} or source_hashes != {
                    str(p): digest(p) for p in source_paths}:
                raise ValueError("source or reused object changed during validation")

        finish_verification(result, post_run_checks)
        return result
    except Exception as error:
        result.update(result="HOST_OPERATION_FAILED", exit=2,
                      error=type(error).__name__, reason=str(error))
        return result
    finally:
        write_result(output, result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accepted-build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reuse-readback", type=Path, required=True)
    parser.add_argument("--check-reuse-only", action="store_true")
    parser.add_argument("--check-reporting-only", action="store_true")
    args = parser.parse_args()
    if args.check_reuse_only:
        print(json.dumps(reuse_controls(args.reuse_readback, args.out)))
        raise SystemExit(0)
    if args.check_reporting_only:
        print(json.dumps(reporting_controls(args.out)))
        raise SystemExit(0)
    outcome = run(args.accepted_build, args.out, args.reuse_readback)
    print(json.dumps({k: outcome[k] for k in ("task", "result", "exit", "started_unix", "finished_unix")}))
    raise SystemExit(outcome["exit"])
