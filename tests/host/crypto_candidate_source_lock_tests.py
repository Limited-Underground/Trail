#!/usr/bin/env python3
"""Adversarial tests for the host-only OTCSL0 source-lock boundary."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import crypto_benchmark as otcb0  # noqa: E402
import crypto_benchmark_baseline as otcbl0  # noqa: E402
import crypto_benchmark_readiness as otcbr0  # noqa: E402
import crypto_candidate_source_lock as source_lock  # noqa: E402


CONTRACT_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json"
)
V1_CONTRACT_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json"
)
PLAN_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto" / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
)
BASELINE_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-093-OT005-BUILD-BASELINE-V0.json"
)
READINESS_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-094-OT005-CANDIDATE-READINESS-V0.json"
)
EXPECTED_SHA256 = "c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f"
EXPECTED_V1_SHA256 = "51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a"


def contract() -> dict:
    return source_lock.load(CONTRACT_PATH)


def v1_contract() -> dict:
    return source_lock.load(V1_CONTRACT_PATH)


def digest(label: str) -> str:
    return hashlib.sha256(label.encode("utf-8")).hexdigest()


def commit(label: str) -> str:
    return hashlib.sha1(label.encode("utf-8")).hexdigest()


def expect_error(action, contains: str) -> None:
    try:
        action()
    except source_lock.ValidationError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected validation failure containing {contains!r}")


def synthetic_source_evidence(candidate_id: str) -> dict:
    value = contract()
    policy = next(
        candidate for candidate in value["candidates"] if candidate["candidate_id"] == candidate_id
    )
    source_commit = policy["observed_source_commit"] or commit(f"source:{candidate_id}")
    tree_sha = digest(f"tree:{candidate_id}")
    required_parent = policy["parent_idf_binding_required"]
    return {
        "schema": "OTCSLE0",
        "version": 0,
        "artifact_kind": "candidate_source_evidence",
        "evidence_id": f"OT-999-OT005-{candidate_id.upper().replace('_', '-')}-SOURCE-EVIDENCE-V0",
        "recorded_date": "2026-08-20",
        "contract_policy_sha256": source_lock.admission_policy_sha256(value),
        "candidate_id": candidate_id,
        "role": policy["role"],
        "version_string": policy["required_version"],
        "license_spdx": policy["license_spdx"],
        "source_kind": policy["source_kind"],
        "lock_kind": policy["permitted_lock_kinds"][0],
        "source_commit": source_commit,
        "acquisition_receipt": {
            "required": policy["acquisition_receipt_required"],
            "receipt_kind": (
                "sha256-canonical-json-acquisition-receipt-v1"
                if policy["acquisition_receipt_required"]
                else None
            ),
            "receipt_sha256": (
                digest(f"receipt:{candidate_id}")
                if policy["acquisition_receipt_required"]
                else None
            ),
        },
        "full_tree_manifest": {
            "manifest_kind": "sha256-utf8-jsonl-posix-tree-v1",
            "artifact_id": "candidate-full-source-tree",
            "manifest_sha256": digest(f"tree-manifest:{candidate_id}"),
            "tree_sha256": tree_sha,
            "entry_count": 5,
            "total_bytes": 4096,
            "regular_file_count": 3,
            "directory_count": 2,
            "symlink_count": 0,
            "reparse_point_count": 0,
            "casefold_collision_count": 0,
        },
        "license_manifest": {
            "manifest_kind": "sha256-utf8-jsonl-license-inventory-v1",
            "artifact_id": "candidate-license-inventory",
            "manifest_sha256": digest(f"license:{candidate_id}"),
            "file_count": 1,
            "declared_spdx": policy["license_spdx"],
        },
        "sbom_manifest": {
            "manifest_kind": "sha256-canonical-spdx-json-v1",
            "artifact_id": "candidate-sbom",
            "manifest_sha256": digest(f"sbom:{candidate_id}"),
            "component_count": 1,
        },
        "transitive_manifest": {
            "manifest_kind": "sha256-utf8-jsonl-transitive-dependencies-v1",
            "artifact_id": "candidate-transitive-dependencies",
            "manifest_sha256": digest(f"transitive:{candidate_id}"),
            "dependency_count": 0,
        },
        "patch_manifest": {
            "manifest_kind": "sha256-utf8-jsonl-ordered-patches-v1",
            "artifact_id": "candidate-patch-set",
            "manifest_sha256": digest(f"patches:{candidate_id}"),
            "patch_count": 0,
            "post_patch_tree_sha256": tree_sha,
        },
        "project_dependency_lock": {
            "lock_kind": policy["permitted_lock_kinds"][0],
            "digest_kind": "sha256-raw-project-lock-bytes-v1",
            "lock_sha256": digest(f"project-lock:{candidate_id}"),
            "logical_path": f"tests/benchmarks/crypto/locks/{candidate_id}.json",
        },
        "parent_idf_binding": {
            "required": required_parent,
            "parent_source_commit": (
                policy["observed_parent_source_commit"] if required_parent else None
            ),
            "gitlink_path": "components/mbedtls/mbedtls" if required_parent else None,
            "gitlink_commit": source_commit if required_parent else None,
            "component_glue_manifest_sha256": (
                digest("esp-idf-mbedtls-component-glue") if required_parent else None
            ),
            "component_glue_manifest_kind": (
                "sha256-utf8-jsonl-posix-tree-v1" if required_parent else None
            ),
        },
        "authority": {field: False for field in source_lock.AUTHORITY_FIELDS},
        "claims": {field: False for field in source_lock.CLAIM_FIELDS},
    }


def accepted_source_facts(evidence: dict) -> dict:
    parent = evidence["parent_idf_binding"]
    return {
        "schema": "OTCSLE0",
        "version": 0,
        "candidate_id": evidence["candidate_id"],
        "role": evidence["role"],
        "version_string": evidence["version_string"],
        "license_spdx": evidence["license_spdx"],
        "source_commit": evidence["source_commit"],
        "lock_kind": evidence["lock_kind"],
        "project_dependency_lock_sha256": evidence["project_dependency_lock"]["lock_sha256"],
        "parent_source_commit": parent["parent_source_commit"],
        "gitlink_path": parent["gitlink_path"],
        "gitlink_commit": parent["gitlink_commit"],
        "source_evidence_sha256": source_lock.canonical_sha256(evidence),
        "source_lock_accepted": True,
        "import_authorized": False,
        "execution_authorized": False,
        "score_credit_added": False,
    }


def synthetic_api_config_evidence(candidate_id: str, source: dict) -> dict:
    return {
        "schema": "OTCAPI0",
        "version": 0,
        "artifact_kind": "candidate_api_config_eligibility_evidence",
        "evidence_id": f"OT-999-OT005-{candidate_id.upper().replace('_', '-')}-API-CONFIG-EVIDENCE-V0",
        "recorded_date": "2026-08-20",
        "contract_policy_sha256": source_lock.admission_policy_sha256(contract()),
        "candidate_id": candidate_id,
        "role": source["role"],
        "version_string": source["version_string"],
        "license_spdx": source["license_spdx"],
        "source_evidence_sha256": source["source_evidence_sha256"],
        "final_sdkconfig_sha256": digest(f"final-sdkconfig:{candidate_id}"),
        "required_operations": list(source_lock.REQUIRED_API_OPERATIONS),
        "operation_evidence_kind": "sha256-canonical-json-operation-evidence-v1",
        "operation_evidence_sha256": {
            operation: digest(f"api:{candidate_id}:{operation}")
            for operation in source_lock.REQUIRED_API_OPERATIONS
        },
        "result": "api_config_eligible",
        "execution_authorized": False,
        "score_credit_added": False,
    }


def accepted_api_facts(evidence: dict, source: dict) -> dict:
    return {
        "schema": "OTCAPI0",
        "version": 0,
        "candidate_id": evidence["candidate_id"],
        "source_evidence_sha256": source["source_evidence_sha256"],
        "final_sdkconfig_sha256": evidence["final_sdkconfig_sha256"],
        "api_config_evidence_sha256": source_lock.canonical_sha256(evidence),
        "api_config_eligible": True,
        "execution_authorized": False,
        "score_credit_added": False,
    }


def synthetic_import_evidence(candidate_id: str, source: dict, api: dict) -> dict:
    return {
        "schema": "OTCIMP0",
        "version": 0,
        "artifact_kind": "candidate_import_evidence",
        "evidence_id": f"OT-999-OT005-{candidate_id.upper().replace('_', '-')}-IMPORT-EVIDENCE-V0",
        "recorded_date": "2026-08-20",
        "contract_policy_sha256": source_lock.admission_policy_sha256(contract()),
        "candidate_id": candidate_id,
        "role": source["role"],
        "version_string": source["version_string"],
        "license_spdx": source["license_spdx"],
        "source_evidence_sha256": source["source_evidence_sha256"],
        "api_config_evidence_sha256": api["api_config_evidence_sha256"],
        "project_dependency_lock_sha256": source["project_dependency_lock_sha256"],
        "build_graph_manifest_sha256": digest(f"build-graph:{candidate_id}"),
        "build_graph_manifest_kind": "sha256-utf8-jsonl-build-graph-v1",
        "result": "imported_for_benchmark_only",
        "benchmark_execution_authorized": False,
        "score_credit_added": False,
    }


def test_canonical_contract_is_exactly_blocked_and_uncredited() -> None:
    value = contract()
    result = source_lock.validate_contract(value)
    assert result == {
        "schema": "OTCSL0",
        "version": 0,
        "admission_id": "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0",
        "status": "admission_contract_frozen_host_only",
        "public_result": (
            "SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; "
            "ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED"
        ),
        "candidate_count": 3,
        "accepted_source_lock_count": 0,
        "otcbr0_blocker_count": 6,
        "readiness_advanced": False,
        "execution_authorized": False,
        "score_credit_added": False,
        "admission_sha256": EXPECTED_SHA256,
        "admission_policy_sha256": EXPECTED_SHA256,
    }
    assert not any(value["authority"].values())
    assert not any(value["claims"].values())


def test_historical_plan_baseline_and_readiness_are_exactly_bound() -> None:
    value = contract()
    assert otcb0.canonical_sha256(otcb0._load(PLAN_PATH)) == source_lock.EXPECTED_PLAN_SHA256
    assert otcbl0.canonical_sha256(otcbl0.load(BASELINE_PATH)) == source_lock.EXPECTED_BASELINE_SHA256
    assert otcbr0.canonical_sha256(otcbr0.load(READINESS_PATH)) == source_lock.EXPECTED_READINESS_SHA256
    changed = copy.deepcopy(value)
    changed["candidate_readiness"]["blocker_count"] = 5
    expect_error(lambda: source_lock.validate_contract(changed), "readiness reference")


def test_candidate_order_versions_licenses_and_lock_kinds_are_exact() -> None:
    value = contract()
    assert [item["candidate_id"] for item in value["candidates"]] == [
        "espressif_libsodium",
        "esp_idf_mbedtls_psa",
        "monocypher",
    ]
    reordered = copy.deepcopy(value)
    reordered["candidates"][0], reordered["candidates"][1] = (
        reordered["candidates"][1],
        reordered["candidates"][0],
    )
    expect_error(lambda: source_lock.validate_contract(reordered), "candidate identity")
    lock_kind = copy.deepcopy(value)
    lock_kind["candidates"][0]["permitted_lock_kinds"] = ["arbitrary-lock"]
    expect_error(lambda: source_lock.validate_contract(lock_kind), "lock-kind allowlist")
    license_drift = copy.deepcopy(value)
    license_drift["candidates"][2]["license_spdx"] = "CC0-1.0"
    expect_error(lambda: source_lock.validate_contract(license_drift), "identity/version/license")


def test_evidence_layers_cannot_individually_claim_a_source_lock() -> None:
    value = contract()
    assert [layer["layer_id"] for layer in value["evidence_layers"]] == list(
        source_lock.EVIDENCE_LAYERS
    )
    assert not any(layer["sufficient_alone"] for layer in value["evidence_layers"])
    claimed = copy.deepcopy(value)
    claimed["evidence_layers"][1]["sufficient_alone"] = True
    expect_error(lambda: source_lock.validate_contract(claimed), "must be false")
    candidate = copy.deepcopy(value)
    candidate["candidates"][0]["source_lock_state"] = "accepted"
    candidate["candidates"][0]["accepted_source_evidence_sha256"] = digest("forged")
    expect_error(lambda: source_lock.validate_contract(candidate), "no accepted source lock")


def test_candidate_specific_trust_anchors_are_empty_and_self_claims_fail() -> None:
    value = contract()
    assert source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256 == {
        "espressif_libsodium": frozenset(),
        "esp_idf_mbedtls_psa": frozenset(),
        "monocypher": frozenset(),
    }
    assert all(
        not anchors
        for registry in (
            source_lock.ACCEPTED_API_CONFIG_EVIDENCE_SHA256,
            source_lock.ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256,
        )
        for anchors in registry.values()
    )
    for candidate_id in source_lock.CANDIDATE_BY_ID:
        assert value["accepted_source_evidence_sha256"][candidate_id] == []
        evidence = synthetic_source_evidence(candidate_id)
        expect_error(
            lambda evidence=evidence: source_lock.validate_source_evidence(evidence, value),
            "not independently accepted",
        )
    future = copy.deepcopy(value)
    future["accepted_source_evidence_sha256"]["espressif_libsodium"] = [digest("accepted")]
    assert source_lock.admission_policy_sha256(future) == source_lock.admission_policy_sha256(value)


def test_v0_future_source_acceptance_is_permanently_disabled() -> None:
    value = contract()
    evidence = synthetic_source_evidence("esp_idf_mbedtls_psa")
    evidence_sha = source_lock.canonical_sha256(evidence)
    future = copy.deepcopy(value)
    future["accepted_source_evidence_sha256"]["esp_idf_mbedtls_psa"] = [evidence_sha]
    original_expected = source_lock.EXPECTED_CONTRACT_SHA256
    original_anchors = source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256[
        "esp_idf_mbedtls_psa"
    ]
    try:
        source_lock.EXPECTED_CONTRACT_SHA256 = source_lock.canonical_sha256(future)
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256[
            "esp_idf_mbedtls_psa"
        ] = frozenset({evidence_sha})
        expect_error(
            lambda: source_lock.validate_source_evidence(evidence, future),
            "v0 source evidence cannot be admitted",
        )
    finally:
        source_lock.EXPECTED_CONTRACT_SHA256 = original_expected
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256[
            "esp_idf_mbedtls_psa"
        ] = original_anchors


def v1_source_evidence(candidate_id: str = "esp_idf_mbedtls_psa") -> dict:
    evidence = synthetic_source_evidence(candidate_id)
    policy = next(item for item in v1_contract()["candidates"] if item["candidate_id"] == candidate_id)
    evidence["version"] = 1
    evidence["contract_policy_sha256"] = source_lock.admission_policy_sha256(v1_contract())
    del evidence["license_spdx"]
    evidence["upstream_license_expression"] = policy["upstream_license_expression"]
    evidence["project_license_choice"] = policy["project_license_choice"] or policy["upstream_license_expression"].split(" OR ")[0]
    evidence["legal_clearance_claimed"] = False
    evidence["license_compatibility_determined"] = False
    manifest = evidence["license_manifest"]
    del manifest["declared_spdx"]
    manifest["upstream_license_expression"] = evidence["upstream_license_expression"]
    manifest["project_license_choice"] = evidence["project_license_choice"]
    manifest["inventory_complete"] = True
    return evidence


def test_v1_contract_and_license_evidence_fail_closed() -> None:
    value = v1_contract()
    result = source_lock.validate_contract(value)
    assert result["version"] == 1 and result["otcbr0_blocker_count"] == 6
    assert result["admission_sha256"] == EXPECTED_V1_SHA256
    assert not result["readiness_advanced"] and not result["score_credit_added"]
    flattened = copy.deepcopy(value)
    candidate = flattened["candidates"][1]
    candidate["upstream_license_expression"] = "Apache-2.0"
    expect_error(lambda: source_lock.validate_contract(flattened), "candidate license identity")
    legal = copy.deepcopy(value)
    legal["license_claims"]["legal_clearance_claimed"] = True
    expect_error(lambda: source_lock.validate_contract(legal), "must be false")
    blocker = copy.deepcopy(value)
    blocker["unchanged_blockers"].pop()
    expect_error(lambda: source_lock.validate_contract(blocker), "six-blocker")
    authority = copy.deepcopy(value)
    authority["authority"]["dependency_acquisition_authorized"] = True
    expect_error(lambda: source_lock.validate_contract(authority), "must be false")
    claim = copy.deepcopy(value)
    claim["claims"]["score_credit_added"] = True
    expect_error(lambda: source_lock.validate_contract(claim), "must be false")
    evidence = v1_source_evidence()
    expect_error(lambda: source_lock.validate_source_evidence(evidence, value), "not independently accepted")
    incomplete = copy.deepcopy(evidence)
    incomplete["license_manifest"]["inventory_complete"] = False
    expect_error(lambda: source_lock.validate_source_evidence(incomplete, value), "must be true")
    wrong_choice = copy.deepcopy(evidence)
    wrong_choice["project_license_choice"] = "MIT"
    wrong_choice["license_manifest"]["project_license_choice"] = "MIT"
    expect_error(lambda: source_lock.validate_source_evidence(wrong_choice, value), "candidate/version/license")
    claimed = copy.deepcopy(evidence)
    claimed["legal_clearance_claimed"] = True
    expect_error(lambda: source_lock.validate_source_evidence(claimed, value), "must be false")


def test_v1_future_source_acceptance_returns_separate_license_facts() -> None:
    value = v1_contract()
    evidence = v1_source_evidence()
    evidence_sha = source_lock.canonical_sha256(evidence)
    future = copy.deepcopy(value)
    future["accepted_source_evidence_sha256"]["esp_idf_mbedtls_psa"] = [evidence_sha]
    original_anchors = source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"]
    original_expected = source_lock.EXPECTED_V1_CONTRACT_SHA256
    try:
        source_lock.EXPECTED_V1_CONTRACT_SHA256 = source_lock.canonical_sha256(future)
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = frozenset({evidence_sha})
        result = source_lock.validate_source_evidence(evidence, future)
    finally:
        source_lock.EXPECTED_V1_CONTRACT_SHA256 = original_expected
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = original_anchors
    assert result["version"] == 1
    assert result["upstream_license_expression"] == "Apache-2.0 OR GPL-2.0-or-later"
    assert result["project_license_choice"] == "Apache-2.0"
    assert result["license_inventory_sha256"] == evidence["license_manifest"]["manifest_sha256"]


def test_v1_license_facts_bind_through_api_and_import_chain() -> None:
    value = v1_contract()
    source_evidence = v1_source_evidence()
    source_sha = source_lock.canonical_sha256(source_evidence)
    source_facts = {
        "schema": "OTCSLE0", "version": 1,
        "candidate_id": source_evidence["candidate_id"], "role": source_evidence["role"],
        "version_string": source_evidence["version_string"],
        "upstream_license_expression": source_evidence["upstream_license_expression"],
        "project_license_choice": source_evidence["project_license_choice"],
        "license_inventory_sha256": source_evidence["license_manifest"]["manifest_sha256"],
        "legal_clearance_claimed": False, "license_compatibility_determined": False,
        "source_commit": source_evidence["source_commit"], "lock_kind": source_evidence["lock_kind"],
        "project_dependency_lock_sha256": source_evidence["project_dependency_lock"]["lock_sha256"],
        "parent_source_commit": source_evidence["parent_idf_binding"]["parent_source_commit"],
        "gitlink_path": source_evidence["parent_idf_binding"]["gitlink_path"],
        "gitlink_commit": source_evidence["parent_idf_binding"]["gitlink_commit"],
        "source_evidence_sha256": source_sha, "source_lock_accepted": True,
        "import_authorized": False, "execution_authorized": False, "score_credit_added": False,
    }
    api = synthetic_api_config_evidence("esp_idf_mbedtls_psa", accepted_source_facts(synthetic_source_evidence("esp_idf_mbedtls_psa")))
    api["version"] = 1
    api["contract_policy_sha256"] = source_lock.admission_policy_sha256(value)
    api["source_evidence_sha256"] = source_sha
    del api["license_spdx"]
    for field in ("upstream_license_expression", "project_license_choice", "license_inventory_sha256", "legal_clearance_claimed", "license_compatibility_determined"):
        api[field] = source_facts[field]
    api_sha = source_lock.canonical_sha256(api)
    api_facts = {
        "schema": "OTCAPI0", "version": 1, "candidate_id": "esp_idf_mbedtls_psa",
        "source_evidence_sha256": source_sha,
        "final_sdkconfig_sha256": api["final_sdkconfig_sha256"],
        "api_config_evidence_sha256": api_sha, "api_config_eligible": True,
        "execution_authorized": False, "score_credit_added": False,
    }
    imported = synthetic_import_evidence("esp_idf_mbedtls_psa", accepted_source_facts(synthetic_source_evidence("esp_idf_mbedtls_psa")), accepted_api_facts(api if "license_spdx" in api else {**api, "license_spdx": "Apache-2.0"}, accepted_source_facts(synthetic_source_evidence("esp_idf_mbedtls_psa"))))
    imported["version"] = 1
    imported["contract_policy_sha256"] = source_lock.admission_policy_sha256(value)
    imported["source_evidence_sha256"] = source_sha
    imported["api_config_evidence_sha256"] = api_sha
    del imported["license_spdx"]
    for field in ("upstream_license_expression", "project_license_choice", "license_inventory_sha256", "legal_clearance_claimed", "license_compatibility_determined"):
        imported[field] = source_facts[field]
    import_sha = source_lock.canonical_sha256(imported)
    future = copy.deepcopy(value)
    future["accepted_source_evidence_sha256"]["esp_idf_mbedtls_psa"] = [source_sha]
    future["accepted_api_config_evidence_sha256"]["esp_idf_mbedtls_psa"] = [api_sha]
    future["accepted_candidate_import_evidence_sha256"]["esp_idf_mbedtls_psa"] = [import_sha]
    originals = (source_lock.EXPECTED_V1_CONTRACT_SHA256, source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"], source_lock.ACCEPTED_API_CONFIG_EVIDENCE_SHA256["esp_idf_mbedtls_psa"], source_lock.ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256["esp_idf_mbedtls_psa"])
    try:
        source_lock.EXPECTED_V1_CONTRACT_SHA256 = source_lock.canonical_sha256(future)
        source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = frozenset({source_sha})
        source_lock.ACCEPTED_API_CONFIG_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = frozenset({api_sha})
        source_lock.ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = frozenset({import_sha})
        accepted_source = source_lock.validate_source_evidence(source_evidence, future)
        accepted_api = source_lock.validate_api_config_evidence(api, future, accepted_source)
        accepted_import = source_lock.validate_candidate_import_evidence(imported, future, accepted_source, accepted_api)
    finally:
        source_lock.EXPECTED_V1_CONTRACT_SHA256, source_lock.ACCEPTED_SOURCE_EVIDENCE_SHA256["esp_idf_mbedtls_psa"], source_lock.ACCEPTED_API_CONFIG_EVIDENCE_SHA256["esp_idf_mbedtls_psa"], source_lock.ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] = originals
    assert accepted_import["version"] == 1
    for field, replacement in (
        ("upstream_license_expression", "Apache-2.0"),
        ("project_license_choice", "GPL-2.0-or-later"),
        ("license_inventory_sha256", digest("forged-license-inventory")),
        ("legal_clearance_claimed", True),
        ("license_compatibility_determined", True),
    ):
        forged_api = copy.deepcopy(api)
        forged_api[field] = replacement
        expect_error(lambda forged_api=forged_api: source_lock.validate_api_config_evidence(forged_api, value, source_facts), "accepted source facts" if field not in ("legal_clearance_claimed", "license_compatibility_determined") else "must be false")
        forged_import = copy.deepcopy(imported)
        forged_import[field] = replacement
        expect_error(lambda forged_import=forged_import: source_lock.validate_candidate_import_evidence(forged_import, value, source_facts, api_facts), "accepted source facts" if field not in ("legal_clearance_claimed", "license_compatibility_determined") else "must be false")
    flattened_api = copy.deepcopy(api)
    del flattened_api["upstream_license_expression"]
    flattened_api["license_spdx"] = "Apache-2.0"
    expect_error(lambda: source_lock.validate_api_config_evidence(flattened_api, value, source_facts), "fields are not exact")
    flattened_import = copy.deepcopy(imported)
    del flattened_import["upstream_license_expression"]
    flattened_import["license_spdx"] = "Apache-2.0"
    expect_error(lambda: source_lock.validate_candidate_import_evidence(flattened_import, value, source_facts, api_facts), "fields are not exact")


def test_api_config_and_import_evidence_are_separately_bound_and_unaccepted() -> None:
    value = contract()
    source_evidence = synthetic_source_evidence("espressif_libsodium")
    source = accepted_source_facts(source_evidence)
    api_evidence = synthetic_api_config_evidence("espressif_libsodium", source)
    expect_error(
        lambda: source_lock.validate_api_config_evidence(api_evidence, value, source),
        "API/config evidence digest is not independently accepted",
    )
    missing_ed25519 = copy.deepcopy(api_evidence)
    missing_ed25519["required_operations"].remove("ed25519_sign")
    expect_error(
        lambda: source_lock.validate_api_config_evidence(
            missing_ed25519, value, source
        ),
        "fixed complete set",
    )
    wrong_source = copy.deepcopy(api_evidence)
    wrong_source["source_evidence_sha256"] = digest("other-source")
    expect_error(
        lambda: source_lock.validate_api_config_evidence(wrong_source, value, source),
        "accepted source digest",
    )

    api = accepted_api_facts(api_evidence, source)
    import_evidence = synthetic_import_evidence(
        "espressif_libsodium", source, api
    )
    expect_error(
        lambda: source_lock.validate_candidate_import_evidence(
            import_evidence, value, source, api
        ),
        "candidate-import evidence digest is not independently accepted",
    )
    wrong_lock = copy.deepcopy(import_evidence)
    wrong_lock["project_dependency_lock_sha256"] = digest("other-lock")
    expect_error(
        lambda: source_lock.validate_candidate_import_evidence(
            wrong_lock, value, source, api
        ),
        "does not bind the project lock",
    )


def test_full_tree_license_sbom_transitive_and_patch_manifests_are_required() -> None:
    value = contract()
    evidence = synthetic_source_evidence("espressif_libsodium")
    for field in (
        "full_tree_manifest",
        "license_manifest",
        "sbom_manifest",
        "transitive_manifest",
        "patch_manifest",
        "project_dependency_lock",
    ):
        changed = copy.deepcopy(evidence)
        del changed[field]
        expect_error(
            lambda changed=changed: source_lock.validate_source_evidence(changed, value),
            "fields are not exact",
        )
    changed_tree = copy.deepcopy(evidence)
    changed_tree["patch_manifest"]["post_patch_tree_sha256"] = digest("other-tree")
    expect_error(
        lambda: source_lock.validate_source_evidence(changed_tree, value),
        "does not bind the admitted full tree",
    )
    conflated = copy.deepcopy(evidence)
    conflated["sbom_manifest"]["manifest_sha256"] = conflated["license_manifest"]["manifest_sha256"]
    expect_error(
        lambda: source_lock.validate_source_evidence(conflated, value),
        "purpose-distinct",
    )


def test_mbedtls_requires_exact_parent_idf_gitlink_binding() -> None:
    value = contract()
    evidence = synthetic_source_evidence("esp_idf_mbedtls_psa")
    wrong_parent = copy.deepcopy(evidence)
    wrong_parent["parent_idf_binding"]["parent_source_commit"] = commit("other-idf")
    expect_error(
        lambda: source_lock.validate_source_evidence(wrong_parent, value),
        "parent ESP-IDF commit mismatch",
    )
    wrong_gitlink = copy.deepcopy(evidence)
    wrong_gitlink["parent_idf_binding"]["gitlink_commit"] = commit("other-mbedtls")
    expect_error(
        lambda: source_lock.validate_source_evidence(wrong_gitlink, value),
        "gitlink and source commit differ",
    )
    external = synthetic_source_evidence("monocypher")
    external["parent_idf_binding"]["parent_source_commit"] = commit("invented-parent")
    expect_error(
        lambda: source_lock.validate_source_evidence(external, value),
        "cannot claim a parent-IDF binding",
    )


def test_acquisition_receipts_and_lock_kinds_do_not_cross_candidates() -> None:
    value = contract()
    missing_receipt = synthetic_source_evidence("espressif_libsodium")
    missing_receipt["acquisition_receipt"]["receipt_sha256"] = None
    expect_error(
        lambda: source_lock.validate_source_evidence(missing_receipt, value),
        "bounded nonempty string",
    )
    invented_receipt = synthetic_source_evidence("esp_idf_mbedtls_psa")
    invented_receipt["acquisition_receipt"].update(
        {
            "receipt_kind": "sha256-canonical-json-acquisition-receipt-v1",
            "receipt_sha256": digest("not-an-acquisition"),
        }
    )
    expect_error(
        lambda: source_lock.validate_source_evidence(invented_receipt, value),
        "cannot claim a new acquisition receipt",
    )
    wrong_lock = synthetic_source_evidence("monocypher")
    wrong_lock["lock_kind"] = "esp_idf_managed_component_lock"
    wrong_lock["project_dependency_lock"]["lock_kind"] = wrong_lock["lock_kind"]
    expect_error(
        lambda: source_lock.validate_source_evidence(wrong_lock, value),
        "lock kind is not permitted",
    )


def test_path_symlink_reparse_and_case_safety_fail_closed() -> None:
    value = contract()
    evidence = synthetic_source_evidence("monocypher")
    for path in (
        "../outside.json",
        "/absolute/lock.json",
        "C:/private/lock.json",
        "locks\\candidate.json",
        "locks/candidate.json\nsecond.json",
    ):
        changed = copy.deepcopy(evidence)
        changed["project_dependency_lock"]["logical_path"] = path
        expect_error(
            lambda changed=changed: source_lock.validate_source_evidence(changed, value),
            "safe relative POSIX path",
        )
    for path in (
        "locks/NUL",
        "locks/CON.txt",
        "locks/com1.lock",
        "locks/LPT9",
        "locks/trailing.",
        "locks/ cafe.json",
        "locks/cafe\u0301.json",
        "locks/control\u0001.json",
    ):
        expect_error(
            lambda path=path: source_lock._safe_logical_path(path, "logical_path"),
            "safe relative POSIX path",
        )
    for field in ("symlink_count", "reparse_point_count", "casefold_collision_count"):
        changed = copy.deepcopy(evidence)
        changed["full_tree_manifest"][field] = 1
        expect_error(
            lambda changed=changed: source_lock.validate_source_evidence(changed, value),
            "integer in range",
        )


def test_exact_types_cycles_depth_and_bounded_loader_fail_closed() -> None:
    value = contract()

    class Text(str):
        pass

    subtype = copy.deepcopy(value)
    subtype["status"] = Text(source_lock.STATUS)
    expect_error(lambda: source_lock.validate_contract(subtype), "noncanonical JSON type")
    cyclic = copy.deepcopy(value)
    cyclic["cycle"] = cyclic
    expect_error(lambda: source_lock.validate_contract(cyclic), "cycle")
    deep: dict = {}
    cursor = deep
    for _ in range(source_lock.MAX_DEPTH + 2):
        cursor["next"] = {}
        cursor = cursor["next"]
    expect_error(lambda: source_lock.validate_contract(deep), "structural bounds")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        duplicate = root / "duplicate.json"
        duplicate.write_text('{"schema":"OTCSL0","schema":"OTCSL0"}', encoding="utf-8")
        expect_error(lambda: source_lock.load(duplicate), "duplicate key")
        oversized = root / "oversized.json"
        oversized.write_bytes(b"x" * (source_lock.MAX_BYTES + 1))
        expect_error(lambda: source_lock.load(oversized), "size limit")


def test_private_text_and_cli_errors_are_sanitized() -> None:
    value = contract()
    private = copy.deepcopy(value)
    private["public_result"] = "secret=do-not-publish"
    expect_error(lambda: source_lock.validate_contract(private), "private machine or device text")
    completed = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "crypto_candidate_source_lock.py"),
            "--contract",
            str(CONTRACT_PATH),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    assert json.loads(completed.stdout)["admission_sha256"] == EXPECTED_SHA256
    hostile = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "crypto_candidate_source_lock.py"),
            "--private=C:" + "\\Users\\operator\\source.json",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: invalid command line"
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr


def test_validator_has_no_acquisition_import_build_or_device_capability() -> None:
    source = (ROOT / "tools" / "crypto_candidate_source_lock.py").read_text(
        encoding="utf-8"
    )
    for token in (
        "import socket",
        "import requests",
        "import urllib",
        "import subprocess",
        "os.system",
        "Start-Process",
        "urlopen",
        "pip install",
        "idf.py build",
    ):
        assert token not in source
    assert "frozenset() for candidate in CANDIDATES" in source


def main() -> int:
    tests = (
        test_canonical_contract_is_exactly_blocked_and_uncredited,
        test_historical_plan_baseline_and_readiness_are_exactly_bound,
        test_candidate_order_versions_licenses_and_lock_kinds_are_exact,
        test_evidence_layers_cannot_individually_claim_a_source_lock,
        test_candidate_specific_trust_anchors_are_empty_and_self_claims_fail,
        test_v0_future_source_acceptance_is_permanently_disabled,
        test_v1_contract_and_license_evidence_fail_closed,
        test_v1_future_source_acceptance_returns_separate_license_facts,
        test_v1_license_facts_bind_through_api_and_import_chain,
        test_api_config_and_import_evidence_are_separately_bound_and_unaccepted,
        test_full_tree_license_sbom_transitive_and_patch_manifests_are_required,
        test_mbedtls_requires_exact_parent_idf_gitlink_binding,
        test_acquisition_receipts_and_lock_kinds_do_not_cross_candidates,
        test_path_symlink_reparse_and_case_safety_fail_closed,
        test_exact_types_cycles_depth_and_bounded_loader_fail_closed,
        test_private_text_and_cli_errors_are_sanitized,
        test_validator_has_no_acquisition_import_build_or_device_capability,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OTCSL0 candidate source-lock scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
