#!/usr/bin/env python3
"""Focused adversarial tests for the OT-149 mbedTLS/PSA frame validator."""

from __future__ import annotations

import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "ot149_mbedtls_psa_frames.py"
SCHEMA = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot121_candidate_benchmarks"
    / "mbedtls-psa-result-frame.schema.json"
)
APP_MAIN = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot149_mbedtls_psa"
    / "common"
    / "app_main.c"
)
PREFIX = b"OTCBXRF2 "
OPERATIONS = (
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
)
PHASES = ("cold", "warm")
GATES = ("psa_crypto_init", "primitive_vectors_and_negative_cases")
COMMON = {
    "schema": "OTCBXRF2",
    "version": 2,
    "scope": "candidate_local_v2",
    "candidate_id": "esp_idf_mbedtls_psa",
    "phase2_complete": False,
}

SPEC = importlib.util.spec_from_file_location("ot149_mbedtls_psa_frames", TOOL)
assert SPEC is not None and SPEC.loader is not None
FRAMES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FRAMES)


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
        }
    ]
    for gate in GATES:
        records.append(
            {
                **COMMON,
                "record_kind": "gate",
                "gate": gate,
                "outcome": "pass",
            }
        )
    for operation_index, operation in enumerate(OPERATIONS):
        for phase_index, phase in enumerate(PHASES):
            values = [
                1000 * operation_index
                + 200 * phase_index
                + ((iteration * 37) % 101)
                for iteration in range(100)
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
    assert len(records) == 1015
    return records


def _encode(records: list[dict[str, object]]) -> bytes:
    return b"".join(
        PREFIX
        + json.dumps(record, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        + b"\n"
        for record in records
    )


def _run_bytes(payload: bytes) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="ot149-frame-test-") as directory:
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
    return FRAMES.parse_capture_bytes(_encode(records))


def _must_reject_payload(payload: bytes, *, hidden: str | None = None) -> None:
    try:
        FRAMES.parse_capture_bytes(payload)
    except FRAMES.FrameError as exc:
        if hidden is not None:
            assert hidden not in str(exc)
        return
    raise AssertionError("malformed capture was accepted")


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


def test_accepts_exact_1015_frame_comparison_capture() -> None:
    result = _must_accept(_valid_records())
    assert result["candidate_id"] == "esp_idf_mbedtls_psa"
    assert result["candidate_role"] == "comparison"
    assert result["selection_eligible"] is False
    assert result["unavailable_operations"] == [
        "ed25519_sign",
        "ed25519_verify",
        "noise_xk_handshake",
    ]
    assert result["operations_required"] == 5
    assert result["operations_completed"] == 5
    assert result["cold_sample_count"] == 500
    assert result["warm_sample_count"] == 500
    assert result["summary_count"] == 10
    assert result["gate_count"] == 2
    assert result["frame_count"] == 1015
    assert result["phase2_complete"] is False
    assert result["radio_used"] is False
    assert result["candidate_selected"] is False
    assert result["resource_delta_admitted"] is False
    assert "start_ready_validated" not in result


def test_framing_utf8_bom_duplicates_and_json_are_strict() -> None:
    valid = _encode(_valid_records())
    _must_reject_payload(b"BADFRAME " + valid.split(b" ", 1)[1])
    _must_reject_payload(valid[:-1])
    _must_reject_payload(valid.replace(b"\n", b"\r\n", 1))
    _must_reject_payload(valid + b"\n")
    _must_reject_payload(b"\xef\xbb\xbf" + valid)
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
            b'"candidate_id":"esp_idf_mbedtls_psa"',
            b'"candidate_id":"mbedtls_\xffpsa"',
            1,
        )
    )
    _must_reject_payload(valid + b"OTCBXRF2 " + (b"x" * 3000) + b"\n")


def test_header_is_exact_and_all_completion_claims_are_false() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("scope", "phase_two"),
        ("operations_required", 8),
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
        ("resource_delta_admitted", True),
        ("phase_two_admitted", True),
        ("result_contract", "OTCBXR1"),
    ):
        records = _valid_records()
        records[0][extra_key] = extra_value
        _must_reject(records)


def test_two_gates_require_exact_order_identity_and_pass() -> None:
    records = _valid_records()
    records[1], records[2] = records[2], records[1]
    _must_reject(records)
    for index in (1, 2):
        records = _valid_records()
        records[index]["outcome"] = "fail"
        _must_reject(records)
    for key, value in (
        ("gate", "sodium_init"),
        ("scope", "phase_two"),
        ("candidate_id", "mbedtls"),
        ("phase2_complete", True),
    ):
        records = _valid_records()
        records[1][key] = value
        _must_reject(records)
    records = _valid_records()
    records.insert(2, copy.deepcopy(records[1]))
    _must_reject(records)


def test_samples_require_exact_operation_phase_iteration_and_count() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("iteration", 1),
        ("duration_us", -1),
        ("duration_us", True),
        ("duration_us", 1.5),
        ("duration_us", 1 << 63),
        ("outcome", "fail"),
        ("phase", "hot"),
        ("operation", "ed25519_sign"),
        ("scope", "phase_two"),
        ("candidate_id", "mbedtls"),
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
    del records[4]
    _must_reject(records)
    records = _valid_records()
    records.insert(4, copy.deepcopy(records[3]))
    _must_reject(records)
    records = _valid_records()
    records[3]["extra"] = 0
    _must_reject(records)


def test_summaries_are_recomputed_exactly() -> None:
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
        ("operation", "sha256"),
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
    records = _valid_records()
    summary = _find(records, "operation_summary", operation="x25519", phase="cold")
    summary["samples"] = 100
    _must_reject(records)


def test_runtime_resources_are_exact_derived_and_not_admission() -> None:
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
    resource["resource_delta_admitted"] = True
    _must_reject(records)
    records = _valid_records()
    records.remove(_find(records, "runtime_resources"))
    _must_reject(records)


def test_terminal_is_exact_local_completion_only() -> None:
    mutations: tuple[tuple[str, object], ...] = (
        ("record_kind", "complete"),
        ("scope", "phase2"),
        ("operations_required", 8),
        ("operations_completed", 4),
        ("outcome", "fail"),
        ("phase2_complete", True),
        ("radio_used", True),
        ("candidate_selected", True),
        ("candidate_id", "mbedtls"),
    )
    for key, value in mutations:
        records = _valid_records()
        records[-1][key] = value
        _must_reject(records)
    records = _valid_records()
    records[-1]["resource_delta_admitted"] = True
    _must_reject(records)
    records = _valid_records()
    records.append(copy.deepcopy(records[-1]))
    _must_reject(records)


def test_start_ready_transport_is_separate_and_capture_fails_closed() -> None:
    valid = _encode(_valid_records())
    _must_reject_payload(b"OTCBX/2 READY\n" + valid)
    _must_reject_payload(b"OTCBX/2 START\n" + valid)
    source = APP_MAIN.read_text(encoding="utf-8")
    wait_index = source.index("wait_for_start();")
    header_index = source.index("ot121_frame_header();")
    assert wait_index < header_index
    assert "OT129_CONTROL_READY" in source
    assert "ot129_control_feed" in source


def test_schema_binds_exact_identity_operations_and_false_claims() -> None:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert len(schema["oneOf"]) == 6
    by_kind = {
        branch["properties"]["record_kind"]["const"]: branch
        for branch in schema["oneOf"]
    }
    assert set(by_kind) == {
        "header",
        "gate",
        "sample",
        "operation_summary",
        "runtime_resources",
        "local_complete",
    }
    for branch in by_kind.values():
        assert branch["additionalProperties"] is False
        assert branch["properties"]["candidate_id"]["const"] == (
            "esp_idf_mbedtls_psa"
        )
        assert branch["properties"]["phase2_complete"]["const"] is False
    assert by_kind["sample"]["properties"]["operation"]["enum"] == list(OPERATIONS)
    assert by_kind["gate"]["properties"]["gate"]["enum"] == list(GATES)
    assert by_kind["header"]["properties"]["radio_used"]["const"] is False
    assert by_kind["header"]["properties"]["candidate_selected"]["const"] is False


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
    print(f"PASS {len(tests)} OT-149 mbedTLS/PSA frame test groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
