"""Private, read-only package binding for OT-201. No ports, CLI or authority issuance."""
from pathlib import Path
import copy
import hashlib
import json
import re
import stat
import security_policy_input_readback as freshness
import security_policy_sync_readback as readback

SCHEMA = "OT201-SYNC-BUNDLE-1"
BUILD_SOURCE_COUNT = 43
CANDIDATE_SIZE = 440432
CANDIDATE_SHA = "25e18eb5f852ea53a005272af13cbbe6fa828833920ecd5c8c79e826984d5a62"
REPORT = "tests/benchmarks/crypto/OT-200-INPUT-SYNC-BUILD-2026-09-12.json"
REPORT_SHA = "7636ec1b3e6d80bb584ed4817680c62b2f5647d30033cd768b235d191a814d93"
CAPTURE_SHA = "6aa64631a2e9893140f6ce5585c317b8d93c72421f216310c57f895dffa78fd2"
SOURCE_PATHS = ('tools/security_policy_bundle.py', 'tools/security_policy_hardware.py', 'tools/security_policy_endpoint.py', 'tools/security_policy_execution.py', 'tools/security_policy_capture.py', 'tools/security_policy_operator.py', 'tools/security_policy_backup.py', 'tools/security_policy_backup_operator.py', 'tools/security_policy_diagnostics.py', 'tools/security_policy_runtime_bundle.py', 'tools/security_policy_input_readback.py', 'tools/security_policy_input_timing.py', 'tools/security_policy_sync_readback.py', 'tools/security_policy_sync_observation.py', 'tools/security_policy_sync_bundle.py', 'tools/security_policy_sync_execution.py', 'tools/security_policy_sync_operator.py', 'tools/security_policy_sync_backup_operator.py', 'tools/security_policy_sync_runtime_bundle.py', 'tools/security_policy_sync_restore_observation.py', 'tools/Invoke-SecurityPolicyOperator.ps1', 'tools/Invoke-SecurityPolicySyncOperator.ps1')
FROZEN_SOURCE_PINS = {'tools/security_policy_bundle.py': '0512d4118bae3d5e23628e19bc75b484b7457b937620acc15cdf7969776ae7b2', 'tools/security_policy_hardware.py': 'cfd6a29b7560e6c6d01c81c916b54742aa328c04a2c20b81320ec9d54d9c0e55', 'tools/security_policy_endpoint.py': '1a86b71b59c7b8357afd00375ed2307860b96316357223b1c2fcf460a19530b1', 'tools/security_policy_execution.py': 'afce87da6ffdc382f402bb774e44086c1e77e9741e1da04f16be733e43ae0089', 'tools/security_policy_capture.py': '6aa64631a2e9893140f6ce5585c317b8d93c72421f216310c57f895dffa78fd2', 'tools/security_policy_operator.py': '12aa023420b12540e5d71d40be11125bfd5e8810134c5b708f1d47299b8df550', 'tools/security_policy_backup.py': '9e9bf4123ef9f1fcd8c95430cd9fcc36c719574374851cae177efadcd2fd975b', 'tools/security_policy_backup_operator.py': 'd07a93636c26efcb02d13914614dadac1a29c7935d4a6e368991559cfae6cfeb', 'tools/security_policy_diagnostics.py': '591d9e447c83b42e7ed5418a137e901e30f3814249f043a8376ea5bc8a916163', 'tools/security_policy_runtime_bundle.py': '8840dfce827151681ff85a4e5a22fe9720d90e000cc795ab44e4d7f52e8ed757', 'tools/security_policy_input_readback.py': 'b1c362db5c416397207f72c47ce95a329bcb1f483c94ebf1877fe7d30ba7ee37', 'tools/security_policy_input_timing.py': '08e408cb36c442769e95112c628cc45fedb5319289d8200be5ea3e6da3d7ccc4', 'tools/security_policy_sync_readback.py': 'e95de8298d6f70646a43a9416e12272dd89316bb264f5275ab95edcbde575983', 'tools/security_policy_sync_observation.py': '5a1d6b2ddd0e2a248b92bcc85676658e741a5ad8dcd50a1eb98f7af3e6026fe3', 'tools/Invoke-SecurityPolicyOperator.ps1': '9caa1355e2d55bf0bea7027cadbdce9666e2dd858774b54ffeffbe3506a3dd01'}
SCOPE = {'version': 'ot187-policy-v0', 'radio': False, 'application_offset': 65536, 'application_span': 589824, 'nvs_offset': 53248, 'nvs_span': 12288, 'image_version': 'ot200-sync-diag-v1', 'image_target': 'heltec_v4_security_sync_diag', 'diagnostic_namespace': 'ot198diag', 'diagnostic_keys': ['stage', 'input'], 'diagnostic_record_contract': 'OT200-SYNC-INPUT-RECORD-1', 'pre_console_nvs_writes': True, 'pre_restore_nvs_observation': True, 'input_detail_write': True}
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
    need(isinstance(pins, dict) and len(pins) == BUILD_SOURCE_COUNT, "build_sources_invalid")
    for name, pin in pins.items():
        need(type(name) is str and not Path(name).is_absolute() and ".." not in Path(name).parts, "build_sources_invalid")
        _descriptor(pin)
        _check_file(root, dict(pin, path=str(root / name)))

def image_binding(package):
    """Project the exact declared candidate contract after caller package admission.

    This checks identity fields only. verify() owns file/source admission; neither
    helper nor package hashes alone establish fresh physical capture custody.
    """
    _keys(package, ("schema", "runtime_sha256", "root", "scope", "candidate", "build_report", "roles", "sources"))
    need(package["schema"] == SCHEMA and package["scope"] == SCOPE, "image_binding_invalid")
    _keys(package["scope"], SCOPE)
    need(all(type(package["scope"][key]) is type(value) for key, value in SCOPE.items()), "image_binding_invalid")
    _descriptor(package["candidate"], CANDIDATE_SIZE, True)
    need(package["candidate"]["sha256"] == CANDIDATE_SHA, "image_binding_invalid")
    try:
        return readback.ImageBinding(SCOPE["diagnostic_record_contract"], SCOPE["image_target"], CANDIDATE_SHA)
    except readback.ReadbackError:
        raise BundleError("image_binding_invalid") from None


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
    image_binding(package)
    candidate = None
    _build(root, package["build_report"])
    if not recovery:
        candidate = _check_file(root, package["candidate"], CANDIDATE_SIZE)
    roles = _roles(root, package["roles"], False)
    for row in roles:
        try:
            freshness.assert_fresh(row["nvs"])
        except freshness.ReadbackError:
            raise BundleError("original_diagnostic_not_fresh") from None
    return _private(dict(package=copy.deepcopy(package), package_sha256=digest(package),
                           candidate=candidate, roles=roles, runtime_sha256=package["runtime_sha256"], sources=copy.deepcopy(package["sources"])))

def sanitized_summary(package):
    """Intentionally excludes all private paths, roles, identities and payloads."""
    return {"schema": SCHEMA, "package_sha256": digest(package), "roles": 2, "radio": False}
