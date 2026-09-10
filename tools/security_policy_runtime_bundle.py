"""Build and verify a private isolated OT-189 runtime; never launch it or access devices."""
from pathlib import Path, PurePosixPath
import ast
import base64
import csv
import email
import hashlib
import re
import shutil

SCHEMA = "OT189-RUNTIME-1"
PTH = b"Lib\nDLLs\npackages\npolicy\n"
ESPTOOL_CONFIG = b"[esptool]\n"
POLICY = tuple("security_policy_" + name + ".py" for name in
               ("bundle", "hardware", "endpoint", "execution", "capture", "operator", "backup", "backup_operator")) + ("Invoke-SecurityPolicyOperator.ps1",)
APPROVED = {"esptool": "5.3.1", "pyserial": "3.5", "bitstring": "4.4.0",
            "bitarray": "3.10.1", "tibs": "0.5.7", "cryptography": "46.0.7",
            "cffi": "2.1.1", "pycparser": "3.0", "reedsolo": "1.7.0", "pyyaml": "6.0.3",
            "intelhex": "2.3.0", "rich-click": "1.9.8", "click": "8.1.8",
            "rich": "15.0.0", "markdown-it-py": "4.2.0", "mdurl": "0.1.2",
            "pygments": "2.20.0", "colorama": "0.4.6"}
ENV = {"python_version": "3.14", "python_full_version": "3.14.6",
       "platform_system": "Windows", "platform_machine": "AMD64", "sys_platform": "win32",
       "os_name": "nt", "platform_python_implementation": "CPython",
       "implementation_name": "cpython", "extra": ""}


class RuntimeBundleError(RuntimeError):
    """Fixed error category without local identifiers or raw exception contents."""


def need(value, code="runtime_bundle_invalid"):
    if not value:
        raise RuntimeBundleError(code)


def normalized(name):
    return re.sub(r"[-_.]+", "-", name).lower()


def checked(path, *, directory=None):
    need(isinstance(path, Path) and path.is_absolute(), "path_invalid")
    for entry in (path, *path.parents):
        if entry.exists() or entry.is_symlink():
            info = entry.lstat()
            need(not entry.is_symlink() and not (getattr(info, "st_file_attributes", 0) & 0x400),
                 "indirect_path")
    if directory is not None:
        need(path.is_dir() if directory else path.is_file(), "path_missing")
    return path


def relative(name):
    need(type(name) is str and name and "\\" not in name and ":" not in name, "relative_path_invalid")
    p = PurePosixPath(name)
    need(not p.is_absolute() and all(x not in ("", ".", "..") for x in name.split("/")),
         "relative_path_invalid")
    return p


def forbidden(name):
    p = PurePosixPath(name)
    return "__pycache__" in p.parts or p.suffix.lower() in (".pyc", ".pyo", ".pth")


def files_under(root):
    result = []
    for entry in root.rglob("*"):
        checked(entry)
        if entry.is_file():
            result.append(entry)
    return result


def version(value):
    need(re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", value) is not None, "version_invalid")
    return tuple(int(x) for x in value.split("."))


def compare(left, op, right, *, numeric=False):
    if numeric:
        if right.endswith(".*"):
            need(op in ("==", "!="), "marker_invalid")
            result = left == right[:-2] or left.startswith(right[:-1])
            return result if op == "==" else not result
        a, b = version(left), version(right)
        width = max(len(a), len(b))
        left, right = a + (0,) * (width-len(a)), b + (0,) * (width-len(b))
    operations = {"==": lambda: left == right, "!=": lambda: left != right,
                  "<": lambda: left < right, "<=": lambda: left <= right,
                  ">": lambda: left > right, ">=": lambda: left >= right,
                  "in": lambda: left in right, "not in": lambda: left not in right}
    need(op in operations, "marker_invalid")
    return operations[op]()


def marker(text):
    def evaluate(node):
        if isinstance(node, ast.BoolOp):
            values = [evaluate(x) for x in node.values]
            if isinstance(node.op, ast.And): return all(values)
            if isinstance(node.op, ast.Or): return any(values)
        if isinstance(node, ast.Compare) and len(node.ops) == 1 and len(node.comparators) == 1:
            left, right = node.left, node.comparators[0]
            need(isinstance(left, ast.Name) and left.id in ENV and
                 isinstance(right, ast.Constant) and type(right.value) is str, "marker_invalid")
            ops = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=",
                   ast.Gt: ">", ast.GtE: ">=", ast.In: "in", ast.NotIn: "not in"}
            need(type(node.ops[0]) in ops, "marker_invalid")
            return compare(ENV[left.id], ops[type(node.ops[0])], right.value,
                           numeric=left.id in ("python_version", "python_full_version"))
        raise RuntimeBundleError("marker_invalid")
    return evaluate(ast.parse(text.strip(), mode="eval").body)


def requirements(metadata):
    result = []
    for line in metadata.get_all("Requires-Dist", []):
        requirement, separator, condition = line.partition(";")
        if separator and not marker(condition):
            continue
        match = re.fullmatch(r"\s*([A-Za-z0-9_.-]+)\s*(.*?)\s*", requirement)
        need(match is not None, "requirement_invalid")
        name, spec = normalized(match[1]), match[2].strip().strip("()")
        need(name in APPROVED, "dependency_not_approved")
        for clause in spec.split(",") if spec else []:
            check = re.fullmatch(r"\s*(==|!=|<=|>=|<|>|~=)\s*([0-9]+(?:\.[0-9]+)*(?:\.\*)?)\s*", clause)
            need(check is not None, "requirement_invalid")
            op, value = check.groups()
            if op == "~=":
                parts = list(version(value)); upper = parts[:-1]
                need(upper, "requirement_invalid")
                upper[-1] += 1
                valid = compare(APPROVED[name], ">=", value, numeric=True) and compare(
                    APPROVED[name], "<", ".".join(map(str, upper)), numeric=True)
            else:
                valid = compare(APPROVED[name], op, value, numeric=True)
            need(valid, "dependency_version_mismatch")
        result.append(name)
    return result


def distribution_closure(site):
    records = {}
    for directory in site.glob("*.dist-info"):
        checked(directory, directory=True)
        metadata_path = checked(directory / "METADATA", directory=False)
        metadata = email.message_from_bytes(metadata_path.read_bytes())
        name = normalized(metadata.get("Name", ""))
        if name not in APPROVED:
            continue
        need(name not in records and metadata.get("Version") == APPROVED[name], "distribution_invalid")
        records[name] = (directory, metadata)
    pending, selected = ["esptool", "pyserial"], {}
    while pending:
        name = pending.pop()
        if name in selected: continue
        need(name in records, "dependency_missing")
        selected[name] = records[name][0]
        pending.extend(requirements(records[name][1]))
    return selected


def package_files(site, directory):
    record = checked(directory / "RECORD", directory=False)
    result = []
    for row in csv.reader(record.read_text(encoding="utf-8").splitlines()):
        need(len(row) == 3, "record_invalid")
        name, expected, size = row
        if name.startswith("../"):
            # Installed console-script wrappers are not imported or copied.
            need(re.fullmatch(r"(?:\.\./)+Scripts/(?:[^/]+|__pycache__/[^/]+\.pyc)", name) is not None, "record_escape")
            continue
        relative(name)
        if forbidden(name): continue
        source = checked(site / name, directory=False)
        raw = source.read_bytes()
        if expected:
            need(expected.startswith("sha256="), "record_hash_invalid")
            digest = base64.urlsafe_b64encode(hashlib.sha256(raw).digest()).decode().rstrip("=")
            need(expected[7:] == digest and size == str(len(raw)), "installed_file_changed")
        else:
            need(source == record and size == "", "record_hash_missing")
        result.append((source, "packages/" + name, hashlib.sha256(raw).hexdigest()))
    need(any(source == record for source, _, _ in result), "record_missing")
    return result


def build(base: Path, site: Path, worktree: Path, destination: Path) -> dict:
    try:
        for root in (base, site, worktree): checked(root, directory=True)
        checked(destination)
        private = checked(worktree / ".private", directory=True)
        need(destination.parent == private and not destination.exists(), "destination_not_fresh")
        selected = distribution_closure(site)
        inputs = [(checked(base / "python.exe", directory=False), "python.exe", None)]
        inputs.extend((checked(path, directory=False), path.name, None) for path in base.glob("*.dll"))
        need(any(name.lower() == "python314.dll" for _, name, _ in inputs), "runtime_dll_missing")
        for folder in ("Lib", "DLLs"):
            root = checked(base / folder, directory=True)
            for path in files_under(root):
                rel = path.relative_to(base).as_posix()
                if "site-packages" not in path.relative_to(root).parts and not forbidden(rel):
                    inputs.append((path, rel, None))
        for directory in selected.values(): inputs.extend(package_files(site, directory))
        for name in POLICY:
            inputs.append((checked(worktree / "tools" / name, directory=False), "policy/" + name, None))
        names = [name for _, name, _ in inputs] + ["python314._pth", "esptool.cfg"]
        need(len(names) == len({name.casefold() for name in names}), "duplicate_capsule_path")
        for name in names: relative(name)
        powershell = shutil.which("pwsh")
        need(powershell is not None, "powershell_missing")
        powershell_path = checked(Path(powershell).absolute(), directory=False)
        powershell_raw = powershell_path.read_bytes()
        destination.mkdir()
        descriptors = {}
        for source, name, admitted_sha in inputs:
            raw = source.read_bytes()
            need(admitted_sha is None or hashlib.sha256(raw).hexdigest() == admitted_sha, "installed_file_changed")
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
            descriptors[name] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        (destination / "python314._pth").write_bytes(PTH)
        descriptors["python314._pth"] = {"bytes": len(PTH), "sha256": hashlib.sha256(PTH).hexdigest()}
        (destination / "esptool.cfg").write_bytes(ESPTOOL_CONFIG)
        descriptors["esptool.cfg"] = {"bytes": len(ESPTOOL_CONFIG), "sha256": hashlib.sha256(ESPTOOL_CONFIG).hexdigest()}
        manifest = {"schema": SCHEMA, "root": str(destination), "worktree": str(worktree),
                    "files": dict(sorted(descriptors.items())),
                    "versions": {"python": "3.14.6", **{name: APPROVED[name] for name in sorted(selected)}},
                    "powershell": {"path": str(powershell_path), "bytes": len(powershell_raw), "sha256": hashlib.sha256(powershell_raw).hexdigest()}}
        verify(destination, manifest)
        return manifest
    except RuntimeBundleError:
        raise
    except BaseException:
        raise RuntimeBundleError("runtime_bundle_build_failed") from None


def verify(capsule: Path, manifest: dict) -> bool:
    try:
        checked(capsule, directory=True)
        need(type(manifest) is dict and set(manifest) == {"schema", "root", "worktree", "files", "versions", "powershell"}
             and manifest["schema"] == SCHEMA and manifest["root"] == str(capsule), "manifest_invalid")
        power = manifest["powershell"]
        need(type(power) is dict and set(power) == {"path", "bytes", "sha256"}, "powershell_invalid")
        power_raw = checked(Path(power["path"]), directory=False).read_bytes()
        need(type(power["bytes"]) is int and power["bytes"] == len(power_raw) and power["sha256"] == hashlib.sha256(power_raw).hexdigest(), "powershell_changed")
        worktree = Path(manifest["worktree"])
        checked(worktree, directory=True)
        need(capsule.parent == worktree / ".private", "capsule_location_invalid")
        pins = manifest["files"]
        need(type(pins) is dict and len(pins) > 0, "manifest_invalid")
        need(len(pins) == len({name.casefold() for name in pins}), "duplicate_capsule_path")
        for name, descriptor in pins.items():
            relative(name)
            need(not forbidden(name) and type(descriptor) is dict and set(descriptor) == {"bytes", "sha256"}
                 and type(descriptor["bytes"]) is int and descriptor["bytes"] >= 0
                 and type(descriptor["sha256"]) is str and re.fullmatch("[0-9a-f]{64}", descriptor["sha256"]), "manifest_invalid")
        actual = {path.relative_to(capsule).as_posix() for path in files_under(capsule)}
        need(actual == set(pins), "capsule_file_set_changed")
        for name, descriptor in pins.items():
            raw = (capsule / name).read_bytes()
            need({"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()} == descriptor, "capsule_file_changed")
        need((capsule / "python314._pth").read_bytes() == PTH, "runtime_path_changed")
        need("esptool.cfg" in pins and (capsule / "esptool.cfg").read_bytes() == ESPTOOL_CONFIG, "esptool_config_changed")
        versions = manifest["versions"]
        need(type(versions) is dict and versions.get("python") == "3.14.6" and
             all(name == "python" or name in APPROVED and value == APPROVED[name] for name, value in versions.items()),
             "versions_invalid")
        selected = distribution_closure(capsule / "packages")
        need(versions == {"python": "3.14.6", **{name: APPROVED[name] for name in selected}}, "versions_invalid")
        need(all("policy/" + name in pins for name in POLICY) and "python.exe" in pins and "python314.dll" in pins,
             "capsule_required_file_missing")
        return True
    except RuntimeBundleError:
        raise
    except BaseException:
        raise RuntimeBundleError("runtime_bundle_verification_failed") from None
