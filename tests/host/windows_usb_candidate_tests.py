from __future__ import annotations

import importlib.util
import json
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = PROJECT_ROOT / "tools" / "Get-OpenTrailUsbCandidates.py"
SPEC = importlib.util.spec_from_file_location("windows_usb_candidates", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Windows USB candidate tool could not be loaded")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def fixtures() -> list[dict[str, object]]:
    return [
        {
            "device": "TEST_PORT_B",
            "vid": 0x303A,
            "pid": 0x0002,
            "serial_number": "private-unit-b",
            "location": "private-location-b",
            "hwid": "private-hardware-instance-b",
        },
        {
            "device": "TEST_PORT_C",
            "vid": 0x2886,
            "pid": 0x0059,
            "serial_number": "private-unit-c",
            "location": "private-location-c",
            "hwid": "private-hardware-instance-c",
        },
        {
            "device": "TEST_PORT_WIO",
            "vid": 0x2886,
            "pid": 0x1667,
            "serial_number": "private-unit-wio",
            "location": "private-location-wio",
            "hwid": "private-hardware-instance-wio",
        },
        {
            "device": "TEST_PORT_A",
            "vid": 0x303A,
            "pid": 0x0002,
            "serial_number": "private-unit-a",
            "location": "private-location-a",
            "hwid": "private-hardware-instance-a",
        },
        {"device": "TEST_PORT_UNKNOWN", "vid": 0x1234, "pid": 0x5678},
        {"device": "TEST_PORT_INVALID", "vid": None, "pid": None},
    ]


def test_default_four_candidate_reduction() -> None:
    result = MODULE.collect_candidates(fixtures())
    expect(result["candidate_count"] == 4, "known runtime families must be selected")
    expect(result["flashing_allowed_count"] == 0, "discovery must never allow flashing")
    expect(
        [item["usb_id"] for item in result["devices"]]
        == ["2886:0059", "2886:1667", "303A:0002", "303A:0002"],
        "candidate ordering must be deterministic",
    )
    wio = next(item for item in result["devices"] if item["usb_id"] == "2886:1667")
    expect(
        wio["runtime_usb_family"] == "seeed_wio_tracker_l1_usb",
        "exact Wio VID/PID must resolve to its bounded USB family",
    )
    for item in result["devices"]:
        expect(item["inspection_available"] is True, "known USB may be inspected")
        expect(
            item["usb_identity_authoritative_for_flash"] is False,
            "USB VID/PID must be explicitly non-authoritative",
        )
        expect(item["flashing_allowed"] is False, "USB identity cannot authorize flash")
        expect(item["exact_profile_resolved"] is False, "profile must remain unresolved")
        expect("local_port" not in item, "local ports must be omitted by default")


def test_sensitive_enumerator_fields_are_never_emitted() -> None:
    serialized = json.dumps(MODULE.collect_candidates(fixtures()), sort_keys=True)
    for value in (
        "private-unit-a",
        "private-unit-b",
        "private-unit-c",
        "private-unit-wio",
        "private-location-a",
        "private-location-wio",
        "private-hardware-instance-a",
        "private-hardware-instance-wio",
        "TEST_PORT_A",
        "TEST_PORT_B",
        "TEST_PORT_C",
        "TEST_PORT_WIO",
    ):
        expect(value not in serialized, f"sensitive enumerator field leaked: {value}")


def test_local_ports_require_explicit_option() -> None:
    result = MODULE.collect_candidates(fixtures(), include_local_ports=True)
    expect(result["privacy"]["local_ports_included"] is True, "port flag must be explicit")
    expect(
        sorted(item["local_port"] for item in result["devices"])
        == ["TEST_PORT_A", "TEST_PORT_B", "TEST_PORT_C", "TEST_PORT_WIO"],
        "explicit local troubleshooting ports must be retained",
    )


def test_unknown_and_invalid_usb_records() -> None:
    default = MODULE.collect_candidates(fixtures())
    expect(
        all(item["usb_id"] != "1234:5678" for item in default["devices"]),
        "unknown USB must be excluded by default",
    )
    expanded = MODULE.collect_candidates(fixtures(), include_unknown=True)
    expect(expanded["candidate_count"] == 5, "explicit unknown inclusion must be bounded")
    unknown = next(item for item in expanded["devices"] if item["usb_id"] == "1234:5678")
    expect(unknown["runtime_usb_family"] == "unknown_usb_serial", "unknowns stay unknown")
    expect(unknown["flashing_allowed"] is False, "unknown USB must remain blocked")


def main() -> None:
    test_default_four_candidate_reduction()
    test_sensitive_enumerator_fields_are_never_emitted()
    test_local_ports_require_explicit_option()
    test_unknown_and_invalid_usb_records()
    print("PASS: 4 privacy-safe Windows USB candidate scenario groups")


if __name__ == "__main__":
    main()
