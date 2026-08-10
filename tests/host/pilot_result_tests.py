from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from field_test_log import FieldLogError  # noqa: E402
from pilot_result import (  # noqa: E402
    build_result_template,
    evaluate_pilot_result,
    validate_public_result,
    write_result_template,
)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def blocked_plan() -> dict:
    path = PROJECT_ROOT / "tests" / "field-plans" / "OT-023-FOUR_PERSON_PILOT_V0.json"
    return json.loads(path.read_text(encoding="utf-8"))


def ready_plan() -> dict:
    plan = blocked_plan()
    plan["plan"]["status"] = "ready"
    plan["hardware_freeze"]["selection_status"] = "frozen"
    plan["hardware_freeze"]["client_model"] = "Reference Client A"
    plan["hardware_freeze"]["firmware_version"] = "pilot-0.1.0"
    return plan


def passing_result() -> dict:
    return {
        "cleanup": {
            "configuration_restored": True,
            "raw_capture_local_only": True,
        },
        "dependencies_used": {
            "internet": False,
            "laptop_during_session": False,
            "phone": False,
            "repeater": False,
            "server": False,
            "vehicle_connection": False,
        },
        "failures": {
            "false_successes": 0,
            "queue_overflows": 0,
            "resets": 0,
        },
        "hardware": {
            "capabilities_verified": {
                "battery": True,
                "display": True,
                "enclosure": True,
                "gnss": True,
                "local_input": True,
                "usb_recovery": True,
            },
            "client_model": "Reference Client A",
            "clients_tested": 4,
            "firmware_version": "pilot-0.1.0",
            "units_identical": True,
        },
        "performance": {
            "critical_latency_max_ms": 1500,
            "ending_battery_min_percent": 72,
            "gps_first_fix_max_ms": 120000,
            "p95_latency_ms": 800,
            "peer_stale_max_ms": 90000,
        },
        "plan_id": "four-person-pilot-v0",
        "privacy": {
            "aggregate_only": True,
            "hardware_identifiers_included": False,
            "participant_identifiers_included": False,
            "precise_coordinates_included": False,
            "secrets_included": False,
            "transport_ports_included": False,
        },
        "schema": "OTPR0",
        "session": {
            "duration_minutes": 60,
            "evidence_complete": True,
            "id": "pilot-open-field-a",
            "operator_safety_confirmed": True,
            "scenario_class": "open-field",
        },
        "topology": {"clients": 4, "repeaters": 0},
        "traffic": {
            "critical_alert": {"origins": 4, "peer_deliveries": 12},
            "operator_visible_duplicates": 0,
            "position": {"origins": 240, "peer_deliveries": 720},
            "quick_status": {"origins": 8, "peer_deliveries": 24},
            "status": {"origins": 48, "peer_deliveries": 144},
        },
        "version": 0,
    }


def expect_invalid(result: dict, message: str) -> None:
    try:
        validate_public_result(result)
    except FieldLogError:
        return
    raise AssertionError(message)


def test_ready_matching_session_passes() -> None:
    evaluation = evaluate_pilot_result(ready_plan(), passing_result())
    expect(evaluation["verdict"] == "pass", "matching result should pass")
    expect(evaluation["metrics"]["position_delivery_ppm"] == 1_000_000, "bad ppm")
    expect(evaluation["metrics"]["peer_delivery_opportunities"] == 900, "bad total")
    expect(evaluation["metrics"]["critical_alert_losses"] == 0, "bad loss")


def test_blocked_plan_cannot_produce_passing_verdict() -> None:
    evaluation = evaluate_pilot_result(blocked_plan(), passing_result())
    expect(evaluation["verdict"] == "ineligible", "blocked plan was treated as ready")
    expect(bool(evaluation["ineligible_reasons"]), "blocked reason was omitted")


def test_delivery_and_reliability_thresholds_fail() -> None:
    result = passing_result()
    result["traffic"]["position"]["peer_deliveries"] = 683
    result["traffic"]["critical_alert"]["peer_deliveries"] = 11
    result["failures"]["resets"] = 1
    evaluation = evaluate_pilot_result(ready_plan(), result)
    expect(evaluation["verdict"] == "fail", "threshold failures should fail")
    expect(evaluation["metrics"]["position_delivery_ppm"] == 948611, "bad floor ppm")
    expect(evaluation["metrics"]["critical_alert_losses"] == 1, "bad loss count")
    expect(len(evaluation["failed_gates"]) == 3, "expected three failed gates")


def test_setup_mismatch_is_ineligible_not_failed() -> None:
    result = passing_result()
    result["dependencies_used"]["phone"] = True
    result["hardware"]["firmware_version"] = "different"
    result["session"]["evidence_complete"] = False
    evaluation = evaluate_pilot_result(ready_plan(), result)
    expect(evaluation["verdict"] == "ineligible", "bad setup should be ineligible")
    expect(len(evaluation["ineligible_reasons"]) == 3, "expected three blockers")


def test_privacy_keys_and_transport_values_fail_closed() -> None:
    result = passing_result()
    result["hardware"]["serial"] = "unit-1"
    expect_invalid(result, "identity-bearing key should fail")

    result = passing_result()
    result["hardware"]["client_model"] = "captured on COM44"
    expect_invalid(result, "transport-bearing value should fail")


def test_noncanonical_shape_and_impossible_delivery_fail_closed() -> None:
    result = passing_result()
    result["notes"] = "unexpected free-form field"
    expect_invalid(result, "noncanonical top-level field should fail")

    result = passing_result()
    result["traffic"]["position"]["peer_deliveries"] = 721
    evaluation = evaluate_pilot_result(ready_plan(), result)
    expect(evaluation["verdict"] == "ineligible", "impossible count should be ineligible")


def test_ready_plan_builds_fail_closed_template() -> None:
    result = build_result_template(
        ready_plan(), session_id="pilot-open-field-b", scenario_class="open-field"
    )
    validate_public_result(result)
    expect(result["plan_id"] == "four-person-pilot-v0", "plan identity was lost")
    expect(result["hardware"]["client_model"] == "Reference Client A", "bad model")
    expect(result["session"]["evidence_complete"] is False, "template claimed evidence")
    expect(result["hardware"]["units_identical"] is False, "template claimed identity")
    expect(
        not any(result["hardware"]["capabilities_verified"].values()),
        "template claimed hardware verification",
    )
    evaluation = evaluate_pilot_result(ready_plan(), result)
    expect(evaluation["verdict"] == "ineligible", "blank template should not pass")


def test_blocked_plan_and_unknown_scenario_refuse_template() -> None:
    try:
        build_result_template(
            blocked_plan(), session_id="pilot-open-field-b", scenario_class="open-field"
        )
    except FieldLogError:
        pass
    else:
        raise AssertionError("blocked plan should not create a result template")

    try:
        build_result_template(
            ready_plan(), session_id="pilot-unknown-b", scenario_class="private-route"
        )
    except FieldLogError:
        pass
    else:
        raise AssertionError("unknown scenario should not create a result template")


def test_template_write_is_atomic_and_refuses_overwrite() -> None:
    result = build_result_template(
        ready_plan(), session_id="pilot-wooded-b", scenario_class="wooded-trail"
    )
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory) / "result.json"
        write_result_template(target, result)
        expect(target.exists(), "template was not written")
        original = target.read_text(encoding="utf-8")
        try:
            write_result_template(target, result)
        except FieldLogError:
            pass
        else:
            raise AssertionError("existing template should not be overwritten")
        expect(target.read_text(encoding="utf-8") == original, "template changed")
        expect(not target.with_suffix(".json.tmp").exists(), "temporary file remained")


def main() -> None:
    test_ready_matching_session_passes()
    test_blocked_plan_cannot_produce_passing_verdict()
    test_delivery_and_reliability_thresholds_fail()
    test_setup_mismatch_is_ineligible_not_failed()
    test_privacy_keys_and_transport_values_fail_closed()
    test_noncanonical_shape_and_impossible_delivery_fail_closed()
    test_ready_plan_builds_fail_closed_template()
    test_blocked_plan_and_unknown_scenario_refuse_template()
    test_template_write_is_atomic_and_refuses_overwrite()
    print("PASS: 9 four-person pilot result scenario groups")


if __name__ == "__main__":
    main()
