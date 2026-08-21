#!/usr/bin/env python3
"""Generate the metadata-only OT-105 mbedTLS/PSA source dependency lock."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import unicodedata
from pathlib import Path


DATE = "2026-08-21"
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
OT102_RAW = "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52"
OT102_EVIDENCE = "fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f"
OT103_RAW = "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105"
LIBSODIUM_EVIDENCE = "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9"
RESULT = (
    "MBEDTLS-PSA-4.1.0-ESP-IDF-GITLINK-SOURCE-DEPENDENCY-LOCK-ADMITTED-"
    "HOST-ONLY-APACHE-2.0; THREE-OTCBR0-REQUIREMENTS-REMAIN; NO-NEW-SOURCE-"
    "ACQUISITION-COPY-API-CONFIG-IMPORT-BUILD-BENCHMARK-OR-SELECTION; "
    "OTCBR0-READINESS-BLOCKED"
)
UPSTREAM_LICENSE = "Apache-2.0 OR GPL-2.0-or-later"
PROJECT_LICENSE = "Apache-2.0"
ROOT = "tests/benchmarks/crypto/esp_idf/mbedtls_4_1_0/"
RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{value}" for value in range(1, 10)),
    *(f"LPT{value}" for value in range(1, 10)),
}
SPDX_EXPRESSIONS = frozenset(
    {
        "Apache-2.0 OR GPL-2.0-or-later",
        "Apache-2.0 OR ISC OR MIT",
        "Apache-2.0 OR ISC OR MIT-0",
        "Apache-2.0",
        "BSD-3-Clause",
        "CC-BY-4.0",
        "CC0-1.0",
        "LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT",
        "MIT",
        "MIT-0",
        "MIT-0 AND Apache-2.0",
        "Unlicense OR CC0-1.0",
    }
)
PARTITIONS = (
    ("mldsa-native", "tf-psa-crypto/drivers/pqcp/mldsa-native/"),
    ("p256-m", "tf-psa-crypto/drivers/p256-m/"),
    ("everest", "tf-psa-crypto/drivers/everest/"),
    ("framework", "tf-psa-crypto/framework/"),
    ("tf-psa-crypto", "tf-psa-crypto/"),
)
PACKAGE_LICENSES = {
    "mbedtls": UPSTREAM_LICENSE,
    "tf-psa-crypto": UPSTREAM_LICENSE,
    "framework": UPSTREAM_LICENSE,
    "everest": "Apache-2.0",
    "p256-m": UPSTREAM_LICENSE,
    "mldsa-native": "NOASSERTION",
    "esp-idf-mbedtls-glue": "Apache-2.0",
}
PACKAGE_VERSIONS = {
    "mbedtls": "4.1.0",
    "tf-psa-crypto": "1.1.0",
    "framework": "NOASSERTION",
    "everest": "NOASSERTION",
    "p256-m": "NOASSERTION",
    "mldsa-native": "NOASSERTION",
    "esp-idf-mbedtls-glue": "v6.0.2",
}
PACKAGE_ORDER = (
    "mbedtls", "tf-psa-crypto", "framework", "everest", "p256-m",
    "mldsa-native", "esp-idf-mbedtls-glue",
)
AUTHORITY_FIELDS = (
    "dependency_acquisition_authorized", "candidate_import_authorized",
    "benchmark_build_authorized", "benchmark_execution_authorized",
    "device_access_authorized", "radio_transmit_authorized",
    "key_or_entropy_operation_authorized", "suite_selection_authorized",
    "packet_v1_authorized", "score_credit_added",
)
CLAIM_FIELDS = (
    "source_acquired", "source_lock_accepted", "candidate_imported",
    "api_config_eligibility_proven", "candidate_benchmark_executed",
    "candidate_selected", "suite_selected", "packet_v1_wire_selected",
    "hardware_or_device_accessed", "physical_evidence_added",
    "score_credit_added",
)


def canonical(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, allow_nan=False, sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def write_json(path: Path, value: object) -> str:
    raw = canonical(value)
    path.write_bytes(raw)
    return sha256(raw)


def write_jsonl(path: Path, rows: list[dict]) -> str:
    raw = b"".join(canonical(row) + b"\n" for row in rows)
    path.write_bytes(raw)
    return sha256(raw)


def git(repo: Path, *args: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(repo), *args], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def git_text(repo: Path, *args: str) -> str:
    return git(repo, *args).decode("utf-8").rstrip("\n")


def safe_path(path: str) -> None:
    if (
        not path or path.startswith("/") or "\\" in path or ":" in path
        or "//" in path or path != unicodedata.normalize("NFC", path)
        or any(unicodedata.category(char).startswith("C") for char in path)
    ):
        raise SystemExit("unsafe path")
    for part in path.split("/"):
        if (
            part in ("", ".", "..") or part != part.strip()
            or part.endswith((".", " "))
            or part.split(".", 1)[0].upper() in RESERVED
        ):
            raise SystemExit("unsafe path")


def ls_tree(repo: Path, *tree_args: str) -> list[dict]:
    raw = git(repo, "ls-tree", "-r", "-z", "--full-tree", *tree_args)
    rows = []
    for entry in raw.split(b"\0"):
        if not entry:
            continue
        left, path_raw = entry.split(b"\t", 1)
        mode, kind, oid = left.decode("ascii").split(" ")
        path = path_raw.decode("utf-8")
        safe_path(path)
        rows.append({"mode": mode, "kind": kind, "git_blob": oid, "path": path})
    return rows


def blob_bytes(repo: Path, rows: list[dict]) -> dict[str, bytes]:
    request = b"".join((row["git_blob"] + "\n").encode("ascii") for row in rows)
    response = subprocess.run(
        ["git", "-C", str(repo), "cat-file", "--batch"],
        input=request, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout
    if len(response) > 64 * 1024 * 1024:
        raise SystemExit("Git object batch exceeds candidate-specific bound")
    result = {}
    offset = 0
    for row in rows:
        newline = response.find(b"\n", offset)
        if newline < 0:
            raise SystemExit("truncated Git object header")
        oid, kind, size_text = response[offset:newline].decode("ascii").split(" ")
        size = int(size_text)
        offset = newline + 1
        raw = response[offset:offset + size]
        offset += size
        if (
            oid != row["git_blob"] or kind != "blob" or len(raw) != size
            or response[offset:offset + 1] != b"\n"
        ):
            raise SystemExit("unexpected or truncated Git object")
        offset += 1
        result[row["path"]] = raw
    if offset != len(response):
        raise SystemExit("unexpected trailing Git object data")
    return result


def directories(paths: list[str]) -> list[str]:
    found = set()
    for value in paths:
        parent = Path(value).parent
        while parent != Path("."):
            found.add(parent.as_posix())
            parent = parent.parent
    return sorted(found, key=lambda item: item.encode("utf-8"))


def partition(path: str) -> str:
    for name, prefix in PARTITIONS:
        if path.startswith(prefix):
            return name
    return "mbedtls"


def spdx_expression(raw: bytes) -> tuple[str, str]:
    text = raw[:8192].decode("utf-8", "ignore")
    for line in text.splitlines()[:80]:
        match = re.search(r"SPDX-License-Identifier:\s*([^*<\r\n\"]+)", line)
        if match is None:
            continue
        if any(char.isalnum() for char in line[:match.start()]):
            continue
        value = match.group(1).strip().rstrip("),;").replace(" -->", "")
        if value in SPDX_EXPRESSIONS:
            return value, "spdx-header"
    return "NOASSERTION", "no-file-spdx-header"


def tree_rows(
    files: list[dict], blobs: dict[str, bytes], *, glue: bool,
) -> tuple[list[dict], list[dict], int]:
    file_rows = []
    for source in files:
        rel = source["path"]
        raw = blobs[rel]
        package_id = "esp-idf-mbedtls-glue" if glue else partition(rel)
        expression, detection = spdx_expression(raw)
        logical = ("component-glue/" if glue else "source/") + rel
        file_rows.append(
            {
                "bytes": len(raw), "declared_expression": expression,
                "detection": detection, "git_blob": source["git_blob"],
                "kind": "regular_file", "logical_path": logical,
                "mode": source["mode"], "package_id": package_id,
                "path": rel, "sha256": sha256(raw),
            }
        )
    directory_rows = [
        {"kind": "directory", "path": path}
        for path in directories([row["path"] for row in file_rows])
    ]
    manifest_rows = sorted(
        directory_rows
        + [
            {key: row[key] for key in (
                "bytes", "git_blob", "kind", "mode", "package_id", "path", "sha256"
            )}
            for row in file_rows
        ],
        key=lambda item: item["path"].encode("utf-8"),
    )
    return manifest_rows, file_rows, sum(row["bytes"] for row in file_rows)


def verification_code(rows: list[dict]) -> str:
    values = sorted(hashlib.sha1(row["raw"]).hexdigest() for row in rows)
    return hashlib.sha1("".join(values).encode("ascii")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("idf", type=Path)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("source_evidence", type=Path)
    parser.add_argument("admission", type=Path)
    args = parser.parse_args()
    idf = args.idf.resolve()
    mbedtls = idf / "components" / "mbedtls" / "mbedtls"
    if any(path.exists() for path in (args.bundle, args.source_evidence, args.admission)):
        raise SystemExit("output already exists")
    checks = (
        (git_text(idf, "rev-parse", "HEAD"), IDF_COMMIT),
        (git_text(idf, "rev-parse", "HEAD^{tree}"), IDF_TREE),
        (git_text(idf, "rev-parse", "HEAD:components/mbedtls"), IDF_COMPONENT_TREE),
        (git_text(mbedtls, "rev-parse", "HEAD"), MBEDTLS_COMMIT),
        (git_text(mbedtls, "rev-parse", "HEAD^{tree}"), MBEDTLS_TREE),
        (git_text(mbedtls, "rev-parse", "HEAD:tf-psa-crypto"), TF_PSA_TREE),
    )
    if any(actual != expected for actual, expected in checks):
        raise SystemExit("unexpected source identity")
    if git_text(idf, "status", "--porcelain=v1", "--untracked-files=all"):
        raise SystemExit("dirty ESP-IDF source")
    if git_text(mbedtls, "status", "--porcelain=v1", "--untracked-files=all"):
        raise SystemExit("dirty mbedTLS source")
    if not git_text(idf, "submodule", "status", "components/mbedtls/mbedtls").startswith(
        " " + MBEDTLS_COMMIT
    ):
        raise SystemExit("unexpected mbedTLS submodule state")

    source_entries = ls_tree(mbedtls, "HEAD")
    if any(row["kind"] != "blob" or row["mode"] not in ("100644", "100755") for row in source_entries):
        raise SystemExit("unexpected mbedTLS entry")
    glue_all = ls_tree(idf, "HEAD", "components/mbedtls")
    gitlinks = [row for row in glue_all if row["kind"] == "commit"]
    glue_entries = [row for row in glue_all if row["kind"] == "blob"]
    if gitlinks != [{
        "mode": "160000", "kind": "commit", "git_blob": MBEDTLS_COMMIT,
        "path": "components/mbedtls/mbedtls",
    }]:
        raise SystemExit("unexpected component gitlink")
    if any(row["mode"] not in ("100644", "100755") for row in glue_entries):
        raise SystemExit("unexpected component glue entry")
    for row in glue_entries:
        row["path"] = row["path"].removeprefix("components/mbedtls/")
        safe_path(row["path"])
    if len(source_entries) != 3551 or len(glue_entries) != 198:
        raise SystemExit("unexpected exact file count")

    source_blobs = blob_bytes(mbedtls, source_entries)
    raw_glue_entries = [dict(row, path="components/mbedtls/" + row["path"]) for row in glue_entries]
    raw_glue_blobs = blob_bytes(idf, raw_glue_entries)
    glue_blobs = {
        path.removeprefix("components/mbedtls/"): raw
        for path, raw in raw_glue_blobs.items()
    }
    source_manifest, source_files, source_bytes = tree_rows(
        source_entries, source_blobs, glue=False
    )
    glue_manifest, glue_files, glue_bytes = tree_rows(
        glue_entries, glue_blobs, glue=True
    )
    if (len(source_manifest), source_bytes) != (4122, 45_182_598):
        raise SystemExit("unexpected source manifest facts")
    if b"set(TF_PSA_CRYPTO_VERSION 1.1.0)" not in source_blobs["tf-psa-crypto/CMakeLists.txt"]:
        raise SystemExit("unexpected TF-PSA-Crypto version anchor")
    if (len(glue_manifest), glue_bytes) != (243, 1_737_063):
        raise SystemExit("unexpected glue manifest facts")
    all_files = source_files + glue_files
    if len({row["logical_path"] for row in all_files}) != 3749:
        raise SystemExit("file coverage collision")
    package_rows = {name: [] for name in PACKAGE_ORDER}
    for row in all_files:
        package_rows[row["package_id"]].append(row)
    if any(not package_rows[name] for name in PACKAGE_ORDER):
        raise SystemExit("empty package partition")

    args.bundle.mkdir(parents=True)
    source_manifest_sha = write_jsonl(args.bundle / "source-tree.jsonl", source_manifest)
    source_tree_sha = sha256(
        b"OTCSL0/v1\0esp-idf-mbedtls-psa-full-source-tree\0"
        + (args.bundle / "source-tree.jsonl").read_bytes()
    )
    glue_manifest_sha = write_jsonl(
        args.bundle / "component-glue-tree.jsonl", glue_manifest
    )
    glue_tree_sha = sha256(
        b"OTCSL0/v1\0esp-idf-mbedtls-component-glue-tree\0"
        + (args.bundle / "component-glue-tree.jsonl").read_bytes()
    )
    license_rows = [
        {
            "declared_expression": row["declared_expression"],
            "detection": row["detection"], "git_blob": row["git_blob"],
            "license_concluded": "NOASSERTION", "package_id": row["package_id"],
            "path": row["logical_path"], "sha256": row["sha256"],
        }
        for row in sorted(all_files, key=lambda item: item["logical_path"].encode("utf-8"))
    ]
    license_sha = write_jsonl(args.bundle / "license-inventory.jsonl", license_rows)
    transitive_rows = [
        {
            "bundled_source_partition": True,
            "declared_package_expression": PACKAGE_LICENSES[name],
            "file_count": len(package_rows[name]), "package_id": name,
            "partition_kind": "most-specific-prefix-exactly-once-v1",
            "runtime_or_link_dependency": False,
            "scope": "component-integration" if name == "esp-idf-mbedtls-glue" else "pinned-source",
        }
        for name in PACKAGE_ORDER
    ]
    transitive_sha = write_jsonl(
        args.bundle / "transitive-dependencies.jsonl", transitive_rows
    )
    patch_rows = [{
        "artifact_id": "candidate-patch-set", "patch_count": 0,
        "patch_count_scope": "opentrail-post-pinned-esp-idf-gitlink-only",
        "pinned_baseline_commit": MBEDTLS_COMMIT,
        "pinned_baseline_tree": MBEDTLS_TREE,
        "post_patch_tree_sha256": source_tree_sha,
        "upstream_or_espressif_divergence_assessed": False,
    }]
    patches_sha = write_jsonl(args.bundle / "patches.jsonl", patch_rows)

    spdx_files = []
    spdx_by_package = {name: [] for name in PACKAGE_ORDER}
    for number, row in enumerate(
        sorted(all_files, key=lambda item: item["logical_path"].encode("utf-8")), 1
    ):
        raw = source_blobs[row["path"]] if row in source_files else glue_blobs[row["path"]]
        spdx = {
            "SPDXID": f"SPDXRef-File-{number:04d}",
            "checksums": [
                {"algorithm": "SHA1", "checksumValue": hashlib.sha1(raw).hexdigest()},
                {"algorithm": "SHA256", "checksumValue": row["sha256"]},
            ],
            "comment": f"Exact Git blob {row['git_blob']}; mode {row['mode']}; package {row['package_id']}.",
            "copyrightText": "NOASSERTION", "fileName": "./" + row["logical_path"],
            "licenseConcluded": "NOASSERTION",
            "licenseInfoInFiles": [row["declared_expression"]],
        }
        spdx_files.append(spdx)
        spdx_by_package[row["package_id"]].append(dict(row=row, raw=raw, spdx=spdx))
    packages = []
    package_ids = {}
    for name in PACKAGE_ORDER:
        identifier = "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", name)
        package_ids[name] = identifier
        packages.append(
            {
                "SPDXID": identifier, "copyrightText": "NOASSERTION",
                "downloadLocation": "NOASSERTION", "filesAnalyzed": True,
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": PACKAGE_LICENSES[name], "name": name,
                "packageVerificationCode": {
                    "packageVerificationCodeValue": verification_code(spdx_by_package[name])
                },
                "versionInfo": PACKAGE_VERSIONS[name],
            }
        )
    relationships = [
        {
            "relatedSpdxElement": package_ids["mbedtls"],
            "relationshipType": "DESCRIBES", "spdxElementId": "SPDXRef-DOCUMENT",
        },
        {
            "relatedSpdxElement": package_ids["esp-idf-mbedtls-glue"],
            "relationshipType": "DESCRIBES", "spdxElementId": "SPDXRef-DOCUMENT",
        },
    ]
    package_parents = {
        "tf-psa-crypto": "mbedtls",
        "framework": "tf-psa-crypto",
        "everest": "tf-psa-crypto",
        "p256-m": "tf-psa-crypto",
        "mldsa-native": "tf-psa-crypto",
    }
    for name in PACKAGE_ORDER:
        if name in package_parents:
            relationships.append(
                {
                    "relatedSpdxElement": package_ids[name],
                    "relationshipType": "CONTAINS",
                    "spdxElementId": package_ids[package_parents[name]],
                }
            )
        for item in spdx_by_package[name]:
            relationships.append(
                {
                    "relatedSpdxElement": item["spdx"]["SPDXID"],
                    "relationshipType": "CONTAINS", "spdxElementId": package_ids[name],
                }
            )
    relationships.append(
        {
            "relatedSpdxElement": package_ids["mbedtls"],
            "relationshipType": "DEPENDS_ON",
            "spdxElementId": package_ids["esp-idf-mbedtls-glue"],
        }
    )
    sbom = {
        "SPDXID": "SPDXRef-DOCUMENT",
        "comment": "Complete pinned source and component-glue file partition; no legal clearance or compatibility conclusion.",
        "creationInfo": {
            "created": "2026-08-21T00:00:00Z",
            "creators": ["Tool: OpenTrail OT-105 deterministic metadata-only generator"],
        },
        "dataLicense": "CC0-1.0",
        "documentDescribes": [package_ids["mbedtls"], package_ids["esp-idf-mbedtls-glue"]],
        "documentNamespace": f"https://opentrail.invalid/spdx/ot-105/mbedtls/{MBEDTLS_COMMIT}",
        "files": spdx_files,
        "hasExtractedLicensingInfos": [{
            "extractedText": "Historic public-domain dedication options referenced by upstream file headers; no legal conclusion is made.",
            "licenseId": "LicenseRef-PD-hp",
        }],
        "name": "OpenTrail-OT-105-mbedTLS-4.1.0-ESP-IDF-gitlink-metadata",
        "packages": packages, "relationships": relationships,
        "spdxVersion": "SPDX-2.3",
    }
    sbom_sha = write_json(args.bundle / "sbom.spdx.json", sbom)

    license_notices = [
        {
            "bytes": 29852, "git_blob": "776ac77eaf5a0da545f95ad7386b9954378fa8ae",
            "path": "source/LICENSE",
            "sha256": "9b405ef4c89342f5eae1dd828882f931747f71001cfba7d114801039b52ad09b",
        },
        {
            "bytes": 29856, "git_blob": "4c47cb2a5a526a482b939705c41b8610c4e59205",
            "path": "source/tf-psa-crypto/LICENSE",
            "sha256": "da8c58f05f135a9d15e9ffad4ecf854cfcc1f014c8abfd75ba05f62630ccc118",
        },
        {
            "bytes": 29861, "git_blob": "75e3d54f39922255ebc6199aea111be094d28b5a",
            "path": "source/tf-psa-crypto/framework/LICENSE",
            "sha256": "11402351e38392230bb8934ba1095c0c0049a296c0f8821f76e4672dff54b490",
        },
        {
            "bytes": 15310, "git_blob": "5fd3c1b13522c1ddb995a3c71b0c9b8a59df2c07",
            "path": "source/tf-psa-crypto/drivers/pqcp/mldsa-native/LICENSE",
            "sha256": "26ee68617256be39e12fbbb0fb0c8c7087980a2020ded46e3ef3c3810c35d847",
        },
        {
            "bytes": 1138, "git_blob": "35741e52a84345968ae458221233faf0fd24e30c",
            "path": "source/tf-psa-crypto/drivers/pqcp/mldsa-native/examples/custom_backend/mldsa_native/src/fips202/native/custom/src/LICENSE",
            "sha256": "c5ebc5c092628cbb9018d4b73d7330ffc84e3f44576c1bddcd884c3e5158a18c",
        },
    ]
    parent_license = {
        "bytes": 11358, "git_blob": "d645695673349e3947e8e5ae42332d0ac3164cd7",
        "path": "LICENSE",
        "sha256": "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
    }
    lock = {
        "boundaries": {
            "api_config_eligibility_proven": False, "benchmark_executed": False,
            "build_executed": False, "candidate_imported": False,
            "candidate_selected": False, "crypto_executed": False,
            "device_accessed": False, "firmware_changed": False, "flashed": False,
            "keys_or_entropy_used": False, "legal_clearance_claimed": False,
            "license_compatibility_determined": False, "physical_evidence_added": False,
            "radio_used": False, "score_credit_added": False,
            "source_acquired_or_copied": False,
        },
        "candidate_id": "esp_idf_mbedtls_psa",
        "evidence": {
            "component_glue": {
                "directory_count": 45, "entry_count": 243, "file_count": 198,
                "kind": "sha256-utf8-jsonl-posix-tree-v1",
                "path": ROOT + "component-glue-tree.jsonl", "sha256": glue_manifest_sha,
                "total_bytes": glue_bytes, "tree_sha256": glue_tree_sha,
            },
            "full_tree": {
                "directory_count": 571, "entry_count": 4122, "file_count": 3551,
                "kind": "sha256-utf8-jsonl-posix-tree-v1",
                "path": ROOT + "source-tree.jsonl", "sha256": source_manifest_sha,
                "total_bytes": source_bytes, "tree_sha256": source_tree_sha,
            },
            "license": {
                "file_count": len(license_rows), "inventory_complete": True,
                "kind": "sha256-utf8-jsonl-license-inventory-v1",
                "path": ROOT + "license-inventory.jsonl", "sha256": license_sha,
            },
            "patches": {
                "kind": "sha256-utf8-jsonl-ordered-patches-v1", "patch_count": 0,
                "patch_count_scope": "opentrail-post-pinned-esp-idf-gitlink-only",
                "pinned_baseline_commit": MBEDTLS_COMMIT,
                "pinned_baseline_tree": MBEDTLS_TREE,
                "path": ROOT + "patches.jsonl", "sha256": patches_sha,
                "upstream_or_espressif_divergence_assessed": False,
            },
            "sbom": {
                "component_count": len(packages), "file_count": len(spdx_files),
                "kind": "sha256-canonical-spdx-json-v1",
                "path": ROOT + "sbom.spdx.json", "sha256": sbom_sha,
            },
            "transitive": {
                "partition_count": len(transitive_rows),
                "kind": "sha256-utf8-jsonl-transitive-dependencies-v1",
                "path": ROOT + "transitive-dependencies.jsonl", "sha256": transitive_sha,
            },
        },
        "license_boundary": {
            "embedded_exceptions_flattened": False, "inventory_complete": True,
            "license_compatibility_determined": False, "license_notices": license_notices,
            "project_license_choice": PROJECT_LICENSE,
            "upstream_license_expression": UPSTREAM_LICENSE,
        },
        "lock_id": "OT-105-OT005-MBEDTLS-PSA-4.1.0-ESP-IDF-GITLINK-DEPENDENCY-LOCK-V0",
        "lock_kind": "esp_idf_gitlink_dependency_lock", "locked_date": DATE,
        "manifest_policy": {
            "allowed_entry_kinds": ["directory", "regular_file"],
            "case_policy": "unicode-15.1-nfc-casefold-unique",
            "forbidden_entry_kinds": [
                "absolute_path", "backslash_path", "dot_segment", "drive_path",
                "fifo", "reparse_point", "socket", "symlink",
            ],
            "path_encoding": "utf8_relative_posix",
            "path_order": "ordinal_bytewise_ascending",
            "partition_policy": "most-specific-prefix-exactly-once-v1",
        },
        "parent_idf": {
            "component_glue_tree_sha256": glue_tree_sha,
            "component_tree": IDF_COMPONENT_TREE, "commit": IDF_COMMIT,
            "gitlink_commit": MBEDTLS_COMMIT,
            "gitlink_path": "components/mbedtls/mbedtls",
            "license_notice": parent_license, "tree": IDF_TREE, "version": "v6.0.2",
        },
        "schema": "OTMPSL0",
        "source": {
            "commit": MBEDTLS_COMMIT, "tf_psa_tree": TF_PSA_TREE,
            "tree": MBEDTLS_TREE, "version": "4.1.0",
        },
        "version": 0,
    }
    lock_sha = write_json(args.bundle / "project-lock.json", lock)
    evidence = {
        "acquisition_receipt": {"receipt_kind": None, "receipt_sha256": None, "required": False},
        "artifact_kind": "candidate_source_evidence",
        "authority": {field: False for field in AUTHORITY_FIELDS},
        "candidate_id": "esp_idf_mbedtls_psa",
        "claims": {field: False for field in CLAIM_FIELDS},
        "contract_policy_sha256": OT097_POLICY,
        "evidence_id": "OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0",
        "full_tree_manifest": {
            "artifact_id": "candidate-full-source-tree", "casefold_collision_count": 0,
            "directory_count": 571, "entry_count": 4122,
            "manifest_kind": "sha256-utf8-jsonl-posix-tree-v1",
            "manifest_sha256": source_manifest_sha, "regular_file_count": 3551,
            "reparse_point_count": 0, "symlink_count": 0,
            "total_bytes": source_bytes, "tree_sha256": source_tree_sha,
        },
        "legal_clearance_claimed": False,
        "license_compatibility_determined": False,
        "license_manifest": {
            "artifact_id": "candidate-license-inventory", "file_count": len(license_rows),
            "inventory_complete": True,
            "manifest_kind": "sha256-utf8-jsonl-license-inventory-v1",
            "manifest_sha256": license_sha, "project_license_choice": PROJECT_LICENSE,
            "upstream_license_expression": UPSTREAM_LICENSE,
        },
        "lock_kind": "esp_idf_gitlink_dependency_lock",
        "parent_idf_binding": {
            "component_glue_manifest_kind": "sha256-utf8-jsonl-posix-tree-v1",
            "component_glue_manifest_sha256": glue_manifest_sha,
            "gitlink_commit": MBEDTLS_COMMIT,
            "gitlink_path": "components/mbedtls/mbedtls",
            "parent_source_commit": IDF_COMMIT, "required": True,
        },
        "patch_manifest": {
            "artifact_id": "candidate-patch-set",
            "manifest_kind": "sha256-utf8-jsonl-ordered-patches-v1",
            "manifest_sha256": patches_sha, "patch_count": 0,
            "post_patch_tree_sha256": source_tree_sha,
        },
        "project_dependency_lock": {
            "digest_kind": "sha256-raw-project-lock-bytes-v1",
            "lock_kind": "esp_idf_gitlink_dependency_lock", "lock_sha256": lock_sha,
            "logical_path": ROOT + "project-lock.json",
        },
        "project_license_choice": PROJECT_LICENSE, "recorded_date": DATE,
        "role": "comparison",
        "sbom_manifest": {
            "artifact_id": "candidate-sbom", "component_count": len(packages),
            "manifest_kind": "sha256-canonical-spdx-json-v1",
            "manifest_sha256": sbom_sha,
        },
        "schema": "OTCSLE0", "source_commit": MBEDTLS_COMMIT,
        "source_kind": "esp_idf_pinned_gitlink",
        "transitive_manifest": {
            "artifact_id": "candidate-transitive-dependencies",
            "dependency_count": len(transitive_rows),
            "manifest_kind": "sha256-utf8-jsonl-transitive-dependencies-v1",
            "manifest_sha256": transitive_sha,
        },
        "upstream_license_expression": UPSTREAM_LICENSE,
        "version": 1, "version_string": "4.1.0",
    }
    evidence_sha = write_json(args.source_evidence, evidence)
    current_three = [
        "final_candidate_build_configuration_unresolved",
        "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
        "direct_radio_mtu_phy_region_unresolved",
    ]
    empty = {"esp_idf_mbedtls_psa": [], "espressif_libsodium": [], "monocypher": []}
    admission = {
        "acceptance_counts": {"api_config": 0, "candidate_import": 0, "source": 3},
        "accepted_api_config_evidence_sha256": empty,
        "accepted_candidate": {
            "candidate_id": "esp_idf_mbedtls_psa", "component_glue_manifest_sha256": glue_manifest_sha,
            "license_inventory_sha256": license_sha,
            "lock_kind": "esp_idf_gitlink_dependency_lock",
            "parent_source_commit": IDF_COMMIT, "project_dependency_lock_sha256": lock_sha,
            "project_license_choice": PROJECT_LICENSE, "source_commit": MBEDTLS_COMMIT,
            "source_evidence_sha256": evidence_sha, "tree_sha256": source_tree_sha,
            "upstream_license_expression": UPSTREAM_LICENSE, "version": "4.1.0",
        },
        "accepted_candidate_import_evidence_sha256": empty,
        "accepted_date": DATE,
        "accepted_source_evidence_sha256": {
            "esp_idf_mbedtls_psa": [evidence_sha],
            "espressif_libsodium": [LIBSODIUM_EVIDENCE],
            "monocypher": [OT102_EVIDENCE],
        },
        "admission_id": "OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0",
        "artifact_kind": "append_only_mbedtls_psa_source_dependency_lock_acceptance_delta",
        "authority": {
            "benchmark_build_authorized": False, "benchmark_execution_authorized": False,
            "candidate_import_authorized": False, "device_access_authorized": False,
            "final_candidate_configuration_authorized": False,
            "key_or_entropy_operation_authorized": False, "packet_v1_authorized": False,
            "radio_transmit_authorized": False, "score_credit_added": False,
            "suite_selection_authorized": False,
        },
        "claims": {
            "api_config_eligibility_proven": False, "benchmark_executed": False,
            "candidate_import_accepted": False, "candidate_selected": False,
            "dependency_lock_accepted": True,
            "final_candidate_configuration_proven": False,
            "hardware_or_device_accessed": False,
            "libsodium_source_lock_remains_accepted": True,
            "mbedtls_psa_source_lock_accepted": True,
            "monocypher_source_lock_remains_accepted": True,
            "packet_v1_wire_selected": False, "physical_evidence_added": False,
            "readiness_accepted": False, "score_credit_added": False,
            "source_acquired_or_copied": False, "suite_selected": False,
        },
        "current_three_blockers": current_three,
        "license_claims": {
            "embedded_exceptions_flattened": False, "legal_clearance_claimed": False,
            "license_compatibility_determined": False,
        },
        "parents": {
            "otcmse0_v0_canonical_sha256": OT096_CANONICAL,
            "otcmse0_v0_raw_sha256": OT096_RAW,
            "otcsla0_v0_raw_sha256": OT100_RAW,
            "otcsl0_v1_policy_sha256": OT097_POLICY,
            "otcsl0_v1_raw_sha256": OT097_RAW,
            "otmsla0_v0_raw_sha256": OT102_RAW,
            "otrtpa0_v0_raw_sha256": OT103_RAW,
        },
        "prior_current_three_blockers": current_three,
        "public_result": RESULT, "schema": "OTMPSLA0",
        "source_evidence": {
            "path": "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0.json",
            "sha256": evidence_sha,
        },
        "status": "mbedtls_psa_source_dependency_lock_admitted_host_only_readiness_blocked",
        "version": 0,
    }
    write_json(args.admission, admission)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
