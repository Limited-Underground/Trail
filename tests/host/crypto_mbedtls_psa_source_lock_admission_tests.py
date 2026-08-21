#!/usr/bin/env python3
"""Adversarial tests for the OT-105 metadata-only mbedTLS/PSA lock."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/crypto_mbedtls_psa_source_lock_admission.py"
GENERATOR = ROOT / "tools/generate_mbedtls_psa_source_lock_v1.py"
ARTIFACT = ROOT / "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0.json"
BUNDLE = ROOT / "tests/benchmarks/crypto/esp_idf/mbedtls_4_1_0"

spec = importlib.util.spec_from_file_location("otmpsla", TOOL)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)


def canonical(value) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, allow_nan=False, sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def git_blob(path: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), "show", "HEAD:" + path])


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.admission = json.loads(ARTIFACT.read_text(encoding="utf-8"))
        cls.result = module.validate(ARTIFACT)

    def mutated(self, action):
        value = copy.deepcopy(self.admission)
        action(value)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "admission.json"
            path.write_bytes(canonical(value))
            with self.assertRaises(module.AdmissionError):
                module.validate(path, enforce_digest=False)

    def test_exact_admission_cli_parent_blobs_and_three_source_count(self):
        self.assertEqual(self.result["acceptance_counts"], {"api_config": 0, "candidate_import": 0, "source": 3})
        self.assertEqual(self.result["current_blocker_count"], 3)
        self.assertFalse(self.result["readiness_advanced"])
        self.assertFalse(self.result["execution_authorized"])
        self.assertFalse(self.result["score_credit_added"])
        parent_blobs = {
            "tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json": module.OT096_RAW,
            "tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json": module.OT097_RAW,
            "tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json": module.OT100_RAW,
            "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json": module.OT102_RAW,
            "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json": module.OT103_RAW,
        }
        for path, digest in parent_blobs.items():
            self.assertEqual(hashlib.sha256(git_blob(path)).hexdigest(), digest, path)
        completed = subprocess.run(
            [sys.executable, str(TOOL), str(ARTIFACT)],
            cwd=ROOT, capture_output=True, text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        cli = json.loads(completed.stdout)
        self.assertEqual(cli["source_file_count"], 3551)
        self.assertEqual(cli["component_glue_file_count"], 198)

    def test_exact_tree_glue_counts_digests_and_metadata_only_scope(self):
        expected = {
            "source-tree.jsonl": (module.SOURCE_MANIFEST_SHA, 1_088_441),
            "license-inventory.jsonl": (module.LICENSE_SHA, 1_326_854),
            "sbom.spdx.json": (module.SBOM_SHA, 2_459_820),
            "transitive-dependencies.jsonl": (module.TRANSITIVE_SHA, 1_737),
            "patches.jsonl": (module.PATCH_SHA, 395),
            "component-glue-tree.jsonl": (module.GLUE_MANIFEST_SHA, 56_033),
            "project-lock.json": (module.LOCK_SHA, 5_076),
        }
        self.assertEqual(sorted(path.name for path in BUNDLE.iterdir()), sorted(expected))
        self.assertFalse((BUNDLE / "source").exists())
        for name, (digest, size) in expected.items():
            raw = (BUNDLE / name).read_bytes()
            self.assertEqual((hashlib.sha256(raw).hexdigest(), len(raw)), (digest, size), name)
        source = [json.loads(line) for line in (BUNDLE / "source-tree.jsonl").read_text(encoding="utf-8").splitlines()]
        glue = [json.loads(line) for line in (BUNDLE / "component-glue-tree.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual((len(source), sum(row["kind"] == "regular_file" for row in source)), (4122, 3551))
        self.assertEqual((len(glue), sum(row["kind"] == "regular_file" for row in glue)), (243, 198))
        self.assertEqual(self.result["source_tree_sha256"], "ff43c530e168ae438bb0329e3278711afa310c48d53bf28d2657da400d6b46c8")
        self.assertEqual(self.result["component_glue_tree_sha256"], "1675dc3f72db6289dbdcddcdbccea626a4c5f23a58c444c702555d586e464748")

    def test_full_license_inventory_preserves_embedded_exceptions(self):
        rows = [json.loads(line) for line in (BUNDLE / "license-inventory.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual(len(rows), 3749)
        self.assertEqual(len({row["path"] for row in rows}), 3749)
        self.assertEqual(sum(row["path"].startswith("source/") for row in rows), 3551)
        self.assertEqual(sum(row["path"].startswith("component-glue/") for row in rows), 198)
        expressions = {row["declared_expression"] for row in rows}
        for expression in (
            "Apache-2.0 OR GPL-2.0-or-later", "Apache-2.0 OR ISC OR MIT",
            "Apache-2.0 OR ISC OR MIT-0", "Apache-2.0", "MIT", "MIT-0",
            "MIT-0 AND Apache-2.0", "BSD-3-Clause", "CC-BY-4.0",
            "LicenseRef-PD-hp OR CC0-1.0 OR 0BSD OR MIT-0 OR MIT",
            "Unlicense OR CC0-1.0", "CC0-1.0", "NOASSERTION",
        ):
            self.assertIn(expression, expressions)
        self.assertTrue(all(row["license_concluded"] == "NOASSERTION" for row in rows))
        prose = next(
            row for row in rows
            if row["path"] == "source/tf-psa-crypto/framework/CONTRIBUTING.md"
        )
        self.assertEqual(
            (prose["declared_expression"], prose["detection"]),
            ("NOASSERTION", "no-file-spdx-header"),
        )
        custom_example = next(
            row for row in rows
            if row["path"].endswith(
                "examples/custom_backend/mldsa_native/src/fips202/native/custom/src/LICENSE"
            )
        )
        self.assertEqual(
            (
                custom_example["package_id"], custom_example["declared_expression"],
                custom_example["detection"], custom_example["sha256"],
            ),
            (
                "mldsa-native", "CC-BY-4.0", "spdx-header",
                "c5ebc5c092628cbb9018d4b73d7330ffc84e3f44576c1bddcd884c3e5158a18c",
            ),
        )
        lock = json.loads((BUNDLE / "project-lock.json").read_text(encoding="utf-8"))
        boundary = lock["license_boundary"]
        self.assertEqual(boundary["upstream_license_expression"], "Apache-2.0 OR GPL-2.0-or-later")
        self.assertEqual(boundary["project_license_choice"], "Apache-2.0")
        self.assertFalse(boundary["embedded_exceptions_flattened"])
        self.assertFalse(boundary["license_compatibility_determined"])
        notices = {row["path"]: row for row in boundary["license_notices"]}
        self.assertEqual(notices["source/LICENSE"]["sha256"], "9b405ef4c89342f5eae1dd828882f931747f71001cfba7d114801039b52ad09b")
        self.assertEqual(notices["source/tf-psa-crypto/framework/LICENSE"]["sha256"], "11402351e38392230bb8934ba1095c0c0049a296c0f8821f76e4672dff54b490")

    def test_sbom_and_transitive_partitions_cover_every_file_once(self):
        sbom = json.loads((BUNDLE / "sbom.spdx.json").read_text(encoding="utf-8"))
        dependencies = [json.loads(line) for line in (BUNDLE / "transitive-dependencies.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual((len(sbom["packages"]), len(sbom["files"]), len(dependencies)), (7, 3749, 7))
        self.assertEqual({row["package_id"]: row["file_count"] for row in dependencies}, module.PACKAGE_COUNTS)
        self.assertTrue(all(row["bundled_source_partition"] is True for row in dependencies))
        self.assertTrue(all(row["runtime_or_link_dependency"] is False for row in dependencies))
        versions = {row["name"]: row["versionInfo"] for row in sbom["packages"]}
        self.assertEqual(versions, module.PACKAGE_VERSIONS)
        package_ids = {
            name: "SPDXRef-Package-" + name for name in module.PACKAGE_COUNTS
        }
        package_containment = {
            (row["spdxElementId"], row["relatedSpdxElement"])
            for row in sbom["relationships"]
            if row["relationshipType"] == "CONTAINS"
            and row["relatedSpdxElement"] in package_ids.values()
        }
        self.assertEqual(
            package_containment,
            {
                (package_ids[parent], package_ids[child])
                for child, parent in module.PACKAGE_PARENTS.items()
            },
        )
        file_names = [row["fileName"] for row in sbom["files"]]
        self.assertEqual(len(file_names), len(set(file_names)))
        self.assertEqual(sum(name.startswith("./source/") for name in file_names), 3551)
        self.assertEqual(sum(name.startswith("./component-glue/") for name in file_names), 198)
        self.assertTrue(all(row["licenseConcluded"] == "NOASSERTION" for row in sbom["files"]))

    def test_project_lock_and_genuine_otcsle_v1_overlay_restore(self):
        before_digest = None
        before_anchor = None
        generic = module._module("ot105_test_otcsl", ROOT / "tools/crypto_candidate_source_lock.py")
        before_digest = generic.EXPECTED_V1_CONTRACT_SHA256
        before_anchor = generic.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"]
        evidence, facts = module._validated_otcsle()
        self.assertEqual((evidence["schema"], evidence["version"]), ("OTCSLE0", 1))
        self.assertEqual(evidence["source_kind"], "esp_idf_pinned_gitlink")
        self.assertEqual(evidence["lock_kind"], "esp_idf_gitlink_dependency_lock")
        self.assertEqual(evidence["parent_idf_binding"]["parent_source_commit"], module.IDF_COMMIT)
        lock = json.loads((BUNDLE / "project-lock.json").read_text(encoding="utf-8"))
        self.assertEqual(lock["evidence"]["transitive"]["partition_count"], 7)
        self.assertNotIn("dependency_count", lock["evidence"]["transitive"])
        patch_evidence = lock["evidence"]["patches"]
        self.assertEqual(patch_evidence["patch_count"], 0)
        self.assertEqual(
            patch_evidence["patch_count_scope"],
            "opentrail-post-pinned-esp-idf-gitlink-only",
        )
        self.assertEqual(patch_evidence["pinned_baseline_commit"], module.MBEDTLS_COMMIT)
        self.assertEqual(patch_evidence["pinned_baseline_tree"], module.MBEDTLS_TREE)
        self.assertFalse(patch_evidence["upstream_or_espressif_divergence_assessed"])
        self.assertTrue(facts["source_lock_accepted"])
        self.assertFalse(facts["import_authorized"])
        self.assertEqual(generic.EXPECTED_V1_CONTRACT_SHA256, before_digest)
        self.assertEqual(generic.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"], before_anchor)
        self.assertEqual(self.result["project_dependency_lock_sha256"], module.LOCK_SHA)

    def test_admission_digest_bypassed_mutations_fail_closed_with_exact_types(self):
        actions = [
            lambda data: data.__setitem__("schema", "changed"),
            lambda data: data.__setitem__("version", False),
            lambda data: data.__setitem__("invented", False),
            lambda data: data["parents"].__setitem__("otcmse0_v0_raw_sha256", "0" * 64),
            lambda data: data["source_evidence"].__setitem__("sha256", "0" * 64),
            lambda data: data["acceptance_counts"].__setitem__("source", True),
            lambda data: data["acceptance_counts"].__setitem__("api_config", False),
            lambda data: data["accepted_source_evidence_sha256"]["esp_idf_mbedtls_psa"].clear(),
            lambda data: data["accepted_api_config_evidence_sha256"]["esp_idf_mbedtls_psa"].append("0" * 64),
            lambda data: data["prior_current_three_blockers"].pop(),
            lambda data: data["current_three_blockers"].reverse(),
            lambda data: data["claims"].__setitem__("api_config_eligibility_proven", True),
            lambda data: data["claims"].__setitem__("mbedtls_psa_source_lock_accepted", 1),
            lambda data: data["authority"].__setitem__("benchmark_build_authorized", True),
            lambda data: data["authority"].__setitem__("device_access_authorized", 0),
            lambda data: data["license_claims"].__setitem__("embedded_exceptions_flattened", True),
            lambda data: data["accepted_candidate"].__setitem__("project_dependency_lock_sha256", "0" * 64),
        ]
        for action in actions:
            with self.subTest(action=action):
                self.mutated(action)

    def test_bundle_byte_path_and_count_mutations_fail_before_claims(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            changed = directory / "changed.jsonl"
            changed.write_bytes((BUNDLE / "source-tree.jsonl").read_bytes() + b" ")
            with self.assertRaises(module.AdmissionError):
                module._jsonl(changed, module.SOURCE_MANIFEST_SHA, max_bytes=module.MAX_TREE, max_lines=4122)
            overlong = directory / "overlong.jsonl"
            overlong.write_bytes(b"{" + b"x" * module.MAX_LINE + b"}\n")
            with self.assertRaises(module.AdmissionError):
                module._jsonl(overlong, hashlib.sha256(overlong.read_bytes()).hexdigest(), max_bytes=module.MAX_TREE, max_lines=4122)
        for path in ("../escape", "./relative", "/absolute", "a//b", "a\\b", "CON", "folder/NUL.txt", "trail.", " lead"):
            with self.subTest(path=path), self.assertRaises(module.AdmissionError):
                module._safe(path)

    def test_generator_and_validator_surfaces_are_metadata_only_and_private_safe(self):
        generator = GENERATOR.read_text(encoding="utf-8")
        validator = TOOL.read_text(encoding="utf-8")
        for token in ("git clone", "git fetch", "git checkout", "copytree", "copy2", "import socket", "import requests", "import urllib", "idf.py", "esptool"):
            self.assertNotIn(token, generator)
        for token in ("import subprocess", "import socket", "import requests", "import urllib", "os.system", "idf.py", "esptool"):
            self.assertNotIn(token, validator)
        self.assertNotIn("C:\\Users", generator)
        self.assertNotIn("C:\\Users", validator)
        self.assertFalse(any(path.is_dir() for path in BUNDLE.iterdir()))
        self.assertEqual(sorted(path.name for path in BUNDLE.iterdir()), [
            "component-glue-tree.jsonl", "license-inventory.jsonl", "patches.jsonl",
            "project-lock.json", "sbom.spdx.json", "source-tree.jsonl",
            "transitive-dependencies.jsonl",
        ])

    def test_cli_failure_is_fixed_sanitized_and_nonauthorizing(self):
        completed = subprocess.run(
            [sys.executable, str(TOOL), r"Z:\restricted\private.json"],
            cwd=ROOT, capture_output=True, text=True,
        )
        self.assertEqual((completed.returncode, completed.stdout, completed.stderr.strip()), (2, "", "OTMPSLA0 validation failed"))
        self.assertNotIn("restricted", completed.stderr)
        self.assertNotIn("private", completed.stderr.lower())
        self.assertFalse(self.admission["claims"]["api_config_eligibility_proven"])
        self.assertFalse(self.admission["claims"]["candidate_import_accepted"])
        self.assertFalse(self.admission["claims"]["readiness_accepted"])


if __name__ == "__main__":
    unittest.main()
