"""Current-attempt settings custody using production paths and synthetic SDKs."""
from pathlib import Path
import copy
import os
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_budget_adoption_tests as adoption
import enrollment_candidate_original_capture as capture
import enrollment_candidate_custody as custody
import enrollment_candidate_rom_adapter as rom


class Fixture(adoption.RetainedFixture):
    def __init__(self):
        super().__init__()
        self.historical = {str(p): p.read_bytes() for p in self.baseline_root.iterdir() if p.is_file()}
        self.current = adoption.RetainedOriginalTests().safe_drift()
        for role in custody.ROLES:
            self.sdks[role].memory['nvs'] = self.current
            self.originals[role]['nvs'] = self.current
        self.request = dict(self.request, schema=capture.CURRENT_NVS_REQUEST)
        grant = capture.decode(self.grant_raw)
        grant['request_sha256'] = capture.sha(capture.canonical(self.request))
        self.grant_raw = capture.canonical(grant)
        self.session = self.make_session()
        self.request_candidate = self.candidate()


class CurrentSettingsTests(unittest.TestCase):
    def fixture(self):
        f = Fixture(); self.addCleanup(f.close); return f

    def unchanged_history(self, f):
        self.assertEqual({str(p): p.read_bytes() for p in f.baseline_root.iterdir() if p.is_file()}, f.historical)

    def test_old_equality_defect_and_explicit_new_mode_full_adoption_restoration(self):
        old = adoption.RetainedFixture(); self.addCleanup(old.close)
        for role in custody.ROLES: old.sdks[role].memory['nvs'] = adoption.RetainedOriginalTests().safe_drift()
        self.assertTrue(old.session.capture().capture_complete)
        with self.assertRaises(capture.CaptureError): old.verify(deadline=2110)
        self.assertEqual(sum(e[0] == 'write' for s in old.sdks.values() for e in s.trace), old.start_writes)
        f = self.fixture(); f.prepare()
        for role in custody.ROLES:
            backend = f.session._backends[role]
            self.assertEqual(backend._counts['nvs'], 3)
            for name in custody.SPANS:
                if name != 'nvs': self.assertEqual(backend._counts[name], 1)
                self.assertEqual(os.path.samefile(f.baseline_journal.original(role, name),
                    f.session._journal.original(role, name)), name != 'nvs')
            self.assertEqual(f.session.pins[role]['nvs'], capture.descriptor(f.current))
            self.assertNotEqual(f.session.pins[role]['nvs'], backend._retained_expected['nvs'])
        result = f.execute()
        self.assertTrue(result.custody_released)
        self.assertTrue(result.owner_confirmed)
        for role in custody.ROLES:
            self.assertTrue(custody._adoption_commit(f.state(), role))
            self.assertEqual(f.sdks[role].memory['nvs'], f.current)
            self.assertEqual(custody.Journal(f.candidate_root, f.attempt).original(role, 'nvs').read_bytes(), f.current)
        self.assertTrue(f.session.release().custody_released)
        self.assertTrue(f.lease.close())
        self.unchanged_history(f)

    def test_mode_cannot_replay_legacy_grant_or_substitute_old_nvs_candidate_request(self):
        f = self.fixture()
        grant = capture.decode(f.grant_raw)
        legacy = dict(f.request, schema='OT-RETAINED-ORIGINAL-REQUEST-1')
        grant['request_sha256'] = capture.sha(capture.canonical(legacy))
        f.grant_raw = capture.canonical(grant); f.session = f.make_session()
        with self.assertRaisesRegex(capture.CaptureError, 'grant_invalid'): f.session.capture()
        self.assertEqual(len(f.calls), f.start_calls)
        g = self.fixture(); self.assertTrue(g.session.capture().capture_complete)
        request = copy.deepcopy(g.request_candidate)
        for role in custody.ROLES:
            request['roles'][role]['originals']['nvs'] = g.session._backends[role]._retained_expected['nvs']
        calls = len(g.calls)
        with self.assertRaises(capture.CaptureError): g.verify(request=request, deadline=2110)
        self.assertEqual(len(g.calls), calls)

    def test_pending_malformed_and_unstable_current_settings_hold_without_reset_or_write(self):
        for fault in ('pending', 'malformed', 'unstable'):
            f = self.fixture()
            if fault == 'pending': f.sdks['A'].memory['nvs'] = adoption.sdk.pending_reset_nvs()
            elif fault == 'malformed': f.sdks['A'].memory['nvs'] = b'X' * 12288
            else:
                count = [0]
                def mutate(fixture, role, query):
                    if role == 'A' and query['operation'] == 'guarded_read' and query['span'] == 'nvs':
                        count[0] += 1
                        if count[0] == 1: fixture.sdks['A'].memory['nvs'] = b'\xff' * 12288
                f.mutate_after = mutate
            with self.subTest(fault=fault):
                result = f.session.capture()
                self.assertFalse(result.capture_complete)
                self.assertFalse(result.custody_released)
                self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.cleanup_failure)
                self.assertFalse(f.session._journal.original('A', 'nvs').exists())
                self.assertTrue((f.root / capture.ACTIVE).exists())
                self.assertEqual(sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace), f.start_writes)
                self.assertFalse(any(q[1] == 'reset_original' for q in f.calls[f.start_calls:]))
                self.unchanged_history(f)

    def test_incomplete_current_snapshot_persistence_stays_held(self):
        f = self.fixture(); once = capture._once
        def partial(path, raw):
            if path.name.endswith('-A-nvs.bin'):
                path.write_bytes(raw[:12]); raise OSError('synthetic incomplete save')
            return once(path, raw)
        with mock.patch.object(capture, '_once', partial): result = f.session.capture()
        self.assertFalse(result.capture_complete)
        self.assertFalse(result.custody_released)
        self.assertFalse(any(q[1] == 'reset_original' for q in f.calls[f.start_calls:]))
        self.assertFalse((f.root / ('enrollment-original-' + '3' * 32 + '-pins.json')).exists())
        self.unchanged_history(f)

    def test_cleanup_exact_current_snapshot_and_single_use_reset(self):
        f = self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        result = f.session.release()
        self.assertTrue(result.custody_released)
        self.assertEqual([q[0] for q in f.calls[f.start_calls:] if q[1] == 'reset_original'], ['A', 'B'])
        self.assertFalse(any('current-nvs-drift' in p.name for p in f.root.iterdir()))
        calls = len(f.calls); f.session.release(); self.assertEqual(len(f.calls), calls)
        self.unchanged_history(f)

    def test_saved_current_settings_changed_before_adoption_refuse_write(self):
        f = self.fixture(); f.prepare()
        f.session._journal.original('A', 'nvs').write_bytes(b'\xff' * 12288)
        result = f.execute()
        self.assertIsNotNone(result.first_failure)
        self.assertFalse(f.session.release().custody_released)
        self.assertEqual(sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace), f.start_writes)
        self.unchanged_history(f)

    def test_immutable_change_and_current_settings_change_after_snapshot_refuse_freshness(self):
        for name in ('bootloader', 'partition', 'otadata', 'application', 'ota0_prefix', 'nvs'):
            f = self.fixture(); self.assertTrue(f.session.capture().capture_complete)
            f.sdks['A'].memory[name] = b'X' * custody.SPANS[name][1]
            with self.subTest(name=name), self.assertRaises(capture.CaptureError): f.verify(deadline=2110)
            self.assertEqual(sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace), f.start_writes)

    def test_same_lease_reentered_token_refuses_before_reads(self):
        f = self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        binding = f.profiles['A'].device_binding
        f.lease.mark_boot(binding, original=True); f.lease.enter_rom(binding, 2110)
        calls = len(f.calls)
        with self.assertRaises(capture.CaptureError): f.verify(deadline=2110)
        self.assertEqual(len(f.calls), calls)

    def test_partial_candidate_write_restores_current_settings_exactly(self):
        f = self.fixture(); f.prepare(); f.sdks['B'].write_fault = 'partial'
        def clear(fixture, role, query):
            if role == 'B' and query['operation'] == 'write': fixture.sdks['B'].write_fault = None
        f.mutate_after = clear
        result = f.execute()
        self.assertIsNotNone(result.first_failure)
        self.assertTrue(result.custody_released)
        self.assertTrue(f.session.release().custody_released)
        for role in custody.ROLES: self.assertEqual(f.sdks[role].memory['nvs'], f.current)
        self.unchanged_history(f)

    def test_reentered_rom_token_at_adoption_refuses_candidate_write(self):
        f = self.fixture(); f.prepare()
        binding = f.profiles['A'].device_binding
        f.lease.mark_boot(binding, original=True); f.lease.enter_rom(binding, 2110)
        result = f.execute()
        self.assertIsNotNone(result.first_failure)
        self.assertEqual(sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace), f.start_writes)


if __name__ == '__main__':
    unittest.main(verbosity=2)
