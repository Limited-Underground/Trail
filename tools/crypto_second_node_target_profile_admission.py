#!/usr/bin/env python3
"""Validate strict append-only OT-119 second-node exact-profile admission."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "tests/benchmarks/crypto"
HARDWARE = ROOT / "tests/hardware"

RECEIPT = CRYPTO / "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-USB-RECEIPT-V0.json"
EVIDENCE = CRYPTO / "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-EVIDENCE-V1.json"
ADMISSION = CRYPTO / "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1.json"

EXPECTED_RECEIPT_RAW_SHA256 = "16e69159aed7b9e7d9304cd7cc16d25b7205fc9283ee7751f26b3b9580df5f7c"
EXPECTED_RECEIPT_SHA256 = "e89f3e027f695d88e764af01b1e032b360a23455a7122121833720d2fbf7adf7"
EXPECTED_EVIDENCE_RAW_SHA256 = "0e8a9862091f7a1c58630bb64fc9250bdb24bddfdf8c09856629dd7dc73255e1"
EXPECTED_EVIDENCE_SHA256 = "7f470316d446cdc3be5a878580418c08bff628e703dd8f419aa5e83f9001d223"
EXPECTED_ADMISSION_RAW_SHA256 = "afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36"
EXPECTED_ADMISSION_SHA256 = "0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443"
HISTORICAL = {
    "otrtpe0": (
        CRYPTO / "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json",
        "517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e",
        "68ebd7818a356500fd941e66e685a84d55d5d01f4446efeaaaf728d47efa68be",
    ),
    "otrtpa0": (
        CRYPTO / "OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json",
        "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105",
        "dc7247ae9b277418c104690193fa7bfce2d9297038d2c24c2f5daedf4dc3331e",
    ),
    "otrer0": (
        HARDWARE / "OT-114-2026-08-21-EXECUTION-RECEIPT-V0.json",
        "d285d43ec30b2d81473b37bf189b14d89db389cb1636cacbe59cf9f84825d1dd",
        "1700446be2216f6520859928e941a72a06605dfdeedad6957b6e4f8d5259e8c4",
    ),
    "otrpe1": (
        CRYPTO / "OT-114-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-V1.json",
        "b6d2a7ce4ebe3ab233ebbc748ab7831ff12cf4d8f6504d2d7e23dae108bd5876",
        "ac7e77a4438772a4c5b5f2b17472b302a3520e186a21b88125a9314ee6998bf0",
    ),
    "otrpa1": (
        CRYPTO / "OT-114-OT005-US915-DIRECT-RADIO-PROFILE-ADMISSION-DELTA-V1.json",
        "19325f730b96b9dbeeb4f64682c4913e7586d1995ef419b26408d82be12ef266",
        "eecf2b821ef2c25274cc5d3a179494b1545eb4a859280a48459ebb83c79ed257",
    ),
    "otcbr1": (
        CRYPTO / "OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json",
        "333f8d525160f45627a13913e5d1adabe8e5c8374290af32b9af1df96ef1bd7e",
        "ad10935a52bbbcb1ed06f523ef5084a8d70b91aca355d7b497e9ad54c18f453e",
    ),
    "otcbx1": (
        CRYPTO / "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json",
        "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
        "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8",
    ),
    "otmapia0": (
        CRYPTO / "OT-118-OT005-MONOCYPHER-API-CONFIG-ADMISSION-DELTA-V0.json",
        "9fbecf19b206b31fae948b6bc7e7aa4e206ba26aa59b94fb7f07d4e1d300810a",
        "df412285515fe29525b0bfd7cba45fd7ccd9a3d601be284242886e8adb19fec9",
    ),
}
OT115 = HARDWARE / "OT-115-2026-08-21.md"
OT115_RAW_SHA256 = "4ea35e4b694133c7e3c7a3499c35da13916cfab1938b7186cf21bc4f2df5a175"

MAX_BYTES = 262_144
PRIVATE_TEXT = re.compile(
    r"[A-Za-z]:\\|/(?:home|users)/|\bCOM[0-9]+\b|"
    r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b|"
    r"\b(?:pin|password|private[_ -]?key|secret)\s*[:=]",
    re.IGNORECASE,
)


class ValidationError(ValueError):
    """Artifact is malformed, private, mutated, or exceeds its authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, "ERROR: invalid arguments\n")


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValidationError("duplicate key")
        result[key] = value
    return result


def load(path: Path, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if not raw or len(raw) > MAX_BYTES:
            raise ValidationError("JSON size invalid")
        if expected_raw_sha256 and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
            raise ValidationError("raw artifact digest mismatch")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
        if type(value) is not dict:
            raise ValidationError("document must be object")
        return value
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as exc:
        raise ValidationError("JSON unreadable or invalid") from exc


def canonical_sha256(value: Any) -> str:
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValidationError("canonical serialization failed") from exc
    return hashlib.sha256(encoded).hexdigest()


def _scan(value: Any, depth: int = 0) -> None:
    if depth > 20:
        raise ValidationError("document exceeds structural bounds")
    if type(value) is dict:
        if len(value) > 128:
            raise ValidationError("document exceeds structural bounds")
        for item in value.values():
            _scan(item, depth + 1)
    elif type(value) is list:
        if len(value) > 128:
            raise ValidationError("document exceeds structural bounds")
        for item in value:
            _scan(item, depth + 1)
    elif type(value) is str:
        if len(value) > 2048 or PRIVATE_TEXT.search(value):
            raise ValidationError("private or unbounded text")
    elif value is not None and type(value) not in (bool, int):
        raise ValidationError("noncanonical JSON type")


def _file_sha256(path: Path, expected: str) -> None:
    try:
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as exc:
        raise ValidationError("bound file unreadable") from exc
    if actual != expected:
        raise ValidationError("bound file digest mismatch")


def _exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    if type(value) is not dict or set(value) != expected:
        raise ValidationError(f"{label} keys mismatch")


def _historical() -> dict[str, dict[str, Any]]:
    records = {}
    for name, (path, raw_sha256, canonical_digest) in HISTORICAL.items():
        record = load(path, raw_sha256)
        if canonical_sha256(record) != canonical_digest:
            raise ValidationError("historical canonical digest mismatch")
        records[name] = record
    _file_sha256(OT115, OT115_RAW_SHA256)
    if records["otrtpe0"]["target_binding"]["evidence_unit"] != "OT-DEV-001":
        raise ValidationError("OT-103 unit mismatch")
    if records["otrpe1"]["bounded_owner_context"]["owner_confirmed"] != [
        "usa_location",
        "attached_supplied_high_band_antennas",
        "two_v4_2_diagnostic_nodes",
        "device_access",
        "firmware_build",
        "flash",
        "low_power_close_bench_transmit",
    ]:
        raise ValidationError("OT-114 corroboration mismatch")
    if records["otrpe1"]["artifact_bindings"]["per_node_exact_image_context_receipts"] != 2:
        raise ValidationError("OT-114 node count mismatch")
    if records["otcbx1"]["target"]["physical_nodes_required_for_measurement"] != 2:
        raise ValidationError("OT-116 target count mismatch")
    if records["otcbx1"]["target"]["second_node_exact_profile_admitted"] is not False:
        raise ValidationError("OT-116 historical state mismatch")
    if records["otmapia0"]["acceptance_counts"] != {
        "source": 3,
        "api_config": 3,
        "candidate_import": 0,
    }:
        raise ValidationError("OT-118 count mismatch")
    if records["otmapia0"]["phase_zero"]["remaining"] != [
        "independent_second_node_exact_profile_admission"
    ]:
        raise ValidationError("OT-118 phase-zero mismatch")
    return records


def validate_receipt(receipt: dict[str, Any]) -> dict[str, Any]:
    _scan(receipt)
    _exact_keys(
        receipt,
        {
            "schema", "version", "artifact_kind", "receipt_id", "observed_date",
            "status", "public_result", "target_selection", "usb_observation",
            "owner_photo_observation", "privacy", "boundaries",
        },
        "receipt",
    )
    identity = (
        receipt["schema"], receipt["version"], receipt["artifact_kind"],
        receipt["receipt_id"], receipt["observed_date"], receipt["status"],
    )
    if identity != (
        "OTRTPR0", 0, "privacy_safe_second_node_exact_profile_observation_receipt",
        "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-USB-RECEIPT-V0",
        "2026-08-22", "second_node_read_only_profile_observed",
    ) or type(receipt["version"]) is not int:
        raise ValidationError("receipt identity mismatch")
    if receipt["target_selection"] != {
        "target_id": "heltec-v4-bench-candidate",
        "evidence_unit": "OT-DEV-002",
        "simultaneously_connected_node_count": 2,
        "selected_unit_owner_confirmed": True,
        "distinct_from_ot_dev_001": True,
    }:
        raise ValidationError("selected unit mismatch")
    if receipt["usb_observation"] != {
        "mode": "read_only_no_stub_rom_queries",
        "operations": ["chip_identity", "flash_identity"],
        "normalized_facts": {
            "mcu_family": "ESP32-S3",
            "processor_revision": "v0.2",
            "crystal_mhz": 40,
            "embedded_psram_bytes": 2097152,
            "flash_bytes": 16777216,
        },
        "transient_rom_entry_or_reset": True,
        "persistent_state_changed": False,
        "firmware_heartbeat_after_probe": "ot_bench",
        "heartbeat_restored": True,
    }:
        raise ValidationError("USB observation mismatch")
    if receipt["owner_photo_observation"] != {
        "sha256": "a4068a1d33b2dd6342422b968c4634ba390810ae1f7ca197bcbd24543b42ce9e",
        "bytes": 244612,
        "width_px": 960,
        "height_px": 1280,
        "owner_supplied": True,
        "same_selected_unit": True,
        "supplied_immediately_after_selected_unit_reboot_confirmation": True,
        "visible_markings": ["HTIT-WB32LAF", "V4.2"],
        "raw_photo_retained_in_repository": False,
        "local_path_retained": False,
        "exif_or_location_retained": False,
    }:
        raise ValidationError("owner photo observation mismatch")
    if receipt["privacy"] != {
        "raw_probe_output_retained": False,
        "serial_port_retained": False,
        "usb_serial_or_hardware_path_retained": False,
        "mac_or_chip_identifier_retained": False,
        "private_device_identifier_retained": False,
        "raw_flash_bytes_read_or_retained": False,
        "raw_efuse_content_retained": False,
        "ble_identity_or_pin_retained": False,
        "coordinate_retained": False,
        "local_path_retained": False,
    }:
        raise ValidationError("privacy boundary mismatch")
    if receipt["boundaries"] != {
        "device_accessed_read_only": True,
        "physical_evidence_added": True,
        "firmware_changed": False,
        "flashed": False,
        "flash_read": False,
        "efuse_read_or_changed": False,
        "radio_used": False,
        "key_or_entropy_operation": False,
        "benchmark_executed": False,
        "candidate_selected": False,
        "hardware_support_claimed": False,
        "compatibility_claimed": False,
        "regulatory_acceptance_claimed": False,
        "score_credit_added": False,
    }:
        raise ValidationError("receipt authority boundary mismatch")
    digest = canonical_sha256(receipt)
    if digest != EXPECTED_RECEIPT_SHA256:
        raise ValidationError("receipt canonical digest mismatch")
    return {"receipt_sha256": digest, "evidence_unit": "OT-DEV-002"}


def _expected_evidence_parents() -> dict[str, str]:
    return {
        "otrtpe0_v0_raw_sha256": HISTORICAL["otrtpe0"][1],
        "otrtpe0_v0_canonical_sha256": HISTORICAL["otrtpe0"][2],
        "otrtpa0_v0_raw_sha256": HISTORICAL["otrtpa0"][1],
        "otrtpa0_v0_canonical_sha256": HISTORICAL["otrtpa0"][2],
        "otrer0_v0_raw_sha256": HISTORICAL["otrer0"][1],
        "otrer0_v0_canonical_sha256": HISTORICAL["otrer0"][2],
        "otrpe1_v1_raw_sha256": HISTORICAL["otrpe1"][1],
        "otrpe1_v1_canonical_sha256": HISTORICAL["otrpe1"][2],
        "otrpa1_v1_raw_sha256": HISTORICAL["otrpa1"][1],
        "otrpa1_v1_canonical_sha256": HISTORICAL["otrpa1"][2],
        "ot115_physical_record_raw_sha256": OT115_RAW_SHA256,
        "otcbr1_v0_raw_sha256": HISTORICAL["otcbr1"][1],
        "otcbr1_v0_canonical_sha256": HISTORICAL["otcbr1"][2],
        "otcbx1_v1_raw_sha256": HISTORICAL["otcbx1"][1],
        "otcbx1_v1_canonical_sha256": HISTORICAL["otcbx1"][2],
        "otmapia0_v0_raw_sha256": HISTORICAL["otmapia0"][1],
        "otmapia0_v0_canonical_sha256": HISTORICAL["otmapia0"][2],
        "otrtpr0_v0_raw_sha256": EXPECTED_RECEIPT_RAW_SHA256,
        "otrtpr0_v0_canonical_sha256": EXPECTED_RECEIPT_SHA256,
    }


def validate_evidence(evidence: dict[str, Any], receipt: dict[str, Any]) -> dict[str, Any]:
    receipt_result = validate_receipt(receipt)
    historical = _historical()
    _scan(evidence)
    _exact_keys(
        evidence,
        {
            "schema", "version", "artifact_kind", "evidence_id", "accepted_date",
            "status", "public_result", "parents", "observation_receipt",
            "target_binding", "exact_profile", "field_provenance", "corroboration",
            "accepted_profile_registry", "boundaries", "claims",
        },
        "evidence",
    )
    if (
        evidence["schema"], evidence["version"], evidence["artifact_kind"],
        evidence["evidence_id"], evidence["accepted_date"], evidence["status"],
    ) != (
        "OTRTPE1", 1, "second_node_exact_received_target_profile_evidence",
        "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-EVIDENCE-V1",
        "2026-08-22", "second_node_exact_received_target_profile_evidenced",
    ) or type(evidence["version"]) is not int:
        raise ValidationError("evidence identity mismatch")
    if evidence["parents"] != _expected_evidence_parents():
        raise ValidationError("evidence parent mismatch")
    if evidence["observation_receipt"] != {
        "path": "tests/benchmarks/crypto/OT-119-OT005-SECOND-NODE-EXACT-PROFILE-USB-RECEIPT-V0.json",
        "raw_sha256": EXPECTED_RECEIPT_RAW_SHA256,
        "canonical_sha256": receipt_result["receipt_sha256"],
    }:
        raise ValidationError("receipt binding mismatch")
    expected_target = dict(historical["otrtpe0"]["target_binding"])
    expected_target["evidence_unit"] = "OT-DEV-002"
    if evidence["target_binding"] != expected_target:
        raise ValidationError("target profile identity mismatch")
    if evidence["exact_profile"] != historical["otrtpe0"]["exact_profile"]:
        raise ValidationError("exact profile mismatch")
    if evidence["field_provenance"] != {
        "usb_observed": [
            "mcu_family", "processor_revision", "crystal_mhz", "flash_bytes",
            "psram_bytes",
        ],
        "owner_photo_observed": [
            "pcb_model", "exact_received_revision", "rf_variant_model",
        ],
        "ot103_exact_profile_and_official_source_bound": [
            "manufacturer", "commercial_family", "manufacturer_part",
            "manufacturer_lora_chip_family", "manufacturer_variant_role",
        ],
    }:
        raise ValidationError("field provenance mismatch")
    if evidence["corroboration"] != {
        "ot114_owner_confirmed_two_v4_2_diagnostic_nodes": True,
        "ot114_per_node_exact_image_context_receipts": 2,
        "ot115_identical_full_image_writes_verified": 2,
        "ot115_bounded_heartbeats_observed": 2,
        "corroboration_closes_profile_without_usb_and_marking_evidence": False,
    }:
        raise ValidationError("corroboration mismatch")
    if evidence["accepted_profile_registry"] != [
        {
            "evidence_unit": "OT-DEV-001",
            "profile_evidence_raw_sha256": HISTORICAL["otrtpe0"][1],
            "profile_admission_canonical_sha256": HISTORICAL["otrtpa0"][2],
        },
        {
            "evidence_unit": "OT-DEV-002",
            "observation_receipt_canonical_sha256": EXPECTED_RECEIPT_SHA256,
        },
    ]:
        raise ValidationError("profile registry mismatch")
    false_boundaries = {
        "raw_photo_retained_in_repository", "raw_probe_output_retained",
        "local_paths_retained", "exif_or_location_retained",
        "private_device_identifier_retained", "serial_port_retained",
        "usb_serial_or_hardware_path_retained", "mac_or_chip_identifier_retained",
        "firmware_changed", "flashed", "flash_read", "efuse_read_or_changed",
        "radio_used", "key_or_entropy_operation", "benchmark_executed",
        "candidate_selected", "suite_selected", "packet_v1_wire_selected",
        "hardware_support_claimed", "compatibility_claimed",
        "regulatory_acceptance_claimed", "score_credit_added",
    }
    if set(evidence["boundaries"]) != false_boundaries or any(evidence["boundaries"].values()):
        raise ValidationError("evidence boundary mismatch")
    if evidence["claims"] != {
        "second_node_exact_profile_evidence_generated": True,
        "bounded_read_only_device_access_performed": True,
        "physical_evidence_added": True,
        "persistent_device_state_changed": False,
        "benchmark_executed": False,
        "candidate_selected": False,
        "hardware_support_proven": False,
        "compatibility_proven": False,
        "regulatory_compliance_proven": False,
        "score_credit_added": False,
    }:
        raise ValidationError("evidence claims mismatch")
    digest = canonical_sha256(evidence)
    if digest != EXPECTED_EVIDENCE_SHA256:
        raise ValidationError("evidence canonical digest mismatch")
    return {
        "evidence_sha256": digest,
        "receipt_sha256": receipt_result["receipt_sha256"],
        "accepted_exact_profile_units": 2,
    }


def validate_admission(
    admission: dict[str, Any], evidence: dict[str, Any], receipt: dict[str, Any]
) -> dict[str, Any]:
    evidence_result = validate_evidence(evidence, receipt)
    _scan(admission)
    _exact_keys(
        admission,
        {
            "schema", "version", "artifact_kind", "admission_id", "accepted_date",
            "status", "public_result", "parents", "accepted_target_profiles",
            "acceptance_counts", "phase_zero", "measurement_blockers", "authority",
            "claims",
        },
        "admission",
    )
    if (
        admission["schema"], admission["version"], admission["artifact_kind"],
        admission["admission_id"], admission["accepted_date"], admission["status"],
    ) != (
        "OTRTPA1", 1, "append_only_second_node_exact_profile_acceptance_delta",
        "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1",
        "2026-08-22",
        "second_node_exact_profile_admitted_phase_zero_complete_measurement_blocked",
    ) or type(admission["version"]) is not int:
        raise ValidationError("admission identity mismatch")
    if admission["parents"] != {
        "otrtpa0_v0_raw_sha256": HISTORICAL["otrtpa0"][1],
        "otrtpa0_v0_canonical_sha256": HISTORICAL["otrtpa0"][2],
        "otcbx1_v1_raw_sha256": HISTORICAL["otcbx1"][1],
        "otcbx1_v1_canonical_sha256": HISTORICAL["otcbx1"][2],
        "otmapia0_v0_raw_sha256": HISTORICAL["otmapia0"][1],
        "otmapia0_v0_canonical_sha256": HISTORICAL["otmapia0"][2],
        "otrtpr0_v0_raw_sha256": EXPECTED_RECEIPT_RAW_SHA256,
        "otrtpr0_v0_canonical_sha256": EXPECTED_RECEIPT_SHA256,
        "otrtpe1_v1_raw_sha256": EXPECTED_EVIDENCE_RAW_SHA256,
        "otrtpe1_v1_canonical_sha256": evidence_result["evidence_sha256"],
    }:
        raise ValidationError("admission parent mismatch")
    if admission["accepted_target_profiles"] != [
        {
            "target_id": "heltec-v4-bench-candidate",
            "evidence_unit": "OT-DEV-001",
            "profile_evidence_raw_sha256": HISTORICAL["otrtpe0"][1],
            "profile_admission_canonical_sha256": HISTORICAL["otrtpa0"][2],
            "supported": False,
        },
        {
            "target_id": "heltec-v4-bench-candidate",
            "evidence_unit": "OT-DEV-002",
            "profile_evidence_canonical_sha256": evidence_result["evidence_sha256"],
            "supported": False,
        },
    ]:
        raise ValidationError("accepted target registry mismatch")
    if admission["acceptance_counts"] != {
        "exact_profile_units": 2,
        "source": 3,
        "api_config": 3,
        "candidate_import": 0,
    }:
        raise ValidationError("acceptance count mismatch")
    if admission["phase_zero"] != {
        "name": "api_configuration_admission",
        "prior_complete": False,
        "prior_remaining": ["independent_second_node_exact_profile_admission"],
        "complete": True,
        "remaining": [],
        "completed": [
            "espressif_libsodium_complete_api_config_admission",
            "esp_idf_mbedtls_psa_partial_api_config_admission",
            "monocypher_partial_api_config_admission",
            "independent_second_node_exact_profile_admission",
        ],
    }:
        raise ValidationError("phase-zero transition mismatch")
    if admission["measurement_blockers"] != [
        "candidate_import_and_build_admissions_absent",
        "fresh_benchmark_execution_authority_absent",
    ]:
        raise ValidationError("measurement blocker mismatch")
    authority = {
        "candidate_import_authorized", "benchmark_build_authorized",
        "benchmark_execution_authorized", "device_access_authorized",
        "flash_authorized", "radio_transmit_authorized",
        "key_or_entropy_operation_authorized", "candidate_selection_authorized",
        "suite_selection_authorized", "packet_v1_authorized", "score_credit_added",
    }
    if set(admission["authority"]) != authority or any(admission["authority"].values()):
        raise ValidationError("authority disposition mismatch")
    if admission["claims"] != {
        "second_node_exact_profile_accepted": True,
        "phase_zero_complete": True,
        "bounded_read_only_device_access_performed": True,
        "physical_evidence_added": True,
        "measurement_ready": False,
        "candidate_imported": False,
        "candidate_benchmark_built": False,
        "candidate_benchmark_executed": False,
        "candidate_selected": False,
        "suite_selected": False,
        "hardware_support_proven": False,
        "compatibility_proven": False,
        "regulatory_compliance_proven": False,
        "firmware_changed": False,
        "flashed": False,
        "radio_used": False,
        "key_or_entropy_operation": False,
        "score_credit_added": False,
    }:
        raise ValidationError("admission claims mismatch")
    digest = canonical_sha256(admission)
    if digest != EXPECTED_ADMISSION_SHA256:
        raise ValidationError("admission canonical digest mismatch")
    return {
        "schema": "OTRTPA1",
        "version": 1,
        "admission_id": admission["admission_id"],
        "accepted_exact_profile_units": 2,
        "source_count": 3,
        "api_config_count": 3,
        "candidate_import_count": 0,
        "phase_zero_complete": True,
        "measurement_ready": False,
        "execution_authorized": False,
        "selection_authorized": False,
        "score_credit_added": False,
        "admission_sha256": digest,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser()
    parser.add_argument("--receipt", required=True, type=Path)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--admission", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate_admission(
            load(args.admission, EXPECTED_ADMISSION_RAW_SHA256),
            load(args.evidence, EXPECTED_EVIDENCE_RAW_SHA256),
            load(args.receipt, EXPECTED_RECEIPT_RAW_SHA256),
        )
    except ValidationError:
        print("ERROR: validation failed", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
