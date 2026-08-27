#!/usr/bin/env python3
"""Adversarial tests for OT-151 early benchmark-failure recognition."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {name}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


failure = load("ot151_failure", ROOT / "tools/ot151_mbedtls_psa_failure_frames.py")
runner = load("ot151_runner", ROOT / "tools/ot151_mbedtls_psa_protocol_runner.py")
fixtures = load(
    "ot149_frame_fixtures", ROOT / "tests/host/ot149_mbedtls_psa_frame_tests.py"
)

PREFIX = b"OTCBXRF2 "
HEADER = PREFIX + (
    b'{"schema":"OTCBXRF2","version":2,"record_kind":"header",'
    b'"scope":"candidate_local_v2","candidate_id":"esp_idf_mbedtls_psa",'
    b'"operations_required":5,"repetitions_cold":100,"repetitions_warm":100,'
    b'"cold_conditioning":"32k_data_sweep","phase2_complete":false,'
    b'"radio_used":false,"candidate_selected":false}\n'
)


def gate(name: str, outcome: str) -> bytes:
    return PREFIX + (
        b'{"schema":"OTCBXRF2","version":2,"record_kind":"gate",'
        b'"scope":"candidate_local_v2","candidate_id":"esp_idf_mbedtls_psa",'
        + f'"gate":"{name}","outcome":"{outcome}",'.encode("ascii")
        + b'"phase2_complete":false}\n'
    )


INIT_PASS = gate("psa_crypto_init", "pass")
INIT_FAIL = gate("psa_crypto_init", "fail")
VECTOR_FAIL = gate("primitive_vectors_and_negative_cases", "fail")
RESOURCES = PREFIX + (
    b'{"schema":"OTCBXRF2","version":2,"record_kind":"runtime_resources",'
    b'"scope":"candidate_local_v2","candidate_id":"esp_idf_mbedtls_psa",'
    b'"heap_domain":"internal_8bit","heap_start_free_bytes":200000,'
    b'"heap_min_free_bytes":190000,"peak_dynamic_ram_bytes":10000,'
    b'"stack_allocation_bytes":8192,"stack_high_water_free_bytes":8000,'
    b'"max_stack_used_bytes":192,"watchdog_resets":0,'
    b'"watchdog_measurement":"uninterrupted_terminal_frame",'
    b'"phase2_complete":false}\n'
)
COMPLETE_FAIL = PREFIX + (
    b'{"schema":"OTCBXRF2","version":2,"record_kind":"local_complete",'
    b'"scope":"candidate_local_v2","candidate_id":"esp_idf_mbedtls_psa",'
    b'"operations_completed":0,"operations_required":5,"outcome":"fail",'
    b'"phase2_complete":false,"radio_used":false,"candidate_selected":false}\n'
)
VECTOR_FAILURE = HEADER + INIT_PASS + VECTOR_FAIL + RESOURCES + COMPLETE_FAIL


class Clock:
    def __init__(self): self.now = 0.0
    def monotonic(self): return self.now
    def sleep(self, seconds): self.now += seconds


class Endpoint:
    def __init__(self, chunks): self.chunks, self.closed = list(chunks), 0
    def write(self, data): return len(data)
    def flush(self): pass
    def read(self, size):
        del size
        return self.chunks.pop(0) if self.chunks else b""
    def close(self): self.closed += 1


class Provider:
    def __init__(self, endpoint): self.endpoint = endpoint
    def reset(self, private_endpoint): del private_endpoint
    def is_present(self, private_endpoint):
        del private_endpoint
        return True
    def open(self, private_endpoint):
        del private_endpoint
        return self.endpoint


class FailureTranscriptTests(unittest.TestCase):
    def reject(self, raw):
        with self.assertRaises(failure.FailureTranscriptError):
            failure.parse_early_failure_bytes(raw)

    def test_01_exact_ot150_vector_failure_shape(self):
        self.assertEqual(
            [len(HEADER), len(INIT_PASS), len(VECTOR_FAIL), len(RESOURCES), len(COMPLETE_FAIL)],
            [309, 196, 217, 454, 276],
        )
        self.assertEqual(len(runner.READY + VECTOR_FAILURE), 1468)
        result = failure.parse_early_failure_bytes(VECTOR_FAILURE)
        self.assertEqual(result["failed_gate"], "primitive_vectors_and_negative_cases")
        self.assertEqual(result["frame_count"], 5)

    def test_02_initialization_failure_is_only_resource_free_shape(self):
        result = failure.parse_early_failure_bytes(HEADER + INIT_FAIL + COMPLETE_FAIL)
        self.assertEqual(result["failed_gate"], "psa_crypto_init")
        self.reject(HEADER + INIT_FAIL + RESOURCES + COMPLETE_FAIL)

    def test_03_order_gate_and_resource_tampering_fail_closed(self):
        self.reject(HEADER + VECTOR_FAIL + INIT_PASS + RESOURCES + COMPLETE_FAIL)
        self.reject(HEADER + INIT_PASS + gate("wrong_gate", "fail") + RESOURCES + COMPLETE_FAIL)
        self.reject(HEADER + INIT_PASS + VECTOR_FAIL + COMPLETE_FAIL)
        self.reject(HEADER + INIT_PASS + VECTOR_FAIL + RESOURCES + RESOURCES + COMPLETE_FAIL)

    def test_04_noncanonical_early_success_and_trailing_data_fail_closed(self):
        spaced = COMPLETE_FAIL.replace(b',"outcome":"fail"', b', "outcome":"fail"')
        success = COMPLETE_FAIL.replace(b'"outcome":"fail"', b'"outcome":"pass"')
        self.reject(HEADER + INIT_PASS + VECTOR_FAIL + RESOURCES + spaced)
        self.reject(HEADER + INIT_PASS + VECTOR_FAIL + RESOURCES + success)
        self.reject(VECTOR_FAILURE + b"X")
        self.reject(VECTOR_FAILURE + RESOURCES)

    def test_05_runner_reports_failure_immediately_with_exact_diagnostics(self):
        clock, endpoint = Clock(), Endpoint([runner.READY + VECTOR_FAILURE])
        with self.assertRaises(runner.CaptureError) as caught:
            runner.capture_local_primitives(
                Provider(endpoint), "PRIVATE-PORT", monotonic=clock.monotonic,
                sleep=clock.sleep, control_timeout=2.0, capture_timeout=180.0,
                presence_timeout=0.5,
            )
        error = caught.exception
        self.assertEqual(error.code, runner.FailureCode.BENCHMARK_REPORTED_FAILURE)
        self.assertEqual(
            (error.diagnostics.bytes_observed, error.diagnostics.complete_lines,
             error.diagnostics.frame_lines_buffered),
            (1468, 6, 5),
        )
        self.assertLess(clock.now, 1.0)
        self.assertEqual(endpoint.closed, 1)

    def test_06_runner_rejects_missing_resource_and_same_chunk_trailer(self):
        for payload in (
            HEADER + INIT_PASS + VECTOR_FAIL + COMPLETE_FAIL,
            VECTOR_FAILURE + b"trailer",
            VECTOR_FAILURE + RESOURCES,
        ):
            with self.subTest(length=len(payload)):
                clock = Clock()
                with self.assertRaises(runner.CaptureError) as caught:
                    runner.capture_local_primitives(
                        Provider(Endpoint([runner.READY + payload])), "PRIVATE-PORT",
                        monotonic=clock.monotonic, sleep=clock.sleep,
                        control_timeout=2.0, capture_timeout=2.0,
                        presence_timeout=0.5,
                    )
                self.assertEqual(caught.exception.code, runner.FailureCode.FRAME_MALFORMED)

    def test_07_strict_1015_frame_success_parser_is_unchanged(self):
        payload = fixtures._encode(fixtures._valid_records())
        result = runner.capture_local_primitives(
            Provider(Endpoint([runner.READY + payload])), "PRIVATE-PORT",
            control_timeout=2.0, capture_timeout=2.0, presence_timeout=0.5,
        )
        self.assertEqual(result.parsed["frame_count"], 1015)
        self.assertEqual(result.diagnostics.frame_lines_buffered, 1015)


if __name__ == "__main__":
    unittest.main(verbosity=2)
