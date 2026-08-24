#!/usr/bin/env python3
"""Focused adversarial tests for the host-only OT-123 Monocypher frame validator."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "ot123_monocypher_frames.py"
PREFIX = b"OTCBXRF2 "
OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
)
PHASES = ("cold", "warm")
COMMON = {
    "schema": "OTCBXRF2",
    "version": 2,
    "scope": "candidate_local_v2",
    "candidate_id": "monocypher",
    "phase2_complete": False,
}


def _statistics(values: list[int]) -> tuple[int, int, int, int]:
    ordered = sorted(values)
    return (
        ordered[0],
        (ordered[49] + ordered[50]) // 2,
        ordered[94],
        ordered[-1],
    )


def _valid_records() -> list[dict[str, object]]:
    records: list[dict[str, object]] = [
        {
            **COMMON,
            "record_kind": "header",
            "operations_required": 5,
            "repetitions_cold": 100,
            "repetitions_warm": 100,
            "cold_conditioning": "32k_data_sweep",
            "radio_used": False,
            "candidate_selected": False,
        },
        {
            **COMMON,
            "record_kind": "gate",
            "gate": "primitive_vectors_and_negative_cases",
            "outcome": "pass",
        },
    ]
    for operation_index, operation in enumerate(OPERATIONS):
        for phase_index, phase in enumerate(PHASES):
            values = [
                1000 * (operation_index + 1)
                + 200 * phase_index
                + ((i * 37) % 101)
                for i in range(100)
            ]
            for iteration, duration in enumerate(values):
                records.append(
                    {
                        **COMMON,
                        "record_kind": "sample",
                        "operation": operation,
                        "phase": phase,
                        "iteration": iteration,
                        "duration_us": duration,
                        "outcome": "pass",
                    }
                )
            minimum, median, p95, maximum = _statistics(values)
            records.append(
                {
                    **COMMON,
                    "record_kind": "operation_summary",
                    "operation": operation,
                    "phase": phase,
                    "min_us": minimum,
                    "median_us": median,
                    "p95_us": p95,
                    "max_us": maximum,
                    "outcome": "pass",
                }
            )
    records.append(
        {
            **COMMON,
            "record_kind": "runtime_resources",
            "heap_domain": "internal_8bit",
            "heap_start_free_bytes": 100000,
            "heap_min_free_bytes": 98000,
            "peak_dynamic_ram_bytes": 2000,
            "stack_allocation_bytes": 8192,
            "stack_high_water_free_bytes": 4096,
            "max_stack_used_bytes": 4096,
            "watchdog_resets": 0,
            "watchdog_measurement": "uninterrupted_terminal_frame",
        }
    )
    records.append(
        {
            **COMMON,
            "record_kind": "local_complete",
            "operations_completed": 5,
            "operations_required": 5,
            "outcome": "pass",
            "radio_used": False,
            "candidate_selected": False,
        }
    )
    return records


def _encode(records: list[dict[str, object]]) -> bytes:
    return b"".join(
        PREFIX
        + json.dumps(record, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        + b"\n"
        for record in records
    )


def _run_bytes(payload: bytes) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="ot121-frame-test-") as directory:
        capture = Path(directory) / "capture.txt"
        capture.write_bytes(payload)
        return subprocess.run(
            [sys.executable, str(TOOL), str(capture)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )


def _must_accept(records: list[dict[str, object]]) -> dict[str, object]:
    completed = _run_bytes(_encode(records))
    assert completed.returncode == 0, completed.stderr
    assert "Traceback" not in completed.stderr
    return json.loads(completed.stdout)


def _must_reject_payload(payload: bytes, *, hidden: str | None = None) -> None:
    completed = _run_bytes(payload)
    assert completed.returncode == 2, (
        completed.returncode,
        completed.stdout,
        completed.stderr,
    )
    assert completed.stdout == ""
    assert completed.stderr.startswith("ERROR: ")
    assert "Traceback" not in completed.stderr
    if hidden is not None:
        assert hidden not in completed.stderr


def _must_reject(
    records: list[dict[str, object]], *, hidden: str | None = None
) -> None:
    _must_reject_payload(_encode(records), hidden=hidden)


def _find(
    records: list[dict[str, object]], kind: str, **values: object
) -> dict[str, object]:
    for record in records:
        if record.get("record_kind") == kind and all(
            record.get(key) == value for key, value in values.items()
        ):
            return record
    raise AssertionError((kind, values))


def test_accepts_exact_local_capture() -> None:
    result = _must_accept(_valid_records())
    assert result["candidate_id"] == "monocypher"
    assert result["candidate_role"] == "comparison"
    assert result["selection_eligible"] is False
    assert result["unavailable_operations"] == [
        "sha256", "hkdf_sha256", "noise_xk_handshake"
    ]
    assert result["scope"] == "candidate_local_v2"
    assert result["operations_required"] == 5
    assert result["operations_completed"] == 5
    assert result["cold_sample_count"] == 500
    assert result["warm_sample_count"] == 500
    assert result["summary_count"] == 10
    assert result["phase2_complete"] is False
    assert result["runtime_resources"]["heap_domain"] == "internal_8bit"
    assert (
        result["runtime_resources"]["watchdog_measurement"]
        == "uninterrupted_terminal_frame"
    )


def test_framing_and_json_are_strict() -> None:
    valid = _encode(_valid_records())
    _must_reject_payload(b"BADFRAME " + valid.split(b" ", 1)[1])
    _must_reject_payload(valid[:-1])
    _must_reject_payload(valid.replace(b"\n", b"\r\n", 1))
    _must_reject_payload(valid + b"\n")
    _must_reject_payload(
        valid.replace(b'"version":2', b'"version":2,"version":2', 1)
    )
    _must_reject_payload(
        valid.replace(b'"schema":"OTCBXRF2"', b'"schema": "OTCBXRF2"', 1)
    )
    _must_reject_payload(
        valid.replace(b'"record_kind":"header"', b'"record_kind":header', 1)
    )
    _must_reject_payload(
        valid.replace(
            b'"candidate_id":"monocypher"',
            b'"candidate_id":"libsodi\xffum"',
            1,
        )
    )
    _must_reject_payload(valid + b"OTCBXRF2 " + (b"x" * 3000) + b"\n")


def test_header_is_exact_and_local_only() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("scope", "phase_two"),
        ("operations_required", 6),
        ("operations_required", True),
        ("phase2_complete", True),
        ("phase2_complete", 0),
        ("candidate_id", "espressif_libsodium"),
        ("repetitions_cold", 99),
        ("repetitions_warm", 101),
        ("cold_conditioning", "none"),
        ("radio_used", True),
        ("candidate_selected", True),
    )
    for key, value in mutations:
        records = _valid_records()
        records[0][key] = value
        _must_reject(records)
    for extra_key, extra_value in (
        ("extra", False),
        ("result_contract", "OTCBXR1"),
        ("required_operations", 7),
    ):
        records = _valid_records()
        records[0][extra_key] = extra_value
        _must_reject(records)


def test_gates_require_exact_common_fields_order_and_pass() -> None:
    records = _valid_records()
    records[1], records[2] = records[2], records[1]
    _must_reject(records)
    for index in (1, 2):
        records = _valid_records()
        records[index]["outcome"] = "fail"
        _must_reject(records)
    for key, value in (
        ("gate", "radio_ready"),
        ("scope", "phase_two"),
        ("candidate_id", "libsodium"),
        ("phase2_complete", True),
    ):
        records = _valid_records()
        records[1][key] = value
        _must_reject(records)
    records = _valid_records()
    records[1]["extra"] = 1
    _must_reject(records)


def test_samples_require_exact_order_count_values_and_common_fields() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("iteration", 1),
        ("duration_us", -1),
        ("duration_us", 0),
        ("duration_us", True),
        ("duration_us", 1.5),
        ("duration_us", 1 << 63),
        ("outcome", "fail"),
        ("phase", "hot"),
        ("operation", "aes"),
        ("scope", "phase_two"),
        ("candidate_id", "libsodium"),
        ("phase2_complete", True),
    )
    for key, value in mutations:
        records = _valid_records()
        first = _find(
            records,
            "sample",
            operation=OPERATIONS[0],
            phase="cold",
            iteration=0,
        )
        first[key] = value
        _must_reject(records)
    records = _valid_records()
    del records[3]
    _must_reject(records)
    records = _valid_records()
    records.insert(3, copy.deepcopy(records[3]))
    _must_reject(records)
    records = _valid_records()
    records[3]["extra"] = 0
    _must_reject(records)


def test_summaries_are_recomputed_exactly_without_samples_field() -> None:
    for key in ("min_us", "median_us", "p95_us", "max_us"):
        records = _valid_records()
        summary = _find(
            records,
            "operation_summary",
            operation=OPERATIONS[0],
            phase="cold",
        )
        summary[key] = int(summary[key]) + 1
        _must_reject(records)
    for key, value in (
        ("outcome", "fail"),
        ("phase", "warm"),
        ("operation", "x25519"),
        ("scope", "phase_two"),
        ("phase2_complete", True),
    ):
        records = _valid_records()
        summary = _find(
            records,
            "operation_summary",
            operation=OPERATIONS[0],
            phase="cold",
        )
        summary[key] = value
        _must_reject(records)
    for extra_key, extra_value in (("samples", 100), ("extra", 0)):
        records = _valid_records()
        summary = _find(
            records,
            "operation_summary",
            operation=OPERATIONS[0],
            phase="cold",
        )
        summary[extra_key] = extra_value
        _must_reject(records)


def test_runtime_resources_are_exact_derived_and_zero_watchdog() -> None:
    for key, value in (
        ("heap_domain", "all_memory"),
        ("heap_min_free_bytes", 100001),
        ("peak_dynamic_ram_bytes", 1999),
        ("stack_allocation_bytes", 4096),
        ("stack_high_water_free_bytes", 8193),
        ("max_stack_used_bytes", 4095),
        ("watchdog_resets", 1),
        ("watchdog_measurement", "reset_reason_guess"),
    ):
        records = _valid_records()
        resource = _find(records, "runtime_resources")
        resource[key] = value
        _must_reject(records)

    records = _valid_records()
    resource = _find(records, "runtime_resources")
    resource["extra"] = 0
    _must_reject(records)

    records = _valid_records()
    resource = _find(records, "runtime_resources")
    records.remove(resource)
    _must_reject(records)

def test_terminal_is_exact_local_completion_not_phase_two() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("record_kind", "complete"),
        ("scope", "phase2"),
        ("operations_required", 6),
        ("operations_completed", 6),
        ("outcome", "fail"),
        ("phase2_complete", True),
        ("radio_used", True),
        ("candidate_selected", True),
        ("candidate_id", "libsodium"),
    )
    for key, value in mutations:
        records = _valid_records()
        records[-1][key] = value
        _must_reject(records)
    records = _valid_records()
    records[-1]["extra"] = False
    _must_reject(records)
    records = _valid_records()
    records.append(copy.deepcopy(records[-1]))
    _must_reject(records)


def test_private_text_cli_errors_and_tool_surface_are_safe() -> None:
    private_value = "C:" + chr(92) + "Users" + chr(92) + "Alice" + chr(92) + "capture.txt"
    records = _valid_records()
    records[0]["operator_path"] = private_value
    completed = _run_bytes(_encode(records))
    assert completed.returncode == 2
    assert completed.stdout == ""
    assert completed.stderr.startswith("ERROR: ")
    assert "Traceback" not in completed.stderr
    assert "Alice" not in completed.stderr
    assert private_value not in completed.stderr

    missing = ROOT / "tests" / "host" / "private-does-not-exist.capture"
    completed = subprocess.run(
        [sys.executable, str(TOOL), str(missing)],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
        check=False,
    )
    assert completed.returncode == 2
    assert completed.stdout == ""
    assert "Traceback" not in completed.stderr
    assert str(missing) not in completed.stderr

    source = TOOL.read_text(encoding="utf-8").lower()
    for forbidden in (
        "import serial",
        "from serial",
        "pyserial",
        "esptool",
        "idf.py",
        "socket",
        "requests",
    ):
        assert forbidden not in source


def main() -> int:
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
    print(f"PASS {len(tests)} OT-123 Monocypher frame test groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
