"""Actual diagnostic observation + unchanged restoration; device I/O is simulated."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import security_policy_execution as execution
import security_policy_sync_restore_observation as stage
import security_policy_input_readback_tests as fixtures
import security_policy_sync_readback_tests as sync_fixtures
from security_policy_execution_tests import Backend


class Tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / ".private").mkdir()
        self.row = {"role": "A", "private_route": "synthetic-route",
                    "private_identity": "1" * 12,
                    "application": b"A" * execution.APP_SPAN,
                    "nvs": fixtures.fixture(), "protected": {
                        name: {"bytes": size, "sha256": execution.sha(bytes([len(name)]) * size)}
                        for name, (_, size) in execution.REGIONS.items()}}
        self.backend = Backend({"roles": [self.row]})
        self.backend.flash["A", execution.APP_OFFSET] = b"D" * execution.APP_SPAN
        self.backend.flash["A", execution.NVS_OFFSET] = fixtures.diagnostic(stage=3)
        grant = execution.Grant("a" * 32, "b" * 64, "execute", None, "c" * 64)
        self.journal = execution.Journal(self.root, grant.attempt, grant.package_sha256,
                                         create=True, grant=grant)
        for event in ("preflight_intent", "preflight_verified", "candidate_write_intent",
                      "candidate_verified", "candidate_boot_intent", "candidate_booted",
                      "serial_open_intent"):
            self.journal.add(event, "A")
        self.journal.add("run_intent", "A", challenge="d" * 32)
        self.backend.leased = "A"
        self.image = sync_fixtures.SYNC_IMAGE
        self.observation = stage.Observation(self.row["nvs"], "A", expected_image=self.image)

    def restore(self):
        return stage.restore_with_observation(self.root, self.backend, self.journal,
                                               self.row, self.observation, observed_image=self.image)

    def assert_restored(self):
        self.assertEqual(self.row["application"], self.backend.flash["A", execution.APP_OFFSET])
        self.assertEqual(self.row["nvs"], self.backend.flash["A", execution.NVS_OFFSET])
        self.assertEqual("original_booted", self.journal.events[-1]["event"])

    def test_capture_before_actual_full_restoration(self):
        result = self.restore()
        self.assertTrue(result["available"])
        self.assertEqual("waiting", result["stage"])
        self.assertEqual({"role", "available", "reason", "stage", "error", "input", "input_status"}, set(result))
        self.assertEqual('not_recorded',result['input_status'])
        self.assert_restored()
        trace = self.backend.trace
        self.assertLess(trace.index(("close", "A", 0)),
                        trace.index(("read", "A", execution.NVS_OFFSET)))
        self.assertLess(trace.index(("read", "A", execution.NVS_OFFSET)),
                        trace.index(("write", "A", execution.APP_OFFSET)))
        captures = list((self.root / ".private").glob("ot201-sync-*.bin"))
        self.assertEqual(1, len(captures))
        self.assertEqual(fixtures.diagnostic(stage=3), captures[0].read_bytes())

    def test_unchanged_strict_result_is_not_synthesized(self):
        self.restore()
        self.assertFalse(any(e["event"] == "evaluation" for e in self.journal.events))

    def test_decoder_failure_restores(self):
        self.backend.flash["A", execution.NVS_OFFSET] = b"?" * execution.NVS_SPAN
        result = self.restore()
        self.assertEqual("record_unavailable", result["reason"])
        self.assert_restored()

    def test_capture_read_failure_restores(self):
        original = self.backend.read
        failed = []
        def read(role, offset, size):
            if offset == execution.NVS_OFFSET and not failed:
                failed.append(True)
                raise RuntimeError("PRIVATE")
            return original(role, offset, size)
        self.backend.read = read
        self.assertEqual("capture_unavailable", self.restore()["reason"])
        self.assert_restored()

    def test_private_save_failure_restores(self):
        with patch.object(stage, "_save_capture", side_effect=OSError("PRIVATE")):
            self.assertFalse(self.restore()["available"])
        self.assert_restored()

    def test_existing_capture_is_not_overwritten(self):
        path = self.root / ".private" / f"ot201-sync-A-{'a' * 32}.bin"
        path.write_bytes(b"existing evidence")
        self.assertFalse(self.restore()["available"])
        self.assertEqual(b"existing evidence", path.read_bytes())
        self.assert_restored()

    def test_close_failure_blocks_all_rom_and_disk_capture(self):
        self.backend.fail = "close"
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertEqual([("close", "A", 0)], self.backend.trace)
        self.assertFalse(list((self.root / ".private").glob("ot201-sync-*.bin")))

    def test_idle_failure_blocks_all_rom(self):
        self.backend.assert_idle = lambda: False
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertEqual([("close", "A", 0)], self.backend.trace)

    def test_journal_failure_blocks_all_rom(self):
        self.journal.healthy = False
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertEqual([("close", "A", 0)], self.backend.trace)

    def test_barrier_write_failure_blocks_rom(self):
        original = self.journal.add
        def add(event, *args, **kwargs):
            if event == "restore_intent":
                self.journal.healthy = False
                raise execution.ExecutionError("journal_write_failed")
            return original(event, *args, **kwargs)
        self.journal.add = add
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertEqual([("close", "A", 0)], self.backend.trace)

    def test_restore_corruption_still_prevents_boot(self):
        self.backend.fail = "corrupt_restore_read"
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertTrue(self.observation.result["available"])
        self.assertEqual(0, self.backend.original_resets)

    def test_preexisting_original_diagnostic_refuses_before_io(self):
        with self.assertRaises(fixtures.decoder.ReadbackError):
            stage.Observation(fixtures.diagnostic(), "A", expected_image=self.image)
        self.assertEqual([], self.backend.trace)

    def test_repeated_capture_cannot_reuse_old_result(self):
        self.restore()
        before = list(self.backend.trace)
        with self.assertRaises(execution.ExecutionError):
            self.observation.capture(self.root, self.backend, self.journal, self.row, observed_image=self.image)
        self.assertFalse(self.observation.result["available"])
        self.assertEqual(before, self.backend.trace)

    def test_changed_original_is_hard_refusal_before_rom(self):
        self.row["nvs"] = b"?" * execution.NVS_SPAN
        with self.assertRaisesRegex(execution.ExecutionError, "diagnostic_original_changed"):
            self.restore()
        self.assertEqual([("close", "A", 0)], self.backend.trace)

    def test_failed_close_clears_prior_observation(self):
        self.restore()
        self.backend.fail = "close"
        with self.assertRaises(execution.ExecutionError):
            self.restore()
        self.assertFalse(self.observation.result["available"])


    def test_fixed_input_reason_survives_capture_then_original_restoration(self):
        self.backend.flash['A',execution.NVS_OFFSET]=sync_fixtures.diagnostic(4,17,sync_fixtures.input_record(terminal=17))
        result=self.restore()
        self.assertEqual((result['stage'],result['error']),('input_result','console_fault'))
        self.assert_restored()
    def test_old_diagnostic_record_is_not_a_new_observation(self):
        import security_policy_stage_readback_tests as previous
        self.backend.flash['A',execution.NVS_OFFSET]=previous.diagnostic(4,2)
        result=self.restore();self.assertFalse(result['available']);self.assert_restored()

    def test_stage3_input_is_partial_progress_after_capture_and_restore(self):
        self.backend.flash['A',execution.NVS_OFFSET]=sync_fixtures.diagnostic(3)
        result=self.restore();self.assert_restored()
        self.assertEqual(result['input_status'],'committed_before_stage_result')
        self.assertEqual(result['input']['terminal_reason'],'none')
        self.assertNotIn('receipt',result);self.assertNotIn('complete',result)

    def test_wrong_observed_image_blocks_capture_but_preserves_restoration(self):
        wrong=stage.readback.ImageBinding(stage.readback.SYNC_CONTRACT,self.image.target,'c'*64)
        result=stage.restore_with_observation(self.root,self.backend,self.journal,self.row,
                                              self.observation,observed_image=wrong)
        self.assertFalse(result['available']);self.assert_restored()
        self.assertFalse(list((self.root/'.private').glob('ot201-sync-*.bin')))
        trace=self.backend.trace
        self.assertLess(trace.index(('write','A',execution.APP_OFFSET)),trace.index(('read','A',execution.NVS_OFFSET)))

    def test_successful_projection_does_not_suppress_restore_failure(self):
        self.backend.flash['A',execution.NVS_OFFSET]=sync_fixtures.diagnostic(8)
        self.backend.fail='restore_write'
        with self.assertRaises(execution.ExecutionError):self.restore()
        self.assertTrue(self.observation.result['available'])
        self.assertEqual(self.backend.original_resets,0)

if __name__ == "__main__":
    unittest.main(verbosity=2)
