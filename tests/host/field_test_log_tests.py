from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from field_test_log import (  # noqa: E402
    FieldLogError,
    summarize_meshcore_soak,
    validate_public_log,
    validate_public_plan,
    write_public_log,
)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def counter(sent: int, received: int) -> dict:
    return {
        "core_errors": 0,
        "direct_rx": 0,
        "direct_tx": 0,
        "flood_rx": received,
        "flood_tx": sent,
        "last_rssi_dbm": 0,
        "last_snr_db": 11.5,
        "queue_length": 0,
        "receive_errors": 0,
        "received": received,
        "rx_airtime_seconds": 1,
        "sent": sent,
        "tx_airtime_seconds": 1,
    }


def raw_soak() -> dict:
    return {
        "cleanup": {"COM6": True, "COM11": True},
        "companion_deltas": {
            "COM6": counter(1, 3),
            "COM11": counter(1, 3),
        },
        "completed_utc": "2026-08-10T07:00:10+00:00",
        "elapsed_minutes": 0.1667,
        "interval_seconds": 3.0,
        "ports": {
            "companion_a": "COM6",
            "companion_b": "COM11",
            "repeater": "COM17",
        },
        "radio_profile": {
            "repeater_repeat_at_start": "on",
            "repeater_repeat_at_end": "on",
            "temporary_channel_index": 1,
        },
        "repeater_delta": counter(2, 2),
        "started_utc": "2026-08-10T07:00:00+00:00",
        "success": True,
        "traffic": {
            "attempted": 2,
            "delivered": 2,
            "duplicates": 0,
            "lost": 0,
            "latency_ms": {
                "max": 242.0,
                "median": 240.0,
                "min": 238.0,
                "p95": 242.0,
            },
        },
    }


def summarize(raw: dict | None = None) -> dict:
    return summarize_meshcore_soak(
        raw or raw_soak(),
        session_id="20260810-bench-a",
        phase="bench-two-plus-repeater",
        location_class="indoor-bench",
        client_model="Heltec V4 OLED",
        client_firmware="v1.16.0-test",
        repeater_model="Seeed SenseCap Solar",
        repeater_firmware="v1.16.0-test",
        frequency_mhz=910.525,
        bandwidth_khz=62.5,
        spreading_factor=7,
        coding_rate=5,
        client_tx_power_dbm=10,
        repeater_tx_power_dbm=22,
    )


def test_summary_redacts_transport_identity() -> None:
    log = summarize()
    serialized = json.dumps(log, sort_keys=True)
    expect("COM6" not in serialized and "COM11" not in serialized, "ports leaked")
    expect('"ports"' not in serialized, "raw ports key leaked")
    expect(log["topology"]["clients"] == 2, "client count was not retained")
    expect(log["traffic"]["delivered"] == 2, "delivery evidence was lost")
    validate_public_log(log)


def test_incoherent_counts_and_latency_fail_closed() -> None:
    log = summarize()
    log["traffic"]["lost"] = 1
    try:
        validate_public_log(log)
    except FieldLogError:
        pass
    else:
        raise AssertionError("incoherent traffic totals should fail")

    log = summarize()
    log["traffic"]["latency_ms"]["p95"] = 100.0
    try:
        validate_public_log(log)
    except FieldLogError:
        pass
    else:
        raise AssertionError("unordered latency summary should fail")

    log = summarize()
    log["topology"]["clients"] = 3
    try:
        validate_public_log(log)
    except FieldLogError:
        pass
    else:
        raise AssertionError("topology and hardware count mismatch should fail")


def test_privacy_scan_rejects_keys_and_values() -> None:
    log = summarize()
    log["hardware"]["serial"] = "ABC123"
    try:
        validate_public_log(log)
    except FieldLogError:
        pass
    else:
        raise AssertionError("serial key should fail privacy validation")

    log = summarize()
    log["session"]["motion"] = "captured on COM44"
    try:
        validate_public_log(log)
    except FieldLogError:
        pass
    else:
        raise AssertionError("transport port value should fail privacy validation")


def test_atomic_write_refuses_unapproved_overwrite() -> None:
    log = summarize()
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory) / "public.json"
        write_public_log(target, log)
        expect(target.exists(), "public log was not written")
        original = target.read_text(encoding="utf-8")
        try:
            write_public_log(target, copy.deepcopy(log))
        except FieldLogError:
            pass
        else:
            raise AssertionError("existing public log should not be overwritten")
        expect(target.read_text(encoding="utf-8") == original, "existing log changed")
        expect(not target.with_suffix(".json.tmp").exists(), "temporary file remained")


def test_four_person_plan_is_valid_but_hardware_blocked() -> None:
    path = PROJECT_ROOT / "tests" / "field-plans" / "OT-023-FOUR_PERSON_PILOT_V0.json"
    plan = json.loads(path.read_text(encoding="utf-8"))
    validate_public_plan(plan)
    expect(plan["plan"]["status"] == "draft_blocked", "plan must remain blocked")
    expect(
        plan["hardware_freeze"]["selection_status"] == "pending",
        "hardware must not be claimed as selected",
    )
    expect(plan["requirements"]["phone_required"] is False, "phone became required")


def test_four_person_plan_math_and_readiness_fail_closed() -> None:
    path = PROJECT_ROOT / "tests" / "field-plans" / "OT-023-FOUR_PERSON_PILOT_V0.json"
    plan = json.loads(path.read_text(encoding="utf-8"))
    plan["traffic"]["peer_delivery_opportunities"] += 1
    try:
        validate_public_plan(plan)
    except FieldLogError:
        pass
    else:
        raise AssertionError("incorrect delivery-opportunity math should fail")

    plan = json.loads(path.read_text(encoding="utf-8"))
    plan["plan"]["status"] = "ready"
    try:
        validate_public_plan(plan)
    except FieldLogError:
        pass
    else:
        raise AssertionError("ready status without frozen hardware should fail")


def main() -> None:
    test_summary_redacts_transport_identity()
    test_incoherent_counts_and_latency_fail_closed()
    test_privacy_scan_rejects_keys_and_values()
    test_atomic_write_refuses_unapproved_overwrite()
    test_four_person_plan_is_valid_but_hardware_blocked()
    test_four_person_plan_math_and_readiness_fail_closed()
    print("PASS: 6 privacy-safe field evidence scenario groups")


if __name__ == "__main__":
    main()
