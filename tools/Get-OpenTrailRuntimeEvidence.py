"""Query redacted MeshCore runtime evidence for discovered USB candidates.

Only read-only version, board, and role requests are used. Runtime evidence is
explicitly non-authoritative for flashing and never exposes raw replies,
identity, pairing data, keys, settings, or local ports by default.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from typing import Any, Callable, Iterable


TOOLS_ROOT = Path(__file__).resolve().parent
USB_TOOL_PATH = TOOLS_ROOT / "Get-OpenTrailUsbCandidates.py"
USB_SPEC = importlib.util.spec_from_file_location("opentrail_usb_candidates", USB_TOOL_PATH)
if USB_SPEC is None or USB_SPEC.loader is None:
    raise RuntimeError("USB candidate module could not be loaded")
USB = importlib.util.module_from_spec(USB_SPEC)
USB_SPEC.loader.exec_module(USB)

_FIRMWARE = re.compile(r"^v(\d{1,3})\.(\d{1,3})\.(\d{1,3})-([0-9a-f]{7,40})$")
_BUILD_DATE = re.compile(r"^\d{2}-[A-Z][a-z]{2}-\d{4}$")
_REPEATER_VERSION = re.compile(
    r"^(v\d{1,3}\.\d{1,3}\.\d{1,3}-[0-9a-f]{7,40}) "
    r"\(Build: (\d{2}-[A-Z][a-z]{2}-\d{4})\)$"
)

_COMPANION_MODELS = {
    "Heltec V4 OLED": "heltec_v4_oled",
    "Seeed Wio Tracker L1": "seeed_wio_tracker_l1",
}


def _validated_firmware(value: Any) -> str:
    if not isinstance(value, str) or _FIRMWARE.fullmatch(value) is None:
        raise ValueError("invalid runtime firmware")
    return value


def _validated_build_date(value: Any) -> str:
    if not isinstance(value, str) or _BUILD_DATE.fullmatch(value) is None:
        raise ValueError("invalid runtime build date")
    return value


def parse_companion_version_json(
    text: str,
    *,
    expected_model: str = "Heltec V4 OLED",
) -> dict[str, Any]:
    try:
        payload = json.loads(text)
    except (TypeError, json.JSONDecodeError) as error:
        raise ValueError("invalid companion version response") from error
    if not isinstance(payload, dict):
        raise ValueError("invalid companion version response")
    runtime_family = _COMPANION_MODELS.get(expected_model)
    if runtime_family is None or payload.get("model") != expected_model:
        raise ValueError("unrecognized companion runtime model")
    protocol = payload.get("fw ver")
    if not isinstance(protocol, int) or isinstance(protocol, bool) or not 0 <= protocol <= 65535:
        raise ValueError("invalid companion runtime protocol")
    return {
        "runtime_query_succeeded": True,
        "runtime_board_family": runtime_family,
        "runtime_firmware": _validated_firmware(payload.get("ver")),
        "runtime_build_date": _validated_build_date(payload.get("fw_build")),
        "runtime_protocol_version": protocol,
        "runtime_role": "meshcore_companion",
        "runtime_identity_authoritative_for_flash": False,
    }


def extract_repeater_reply(command: str, response: str) -> str:
    lines = [
        line.strip()
        for line in response.replace("\r", "").split("\n")
        if line.strip() and line.strip() != command
    ]
    if len(lines) != 1 or not lines[0].startswith("->"):
        raise ValueError("unexpected repeater runtime response")
    value = lines[0][2:].strip()
    if value.startswith(">"):
        value = value[1:].strip()
    if not value:
        raise ValueError("empty repeater runtime response")
    return value


def reduce_repeater_runtime(responses: dict[str, str]) -> dict[str, Any]:
    if set(responses) != {"board", "ver", "get role"}:
        raise ValueError("incomplete repeater runtime response")
    board = extract_repeater_reply("board", responses["board"])
    if board != "Seeed SenseCap Solar":
        raise ValueError("unrecognized repeater runtime model")
    version_value = extract_repeater_reply("ver", responses["ver"])
    match = _REPEATER_VERSION.fullmatch(version_value)
    if match is None:
        raise ValueError("invalid repeater runtime version")
    role = extract_repeater_reply("get role", responses["get role"]).lower()
    if role != "repeater":
        raise ValueError("unexpected repeater runtime role")
    return {
        "runtime_query_succeeded": True,
        "runtime_board_family": "seeed_sensecap_solar",
        "runtime_firmware": _validated_firmware(match.group(1)),
        "runtime_build_date": _validated_build_date(match.group(2)),
        "runtime_protocol_version": None,
        "runtime_role": "meshcore_repeater",
        "runtime_identity_authoritative_for_flash": False,
    }


def _resolve_meshcli() -> str:
    resolved = shutil.which("meshcli")
    if resolved:
        return resolved
    appdata = os.environ.get("APPDATA")
    if appdata:
        fallback = Path(appdata) / "Python" / "Python314" / "Scripts" / "meshcli.exe"
        if fallback.is_file():
            return str(fallback)
    raise FileNotFoundError("meshcli unavailable")


def _query_companion_model(port: str, expected_model: str) -> dict[str, Any]:
    completed = subprocess.run(
        [_resolve_meshcli(), "-j", "-s", port, "ver"],
        capture_output=True,
        check=False,
        text=True,
        timeout=15,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if completed.returncode != 0:
        raise RuntimeError("companion version query failed")
    return parse_companion_version_json(
        completed.stdout,
        expected_model=expected_model,
    )


def query_companion(port: str) -> dict[str, Any]:
    return _query_companion_model(port, "Heltec V4 OLED")


def query_wio_companion(port: str) -> dict[str, Any]:
    return _query_companion_model(port, "Seeed Wio Tracker L1")


def _serial_cli_command(connection: Any, command: str) -> str:
    connection.reset_input_buffer()
    connection.write((command + "\r\n").encode("ascii"))
    connection.flush()
    time.sleep(0.35)
    data = bytearray()
    idle = 0
    while idle < 4:
        waiting = connection.in_waiting
        if waiting:
            data.extend(connection.read(waiting))
            idle = 0
        else:
            idle += 1
        time.sleep(0.1)
    return data.decode("utf-8", "replace")


def query_repeater(port: str) -> dict[str, Any]:
    import serial

    with serial.Serial(port, 115200, timeout=0.25) as connection:
        time.sleep(0.25)
        responses = {
            command: _serial_cli_command(connection, command)
            for command in ("board", "ver", "get role")
        }
    return reduce_repeater_runtime(responses)


def collect_runtime_evidence(
    records: Iterable[Any],
    *,
    companion_query: Callable[[str], dict[str, Any]] = query_companion,
    wio_companion_query: Callable[[str], dict[str, Any]] = query_wio_companion,
    repeater_query: Callable[[str], dict[str, Any]] = query_repeater,
    include_local_ports: bool = False,
) -> dict[str, Any]:
    selected = USB.select_candidate_records(records)
    discovery = USB.collect_candidates(
        [item[2] for item in selected], include_local_ports=include_local_ports
    )
    devices = discovery["devices"]
    for device, selected_item in zip(devices, selected, strict=True):
        port = selected_item[1]
        family = selected_item[3]["name"]
        try:
            if family == "espressif_application_usb":
                runtime = companion_query(port)
            elif family == "seeed_wio_tracker_l1_usb":
                runtime = wio_companion_query(port)
            elif family == "seeed_tinyusb_serial":
                runtime = repeater_query(port)
            else:
                raise ValueError("unsupported runtime family")
            device.update(runtime)
        except Exception as error:
            device.update(
                {
                    "runtime_query_succeeded": False,
                    "runtime_error_type": type(error).__name__,
                    "runtime_identity_authoritative_for_flash": False,
                }
            )
        device["flashing_allowed"] = False

    return {
        "schema": "ot_meshcore_runtime_evidence_v0",
        "read_only": True,
        "state_changes_made": False,
        "privacy": {
            "local_ports_included": include_local_ports,
            "serial_numbers_included": False,
            "hardware_instance_ids_included": False,
            "device_locations_included": False,
            "raw_responses_included": False,
            "pairing_data_included": False,
            "device_identity_included": False,
        },
        "candidate_count": len(devices),
        "runtime_query_success_count": sum(
            item.get("runtime_query_succeeded") is True for item in devices
        ),
        "flashing_allowed_count": 0,
        "devices": devices,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-local-ports", action="store_true")
    args = parser.parse_args()

    from serial.tools import list_ports

    snapshot = collect_runtime_evidence(
        list_ports.comports(), include_local_ports=args.include_local_ports
    )
    print(json.dumps(snapshot, indent=2, sort_keys=True))
    return 0 if snapshot["runtime_query_success_count"] == snapshot["candidate_count"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
