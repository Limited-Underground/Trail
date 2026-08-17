#!/usr/bin/env python3
"""Focused deterministic tests for the OT-071 offline evidence verifier."""

from __future__ import annotations

import ast
import contextlib
import hashlib
import importlib.util
import io
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "protected_storage_transition_evidence.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_tool():
    spec = importlib.util.spec_from_file_location(
        "protected_storage_transition_evidence", TOOL_PATH
    )
    require(spec is not None and spec.loader is not None, "unable to load verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TOOL = load_tool()


def exact_partition_table() -> bytes:
    rows = (
        (0x01, 0x00, 0x009000, 0x002000, "otadata"),
        (0x00, 0x00, 0x010000, 0x4F0000, "factory"),
        (0x00, 0x10, 0x500000, 0x500000, "ota_0"),
        (0x00, 0x11, 0xA00000, 0x500000, "ota_1"),
        (0x40, 0x00, 0xF00000, 0x100000, "ot_state"),
    )
    entries = b"".join(
        struct.pack(
            "<HBBLL16sL",
            0x50AA,
            entry_type,
            subtype,
            offset,
            size,
            label.encode("ascii").ljust(16, b"\x00"),
            0,
        )
        for entry_type, subtype, offset, size, label in rows
    )
    checksum = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    table = (entries + checksum).ljust(TOOL.PARTITION_TABLE_SIZE, b"\xff")
    require(
        hashlib.sha256(table).hexdigest().upper() == TOOL.PARTITION_TABLE_SHA256,
        "deterministic partition fixture no longer has the accepted digest",
    )
    return table


PARTITION_TABLE = exact_partition_table()
BLANK_SOURCE = b"\xff" * TOOL.SOURCE_REGION_SIZE


class ChunkedStream:
    def __init__(self, data: bytes, maximum_return: int = 997) -> None:
        self._data = data
        self._offset = 0
        self._maximum_return = maximum_return
        self.requests: list[int] = []

    def read(self, size: int) -> bytes:
        require(size > 0, "verifier must always use bounded streaming reads")
        self.requests.append(size)
        end = min(self._offset + min(size, self._maximum_return), len(self._data))
        result = self._data[self._offset:end]
        self._offset = end
        return result


class UnreadableStream:
    def read(self, size: int) -> bytes:
        raise AssertionError("invalid identities must be rejected before reading")


class FailingStream:
    def read(self, size: int) -> bytes:
        raise OSError("private path and byte details must not escape")


def verify(partition: bytes, source: bytes, operation_id: int = 7,
           evidence_set_id: int = 11) -> str:
    return TOOL.verify_streams(
        io.BytesIO(partition), io.BytesIO(source), operation_id, evidence_set_id
    )


def test_exact_success_is_streamed_and_nonauthorizing() -> None:
    partition = ChunkedStream(PARTITION_TABLE, 113)
    source = ChunkedStream(BLANK_SOURCE, 4093)
    outcome = TOOL.verify_streams(partition, source, 1, TOOL.MAX_IDENTITY)
    require(outcome == TOOL.SATISFIED, "exact artifacts must satisfy source proof")
    require(len(partition.requests) > 2 and len(source.requests) > 100,
            "both artifacts must be streamed")
    require(max(partition.requests + source.requests) <= TOOL.STREAM_CHUNK_SIZE,
            "streaming read exceeded its fixed bound")
    require("AUTHOR" not in outcome and "WRITE" not in outcome,
            "success must remain source-proof-only evidence")
    require(hashlib.sha256(BLANK_SOURCE).hexdigest().upper() ==
            TOOL.SOURCE_REGION_SHA256,
            "blank-region contract digest changed")


def test_exact_sizes_are_required() -> None:
    require(verify(PARTITION_TABLE[:-1], BLANK_SOURCE) ==
            TOOL.DENY_PARTITION_SIZE, "short partition artifact must deny")
    require(verify(PARTITION_TABLE + b"\xff", BLANK_SOURCE) ==
            TOOL.DENY_PARTITION_SIZE, "long partition artifact must deny")
    require(verify(PARTITION_TABLE, BLANK_SOURCE[:-1]) ==
            TOOL.DENY_SOURCE_SIZE, "short source region must deny")
    require(verify(PARTITION_TABLE, BLANK_SOURCE + b"\xff") ==
            TOOL.DENY_SOURCE_SIZE, "long source region must deny")


def test_non_ff_positions_share_one_sanitized_outcome() -> None:
    positions = (0, 1, 65535, 65536, TOOL.SOURCE_REGION_SIZE - 1)
    outcomes = []
    for position in positions:
        source = bytearray(BLANK_SOURCE)
        source[position] = 0
        outcomes.append(verify(PARTITION_TABLE, bytes(source)))
    require(set(outcomes) == {TOOL.DENY_SOURCE_NONBLANK},
            "nonblank evidence must not reveal byte location")


def test_wrong_partition_has_one_fixed_digest_denial() -> None:
    wrong_first = bytearray(PARTITION_TABLE)
    wrong_first[0] ^= 1
    wrong_last = bytearray(PARTITION_TABLE)
    wrong_last[-1] ^= 1
    require(
        {
            verify(bytes(wrong_first), BLANK_SOURCE),
            verify(bytes(wrong_last), BLANK_SOURCE),
        } == {TOOL.DENY_PARTITION_DIGEST},
        "wrong same-size partition artifacts must share one sanitized denial",
    )


def test_identities_are_positive_bounded_and_read_before_bytes() -> None:
    invalid = (None, 0, -1, False, "1", TOOL.MAX_IDENTITY + 1)
    for value in invalid:
        require(
            TOOL.verify_streams(UnreadableStream(), UnreadableStream(), value, 1)
            == TOOL.DENY_INVALID_IDENTITY,
            "invalid operation identity must deny before reads",
        )
        require(
            TOOL.verify_streams(UnreadableStream(), UnreadableStream(), 1, value)
            == TOOL.DENY_INVALID_IDENTITY,
            "invalid evidence-set identity must deny before reads",
        )


def test_read_failures_and_cli_output_are_redacted_json() -> None:
    outcome = TOOL.verify_streams(FailingStream(), FailingStream(), 987654, 456789)
    require(outcome == TOOL.DENY_READ_FAILURE,
            "read exceptions must collapse to one fixed denial")

    private_path = r"C:\private\unit-identifier\COM77\secret.bin"
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        exit_code = TOOL.main([
            "--partition-table", private_path,
            "--source-region", private_path,
            "--operation-id", "987654",
            "--evidence-set-id", "456789",
        ])
    rendered = output.getvalue().strip()
    require(exit_code != 0, "denial must return nonzero")
    require(json.loads(rendered) == {
        "schema": TOOL.OUTPUT_SCHEMA,
        "outcome": TOOL.DENY_READ_FAILURE,
    }, "CLI must emit exactly one fixed-shape JSON object")
    for forbidden in (private_path, "COM77", "987654", "456789", "secret"):
        require(forbidden not in rendered, "CLI output leaked private input")

    original = TOOL.verify_files
    try:
        TOOL.verify_files = lambda *_: TOOL.SATISFIED
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exit_code = TOOL.main([
                "--evidence-set-id", "2",
                "--partition-table", "partition",
                "--operation-id", "1",
                "--source-region", "source",
            ])
    finally:
        TOOL.verify_files = original
    require(exit_code == 0 and json.loads(output.getvalue()) == {
        "schema": TOOL.OUTPUT_SCHEMA,
        "outcome": TOOL.SATISFIED,
    }, "satisfied CLI result must be JSON and exit zero")


def test_cli_rejects_malformed_and_zero_identity_without_echo() -> None:
    cases = (
        (["--partition-table", "private-value"], TOOL.DENY_INVALID_INPUT),
        ([
            "--partition-table", "private-value",
            "--source-region", "private-value",
            "--operation-id", "0",
            "--evidence-set-id", "2",
        ], TOOL.DENY_INVALID_IDENTITY),
    )
    for arguments, expected in cases:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            exit_code = TOOL.main(arguments)
        rendered = output.getvalue().strip()
        require(exit_code != 0 and json.loads(rendered) == {
            "schema": TOOL.OUTPUT_SCHEMA,
            "outcome": expected,
        }, "invalid CLI input must return fixed sanitized JSON")
        require("private-value" not in rendered,
                "invalid CLI output must not echo rejected input")


def test_tool_has_only_read_only_file_io_and_no_device_api() -> None:
    source = TOOL_PATH.read_text(encoding="utf-8")
    tree = ast.parse(source)
    imported = {
        alias.name.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, (ast.Import, ast.ImportFrom))
        for alias in node.names
    }
    require(not imported.intersection({"serial", "socket", "subprocess", "esptool"}),
            "verifier must expose no device or process API")
    open_calls = [
        node for node in ast.walk(tree)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and
        node.func.id == "open"
    ]
    require(len(open_calls) == 2, "verifier must open only its two input files")
    for call in open_calls:
        require(len(call.args) >= 2 and isinstance(call.args[1], ast.Constant) and
                call.args[1].value == "rb",
                "every verifier file open must be binary read-only")


def main() -> int:
    tests = (
        test_exact_success_is_streamed_and_nonauthorizing,
        test_exact_sizes_are_required,
        test_non_ff_positions_share_one_sanitized_outcome,
        test_wrong_partition_has_one_fixed_digest_denial,
        test_identities_are_positive_bounded_and_read_before_bytes,
        test_read_failures_and_cli_output_are_redacted_json,
        test_cli_rejects_malformed_and_zero_identity_without_echo,
        test_tool_has_only_read_only_file_io_and_no_device_api,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} protected-storage transition evidence groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
