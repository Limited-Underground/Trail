"""No I/O: actual inherited parser budgets, startup policy and strict checkpoints."""
from pathlib import Path
import json
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from noise_xk_contained_endpoint import ContainedReceiptEndpoint
from noise_xk_buffered_receipt_endpoint import frozen_adapter


def wire(kind, **fields):
    return ("OT153 " + kind + " " + " ".join(k + "=" + str(v) for k, v in fields.items()) + "\n").encode()


IDENTITY = dict(role="I", scenario="baseline", message="m3")
START = wire("TX_START", **IDENTITY, start_us=100)


def tx(**changes):
    fields = dict(schema="OTNXTXDIAG1", **IDENTITY, result=0, start_us=100, done_us=200, measured_us=100)
    fields.update(changes)
    return wire("TX_RETURN", **fields)


def rx(**changes):
    fields = dict(schema="OTNXTXDIAG1", **IDENTITY, result=0, start_us=210, done_us=220, measured_us=10, attempted="yes")
    fields.update(changes)
    return wire("RX_REARM_RETURN", **fields)


def done(**changes):
    fields = dict(**IDENTITY, result=0, start_us=100, done_us=200, measured_us=100, rx_restart=0)
    fields.update(changes)
    return wire("TX_DONE", **fields)


class Serial:
    def __init__(self, chunks, step=0.1):
        self.chunks, self.now, self.step, self.writes = list(chunks), 0.0, step, []
    def readline(self, size):
        self.now += self.step
        return self.chunks.pop(0) if self.chunks else b""
    def write(self, data): self.writes.append(data)
    def flush(self): pass
    def close(self): pass


def endpoint(chunks, step=0.1):
    serial = Serial(chunks, step)
    return ContainedReceiptEndpoint(serial, monotonic=lambda: serial.now), serial


class Tests(unittest.TestCase):
    def test_all_byte_splits_and_retained_receipt_type(self):
        data = tx() + rx() + done()
        for cut in range(1, len(data)):
            ep, _ = endpoint([START, data[:cut], data[cut:]])
            start = ep.expect("TX_START", 5000)
            result = ep.expect("TX_DONE", 5000)
            self.assertIs(type(result), type(start))
            self.assertEqual(result.fields["result"], "0")
            self.assertEqual([r["kind"] for r in ep.diagnostic_snapshot()["checkpoints"]], ["TX_RETURN", "RX_REARM_RETURN"])

    def test_invalid_checkpoint_contracts_fail_closed(self):
        cases = [[rx(), tx(), done()], [tx(), tx(), rx(), done()], [tx(), done()], [done()],
                 [tx(extra="private"), rx(), done()], [tx(role="R"), rx(), done()],
                 [tx(start_us=99), rx(), done()], [tx(result=32768), rx(), done()],
                 [tx(done_us=2**63), rx(), done()], [tx(measured_us=99), rx(), done()],
                 [tx(), rx(attempted="no"), done()], [tx(), rx(start_us=190, measured_us=30), done()],
                 [tx(), rx(), done(result=-1)], [tx(), rx(), done(rx_restart=-1)],
                 [tx().replace(b"result=0", b"result="), rx(), done()]]
        for chunks in cases:
            ep, _ = endpoint([START] + chunks)
            ep.expect("TX_START", 5000)
            with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_sequence_invalid"):
                ep.expect("TX_DONE", 5000)
            with self.assertRaisesRegex(frozen_adapter.AdapterError, "endpoint_failed"):
                ep.expect("STATUS", 5000)

    def test_failure_checkpoint_is_retained_without_claiming_runner_success(self):
        for code in (-705, -2):
            ep, _ = endpoint([START, tx(result=code), rx(result=code, attempted="no"), done(result=code, rx_restart=code)])
            ep.expect("TX_START", 5000)
            self.assertEqual(ep.expect("TX_DONE", 5000).fields["result"], str(code))
            self.assertEqual(ep.diagnostic_snapshot()["checkpoints"][1]["attempted"], "no")
        ep, _ = endpoint([START, tx(result=-2), rx(), done(result=-2)])
        ep.expect("TX_START", 5000)
        self.assertEqual(ep.expect("TX_DONE", 5000).fields["result"], "-2")

    def test_checkpoint_not_accepted_outside_matching_phase(self):
        for kind in ("TX_START", "STATUS", "TX_RETURN"):
            ep, _ = endpoint([tx()])
            with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_sequence_invalid"):
                ep.expect(kind, 5000)
        ep, _ = endpoint([tx()])
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_sequence_invalid"):
            ep.query_ready(challenge_factory=lambda: "a" * 32)

    def test_original_absolute_deadline_not_restarted_by_checkpoint(self):
        ep, serial = endpoint([START], step=0.1)
        ep.expect("TX_START", 5000)
        serial.chunks = [tx(), rx(), done()]
        serial.step = 2
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_timeout"):
            ep.expect("TX_DONE", 5000)
        self.assertEqual(len(ep.diagnostic_snapshot()["checkpoints"]), 2)

    def test_private_parser_noise_counters_only_and_saturation(self):
        ep, _ = endpoint([b"secret raw bytes\n", b"\xff private\n", b"OT153 STATUS secret=\n", wire("STATUS", accepted="yes")])
        ep.expect("STATUS", 5000)
        snapshot = ep.diagnostic_snapshot()
        self.assertEqual(snapshot["parser_misses"], dict(non_ascii=1, non_protocol=1, malformed_protocol=1, framing_invalid=0))
        self.assertNotIn("private", json.dumps(snapshot))
        self.assertNotIn("secret", json.dumps(snapshot))
        ep._parser_counts["non_protocol"] = 65535
        ep._count("non_protocol")
        self.assertEqual(ep.diagnostic_snapshot()["parser_misses"]["non_protocol"], 65535)

    def test_bounded_ring_and_defensive_snapshot(self):
        ep, serial = endpoint([])
        for _ in range(70):
            serial.chunks = [START, tx(), rx(), done()]
            ep.expect("TX_START", 5000)
            ep.expect("TX_DONE", 5000)
        snapshot = ep.diagnostic_snapshot()
        self.assertEqual(len(snapshot["checkpoints"]), 128)
        snapshot["checkpoints"][0]["message"] = "private"
        self.assertEqual(ep.diagnostic_snapshot()["checkpoints"][0]["message"], "m3")

    def test_startup_tolerance_and_fresh_ready_contract_unchanged(self):
        def ready(challenge):
            return wire("READY", schema="OTNXREADY1", challenge=challenge, accepted="yes", stale_selftest="yes", radio_ready="yes", idle="yes", tx="no")
        ep, _ = endpoint([b"OT153 BOOT broken=\n", ready("b" * 32), ready("a" * 32)])
        challenge, receipt = ep.query_ready(challenge_factory=lambda: "a" * 32)
        self.assertEqual(challenge, "a" * 32)
        self.assertEqual(receipt.fields["idle"], "yes")
        ep, _ = endpoint([b"OT153 UNKNOWN broken=\n"])
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "readiness_receipt_invalid"):
            ep.query_ready(challenge_factory=lambda: "a" * 32)


if __name__ == "__main__": unittest.main()
