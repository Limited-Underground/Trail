#!/usr/bin/env python3
"""Offline, read-only evidence verifier for the OT-071 source proof.

The verifier deliberately returns one fixed category only. It does not return
or retain input paths, operation identities, observed bytes, byte positions, or
observed digests. A satisfied result is evidence for the OT-070 admission
guard; it is not partition-transition or device-write authority.
"""

from __future__ import annotations

import hashlib
import json
import sys
from typing import BinaryIO, Sequence


PARTITION_TABLE_SIZE = 3072
PARTITION_TABLE_SHA256 = (
    "84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB"
)
SOURCE_REGION_SIZE = 1048576
SOURCE_REGION_SHA256 = (
    "F5FB04AA5B882706B9309E885F19477261336EF76A150C3B4D3489DFAC3953EC"
)
STREAM_CHUNK_SIZE = 16384
MAX_IDENTITY = (1 << 64) - 1
OUTPUT_SCHEMA = "OTPSTE0/v0"

SATISFIED = "SOURCE-PROOF-SATISFIED-ONLY"
DENY_INVALID_INPUT = "DENY-INVALID-INPUT"
DENY_INVALID_IDENTITY = "DENY-INVALID-IDENTITY"
DENY_PARTITION_SIZE = "DENY-PARTITION-SIZE"
DENY_PARTITION_DIGEST = "DENY-PARTITION-DIGEST"
DENY_SOURCE_SIZE = "DENY-SOURCE-SIZE"
DENY_SOURCE_NONBLANK = "DENY-SOURCE-NONBLANK"
DENY_SOURCE_DIGEST = "DENY-SOURCE-DIGEST"
DENY_READ_FAILURE = "DENY-READ-FAILURE"


def _valid_identity(value: object) -> bool:
    return type(value) is int and 0 < value <= MAX_IDENTITY


def _partition_outcome(stream: BinaryIO) -> str | None:
    digest = hashlib.sha256()
    count = 0
    while count <= PARTITION_TABLE_SIZE:
        chunk = stream.read(
            min(STREAM_CHUNK_SIZE, PARTITION_TABLE_SIZE + 1 - count)
        )
        if not chunk:
            break
        count += len(chunk)
        if count > PARTITION_TABLE_SIZE:
            return DENY_PARTITION_SIZE
        digest.update(chunk)

    if count != PARTITION_TABLE_SIZE:
        return DENY_PARTITION_SIZE
    if digest.hexdigest().upper() != PARTITION_TABLE_SHA256:
        return DENY_PARTITION_DIGEST
    return None


def _source_outcome(stream: BinaryIO) -> str | None:
    digest = hashlib.sha256()
    count = 0
    nonblank = False
    while count <= SOURCE_REGION_SIZE:
        chunk = stream.read(min(STREAM_CHUNK_SIZE, SOURCE_REGION_SIZE + 1 - count))
        if not chunk:
            break
        count += len(chunk)
        if count > SOURCE_REGION_SIZE:
            return DENY_SOURCE_SIZE
        if not nonblank and chunk != b"\xff" * len(chunk):
            nonblank = True
        if not nonblank:
            digest.update(chunk)

    if count != SOURCE_REGION_SIZE:
        return DENY_SOURCE_SIZE
    if nonblank:
        return DENY_SOURCE_NONBLANK
    if digest.hexdigest().upper() != SOURCE_REGION_SHA256:
        return DENY_SOURCE_DIGEST
    return None


def verify_streams(
    partition_table: BinaryIO,
    source_region: BinaryIO,
    operation_id: int,
    evidence_set_id: int,
) -> str:
    """Return a fixed sanitized outcome for two already-open binary streams."""

    if not (_valid_identity(operation_id) and _valid_identity(evidence_set_id)):
        return DENY_INVALID_IDENTITY
    try:
        partition_result = _partition_outcome(partition_table)
        if partition_result is not None:
            return partition_result
        source_result = _source_outcome(source_region)
        if source_result is not None:
            return source_result
    except Exception:
        return DENY_READ_FAILURE
    return SATISFIED


def verify_files(
    partition_table_path: str,
    source_region_path: str,
    operation_id: int,
    evidence_set_id: int,
) -> str:
    """Open two ordinary files read-only and return only a sanitized outcome."""

    if not (_valid_identity(operation_id) and _valid_identity(evidence_set_id)):
        return DENY_INVALID_IDENTITY
    try:
        with open(partition_table_path, "rb") as partition_table:
            with open(source_region_path, "rb") as source_region:
                return verify_streams(
                    partition_table, source_region, operation_id, evidence_set_id
                )
    except Exception:
        return DENY_READ_FAILURE


def _parse_identity(value: str) -> int | None:
    try:
        parsed = int(value, 10)
    except (TypeError, ValueError):
        return None
    return parsed if _valid_identity(parsed) else None


def _parse_arguments(
    arguments: Sequence[str],
) -> tuple[str, str, int, int] | str:
    expected = {
        "--partition-table",
        "--source-region",
        "--operation-id",
        "--evidence-set-id",
    }
    if len(arguments) != 8:
        return DENY_INVALID_INPUT
    values: dict[str, str] = {}
    for index in range(0, len(arguments), 2):
        name = arguments[index]
        if name not in expected or name in values:
            return DENY_INVALID_INPUT
        values[name] = arguments[index + 1]
    if set(values) != expected:
        return DENY_INVALID_INPUT
    operation_id = _parse_identity(values["--operation-id"])
    evidence_set_id = _parse_identity(values["--evidence-set-id"])
    if operation_id is None or evidence_set_id is None:
        return DENY_INVALID_IDENTITY
    return (
        values["--partition-table"],
        values["--source-region"],
        operation_id,
        evidence_set_id,
    )


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    parsed = _parse_arguments(arguments)
    outcome = parsed if isinstance(parsed, str) else verify_files(*parsed)
    print(
        json.dumps(
            {"schema": OUTPUT_SCHEMA, "outcome": outcome},
            separators=(",", ":"),
        )
    )
    return 0 if outcome == SATISFIED else 1


if __name__ == "__main__":
    raise SystemExit(main())
