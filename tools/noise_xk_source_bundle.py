"""Host-only source/image bindings; no authority, device access or execution CLI.

Source verification and recovery-image verification are deliberately separate.
These hashes do not establish physical identity, installed state or radio admission.
They check on-disk snapshots, not the identity of already imported code objects.
"""
from __future__ import annotations

import hashlib
from pathlib import Path, PurePosixPath
import re
from typing import Mapping

SOURCE_PATHS = (
    "tools/noise_xk_bound_backend.py",
    "tools/noise_xk_source_bundle.py",
    "tools/noise_xk_role_recovery_coordinator.py",
    "tools/noise_xk_ready_runner.py",
    "tools/noise_xk_buffered_runtime.py",
    "tools/noise_xk_buffered_receipt_endpoint.py",
    "tools/ot156_noise_xk_radio_runner.py",
    "tools/ot156_noise_xk_radio_runtime.py",
    "tools/ot153_noise_xk_radio_hardware_adapter.py",
    "tools/ot153_noise_xk_radio_coordinator.py",
    "tools/ot153_noise_xk_radio_runner.py",
)
SOURCE_SCHEMA = "noise-xk-source-closure-v1"
IMAGE_SCHEMA = "noise-xk-role-images-v1"
IMAGE_ROLES = ("benchmark", "restore_a", "restore_b")
_MAX_FILE_BYTES = 64 * 1024 * 1024


class BundleError(ValueError):
    """Only a fixed, privacy-safe error code is returned."""


def _require(ok: bool, code: str) -> None:
    if not ok:
        raise BundleError(code)


def _relative(value: object) -> str:
    _require(type(value) is str and bool(value), "path_invalid")
    _require(not any(char in value for char in ("\\", ":", "\x00")), "path_invalid")
    path = PurePosixPath(value)
    _require(bool(path.parts) and not path.is_absolute() and str(path) == value and
             all(part not in (".", "..") for part in path.parts), "path_invalid")
    return value


def _link(path: Path) -> bool:
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())


def _file(root: Path, relative: str) -> bytes:
    failed = False
    raw = None
    try:
        _require(not _link(root) and root.is_dir(), "root_invalid")
        base = root.resolve()
        target = base.joinpath(*PurePosixPath(_relative(relative)).parts)
        for path in (target, *target.parents):
            if path == base:
                break
            _require(not _link(path), "link_forbidden")
        _require(target.resolve().is_relative_to(base) and target.is_file(), "file_missing")
        _require(0 < target.stat().st_size <= _MAX_FILE_BYTES, "file_size_invalid")
        raw = target.read_bytes()
        _require(0 < len(raw) <= _MAX_FILE_BYTES, "file_size_invalid")
    except OSError:
        failed = True
    if failed:
        raise BundleError("file_unreadable")
    return raw


def _descriptor(root: Path, relative: str) -> dict:
    raw = _file(root, relative)
    return {"path": relative, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def _valid_descriptor(value: object) -> None:
    _require(type(value) is dict and set(value) == {"path", "bytes", "sha256"}, "descriptor_invalid")
    _relative(value["path"])
    _require(type(value["bytes"]) is int and 0 < value["bytes"] <= _MAX_FILE_BYTES,
             "descriptor_invalid")
    _require(type(value["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", value["sha256"]) is not None,
             "descriptor_invalid")


def freeze_sources(root: Path) -> dict:
    return {"schema": SOURCE_SCHEMA, "sources": [_descriptor(root, path) for path in SOURCE_PATHS]}


def verify_sources(root: Path, manifest: object) -> None:
    _require(type(manifest) is dict and set(manifest) == {"schema", "sources"} and
             manifest["schema"] == SOURCE_SCHEMA and type(manifest["sources"]) is list,
             "source_manifest_invalid")
    for value in manifest["sources"]:
        _valid_descriptor(value)
    paths = [value["path"] for value in manifest["sources"]]
    _require(len(paths) == len(set(paths)) and set(paths) == set(SOURCE_PATHS), "source_closure_mismatch")
    for value in manifest["sources"]:
        _require(_descriptor(root, value["path"]) == value, "source_digest_mismatch")


def _image_paths(paths: object) -> None:
    _require(type(paths) is dict and set(paths) == set(IMAGE_ROLES), "image_roles_invalid")
    for path in paths.values():
        _relative(path)


def _image_descriptor(root: Path, relative: str) -> dict:
    value = _descriptor(root, relative)
    return {"name": PurePosixPath(relative).name, "bytes": value["bytes"], "sha256": value["sha256"]}


def freeze_images(root: Path, paths: Mapping[str, str]) -> dict:
    _image_paths(paths)
    return {"schema": IMAGE_SCHEMA, "images": {role: _image_descriptor(root, paths[role]) for role in IMAGE_ROLES}}


def verify_images(root: Path, manifest: object, paths: Mapping[str, str], *, recovery: bool = False) -> None:
    _require(type(recovery) is bool, "recovery_mode_invalid")
    _image_paths(paths)
    _require(type(manifest) is dict and set(manifest) == {"schema", "images"} and
             manifest["schema"] == IMAGE_SCHEMA and type(manifest["images"]) is dict and
             set(manifest["images"]) == set(IMAGE_ROLES), "image_manifest_invalid")
    for value in manifest["images"].values():
        _require(type(value) is dict and set(value) == {"name", "bytes", "sha256"}, "descriptor_invalid")
        _require(type(value["name"]) is str and "/" not in value["name"], "descriptor_invalid")
        _valid_descriptor({"path": value["name"], "bytes": value["bytes"], "sha256": value["sha256"]})
    roles = ("restore_a", "restore_b") if recovery else IMAGE_ROLES
    for role in roles:
        _require(_image_descriptor(root, paths[role]) == manifest["images"][role], "image_digest_mismatch")
