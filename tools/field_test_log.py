"""Create and validate privacy-safe OpenTrail field-test summaries.

Raw hardware captures remain local. The public OTFL0 record deliberately omits
transport ports, serial numbers, addresses, keys, channel names, coordinates,
and per-message payloads while retaining repeatable aggregate evidence.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
from typing import Any


SCHEMA = "OTFL0"
VERSION = 0
PLAN_SCHEMA = "OTFP0"
PLAN_VERSION = 0
PHASES = {
    "bench-two-plus-repeater",
    "four-standalone",
    "four-plus-repeater",
    "eight-plus-repeater",
}
LOCATION_CLASSES = {
    "indoor-bench",
    "open-field",
    "wooded-trail",
    "rolling-road",
    "urban-obstructed",
    "other-redacted",
}
PROHIBITED_KEYS = {
    "address",
    "addresses",
    "channel_name",
    "coordinate",
    "coordinates",
    "latitude",
    "longitude",
    "mac",
    "pin",
    "port",
    "ports",
    "private_key",
    "public_key",
    "secret",
    "serial",
    "serial_number",
}
SAFE_SESSION_ID = re.compile(r"^[a-z0-9][a-z0-9-]{2,63}$")
COM_PORT = re.compile(r"\bCOM\d+\b", re.IGNORECASE)
MAC_ADDRESS = re.compile(r"\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b", re.IGNORECASE)


class FieldLogError(ValueError):
    pass


def _require_mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FieldLogError(f"{name} must be an object")
    return value


def _require_integer(value: Any, name: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise FieldLogError(f"{name} must be an integer >= {minimum}")
    return value


def _require_number(value: Any, name: str, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise FieldLogError(f"{name} must be numeric")
    result = float(value)
    if minimum is not None and result < minimum:
        raise FieldLogError(f"{name} must be >= {minimum}")
    return result


def _require_timestamp(value: Any, name: str) -> str:
    if not isinstance(value, str):
        raise FieldLogError(f"{name} must be an ISO-8601 string")
    normalized = value.replace("Z", "+00:00")
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise FieldLogError(f"{name} is not valid ISO-8601") from exc
    if parsed.tzinfo is None:
        raise FieldLogError(f"{name} must include a UTC offset")
    return value


def _copy_counter(counter: dict[str, Any]) -> dict[str, int | float]:
    integer_fields = (
        "core_errors",
        "direct_rx",
        "direct_tx",
        "flood_rx",
        "flood_tx",
        "queue_length",
        "receive_errors",
        "received",
        "rx_airtime_seconds",
        "sent",
        "tx_airtime_seconds",
    )
    result: dict[str, int | float] = {}
    for field in integer_fields:
        result[field] = _require_integer(counter.get(field), f"counter.{field}")
    result["last_rssi_dbm"] = _require_number(
        counter.get("last_rssi_dbm"), "counter.last_rssi_dbm"
    )
    result["last_snr_db"] = _require_number(
        counter.get("last_snr_db"), "counter.last_snr_db"
    )
    return result


def summarize_meshcore_soak(
    raw: dict[str, Any],
    *,
    session_id: str,
    phase: str,
    location_class: str,
    client_model: str,
    client_firmware: str,
    repeater_model: str,
    repeater_firmware: str,
    frequency_mhz: float,
    bandwidth_khz: float,
    spreading_factor: int,
    coding_rate: int,
    client_tx_power_dbm: int,
    repeater_tx_power_dbm: int,
) -> dict[str, Any]:
    if not SAFE_SESSION_ID.fullmatch(session_id):
        raise FieldLogError("session_id must use 3-64 lowercase letters, digits, or dashes")
    if phase not in PHASES:
        raise FieldLogError(f"unsupported phase: {phase}")
    if location_class not in LOCATION_CLASSES:
        raise FieldLogError(f"unsupported location class: {location_class}")
    if not all(
        isinstance(value, str) and value.strip()
        for value in (
            client_model,
            client_firmware,
            repeater_model,
            repeater_firmware,
        )
    ):
        raise FieldLogError("hardware model and firmware labels are required")

    traffic = _require_mapping(raw.get("traffic"), "traffic")
    latency = _require_mapping(traffic.get("latency_ms"), "traffic.latency_ms")
    companion_deltas = _require_mapping(
        raw.get("companion_deltas"), "companion_deltas"
    )
    repeater_delta = _require_mapping(raw.get("repeater_delta"), "repeater_delta")
    cleanup = _require_mapping(raw.get("cleanup"), "cleanup")
    radio_profile = _require_mapping(raw.get("radio_profile"), "radio_profile")
    if len(companion_deltas) == 0:
        raise FieldLogError("at least one companion delta is required")

    clients = [
        {
            "label": f"client-{index}",
            "model": client_model,
            "firmware": client_firmware,
            "counters": _copy_counter(_require_mapping(counter, "companion counter")),
        }
        for index, (_, counter) in enumerate(sorted(companion_deltas.items()), start=1)
    ]
    cleanup_verified = sum(value is True for value in cleanup.values())
    output: dict[str, Any] = {
        "schema": SCHEMA,
        "version": VERSION,
        "session": {
            "id": session_id,
            "phase": phase,
            "started_utc": _require_timestamp(raw.get("started_utc"), "started_utc"),
            "completed_utc": _require_timestamp(
                raw.get("completed_utc"), "completed_utc"
            ),
            "duration_seconds": round(
                _require_number(raw.get("elapsed_minutes"), "elapsed_minutes", 0)
                * 60,
                3,
            ),
            "location_class": location_class,
            "motion": "stationary" if location_class == "indoor-bench" else "redacted",
        },
        "topology": {
            "clients": len(clients),
            "repeaters": 1,
            "repeater_required": "not_proved",
        },
        "hardware": {
            "clients": clients,
            "repeater": {
                "label": "repeater-1",
                "model": repeater_model,
                "firmware": repeater_firmware,
                "counters": _copy_counter(repeater_delta),
            },
        },
        "radio": {
            "frequency_mhz": _require_number(frequency_mhz, "frequency_mhz", 0),
            "bandwidth_khz": _require_number(bandwidth_khz, "bandwidth_khz", 0),
            "spreading_factor": _require_integer(
                spreading_factor, "spreading_factor", 5
            ),
            "coding_rate_denominator": _require_integer(coding_rate, "coding_rate", 5),
            "client_tx_power_dbm": int(client_tx_power_dbm),
            "repeater_tx_power_dbm": int(repeater_tx_power_dbm),
            "repeater_enabled_start": radio_profile.get("repeater_repeat_at_start")
            == "on",
            "repeater_enabled_end": radio_profile.get("repeater_repeat_at_end")
            == "on",
        },
        "traffic": {
            "interval_seconds": _require_number(
                raw.get("interval_seconds"), "interval_seconds", 0
            ),
            "attempted": _require_integer(traffic.get("attempted"), "traffic.attempted"),
            "delivered": _require_integer(traffic.get("delivered"), "traffic.delivered"),
            "lost": _require_integer(traffic.get("lost"), "traffic.lost"),
            "duplicates": _require_integer(
                traffic.get("duplicates"), "traffic.duplicates"
            ),
            "latency_ms": {
                "minimum": _require_number(latency.get("min"), "latency.min", 0),
                "median": _require_number(latency.get("median"), "latency.median", 0),
                "p95": _require_number(latency.get("p95"), "latency.p95", 0),
                "maximum": _require_number(latency.get("max"), "latency.max", 0),
            },
        },
        "cleanup": {
            "clients_expected": len(cleanup),
            "clients_verified": cleanup_verified,
            "lease_journal_removed": cleanup_verified == len(cleanup),
        },
        "result": {
            "raw_test_reported_success": raw.get("success") is True,
        },
        "privacy": {
            "aggregate_only": True,
            "transport_ports_included": False,
            "hardware_identifiers_included": False,
            "precise_coordinates_included": False,
            "secrets_included": False,
        },
    }
    validate_public_log(output)
    return output


def _scan_privacy(value: Any, path: str, errors: list[str]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            normalized = str(key).lower()
            child_path = f"{path}.{key}" if path else str(key)
            if normalized in PROHIBITED_KEYS:
                errors.append(f"prohibited key at {child_path}")
            _scan_privacy(child, child_path, errors)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _scan_privacy(child, f"{path}[{index}]", errors)
    elif isinstance(value, str):
        if COM_PORT.search(value):
            errors.append(f"transport port found at {path}")
        if MAC_ADDRESS.search(value):
            errors.append(f"MAC address found at {path}")


def _validate_counter(value: Any, name: str, errors: list[str]) -> None:
    try:
        counter = _require_mapping(value, name)
        _copy_counter(counter)
    except FieldLogError as exc:
        errors.append(str(exc))


def validate_public_log(log: dict[str, Any]) -> None:
    errors: list[str] = []
    if log.get("schema") != SCHEMA or log.get("version") != VERSION:
        errors.append("schema/version must be OTFL0/v0")
    session = log.get("session")
    if not isinstance(session, dict):
        errors.append("session must be an object")
    else:
        if not isinstance(session.get("id"), str) or not SAFE_SESSION_ID.fullmatch(
            session["id"]
        ):
            errors.append("session.id is invalid")
        if session.get("phase") not in PHASES:
            errors.append("session.phase is invalid")
        if session.get("location_class") not in LOCATION_CLASSES:
            errors.append("session.location_class is invalid")
        for key in ("started_utc", "completed_utc"):
            try:
                _require_timestamp(session.get(key), f"session.{key}")
            except FieldLogError as exc:
                errors.append(str(exc))

    traffic = log.get("traffic")
    if not isinstance(traffic, dict):
        errors.append("traffic must be an object")
    else:
        try:
            attempted = _require_integer(traffic.get("attempted"), "traffic.attempted")
            delivered = _require_integer(traffic.get("delivered"), "traffic.delivered")
            lost = _require_integer(traffic.get("lost"), "traffic.lost")
            _require_integer(traffic.get("duplicates"), "traffic.duplicates")
            if delivered + lost != attempted:
                errors.append("traffic delivered + lost must equal attempted")
            latency = _require_mapping(traffic.get("latency_ms"), "traffic.latency_ms")
            ordered = [
                _require_number(latency.get(key), f"latency.{key}", 0)
                for key in ("minimum", "median", "p95", "maximum")
            ]
            if ordered != sorted(ordered):
                errors.append("latency minimum/median/p95/maximum must be ordered")
        except FieldLogError as exc:
            errors.append(str(exc))

    topology = log.get("topology")
    client_count = 0
    repeater_count = 0
    if not isinstance(topology, dict):
        errors.append("topology must be an object")
    else:
        try:
            client_count = _require_integer(topology.get("clients"), "topology.clients", 1)
            repeater_count = _require_integer(
                topology.get("repeaters"), "topology.repeaters"
            )
            if client_count > 16 or repeater_count > 16:
                errors.append("topology exceeds the bounded 16-node planning limit")
            if topology.get("repeater_required") not in {
                "not_applicable",
                "not_proved",
                "proved",
            }:
                errors.append("topology.repeater_required is invalid")
        except FieldLogError as exc:
            errors.append(str(exc))

    hardware = log.get("hardware")
    if not isinstance(hardware, dict):
        errors.append("hardware must be an object")
    else:
        clients = hardware.get("clients")
        if not isinstance(clients, list) or len(clients) != client_count:
            errors.append("hardware.clients must match topology.clients")
        else:
            labels: set[str] = set()
            for index, client in enumerate(clients):
                if not isinstance(client, dict):
                    errors.append(f"hardware.clients[{index}] must be an object")
                    continue
                label = client.get("label")
                if not isinstance(label, str) or not re.fullmatch(r"client-[1-9][0-9]*", label):
                    errors.append(f"hardware.clients[{index}].label is invalid")
                elif label in labels:
                    errors.append("hardware client labels must be unique")
                else:
                    labels.add(label)
                for field in ("model", "firmware"):
                    if not isinstance(client.get(field), str) or not client[field].strip():
                        errors.append(f"hardware.clients[{index}].{field} is required")
                _validate_counter(
                    client.get("counters"),
                    f"hardware.clients[{index}].counters",
                    errors,
                )
        repeater = hardware.get("repeater")
        if repeater_count == 0:
            if repeater is not None:
                errors.append("hardware.repeater must be null when no repeater is present")
        elif repeater_count == 1:
            if not isinstance(repeater, dict):
                errors.append("hardware.repeater must be an object")
            else:
                for field in ("label", "model", "firmware"):
                    if not isinstance(repeater.get(field), str) or not repeater[field].strip():
                        errors.append(f"hardware.repeater.{field} is required")
                _validate_counter(
                    repeater.get("counters"), "hardware.repeater.counters", errors
                )
        else:
            errors.append("OTFL0/v0 supports at most one summarized repeater")

    radio = log.get("radio")
    if not isinstance(radio, dict):
        errors.append("radio must be an object")
    else:
        try:
            _require_number(radio.get("frequency_mhz"), "radio.frequency_mhz", 1)
            _require_number(radio.get("bandwidth_khz"), "radio.bandwidth_khz", 1)
            spreading_factor = _require_integer(
                radio.get("spreading_factor"), "radio.spreading_factor", 5
            )
            coding_rate = _require_integer(
                radio.get("coding_rate_denominator"),
                "radio.coding_rate_denominator",
                5,
            )
            if spreading_factor > 12 or coding_rate > 8:
                errors.append("radio modulation is outside supported LoRa bounds")
            for field in ("client_tx_power_dbm", "repeater_tx_power_dbm"):
                if isinstance(radio.get(field), bool) or not isinstance(
                    radio.get(field), int
                ):
                    errors.append(f"radio.{field} must be an integer")
            for field in ("repeater_enabled_start", "repeater_enabled_end"):
                if not isinstance(radio.get(field), bool):
                    errors.append(f"radio.{field} must be Boolean")
        except FieldLogError as exc:
            errors.append(str(exc))

    cleanup = log.get("cleanup")
    if not isinstance(cleanup, dict):
        errors.append("cleanup must be an object")
    else:
        try:
            expected = _require_integer(
                cleanup.get("clients_expected"), "cleanup.clients_expected"
            )
            verified = _require_integer(
                cleanup.get("clients_verified"), "cleanup.clients_verified"
            )
            if expected != client_count or verified > expected:
                errors.append("cleanup client counts are inconsistent")
            if not isinstance(cleanup.get("lease_journal_removed"), bool):
                errors.append("cleanup.lease_journal_removed must be Boolean")
        except FieldLogError as exc:
            errors.append(str(exc))

    result = log.get("result")
    if not isinstance(result, dict) or not isinstance(
        result.get("raw_test_reported_success"), bool
    ):
        errors.append("result.raw_test_reported_success must be Boolean")

    privacy = log.get("privacy")
    if not isinstance(privacy, dict) or privacy != {
        "aggregate_only": True,
        "transport_ports_included": False,
        "hardware_identifiers_included": False,
        "precise_coordinates_included": False,
        "secrets_included": False,
    }:
        errors.append("privacy declaration is missing or noncanonical")
    _scan_privacy(log, "", errors)
    if errors:
        raise FieldLogError("; ".join(errors))


def validate_public_plan(plan_document: dict[str, Any]) -> None:
    errors: list[str] = []
    if (
        plan_document.get("schema") != PLAN_SCHEMA
        or plan_document.get("version") != PLAN_VERSION
    ):
        errors.append("schema/version must be OTFP0/v0")

    plan = plan_document.get("plan")
    status = None
    if not isinstance(plan, dict):
        errors.append("plan must be an object")
    else:
        plan_id = plan.get("id")
        if not isinstance(plan_id, str) or not SAFE_SESSION_ID.fullmatch(plan_id):
            errors.append("plan.id is invalid")
        status = plan.get("status")
        if status not in {"draft_blocked", "ready"}:
            errors.append("plan.status is invalid")
        if plan.get("phase") != "four-standalone":
            errors.append("plan.phase must be four-standalone")

    topology = plan_document.get("topology")
    if not isinstance(topology, dict):
        errors.append("topology must be an object")
    else:
        if topology.get("clients") != 4 or topology.get("repeaters") != 0:
            errors.append("v0 pilot topology must be four clients and no repeater")

    requirements = plan_document.get("requirements")
    required_false = {
        "internet_required",
        "laptop_required_during_session",
        "phone_required",
        "repeater_required",
        "server_required",
        "vehicle_connection_required",
    }
    if not isinstance(requirements, dict) or set(requirements) != required_false:
        errors.append("requirements must contain the canonical dependency flags")
    elif any(requirements[key] is not False for key in required_false):
        errors.append("the standalone pilot cannot require external dependencies")

    hardware = plan_document.get("hardware_freeze")
    if not isinstance(hardware, dict):
        errors.append("hardware_freeze must be an object")
    else:
        selection = hardware.get("selection_status")
        model = hardware.get("client_model")
        firmware = hardware.get("firmware_version")
        capability_flags = (
            "battery_required",
            "display_required",
            "enclosure_required",
            "gnss_required",
            "identical_units_required",
            "local_input_required",
            "usb_recovery_required",
        )
        if any(hardware.get(flag) is not True for flag in capability_flags):
            errors.append("all standalone hardware capability gates must remain required")
        if selection == "pending":
            if status != "draft_blocked" or model is not None or firmware is not None:
                errors.append("pending hardware requires draft_blocked with no claimed model")
        elif selection == "frozen":
            if status != "ready":
                errors.append("frozen hardware requires ready plan status")
            if not isinstance(model, str) or not model.strip():
                errors.append("frozen hardware requires client_model")
            if not isinstance(firmware, str) or not firmware.strip():
                errors.append("frozen hardware requires firmware_version")
        else:
            errors.append("hardware_freeze.selection_status is invalid")

    session = plan_document.get("session")
    client_count = 4
    duration_minutes = 0
    if not isinstance(session, dict):
        errors.append("session must be an object")
    else:
        try:
            duration_minutes = _require_integer(
                session.get("duration_minutes"), "session.duration_minutes", 1
            )
            minimum_sessions = _require_integer(
                session.get("minimum_sessions"), "session.minimum_sessions", 1
            )
            if minimum_sessions < 3:
                errors.append("at least three materially different sessions are required")
            scenarios = session.get("scenario_classes")
            if not isinstance(scenarios, list) or any(
                not isinstance(item, str) for item in scenarios
            ):
                errors.append("session scenario classes must be a string array")
            elif len(set(scenarios)) < 3:
                errors.append("at least three unique broad scenario classes are required")
            elif any(item not in LOCATION_CLASSES for item in scenarios):
                errors.append("session scenario class is invalid")
        except FieldLogError as exc:
            errors.append(str(exc))

    traffic = plan_document.get("traffic")
    if not isinstance(traffic, dict):
        errors.append("traffic must be an object")
    else:
        try:
            position_interval = _require_integer(
                traffic.get("position_interval_seconds"),
                "traffic.position_interval_seconds",
                1,
            )
            status_interval = _require_integer(
                traffic.get("status_interval_seconds"),
                "traffic.status_interval_seconds",
                1,
            )
            quick_per_client = _require_integer(
                traffic.get("quick_statuses_per_client"),
                "traffic.quick_statuses_per_client",
            )
            alerts_per_client = _require_integer(
                traffic.get("critical_alerts_per_client"),
                "traffic.critical_alerts_per_client",
            )
            seconds = duration_minutes * 60
            expected = {
                "position_origins": (seconds // position_interval) * client_count,
                "status_origins": (seconds // status_interval) * client_count,
                "quick_status_origins": quick_per_client * client_count,
                "critical_alert_origins": alerts_per_client * client_count,
            }
            expected["peer_delivery_opportunities"] = sum(expected.values()) * (
                client_count - 1
            )
            for key, value in expected.items():
                if traffic.get(key) != value:
                    errors.append(f"traffic.{key} must equal {value}")
        except FieldLogError as exc:
            errors.append(str(exc))

    acceptance = plan_document.get("acceptance")
    if not isinstance(acceptance, dict):
        errors.append("acceptance must be an object")
    else:
        zero_fields = (
            "critical_alert_losses_allowed",
            "false_successes_allowed",
            "operator_visible_duplicates_allowed",
            "queue_overflows_allowed",
            "resets_allowed",
        )
        if any(acceptance.get(field) != 0 for field in zero_fields):
            errors.append("critical loss/false success/duplicate/overflow/reset limits are zero")
        try:
            position_ppm = _require_integer(
                acceptance.get("position_delivery_min_ppm"),
                "acceptance.position_delivery_min_ppm",
                1,
            )
            if position_ppm > 1000000:
                errors.append("position delivery threshold cannot exceed 1000000 ppm")
            for field in (
                "critical_latency_max_ms",
                "ending_battery_min_percent",
                "gps_first_fix_max_ms",
                "peer_stale_max_ms",
                "p95_latency_max_ms",
            ):
                _require_integer(acceptance.get(field), f"acceptance.{field}", 1)
            if acceptance.get("ending_battery_min_percent", 101) > 100:
                errors.append("ending battery threshold cannot exceed 100 percent")
        except FieldLogError as exc:
            errors.append(str(exc))

    privacy = plan_document.get("privacy")
    canonical_privacy = {
        "aggregate_only": True,
        "hardware_identifiers_included": False,
        "participant_identifiers_included": False,
        "precise_coordinates_included": False,
        "secrets_included": False,
        "transport_ports_included": False,
    }
    if privacy != canonical_privacy:
        errors.append("plan privacy declaration is missing or noncanonical")
    _scan_privacy(plan_document, "", errors)
    if errors:
        raise FieldLogError("; ".join(errors))


def write_public_log(path: Path, log: dict[str, Any], overwrite: bool = False) -> None:
    validate_public_log(log)
    if path.exists() and not overwrite:
        raise FieldLogError(f"output already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(log, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    summarize = subparsers.add_parser("summarize")
    summarize.add_argument("--input", type=Path, required=True)
    summarize.add_argument("--output", type=Path, required=True)
    summarize.add_argument("--session-id", required=True)
    summarize.add_argument("--phase", choices=sorted(PHASES), required=True)
    summarize.add_argument(
        "--location-class", choices=sorted(LOCATION_CLASSES), required=True
    )
    summarize.add_argument("--client-model", required=True)
    summarize.add_argument("--client-firmware", required=True)
    summarize.add_argument("--repeater-model", required=True)
    summarize.add_argument("--repeater-firmware", required=True)
    summarize.add_argument("--frequency-mhz", type=float, required=True)
    summarize.add_argument("--bandwidth-khz", type=float, required=True)
    summarize.add_argument("--spreading-factor", type=int, required=True)
    summarize.add_argument("--coding-rate", type=int, required=True)
    summarize.add_argument("--client-tx-power-dbm", type=int, required=True)
    summarize.add_argument("--repeater-tx-power-dbm", type=int, required=True)
    summarize.add_argument("--overwrite", action="store_true")

    validate = subparsers.add_parser("validate")
    validate.add_argument("--input", type=Path, required=True)
    validate_plan = subparsers.add_parser("validate-plan")
    validate_plan.add_argument("--input", type=Path, required=True)
    return parser


def main() -> int:
    args = _build_parser().parse_args()
    try:
        raw = json.loads(args.input.read_text(encoding="utf-8"))
        if args.command == "validate":
            validate_public_log(_require_mapping(raw, "document"))
            print(f"PASS: valid {SCHEMA}/v{VERSION} public field-test log")
            return 0
        if args.command == "validate-plan":
            validate_public_plan(_require_mapping(raw, "document"))
            print(f"PASS: valid {PLAN_SCHEMA}/v{PLAN_VERSION} public field-test plan")
            return 0
        log = summarize_meshcore_soak(
            _require_mapping(raw, "document"),
            session_id=args.session_id,
            phase=args.phase,
            location_class=args.location_class,
            client_model=args.client_model,
            client_firmware=args.client_firmware,
            repeater_model=args.repeater_model,
            repeater_firmware=args.repeater_firmware,
            frequency_mhz=args.frequency_mhz,
            bandwidth_khz=args.bandwidth_khz,
            spreading_factor=args.spreading_factor,
            coding_rate=args.coding_rate,
            client_tx_power_dbm=args.client_tx_power_dbm,
            repeater_tx_power_dbm=args.repeater_tx_power_dbm,
        )
        write_public_log(args.output, log, overwrite=args.overwrite)
        print(json.dumps({"success": True, "output": str(args.output)}))
        return 0
    except (FieldLogError, OSError, json.JSONDecodeError) as exc:
        print(json.dumps({"success": False, "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
