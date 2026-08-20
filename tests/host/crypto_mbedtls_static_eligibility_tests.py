#!/usr/bin/env python3
"""Adversarial tests for the host-only OTCMSE0 static-eligibility boundary."""

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

import crypto_mbedtls_static_eligibility as static_audit  # noqa: E402


CONTRACT_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json"
)
EXPECTED_SHA256 = "3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e"
HISTORICAL_BLOBS = {
    "tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json": "47c210c6257cd104d07f8e043f2cd1c688195136bbd3fcbafb8e6da095d18884",
    "tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json": "240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83",
    "tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json": "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae",
    "tests/benchmarks/crypto/OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json": "3fb904e1d5770613ec5d84560ea91dc3ec318a8a96c89c7d4333aa229267bab8",
    "tools/crypto_benchmark.py": "6d1f4bb8649018bc2801342ebaa8a3828fe724abdaa1309842c711b5562b989d",
    "tools/crypto_benchmark_baseline.py": "84e441141708d839d6cb13117476068a7c36570fdafd880173196205c778c747",
    "tools/crypto_benchmark_readiness.py": "eee3a1bdc2d24bce36059571c1764052ce12b1372a1fe305f95c6920e24b279a",
    "tools/crypto_candidate_source_lock.py": "a004c6bca6919ee81e597ad83f164ac800f7347e48a5fb04931f31a1025b6e5b",
}


def contract() -> dict:
    return static_audit.load(CONTRACT_PATH)


def expect_error(action, contains: str) -> None:
    try:
        action()
    except static_audit.ValidationError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected validation failure containing {contains!r}")


def expect_semantic_error(value: dict, contains: str) -> None:
    original = static_audit.EXPECTED_CONTRACT_SHA256
    try:
        static_audit.EXPECTED_CONTRACT_SHA256 = static_audit.canonical_sha256(value)
        expect_error(lambda: static_audit.validate(value), contains)
    finally:
        static_audit.EXPECTED_CONTRACT_SHA256 = original


def test_canonical_contract_and_public_result() -> None:
    value = contract()
    result = static_audit.validate(value)
    assert result["canonical_sha256"] == EXPECTED_SHA256
    assert result["present_operation_count"] == 5
    assert result["absent_operation_count"] == 3
    assert result["candidate_api_config_eligible"] is False
    assert result["source_lock_accepted"] is False
    assert result["readiness_advanced"] is False
    assert result["execution_authorized"] is False
    assert result["score_credit_added"] is False
    assert "INELIGIBLE" in result["public_result"]
    assert "BLOCKER4-REMAINS-OPEN" in result["public_result"]


def test_parent_contracts_and_historical_bytes_are_frozen() -> None:
    value = contract()
    assert value["parent_contracts"] == static_audit.EXPECTED_PARENT_CONTRACTS
    for logical_path, expected_sha in HISTORICAL_BLOBS.items():
        raw = subprocess.check_output(
            ["git", "-C", str(ROOT), "show", f"HEAD:{logical_path}"]
        )
        assert hashlib.sha256(raw).hexdigest() == expected_sha, logical_path
        subprocess.run(
            ["git", "-C", str(ROOT), "diff", "--quiet", "--", logical_path],
            check=True,
        )


def test_exact_provenance_and_license_boundary() -> None:
    value = contract()
    provenance = value["provenance"]
    assert provenance["esp_idf"]["source_commit"] == "7101770dc6db2667b3c477cc31365dd1acd6db4e"
    assert provenance["esp_idf"]["mbedtls_gitlink_commit"] == "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5"
    assert provenance["mbedtls"]["version"] == "4.1.0"
    assert provenance["mbedtls"]["tf_psa_is_separate_gitlink"] is False
    assert provenance["license"]["upstream_expression"] == "Apache-2.0 OR GPL-2.0-or-later"
    assert provenance["license"]["project_choice"] == "Apache-2.0"
    assert provenance["license"]["full_otcsle_license_inventory_complete"] is False
    assert provenance["source_acquired_or_imported_by_ot096"] is False

    forged = copy.deepcopy(value)
    forged["provenance"]["license"]["upstream_expression"] = "Apache-2.0"
    expect_semantic_error(forged, "upstream_expression")
    complete = copy.deepcopy(value)
    complete["provenance"]["license"]["full_otcsle_license_inventory_complete"] = True
    expect_semantic_error(complete, "complete OTCSLE")


def test_tree_glue_and_blob_anchors_are_exact_and_bounded() -> None:
    value = contract()
    assert value["provenance"]["mbedtls"]["full_tree_entry_count"] == 3551
    assert value["provenance"]["mbedtls"]["tf_psa_entry_count"] == 3203
    assert value["provenance"]["component_glue"]["entry_count"] == 199
    assert value["provenance"]["component_glue"]["gitlink_count"] == 1
    assert [item["anchor_id"] for item in value["source_anchors"]] == list(
        static_audit.SOURCE_ANCHOR_IDS
    )

    duplicate = copy.deepcopy(value)
    duplicate["source_anchors"][1]["anchor_id"] = duplicate["source_anchors"][0]["anchor_id"]
    expect_semantic_error(duplicate, "anchor_id")
    missing = copy.deepcopy(value)
    missing["source_anchors"].pop()
    expect_semantic_error(missing, "anchor count")
    invalid_hash = copy.deepcopy(value)
    invalid_hash["source_anchors"][0]["canonical_git_blob_sha256"] = "0" * 63
    expect_semantic_error(invalid_hash, "SHA-256")

    for index, anchor in enumerate(value["source_anchors"]):
        for field, replacement in (
            ("git_blob_oid", "f" * 40),
            ("canonical_git_blob_sha256", "f" * 64),
            ("canonical_git_blob_bytes", anchor["canonical_git_blob_bytes"] + 1),
        ):
            forged_anchor = copy.deepcopy(value)
            forged_anchor["source_anchors"][index][field] = replacement
            expect_semantic_error(forged_anchor, f"source_anchors[{index}].{field}")

    for section, field in (
        ("mbedtls", "full_tree_manifest_sha256"),
        ("mbedtls", "tf_psa_manifest_sha256"),
        ("component_glue", "manifest_sha256"),
    ):
        forged_manifest = copy.deepcopy(value)
        forged_manifest["provenance"][section][field] = "f" * 64
        expect_semantic_error(forged_manifest, field)

    forged_license = copy.deepcopy(value)
    forged_license["provenance"]["license"]["canonical_git_blob_sha256"] = "f" * 64
    expect_semantic_error(forged_license, "canonical_git_blob_sha256")


def test_fixed_operation_order_and_5_of_8_result() -> None:
    value = contract()
    operations = value["operation_matrix"]
    assert [item["operation"] for item in operations] == [item[0] for item in static_audit.OPERATION_POLICY]
    assert sum(item["source_implementation_present"] is True for item in operations) == 5
    assert sum(item["source_implementation_present"] is False for item in operations) == 3
    assert all(item["final_config_proven"] is False for item in operations)

    reordered = copy.deepcopy(value)
    reordered["operation_matrix"][0], reordered["operation_matrix"][1] = (
        reordered["operation_matrix"][1],
        reordered["operation_matrix"][0],
    )
    expect_semantic_error(reordered, "operation policy")
    removed = copy.deepcopy(value)
    removed["operation_matrix"].pop()
    expect_semantic_error(removed, "operation count")


def test_ed25519_identifiers_cannot_become_implementation_evidence() -> None:
    value = contract()
    for index in (0, 1):
        forged = copy.deepcopy(value)
        forged["operation_matrix"][index]["source_implementation_present"] = True
        forged["summary"]["source_implementation_present_count"] = 6
        forged["summary"]["source_implementation_absent_count"] = 2
        expect_semantic_error(forged, "operation policy")
    assert "PSA_ALG_PURE_EDDSA" in value["operation_matrix"][0]["api_symbols"]
    assert "no_ed25519_implementation" in value["operation_matrix"][0]["source_state"]


def test_noise_cannot_be_manufactured_from_primitive_presence() -> None:
    value = contract()
    noise = value["operation_matrix"][-1]
    assert noise["source_implementation_present"] is False
    assert noise["api_symbols"] == []
    assert noise["source_anchor_ids"] == []
    forged = copy.deepcopy(value)
    forged["operation_matrix"][-1]["source_implementation_present"] = True
    forged["operation_matrix"][-1]["api_symbols"] = ["psa_raw_key_agreement"]
    forged["operation_matrix"][-1]["source_anchor_ids"] = ["tf_psa_core"]
    forged["summary"]["source_implementation_present_count"] = 6
    forged["summary"]["source_implementation_absent_count"] = 2
    expect_semantic_error(forged, "operation policy")


def test_defaults_and_ot093_config_cannot_become_final_evidence() -> None:
    value = contract()
    config = value["configuration"]
    assert config["ot093_generated_sdkconfig_role"] == "PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0"
    assert config["final_candidate_sdkconfig_sha256"] is None
    assert config["final_candidate_config_resolved"] is False
    assert config["defaults_are_final_config_evidence"] is False
    assert config["default_source_states"][-1]["idf_default"] == "disabled"

    final = copy.deepcopy(value)
    final["configuration"]["final_candidate_sdkconfig_sha256"] = "1" * 64
    final["configuration"]["final_candidate_config_resolved"] = True
    expect_semantic_error(final, "final candidate sdkconfig")
    default_claim = copy.deepcopy(value)
    default_claim["configuration"]["defaults_are_final_config_evidence"] = True
    expect_semantic_error(default_claim, "defaults cannot")
    operation_claim = copy.deepcopy(value)
    operation_claim["operation_matrix"][2]["final_config_proven"] = True
    expect_semantic_error(operation_claim, "cannot prove final")


def test_summary_cannot_advance_readiness_or_close_blocker() -> None:
    for field in (
        "complete_fixed_operation_set_present",
        "final_candidate_config_resolved",
        "candidate_api_config_eligible",
        "source_lock_accepted",
        "readiness_advanced",
        "atomic_blocker_closed",
    ):
        forged = contract()
        forged["summary"][field] = True
        expect_semantic_error(forged, f"summary.{field}")


def test_exact_six_blockers_remain_open() -> None:
    value = contract()
    assert tuple(value["unchanged_blockers"]) == static_audit.EXPECTED_BLOCKERS
    missing = copy.deepcopy(value)
    missing["unchanged_blockers"].remove(
        "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved"
    )
    expect_semantic_error(missing, "six readiness blockers")
    reordered = copy.deepcopy(value)
    reordered["unchanged_blockers"].reverse()
    expect_semantic_error(reordered, "six readiness blockers")


def test_authority_and_claims_are_exact_false_booleans() -> None:
    value = contract()
    assert all(item is False for item in value["authority"].values())
    assert all(item is False for item in value["claims"].values())
    for section, field in (
        ("authority", "benchmark_execution_authorized"),
        ("authority", "key_or_entropy_operation_authorized"),
        ("claims", "source_lock_accepted"),
        ("claims", "score_credit_added"),
    ):
        forged = copy.deepcopy(value)
        forged[section][field] = True
        expect_semantic_error(forged, "authority or credit")
        wrong_type = copy.deepcopy(value)
        wrong_type[section][field] = 0
        expect_semantic_error(wrong_type, "Boolean")


def test_strict_types_large_integer_and_direct_subclasses() -> None:
    wrong_version = contract()
    wrong_version["version"] = False
    expect_semantic_error(wrong_version, "version")
    wrong_count = contract()
    wrong_count["summary"]["required_operation_count"] = True
    expect_semantic_error(wrong_count, "required_operation_count")
    huge = contract()
    huge["provenance"]["component_glue"]["entry_count"] = 1 << 200
    expect_error(lambda: static_audit.validate(huge), "integer exceeds")

    class HostileString(str):
        pass

    subclass = contract()
    subclass["status"] = HostileString(static_audit.STATUS)
    expect_error(lambda: static_audit.validate(subclass), "unsupported JSON type")


def test_direct_depth_cycle_and_repeated_container_guards() -> None:
    deep = contract()
    nested: dict = {}
    cursor = nested
    for _ in range(static_audit.MAX_DEPTH + 3):
        child: dict = {}
        cursor["x"] = child
        cursor = child
    deep["extra"] = nested
    expect_error(lambda: static_audit.validate(deep), "structural limits")

    cycle = contract()
    cycle["cycle"] = cycle
    expect_error(lambda: static_audit.validate(cycle), "repeated container")

    shared = {}
    repeated = contract()
    repeated["one"] = shared
    repeated["two"] = shared
    expect_error(lambda: static_audit.validate(repeated), "repeated container")


def test_loader_duplicate_utf8_size_and_depth_guards() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        duplicate = root / "duplicate.json"
        duplicate.write_text('{"schema":"OTCMSE0","schema":"OTCMSE0"}', encoding="utf-8")
        expect_error(lambda: static_audit.load(duplicate), "duplicate")

        invalid_utf8 = root / "invalid-utf8.json"
        invalid_utf8.write_bytes(b"{\"x\":\xff}")
        expect_error(lambda: static_audit.load(invalid_utf8), "unreadable or invalid")

        oversized = root / "oversized.json"
        oversized.write_bytes(b"{" + b" " * static_audit.MAX_BYTES + b"}")
        expect_error(lambda: static_audit.load(oversized), "size limit")

        deep_path = root / "deep.json"
        nested: dict = {"leaf": 0}
        for _ in range(static_audit.MAX_DEPTH + 3):
            nested = {"next": nested}
        deep_path.write_text(json.dumps(nested), encoding="utf-8")
        expect_error(lambda: static_audit.validate(static_audit.load(deep_path)), "structural limits")


def test_privacy_and_logical_path_guards() -> None:
    private_path = contract()
    private_path["public_note"] = "C:\\Users\\owner\\secret.txt"
    expect_error(lambda: static_audit.validate(private_path), "private text")
    private_mac = contract()
    private_mac["public_note"] = ":".join(("aa", "bb", "cc", "dd", "ee", "ff"))
    expect_error(lambda: static_audit.validate(private_mac), "private text")
    private_pin = contract()
    private_pin["public_note"] = "pin=123456"
    expect_error(lambda: static_audit.validate(private_pin), "private text")

    for unsafe in (
        "NUL",
        "components/CON.txt",
        "components/trailing.",
        "components/trailing ",
        "../escape",
        "C:/escape",
        "components\\escape",
    ):
        value = contract()
        value["source_anchors"][0]["logical_path"] = unsafe
        expect_semantic_error(value, "path")


def test_cli_success_is_public_safe_and_failures_are_sanitized() -> None:
    script = ROOT / "tools" / "crypto_mbedtls_static_eligibility.py"
    accepted = subprocess.run(
        [sys.executable, str(script), str(CONTRACT_PATH)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert accepted.returncode == 0, accepted.stderr
    assert accepted.stderr == ""
    result = json.loads(accepted.stdout)
    assert result["canonical_sha256"] == EXPECTED_SHA256
    assert result["execution_authorized"] is False
    assert result["score_credit_added"] is False
    assert "\\" not in accepted.stdout
    assert str(ROOT) not in accepted.stdout

    with tempfile.TemporaryDirectory() as temp:
        hostile_value = contract()
        hostile_value["public_note"] = "password=do-not-echo"
        hostile_path = Path(temp) / "hostile.json"
        hostile_path.write_text(json.dumps(hostile_value), encoding="utf-8")
        rejected = subprocess.run(
            [sys.executable, str(script), str(hostile_path)],
            capture_output=True,
            text=True,
            check=False,
        )
        assert rejected.returncode == 2
        assert rejected.stdout == ""
        assert rejected.stderr.strip() == "ERROR: contract validation failed"
        assert "do-not-echo" not in rejected.stderr
        assert str(hostile_path) not in rejected.stderr

    hostile_argv = subprocess.run(
        [sys.executable, str(script), "--unknown", "C:\\Users\\owner\\secret.json"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert hostile_argv.returncode == 2
    assert hostile_argv.stdout == ""
    assert hostile_argv.stderr.strip() == "ERROR: invalid command line"
    assert "secret.json" not in hostile_argv.stderr


def test_digest_anchor_rejects_any_unreviewed_static_change() -> None:
    value = contract()
    original = static_audit.EXPECTED_CONTRACT_SHA256
    try:
        static_audit.EXPECTED_CONTRACT_SHA256 = "f" * 64
        expect_error(lambda: static_audit.validate(value), "digest is not accepted")
    finally:
        static_audit.EXPECTED_CONTRACT_SHA256 = original


def main() -> int:
    tests = (
        test_canonical_contract_and_public_result,
        test_parent_contracts_and_historical_bytes_are_frozen,
        test_exact_provenance_and_license_boundary,
        test_tree_glue_and_blob_anchors_are_exact_and_bounded,
        test_fixed_operation_order_and_5_of_8_result,
        test_ed25519_identifiers_cannot_become_implementation_evidence,
        test_noise_cannot_be_manufactured_from_primitive_presence,
        test_defaults_and_ot093_config_cannot_become_final_evidence,
        test_summary_cannot_advance_readiness_or_close_blocker,
        test_exact_six_blockers_remain_open,
        test_authority_and_claims_are_exact_false_booleans,
        test_strict_types_large_integer_and_direct_subclasses,
        test_direct_depth_cycle_and_repeated_container_guards,
        test_loader_duplicate_utf8_size_and_depth_guards,
        test_privacy_and_logical_path_guards,
        test_cli_success_is_public_safe_and_failures_are_sanitized,
        test_digest_anchor_rejects_any_unreviewed_static_change,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OTCMSE0 Mbed TLS static-eligibility scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
