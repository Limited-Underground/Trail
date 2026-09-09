"""Synthetic host transport faults; no hardware or physical root-cause claim."""
from pathlib import Path
import json
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from noise_xk_contained_endpoint import ContainedReceiptEndpoint
from noise_xk_receipt_observation_endpoint import ReceiptObservationEndpoint, _ObservedReads, _add
from noise_xk_buffered_receipt_endpoint import frozen_adapter, _MAX_BYTES, _MAX_READS


# Accepted generated printf field order; all identifiers are synthetic zeros.
PREFIX = "I (1099) ot153_noise_radio: OT153 "
IDENTITY = "session_hash=" + "0"*16 + " attempt_hash=" + "0"*16 + " role=I scenario=baseline message=m3 "
START = (PREFIX + "TX_START " + IDENTITY + "wire=64 payload_sha256=" + "0"*64 + " start_us=966604\n").encode()
TX = (PREFIX + "TX_RETURN schema=OTNXTXDIAG1 role=I scenario=baseline message=m3 result=0 start_us=966604 done_us=1092611 measured_us=126007\n").encode()
RX = (PREFIX + "RX_REARM_RETURN schema=OTNXTXDIAG1 role=I scenario=baseline message=m3 result=0 attempted=yes start_us=1093073 done_us=1098992 measured_us=5919\n").encode()
DONE = (PREFIX + "TX_DONE " + IDENTITY + "result=0 start_us=966604 done_us=1092611 measured_us=126007 wire=64 payload_sha256=" + "0"*64 + " rx_restart=0 permit_consumed=yes\n").encode()
STATUS = b"OT153 STATUS accepted=yes\n"


class Serial:
    def __init__(self, chunks, step=0.05):
        self.chunks, self.step, self.now = list(chunks), step, 0.0
        self.sizes, self.clock_calls, self.closed = [], 0, False
    def readline(self, size):
        self.sizes.append(size)
        self.now += self.step
        raw = self.chunks.pop(0) if self.chunks else b""
        if isinstance(raw, BaseException):
            raise raw
        return raw
    def clock(self):
        self.clock_calls += 1
        return self.now
    def close(self): self.closed = True


def run(chunks, cls=ReceiptObservationEndpoint):
    serial = Serial([START, TX, RX] + chunks)
    ep = cls(serial, monotonic=serial.clock)
    ep.expect("TX_START", 5000)
    try:
        value = ep.expect("TX_DONE", 5000)
        outcome = ("accepted", value.kind, value.fields)
    except frozen_adapter.AdapterError as exc:
        outcome = (str(exc),)
    return ep, serial, outcome


class Tests(unittest.TestCase):
    def test_all_318_fragment_splits_accepted(self):
        self.assertEqual(len(DONE), 319)
        for cut in range(1, len(DONE)):
            ep, _, outcome = run([DONE[:cut], DONE[cut:]])
            self.assertEqual(outcome[0], "accepted")
            row = ep.diagnostic_snapshot()["receipt_observations"][-1]
            self.assertEqual((row["read_calls"], row["read_bytes"], row["parsed_lines"]), (4, len(TX+RX+DONE), 3))
            self.assertEqual((row["pending_bytes"], row["pending_state"]), (0, "empty"))

    def test_all_319_proper_prefixes_preserved_before_clear(self):
        for cut in range(len(DONE)):
            ep, _, outcome = run([DONE[:cut]])
            self.assertEqual(outcome, ("receipt_timeout",))
            snapshot = ep.diagnostic_snapshot()
            row = snapshot["receipt_observations"][-1]
            self.assertEqual(row["pending_bytes"], cut)
            self.assertEqual(row["pending_state"], "unterminated_bytes" if cut else "empty")
            self.assertEqual(row["read_bytes"], len(TX+RX)+cut)
            self.assertGreater(row["empty_reads"], 0)
            self.assertEqual(len(ep._pending), 0)
            self.assertEqual(len(snapshot["checkpoints"]), 2)
            self.assertEqual(sum(snapshot["parser_misses"].values()), 0)

    def test_missing_lf_in_64_byte_chunks(self):
        ep, _, outcome = run([DONE[:-1][i:i+64] for i in range(0, len(DONE)-1, 64)])
        self.assertEqual(outcome, ("receipt_timeout",))
        self.assertEqual(ep.diagnostic_snapshot()["receipt_observations"][-1]["pending_bytes"], 318)

    def test_predecessor_parity_for_faults_and_clock_calls(self):
        cases = [[DONE], [DONE[:-1]], [], [b"private unparsed\n"], [b"\xff private\n"],
                 [b"OT153 STATUS x=\n"], [b"\x1bprivate\n"], [bytearray(b"private")],
                 [b"x" * (_MAX_BYTES+1)], [ValueError("private exception")], [b"OT153 TX_DONE result=1\n"]]
        for chunks in cases:
            newer, ns, no = run(chunks)
            older, os, oo = run(chunks, ContainedReceiptEndpoint)
            self.assertEqual(no, oo)
            self.assertEqual((ns.sizes, ns.clock_calls, ns.now), (os.sizes, os.clock_calls, os.now))
            snapshot = newer.diagnostic_snapshot()
            snapshot.pop("receipt_observations")
            snapshot["schema"] = "noise-xk-contained-endpoint-diagnostics-v1"
            self.assertEqual(snapshot, older.diagnostic_snapshot())
            self.assertIs(newer._serial, ns)

    def test_late_read_bytes_observed_but_not_parsed(self):
        serial = Serial([STATUS], step=5)
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_timeout"):
            ep.expect("STATUS", 5000)
        row = ep.diagnostic_snapshot()["receipt_observations"][-1]
        self.assertEqual((row["read_bytes"], row["last_read_bytes"], row["pending_bytes"], row["parsed_lines"]), (len(STATUS), len(STATUS), 0, 0))

    def test_buffered_next_line_and_no_read_acceptance(self):
        serial = Serial([STATUS+STATUS])
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        ep.expect("STATUS", 5000)
        row = ep.diagnostic_snapshot()["receipt_observations"][-1]
        self.assertEqual((row["pending_bytes"], row["pending_state"]), (len(STATUS), "complete_line_buffered"))
        ep.expect("STATUS", 5000)
        row = ep.diagnostic_snapshot()["receipt_observations"][-1]
        self.assertEqual((row["read_calls"], row["read_bytes"], row["parsed_lines"]), (0, 0, 1))

    def test_original_read_budget_and_size_failure(self):
        serial = Serial([], step=0)
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_read_budget"):
            ep.expect("STATUS", 5000)
        self.assertEqual(ep.diagnostic_snapshot()["receipt_observations"][-1]["read_calls"], _MAX_READS)
        serial = Serial([b"x"*_MAX_BYTES, b"x"])
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_size_invalid"):
            ep.expect("STATUS", 5000)
        self.assertEqual(ep.diagnostic_snapshot()["receipt_observations"][-1]["pending_bytes"], _MAX_BYTES+1)

    def test_private_values_and_invalid_request_never_retained(self):
        for kind, timeout in [("private secret kind", 5000), ([], 5000), ("STATUS", "secret")]:
            serial = Serial([])
            ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
            with self.assertRaises(frozen_adapter.AdapterError): ep.expect(kind, timeout)
            snapshot = json.dumps(ep.diagnostic_snapshot())
            self.assertNotIn("secret", snapshot)
            self.assertNotIn("private", snapshot)
            if type(kind) is not str or type(timeout) is not int:
                self.assertEqual(serial.sizes, [])
        ep, _, _ = run([ValueError("secret")])
        row = ep.diagnostic_snapshot()["receipt_observations"][-1]
        self.assertEqual(row["read_errors"], 1)
        self.assertNotIn("secret", json.dumps(ep.diagnostic_snapshot()))

    def test_base_exception_object_preserved_and_handle_restored(self):
        error = KeyboardInterrupt("private")
        serial = Serial([error])
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        with self.assertRaises(KeyboardInterrupt) as caught: ep.expect("STATUS", 5000)
        self.assertIs(caught.exception, error)
        self.assertIs(ep._serial, serial)
        row = ep.diagnostic_snapshot()["receipt_observations"][-1]
        self.assertEqual((row["outcome"], row["read_errors"]), ("other_failure", 1))
        ep.close()
        self.assertTrue(serial.closed)

    def test_proxy_forwards_exact_args_return_and_public_attributes(self):
        raw = bytearray(b"synthetic")
        serial = Serial([raw])
        serial.port = "synthetic-port"
        row = dict.fromkeys(("read_calls", "last_read_bytes", "read_errors", "read_bytes", "empty_reads", "invalid_reads"), 0)
        proxy = _ObservedReads(serial, row)
        self.assertEqual(proxy.port, serial.port)
        self.assertIs(proxy.readline(123), raw)
        self.assertEqual(serial.sizes, [123])
        self.assertEqual(row["invalid_reads"], 1)
        proxy.close()
        self.assertTrue(serial.closed)

    def test_query_ready_unobserved_and_unchanged(self):
        challenge = "a" * 32
        ready = ("OT153 READY schema=OTNXREADY1 challenge=" + challenge + " accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no\n").encode()
        class ReadySerial(Serial):
            def write(self, data): self.written = data; return len(data)
            def flush(self): pass
        results = []
        for cls in (ContainedReceiptEndpoint, ReceiptObservationEndpoint):
            serial = ReadySerial([ready])
            ep = cls(serial, monotonic=serial.clock)
            value, receipt = ep.query_ready(challenge_factory=lambda: challenge)
            results.append((value, receipt.fields, serial.sizes, serial.clock_calls, serial.written))
        self.assertEqual(results[0], results[1])
        self.assertEqual(ep.diagnostic_snapshot()["receipt_observations"], [])

    def test_bounded_ring_snapshot_and_saturating_counters(self):
        serial = Serial([STATUS]*140)
        ep = ReceiptObservationEndpoint(serial, monotonic=serial.clock)
        for _ in range(140): ep.expect("STATUS", 5000)
        snapshot = ep.diagnostic_snapshot()
        self.assertEqual(len(snapshot["receipt_observations"]), 128)
        snapshot["receipt_observations"][0]["kind"] = "private"
        self.assertNotIn("private", json.dumps(ep.diagnostic_snapshot()))
        row = {"read_bytes": 65534}
        _add(row, "read_bytes", 999999)
        self.assertEqual(row["read_bytes"], 65535)
        before = ep.diagnostic_snapshot()
        ep.close()
        self.assertEqual(ep.diagnostic_snapshot(), before)


if __name__ == "__main__": unittest.main()
