"""Narrow corrected-Monocypher admission using hash-pinned OT149 accounting rules.

The isolated successor changes only its identity, corrected configuration pin,
and accepted OT143 provenance binding. Historical admissions remain unchanged.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "tools/crypto_matched_resource_accounting.py"
BASE_SHA256 = "54ac45f5676605c767cfa6144d75e67c12aa919228491d68967c8a7d53910b7e"
CONTRACT_ID = "OT-163-OT005-MONOCYPHER-CORRECTED-RESOURCE-CONTRACT-V1"
CONTRACT_REPO_PATH = "tests/benchmarks/crypto/" + CONTRACT_ID + ".json"
CORRECTED_CONFIG = "5807fe7fc6d4ef3325f06099674f07080660eabd20e0b078225247605c814817"
BUILD_BINDING = (
    "tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json",
    "1045d5d59c26775b8a8c2a8520226fcb224b566f5554b5ff495b9876a8af8c37",
    "4fb65c92ab6b6664954ce84ba0da4ed9c4032b47d6d81ff0cc37186efb116d4d",
)


def _engine() -> dict[str, Any]:
    raw = BASE_PATH.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASE_SHA256:
        raise ValueError("accounting predecessor source changed")
    source = raw.decode("utf-8")
    replacements = (
        ('OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1', CONTRACT_ID, 2),
        ('SCHEMA = "OTMRAC1"', 'SCHEMA = "OT163MCC1"', 1),
        ('RESULT_SCHEMA = "OTMRAR1"', 'RESULT_SCHEMA = "OT163MCR1"', 1),
        ('value["accepted_date"] == "2026-08-26"', 'value["accepted_date"] == "2026-09-10"', 1),
        ('4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f', CORRECTED_CONFIG, 1),
    )
    for before, after, count in replacements:
        if source.count(before) != count:
            raise ValueError("accounting successor anchor changed")
        source = source.replace(before, after)
    scope: dict[str, Any] = {"__name__": "_ot163_corrected_accounting", "__file__": str(BASE_PATH)}
    exec(compile(source, str(BASE_PATH), "exec"), scope)
    scope["PARENT_BINDINGS"]["corrected_target_build"] = BUILD_BINDING
    return scope


def validate_contract(value: dict[str, Any], root: Path = ROOT) -> dict[str, Any]:
    return _engine()["validate_contract"](value, root)


def validate_result(contract: dict[str, Any], result: dict[str, Any], *,
                    root: Path = ROOT, contract_path: Path | None = None) -> dict[str, Any]:
    if result.get("candidate_id") != "monocypher":
        raise ValueError("successor accepts corrected Monocypher only")
    return _engine()["validate_result"](contract, result, root=root, contract_path=contract_path)
