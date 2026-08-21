#!/usr/bin/env python3
"""Strict metadata-only validator for OT-105's pinned mbedTLS/PSA lock."""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import re
import sys
import unicodedata
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "tests/benchmarks/crypto/esp_idf/mbedtls_4_1_0"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0.json"
ADMISSION = ROOT / "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
OT096 = ROOT / "tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json"
OT097 = ROOT / "tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json"
OT100 = ROOT / "tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
OT102 = ROOT / "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
OT103 = ROOT / "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json"

SOURCE_MANIFEST_SHA = "ad22dd6aa7d86291918377d3dc76c37b9bcb6c3da0c498c9d302c88b7892fa9c"
LICENSE_SHA = "c32d297c204026621aa462a4465d6adabf7fa31fa19d8f84d0874090cb59e6ce"
SBOM_SHA = "4fc3c63306582732b2628b962352286f256af83036c2c6d65d62f27e0702e9dd"
TRANSITIVE_SHA = "c3587835aae512fe32da9ec2f19df73825020dcbebf74d40e1e2e1a375e43289"
PATCH_SHA = "8fc21a0b4821a0348ea713fd0eca142faa4141b0764b961ddd0a6d5207b34c55"
GLUE_MANIFEST_SHA = "9e06d20b3659cd511009c31da9738477c06ab0cda15a9f0e33461efd7cdb4f1c"
LOCK_SHA = "12f8699d8d286a484e054df186fb0e8c97b75263d23caf4bd77ed48082e9c7ab"
EVIDENCE_SHA = "ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49"
ADMISSION_SHA = "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85"
SOURCE_TREE_SHA = ""  # Derived and checked against the exact evidence/lock value.
GLUE_TREE_SHA = ""  # Derived and checked against the exact lock value.

IDF_COMMIT = "7101770dc6db2667b3c477cc31365dd1acd6db4e"
IDF_TREE = "402f8035c2915b97713251ec036bd6afb457f9fd"
IDF_COMPONENT_TREE = "22c8f32fe2cf02a128c8f7a39363ecf3f70fb9ff"
MBEDTLS_COMMIT = "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5"
MBEDTLS_TREE = "c8766facace97c13f9996d08638dc4ba52f66e4d"
TF_PSA_TREE = "3f133cd7475b00c0f7e7e2f2548d5f64813c17b5"
OT096_RAW = "1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030"
OT096_CANONICAL = "3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e"
OT097_RAW = "d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427"
OT097_POLICY = "51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a"
OT100_RAW = "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0"
OT100_CANONICAL = "9f253738d1766a2d6d273ff5d566bb42c828e6db8cdc3a4b156068f55c07075d"
OT102_RAW = "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52"
OT102_EVIDENCE = "fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f"
OT103_RAW = "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105"
OT103_CANONICAL = "dc7247ae9b277418c104690193fa7bfce2d9297038d2c24c2f5daedf4dc3331e"
LIBSODIUM_EVIDENCE = "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9"
RESULT = (
    "MBEDTLS-PSA-4.1.0-ESP-IDF-GITLINK-SOURCE-DEPENDENCY-LOCK-ADMITTED-"
    "HOST-ONLY-APACHE-2.0; THREE-OTCBR0-REQUIREMENTS-REMAIN; NO-NEW-SOURCE-"
    "ACQUISITION-COPY-API-CONFIG-IMPORT-BUILD-BENCHMARK-OR-SELECTION; "
    "OTCBR0-READINESS-BLOCKED"
)
CURRENT_THREE = [
    "final_candidate_build_configuration_unresolved",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "direct_radio_mtu_phy_region_unresolved",
]
PACKAGE_COUNTS = {
    "mbedtls": 348, "tf-psa-crypto": 587, "framework": 1080,
    "everest": 24, "p256-m": 9, "mldsa-native": 1503,
    "esp-idf-mbedtls-glue": 198,
}
PACKAGE_LICENSES = {
    "mbedtls": "Apache-2.0 OR GPL-2.0-or-later",
    "tf-psa-crypto": "Apache-2.0 OR GPL-2.0-or-later",
    "framework": "Apache-2.0 OR GPL-2.0-or-later",
    "everest": "Apache-2.0",
    "p256-m": "Apache-2.0 OR GPL-2.0-or-later",
    "mldsa-native": "NOASSERTION",
    "esp-idf-mbedtls-glue": "Apache-2.0",
}
PACKAGE_VERSIONS = {
    "mbedtls": "4.1.0", "tf-psa-crypto": "1.1.0",
    "framework": "NOASSERTION", "everest": "NOASSERTION",
    "p256-m": "NOASSERTION", "mldsa-native": "NOASSERTION",
    "esp-idf-mbedtls-glue": "v6.0.2",
}
PACKAGE_PARENTS = {
    "tf-psa-crypto": "mbedtls", "framework": "tf-psa-crypto",
    "everest": "tf-psa-crypto", "p256-m": "tf-psa-crypto",
    "mldsa-native": "tf-psa-crypto",
}
SPDX_EXPRESSIONS = frozenset(
    {
        "NOASSERTION", "Apache-2.0 OR GPL-2.0-or-later",
        "Apache-2.0 OR ISC OR MIT", "Apache-2.0 OR ISC OR MIT-0",
        "Apache-2.0", "BSD-3-Clause", "CC-BY-4.0", "CC0-1.0",
        "LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT", "MIT",
        "MIT-0", "MIT-0 AND Apache-2.0", "Unlicense OR CC0-1.0",
    }
)
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
PRIVATE = (
    re.compile(r"[A-Za-z]:\\"), re.compile(r"/(?:home|users)/", re.I),
    re.compile(r"\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]", re.I),
)
RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{x}" for x in range(1, 10)), *(f"LPT{x}" for x in range(1, 10)),
}
MAX_SMALL = 262_144
MAX_TREE = 1_500_000
MAX_LICENSE = 1_700_000
MAX_SBOM = 3_200_000
MAX_LINE = 4096
MAX_DEPTH = 24
MAX_NODES = 400_000
MAX_STRING = 4096


class AdmissionError(ValueError):
    pass


def _module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def _pairs(items):
    value = {}
    for key, item in items:
        if key in value:
            raise AdmissionError("duplicate key")
        value[key] = item
    return value


def _canonical(value) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, allow_nan=False, sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _same(left, right) -> bool:
    if type(left) is not type(right):
        return False
    if type(left) is dict:
        return set(left) == set(right) and all(_same(left[key], right[key]) for key in left)
    if type(left) is list:
        return len(left) == len(right) and all(_same(a, b) for a, b in zip(left, right))
    return left == right


def _scan(value, depth=0, count=None):
    count = [0] if count is None else count
    count[0] += 1
    if depth > MAX_DEPTH or count[0] > MAX_NODES:
        raise AdmissionError("structure bounds")
    if type(value) is dict:
        for key, item in value.items():
            if type(key) is not str or not key or len(key) > MAX_STRING:
                raise AdmissionError("key bounds")
            _scan(item, depth + 1, count)
    elif type(value) is list:
        for item in value:
            _scan(item, depth + 1, count)
    elif type(value) is str:
        if (
            not value or len(value) > MAX_STRING
            or value != unicodedata.normalize("NFC", value)
            or any(pattern.search(value) for pattern in PRIVATE)
        ):
            raise AdmissionError("text bounds")
    elif value is not None and type(value) not in (bool, int):
        raise AdmissionError("scalar type")


def _bytes(path: Path, expected: str, limit: int) -> bytes:
    digest = hashlib.sha256()
    chunks = []
    size = 0
    with Path(path).open("rb") as stream:
        while True:
            chunk = stream.read(65_536)
            if not chunk:
                break
            size += len(chunk)
            if size > limit:
                raise AdmissionError("byte bound")
            digest.update(chunk)
            chunks.append(chunk)
    raw = b"".join(chunks)
    if not raw or digest.hexdigest() != expected:
        raise AdmissionError("immutable bytes")
    return raw


def _json(path: Path, expected: str, limit=MAX_SMALL):
    raw = _bytes(path, expected, limit)
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except Exception as exc:
        raise AdmissionError("json") from exc
    _scan(value)
    if raw != _canonical(value):
        raise AdmissionError("noncanonical json")
    return value


def _relaxed_json(path: Path, canonical_digest: str):
    raw = Path(path).read_bytes()
    if not raw or len(raw) > MAX_SMALL:
        raise AdmissionError("parent byte bound")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except Exception as exc:
        raise AdmissionError("parent json") from exc
    _scan(value)
    if hashlib.sha256(_canonical(value)).hexdigest() != canonical_digest:
        raise AdmissionError("parent canonical digest")
    return value


def _jsonl(path: Path, expected: str, *, max_bytes: int, max_lines: int):
    digest = hashlib.sha256()
    rows = []
    total = 0
    with Path(path).open("rb") as stream:
        for line in stream:
            total += len(line)
            if total > max_bytes or len(line) > MAX_LINE or len(rows) >= max_lines:
                raise AdmissionError("jsonl bounds")
            digest.update(line)
            if not line.endswith(b"\n") or b"\r" in line or line.startswith(b"\xef\xbb\xbf"):
                raise AdmissionError("jsonl encoding")
            raw = line[:-1]
            try:
                value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
            except Exception as exc:
                raise AdmissionError("jsonl") from exc
            _scan(value)
            if raw != _canonical(value):
                raise AdmissionError("noncanonical jsonl")
            rows.append(value)
    if not rows or digest.hexdigest() != expected:
        raise AdmissionError("jsonl digest")
    return rows


def _keys(value, expected, name):
    if type(value) is not dict or set(value) != set(expected):
        raise AdmissionError(name + " fields")


def _integer(value, minimum=0):
    return type(value) is int and value >= minimum


def _safe(path):
    if (
        type(path) is not str or not path or path.startswith("/") or "\\" in path
        or ":" in path or "//" in path or path != unicodedata.normalize("NFC", path)
    ):
        raise AdmissionError("unsafe path")
    for part in path.split("/"):
        if (
            part in ("", ".", "..") or part != part.strip()
            or part.endswith((".", " ")) or part.split(".", 1)[0].upper() in RESERVED
            or any(unicodedata.category(char).startswith("C") for char in part)
        ):
            raise AdmissionError("unsafe path")
    return path


def _tree(path: Path, digest: str, count: int, files: int, directories: int, total_bytes: int, prefix: bytes):
    rows = _jsonl(path, digest, max_bytes=MAX_TREE, max_lines=count)
    if len(rows) != count or [row.get("path") for row in rows] != sorted(
        (row.get("path") for row in rows), key=lambda item: item.encode("utf-8")
    ):
        raise AdmissionError("tree order/count")
    seen = set()
    folds = set()
    file_rows = {}
    directory_count = 0
    byte_count = 0
    package_count = {name: 0 for name in PACKAGE_COUNTS}
    for row in rows:
        rel = _safe(row.get("path"))
        folded = rel.casefold()
        if rel in seen or folded in folds:
            raise AdmissionError("tree collision")
        seen.add(rel)
        folds.add(folded)
        if row.get("kind") == "directory":
            _keys(row, {"kind", "path"}, "directory")
            directory_count += 1
            continue
        _keys(row, {"bytes", "git_blob", "kind", "mode", "package_id", "path", "sha256"}, "file")
        if (
            row["kind"] != "regular_file" or not _integer(row["bytes"])
            or row["mode"] not in ("100644", "100755")
            or type(row["git_blob"]) is not str or not HEX40.fullmatch(row["git_blob"])
            or type(row["sha256"]) is not str or not HEX64.fullmatch(row["sha256"])
            or row["package_id"] not in PACKAGE_COUNTS
        ):
            raise AdmissionError("file facts")
        file_rows[rel] = row
        byte_count += row["bytes"]
        package_count[row["package_id"]] += 1
    if (
        len(file_rows) != files or directory_count != directories
        or byte_count != total_bytes or any(
            package_count[name] != expected
            for name, expected in PACKAGE_COUNTS.items()
            if package_count[name]
        )
    ):
        raise AdmissionError("tree facts")
    domain = hashlib.sha256(prefix + Path(path).read_bytes()).hexdigest()
    return file_rows, domain


def _validate_layers():
    source, source_tree_sha = _tree(
        BUNDLE / "source-tree.jsonl", SOURCE_MANIFEST_SHA,
        4122, 3551, 571, 45_182_598,
        b"OTCSL0/v1\0esp-idf-mbedtls-psa-full-source-tree\0",
    )
    glue, glue_tree_sha = _tree(
        BUNDLE / "component-glue-tree.jsonl", GLUE_MANIFEST_SHA,
        243, 198, 45, 1_737_063,
        b"OTCSL0/v1\0esp-idf-mbedtls-component-glue-tree\0",
    )
    expected = {"source/" + key: value for key, value in source.items()}
    expected.update({"component-glue/" + key: value for key, value in glue.items()})
    licenses = _jsonl(
        BUNDLE / "license-inventory.jsonl", LICENSE_SHA,
        max_bytes=MAX_LICENSE, max_lines=3749,
    )
    if len(licenses) != 3749 or [row.get("path") for row in licenses] != sorted(expected, key=lambda item: item.encode("utf-8")):
        raise AdmissionError("license coverage/order")
    for row in licenses:
        _keys(row, {"declared_expression", "detection", "git_blob", "license_concluded", "package_id", "path", "sha256"}, "license")
        file_row = expected.get(_safe(row["path"]))
        if (
            file_row is None or row["declared_expression"] not in SPDX_EXPRESSIONS
            or row["detection"] not in ("spdx-header", "no-file-spdx-header")
            or (row["declared_expression"] == "NOASSERTION") is not (row["detection"] == "no-file-spdx-header")
            or row["license_concluded"] != "NOASSERTION"
            or row["package_id"] != file_row["package_id"]
            or row["git_blob"] != file_row["git_blob"] or row["sha256"] != file_row["sha256"]
        ):
            raise AdmissionError("license binding")
    dependencies = _jsonl(
        BUNDLE / "transitive-dependencies.jsonl", TRANSITIVE_SHA,
        max_bytes=MAX_SMALL, max_lines=7,
    )
    expected_dependencies = [
        {
            "bundled_source_partition": True,
            "declared_package_expression": PACKAGE_LICENSES[name],
            "file_count": count, "package_id": name,
            "partition_kind": "most-specific-prefix-exactly-once-v1",
            "runtime_or_link_dependency": False,
            "scope": "component-integration" if name == "esp-idf-mbedtls-glue" else "pinned-source",
        }
        for name, count in PACKAGE_COUNTS.items()
    ]
    if not _same(dependencies, expected_dependencies):
        raise AdmissionError("dependency partitions")
    patches = _jsonl(BUNDLE / "patches.jsonl", PATCH_SHA, max_bytes=MAX_SMALL, max_lines=1)
    expected_patch = {
        "artifact_id": "candidate-patch-set", "patch_count": 0,
        "patch_count_scope": "opentrail-post-pinned-esp-idf-gitlink-only",
        "pinned_baseline_commit": MBEDTLS_COMMIT,
        "pinned_baseline_tree": MBEDTLS_TREE,
        "post_patch_tree_sha256": source_tree_sha,
        "upstream_or_espressif_divergence_assessed": False,
    }
    if not _same(patches, [expected_patch]):
        raise AdmissionError("patch boundary")
    sbom = _json(BUNDLE / "sbom.spdx.json", SBOM_SHA, MAX_SBOM)
    _keys(sbom, {"SPDXID", "comment", "creationInfo", "dataLicense", "documentDescribes", "documentNamespace", "files", "hasExtractedLicensingInfos", "name", "packages", "relationships", "spdxVersion"}, "sbom")
    if sbom["spdxVersion"] != "SPDX-2.3" or len(sbom["packages"]) != 7 or len(sbom["files"]) != 3749 or len(sbom["relationships"]) != 3757:
        raise AdmissionError("sbom counts")
    package_ids = {"SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", name): name for name in PACKAGE_COUNTS}
    if [row.get("SPDXID") for row in sbom["packages"]] != list(package_ids):
        raise AdmissionError("sbom packages")
    spdx_ids = {}
    sha1_by_package = {name: [] for name in PACKAGE_COUNTS}
    for row in sbom["files"]:
        _keys(row, {"SPDXID", "checksums", "comment", "copyrightText", "fileName", "licenseConcluded", "licenseInfoInFiles"}, "sbom file")
        logical = row["fileName"].removeprefix("./")
        file_row = expected.get(logical)
        checks = {item.get("algorithm"): item.get("checksumValue") for item in row["checksums"]}
        if (
            file_row is None or logical in spdx_ids.values() or set(checks) != {"SHA1", "SHA256"}
            or checks["SHA256"] != file_row["sha256"] or not HEX40.fullmatch(checks["SHA1"] or "")
            or row["licenseConcluded"] != "NOASSERTION"
        ):
            raise AdmissionError("sbom file binding")
        spdx_ids[row["SPDXID"]] = logical
        sha1_by_package[file_row["package_id"]].append(checks["SHA1"])
    if set(spdx_ids.values()) != set(expected):
        raise AdmissionError("sbom file coverage")
    for package in sbom["packages"]:
        name = package_ids[package["SPDXID"]]
        code = hashlib.sha1("".join(sorted(sha1_by_package[name])).encode("ascii")).hexdigest()
        if (
            package.get("licenseConcluded") != "NOASSERTION"
            or package.get("licenseDeclared") != PACKAGE_LICENSES[name]
            or package.get("versionInfo") != PACKAGE_VERSIONS[name]
            or package.get("packageVerificationCode", {}).get("packageVerificationCodeValue") != code
        ):
            raise AdmissionError("sbom package binding")
    containment = {
        (row.get("spdxElementId"), row.get("relatedSpdxElement"))
        for row in sbom["relationships"] if row.get("relationshipType") == "CONTAINS"
    }
    package_containment = {
        (left, right) for left, right in containment
        if left in package_ids and right in package_ids
    }
    expected_package_containment = {
        (
            "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", parent),
            "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", child),
        )
        for child, parent in PACKAGE_PARENTS.items()
    }
    if package_containment != expected_package_containment:
        raise AdmissionError("sbom package hierarchy")
    for spdx_id, logical in spdx_ids.items():
        package = expected[logical]["package_id"]
        if ("SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", package), spdx_id) not in containment:
            raise AdmissionError("sbom containment")
    return source, glue, source_tree_sha, glue_tree_sha, sbom, dependencies


def _validate_lock(source_tree_sha, glue_tree_sha):
    lock = _json(BUNDLE / "project-lock.json", LOCK_SHA)
    if (
        lock.get("schema") != "OTMPSL0" or type(lock.get("version")) is not int
        or lock["version"] != 0 or lock.get("candidate_id") != "esp_idf_mbedtls_psa"
        or lock.get("lock_kind") != "esp_idf_gitlink_dependency_lock"
        or lock.get("locked_date") != "2026-08-21"
        or lock.get("source") != {"commit": MBEDTLS_COMMIT, "tf_psa_tree": TF_PSA_TREE, "tree": MBEDTLS_TREE, "version": "4.1.0"}
    ):
        raise AdmissionError("lock identity")
    parent = lock.get("parent_idf", {})
    if (
        parent.get("commit") != IDF_COMMIT or parent.get("tree") != IDF_TREE
        or parent.get("component_tree") != IDF_COMPONENT_TREE
        or parent.get("gitlink_path") != "components/mbedtls/mbedtls"
        or parent.get("gitlink_commit") != MBEDTLS_COMMIT
        or parent.get("component_glue_tree_sha256") != glue_tree_sha
    ):
        raise AdmissionError("lock parent binding")
    evidence = lock.get("evidence", {})
    expected_digests = {
        "full_tree": SOURCE_MANIFEST_SHA, "component_glue": GLUE_MANIFEST_SHA,
        "license": LICENSE_SHA, "patches": PATCH_SHA, "sbom": SBOM_SHA,
        "transitive": TRANSITIVE_SHA,
    }
    if set(evidence) != set(expected_digests) or any(evidence[name].get("sha256") != digest for name, digest in expected_digests.items()):
        raise AdmissionError("lock evidence")
    patch_evidence = evidence["patches"]
    if not _same(
        {
            "patch_count": patch_evidence.get("patch_count"),
            "patch_count_scope": patch_evidence.get("patch_count_scope"),
            "pinned_baseline_commit": patch_evidence.get("pinned_baseline_commit"),
            "pinned_baseline_tree": patch_evidence.get("pinned_baseline_tree"),
            "upstream_or_espressif_divergence_assessed": patch_evidence.get(
                "upstream_or_espressif_divergence_assessed"
            ),
        },
        {
            "patch_count": 0,
            "patch_count_scope": "opentrail-post-pinned-esp-idf-gitlink-only",
            "pinned_baseline_commit": MBEDTLS_COMMIT,
            "pinned_baseline_tree": MBEDTLS_TREE,
            "upstream_or_espressif_divergence_assessed": False,
        },
    ):
        raise AdmissionError("lock patch scope")
    transitive_evidence = evidence["transitive"]
    if (
        type(transitive_evidence.get("partition_count")) is not int
        or transitive_evidence["partition_count"] != 7
        or "dependency_count" in transitive_evidence
    ):
        raise AdmissionError("lock partition count")
    if evidence["full_tree"].get("tree_sha256") != source_tree_sha or evidence["component_glue"].get("tree_sha256") != glue_tree_sha:
        raise AdmissionError("lock tree digest")
    boundaries = lock.get("boundaries")
    if type(boundaries) is not dict or not boundaries or any(type(value) is not bool or value for value in boundaries.values()):
        raise AdmissionError("lock boundaries")
    license_boundary = lock.get("license_boundary", {})
    expected_notices = [
        {"bytes": 29852, "git_blob": "776ac77eaf5a0da545f95ad7386b9954378fa8ae", "path": "source/LICENSE", "sha256": "9b405ef4c89342f5eae1dd828882f931747f71001cfba7d114801039b52ad09b"},
        {"bytes": 29856, "git_blob": "4c47cb2a5a526a482b939705c41b8610c4e59205", "path": "source/tf-psa-crypto/LICENSE", "sha256": "da8c58f05f135a9d15e9ffad4ecf854cfcc1f014c8abfd75ba05f62630ccc118"},
        {"bytes": 29861, "git_blob": "75e3d54f39922255ebc6199aea111be094d28b5a", "path": "source/tf-psa-crypto/framework/LICENSE", "sha256": "11402351e38392230bb8934ba1095c0c0049a296c0f8821f76e4672dff54b490"},
        {"bytes": 15310, "git_blob": "5fd3c1b13522c1ddb995a3c71b0c9b8a59df2c07", "path": "source/tf-psa-crypto/drivers/pqcp/mldsa-native/LICENSE", "sha256": "26ee68617256be39e12fbbb0fb0c8c7087980a2020ded46e3ef3c3810c35d847"},
        {"bytes": 1138, "git_blob": "35741e52a84345968ae458221233faf0fd24e30c", "path": "source/tf-psa-crypto/drivers/pqcp/mldsa-native/examples/custom_backend/mldsa_native/src/fips202/native/custom/src/LICENSE", "sha256": "c5ebc5c092628cbb9018d4b73d7330ffc84e3f44576c1bddcd884c3e5158a18c"},
    ]
    expected_parent_notice = {
        "bytes": 11358, "git_blob": "d645695673349e3947e8e5ae42332d0ac3164cd7",
        "path": "LICENSE", "sha256": "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
    }
    if (
        license_boundary.get("upstream_license_expression") != "Apache-2.0 OR GPL-2.0-or-later"
        or license_boundary.get("project_license_choice") != "Apache-2.0"
        or license_boundary.get("inventory_complete") is not True
        or license_boundary.get("embedded_exceptions_flattened") is not False
        or license_boundary.get("license_compatibility_determined") is not False
        or not _same(license_boundary.get("license_notices"), expected_notices)
        or not _same(parent.get("license_notice"), expected_parent_notice)
    ):
        raise AdmissionError("lock license boundary")
    return lock


def _validated_otcsle(evidence_path=EVIDENCE):
    contract = _relaxed_json(OT097, OT097_POLICY)
    evidence = _json(Path(evidence_path), EVIDENCE_SHA)
    source_lock = _module("ot105_otcsl", ROOT / "tools/crypto_candidate_source_lock.py")
    base = source_lock.validate_contract(contract)
    if base["admission_policy_sha256"] != OT097_POLICY:
        raise AdmissionError("policy validation")
    future = copy.deepcopy(contract)
    future["accepted_source_evidence_sha256"]["esp_idf_mbedtls_psa"] = [EVIDENCE_SHA]
    old_digest = source_lock.EXPECTED_V1_CONTRACT_SHA256
    old_anchor = source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"]
    try:
        source_lock.EXPECTED_V1_CONTRACT_SHA256 = source_lock.canonical_sha256(future)
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = frozenset({EVIDENCE_SHA})
        facts = source_lock.validate_source_evidence(evidence, future)
    except Exception as exc:
        raise AdmissionError("OTCSLE0/v1 validation") from exc
    finally:
        source_lock.EXPECTED_V1_CONTRACT_SHA256 = old_digest
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = old_anchor
    return evidence, facts


def _validate_parents():
    ot096 = _relaxed_json(OT096, OT096_CANONICAL)
    _relaxed_json(OT100, OT100_CANONICAL)
    ot102 = _relaxed_json(OT102, OT102_RAW)
    ot103 = _relaxed_json(OT103, OT103_CANONICAL)
    if (
        ot096.get("provenance", {}).get("esp_idf", {}).get("source_commit") != IDF_COMMIT
        or ot096.get("provenance", {}).get("mbedtls", {}).get("source_commit") != MBEDTLS_COMMIT
        or ot096.get("summary", {}).get("source_lock_accepted") is not False
        or ot102.get("accepted_source_evidence_sha256", {}).get("monocypher") != [OT102_EVIDENCE]
        or ot103.get("current_three_blockers") != CURRENT_THREE
    ):
        raise AdmissionError("parent semantics")


def validate(admission_path=ADMISSION, *, enforce_digest=True):
    admission_path = Path(admission_path)
    if enforce_digest:
        admission = _json(admission_path, ADMISSION_SHA)
    else:
        raw = admission_path.read_bytes()
        if not raw or len(raw) > MAX_SMALL:
            raise AdmissionError("admission bounds")
        try:
            admission = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
        except Exception as exc:
            raise AdmissionError("admission json") from exc
        _scan(admission)
        expected = _json(ADMISSION, ADMISSION_SHA)
        if not _same(admission, expected):
            raise AdmissionError("admission semantic mismatch")
    _validate_parents()
    source, glue, source_tree_sha, glue_tree_sha, sbom, dependencies = _validate_layers()
    lock = _validate_lock(source_tree_sha, glue_tree_sha)
    evidence, facts = _validated_otcsle()
    if (
        admission.get("schema") != "OTMPSLA0" or type(admission.get("version")) is not int
        or admission["version"] != 0 or admission.get("public_result") != RESULT
        or admission.get("acceptance_counts") != {"api_config": 0, "candidate_import": 0, "source": 3}
        or admission.get("prior_current_three_blockers") != CURRENT_THREE
        or admission.get("current_three_blockers") != CURRENT_THREE
        or admission.get("source_evidence") != {
            "path": "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0.json",
            "sha256": EVIDENCE_SHA,
        }
    ):
        raise AdmissionError("admission semantics")
    parents = {
        "otcmse0_v0_canonical_sha256": OT096_CANONICAL, "otcmse0_v0_raw_sha256": OT096_RAW,
        "otcsla0_v0_raw_sha256": OT100_RAW, "otcsl0_v1_policy_sha256": OT097_POLICY,
        "otcsl0_v1_raw_sha256": OT097_RAW, "otmsla0_v0_raw_sha256": OT102_RAW,
        "otrtpa0_v0_raw_sha256": OT103_RAW,
    }
    if admission.get("parents") != parents:
        raise AdmissionError("admission parent binding")
    sources = admission.get("accepted_source_evidence_sha256")
    if sources != {
        "esp_idf_mbedtls_psa": [EVIDENCE_SHA],
        "espressif_libsodium": [LIBSODIUM_EVIDENCE], "monocypher": [OT102_EVIDENCE],
    }:
        raise AdmissionError("source anchors")
    empty = {"esp_idf_mbedtls_psa": [], "espressif_libsodium": [], "monocypher": []}
    if admission.get("accepted_api_config_evidence_sha256") != empty or admission.get("accepted_candidate_import_evidence_sha256") != empty:
        raise AdmissionError("api/import anchors")
    claims = admission.get("claims", {})
    true_claims = {
        "dependency_lock_accepted", "libsodium_source_lock_remains_accepted",
        "mbedtls_psa_source_lock_accepted", "monocypher_source_lock_remains_accepted",
    }
    if any(type(value) is not bool or value is not (name in true_claims) for name, value in claims.items()) or set(claims) != {
        "api_config_eligibility_proven", "benchmark_executed", "candidate_import_accepted",
        "candidate_selected", "dependency_lock_accepted", "final_candidate_configuration_proven",
        "hardware_or_device_accessed", "libsodium_source_lock_remains_accepted",
        "mbedtls_psa_source_lock_accepted", "monocypher_source_lock_remains_accepted",
        "packet_v1_wire_selected", "physical_evidence_added", "readiness_accepted",
        "score_credit_added", "source_acquired_or_copied", "suite_selected",
    }:
        raise AdmissionError("claims")
    for section in ("authority", "license_claims"):
        values = admission.get(section, {})
        if not values or any(type(value) is not bool or value for value in values.values()):
            raise AdmissionError(section)
    candidate = admission.get("accepted_candidate", {})
    if (
        candidate.get("source_evidence_sha256") != EVIDENCE_SHA
        or candidate.get("project_dependency_lock_sha256") != LOCK_SHA
        or candidate.get("component_glue_manifest_sha256") != GLUE_MANIFEST_SHA
        or candidate.get("source_commit") != MBEDTLS_COMMIT
        or candidate.get("tree_sha256") != source_tree_sha
        or facts.get("source_lock_accepted") is not True
        or facts.get("import_authorized") is not False
    ):
        raise AdmissionError("accepted candidate")
    return {
        "schema": "OTMPSLA0", "version": 0, "public_result": RESULT,
        "source_file_count": len(source), "component_glue_file_count": len(glue),
        "license_file_count": len(source) + len(glue),
        "sbom_component_count": len(sbom["packages"]),
        "transitive_partition_count": len(dependencies),
        "source_tree_sha256": source_tree_sha, "component_glue_tree_sha256": glue_tree_sha,
        "project_dependency_lock_sha256": hashlib.sha256(_canonical(lock)).hexdigest(),
        "source_evidence_sha256": EVIDENCE_SHA, "acceptance_counts": admission["acceptance_counts"],
        "current_blocker_count": 3, "readiness_advanced": False,
        "execution_authorized": False, "score_credit_added": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("admission", type=Path, nargs="?", default=ADMISSION)
    args = parser.parse_args(argv)
    try:
        result = validate(args.admission)
    except (OSError, AdmissionError, KeyError, TypeError, UnicodeError, RecursionError, json.JSONDecodeError):
        print("OTMPSLA0 validation failed", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
