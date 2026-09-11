"""Private, read-only package binding for OT-196. No ports, CLI or authority issuance."""
from pathlib import Path
import copy
import hashlib
import json
import re
import stat
import security_policy_stage_readback as readback

SCHEMA = "OT196-STAGE-BUNDLE-1"
CANDIDATE_SIZE = 438784
CANDIDATE_SHA = "449e3249bf6d92e5bcfa394244bd8b15544ee2f5aa8f0f4eded13321eea06164"
REPORT = "tests/benchmarks/crypto/OT-195-DURABLE-STAGE-BUILD-2026-09-11.json"
REPORT_SHA = "4489c05d7542cb883b67eaf54e7a150168be68e12e5175aa5df49affbdfd9615"
CAPTURE_SHA = "6aa64631a2e9893140f6ce5585c317b8d93c72421f216310c57f895dffa78fd2"
SOURCE_PATHS = tuple("tools/security_policy_" + name + ".py" for name in
                     ("bundle", "hardware", "endpoint", "execution", "capture", "operator", "backup", "backup_operator", "diagnostics", "runtime_bundle", "stage_bundle", "stage_execution", "stage_operator", "stage_backup_operator", "stage_runtime_bundle", "stage_readback", "stage_observation")) + ("tools/Invoke-SecurityPolicyStageOperator.ps1", "tools/Invoke-SecurityPolicyOperator.ps1")
FROZEN_SOURCE_PINS = {'tools/security_policy_bundle.py': '0512d4118bae3d5e23628e19bc75b484b7457b937620acc15cdf7969776ae7b2',
 'tools/security_policy_hardware.py': 'cfd6a29b7560e6c6d01c81c916b54742aa328c04a2c20b81320ec9d54d9c0e55',
 'tools/security_policy_endpoint.py': '1a86b71b59c7b8357afd00375ed2307860b96316357223b1c2fcf460a19530b1',
 'tools/security_policy_execution.py': 'afce87da6ffdc382f402bb774e44086c1e77e9741e1da04f16be733e43ae0089',
 'tools/security_policy_capture.py': '6aa64631a2e9893140f6ce5585c317b8d93c72421f216310c57f895dffa78fd2',
 'tools/security_policy_operator.py': '12aa023420b12540e5d71d40be11125bfd5e8810134c5b708f1d47299b8df550',
 'tools/security_policy_backup.py': '9e9bf4123ef9f1fcd8c95430cd9fcc36c719574374851cae177efadcd2fd975b',
 'tools/security_policy_backup_operator.py': 'd07a93636c26efcb02d13914614dadac1a29c7935d4a6e368991559cfae6cfeb',
 'tools/security_policy_diagnostics.py': '591d9e447c83b42e7ed5418a137e901e30f3814249f043a8376ea5bc8a916163',
 'tools/security_policy_runtime_bundle.py': '8840dfce827151681ff85a4e5a22fe9720d90e000cc795ab44e4d7f52e8ed757',
 'tools/security_policy_stage_readback.py': '626cd8587c0e6e3f39723b1b746e43e3d85f434ea46497e7146ea61825afe201',
 'tools/security_policy_stage_observation.py': '59db82273df59f0d84865baf645b12764456bf4a927881d52e83b5914ad6f7cc',
 'tools/Invoke-SecurityPolicyOperator.ps1': '9caa1355e2d55bf0bea7027cadbdce9666e2dd858774b54ffeffbe3506a3dd01'}
SCOPE = {"version": "ot187-policy-v0", "radio": False, "application_offset": 65536,
         "application_span": 589824, "nvs_offset": 53248, "nvs_span": 12288, "image_version": "ot195-policy-diag-v0",
         "diagnostic_namespace": "ot195diag", "diagnostic_key": "stage",
         "pre_console_nvs_writes": True, "pre_restore_nvs_observation": True}
PARTITION_SHA = "b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601"
OTA_SHA = "7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f"
PROTECTED = {"bootloader": 32768, "partition": 4096, "ota": 8192}

class BundleError(ValueError):
    """Fixed categories only; never include paths or identities."""

class PrivateMaterial(dict):
    def __repr__(self):
        return "<private security policy material>"
    __str__ = __repr__

def _private(value):
    if isinstance(value, dict):
        return PrivateMaterial({k: _private(v) for k, v in value.items()})
    if isinstance(value, list):
        return [_private(v) for v in value]
    return value

def need(ok, code):
    if not ok:
        raise BundleError(code)

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def _pairs(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, "duplicate_key")
        result[key] = value
    return result

def loads(raw):
    """Strict JSON decoding; no filesystem access and no exception content leakage."""
    try:
        return _private(json.loads(raw, object_pairs_hook=_pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(BundleError("json_invalid"))))
    except BundleError:
        raise
    except Exception:
        raise BundleError("json_invalid") from None

def digest(package):
    try:
        return sha(json.dumps(package, sort_keys=True, separators=(",", ":"),
                              ensure_ascii=True, allow_nan=False).encode("ascii"))
    except Exception:
        raise BundleError("package_invalid") from None

def _keys(value, keys):
    need(isinstance(value, dict) and set(value) == set(keys), "schema_invalid")

def _descriptor(value, size=None, path=False):
    _keys(value, ("path", "bytes", "sha256") if path else ("bytes", "sha256"))
    need(type(value["bytes"]) is int and 0 < value["bytes"] <= 16777216, "descriptor_invalid")
    need(size is None or value["bytes"] == size, "descriptor_invalid")
    need(type(value["sha256"]) is str and re.fullmatch("[0-9a-f]{64}", value["sha256"]), "descriptor_invalid")
    if path:
        need(type(value["path"]) is str, "path_invalid")

def _root(root):
    try:
        p = Path(root)
        need(p.is_absolute() and p.is_dir(), "root_invalid")
        _nonreparse(p)
        return p.resolve()
    except (OSError, ValueError, TypeError):
        raise BundleError("root_invalid") from None

def _nonreparse(path):
    for p in (path, *path.parents):
        info = p.lstat()
        need(not stat.S_ISLNK(info.st_mode) and not getattr(info, "st_file_attributes", 0) & 0x400,
             "reparse_rejected")

def _path(root, value, private=False, exists=True):
    try:
        p = Path(value)
        need(p.is_absolute() and ".." not in p.parts, "path_invalid")
        base = root / ".private" if private else root
        need(p.is_relative_to(base), "path_outside")
        if exists:
            _nonreparse(p)
            need(p.is_file(), "file_missing")
        else:
            parent = p
            while not parent.exists() and parent != parent.parent:
                parent = parent.parent
            _nonreparse(parent)
        need(p.resolve().is_relative_to(base), "path_outside")
        return p
    except BundleError:
        raise
    except (OSError, ValueError, TypeError):
        raise BundleError("file_unavailable") from None

def _read(path):
    try:
        need(path.stat().st_size <= 16777216, "file_size")
        return path.read_bytes()
    except BundleError:
        raise
    except OSError:
        raise BundleError("file_unavailable") from None

def _file(root, value, size=None, private=False):
    p = _path(root, value, private)
    raw = _read(p)
    need(size is None or len(raw) == size, "file_size")
    return {"path": str(p), "bytes": len(raw), "sha256": sha(raw)}

def _check_file(root, descriptor, size=None, private=False):
    _descriptor(descriptor, size, True)
    p = _path(root, descriptor["path"], private)
    raw = _read(p)
    need(len(raw) == descriptor["bytes"] and sha(raw) == descriptor["sha256"], "file_changed")
    return raw

def _identity(value):
    need(type(value) is str and re.fullmatch(r"(?:[a-fA-F0-9]{12}|[a-fA-F0-9]{2}(?::[a-fA-F0-9]{2}){5}|[a-fA-F0-9]{2}(?:-[a-fA-F0-9]{2}){5})", value), "identity_invalid")
    return value.replace(":", "").replace("-", "").lower()

def _roles(root, roles, freezing):
    need(type(roles) in (list, tuple) and len(roles) == 2, "roles_invalid")
    result = []
    for index, role in enumerate(roles):
        _keys(role, ("role", "private_route", "private_identity", "application", "nvs", "protected"))
        need(role["role"] == "AB"[index], "roles_invalid")
        route = role["private_route"]
        need(type(route) is str and 0 < len(route) <= 512 and route == route.strip()
             and all(ord(c) >= 32 and ord(c) != 127 for c in route), "route_invalid")
        identity = _identity(role["private_identity"])
        _keys(role["protected"], PROTECTED)
        for key, size in PROTECTED.items():
            _descriptor(role["protected"][key], size)
        need(role["protected"]["partition"]["sha256"] == PARTITION_SHA
             and role["protected"]["ota"]["sha256"] == OTA_SHA, "boot_selection_invalid")
        entry = copy.deepcopy(role)
        entry["private_identity"] = identity
        if freezing:
            entry["application"] = _file(root, role["application"], 589824, True)
            entry["nvs"] = _file(root, role["nvs"], 12288, True)
        else:
            entry["application"] = _check_file(root, role["application"], 589824, True)
            entry["nvs"] = _check_file(root, role["nvs"], 12288, True)
        result.append(PrivateMaterial(entry))
    need(result[0]["private_route"].casefold() != result[1]["private_route"].casefold()
         and result[0]["private_identity"] != result[1]["private_identity"], "roles_not_unique")
    # Distinct custody files are mandatory even when original bytes happen to match.
    for name in ("application", "nvs"):
        values = [r[name]["path"] if not freezing else result[i][name]["path"] for i, r in enumerate(roles)]
        need(values[0].casefold() != values[1].casefold(), "custody_not_unique")
    return result

def _sources(root):
    result = {}
    for name in SOURCE_PATHS:
        descriptor = _file(root, root / name)
        result[name] = {key: descriptor[key] for key in ("bytes", "sha256")}
    for name, expected in FROZEN_SOURCE_PINS.items():
        need(result[name]["sha256"] == expected, "frozen_source_changed")
    need(result["tools/security_policy_capture.py"]["sha256"] == CAPTURE_SHA, "capture_changed")
    return result

def _build(root, descriptor):
    need(descriptor["path"] == str(root / REPORT) and descriptor["sha256"] == REPORT_SHA, "build_pin_invalid")
    report = loads(_check_file(root, descriptor))
    pins = report.get("source_pins")
    need(isinstance(pins, dict) and len(pins) == 37, "build_sources_invalid")
    for name, pin in pins.items():
        need(type(name) is str and not Path(name).is_absolute() and ".." not in Path(name).parts, "build_sources_invalid")
        _descriptor(pin)
        _check_file(root, dict(pin, path=str(root / name)))

def freeze(root: Path, candidate: Path, roles: tuple[dict, dict], *, runtime_sha256: str) -> dict:
    root = _root(root)
    candidate = _file(root, candidate, CANDIDATE_SIZE)
    need(candidate["sha256"] == CANDIDATE_SHA, "candidate_changed")
    report = _file(root, root / REPORT)
    _build(root, report)
    package = PrivateMaterial(schema=SCHEMA, runtime_sha256=runtime_sha256, root=str(root), scope=copy.deepcopy(SCOPE),
        candidate=candidate, build_report=report, roles=_roles(root, roles, True), sources=_sources(root))
    verify(root, package)
    return _private(package)

def verify(root, package, recovery=False):
    need(type(recovery) is bool, "recovery_invalid")
    root = _root(root)
    _keys(package, ("schema", "runtime_sha256", "root", "scope", "candidate", "build_report", "roles", "sources"))
    need(type(package["runtime_sha256"]) is str and re.fullmatch("[0-9a-f]{64}", package["runtime_sha256"]), "runtime_pin_invalid")
    _keys(package["scope"], SCOPE)
    need(all(type(package["scope"][key]) is type(value) for key, value in SCOPE.items()), "scope_invalid")
    need(package["schema"] == SCHEMA and package["root"] == str(root)
         and package["scope"] == SCOPE and type(package["scope"].get("radio")) is bool, "scope_invalid")
    _descriptor(package["candidate"], CANDIDATE_SIZE, True)
    need(package["candidate"]["sha256"] == CANDIDATE_SHA, "candidate_changed")
    _path(root, package["candidate"]["path"], exists=not recovery)
    _descriptor(package["build_report"], path=True)
    need(package["build_report"]["path"] == str(root / REPORT)
         and package["build_report"]["sha256"] == REPORT_SHA, "build_pin_invalid")
    _keys(package["sources"], SOURCE_PATHS)
    for pin in package["sources"].values():
        _descriptor(pin)
    need(_sources(root) == package["sources"], "source_changed")
    candidate = None
    _build(root, package["build_report"])
    if not recovery:
        candidate = _check_file(root, package["candidate"], CANDIDATE_SIZE)
    roles = _roles(root, package["roles"], False)
    for row in roles:
        try:
            readback.assert_fresh(row["nvs"])
        except readback.ReadbackError:
            raise BundleError("original_diagnostic_not_fresh") from None
    return _private(dict(package=copy.deepcopy(package), package_sha256=digest(package),
                           candidate=candidate, roles=roles, runtime_sha256=package["runtime_sha256"], sources=copy.deepcopy(package["sources"])))

def sanitized_summary(package):
    """Intentionally excludes all private paths, roles, identities and payloads."""
    return {"schema": SCHEMA, "package_sha256": digest(package), "roles": 2, "radio": False}
