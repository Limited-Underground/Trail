from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from map_package_manifest import (  # noqa: E402
    MapManifestError,
    validate_manifest,
    verify_package,
)


PACKAGE_BYTES = b"synthetic OpenTrail map package fixture\n"


def manifest() -> dict:
    return {
        "compatibility": {
            "minimum_firmware": "0.1.0",
            "reader": "pmtiles-3",
            "required_storage_bytes": 4096,
            "scratch_bytes": 1024,
        },
        "content": {
            "byte_length": len(PACKAGE_BYTES),
            "sha256": hashlib.sha256(PACKAGE_BYTES).hexdigest(),
            "tile_count": 1,
        },
        "coverage": {
            "east": -81.0,
            "max_zoom": 14,
            "min_zoom": 8,
            "north": 36.0,
            "south": 35.0,
            "west": -82.0,
        },
        "package": {
            "container": "pmtiles-3",
            "distribution": "local-only",
            "id": "synthetic-map-fixture",
            "projection": "EPSG:3857",
            "revision": "fixture-1",
            "tile_encoding": "jpeg",
            "tile_scheme": "xyz",
        },
        "schema": "OTMP0",
        "source": {
            "attribution_text": "Synthetic test data",
            "attribution_url": "https://example.invalid/attribution",
            "dataset": "Synthetic grid",
            "dataset_revision": "fixture-1",
            "license_id": "CC0-1.0",
            "license_url": "https://creativecommons.org/publicdomain/zero/1.0/",
            "offline_rights_reference": "https://example.invalid/offline-rights",
            "offline_use_permitted": True,
            "redistribution_permitted": True,
            "style_revision": "fixture-1",
        },
        "version": 0,
    }


def expect_error(document: dict, message: str) -> None:
    try:
        validate_manifest(document)
    except MapManifestError:
        return
    raise AssertionError(message)


def test_valid_candidate_matrix() -> None:
    pmtiles = manifest()
    validate_manifest(pmtiles)

    mbtiles = manifest()
    mbtiles["package"]["container"] = "mbtiles-1.3"
    mbtiles["package"]["tile_scheme"] = "tms"
    mbtiles["compatibility"]["reader"] = "mbtiles-1.3"
    validate_manifest(mbtiles)

    indexed = manifest()
    indexed["package"]["container"] = "indexed-raster-v0"
    indexed["package"]["tile_encoding"] = "rgb565"
    indexed["package"]["distribution"] = "redistributable"
    indexed["compatibility"]["reader"] = "indexed-raster-v0"
    validate_manifest(indexed)


def test_shape_version_and_identifiers_fail_closed() -> None:
    value = manifest()
    value["notes"] = "not canonical"
    expect_error(value, "extra top-level fields should fail")

    value = manifest()
    value["version"] = 1
    expect_error(value, "future versions should fail")

    value = manifest()
    value["package"]["id"] = "Private Route Name"
    expect_error(value, "unsafe package identifiers should fail")

    value = manifest()
    value["content"]["byte_length"] = 1 << 63
    expect_error(value, "unbounded package lengths should fail")

    value = manifest()
    value["content"]["tile_count"] = 1 << 32
    expect_error(value, "unbounded tile counts should fail")


def test_coverage_and_zoom_bounds_fail_closed() -> None:
    value = manifest()
    value["coverage"]["west"] = value["coverage"]["east"]
    expect_error(value, "empty longitude coverage should fail")

    value = manifest()
    value["coverage"]["north"] = 90
    expect_error(value, "non-Mercator latitude should fail")

    value = manifest()
    value["coverage"]["max_zoom"] = 23
    expect_error(value, "unsupported zoom should fail")


def test_container_encoding_and_storage_coherence() -> None:
    value = manifest()
    value["package"]["tile_scheme"] = "tms"
    expect_error(value, "wrong container scheme should fail")

    value = manifest()
    value["package"]["tile_encoding"] = "rgb565"
    expect_error(value, "nonstandard PMTiles RGB565 should fail")

    value = manifest()
    value["compatibility"]["required_storage_bytes"] = 1
    expect_error(value, "insufficient declared storage should fail")


def test_rights_attribution_and_public_tile_hosts_fail_closed() -> None:
    value = manifest()
    value["source"]["offline_use_permitted"] = False
    expect_error(value, "missing offline rights should fail")

    value = manifest()
    value["package"]["distribution"] = "redistributable"
    value["source"]["redistribution_permitted"] = False
    expect_error(value, "unlicensed redistribution should fail")

    value = manifest()
    value["source"]["offline_rights_reference"] = (
        "https://tile.openstreetmap.org/8/1/1.png"
    )
    expect_error(value, "public OSM tile service should fail")

    value = manifest()
    value["source"]["offline_rights_reference"] = (
        "https://a.tile.openstreetmap.org/8/1/1.png"
    )
    expect_error(value, "public OSM tile subdomains should fail")

    value = manifest()
    value["source"]["offline_rights_reference"] = (
        "https://example.invalid/offline-rights?token=private"
    )
    expect_error(value, "query-bearing rights references should fail")

    value = manifest()
    value["source"]["attribution_text"] = "<hidden>"
    expect_error(value, "markup attribution should fail")


def test_exact_package_verification_and_mutation_failure() -> None:
    value = manifest()
    with tempfile.TemporaryDirectory() as directory:
        package = Path(directory) / "map.pmtiles"
        package.write_bytes(PACKAGE_BYTES)
        verify_package(value, package)

        package.write_bytes(b"X" + PACKAGE_BYTES[1:])
        try:
            verify_package(value, package)
        except MapManifestError as exc:
            if str(exc) != "package digest does not match manifest":
                raise
        else:
            raise AssertionError("same-length mutation should fail digest")

        package.write_bytes(PACKAGE_BYTES[:-1])
        try:
            verify_package(value, package)
        except MapManifestError as exc:
            if str(exc) != "package byte length does not match manifest":
                raise
        else:
            raise AssertionError("truncated package should fail length")


def test_cli_is_deterministic_and_does_not_echo_paths() -> None:
    tool = PROJECT_ROOT / "tools" / "map_package_manifest.py"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        manifest_path = root / "private-location-name.json"
        package_path = root / "private-location-name.pmtiles"
        manifest_path.write_text(json.dumps(manifest()), encoding="utf-8")
        package_path.write_bytes(PACKAGE_BYTES)

        verified = subprocess.run(
            [
                sys.executable,
                str(tool),
                "verify",
                "--manifest",
                str(manifest_path),
                "--package",
                str(package_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        if verified.returncode != 0 or verified.stdout.strip() != (
            "PASS: verified OTMP0/v0 map package"
        ):
            raise AssertionError("canonical CLI verify should pass")

        invalid = copy.deepcopy(manifest())
        invalid["source"]["offline_use_permitted"] = False
        manifest_path.write_text(json.dumps(invalid), encoding="utf-8")
        rejected = subprocess.run(
            [
                sys.executable,
                str(tool),
                "verify",
                "--manifest",
                str(manifest_path),
                "--package",
                str(package_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        if rejected.returncode == 0:
            raise AssertionError("invalid CLI manifest should fail")
        combined = rejected.stdout + rejected.stderr
        if "private-location-name" in combined or str(root) in combined:
            raise AssertionError("rejected CLI input leaked a path")

        manifest_path.write_text(
            '{"schema":"OTMP0","schema":"OTMP0"}', encoding="utf-8"
        )
        duplicate = subprocess.run(
            [
                sys.executable,
                str(tool),
                "validate",
                "--manifest",
                str(manifest_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        if duplicate.returncode == 0 or "duplicate field" not in duplicate.stdout:
            raise AssertionError("duplicate JSON fields should fail explicitly")

        manifest_path.write_bytes(b" " * (64 * 1024 + 1))
        oversized = subprocess.run(
            [
                sys.executable,
                str(tool),
                "validate",
                "--manifest",
                str(manifest_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        if oversized.returncode == 0 or "size limit" not in oversized.stdout:
            raise AssertionError("oversized manifests should fail before JSON parsing")


def main() -> None:
    test_valid_candidate_matrix()
    test_shape_version_and_identifiers_fail_closed()
    test_coverage_and_zoom_bounds_fail_closed()
    test_container_encoding_and_storage_coherence()
    test_rights_attribution_and_public_tile_hosts_fail_closed()
    test_exact_package_verification_and_mutation_failure()
    test_cli_is_deterministic_and_does_not_echo_paths()
    print("PASS: 7 offline map package manifest scenario groups")


if __name__ == "__main__":
    main()
