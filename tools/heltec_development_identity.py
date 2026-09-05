#!/usr/bin/env python3
"""Read and bind an ESP32-S3 factory base MAC for bench-device preflight.

Raw identifiers live only in the ignored development registry. Repository and
CI output can use the stable OT-DEV identifier returned by this tool.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


SCHEMA_VERSION = "OTDEV-ID/v1"
DEFAULT_REGISTRY = (
    Path(__file__).resolve().parents[1]
    / ".private"
    / "development-device-identities.json"
)
INVENTORY_ID_RE = re.compile(r"^OT-DEV-[0-9]{3}$")
BASE_MAC_RE = re.compile(
    r"(?im)^\s*MAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})\s*$"
)


class IdentityError(RuntimeError):
    """A stable, non-transport-specific identity failure."""


def normalize_inventory_id(value: str) -> str:
    normalized = value.strip().upper()
    if not INVENTORY_ID_RE.fullmatch(normalized):
        raise IdentityError("invalid_inventory_id")
    return normalized


def normalize_base_mac(value: str) -> str:
    compact = re.sub(r"[^0-9a-fA-F]", "", value).upper()
    if len(compact) != 12 or not re.fullmatch(r"[0-9A-F]{12}", compact):
        raise IdentityError("invalid_base_mac")
    return ":".join(compact[index : index + 2] for index in range(0, 12, 2))


def parse_base_mac(output: str) -> str:
    matches = {normalize_base_mac(value) for value in BASE_MAC_RE.findall(output)}
    if len(matches) != 1:
        raise IdentityError("read_mac_output_invalid")
    return next(iter(matches))


def read_base_mac(port: str, timeout_seconds: int = 30) -> str:
    if not port or not port.strip():
        raise IdentityError("port_required")
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "115200",
        "--before",
        "no-reset",
        "--after",
        "no-reset",
        "--no-stub",
        "read-mac",
    ]
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise IdentityError("read_mac_transport_failed") from exc
    if result.returncode != 0:
        raise IdentityError("read_mac_failed")
    return parse_base_mac(result.stdout + "\n" + result.stderr)


def new_registry() -> dict[str, Any]:
    return {"schema_version": SCHEMA_VERSION, "devices": []}


def load_registry(path: Path) -> dict[str, Any]:
    if not path.exists():
        return new_registry()
    try:
        registry = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise IdentityError("registry_unreadable") from exc
    if registry.get("schema_version") != SCHEMA_VERSION:
        raise IdentityError("registry_schema_invalid")
    devices = registry.get("devices")
    if not isinstance(devices, list):
        raise IdentityError("registry_devices_invalid")
    seen_ids: set[str] = set()
    seen_macs: set[str] = set()
    for device in devices:
        if not isinstance(device, dict):
            raise IdentityError("registry_device_invalid")
        inventory_id = normalize_inventory_id(str(device.get("inventory_id", "")))
        base_mac = normalize_base_mac(str(device.get("esp32s3_base_mac", "")))
        if inventory_id in seen_ids or base_mac in seen_macs:
            raise IdentityError("registry_identity_duplicate")
        seen_ids.add(inventory_id)
        seen_macs.add(base_mac)
    return registry


def enroll(
    registry: dict[str, Any], inventory_id: str, base_mac: str, observed_at: str
) -> str:
    normalized_id = normalize_inventory_id(inventory_id)
    normalized_mac = normalize_base_mac(base_mac)
    devices = registry["devices"]
    for device in devices:
        if device["inventory_id"] == normalized_id:
            if normalize_base_mac(device["esp32s3_base_mac"]) != normalized_mac:
                raise IdentityError("inventory_id_already_bound")
            device["last_verified_at"] = observed_at
            return "MATCHED"
        if normalize_base_mac(device["esp32s3_base_mac"]) == normalized_mac:
            raise IdentityError("base_mac_already_bound")
    devices.append(
        {
            "inventory_id": normalized_id,
            "chip": "esp32s3",
            "esp32s3_base_mac": normalized_mac,
            "enrolled_at": observed_at,
            "last_verified_at": observed_at,
        }
    )
    devices.sort(key=lambda item: item["inventory_id"])
    return "ENROLLED"


def verify(registry: dict[str, Any], inventory_id: str, base_mac: str) -> None:
    normalized_id = normalize_inventory_id(inventory_id)
    normalized_mac = normalize_base_mac(base_mac)
    for device in registry["devices"]:
        if device["inventory_id"] == normalized_id:
            if normalize_base_mac(device["esp32s3_base_mac"]) != normalized_mac:
                raise IdentityError("device_identity_mismatch")
            return
    raise IdentityError("inventory_id_not_enrolled")


def save_registry(path: Path, registry: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    payload = json.dumps(registry, indent=2) + "\n"
    try:
        temporary.write_text(payload, encoding="utf-8")
        temporary.replace(path)
    except OSError as exc:
        raise IdentityError("registry_write_failed") from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("enroll", "verify"))
    parser.add_argument("--inventory-id", required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--show-mac", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        inventory_id = normalize_inventory_id(args.inventory_id)
        base_mac = read_base_mac(args.port)
        registry = load_registry(args.registry)
        if args.mode == "enroll":
            observed_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
            outcome = enroll(registry, inventory_id, base_mac, observed_at)
            save_registry(args.registry, registry)
        else:
            verify(registry, inventory_id, base_mac)
            outcome = "MATCHED"
    except IdentityError as exc:
        print(f"{SCHEMA_VERSION} DENIED {exc}", file=sys.stderr)
        return 2
    suffix = f" MAC={base_mac}" if args.show_mac else ""
    print(f"{SCHEMA_VERSION} {outcome} {inventory_id}{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
