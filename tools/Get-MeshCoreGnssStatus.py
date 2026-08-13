"""Emit a redacted, read-only MeshCore GNSS status snapshot.

Companion telemetry can contain coordinates and device identity. This tool
reduces responses in memory to coarse GNSS state and never prints raw frames,
coordinates, node names, public keys, PINs, addresses, or channel data.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import re
import time
from typing import Any


_REPEATER_ON = re.compile(
    r"^on,\s*([a-z][a-z _-]{0,20}),\s*(fix|no fix),\s*(\d{1,3})\s+sats?$",
    re.IGNORECASE,
)


def companion_custom_status(payload: Any) -> dict[str, Any]:
    if not isinstance(payload, dict) or "gps" not in payload:
        return {"gps_detected": False, "gps_active": None}
    value = payload["gps"]
    if value not in ("0", "1", 0, 1, False, True):
        raise ValueError("invalid companion GPS setting")
    return {"gps_detected": True, "gps_active": str(int(value)) == "1"}


def companion_telemetry_has_gps(payload: Any) -> bool:
    if not isinstance(payload, dict):
        return False
    lpp = payload.get("lpp")
    if not isinstance(lpp, list):
        return False
    return any(
        isinstance(item, dict) and str(item.get("type", "")).lower() == "gps"
        for item in lpp
    )


def parse_repeater_gps_response(command: str, response: str) -> dict[str, Any]:
    lines = [
        line.strip()
        for line in response.replace("\r", "").split("\n")
        if line.strip() and line.strip() != command
    ]
    if len(lines) != 1 or not lines[0].startswith("->"):
        raise ValueError("unexpected repeater GPS response")
    value = lines[0][2:].strip().lower()
    if value == "off":
        return {
            "gps_detected": True,
            "gps_active": False,
            "gps_status": "off",
            "gps_fix": False,
            "satellites": None,
        }
    match = _REPEATER_ON.fullmatch(value)
    if match is None:
        raise ValueError("unexpected repeater GPS response")
    status_name = match.group(1).replace("_", " ").strip()
    if status_name not in ("active", "inactive"):
        raise ValueError("unexpected repeater GPS status")
    satellites = int(match.group(3))
    if satellites > 128:
        raise ValueError("invalid repeater satellite count")
    return {
        "gps_detected": True,
        "gps_active": True,
        "gps_status": status_name,
        "gps_fix": match.group(2).lower() == "fix",
        "satellites": satellites,
    }


async def query_companion(port: str, role: str) -> dict[str, Any]:
    from meshcore import MeshCore

    node = None
    try:
        node = await MeshCore.create_serial(
            port=port, only_error=True, default_timeout=10
        )
        if node is None:
            raise ConnectionError("companion connection unavailable")
        custom = await node.commands.get_custom_vars()
        if custom is None or custom.is_error():
            raise RuntimeError("companion custom-variable query failed")
        status = companion_custom_status(custom.payload)
        status.update({"role": role, "query_succeeded": True})
        if status["gps_active"]:
            telemetry = await node.commands.get_self_telemetry()
            if telemetry is None or telemetry.is_error():
                raise RuntimeError("companion telemetry query failed")
            status["gps_telemetry_present"] = companion_telemetry_has_gps(
                telemetry.payload
            )
        else:
            status["gps_telemetry_present"] = False
        return status
    except Exception as error:
        return {
            "role": role,
            "query_succeeded": False,
            "error_type": type(error).__name__,
        }
    finally:
        if node is not None:
            try:
                await node.disconnect()
            except Exception:
                pass


def query_repeater(port: str, role: str) -> dict[str, Any]:
    import serial

    try:
        with serial.Serial(port, 115200, timeout=0.25) as connection:
            time.sleep(0.25)
            connection.reset_input_buffer()
            connection.write(b"gps\r\n")
            connection.flush()
            time.sleep(0.75)
            data = bytearray()
            idle = 0
            while idle < 4:
                if connection.in_waiting:
                    data.extend(connection.read(connection.in_waiting))
                    idle = 0
                else:
                    idle += 1
                time.sleep(0.1)
        status = parse_repeater_gps_response(
            "gps", data.decode("utf-8", "replace")
        )
        status.update({"role": role, "query_succeeded": True})
        return status
    except Exception as error:
        return {
            "role": role,
            "query_succeeded": False,
            "error_type": type(error).__name__,
        }


async def collect(args: argparse.Namespace) -> dict[str, Any]:
    devices: list[dict[str, Any]] = []
    for index, port in enumerate(args.companion, start=1):
        result = await query_companion(port, f"bench_client_{index}")
        if args.include_local_ports:
            result["local_port"] = port
        devices.append(result)
    for index, port in enumerate(args.repeater, start=1):
        result = query_repeater(port, f"packaged_repeater_{index}")
        if args.include_local_ports:
            result["local_port"] = port
        devices.append(result)
    return {
        "schema": "ot_meshcore_gnss_snapshot_v0",
        "read_only": True,
        "privacy": {
            "coordinates_included": False,
            "device_identity_included": False,
            "keys_or_pins_included": False,
            "raw_responses_included": False,
        },
        "devices": devices,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--companion", action="append", default=[])
    parser.add_argument("--repeater", action="append", default=[])
    parser.add_argument("--include-local-ports", action="store_true")
    args = parser.parse_args()
    if not args.companion and not args.repeater:
        parser.error("at least one --companion or --repeater is required")
    logging.disable(logging.CRITICAL)
    snapshot = asyncio.run(collect(args))
    print(json.dumps(snapshot, indent=2, sort_keys=True))
    return 0 if all(item["query_succeeded"] for item in snapshot["devices"]) else 2


if __name__ == "__main__":
    raise SystemExit(main())
