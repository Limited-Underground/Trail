#!/usr/bin/env python3
"""Adversarial host tests for the OT-120 retained import/build contract lane."""
from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import crypto_candidate_import_build_admission as validator  # noqa: E402


CONTRACT = ROOT / "tests/benchmarks/crypto/OT-120-OT005-CANDIDATE-IMPORT-BUILD-CONTRACT-V1.json"


def contract() -> dict:
    return validator.load(CONTRACT)


def expect_error(action, text: str = "") -> None:
    try:
        action()
    except validator.ValidationError as exc:
        assert text in str(exc), (text, str(exc))
        return
    raise AssertionError("expected validation failure")


def synthetic_graph(candidate: dict) -> list[dict]:
    entries = [{
        "record_kind": "build_graph_entry",
        "logical_path": "candidate/source.c",
        "artifact_role": "candidate_source",
        "bytes": 101,
        "sha256": hashlib.sha256(candidate["candidate_id"].encode()).hexdigest(),
        "retained_in_final_link": False,
        "operation_id": None,
    }]
    for operation in candidate["eligible_operations"]:
        entries.append({
            "record_kind": "build_graph_entry",
            "logical_path": f"operations/{operation}.o",
            "artifact_role": "benchmark_adapter",
            "bytes": 202,
            "sha256": hashlib.sha256(operation.encode()).hexdigest(),
            "retained_in_final_link": True,
            "operation_id": operation,
        })
    entries.sort(key=lambda item: item["logical_path"])
    return [{
        "schema": "OTCIBG1",
        "version": 1,
        "record_kind": "header",
        "candidate_id": candidate["candidate_id"],
        "entry_count": len(entries),
        "source_evidence_sha256": candidate["source_evidence_sha256"],
        "api_config_evidence_sha256": candidate["api_config_evidence_canonical_sha256"],
        "generated_sdkconfig_sha256": candidate["generated_sdkconfig_sha256"],
    }, *entries]


def synthetic_evidence(candidate: dict, value: dict, graph_raw: str) -> dict:
    slug = dict(validator.CANDIDATE_SLUGS)[candidate["candidate_id"]]
    artifacts = []
    for role in validator.ARTIFACT_ROLES:
        if role == "generated_sdkconfig":
            size = candidate["generated_sdkconfig_bytes"]
            digest = candidate["generated_sdkconfig_sha256"]
            name = "sdkconfig"
        else:
            name = f"{role}.bin"
            size = 1000 + len(role)
            digest = hashlib.sha256(f"{candidate['candidate_id']}:{role}".encode()).hexdigest()
        artifacts.append({"role": role, "name": name, "bytes": size, "sha256": digest})
    source_bindings = {
        field: candidate[field] for field in (
            "version", "source_evidence_sha256", "source_admission_canonical_sha256",
            "project_dependency_lock_sha256", "source_manifest_sha256", "sbom_sha256",
            "api_config_evidence_canonical_sha256", "generated_sdkconfig_bytes",
            "generated_sdkconfig_sha256", "coverage_state", "eligible_operations",
            "unavailable_operations",
        )
    }
    graph_binding = {
        "path": f"tests/benchmarks/crypto/OT-120-OT005-{slug}-IMPORT-BUILD-GRAPH-V1.jsonl",
        "raw_sha256": graph_raw,
        "entry_count": len(synthetic_graph(candidate)) - 1,
        "manifest_kind": "canonical-lf-jsonl-build-graph-v1",
    }
    normalized = validator.canonical_sha256({
        "candidate_id": candidate["candidate_id"],
        "source_bindings": source_bindings,
        "target": value["target"],
        "toolchain": value["toolchain"],
        "graph_binding": graph_binding,
        "artifacts": artifacts,
    })
    runs = []
    for suffix in ("a", "b"):
        runs.append({
            "profile": f"ot120-{slug.lower()}-{suffix}",
            "initial_build_directory_absent": True,
            "build_exit_code": 0,
            "compiler_warning_count": 0,
            "raw_build_evidence_sha256": hashlib.sha256(f"raw:{candidate['candidate_id']}:{suffix}".encode()).hexdigest(),
            "normalized_receipt_sha256": normalized,
            "artifacts": copy.deepcopy(artifacts),
        })
    return {
        "schema": "OTCIBE1",
        "version": 1,
        "artifact_kind": "retained_candidate_import_build_evidence",
        "evidence_id": f"OT-120-OT005-{slug}-IMPORT-BUILD-EVIDENCE-V1",
        "recorded_date": "2026-08-22",
        "status": "candidate_import_build_evidence_complete_pending_atomic_admission",
        "public_result": "HOST-ONLY-IMPORT-BUILD-PASSED-PENDING-ATOMIC-ADMISSION",
        "contract_raw_sha256": hashlib.sha256(CONTRACT.read_bytes()).hexdigest(),
        "contract_canonical_sha256": validator.canonical_sha256(value),
        "candidate_id": candidate["candidate_id"],
        "source_bindings": source_bindings,
        "target": copy.deepcopy(value["target"]),
        "toolchain": copy.deepcopy(value["toolchain"]),
        "graph_binding": graph_binding,
        "build_reproducibility": {
            "clean_run_count": 2,
            "independent_build_directories": True,
            "shared_compiler_cache_disabled": True,
            "component_manager_network_disabled": True,
            "reproducible_paths_normalized": True,
            "runs": runs,
        },
        "one_time_authority": {
            "host_only_phase_one_instruction_used": True, "consumed": True,
            "hardware_scope_included": False, "benchmark_execution_scope_included": False,
        },
        "boundaries": {
            "benchmark_executed": False, "device_accessed": False,
            "flashed": False, "radio_used": False, "key_or_entropy_operation": False,
            "candidate_selected": False, "suite_selected": False,
            "packet_v1_authorized": False, "score_credit_added": False,
        },
        "claims": {
            "candidate_imported_for_benchmark": True,
            "candidate_benchmark_built": True,
            "benchmark_executed": False, "hardware_or_device_accessed": False,
            "candidate_selected": False, "support_proven": False,
            "compatibility_proven": False, "regulatory_compliance_proven": False,
            "score_credit_added": False,
        },
    }


def evidence_fixture(candidate: dict, value: dict):
    graph = synthetic_graph(candidate)
    graph_raw = hashlib.sha256(
        "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n" for item in graph).encode()
    ).hexdigest()
    return graph, synthetic_evidence(candidate, value, graph_raw), graph_raw


def synthetic_evidence_raw(evidence: dict) -> str:
    raw = json.dumps(evidence, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n"
    return hashlib.sha256(raw.encode()).hexdigest()


def validate_synthetic_evidence(evidence: dict, *args, **kwargs) -> dict:
    candidate_id = evidence["candidate_id"]
    if type(candidate_id) is not str or candidate_id not in validator.EXPECTED_EVIDENCE_SHA256:
        return validator.validate_evidence(evidence, *args, **kwargs)
    expected = validator.EXPECTED_EVIDENCE_SHA256[candidate_id]
    validator.EXPECTED_EVIDENCE_SHA256[candidate_id] = None
    try:
        return validator.validate_evidence(evidence, *args, **kwargs)
    finally:
        validator.EXPECTED_EVIDENCE_SHA256[candidate_id] = expected


def validate_synthetic_admission(admission: dict, *args, **kwargs) -> dict:
    expected = validator.EXPECTED_ADMISSION_SHA256
    validator.EXPECTED_ADMISSION_SHA256 = None
    try:
        return validator.validate_admission(admission, *args, **kwargs)
    finally:
        validator.EXPECTED_ADMISSION_SHA256 = expected


def synthetic_admission(results: list[dict], evidences: list[dict], graph_raws: list[str], value: dict) -> dict:
    return {
        "schema": "OTCIBA1", "version": 1,
        "artifact_kind": "append_only_atomic_candidate_import_build_admission",
        "admission_id": "OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1",
        "accepted_date": "2026-08-22",
        "status": "phase_one_complete_measurement_awaits_fresh_authority",
        "public_result": "THREE-CANDIDATE-IMPORT-BUILD-ADMISSIONS-ACCEPTED; COUNTS-3-3-3; PHASE-1-COMPLETE; MEASUREMENT-AWAITS-FRESH-AUTHORITY",
        "parents": {
            "contract_raw_sha256": hashlib.sha256(CONTRACT.read_bytes()).hexdigest(),
            "contract_canonical_sha256": validator.canonical_sha256(value),
            "otrtpa1_v1_raw_sha256": "afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36",
            "otrtpa1_v1_canonical_sha256": "0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443",
        },
        "accepted_candidate_imports": [
            {
                "candidate_id": result["candidate_id"],
                "evidence_raw_sha256": result["evidence_raw_sha256"],
                "evidence_canonical_sha256": result["evidence_sha256"],
                "graph_raw_sha256": graph_raw,
            }
            for result, evidence, graph_raw in zip(results, evidences, graph_raws)
        ],
        "acceptance_counts": {"exact_profile_units": 2, "source": 3, "api_config": 3, "candidate_import": 3},
        "phases": {"phase_zero_complete": True, "phase_one_complete": True, "phase_two_execution_admitted": False, "phase_three_admission_complete": False},
        "measurement_blockers": ["fresh_benchmark_execution_authority_absent"],
        "one_time_authority": {"host_only_phase_one_instruction_used": True, "consumed": True, "hardware_scope_included": False, "benchmark_execution_scope_included": False},
        "continuing_authority": {
            "candidate_import_authorized": False, "benchmark_build_authorized": False,
            "benchmark_execution_authorized": False, "device_access_authorized": False,
            "flash_authorized": False, "radio_transmit_authorized": False,
            "key_or_entropy_operation_authorized": False, "candidate_selection_authorized": False,
            "suite_selection_authorized": False, "packet_v1_authorized": False,
            "score_credit_added": False,
        },
        "claims": {
            "candidate_imports_accepted": True, "candidate_benchmark_builds_accepted": True,
            "phase_one_complete": True, "measurement_ready": False,
            "benchmark_executed": False, "hardware_or_device_accessed": False,
            "candidate_selected": False, "suite_selected": False,
            "support_proven": False, "compatibility_proven": False,
            "regulatory_compliance_proven": False, "score_credit_added": False,
        },
    }


def test_contract_is_exact_and_all_parents_are_live() -> None:
    value = contract()
    result = validator.validate_contract(value)
    assert result["candidate_count"] == 3
    assert result["prior_candidate_import_count"] == 0
    assert result["accepted_candidate_import_count"] == 3
    assert result["phase_zero_complete"] is True
    assert result["phase_one_contract_frozen"] is True
    assert result["measurement_ready"] is False
    assert result["contract_sha256"] == validator.canonical_sha256(value)
    for parent, expected in zip(value["append_only_parents"], validator.PARENTS):
        assert tuple(parent.values()) == expected


def test_ot099_is_historical_and_cannot_be_promoted() -> None:
    value = contract()
    assert value["historical_non_admitting_reference"]["admissible_for_phase_one"] is False
    changed = copy.deepcopy(value)
    changed["historical_non_admitting_reference"]["admissible_for_phase_one"] = True
    expect_error(lambda: validator.validate_contract(changed, verify_parent_files=False), "OT-099")


def test_candidate_order_counts_configs_and_coverage_are_exact() -> None:
    value = contract()
    assert value["candidate_order"] == [item[0] for item in validator.CANDIDATE_SLUGS]
    assert [len(item["eligible_operations"]) for item in value["candidates"]] == [8, 5, 5]
    assert [item["selection_eligible"] for item in value["candidates"]] == [True, False, False]
    mutations = [
        (("candidate_order",), list(reversed(value["candidate_order"]))),
        (("candidates", 0, "generated_sdkconfig_sha256"), "0" * 64),
        (("candidates", 1, "sbom_sha256"), "1" * 64),
        (("candidates", 2, "eligible_operations"), ["x25519"]),
        (("admission_policy", "accepted_counts", "candidate_import"), 2),
    ]
    for path, replacement in mutations:
        changed = copy.deepcopy(value)
        target = changed
        for part in path[:-1]:
            target = target[part]
        target[path[-1]] = replacement
        expect_error(lambda changed=changed: validator.validate_contract(changed, verify_parent_files=False))


def test_contract_authority_is_one_time_and_fail_closed() -> None:
    value = contract()
    authority = value["authority"]
    assert authority["contract_validation_authorized"] is True
    assert authority["one_time_host_only_phase_one_authorized"] is True
    assert authority["one_time_authority_consumed_by_admission_required"] is True
    for field in authority:
        if field.startswith("continuing_") or field in {
            "benchmark_execution_authorized", "device_access_authorized", "flash_authorized",
            "radio_transmit_authorized", "key_or_entropy_operation_authorized",
            "candidate_selection_authorized", "suite_selection_authorized",
            "packet_v1_authorized", "score_credit_added",
        }:
            assert authority[field] is False
    changed = copy.deepcopy(value)
    changed["authority"]["device_access_authorized"] = True
    expect_error(lambda: validator.validate_contract(changed, verify_parent_files=False), "authority")


def test_graph_requires_sorted_unique_candidate_and_operation_anchors() -> None:
    value = contract()
    for candidate in value["candidates"]:
        graph = synthetic_graph(candidate)
        result = validator.validate_graph(graph, candidate)
        assert result["eligible_anchors_retained"] is True
        assert result["unavailable_anchors_retained"] is False
        changed = copy.deepcopy(graph)
        changed.pop()
        changed[0]["entry_count"] -= 1
        expect_error(lambda changed=changed, candidate=candidate: validator.validate_graph(changed, candidate), "anchors")
        if candidate["unavailable_operations"]:
            changed = copy.deepcopy(graph)
            changed.append({
                "record_kind": "build_graph_entry", "logical_path": "zz/unavailable.o",
                "artifact_role": "benchmark_adapter", "bytes": 1,
                "sha256": "2" * 64, "retained_in_final_link": True,
                "operation_id": candidate["unavailable_operations"][0],
            })
            changed[0]["entry_count"] += 1
            expect_error(lambda changed=changed, candidate=candidate: validator.validate_graph(changed, candidate), "unavailable")


def test_evidence_requires_two_equal_clean_zero_warning_builds() -> None:
    value = contract()
    candidate = value["candidates"][0]
    graph, evidence, graph_raw = evidence_fixture(candidate, value)
    result = validate_synthetic_evidence(evidence, graph, value, evidence_raw_sha256=synthetic_evidence_raw(evidence), graph_raw_sha256=graph_raw)
    assert result["candidate_imported"] is True
    assert result["candidate_benchmark_built"] is True
    assert result["benchmark_executed"] is False
    for path, replacement, message in (
        (("build_reproducibility", "runs", 0, "compiler_warning_count"), 1, "build run"),
        (("build_reproducibility", "runs", 1, "artifacts", 0, "sha256"), "3" * 64, "equality"),
        (("build_reproducibility", "runs", 0, "artifacts", 5, "sha256"), "4" * 64, "sdkconfig"),
        (("boundaries", "device_accessed"), True, "authority"),
        (("claims", "benchmark_executed"), True, "claims"),
    ):
        changed = copy.deepcopy(evidence)
        target = changed
        for part in path[:-1]:
            target = target[part]
        target[path[-1]] = replacement
        expect_error(
            lambda changed=changed: validate_synthetic_evidence(changed, graph, value, evidence_raw_sha256=synthetic_evidence_raw(changed), graph_raw_sha256=graph_raw),
            message,
        )


def test_atomic_admission_rejects_partial_counts_authority_and_claims() -> None:
    value = contract()
    graphs, evidences, graph_raws, results = [], [], [], []
    for candidate in value["candidates"]:
        graph, evidence, graph_raw = evidence_fixture(candidate, value)
        graphs.append(graph)
        evidences.append(evidence)
        graph_raws.append(graph_raw)
        results.append(validate_synthetic_evidence(evidence, graph, value, evidence_raw_sha256=synthetic_evidence_raw(evidence), graph_raw_sha256=graph_raw))
    admission = synthetic_admission(results, evidences, graph_raws, value)
    result = validate_synthetic_admission(admission, value, results)
    assert result["candidate_import_count"] == 3
    assert result["phase_one_complete"] is True
    assert result["measurement_ready"] is False
    changed = copy.deepcopy(admission)
    changed["accepted_candidate_imports"].pop()
    expect_error(lambda: validate_synthetic_admission(changed, value, results[:2]), "atomic")
    changed = copy.deepcopy(admission)
    changed["acceptance_counts"]["candidate_import"] = 2
    expect_error(lambda: validate_synthetic_admission(changed, value, results), "count")
    changed = copy.deepcopy(admission)
    changed["measurement_blockers"] = []
    expect_error(lambda: validate_synthetic_admission(changed, value, results), "blocker")
    changed = copy.deepcopy(admission)
    changed["continuing_authority"]["benchmark_execution_authorized"] = True
    expect_error(lambda: validate_synthetic_admission(changed, value, results), "authority")


def test_malformed_shapes_fail_closed_without_runtime_exceptions() -> None:
    value = contract()
    changed_contract = copy.deepcopy(value)
    changed_contract["candidates"][0] = None
    expect_error(
        lambda: validator.validate_contract(changed_contract, verify_parent_files=False),
        "candidate set",
    )
    expect_error(lambda: validator.validate_graph([], value["candidates"][0]), "at least one")

    candidate = value["candidates"][0]
    graph, evidence, graph_raw = evidence_fixture(candidate, value)
    changed_evidence = copy.deepcopy(evidence)
    changed_evidence["candidate_id"] = []
    expect_error(
        lambda: validate_synthetic_evidence(
            changed_evidence, graph, value,
            evidence_raw_sha256=synthetic_evidence_raw(changed_evidence), graph_raw_sha256=graph_raw,
        ),
        "candidate",
    )
    changed_evidence = copy.deepcopy(evidence)
    del changed_evidence["build_reproducibility"]["runs"]
    expect_error(
        lambda: validate_synthetic_evidence(
            changed_evidence, graph, value,
            evidence_raw_sha256=synthetic_evidence_raw(changed_evidence), graph_raw_sha256=graph_raw,
        ),
        "keys",
    )
    changed_evidence = copy.deepcopy(evidence)
    changed_evidence["build_reproducibility"]["runs"][0]["artifacts"][0] = None
    expect_error(
        lambda: validate_synthetic_evidence(
            changed_evidence, graph, value,
            evidence_raw_sha256=synthetic_evidence_raw(changed_evidence), graph_raw_sha256=graph_raw,
        ),
        "artifact roles",
    )

def test_private_text_duplicate_keys_and_partial_cli_fail_closed() -> None:
    value = contract()
    changed = copy.deepcopy(value)
    changed["public_result"] = "C:\\Users\\operator\\private.txt"
    expect_error(lambda: validator.validate_contract(changed, verify_parent_files=False), "private")
    with tempfile.TemporaryDirectory() as temporary:
        duplicate = Path(temporary) / "duplicate.json"
        duplicate.write_text('{"schema":"OTCIBC1","schema":"OTCIBC1"}', encoding="utf-8")
        expect_error(lambda: validator.load(duplicate), "duplicate")
    completed = subprocess.run(
        [sys.executable, str(validator.__file__), "--contract", str(CONTRACT)],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert completed.returncode == 0, completed.stderr
    payload = json.loads(completed.stdout)
    assert payload["schema"] == "OTCIBC1" and payload["measurement_ready"] is False
    completed = subprocess.run(
        [sys.executable, str(validator.__file__), "--contract", str(CONTRACT), "--admission", "missing.json"],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert completed.returncode == 2 and "exactly three" in completed.stderr


def test_pinned_live_phase_one_artifacts_validate_atomically() -> None:
    expected_graphs = {
        "espressif_libsodium": "c939dbc7afbc103a44c16d92474528acc782ca442873f06fa9a8a8b04aaec20c",
        "esp_idf_mbedtls_psa": "7338e2383f152d554d6a64e3d46e7260b0c9dba79099311ec7e140b1ccde7a55",
        "monocypher": "9e59ab26cca582301027ee3544bfd69643dfde5b8a84a2ae494cb98969ba9645",
    }
    expected_evidence_raw = {
        "espressif_libsodium": "735b4755f25da280cde7ba79387f5eeeee5a38bf477b60042e64d31f20f2186f",
        "esp_idf_mbedtls_psa": "e4cf79a47c6cd3a64f44412bf6b010815e7e66f3e7633242d2c2c89c61bf1307",
        "monocypher": "4ba0b05ab4ee043e506b8bc15d5140451b0e6b1b95fd5eb46bae26a595789633",
    }
    expected_evidence_canonical = {
        "espressif_libsodium": "28c98e83cf2149177353f47346e8c37d263e8a436a10bff3b3f4cefe7608bd49",
        "esp_idf_mbedtls_psa": "5ed9d04e6d773be599e22bbccb3a8117850d99636dfc3a30adeefcc1f384866d",
        "monocypher": "390a94a0d256f4a8863c0d44363b788c7b8c9a91c4e94bebdf9356d8ca1a0c61",
    }
    assert validator.EXPECTED_GRAPH_RAW_SHA256 == expected_graphs
    assert validator.EXPECTED_EVIDENCE_RAW_SHA256 == expected_evidence_raw
    assert validator.EXPECTED_EVIDENCE_SHA256 == expected_evidence_canonical
    assert validator.EXPECTED_ADMISSION_RAW_SHA256 == "90af31966553bee58fcf71e4decfee8d2bcadfee58ef026e3f96cffcd6f45ccf"
    assert validator.EXPECTED_ADMISSION_SHA256 == "0c55f49803d833c075670b17fa8d033bd5a7cd4997e8714ff247161f7fa2057b"
    command = [sys.executable, str(validator.__file__), "--contract", str(CONTRACT)]
    command.extend(("--admission", str(validator.ADMISSION)))
    for candidate_id, _ in validator.CANDIDATE_SLUGS:
        command.extend(("--evidence", str(validator.EVIDENCE_PATHS[candidate_id])))
    for candidate_id, _ in validator.CANDIDATE_SLUGS:
        command.extend(("--graph", str(validator.GRAPH_PATHS[candidate_id])))
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    assert completed.returncode == 0, completed.stderr
    payload = json.loads(completed.stdout)
    assert payload == {
        "admission_sha256": validator.EXPECTED_ADMISSION_SHA256,
        "candidate_import_count": 3,
        "execution_authorized": False,
        "measurement_ready": False,
        "phase_one_complete": True,
        "phase_zero_complete": True,
        "schema": "OTCIBA1",
        "score_credit_added": False,
        "selection_authorized": False,
        "version": 1,
    }


def test_validator_has_no_hardware_or_network_runtime_imports() -> None:
    source = Path(validator.__file__).read_text(encoding="utf-8")
    for token in ("import serial", "import requests", "import socket", "import bleak", "esptool"):
        assert token not in source


def main() -> int:
    tests = [
        test_contract_is_exact_and_all_parents_are_live,
        test_ot099_is_historical_and_cannot_be_promoted,
        test_candidate_order_counts_configs_and_coverage_are_exact,
        test_contract_authority_is_one_time_and_fail_closed,
        test_graph_requires_sorted_unique_candidate_and_operation_anchors,
        test_evidence_requires_two_equal_clean_zero_warning_builds,
        test_atomic_admission_rejects_partial_counts_authority_and_claims,
        test_malformed_shapes_fail_closed_without_runtime_exceptions,
        test_private_text_duplicate_keys_and_partial_cli_fail_closed,
        test_pinned_live_phase_one_artifacts_validate_atomically,
        test_validator_has_no_hardware_or_network_runtime_imports,
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-120 retained import/build contract scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
