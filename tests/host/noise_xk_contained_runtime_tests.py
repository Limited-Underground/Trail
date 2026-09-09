"""Contained successor through real byte parser, runner and guarded fake backend."""
import contextlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_contained_runtime as runtime


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


base = load("_contained_runtime_fixture", "tests/host/noise_xk_solicited_execution_tests.py")
endpoint_tests = load("_contained_endpoint_fixture", "tests/host/noise_xk_contained_endpoint_tests.py")


class Fixture:
    def __init__(self, partial=False): self.partial = partial
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.module = runtime.make_backend_module()
        self.stack.enter_context(mock.patch.object(base, "backend", self.module))
        self.f = self.stack.enter_context(base.Fixture())
        self.endpoints = []
        original_open = self.f.backend.open_radio_endpoint
        def open_endpoint(route):
            ep = original_open(route)
            clock = self.f.boards[route].host_clock
            ep._monotonic, ep._sleep = clock.monotonic, clock.sleep
            ep._endpoint._monotonic = clock.monotonic
            self.endpoints.append(ep)
            return ep
        self.f.backend.open_radio_endpoint = open_endpoint
        original = base.Board.receipt
        partial = self.partial
        def receipt(board, kind, fields):
            if kind == "TX_DONE":
                tx = {key: fields[key] for key in ("role", "scenario", "message", "result", "start_us", "done_us", "measured_us")}
                original(board, "TX_RETURN", {"schema": "OTNXTXDIAG1", **tx})
                if partial and fields["message"] == "m3":
                    return
                rx = {**tx, "result": fields["rx_restart"], "start_us": fields["done_us"],
                      "done_us": fields["done_us"], "measured_us": "0", "attempted": "yes"}
                original(board, "RX_REARM_RETURN", {"schema": "OTNXTXDIAG1", **rx})
            original(board, kind, fields)
        self.stack.enter_context(mock.patch.object(base.Board, "receipt", receipt))
        return self
    def __exit__(self, *args): return self.stack.__exit__(*args)


class Handle(endpoint_tests.Serial):
    def __init__(self, chunks=()):
        super().__init__(chunks)
        self.is_open = False
        self.fail_close = False
    def open(self): self.is_open = True
    def close(self):
        if self.fail_close: raise OSError("private device close detail")
        self.is_open = False


class Tests(unittest.TestCase):
    def test_lifecycle_archive_remains_under_backend_lock(self):
        with Fixture() as x:
            ep = x.f.backend.open_radio_endpoint(x.f.routes[0])
            original = ep._archive_detached
            observations = []
            def archive(prior):
                def probe():
                    acquired = x.f.backend._lock.acquire(blocking=False)
                    observations.append(acquired)
                    if acquired:
                        x.f.backend._lock.release()
                thread = threading.Thread(target=probe, daemon=True)
                thread.start()
                thread.join(timeout=1)
                self.assertFalse(thread.is_alive())
                return original(prior)
            ep._archive_detached = archive
            ep.reopen()
            ep.close()
            self.assertEqual(observations, [False, False])

    def test_full_runner_bytes_result_and_restoration_with_checkpoints(self):
        with Fixture() as x:
            result = x.f.execute()
            self.assertTrue(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["radio_result"]["totals"]["fragments"], 14)
            self.assertEqual(result["radio_result"]["totals"]["radio_payload_wire_bytes"], 736)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            rows = [row for ep in x.endpoints for row in ep.diagnostic_snapshot()["checkpoints"]]
            self.assertEqual(len(rows), 28)
            self.assertEqual(sum(r["kind"] == "TX_RETURN" for r in rows), 14)
            self.assertTrue(all(ep._endpoint is None for ep in x.endpoints))
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))
            self.assertTrue(all(len(b.handles) == 2 for b in x.f.boards.values()))
            self.assertNotIn("session_hash", json.dumps(rows))

    def test_partial_checkpoint_survives_timeout_and_cleanup(self):
        with Fixture(partial=True) as x:
            with self.assertRaises(base.c.CoordinatorError): x.f.execute()
            result = json.loads(base.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertFalse(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["failure"]["stage"], "cycle1_baseline")
            rows = x.endpoints[0].diagnostic_snapshot()["checkpoints"]
            self.assertEqual(rows[-1]["kind"], "TX_RETURN")
            self.assertEqual(rows[-1]["message"], "m3")
            self.assertFalse(x.f.backend._leases)

    def test_failed_close_retains_lease_and_diagnostics(self):
        with Fixture() as x:
            ep = x.f.backend.open_radio_endpoint(x.f.routes[0])
            ep._endpoint._receipt_from_line(b"private console detail\n")
            handle = ep._issued_handles[0]
            def fail_close(): raise OSError("private close detail")
            handle.close = fail_close
            with self.assertRaisesRegex(x.module.BackendError, "radio_close_unconfirmed"):
                ep.close()
            self.assertTrue(x.f.backend._leases)
            self.assertEqual(ep.diagnostic_snapshot()["parser_misses"]["non_protocol"], 1)
            with self.assertRaisesRegex(x.module.BackendError, "radio_lease_active"):
                x.f.backend.admit_rom(x.f.routes[0])
            with self.assertRaisesRegex(x.module.BackendError, "radio_close_unconfirmed"):
                ep.close()
            self.assertEqual(ep.diagnostic_snapshot()["parser_misses"]["non_protocol"], 1)

    def test_role_change_still_blocks_command_and_reopen(self):
        with Fixture() as x:
            ep = x.f.backend.open_radio_endpoint(x.f.routes[0])
            old_inventory = x.f.backend._inventory
            x.f.backend._inventory = lambda: []
            for operation in (lambda: ep.write_command("status"), ep.reopen):
                with self.assertRaisesRegex(x.module.BackendError, "passive_identity_mismatch"):
                    operation()
            self.assertEqual(len(ep._issued_handles), 1)
            x.f.backend._inventory = old_inventory
            ep.close()
            self.assertFalse(x.f.backend._leases)

    def test_fresh_handle_reuse_rejected_without_duplicate_archive(self):
        clock = base.serial.Clock()
        first, second = Handle(), Handle()
        handles = iter((first, second))
        ep = runtime.ContainedReconnectableEndpoint("anonymous", lambda: next(handles, first),
            monotonic=clock.monotonic, sleep=clock.sleep)
        ep._endpoint._receipt_from_line(b"private\n")
        ep.reopen()
        ep._endpoint._receipt_from_line(b"private\n")
        with self.assertRaisesRegex(runtime._runtime.lifecycle.AdapterError, "endpoint_reopen_failed"):
            ep.reopen()
        self.assertEqual(ep.diagnostic_snapshot()["parser_misses"]["non_protocol"], 2)
        self.assertEqual(len(ep._issued_handles), 2)
        ep.close()
        self.assertEqual(ep.diagnostic_snapshot()["parser_misses"]["non_protocol"], 2)

    def test_bounded_archive_across_reopens_and_defensive_snapshot(self):
        clock = base.serial.Clock()
        ep = runtime.ContainedReconnectableEndpoint("anonymous", Handle,
            monotonic=clock.monotonic, sleep=clock.sleep)
        for _ in range(70):
            parser = ep._endpoint
            parser._serial.chunks = [endpoint_tests.START, endpoint_tests.tx(), endpoint_tests.rx(), endpoint_tests.done()]
            ep.expect("TX_START", 5000)
            ep.expect("TX_DONE", 5000)
            parser._parser_counts["non_protocol"] = 65535
            ep.reopen()
        ep.close()
        snapshot = ep.diagnostic_snapshot()
        self.assertEqual(len(snapshot["checkpoints"]), 128)
        self.assertEqual(snapshot["parser_misses"]["non_protocol"], 65535)
        snapshot["checkpoints"][0]["role"] = "private"
        self.assertEqual(ep.diagnostic_snapshot()["checkpoints"][0]["role"], "I")


if __name__ == "__main__": unittest.main()
