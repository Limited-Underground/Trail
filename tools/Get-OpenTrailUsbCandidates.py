"""Enumerate privacy-safe USB candidates for the future OpenTrail loader.

This tool performs discovery only. USB VID/PID values identify a transport
family, not an exact board, revision, target role, or safe firmware image.
Consequently every emitted candidate remains blocked from flashing.
"""

from __future__ import annotations

import argparse
import json
from typing import Any, Iterable


KNOWN_RUNTIME_USB_FAMILIES = {
    (0x303A, 0x0002): {
        "name": "espressif_application_usb",
        "inspection_note": (
            "Possible ESP32 application runtime; exact processor and board remain unresolved."
        ),
    },
    (0x2886, 0x0059): {
        "name": "seeed_tinyusb_serial",
        "inspection_note": (
            "Possible Seeed TinyUSB runtime; exact processor and board remain unresolved."
        ),
    },
    (0x2886, 0x1667): {
        "name": "seeed_wio_tracker_l1_usb",
        "inspection_note": (
            "Seeed Wio Tracker L1 USB family; exact received revision and target "
            "role remain unresolved."
        ),
    },
}

DISCOVERY_EVIDENCE_GAPS = [
    "low_level_processor_and_memory_probe_required",
    "exact_profile_required",
    "target_role_unresolved",
    "board_revision_unresolved",
    "bootloader_schema_unresolved",
]


def _field(record: Any, name: str) -> Any:
    if isinstance(record, dict):
        return record.get(name)
    return getattr(record, name, None)


def _normalized_usb_id(record: Any) -> tuple[int, int] | None:
    vid = _field(record, "vid")
    pid = _field(record, "pid")
    if not isinstance(vid, int) or isinstance(vid, bool):
        return None
    if not isinstance(pid, int) or isinstance(pid, bool):
        return None
    if not 0 <= vid <= 0xFFFF or not 0 <= pid <= 0xFFFF:
        return None
    return vid, pid


def select_candidate_records(
    records: Iterable[Any],
    *,
    include_unknown: bool = False,
) -> list[tuple[tuple[int, int], str, Any, dict[str, str] | None]]:
    selected: list[tuple[tuple[int, int], str, Any, dict[str, str] | None]] = []
    for record in records:
        usb_id = _normalized_usb_id(record)
        if usb_id is None:
            continue
        family = KNOWN_RUNTIME_USB_FAMILIES.get(usb_id)
        if family is None and not include_unknown:
            continue
        device = _field(record, "device")
        local_sort_key = device if isinstance(device, str) else ""
        selected.append((usb_id, local_sort_key, record, family))

    selected.sort(key=lambda item: (item[0][0], item[0][1], item[1]))
    return selected


def collect_candidates(
    records: Iterable[Any],
    *,
    include_unknown: bool = False,
    include_local_ports: bool = False,
) -> dict[str, Any]:
    selected = select_candidate_records(records, include_unknown=include_unknown)
    devices: list[dict[str, Any]] = []
    for index, (usb_id, _sort_key, record, family) in enumerate(selected, start=1):
        candidate = {
            "candidate": f"usb_candidate_{index}",
            "connected": True,
            "connection": "serial_usb",
            "usb_id": f"{usb_id[0]:04X}:{usb_id[1]:04X}",
            "runtime_usb_family": family["name"] if family else "unknown_usb_serial",
            "inspection_note": (
                family["inspection_note"]
                if family
                else "Unrecognized USB serial runtime; exact processor and board remain unresolved."
            ),
            "inspection_available": True,
            "usb_identity_authoritative_for_flash": False,
            "low_level_probe_complete": False,
            "exact_profile_resolved": False,
            "target_role": "unresolved",
            "flashing_allowed": False,
            "blocking_evidence_gaps": list(DISCOVERY_EVIDENCE_GAPS),
        }
        if include_local_ports:
            device = _field(record, "device")
            if isinstance(device, str) and device:
                candidate["local_port"] = device
        devices.append(candidate)

    return {
        "schema": "ot_windows_usb_candidates_v0",
        "read_only": True,
        "state_changes_made": False,
        "privacy": {
            "local_ports_included": include_local_ports,
            "serial_numbers_included": False,
            "hardware_instance_ids_included": False,
            "device_locations_included": False,
            "device_identity_included": False,
        },
        "candidate_count": len(devices),
        "flashing_allowed_count": 0,
        "devices": devices,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--include-unknown",
        action="store_true",
        help="include USB serial devices outside the recognized runtime families",
    )
    parser.add_argument(
        "--include-local-ports",
        action="store_true",
        help="include transient local port names for interactive troubleshooting",
    )
    args = parser.parse_args()

    from serial.tools import list_ports

    snapshot = collect_candidates(
        list_ports.comports(),
        include_unknown=args.include_unknown,
        include_local_ports=args.include_local_ports,
    )
    print(json.dumps(snapshot, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
