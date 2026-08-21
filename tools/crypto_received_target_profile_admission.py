#!/usr/bin/env python3
"""Strict offline validator for the OT-103 received-target profile delta."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OT094 = ROOT / "tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json"
OT102 = ROOT / "tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json"
OT059 = ROOT / "tests/hardware/OT-059-2026-08-15.md"
OT061 = ROOT / "tests/hardware/OT-061-2026-08-16.md"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json"
ADMISSION = ROOT / "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json"

OT094_SHA = "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae"
OT102_SHA = "6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52"
OT059_SHA = "4cce747a9b39354346efc638cde2c6851e17a0b6cc6fd3339302c3674a34ba9e"
OT061_SHA = "fb5c9a00222cffecf7afc991866f5e86e8f21cc6fe92e1bf917dd1912644564a"
EVIDENCE_SHA = "517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e"
ADMISSION_SHA = "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105"
PDF_SHA = "d284d4f01f9e801bb8407386cf50ee4d099ed3c3f5e9153683cb5819b53f7f4d"
PDF_URL = "https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf"
RESULT = "EXACT-RECEIVED-TARGET-PROFILE-ADMITTED-FOR-OT-DEV-001; THREE-OTCBR0-REQUIREMENTS-REMAIN; NO-SUPPORT-COMPATIBILITY-REGULATORY-RADIO-PROFILE-BENCHMARK-OR-SELECTION; OTCBR0-READINESS-BLOCKED"

HISTORICAL_SIX = [
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "espressif_libsodium_source_lock_absent",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "monocypher_source_lock_absent",
    "direct_radio_mtu_phy_region_unresolved",
]
PRIOR_FOUR = [
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "direct_radio_mtu_phy_region_unresolved",
]
CURRENT_THREE = PRIOR_FOUR[1:]
PARENTS = {
    "otcbr0_v0_raw_sha256": OT094_SHA,
    "otmsla0_v0_raw_sha256": OT102_SHA,
    "ot059_raw_sha256": OT059_SHA,
    "ot061_raw_sha256": OT061_SHA,
}
MAX_BYTES = 65_536
MAX_DEPTH = 16
MAX_NODES = 4096
MAX_STRING = 2048
HEX64 = re.compile(r"^[0-9a-f]{64}$")
PRIVATE = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(r"\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]", re.IGNORECASE),
)


class AdmissionError(ValueError):
    """Evidence is malformed, mutated, private, or exceeds its authority."""


def _module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AdmissionError("parent validator unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _pairs(items):
    value = {}
    for key, item in items:
        if key in value:
            raise AdmissionError("duplicate key")
        value[key] = item
    return value


def _scan(value, depth=0, nodes=None):
    nodes = [0] if nodes is None else nodes
    nodes[0] += 1
    if depth > MAX_DEPTH or nodes[0] > MAX_NODES:
        raise AdmissionError("bounds")
    if type(value) is dict:
        for key, item in value.items():
            if type(key) is not str or not key or len(key) > MAX_STRING or unicodedata.normalize("NFC", key) != key:
                raise AdmissionError("key")
            _scan(item, depth + 1, nodes)
    elif type(value) is list:
        for item in value:
            _scan(item, depth + 1, nodes)
    elif type(value) is str:
        if not value or len(value) > MAX_STRING or unicodedata.normalize("NFC", value) != value or any(pattern.search(value) for pattern in PRIVATE):
            raise AdmissionError("text")
    elif value is not None and type(value) not in (bool, int):
        raise AdmissionError("scalar")


def _raw(path: Path, digest: str, limit=MAX_BYTES) -> bytes:
    with Path(path).open("rb") as source:
        raw = source.read(limit + 1)
    if not raw or len(raw) > limit or hashlib.sha256(raw).hexdigest() != digest:
        raise AdmissionError("immutable bytes")
    return raw


def _json(path: Path, digest: str | None = None):
    with Path(path).open("rb") as source:
        raw = source.read(MAX_BYTES + 1)
    if not raw or len(raw) > MAX_BYTES or (digest is not None and hashlib.sha256(raw).hexdigest() != digest):
        raise AdmissionError("json bytes")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise AdmissionError("json") from exc
    _scan(value)
    if type(value) is not dict:
        raise AdmissionError("json root")
    return value


def _keys(value, expected, name):
    if type(value) is not dict or set(value) != set(expected):
        raise AdmissionError(name + " fields")


def _same(value, expected):
    if type(value) is not type(expected):
        return False
    if type(expected) is dict:
        return set(value) == set(expected) and all(_same(value[key], item) for key, item in expected.items())
    if type(expected) is list:
        return len(value) == len(expected) and all(_same(item, wanted) for item, wanted in zip(value, expected))
    return value == expected


def _exact(value, expected, name):
    if not _same(value, expected):
        raise AdmissionError(name)


def _exact_false(value, keys, name):
    _keys(value, keys, name)
    if any(type(item) is not bool or item for item in value.values()):
        raise AdmissionError(name)


def _validate_parent_chain(ot094_path, ot102_path, ot059_path, ot061_path):
    readiness = _json(Path(ot094_path), OT094_SHA)
    _raw(Path(ot059_path), OT059_SHA)
    _raw(Path(ot061_path), OT061_SHA)
    ot102_module = _module("ot103_ot102", ROOT / "tools/crypto_monocypher_source_lock_admission.py")
    try:
        prior = ot102_module.validate(Path(ot102_path))
    except Exception as exc:
        raise AdmissionError("OT-102 parent") from exc
    if readiness.get("schema") != "OTCBR0" or readiness.get("version") != 0 or readiness.get("blockers") != HISTORICAL_SIX:
        raise AdmissionError("OT-094 parent")
    if prior.get("current_four_blockers") != PRIOR_FOUR or prior.get("acceptance_counts") != {"api_config": 0, "candidate_import": 0, "source": 2}:
        raise AdmissionError("OT-102 state")
    return readiness, prior


def _validate_evidence(value):
    _keys(value, {"schema", "version", "artifact_kind", "evidence_id", "accepted_date", "status", "target_binding", "exact_profile", "parent_evidence", "owner_photo_evidence", "official_manufacturer_source", "closure_basis", "boundaries"}, "evidence")
    identity = (value["schema"], value["version"], value["artifact_kind"], value["evidence_id"], value["accepted_date"], value["status"])
    if identity != ("OTRTPE0", 0, "exact_received_target_profile_evidence", "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0", "2026-08-20", "exact_received_target_profile_evidenced") or type(value["version"]) is not int:
        raise AdmissionError("evidence identity")
    target = {
        "target_id": "heltec-v4-bench-candidate",
        "evidence_unit": "OT-DEV-001",
        "manufacturer": "Heltec Automation",
        "commercial_family": "WiFi LoRa 32 V4",
        "pcb_model": "HTIT-WB32LAF",
        "exact_received_revision": "V4.2",
        "manufacturer_variant_role": "documented_high_band_variant",
    }
    profile = {
        "mcu_family": "ESP32-S3",
        "manufacturer_part": "ESP32-S3R2",
        "processor_revision": "v0.2",
        "crystal_mhz": 40,
        "flash_bytes": 16777216,
        "psram_bytes": 2097152,
        "manufacturer_lora_chip_family": "SX1262",
        "rf_variant_model": "HTIT-WB32LAF",
    }
    _exact(value["target_binding"], target, "target binding")
    _exact(value["exact_profile"], profile, "exact profile")
    _exact(value["parent_evidence"], PARENTS, "evidence parents")
    expected_photos = [
        {"photo_id": "owner-photo-01-gnss-view", "sha256": "1c4c162cc6baecc88bfa145e3677806d960e9eb108621e57ebd17556bc827208", "bytes": 205908, "width_px": 960, "height_px": 1280, "role": "gnss_daughterboard_marking_corroboration", "privacy_safe_markings": ["QUECTEL", "L76K"], "closure_input": False},
        {"photo_id": "owner-photo-02-mcu-pcb-view", "sha256": "5ab60a7b2328b6f2320fdadb8e1b7dad14b2cac39ec20febba27f2bc23c2c733", "bytes": 241142, "width_px": 960, "height_px": 1280, "role": "pcb_model_revision_and_mcu_marking", "privacy_safe_markings": ["HTIT-WB32LAF", "V4.2", "ESP32-S3", "ESP32-S3R2", "40.000 MHZ"], "closure_input": True},
        {"photo_id": "owner-photo-03-assembled-side-view", "sha256": "325a87f128d4becffc17f6d7c20acf756a11eb6061af89b9067b309a6a69611f", "bytes": 120968, "width_px": 1280, "height_px": 960, "role": "assembled_unit_component_association", "privacy_safe_markings": [], "assembly_observations": ["display_and_board_co_located", "l76k_daughterboard_attached"], "closure_input": False},
        {"photo_id": "owner-photo-04-front-pcb-view", "sha256": "92bf6a9a0e2e96d1bac5ff414a1dc92d69e7a81e0a89e77b46e0ac6490495b49", "bytes": 128451, "width_px": 1280, "height_px": 960, "role": "assembled_unit_revision_corroboration", "privacy_safe_markings": ["V4"], "closure_input": False},
        {"photo_id": "owner-photo-05-package-label", "sha256": "e774e8663f6884301b384b5a44f68fe48dbe8e505ab18183f431f972478a96b5", "bytes": 98820, "width_px": 960, "height_px": 1280, "role": "owner_matched_original_package_label_corroboration", "privacy_safe_markings": ["WiFi LoRa 32 V4", "LoRa Dev-kits", "LoRa Band", "HF 863-928"], "printed_checkbox_state_claimed": False, "closure_input": False},
    ]
    if not _same(value["owner_photo_evidence"], expected_photos) or len({item["sha256"] for item in value["owner_photo_evidence"]}) != 5:
        raise AdmissionError("photo evidence")
    official = {
        "url": PDF_URL,
        "sha256": PDF_SHA,
        "bytes": 1349532,
        "document_title": "WiFi LoRa 32 V4",
        "document_version": "V4.2.0",
        "retained_in_repository": False,
        "facts": [
            {"table": "1.5", "model": "HTIT-WB32LAF", "frequency_min_mhz": 868, "frequency_max_mhz": 928, "maximum_tx_power_dbm": 28, "maximum_tx_power_tolerance_db": 1},
            {"table": "3.5.1", "model": "HTIT-WB32LAF", "frequency_min_mhz": 863, "frequency_max_mhz": 928, "maximum_tx_power_dbm": 28, "maximum_tx_power_tolerance_db": 1},
            {"table": "3.1", "master_chip": "ESP32-S3R2", "lora_chip": "SX1262", "flash_bytes": 16777216, "psram_bytes": 2097152},
        ],
    }
    if not _same(value["official_manufacturer_source"], official) or official["facts"][0]["frequency_min_mhz"] == official["facts"][1]["frequency_min_mhz"]:
        raise AdmissionError("manufacturer source")
    closure = {
        "primary_inputs": ["owner-photo-02-mcu-pcb-view", "OT-059", "OT-061", "official-table-1.5", "official-table-3.5.1"],
        "corroborating_inputs": ["owner-photo-01-gnss-view", "owner-photo-03-assembled-side-view", "owner-photo-04-front-pcb-view", "owner-photo-05-package-label"],
        "resolved_field_set": ["manufacturer", "board_model", "exact_received_revision", "rf_variant_model", "mcu", "processor_revision", "flash_bytes", "psram_bytes"],
    }
    _exact(value["closure_basis"], closure, "closure basis")
    boundary_fields = {"raw_photos_retained_in_repository", "pdf_retained_in_repository", "local_paths_retained", "exif_or_location_retained", "private_device_identifier_retained", "package_checkbox_state_claimed", "band_values_normalized_or_reconciled", "manufacturer_lora_chip_electrically_verified", "rf_front_end_electrically_verified", "installed_antenna_verified", "direct_radio_profile_resolved", "legal_region_selected", "regulatory_acceptance_claimed", "hardware_support_claimed", "compatibility_claimed", "final_candidate_configuration_proven", "benchmark_executed", "candidate_selected", "firmware_changed", "device_accessed_for_this_increment", "flashed", "radio_used", "key_or_entropy_operation", "score_credit_added"}
    _exact_false(value["boundaries"], boundary_fields, "boundaries")
    return value


def validate(admission_path=ADMISSION, evidence_path=EVIDENCE, ot094_path=OT094, ot102_path=OT102, ot059_path=OT059, ot061_path=OT061, enforce_digest=True):
    _, prior = _validate_parent_chain(ot094_path, ot102_path, ot059_path, ot061_path)
    evidence = _validate_evidence(_json(Path(evidence_path), EVIDENCE_SHA if enforce_digest else None))
    admission = _json(Path(admission_path), ADMISSION_SHA if enforce_digest else None)
    _keys(admission, {"schema", "version", "artifact_kind", "admission_id", "accepted_date", "status", "public_result", "parents", "profile_evidence", "accepted_target_profile", "evidence_counts", "preserved_crypto_acceptance_counts", "historical_six_blockers", "prior_current_four_blockers", "closed_by_this_delta", "current_three_blockers", "owner_evidence", "authority", "claims"}, "admission")
    identity = (admission["schema"], admission["version"], admission["artifact_kind"], admission["admission_id"], admission["accepted_date"], admission["status"], admission["public_result"])
    if identity != ("OTRTPA0", 0, "append_only_exact_received_target_profile_acceptance_delta", "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0", "2026-08-20", "exact_received_target_profile_admitted_readiness_blocked", RESULT) or type(admission["version"]) is not int:
        raise AdmissionError("admission identity")
    accepted = {"target_id": "heltec-v4-bench-candidate", "evidence_unit": "OT-DEV-001", "manufacturer": "Heltec Automation", "board_model": "HTIT-WB32LAF", "exact_received_revision": "V4.2", "rf_variant_model": "HTIT-WB32LAF", "mcu": "ESP32-S3", "manufacturer_part": "ESP32-S3R2", "processor_revision": "v0.2", "flash_bytes": 16777216, "psram_bytes": 2097152, "supported": False}
    if not _same(admission["parents"], PARENTS) or not _same(evidence["parent_evidence"], PARENTS) or not _same(admission["profile_evidence"], {"path": "tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json", "sha256": EVIDENCE_SHA}) or not _same(admission["accepted_target_profile"], accepted):
        raise AdmissionError("admission binding")
    if not _same(admission["evidence_counts"], {"owner_photos": 5, "closure_input_photos": 1, "corroborating_photos": 4, "official_source_documents": 1, "immutable_parent_records": 4}) or not _same(admission["preserved_crypto_acceptance_counts"], prior["acceptance_counts"]) or not _same(admission["preserved_crypto_acceptance_counts"], {"source": 2, "api_config": 0, "candidate_import": 0}):
        raise AdmissionError("counts")
    if not _same(admission["historical_six_blockers"], HISTORICAL_SIX) or not _same(admission["prior_current_four_blockers"], prior["current_four_blockers"]) or not _same(admission["prior_current_four_blockers"], PRIOR_FOUR) or not _same(admission["current_three_blockers"], CURRENT_THREE):
        raise AdmissionError("blockers")
    if not _same(admission["closed_by_this_delta"], [{"blocker_id": HISTORICAL_SIX[0], "closure_evidence_sha256": EVIDENCE_SHA}]):
        raise AdmissionError("closure")
    if not _same(admission["owner_evidence"], {"owner_supplied_photo_set": True, "received_unit_association": "OT-DEV-001", "new_device_access_for_this_increment": False}):
        raise AdmissionError("owner evidence")
    authority_fields = {"benchmark_build_authorized", "benchmark_execution_authorized", "candidate_import_authorized", "device_access_authorized", "final_candidate_configuration_authorized", "key_or_entropy_operation_authorized", "packet_v1_authorized", "radio_profile_selection_authorized", "radio_transmit_authorized", "score_credit_added", "suite_selection_authorized"}
    _exact_false(admission["authority"], authority_fields, "authority")
    claim_fields = {"exact_received_target_profile_accepted", "physical_evidence_added", "libsodium_source_lock_remains_accepted", "monocypher_source_lock_remains_accepted", "readiness_accepted", "final_candidate_configuration_proven", "direct_radio_profile_resolved", "radio_region_selected", "regulatory_acceptance_claimed", "hardware_support_claimed", "compatibility_claimed", "manufacturer_lora_chip_electrically_verified", "benchmark_executed", "candidate_selected", "suite_selected", "packet_v1_wire_selected", "hardware_or_device_accessed_for_this_increment", "firmware_changed", "flashed", "radio_used", "key_or_entropy_operation", "score_credit_added"}
    _keys(admission["claims"], claim_fields, "claims")
    true_claims = {"exact_received_target_profile_accepted", "physical_evidence_added", "libsodium_source_lock_remains_accepted", "monocypher_source_lock_remains_accepted"}
    if any(type(item) is not bool or item is not (name in true_claims) for name, item in admission["claims"].items()):
        raise AdmissionError("claims")
    return admission


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("admission", nargs="?", type=Path, default=ADMISSION)
    parser.add_argument("--evidence", type=Path, default=EVIDENCE)
    parser.add_argument("--ot094", type=Path, default=OT094)
    parser.add_argument("--ot102", type=Path, default=OT102)
    parser.add_argument("--ot059", type=Path, default=OT059)
    parser.add_argument("--ot061", type=Path, default=OT061)
    args = parser.parse_args(argv)
    try:
        record = validate(args.admission, args.evidence, args.ot094, args.ot102, args.ot059, args.ot061)
    except (OSError, AdmissionError, KeyError, TypeError, UnicodeError, RecursionError, json.JSONDecodeError):
        print("OTRTPA0 validation failed", file=sys.stderr)
        return 2
    print(record["public_result"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
