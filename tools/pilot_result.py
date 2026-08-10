"""Validate and evaluate privacy-safe four-person pilot results.

OTPR0 contains aggregate session evidence only. It deliberately excludes
participant identity, device identity, precise location, transport details, and
secrets. Evaluation is deterministic against one ready OTFP0 plan.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from field_test_log import (
    FieldLogError,
    SAFE_SESSION_ID,
    _require_integer,
    _require_mapping,
    _require_number,
    _scan_privacy,
    validate_public_plan,
)


SCHEMA = "OTPR0"
VERSION = 0
TRAFFIC_CLASSES = ("position", "status", "quick_status", "critical_alert")
CAPABILITY_FLAGS = (
    "battery",
    "display",
    "enclosure",
    "gnss",
    "local_input",
    "usb_recovery",
)
DEPENDENCY_FLAGS = (
    "internet",
    "laptop_during_session",
    "phone",
    "repeater",
    "server",
    "vehicle_connection",
)
TOP_LEVEL_KEYS = {
    "cleanup",
    "dependencies_used",
    "failures",
    "hardware",
    "performance",
    "plan_id",
    "privacy",
    "schema",
    "session",
    "topology",
    "traffic",
    "version",
}


def _require_exact_keys(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected:
        raise FieldLogError(f"{name} must contain only the canonical fields")


def _require_boolean(value: Any, name: str) -> bool:
    if not isinstance(value, bool):
        raise FieldLogError(f"{name} must be Boolean")
    return value


def validate_public_result(result: dict[str, Any]) -> None:
    if result.get("schema") != SCHEMA or result.get("version") != VERSION:
        raise FieldLogError("schema/version must be OTPR0/v0")
    _require_exact_keys(result, TOP_LEVEL_KEYS, "result")

    plan_id = result.get("plan_id")
    if not isinstance(plan_id, str) or not SAFE_SESSION_ID.fullmatch(plan_id):
        raise FieldLogError("plan_id is invalid")

    session = _require_mapping(result.get("session"), "session")
    _require_exact_keys(
        session,
        {
            "duration_minutes",
            "evidence_complete",
            "id",
            "operator_safety_confirmed",
            "scenario_class",
        },
        "session",
    )
    session_id = session.get("id")
    if not isinstance(session_id, str) or not SAFE_SESSION_ID.fullmatch(session_id):
        raise FieldLogError("session.id is invalid")
    scenario = session.get("scenario_class")
    if not isinstance(scenario, str) or not scenario:
        raise FieldLogError("session.scenario_class must be nonempty")
    _require_integer(session.get("duration_minutes"), "session.duration_minutes", 1)
    _require_boolean(session.get("evidence_complete"), "session.evidence_complete")
    _require_boolean(
        session.get("operator_safety_confirmed"),
        "session.operator_safety_confirmed",
    )

    topology = _require_mapping(result.get("topology"), "topology")
    _require_exact_keys(topology, {"clients", "repeaters"}, "topology")
    _require_integer(topology.get("clients"), "topology.clients", 1)
    _require_integer(topology.get("repeaters"), "topology.repeaters")

    dependencies = _require_mapping(
        result.get("dependencies_used"), "dependencies_used"
    )
    _require_exact_keys(dependencies, set(DEPENDENCY_FLAGS), "dependencies_used")
    for flag in DEPENDENCY_FLAGS:
        _require_boolean(dependencies.get(flag), f"dependencies_used.{flag}")

    hardware = _require_mapping(result.get("hardware"), "hardware")
    _require_exact_keys(
        hardware,
        {
            "capabilities_verified",
            "client_model",
            "clients_tested",
            "firmware_version",
            "units_identical",
        },
        "hardware",
    )
    for field in ("client_model", "firmware_version"):
        value = hardware.get(field)
        if not isinstance(value, str) or not value.strip():
            raise FieldLogError(f"hardware.{field} must be nonempty")
    _require_integer(hardware.get("clients_tested"), "hardware.clients_tested", 1)
    _require_boolean(hardware.get("units_identical"), "hardware.units_identical")
    capabilities = _require_mapping(
        hardware.get("capabilities_verified"), "hardware.capabilities_verified"
    )
    _require_exact_keys(
        capabilities, set(CAPABILITY_FLAGS), "hardware.capabilities_verified"
    )
    for flag in CAPABILITY_FLAGS:
        _require_boolean(capabilities.get(flag), f"capabilities_verified.{flag}")

    traffic = _require_mapping(result.get("traffic"), "traffic")
    _require_exact_keys(
        traffic,
        set(TRAFFIC_CLASSES) | {"operator_visible_duplicates"},
        "traffic",
    )
    for traffic_class in TRAFFIC_CLASSES:
        record = _require_mapping(traffic.get(traffic_class), f"traffic.{traffic_class}")
        _require_exact_keys(record, {"origins", "peer_deliveries"}, f"traffic.{traffic_class}")
        _require_integer(record.get("origins"), f"traffic.{traffic_class}.origins")
        _require_integer(
            record.get("peer_deliveries"),
            f"traffic.{traffic_class}.peer_deliveries",
        )
    _require_integer(
        traffic.get("operator_visible_duplicates"),
        "traffic.operator_visible_duplicates",
    )

    performance = _require_mapping(result.get("performance"), "performance")
    _require_exact_keys(
        performance,
        {
            "critical_latency_max_ms",
            "ending_battery_min_percent",
            "gps_first_fix_max_ms",
            "p95_latency_ms",
            "peer_stale_max_ms",
        },
        "performance",
    )
    for field in performance:
        _require_number(performance.get(field), f"performance.{field}", 0)
    if performance["ending_battery_min_percent"] > 100:
        raise FieldLogError("performance.ending_battery_min_percent cannot exceed 100")

    failures = _require_mapping(result.get("failures"), "failures")
    _require_exact_keys(
        failures, {"false_successes", "queue_overflows", "resets"}, "failures"
    )
    for field in failures:
        _require_integer(failures.get(field), f"failures.{field}")

    cleanup = _require_mapping(result.get("cleanup"), "cleanup")
    _require_exact_keys(
        cleanup,
        {"configuration_restored", "raw_capture_local_only"},
        "cleanup",
    )
    for field in cleanup:
        _require_boolean(cleanup.get(field), f"cleanup.{field}")

    privacy = result.get("privacy")
    canonical_privacy = {
        "aggregate_only": True,
        "hardware_identifiers_included": False,
        "participant_identifiers_included": False,
        "precise_coordinates_included": False,
        "secrets_included": False,
        "transport_ports_included": False,
    }
    if privacy != canonical_privacy:
        raise FieldLogError("result privacy declaration is missing or noncanonical")

    privacy_errors: list[str] = []
    _scan_privacy(result, "", privacy_errors)
    if privacy_errors:
        raise FieldLogError("; ".join(privacy_errors))


def _expected_origins(plan: dict[str, Any]) -> dict[str, int]:
    traffic = _require_mapping(plan.get("traffic"), "plan.traffic")
    return {
        "position": traffic["position_origins"],
        "status": traffic["status_origins"],
        "quick_status": traffic["quick_status_origins"],
        "critical_alert": traffic["critical_alert_origins"],
    }


def evaluate_pilot_result(
    plan: dict[str, Any], result: dict[str, Any]
) -> dict[str, Any]:
    validate_public_plan(plan)
    validate_public_result(result)

    ineligible: list[str] = []
    failed: list[str] = []
    plan_record = _require_mapping(plan["plan"], "plan.plan")
    hardware_freeze = _require_mapping(plan["hardware_freeze"], "plan.hardware_freeze")
    session_plan = _require_mapping(plan["session"], "plan.session")
    topology_plan = _require_mapping(plan["topology"], "plan.topology")
    acceptance = _require_mapping(plan["acceptance"], "plan.acceptance")

    if plan_record["status"] != "ready" or hardware_freeze["selection_status"] != "frozen":
        ineligible.append("plan hardware and firmware are not frozen as ready")
    if result["plan_id"] != plan_record["id"]:
        ineligible.append("result plan_id does not match the evaluated plan")

    session = result["session"]
    if session["duration_minutes"] != session_plan["duration_minutes"]:
        ineligible.append("session duration does not match the plan")
    if session["scenario_class"] not in session_plan["scenario_classes"]:
        ineligible.append("session scenario class is outside the plan")
    if not session["evidence_complete"]:
        ineligible.append("session evidence is incomplete")
    if not session["operator_safety_confirmed"]:
        ineligible.append("operator safety was not confirmed")

    topology = result["topology"]
    if topology != topology_plan:
        ineligible.append("session topology does not match the plan")
    if any(result["dependencies_used"].values()):
        ineligible.append("an external dependency was used during the session")

    hardware = result["hardware"]
    if hardware["client_model"] != hardware_freeze["client_model"]:
        ineligible.append("client model does not match the frozen plan")
    if hardware["firmware_version"] != hardware_freeze["firmware_version"]:
        ineligible.append("firmware version does not match the frozen plan")
    if hardware["clients_tested"] != topology_plan["clients"]:
        ineligible.append("tested client count does not match the plan")
    if not hardware["units_identical"]:
        ineligible.append("pilot units were not identical")
    if not all(hardware["capabilities_verified"].values()):
        ineligible.append("one or more standalone hardware capabilities were unverified")

    if not result["cleanup"]["configuration_restored"]:
        ineligible.append("temporary configuration cleanup was not verified")
    if not result["cleanup"]["raw_capture_local_only"]:
        ineligible.append("raw capture was not retained locally")

    clients = topology_plan["clients"]
    expected_origins = _expected_origins(plan)
    expected_deliveries = {
        key: origins * (clients - 1) for key, origins in expected_origins.items()
    }
    delivered: dict[str, int] = {}
    for traffic_class in TRAFFIC_CLASSES:
        record = result["traffic"][traffic_class]
        if record["origins"] != expected_origins[traffic_class]:
            ineligible.append(f"{traffic_class} origin count does not match the plan")
        if record["peer_deliveries"] > expected_deliveries[traffic_class]:
            ineligible.append(f"{traffic_class} peer deliveries exceed opportunities")
        delivered[traffic_class] = min(
            record["peer_deliveries"], expected_deliveries[traffic_class]
        )

    position_expected = expected_deliveries["position"]
    position_ppm = (
        delivered["position"] * 1_000_000 // position_expected
        if position_expected
        else 0
    )
    critical_losses = expected_deliveries["critical_alert"] - delivered["critical_alert"]

    if position_ppm < acceptance["position_delivery_min_ppm"]:
        failed.append("position delivery is below the pilot threshold")
    if critical_losses > acceptance["critical_alert_losses_allowed"]:
        failed.append("critical-alert loss exceeds the pilot threshold")
    if (
        result["traffic"]["operator_visible_duplicates"]
        > acceptance["operator_visible_duplicates_allowed"]
    ):
        failed.append("operator-visible duplicates exceed the pilot threshold")

    performance = result["performance"]
    upper_limits = {
        "critical_latency_max_ms": "critical_latency_max_ms",
        "gps_first_fix_max_ms": "gps_first_fix_max_ms",
        "p95_latency_ms": "p95_latency_max_ms",
        "peer_stale_max_ms": "peer_stale_max_ms",
    }
    for result_field, acceptance_field in upper_limits.items():
        if performance[result_field] > acceptance[acceptance_field]:
            failed.append(f"{result_field} exceeds the pilot threshold")
    if performance["ending_battery_min_percent"] < acceptance["ending_battery_min_percent"]:
        failed.append("ending battery is below the pilot threshold")

    failure_limits = {
        "false_successes": "false_successes_allowed",
        "queue_overflows": "queue_overflows_allowed",
        "resets": "resets_allowed",
    }
    for result_field, acceptance_field in failure_limits.items():
        if result["failures"][result_field] > acceptance[acceptance_field]:
            failed.append(f"{result_field} exceed the pilot threshold")

    verdict = "ineligible" if ineligible else "fail" if failed else "pass"
    return {
        "failed_gates": failed,
        "ineligible_reasons": ineligible,
        "metrics": {
            "critical_alert_losses": critical_losses,
            "peer_deliveries_observed": sum(delivered.values()),
            "peer_delivery_opportunities": sum(expected_deliveries.values()),
            "position_delivery_ppm": position_ppm,
        },
        "plan_id": plan_record["id"],
        "session_id": session["id"],
        "verdict": verdict,
    }


def build_result_template(
    plan: dict[str, Any], *, session_id: str, scenario_class: str
) -> dict[str, Any]:
    validate_public_plan(plan)
    plan_record = _require_mapping(plan["plan"], "plan.plan")
    hardware = _require_mapping(plan["hardware_freeze"], "plan.hardware_freeze")
    session = _require_mapping(plan["session"], "plan.session")
    topology = _require_mapping(plan["topology"], "plan.topology")
    if plan_record["status"] != "ready" or hardware["selection_status"] != "frozen":
        raise FieldLogError("cannot create a result template until the plan is ready")
    if not SAFE_SESSION_ID.fullmatch(session_id):
        raise FieldLogError("session_id is invalid")
    if scenario_class not in session["scenario_classes"]:
        raise FieldLogError("scenario_class is outside the plan")

    template = {
        "cleanup": {
            "configuration_restored": False,
            "raw_capture_local_only": True,
        },
        "dependencies_used": {flag: False for flag in DEPENDENCY_FLAGS},
        "failures": {
            "false_successes": 0,
            "queue_overflows": 0,
            "resets": 0,
        },
        "hardware": {
            "capabilities_verified": {flag: False for flag in CAPABILITY_FLAGS},
            "client_model": hardware["client_model"],
            "clients_tested": topology["clients"],
            "firmware_version": hardware["firmware_version"],
            "units_identical": False,
        },
        "performance": {
            "critical_latency_max_ms": 0,
            "ending_battery_min_percent": 0,
            "gps_first_fix_max_ms": 0,
            "p95_latency_ms": 0,
            "peer_stale_max_ms": 0,
        },
        "plan_id": plan_record["id"],
        "privacy": {
            "aggregate_only": True,
            "hardware_identifiers_included": False,
            "participant_identifiers_included": False,
            "precise_coordinates_included": False,
            "secrets_included": False,
            "transport_ports_included": False,
        },
        "schema": SCHEMA,
        "session": {
            "duration_minutes": session["duration_minutes"],
            "evidence_complete": False,
            "id": session_id,
            "operator_safety_confirmed": False,
            "scenario_class": scenario_class,
        },
        "topology": dict(topology),
        "traffic": {
            traffic_class: {"origins": 0, "peer_deliveries": 0}
            for traffic_class in TRAFFIC_CLASSES
        }
        | {"operator_visible_duplicates": 0},
        "version": VERSION,
    }
    validate_public_result(template)
    return template


def write_result_template(path: Path, result: dict[str, Any]) -> None:
    validate_public_result(result)
    if path.exists():
        raise FieldLogError(f"output already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    if temporary.exists():
        raise FieldLogError(f"temporary output already exists: {temporary}")
    temporary.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--result", type=Path)
    mode.add_argument("--new-result", type=Path)
    parser.add_argument("--session-id")
    parser.add_argument("--scenario-class")
    return parser


def main() -> int:
    args = _build_parser().parse_args()
    try:
        plan = _require_mapping(
            json.loads(args.plan.read_text(encoding="utf-8")), "plan document"
        )
        if args.new_result is not None:
            if args.session_id is None or args.scenario_class is None:
                raise FieldLogError(
                    "--new-result requires --session-id and --scenario-class"
                )
            template = build_result_template(
                plan,
                session_id=args.session_id,
                scenario_class=args.scenario_class,
            )
            write_result_template(args.new_result, template)
            print(json.dumps({"output": str(args.new_result), "success": True}))
            return 0
        if args.session_id is not None or args.scenario_class is not None:
            raise FieldLogError(
                "--session-id and --scenario-class are only valid with --new-result"
            )
        result = _require_mapping(
            json.loads(args.result.read_text(encoding="utf-8")), "result document"
        )
        evaluation = evaluate_pilot_result(plan, result)
        print(json.dumps(evaluation, indent=2, sort_keys=True))
        return 0 if evaluation["verdict"] == "pass" else 2
    except (FieldLogError, OSError, json.JSONDecodeError) as exc:
        print(json.dumps({"error": str(exc), "verdict": "invalid"}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
