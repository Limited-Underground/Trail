#!/usr/bin/env python3
"""Strict host-only OT-108 per-candidate API/config acceptance contract."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA, VERSION = "OTCAC0", 1
CONTRACT_ID = "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1"
PUBLIC_RESULT = ("PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-FROZEN-HOST-ONLY; "
                 "PARTIAL-COMPARISONS-STRUCTURALLY-NONSELECTABLE; ZERO-API-CONFIG-EVIDENCE-ACCEPTED; "
                 "TWO-OTCBR0-REQUIREMENTS-REMAIN; OTCBR0-READINESS-BLOCKED")
EXPECTED_CONTRACT_SHA256 = "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22"
MAX_BYTES, MAX_DEPTH, MAX_NODES, MAX_STRING = 131072, 16, 4096, 512
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$")
EVIDENCE_ID = re.compile(r"^OT-[0-9]{3}-OT005-[A-Z0-9-]+-API-CONFIG-EVIDENCE-V2$")
OPERATIONS = ("ed25519_sign", "ed25519_verify", "x25519", "sha256", "hkdf_sha256",
              "chacha20poly1305_encrypt", "chacha20poly1305_decrypt", "noise_xk_handshake")
PARENTS = {
    "otcsl0_v1_policy_sha256": "51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a",
    "otcsla0_v0_raw_sha256": "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0",
    "otmsla0_v0_raw_sha256": "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52",
    "otmpsla0_v0_raw_sha256": "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85",
    "otcbcga0_v0_raw_sha256": "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
}
CANDIDATES = (
    ("espressif_libsodium", "primary", "8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9", "df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0", "b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f"),
    ("esp_idf_mbedtls_psa", "comparison", "ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49", "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85", "9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686"),
    ("monocypher", "comparison", "fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f", "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52", "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"),
)
CANDIDATE_RECORDS = tuple({"candidate_id": i, "role": r, "source_evidence_sha256": s,
                           "source_admission_raw_sha256": a, "generated_sdkconfig_sha256": c}
                          for i, r, s, a, c in CANDIDATES)
CANDIDATE_BY_ID = {c["candidate_id"]: c for c in CANDIDATE_RECORDS}
ACCEPTED_API_CONFIG_EVIDENCE_SHA256 = {c[0]: frozenset() for c in CANDIDATES}
AUTHORITY_FIELDS = ("candidate_import_authorized", "benchmark_build_authorized", "benchmark_execution_authorized",
                    "device_access_authorized", "radio_transmit_authorized", "key_or_entropy_operation_authorized",
                    "candidate_selection_authorized", "suite_selection_authorized", "packet_v1_authorized", "score_credit_added")
CLAIM_FIELDS = ("api_config_evidence_accepted", "candidate_imported", "candidate_benchmark_executed", "candidate_selected",
                "suite_selected", "packet_v1_wire_selected", "hardware_or_device_accessed", "physical_evidence_added", "score_credit_added")

class ValidationError(ValueError): pass

class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, "ERROR: invalid arguments\n")

def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    out = {}
    for key, value in items:
        if key in out: raise ValidationError("JSON contains a duplicate key")
        out[key] = value
    return out

def _obj(value: Any, name: str) -> dict[str, Any]:
    if type(value) is not dict: raise ValidationError(f"{name} must be an object")
    return value

def _arr(value: Any, name: str) -> list[Any]:
    if type(value) is not list: raise ValidationError(f"{name} must be an array")
    return value

def _keys(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected: raise ValidationError(f"{name} fields are not exact")

def _text(value: Any, name: str) -> str:
    if type(value) is not str or not value or len(value) > MAX_STRING: raise ValidationError(f"{name} must be bounded text")
    return value

def _sha(value: Any, name: str) -> str:
    value = _text(value, name)
    if not HEX64.fullmatch(value): raise ValidationError(f"{name} must be a lowercase SHA-256")
    return value

def _bool(value: Any, name: str, expected: bool) -> None:
    if type(value) is not bool or value is not expected: raise ValidationError(f"{name} must be {str(expected).lower()}")

def _scan(value: Any) -> None:
    seen, nodes = set(), 0
    def visit(item: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if depth > MAX_DEPTH or nodes > MAX_NODES: raise ValidationError("document exceeds structural bounds")
        if type(item) in (dict, list):
            if id(item) in seen: raise ValidationError("document contains a cycle")
            seen.add(id(item))
            for child in (item.values() if type(item) is dict else item): visit(child, depth + 1)
            seen.remove(id(item))
        elif type(item) is str:
            if len(item) > MAX_STRING: raise ValidationError("document string exceeds limit")
            if re.search(r"[A-Za-z]:\\|/(?:home|users)/|\bCOM[0-9]+\b|\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", item, re.I):
                raise ValidationError("document contains private machine or device text")
        elif item is not None and type(item) not in (bool, int): raise ValidationError("document contains a noncanonical JSON type")
    visit(value, 0)

def load(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if len(raw) > MAX_BYTES: raise ValidationError("JSON exceeds size limit")
        return _obj(json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs), "document")
    except ValidationError: raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc: raise ValidationError("JSON unreadable or invalid") from exc

def canonical_sha256(value: Any) -> str:
    try: raw = json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode()
    except (TypeError, ValueError, RecursionError, OverflowError) as exc: raise ValidationError("canonical serialization failed") from exc
    return hashlib.sha256(raw).hexdigest()

def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    _scan(contract); contract = _obj(contract, "contract")
    _keys(contract, {"schema", "version", "artifact_kind", "contract_id", "accepted_date", "status", "public_result", "parents",
                     "evidence_policy", "candidates", "accepted_api_config_evidence_sha256", "acceptance_counts", "current_two_blockers", "authority", "claims"}, "contract")
    if (contract["schema"], contract["version"], contract["artifact_kind"], contract["contract_id"], contract["accepted_date"], contract["status"], contract["public_result"]) != (
        SCHEMA, VERSION, "per_candidate_api_config_acceptance_contract", CONTRACT_ID, "2026-08-21", "per_candidate_api_config_acceptance_contract_frozen_host_only", PUBLIC_RESULT):
        raise ValidationError("canonical contract identity or result mismatch")
    if contract["parents"] != PARENTS: raise ValidationError("accepted predecessor binding mismatch")
    policy = {"evidence_schema":"OTCAPI0", "evidence_version":2, "operation_order":list(OPERATIONS),
              "coverage_states":["complete_selectable","comparison_partial"], "partial_requires_comparison_role":True,
              "partial_requires_strict_nonempty_subset":True, "complete_coverage_required_for_selection_eligibility":True,
              "selection_eligibility_does_not_authorize_selection":True, "candidate_specific_sdkconfig_required":True,
              "common_sdkconfig_forbidden":True, "independent_digest_admission_required":True}
    if contract["evidence_policy"] != policy or contract["candidates"] != list(CANDIDATE_RECORDS): raise ValidationError("candidate or evidence policy mismatch")
    anchors = _obj(contract["accepted_api_config_evidence_sha256"], "anchors"); _keys(anchors, set(CANDIDATE_BY_ID), "anchors")
    if any(anchors[c] != [] for c in anchors): raise ValidationError("accepted API/config registry must remain empty")
    if contract["acceptance_counts"] != {"source":3,"api_config":0,"candidate_import":0}: raise ValidationError("acceptance counts mismatch")
    if contract["current_two_blockers"] != ["esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved", "direct_radio_mtu_phy_region_unresolved"]: raise ValidationError("two-blocker state mismatch")
    for name, fields in (("authority", AUTHORITY_FIELDS), ("claims", CLAIM_FIELDS)):
        item = _obj(contract[name], name); _keys(item, set(fields), name)
        for field in fields: _bool(item[field], f"{name}.{field}", False)
    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 and digest != EXPECTED_CONTRACT_SHA256: raise ValidationError("canonical contract digest mismatch")
    return {"schema":SCHEMA,"version":VERSION,"contract_id":CONTRACT_ID,"status":contract["status"],"public_result":PUBLIC_RESULT,
            "contract_sha256":digest,"source_count":3,"accepted_api_config_count":0,"candidate_import_count":0,"blocker_count":2,
            "execution_authorized":False,"selection_authorized":False,"score_credit_added":False}

def validate_evidence_structure(evidence: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    contract_result = validate_contract(contract); _scan(evidence); evidence = _obj(evidence, "evidence")
    _keys(evidence, {"schema","version","artifact_kind","evidence_id","recorded_date","contract_sha256","candidate_id","role",
                     "source_evidence_sha256","final_sdkconfig_sha256","operation_results","coverage_state","comparison_measurement_eligible",
                     "selection_eligible","result","execution_authorized","selection_authorized","score_credit_added"}, "evidence")
    if evidence["schema"] != "OTCAPI0" or evidence["version"] != 2 or evidence["artifact_kind"] != "candidate_api_config_evidence": raise ValidationError("evidence schema/version/kind mismatch")
    if not EVIDENCE_ID.fullmatch(_text(evidence["evidence_id"], "evidence_id")) or not DATE.fullmatch(_text(evidence["recorded_date"], "recorded_date")): raise ValidationError("evidence identifier or date mismatch")
    if _sha(evidence["contract_sha256"], "contract_sha256") != contract_result["contract_sha256"]: raise ValidationError("evidence contract binding mismatch")
    candidate = CANDIDATE_BY_ID.get(_text(evidence["candidate_id"], "candidate_id"))
    if candidate is None or evidence["role"] != candidate["role"]: raise ValidationError("candidate identity or role mismatch")
    if _sha(evidence["source_evidence_sha256"], "source_evidence_sha256") != candidate["source_evidence_sha256"]: raise ValidationError("source evidence binding mismatch")
    if _sha(evidence["final_sdkconfig_sha256"], "final_sdkconfig_sha256") != candidate["generated_sdkconfig_sha256"]: raise ValidationError("candidate sdkconfig binding mismatch")
    operations = _arr(evidence["operation_results"], "operation_results")
    if len(operations) != len(OPERATIONS): raise ValidationError("operation results must preserve fixed set")
    digests = []
    for index, expected_id in enumerate(OPERATIONS):
        item = _obj(operations[index], f"operation[{index}]"); _keys(item, {"operation_id","state","evidence_sha256"}, f"operation[{index}]")
        if item["operation_id"] != expected_id: raise ValidationError("operation identity or order mismatch")
        if item["state"] == "eligible": digests.append(_sha(item["evidence_sha256"], f"operation[{index}].evidence_sha256"))
        elif item["state"] == "unavailable":
            if item["evidence_sha256"] is not None: raise ValidationError("unavailable operation cannot carry evidence")
        else: raise ValidationError("operation state mismatch")
    if len(digests) != len(set(digests)): raise ValidationError("operation evidence digests must be purpose-distinct")
    complete, partial = len(digests) == len(OPERATIONS), 0 < len(digests) < len(OPERATIONS)
    if complete:
        if evidence["coverage_state"] != "complete_selectable" or evidence["result"] != "complete_api_config_eligible": raise ValidationError("complete coverage mismatch")
        _bool(evidence["selection_eligible"], "selection_eligible", True)
    elif partial:
        if candidate["role"] != "comparison": raise ValidationError("partial evidence requires comparison role")
        if evidence["coverage_state"] != "comparison_partial" or evidence["result"] != "partial_comparison_only": raise ValidationError("partial coverage mismatch")
        _bool(evidence["selection_eligible"], "selection_eligible", False)
    else: raise ValidationError("partial comparison must be a strict nonempty subset")
    _bool(evidence["comparison_measurement_eligible"], "comparison_measurement_eligible", True)
    for field in ("execution_authorized","selection_authorized","score_credit_added"): _bool(evidence[field], field, False)
    return {"schema":"OTCAPI0","version":2,"candidate_id":candidate["candidate_id"],"role":candidate["role"],
            "source_evidence_sha256":candidate["source_evidence_sha256"],"final_sdkconfig_sha256":candidate["generated_sdkconfig_sha256"],
            "coverage_state":evidence["coverage_state"],"eligible_operation_count":len(digests),"comparison_measurement_eligible":True,
            "selection_eligible":complete,"execution_authorized":False,"selection_authorized":False,"score_credit_added":False,
            "api_config_evidence_sha256":canonical_sha256(evidence)}

def validate_evidence(evidence: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    result = validate_evidence_structure(evidence, contract)
    if result["api_config_evidence_sha256"] not in ACCEPTED_API_CONFIG_EVIDENCE_SHA256[result["candidate_id"]]: raise ValidationError("API/config evidence digest is not independently accepted")
    return result

def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(); parser.add_argument("--contract", required=True); args = parser.parse_args(argv)
    try: result = validate_contract(load(Path(args.contract)))
    except ValidationError: print("ERROR: validation failed", file=sys.stderr); return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":"))); return 0

if __name__ == "__main__": raise SystemExit(main())
