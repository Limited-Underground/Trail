from __future__ import annotations

import importlib.util
import json
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = PROJECT_ROOT / "tools" / "Get-OpenTrailLoaderInspection.py"
SPEC = importlib.util.spec_from_file_location("loader_inspection", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("loader inspection module could not be loaded")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def candidate(number: int, family: str, role: str) -> dict[str, object]:
    return {
        "candidate": f"usb_candidate_{number}",
        "runtime_query_succeeded": True,
        "runtime_board_family": family,
        "runtime_role": role,
        "runtime_firmware": "v1.16.0-07a3ca9",
        "blocking_evidence_gaps": [
            "low_level_processor_and_memory_probe_required",
            "exact_profile_required",
            "target_role_unresolved",
            "board_revision_unresolved",
            "bootloader_schema_unresolved",
        ],
        "flashing_allowed": False,
    }


def snapshot(devices: list[dict[str, object]]) -> dict[str, object]:
    return {"schema": "ot_meshcore_runtime_evidence_v0", "devices": devices}


def test_three_device_view_is_clear_and_blocked() -> None:
    view = MODULE.build_loader_view(
        snapshot(
            [
                candidate(1, "seeed_sensecap_solar", "meshcore_repeater"),
                candidate(2, "heltec_v4_oled", "meshcore_companion"),
                candidate(3, "heltec_v4_oled", "meshcore_companion"),
            ]
        )
    )
    expect(view["candidate_count"] == 3, "three candidates required")
    expect(view["inspected_count"] == 3, "three inspected candidates required")
    expect(view["ready_to_flash_count"] == 0, "no candidate may be ready")
    expect(
        view["screen"]["summary"] == "3 found · 3 inspected · 0 ready to flash",
        "summary must be operator-readable",
    )
    expect(
        [item["display_name"] for item in view["devices"]]
        == ["SenseCAP Solar", "Heltec V4 OLED", "Heltec V4 OLED"],
        "board-family labels must be familiar and bounded",
    )


def test_sensitive_runtime_fields_are_discarded() -> None:
    item = candidate(1, "heltec_v4_oled", "meshcore_companion")
    item.update(
        {
            "local_port": "PRIVATE_PORT",
            "serial_number": "PRIVATE_SERIAL",
            "raw_response": "PRIVATE_RAW",
            "pairing_pin": "PRIVATE_PIN",
            "device_identity": "PRIVATE_IDENTITY",
        }
    )
    serialized = json.dumps(MODULE.build_loader_view(snapshot([item])))
    for private in (
        "PRIVATE_PORT",
        "PRIVATE_SERIAL",
        "PRIVATE_RAW",
        "PRIVATE_PIN",
        "PRIVATE_IDENTITY",
    ):
        expect(private not in serialized, f"private runtime field leaked: {private}")


def test_failed_or_unrecognized_runtime_gets_generic_card() -> None:
    failed = candidate(1, "private-family", "private-role")
    failed["runtime_query_succeeded"] = False
    failed["runtime_error_type"] = "PrivateDetailedError"
    view = MODULE.build_loader_view(snapshot([failed]))
    card = view["devices"][0]
    expect(card["display_name"] == "USB device", "failed runtime must stay generic")
    expect(card["flash_status"] == "Blocked", "failed runtime must remain blocked")
    expect("PrivateDetailedError" not in json.dumps(view), "error detail must not leak")


def test_unexpected_shape_or_permissions_fail_closed() -> None:
    try:
        MODULE.build_loader_view({"schema": "wrong", "devices": []})
    except ValueError:
        pass
    else:
        raise AssertionError("wrong schema must fail closed")

    item = candidate(1, "heltec_v4_oled", "meshcore_companion")
    item["flashing_allowed"] = True
    try:
        MODULE.build_loader_view(snapshot([item]))
    except ValueError:
        pass
    else:
        raise AssertionError("unexpected flash permission must fail closed")


def main() -> None:
    test_three_device_view_is_clear_and_blocked()
    test_sensitive_runtime_fields_are_discarded()
    test_failed_or_unrecognized_runtime_gets_generic_card()
    test_unexpected_shape_or_permissions_fail_closed()
    print("PASS: 4 loader inspection view scenario groups")


if __name__ == "__main__":
    main()
