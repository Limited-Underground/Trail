"""Build the privacy-safe inspection view for the future Windows loader.

The view is presentation data only. It can refresh connected runtime evidence
and explain fixed blockers, but exposes no write, erase, reset, recovery, file,
network, or firmware-selection action.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
from typing import Any, Iterable


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_TOOL_PATH = TOOLS_ROOT / "Get-OpenTrailRuntimeEvidence.py"
SPEC = importlib.util.spec_from_file_location(
    "opentrail_runtime_evidence", RUNTIME_TOOL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("runtime evidence module could not be loaded")
RUNTIME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNTIME)

_FIRMWARE = re.compile(r"^v\d{1,3}\.\d{1,3}\.\d{1,3}-[0-9a-f]{7,40}$")

_FAMILY_VIEW = {
    ("heltec_v4_oled", "meshcore_companion"): (
        "Heltec V4 OLED",
        "MeshCore companion",
    ),
    ("seeed_sensecap_solar", "meshcore_repeater"): (
        "SenseCAP Solar",
        "MeshCore repeater",
    ),
    ("seeed_wio_tracker_l1", "meshcore_companion"): (
        "Wio Tracker L1",
        "MeshCore companion",
    ),
}

_HARDWARE_PROFILE_VIEW = {
    ("heltec_v4_oled", "meshcore_companion"): {
        "profile_candidate": "Heltec WiFi LoRa 32 V4 family",
        "evidence_level": "Runtime candidate only",
        "observed_now": (
            "Recognized Espressif USB family and allowlisted Heltec V4 OLED "
            "companion response."
        ),
        "published_baseline": (
            "Heltec's V4 family documentation lists ESP32-S3R2, 16 MB external "
            "flash, SX1262, and a 0.96-inch OLED."
        ),
        "next_step": (
            "Use one supervised read-only maintenance attempt for processor, memory, and "
            "bootloader evidence; confirm the received revision separately."
        ),
        "maintenance_restart_required": True,
        "maintenance_attempt_limit": 1,
        "runtime_recovery_required_before_retry": True,
        "maintenance_caution": (
            "ONE ATTEMPT PER SESSION. If automatic reset fails, stop. Do not try "
            "again until the USB cable has been reconnected when needed and normal "
            "runtime inspection succeeds."
        ),
        "authoritative_for_flash": False,
    },
    ("seeed_sensecap_solar", "meshcore_repeater"): {
        "profile_candidate": "SenseCAP Solar Node family",
        "evidence_level": "Runtime candidate only",
        "observed_now": (
            "Recognized Seeed USB family and allowlisted SenseCAP Solar repeater response."
        ),
        "published_baseline": (
            "Seeed's Solar Node documentation lists XIAO nRF52840 Plus and "
            "Wio-SX1262; P1-Pro adds L76K GNSS."
        ),
        "next_step": (
            "Use one supervised read-only DFU or bootloader session for low-level evidence; "
            "confirm the received P1/P1-Pro revision separately."
        ),
        "maintenance_restart_required": True,
        "maintenance_attempt_limit": 1,
        "runtime_recovery_required_before_retry": True,
        "maintenance_caution": (
            "ONE ATTEMPT PER SESSION. If maintenance entry fails, stop. Do not try "
            "again until normal USB enumeration and runtime inspection both succeed."
        ),
        "authoritative_for_flash": False,
    },
    ("seeed_wio_tracker_l1", "meshcore_companion"): {
        "profile_candidate": "Wio Tracker L1 family",
        "evidence_level": "Runtime candidate only",
        "observed_now": (
            "Recognized Seeed USB family and allowlisted Wio Tracker L1 "
            "companion response."
        ),
        "published_baseline": (
            "Seeed's Wio Tracker L1 Pro family documentation lists nRF52840, "
            "Wio-SX1262, L76K GNSS, and an OLED; the runtime label does not "
            "establish the exact received Pro model, SKU, or revision."
        ),
        "next_step": (
            "Use one supervised read-only DFU or bootloader session for low-level "
            "evidence; confirm the exact received model, SKU, and revision separately."
        ),
        "maintenance_restart_required": True,
        "maintenance_attempt_limit": 1,
        "runtime_recovery_required_before_retry": True,
        "maintenance_caution": (
            "ONE ATTEMPT PER SESSION. If maintenance entry fails, stop. Do not try "
            "again until normal USB enumeration and runtime inspection both succeed."
        ),
        "authoritative_for_flash": False,
    },
}

_UNKNOWN_HARDWARE_PROFILE = {
    "profile_candidate": "No supported profile candidate",
    "evidence_level": "USB transport only",
    "observed_now": (
        "A bounded USB serial candidate is present; its runtime was not identified."
    ),
    "published_baseline": (
        "No vendor hardware baseline is applied to an unidentified device."
    ),
    "next_step": (
        "Identify an allowlisted MeshCore runtime before offering any maintenance "
        "profiling step."
    ),
    "maintenance_restart_required": False,
    "maintenance_attempt_limit": 0,
    "runtime_recovery_required_before_retry": True,
    "maintenance_caution": (
        "MAINTENANCE BLOCKED. Runtime identity must succeed before any low-level "
        "session is considered."
    ),
    "authoritative_for_flash": False,
}

_BLOCKER_VIEW = {
    "low_level_processor_and_memory_probe_required": (
        "Low-level processor and memory probe required"
    ),
    "exact_profile_required": "Exact hardware profile required",
    "target_role_unresolved": "Product target role unresolved",
    "board_revision_unresolved": "Board revision unresolved",
    "bootloader_schema_unresolved": "Bootloader schema unresolved",
}


def _safe_candidate_card(candidate: Any) -> dict[str, Any]:
    if not isinstance(candidate, dict):
        raise ValueError("invalid runtime candidate")
    ordinal = candidate.get("candidate")
    if not isinstance(ordinal, str) or re.fullmatch(r"usb_candidate_\d{1,3}", ordinal) is None:
        raise ValueError("invalid candidate ordinal")

    query_succeeded = candidate.get("runtime_query_succeeded") is True
    family_key = (
        candidate.get("runtime_board_family"),
        candidate.get("runtime_role"),
    )
    family = _FAMILY_VIEW.get(family_key) if query_succeeded else None
    firmware = candidate.get("runtime_firmware")
    if family is None or not isinstance(firmware, str) or _FIRMWARE.fullmatch(firmware) is None:
        return {
            "candidate": ordinal,
            "display_name": "USB device",
            "installed_runtime": "Runtime details unavailable",
            "firmware": None,
            "connection": "USB",
            "inspection_status": "Needs attention",
            "hardware_profile": dict(_UNKNOWN_HARDWARE_PROFILE),
            "status_tone": "warning",
            "flash_status": "Blocked",
            "blockers": ["Recognized runtime evidence required"],
            "actions": {"inspect": True, "flash": False},
        }

    raw_blockers = candidate.get("blocking_evidence_gaps")
    if not isinstance(raw_blockers, list):
        raise ValueError("invalid candidate blockers")
    blockers: list[str] = []
    for blocker in raw_blockers:
        if blocker not in _BLOCKER_VIEW:
            raise ValueError("unrecognized candidate blocker")
        label = _BLOCKER_VIEW[blocker]
        if label not in blockers:
            blockers.append(label)
    if not blockers:
        raise ValueError("missing candidate blockers")
    if candidate.get("flashing_allowed") is not False:
        raise ValueError("inspection candidate unexpectedly allows flashing")

    return {
        "candidate": ordinal,
        "display_name": family[0],
        "installed_runtime": family[1],
        "firmware": firmware,
        "connection": "USB",
        "inspection_status": "Connected and inspected",
        "hardware_profile": dict(_HARDWARE_PROFILE_VIEW[family_key]),
        "status_tone": "information",
        "flash_status": "Blocked",
        "blockers": blockers,
        "actions": {"inspect": True, "flash": False},
    }


def build_loader_view(snapshot: Any) -> dict[str, Any]:
    if not isinstance(snapshot, dict) or snapshot.get("schema") != "ot_meshcore_runtime_evidence_v0":
        raise ValueError("invalid runtime evidence snapshot")
    devices = snapshot.get("devices")
    if not isinstance(devices, list) or len(devices) > 64:
        raise ValueError("invalid runtime candidate collection")
    cards = [_safe_candidate_card(item) for item in devices]
    inspected = sum(card["inspection_status"] == "Connected and inspected" for card in cards)
    return {
        "schema": "ot_loader_inspection_view_v0",
        "screen": {
            "title": "Device Utility",
            "eyebrow": "Connected devices",
            "phase": "Inspection only",
            "summary": f"{len(cards)} found · {inspected} inspected · 0 ready to flash",
            "notice": (
                "USB and installed runtime names do not prove an exact supported board. "
                "Flash remains disabled until a signed bundle and every board gate pass."
            ),
        },
        "global_actions": {
            "refresh": {"enabled": True},
            "select_firmware": {
                "enabled": False,
                "reason": "No approved signed firmware-bundle workflow is connected",
            },
            "flash": {
                "enabled": False,
                "reason": "No connected candidate has final write admission",
            },
            "clean_install": {"enabled": False},
            "recovery": {"enabled": False},
        },
        "candidate_count": len(cards),
        "inspected_count": inspected,
        "ready_to_flash_count": 0,
        "privacy": {
            "local_ports_included": False,
            "serial_numbers_included": False,
            "hardware_instance_ids_included": False,
            "device_locations_included": False,
            "raw_responses_included": False,
            "pairing_data_included": False,
            "device_identity_included": False,
        },
        "devices": cards,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.parse_args()

    from serial.tools import list_ports

    runtime_snapshot = RUNTIME.collect_runtime_evidence(list_ports.comports())
    view = build_loader_view(runtime_snapshot)
    print(json.dumps(view, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
