"""Clean-checkout CI adapter for the frozen OT-0005i host composition.

Rebuilds the accepted source recipe; never reads private evidence/object caches.
No target registration, production activation or device operations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "tests/host/fixtures/ot0005i_host_build.json"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--compiler-dir", type=Path)
    args = parser.parse_args()
    recipe_bytes = RECIPE.read_bytes()
    recipe_hash = hashlib.sha256(recipe_bytes).hexdigest()
    recipe = json.loads(recipe_bytes)
    generated_pins = {}
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    archive = args.archive
    if archive is None:
        archive = output / "libsodium-1.0.22.zip"
        with urllib.request.urlopen(recipe["archive_url"], timeout=60) as response:
            archive.write_bytes(response.read())
    if digest(archive) != recipe["archive_sha256"]:
        raise ValueError("Pinned libsodium archive mismatch")
    extracted = output / "dependency"
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            target = (extracted / member.filename).resolve()
            if not target.is_relative_to(extracted.resolve()):
                raise ValueError("Archive path outside dependency directory")
            if (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError("Archive symbolic link refused")
        bundle.extractall(extracted)
    candidates = list(extracted.glob("**/src/libsodium/include/sodium.h"))
    if len(candidates) != 1:
        raise ValueError("Unambiguous pinned source root required")
    sodium = candidates[0].parents[3]
    values = {"${ROOT}": str(ROOT), "${SODIUM}": str(sodium), "${OUT}": str(output)}

    def expand(value):
        for marker, replacement in values.items():
            value = value.replace(marker, replacement)
        return value

    def verify():
        if RECIPE.read_bytes() != recipe_bytes:
            raise ValueError("Build recipe changed during verification")
        for path, expected in generated_pins.items():
            if digest(path) != expected:
                raise ValueError("Generated header changed during verification")
        for name, expected in recipe["source_pins"].items():
            raw = Path(expand(name)).read_bytes()
            if name.startswith("${ROOT}/"):
                raw = raw.replace(b"\r\n", b"\n")
            if hashlib.sha256(raw).hexdigest() != expected:
                raise ValueError("Source pin mismatch: " + name)

    for name, content in recipe["generated"].items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content.encode("utf-8"))
        generated_pins[target] = digest(target)
    env = dict(os.environ)
    compiler_dir = args.compiler_dir
    if compiler_dir is None and env.get("OPENTRAIL_MSYS2_ROOT"):
        compiler_dir = Path(env["OPENTRAIL_MSYS2_ROOT"]) / "ucrt64/bin"
    if compiler_dir:
        env["PATH"] = str(compiler_dir) + os.pathsep + env.get("PATH", "")
    verify()
    for index, command in enumerate(recipe["commands"]):
        command = list(map(expand, command))
        compiler = shutil.which(command[0], path=env.get("PATH"))
        if compiler is None:
            raise ValueError("Pinned recipe compiler family unavailable")
        command[0] = compiler
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=180)
        (output / ("build-%02d.log" % index)).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError("Build failed: " + result.stderr)
    child = subprocess.run([str(output / "packet-service.exe")], env=env,
                           capture_output=True, text=True, timeout=180)
    verify()
    expected = "PASS 68 real committed Packet/driver/protected-owner host scenarios"
    if child.returncode != 0 or child.stdout.strip() != expected or child.stderr:
        raise RuntimeError("Host suite failed: " + child.stdout + child.stderr)
    record = {"result": "PASS", "host_scenarios": 68, "exit": child.returncode,
              "stdout": child.stdout, "stderr": child.stderr,
              "recipe_sha256": recipe_hash, "physical": False,
              "private_evidence_required": False, "archive_sha256": digest(archive)}
    (output / "RESULT.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(child.stdout, end="")


if __name__ == "__main__":
    main()
