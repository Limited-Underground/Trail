#!/usr/bin/env python3
"""Build the denied, offline-only OTPS0/v0 partition artifact bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


SCHEMA = "OTPSTB0/v0"
STATUS = "OFFLINE-ONLY-NOT-AUTHORIZED-NOT-EXECUTED"
REQUIRED_IDF_VERSION = "ESP-IDF v6.0.2"
PARTITION_TABLE_SIZE = 3072
CANDIDATE_SOURCE_SIZE = 411
CANDIDATE_SOURCE_SHA256 = (
    "310FD207687C6D9964F8C1BF83031ACFD5EAFD837E7DBFCFF429A5CEE168C3CA"
)
CANDIDATE_BINARY_SHA256 = (
    "F83EDE6D0F206D6032147A2AF0B526700BCB888A6C0CADDF6DD17724E5600E72"
)
CANDIDATE_ENTRIES_MD5 = "2D318DA9C2F0566688277C3BA110A8E9"

PROJECT_ROOT = Path(__file__).resolve().parents[1]
TARGET_ROOT = PROJECT_ROOT / "firmware" / "targets" / "heltec_v4_bench"
CANDIDATE_SOURCE = TARGET_ROOT / "protected-storage-partitions.candidate.csv"
DEFAULT_OUTPUT = (
    PROJECT_ROOT
    / "build"
    / "targets"
    / "heltec_v4_bench"
    / "protected-storage-recovery-bundle"
)

# Numeric values are the exact ESP-IDF partition-table representation.
EXPECTED_ROWS = (
    ("otadata", 0x01, 0x00, 0x009000, 0x002000, 0x00000000),
    ("factory", 0x00, 0x00, 0x010000, 0x4F0000, 0x00000000),
    ("ota_0", 0x00, 0x10, 0x500000, 0x500000, 0x00000000),
    ("ota_1", 0x00, 0x11, 0xA00000, 0x500000, 0x00000000),
    ("ot_auth", 0x01, 0x02, 0xF00000, 0x010000, 0x00000001),
    ("ot_state", 0x40, 0x00, 0xF10000, 0x0F0000, 0x00000000),
)


class BundleError(RuntimeError):
    """A fixed fail-closed offline bundle error."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def _run_fixed(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise BundleError("required offline ESP-IDF tool did not complete") from error
    if result.returncode != 0:
        raise BundleError("required offline ESP-IDF tool rejected the artifact")
    return result.stdout.strip()


def _validate_source() -> None:
    try:
        source = CANDIDATE_SOURCE.read_bytes()
    except OSError as error:
        raise BundleError("candidate partition source is unavailable") from error
    if len(source) != CANDIDATE_SOURCE_SIZE:
        raise BundleError("candidate partition source length mismatch")
    if _sha256(source) != CANDIDATE_SOURCE_SHA256:
        raise BundleError("candidate partition source identity mismatch")


def _decode_and_validate(binary: bytes) -> list[dict[str, int | str]]:
    if len(binary) != PARTITION_TABLE_SIZE:
        raise BundleError("candidate partition binary length mismatch")
    if _sha256(binary) != CANDIDATE_BINARY_SHA256:
        raise BundleError("candidate partition binary identity mismatch")

    decoded: list[dict[str, int | str]] = []
    entry_bytes = bytearray()
    offset = 0
    for expected in EXPECTED_ROWS:
        raw = binary[offset:offset + 32]
        if len(raw) != 32:
            raise BundleError("candidate partition entry is incomplete")
        magic, entry_type, subtype, address, size, label_raw, flags = (
            struct.unpack("<HBBLL16sL", raw)
        )
        if magic != 0x50AA:
            raise BundleError("candidate partition entry magic mismatch")
        label_parts = label_raw.split(b"\x00", 1)
        try:
            label = label_parts[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise BundleError("candidate partition label is invalid") from error
        if len(label_parts) == 2 and any(label_parts[1]):
            raise BundleError("candidate partition label padding is invalid")
        observed = (label, entry_type, subtype, address, size, flags)
        if observed != expected:
            raise BundleError("candidate partition row mismatch")
        decoded.append({
            "name": label,
            "type": entry_type,
            "subtype": subtype,
            "offset": address,
            "size_bytes": size,
            "flags": flags,
        })
        entry_bytes.extend(raw)
        offset += 32

    checksum = binary[offset:offset + 32]
    if len(checksum) != 32 or checksum[:2] != b"\xEB\xEB":
        raise BundleError("candidate partition checksum record is missing")
    if checksum[2:16] != b"\xFF" * 14:
        raise BundleError("candidate partition checksum record is malformed")
    expected_md5 = hashlib.md5(bytes(entry_bytes)).digest()  # nosec B324
    if checksum[16:] != expected_md5:
        raise BundleError("candidate partition entry checksum mismatch")
    if expected_md5.hex().upper() != CANDIDATE_ENTRIES_MD5:
        raise BundleError("candidate partition entry identity mismatch")
    if any(value != 0xFF for value in binary[offset + 32:]):
        raise BundleError("candidate partition binary padding is not erased")
    return decoded


def _validate_idf_version(version_path: Path) -> None:
    try:
        text = version_path.read_text(encoding="utf-8")
    except OSError as error:
        raise BundleError("ESP-IDF version declaration is unavailable") from error
    declarations: dict[str, int] = {}
    pattern = re.compile(r"set\(IDF_VERSION_(MAJOR|MINOR|PATCH)\s+([0-9]+)\)")
    for line in text.splitlines():
        match = pattern.fullmatch(line.strip())
        if match is not None:
            declarations[match.group(1)] = int(match.group(2))
    observed = (
        declarations.get("MAJOR"),
        declarations.get("MINOR"),
        declarations.get("PATCH"),
    )
    if observed != (6, 0, 2):
        raise BundleError("ESP-IDF version mismatch")


def build_bundle(
    idf_path: Path,
    output_dir: Path,
    python_executable: str = sys.executable,
) -> dict:
    """Generate and verify one offline bundle without any physical authority."""
    _validate_source()
    try:
        idf_root = idf_path.resolve(strict=True)
    except OSError as error:
        raise BundleError("ESP-IDF root is unavailable") from error
    version_path = idf_root / "tools" / "cmake" / "version.cmake"
    partition_tool = (
        idf_root / "components" / "partition_table" / "gen_esp32part.py"
    )
    if not version_path.is_file() or not partition_tool.is_file():
        raise BundleError("required ESP-IDF offline tooling is unavailable")
    _validate_idf_version(version_path)

    destination = output_dir.resolve(strict=False)
    if destination.exists():
        raise BundleError("bundle output already exists")
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".ot075-bundle-", dir=destination.parent))
    artifact = stage / "protected-storage-partition-table.bin"
    receipt_path = stage / "protected-storage-recovery-bundle.json"
    completed = False
    try:
        _run_fixed([
            python_executable,
            str(partition_tool),
            "--flash-size",
            "16MB",
            "--quiet",
            str(CANDIDATE_SOURCE),
            str(artifact),
        ])
        try:
            binary = artifact.read_bytes()
        except OSError as error:
            raise BundleError("candidate partition binary was not produced") from error
        decoded = _decode_and_validate(binary)
        receipt = {
            "schema": SCHEMA,
            "status": STATUS,
            "framework": REQUIRED_IDF_VERSION,
            "target": "heltec_v4_bench",
            "candidate_source": {
                "file": CANDIDATE_SOURCE.name,
                "bytes": CANDIDATE_SOURCE_SIZE,
                "sha256": CANDIDATE_SOURCE_SHA256,
            },
            "candidate_binary": {
                "file": artifact.name,
                "bytes": PARTITION_TABLE_SIZE,
                "sha256": CANDIDATE_BINARY_SHA256,
                "entry_md5": CANDIDATE_ENTRIES_MD5,
                "flash_offset": 0x008000,
                "rows": decoded,
            },
            "execution": {"commands": [], "attempts": 0},
            "authority": {
                "device_access_authorized": False,
                "partition_table_write_authorized": False,
                "recovery_authorized": False,
            },
        }
        receipt_path.write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        stage.replace(destination)
        completed = True
        return receipt
    finally:
        if not completed and stage.exists():
            shutil.rmtree(stage)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build the denied offline OTPS0/v0 partition artifact bundle."
    )
    parser.add_argument("--idf-path", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        receipt = build_bundle(arguments.idf_path, DEFAULT_OUTPUT)
    except BundleError as error:
        print(f"DENY: {error}", file=sys.stderr)
        return 1
    print(json.dumps({
        "schema": receipt["schema"],
        "status": receipt["status"],
        "candidate_binary_sha256": receipt["candidate_binary"]["sha256"],
    }, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
