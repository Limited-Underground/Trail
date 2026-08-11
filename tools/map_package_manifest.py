"""Validate and verify offline OpenTrail map-package manifests.

OTMP0 is off-device package metadata. This tool performs no network, device,
map-download, activation, or package write operation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Any
from urllib.parse import urlparse


SCHEMA = "OTMP0"
VERSION = 0
MERCATOR_LATITUDE_LIMIT = 85.05112878
MAX_MANIFEST_BYTES = 64 * 1024
MAX_PACKAGE_BYTES = (1 << 63) - 1
MAX_TILE_COUNT = (1 << 32) - 1
SAFE_ID = re.compile(r"^[a-z0-9][a-z0-9-]{2,63}$")
SAFE_REVISION = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
BLOCKED_PUBLIC_TILE_HOSTS = {
    "tile.openstreetmap.org",
    "vector.openstreetmap.org",
}
CONTAINER_RULES = {
    "mbtiles-1.3": {"scheme": "tms", "encodings": {"jpeg"}},
    "pmtiles-3": {"scheme": "xyz", "encodings": {"jpeg"}},
    "indexed-raster-v0": {"scheme": "xyz", "encodings": {"jpeg", "rgb565"}},
}
TOP_LEVEL_KEYS = {
    "compatibility",
    "content",
    "coverage",
    "package",
    "schema",
    "source",
    "version",
}


class MapManifestError(ValueError):
    pass


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise MapManifestError(f"{name} must be an object")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected:
        raise MapManifestError(f"{name} must contain only canonical fields")


def _integer(
    value: Any,
    name: str,
    minimum: int = 0,
    maximum: int | None = None,
) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or (maximum is not None and value > maximum)
    ):
        bounds = f"between {minimum} and {maximum}" if maximum is not None else f">= {minimum}"
        raise MapManifestError(f"{name} must be an integer {bounds}")
    return value


def _number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MapManifestError(f"{name} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise MapManifestError(f"{name} must be finite")
    return result


def _text(value: Any, name: str, maximum: int = 128) -> str:
    if (
        not isinstance(value, str)
        or not value.strip()
        or len(value) > maximum
        or any(ord(character) < 32 for character in value)
    ):
        raise MapManifestError(f"{name} must be nonempty bounded text")
    return value


def _https_url(value: Any, name: str) -> str:
    url = _text(value, name, 512)
    parsed = urlparse(url)
    if (
        parsed.scheme != "https"
        or not parsed.netloc
        or parsed.username
        or parsed.password
        or parsed.query
    ):
        raise MapManifestError(
            f"{name} must be an HTTPS URL without credentials or a query"
        )
    host = (parsed.hostname or "").lower()
    if any(host == blocked or host.endswith(f".{blocked}") for blocked in BLOCKED_PUBLIC_TILE_HOSTS):
        raise MapManifestError(f"{name} cannot reference a public OSM tile service")
    return url


def validate_manifest(document: dict[str, Any]) -> None:
    _exact_keys(document, TOP_LEVEL_KEYS, "manifest")
    if document.get("schema") != SCHEMA or document.get("version") != VERSION:
        raise MapManifestError("schema/version must be OTMP0/v0")

    package = _mapping(document.get("package"), "package")
    _exact_keys(
        package,
        {
            "container",
            "distribution",
            "id",
            "projection",
            "revision",
            "tile_encoding",
            "tile_scheme",
        },
        "package",
    )
    package_id = package.get("id")
    if not isinstance(package_id, str) or not SAFE_ID.fullmatch(package_id):
        raise MapManifestError("package.id is invalid")
    revision = package.get("revision")
    if not isinstance(revision, str) or not SAFE_REVISION.fullmatch(revision):
        raise MapManifestError("package.revision is invalid")
    container = package.get("container")
    if container not in CONTAINER_RULES:
        raise MapManifestError("package.container is unsupported")
    rules = CONTAINER_RULES[container]
    if package.get("tile_scheme") != rules["scheme"]:
        raise MapManifestError("package.tile_scheme does not match the container")
    if package.get("tile_encoding") not in rules["encodings"]:
        raise MapManifestError("package.tile_encoding does not match the container")
    if package.get("projection") != "EPSG:3857":
        raise MapManifestError("package.projection must be EPSG:3857")
    if package.get("distribution") not in {"local-only", "redistributable"}:
        raise MapManifestError("package.distribution is invalid")

    coverage = _mapping(document.get("coverage"), "coverage")
    _exact_keys(
        coverage,
        {"east", "max_zoom", "min_zoom", "north", "south", "west"},
        "coverage",
    )
    west = _number(coverage.get("west"), "coverage.west")
    east = _number(coverage.get("east"), "coverage.east")
    south = _number(coverage.get("south"), "coverage.south")
    north = _number(coverage.get("north"), "coverage.north")
    if not (-180 <= west < east <= 180):
        raise MapManifestError("coverage longitude bounds are invalid")
    if not (
        -MERCATOR_LATITUDE_LIMIT
        <= south
        < north
        <= MERCATOR_LATITUDE_LIMIT
    ):
        raise MapManifestError("coverage latitude bounds are invalid")
    min_zoom = _integer(coverage.get("min_zoom"), "coverage.min_zoom")
    max_zoom = _integer(coverage.get("max_zoom"), "coverage.max_zoom")
    if min_zoom > max_zoom or max_zoom > 22:
        raise MapManifestError("coverage zoom range is invalid")

    source = _mapping(document.get("source"), "source")
    _exact_keys(
        source,
        {
            "attribution_text",
            "attribution_url",
            "dataset",
            "dataset_revision",
            "license_id",
            "license_url",
            "offline_rights_reference",
            "offline_use_permitted",
            "redistribution_permitted",
            "style_revision",
        },
        "source",
    )
    for field in ("dataset", "dataset_revision", "style_revision", "license_id"):
        _text(source.get(field), f"source.{field}")
    attribution = _text(source.get("attribution_text"), "source.attribution_text", 160)
    if any(character in attribution for character in "<>&"):
        raise MapManifestError("source.attribution_text must be plain text")
    for field in (
        "attribution_url",
        "license_url",
        "offline_rights_reference",
    ):
        _https_url(source.get(field), f"source.{field}")
    if source.get("offline_use_permitted") is not True:
        raise MapManifestError("source.offline_use_permitted must be true")
    if not isinstance(source.get("redistribution_permitted"), bool):
        raise MapManifestError("source.redistribution_permitted must be Boolean")
    if (
        package["distribution"] == "redistributable"
        and not source["redistribution_permitted"]
    ):
        raise MapManifestError("redistributable packages require redistribution rights")

    content = _mapping(document.get("content"), "content")
    _exact_keys(content, {"byte_length", "sha256", "tile_count"}, "content")
    byte_length = _integer(
        content.get("byte_length"), "content.byte_length", 1, MAX_PACKAGE_BYTES
    )
    _integer(content.get("tile_count"), "content.tile_count", 1, MAX_TILE_COUNT)
    digest = content.get("sha256")
    if (
        not isinstance(digest, str)
        or not SHA256.fullmatch(digest)
        or len(set(digest)) == 1
    ):
        raise MapManifestError("content.sha256 is invalid")

    compatibility = _mapping(document.get("compatibility"), "compatibility")
    _exact_keys(
        compatibility,
        {"minimum_firmware", "reader", "required_storage_bytes", "scratch_bytes"},
        "compatibility",
    )
    for field in ("minimum_firmware", "reader"):
        value = compatibility.get(field)
        if not isinstance(value, str) or not SAFE_REVISION.fullmatch(value):
            raise MapManifestError(f"compatibility.{field} is invalid")
    required_storage = _integer(
        compatibility.get("required_storage_bytes"),
        "compatibility.required_storage_bytes",
        1,
        MAX_PACKAGE_BYTES,
    )
    scratch = _integer(
        compatibility.get("scratch_bytes"),
        "compatibility.scratch_bytes",
        maximum=MAX_PACKAGE_BYTES,
    )
    if required_storage < byte_length:
        raise MapManifestError("required storage cannot be smaller than the package")
    if scratch > required_storage:
        raise MapManifestError("scratch space cannot exceed required storage")


def verify_package(document: dict[str, Any], package_path: Path) -> None:
    validate_manifest(document)
    expected_length = document["content"]["byte_length"]
    expected_digest = document["content"]["sha256"]
    try:
        if not package_path.is_file() or package_path.stat().st_size != expected_length:
            raise MapManifestError("package byte length does not match manifest")
        digest = hashlib.sha256()
        with package_path.open("rb") as package:
            while chunk := package.read(1024 * 1024):
                digest.update(chunk)
    except OSError as exc:
        raise MapManifestError("package could not be read") from exc
    if digest.hexdigest() != expected_digest:
        raise MapManifestError("package digest does not match manifest")


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise MapManifestError("manifest contains a duplicate field")
        result[key] = value
    return result


def _load_manifest(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_MANIFEST_BYTES + 1)
        if len(raw) > MAX_MANIFEST_BYTES:
            raise MapManifestError("manifest exceeds the size limit")
        value = json.loads(
            raw.decode("utf-8"), object_pairs_hook=_reject_duplicate_keys
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise MapManifestError("manifest could not be read as JSON") from exc
    return _mapping(value, "manifest")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--manifest", type=Path, required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--package", type=Path, required=True)
    return parser


def main() -> int:
    args = _build_parser().parse_args()
    try:
        manifest = _load_manifest(args.manifest)
        if args.command == "validate":
            validate_manifest(manifest)
            print("PASS: valid OTMP0/v0 map package manifest")
        else:
            verify_package(manifest, args.package)
            print("PASS: verified OTMP0/v0 map package")
        return 0
    except MapManifestError as exc:
        print(json.dumps({"error": str(exc), "success": False}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
