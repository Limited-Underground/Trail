"""Composed byte runtime, source admission and restoration; no physical I/O."""
import contextlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_receipt_observation_execution as observed

spec = importlib.util.spec_from_file_location("_observation_composed_fixture",
    ROOT / "tests/host/noise_xk_contained_execution_tests.py")
prior = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = prior
spec.loader.exec_module(prior)


@contextlib.contextmanager
def fixture(partial=False):
    with mock.patch.object(prior, "contained", observed):
        with prior.Fixture(partial=partial) as x:
            x.snapshot["sources"] = observed.freeze_sources(x.f.root)
            yield x


class Tests(unittest.TestCase):
    def test_complete_run_observations_survive_reopen_and_close(self):
        with fixture() as x:
            result = x.execute()
            self.assertTrue(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["radio_result"]["totals"]["fragments"], 14)
            self.assertEqual(result["radio_result"]["totals"]["radio_payload_wire_bytes"], 736)
            snapshot = x.session.diagnostic_snapshot()
            for role in ("A", "B"):
                lifecycle = x.session._observer._endpoints[role]
                self.assertIsNone(lifecycle._endpoint)
                rows = snapshot["roles"][role]["receipt_observations"]
                self.assertGreater(len(rows), 14)
                self.assertLessEqual(len(rows), 128)
                self.assertTrue(any(r["kind"] == "TX_DONE" and r["outcome"] == "accepted" for r in rows))
                self.assertEqual(snapshot["roles"][role]["observation_archive_failures"], 0)
                rows[0]["read_bytes"] = -1
                self.assertNotEqual(lifecycle.diagnostic_snapshot()["receipt_observations"][0]["read_bytes"], -1)
                archived = lifecycle.diagnostic_snapshot()
                lifecycle.close()
                self.assertEqual(lifecycle.diagnostic_snapshot(), archived)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))
            text = json.dumps(snapshot)
            for private in ("session_hash", "anonymous-A", "anonymous-B", "stale-old-handle-tail"):
                self.assertNotIn(private, text)

    def test_absent_and_unterminated_m3_distinguished_after_restoration(self):
        for tail in (b"", b"OT153 TX_DONE incomplete"):
            with self.subTest(tail_length=len(tail)), fixture() as x:
                original = prior.base.base.Serial.readline
                def read(handle, size):
                    if handle.board.queue and not handle.fragments:
                        receipt = handle.board.queue[0]
                        if receipt.kind == "TX_DONE" and receipt.fields["message"] == "m3":
                            handle.board.queue.popleft()
                            handle.board.host_clock.now += 0.01
                            return tail
                    return original(handle, size)
                with mock.patch.object(prior.base.base.Serial, "readline", read):
                    with self.assertRaises(prior.base.prior.CoordinatorError):
                        x.execute()
                role = x.session.diagnostic_snapshot()["roles"]["A"]
                row = next(r for r in role["receipt_observations"] if r["outcome"] == "receipt_timeout")
                self.assertEqual(row["kind"], "TX_DONE")
                self.assertEqual(row["pending_bytes"], len(tail))
                self.assertEqual(row["pending_state"], "unterminated_bytes" if tail else "empty")
                self.assertGreater(row["empty_reads"], 0)
                self.assertEqual(role["checkpoints"][-1]["kind"], "RX_REARM_RETURN")
                self.assertFalse(any(role["parser_misses"].values()))
                self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
                self.assertFalse(x.f.backend._leases)

    def test_both_successor_sources_required_before_io(self):
        for relative in observed.SOURCE_PATHS[-2:]:
            with fixture() as x:
                (x.f.root / relative).write_bytes(b"tampered")
                with self.assertRaises(observed.BundleError): x.execute()
                self.assertEqual(x.f.events, [])
                self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())

    def test_old_manifest_cannot_admit_new_runtime(self):
        with fixture() as x:
            x.snapshot["sources"] = observed.contained.freeze_sources(x.f.root)
            with self.assertRaises(observed.BundleError): x.execute()
            self.assertEqual(x.f.events, [])

    def test_recovery_requires_successor_sources_and_preserves_both_originals(self):
        with fixture() as x:
            x.f.fail_ready, x.f.fail_restore = "A", "A"
            with self.assertRaises(prior.base.prior.CoordinatorError): x.execute()
            source = x.f.root / observed.SOURCE_PATHS[-1]
            raw = source.read_bytes()
            source.write_bytes(b"tampered")
            before = list(x.f.events)
            with self.assertRaises(observed.BundleError): x.recover()
            self.assertEqual(x.f.events, before)
            source.write_bytes(raw)
            x.f.benchmark.unlink()
            x.f.fail_restore = None
            self.assertTrue(x.recover()["restoration_complete"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)

    def test_consumed_grant_and_namespaces_remain_isolated(self):
        first, second = observed.ReceiptObservationSession(3), observed.ReceiptObservationSession(4)
        old = observed.contained.ContainedExecutionSession(3)
        self.assertEqual(len({s.coordinator.JOURNAL_NAME for s in (first, second, old)}), 3)
        self.assertEqual(len(observed.SOURCE_PATHS), 42)
        self.assertEqual(len(observed.contained.SOURCE_PATHS), 40)
        with fixture() as x:
            x.execute()
            before = list(x.f.events)
            with self.assertRaises(prior.base.prior.CoordinatorError): x.execute()
            self.assertEqual(x.f.events, before)

    def test_failed_reopen_preserves_observations_and_restores_both_roles(self):
        with fixture() as x:
            x.f.boards[x.f.routes[0]].fail_reopen = True
            with self.assertRaises(prior.base.prior.CoordinatorError): x.execute()
            snapshot = x.session.diagnostic_snapshot()["roles"]["A"]
            self.assertTrue(snapshot["receipt_observations"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))

    def test_archive_snapshot_failure_still_restores_both_roles(self):
        with fixture() as x:
            original_open = x.f.backend.open_radio_endpoint
            def open_endpoint(route):
                endpoint = original_open(route)
                original_archive = endpoint._archive_detached
                def archive(detached):
                    if detached is not None and endpoint._endpoint is None:
                        detached.diagnostic_snapshot = mock.Mock(side_effect=RuntimeError("private failure"))
                    return original_archive(detached)
                endpoint._archive_detached = archive
                return endpoint
            x.f.backend.open_radio_endpoint = open_endpoint
            result = x.execute()
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)
            snapshot = x.session.diagnostic_snapshot()
            self.assertNotIn("private failure", json.dumps(snapshot))
            for role in ("A", "B"):
                self.assertEqual(snapshot["roles"][role]["observation_archive_failures"], 1)


if __name__ == "__main__": unittest.main()
