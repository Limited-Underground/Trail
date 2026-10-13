"""Real maintained CaptureSession/ROM worker with the existing FakeSDK fixture.

Every artifact is synthetic and temporary. No real SDK, USB or owner receipt.
"""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import enrollment_candidate_budget_adoption_tests as retained
capture, rom = retained.capture, retained.rom
SOURCE = ROOT / 'tools' / 'enrollment_candidate_recovery_diagnostic.py'
spec = importlib.util.spec_from_file_location('diagnostic_maintained_under_test', SOURCE)
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)


class MaintainedDiagnosticTests(unittest.TestCase):
    def build(self):
        f = retained.RetainedFixture()
        self.addCleanup(f.close)
        old = f.private / 'historical-quarantine-fixture'; old.mkdir()
        for i in range(39): (old / f'legacy-{i}.json').write_bytes(b'preserved synthetic historical state')
        def write(name, value):
            p = f.private / (name + '.json'); p.write_bytes(d.canonical(value) + b'\n')
            return d.reference(p, f.private)
        preserved = write('diagnostic-preserved', {'schema': 'OT-LEGACY-SESSION-PRESERVATION-1',
            'session': str(old), 'before_equals_after': True,
            'files': {str(p): d.reference(p, f.private) for p in old.iterdir()}})
        disposition = write('diagnostic-disposition', {'schema': 'OT-MANUAL-RECOVERY-DISPOSITION-1',
            'status': 'manual_recovery_observed_baseline_unresolved', 'session': str(old),
            'candidate_eligible': False, 'automatic_custody_released': False, 'references': {}})
        grant = capture.decode(f.grant_raw)
        package = {'controller_sha256': d.sha(SOURCE.read_bytes()), 'source_pins': write('synthetic-source-pins', {'synthetic': True}),
            'assembly': write('synthetic-assembly', {'synthetic': True}),
            'capture_grant': write('diagnostic-grant', grant), 'capture_request': write('diagnostic-request', f.request),
            'comparator_request': write('diagnostic-comparator', f.candidate()),
            'legacy_preservation': preserved, 'legacy_disposition': disposition}
        package['legacy_gate_module'] = write('synthetic-gate', {'synthetic': True})
        data = {'capture_grant': d.read_ref(package['capture_grant'], f.private), 'identities': d.canonical({'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': f.identities}),
            'profiles': d.canonical({'schema': 'OT-CANDIDATE-PROFILES-1', 'roles': {r: vars(p) for r, p in f.profiles.items()}}),
            'binding_key': f.key, 'legacy_preservation': d.read_ref(preserved, f.private),
            'legacy_disposition': d.read_ref(disposition, f.private),
            'legacy_gate_module': d.read_ref(package['legacy_gate_module'], f.private)}
        prepared = {'private': f.private, 'output': f.root, 'package': package, 'data': data,
            'package_ref': write('diagnostic-package', package), 'authorization_ref': write('synthetic-authority', {'synthetic': True}),
            'source_root': Path(f.manifest['root']) / 'policy', 'request': f.request, 'grant': grant,
            'comparator': f.candidate(), 'capture_deadline': f.clock() + grant['capture_expires_utc'] - f.utc(),
            'cleanup_deadline': f.clock() + grant['cleanup_expires_utc'] - f.utc()}
        modules = {'enrollment_candidate_original_capture': capture,
            'enrollment_candidate_rom_adapter': rom,
            'enrollment_candidate_runtime': SimpleNamespace(verify_assembly=lambda *a, **kw: SimpleNamespace(
                root=Path(f.manifest['root']), manifest_sha256='1' * 64, manifest_path=f.private / 'manifest.json'))}
        return f, prepared, modules

    def run_fixture(self, f, prepared, modules):
        baseline = {p: p.read_bytes() for p in f.baseline_root.iterdir() if p.is_file()}
        quarantine = {p: p.read_bytes() for p in Path(d.decode(prepared['data']['legacy_preservation'])['session']).iterdir()}
        call_start, writes = len(f.calls), sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace)
        with mock.patch.object(rom, 'Runtime', return_value=f.runtime), mock.patch.object(rom, 'HardwareLease', return_value=f.lease), \
             mock.patch.object(d.time, 'time', f.utc), mock.patch.object(d.time, 'monotonic', f.clock), \
             mock.patch.object(d, 'REVIEWED_QUARANTINE_SHA256', prepared['package']['legacy_disposition']['sha256']), \
             mock.patch.object(d, 'REVIEWED_PRESERVATION_SHA256', prepared['package']['legacy_preservation']['sha256']), \
             mock.patch.object(d, 'REVIEWED_GATE_SHA256', prepared['package']['legacy_gate_module']['sha256']):
            ref = d._run_admitted(prepared, modules)
        receipt = d.decode(d.read_ref(ref, f.private))
        self.assertEqual(baseline, {p: p.read_bytes() for p in f.baseline_root.iterdir() if p.is_file()})
        self.assertEqual(quarantine, {p: p.read_bytes() for p in quarantine})
        self.assertEqual(writes, sum(e[0] == 'write' for s in f.sdks.values() for e in s.trace))
        calls = f.calls[call_start:]
        self.assertFalse(any(op in ('write', 'boot_candidate', 'restart_candidate') for _, op, _, _ in calls))
        for role in ('A', 'B'):
            self.assertLessEqual(sum(r == role and op == 'reset_original' for r, op, _, _ in calls), 1)
        self.assertFalse(receipt['candidate_eligible'])
        self.assertFalse(receipt['automatic_legacy_custody_released'])
        return receipt, calls

    def test_real_retained_equal_verify_terminal_release_no_new_backup(self):
        f, p, modules = self.build()
        receipt, calls = self.run_fixture(f, p, modules)
        self.assertTrue(receipt['capture_complete'])
        self.assertTrue(receipt['freshness_verified'])
        self.assertTrue(receipt['lease_closed'])
        self.assertIsNone(receipt['cleanup_failure'])
        self.assertEqual([r for r, op, _, _ in calls if op == 'reset_original'], ['A', 'B'])
        self.assertEqual(sum(op == 'guarded_read' for _, op, _, _ in calls), 12)
        self.assertFalse(any(op == 'read' for _, op, _, _ in calls))
        self.assertFalse(receipt['current_nvs_drift'])

    def test_real_safe_current_nvs_drift_keeps_raw_private_and_baseline_unchanged(self):
        f, p, modules = self.build()
        raw = retained.RetainedOriginalTests.safe_drift(None)
        for role in ('A', 'B'): f.sdks[role].memory['nvs'] = raw
        receipt, calls = self.run_fixture(f, p, modules)
        self.assertFalse(receipt['freshness_verified'])
        self.assertTrue(receipt['lease_closed'])
        self.assertIsNone(receipt['cleanup_failure'])
        self.assertEqual(set(receipt['current_nvs_drift']), {'A', 'B'})
        self.assertEqual([r for r, op, _, _ in calls if op == 'reset_original'], ['A', 'B'])
        for role in ('A', 'B'):
            evidence = receipt['current_nvs_drift'][role]
            self.assertEqual(d.read_ref(evidence['data'], f.private), raw)
            proof = next(p for p in receipt['proofs'] if p['role'] == role and p['operation'] == 'reset_original')
            self.assertEqual(proof['reset_current_nvs'], capture.descriptor(raw))
            self.assertEqual(set(proof['immutable_five']), d.IMMUTABLE)

    def test_real_immutable_mismatch_both_roles_produces_zero_reset(self):
        f, p, modules = self.build()
        for role in ('A', 'B'): f.sdks[role].memory['application'] = b'Z' * 733184
        receipt, calls = self.run_fixture(f, p, modules)
        self.assertFalse(receipt['lease_closed'])
        self.assertIsNotNone(receipt['cleanup_failure'])
        self.assertFalse(any(op == 'reset_original' for _, op, _, _ in calls))
        self.assertFalse(any(p['operation'] == 'reset_original' for p in receipt['proofs']))

    def test_real_unsafe_pending_nvs_produces_zero_reset(self):
        f, p, modules = self.build()
        raw = retained.sdk.pending_reset_nvs()
        for role in ('A', 'B'): f.sdks[role].memory['nvs'] = raw
        receipt, calls = self.run_fixture(f, p, modules)
        self.assertIsNotNone(receipt['cleanup_failure'])
        self.assertFalse(any(op == 'reset_original' for _, op, _, _ in calls))


if __name__ == '__main__':
    unittest.main()
