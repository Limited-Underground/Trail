"""Actual acquisition/coordinator/candidate custody composition, synthetic I/O.

No devices, ports, SDK operations or native human interface are accessed. Flash
bytes, transport and acknowledgements are explicit doubles; the lease, journals,
grants, runner, coordinator, custody and candidate operator are real host code.
"""
import builtins
import copy
import importlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_capture_runner as capture_runner
import enrollment_candidate_original_capture as capture
import enrollment_candidate_runner as runner
import enrollment_candidate_runtime as runtime
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_custody as custody
import enrollment_candidate_runner_tests as prior


class Fixture(prior.RunnerFixture):
    def __init__(self):
        super().__init__()
        self.old.runtime.private_root = self.disk.private
        self.old.runtime.verify = lambda deadline: {'root': str(self.assembly.root)}
        self.capture_backends, self.capture_log = [], []
        self.capture_fault, self.claim_fault, self.handoff_fault = None, None, None
        self.capture_request = {'schema': 'OT-ORIGINAL-CAPTURE-REQUEST-1',
            'runtime_sha256': self.assembly.manifest_sha256, 'roles': {r: {
                'device_binding': d.binding, 'profile_evidence_sha256': ('a' if r == 'A' else 'b') * 64}
                for r, d in self.devices.items()}}
        self.capture_grant = {'schema': 'OT-ORIGINAL-CAPTURE-GRANT-1', 'attempt': '4' * 32,
            'request_sha256': custody.sha(custody.canonical(self.capture_request)),
            'runtime_sha256': self.assembly.manifest_sha256, 'operation': 'capture',
            'origin_attempt': None, 'attempt_count': 1, 'actions': list(capture.ACTIONS),
            'issued_utc': 100, 'capture_expires_utc': 200, 'cleanup_expires_utc': 300}
        self.handoff = self.disk.private / 'candidate-handoff.json'
        self.capture_package_path = self.disk.private / 'capture-package.json'
        self.capture_package = dict(schema='OT-ORIGINAL-CAPTURE-RUNNER-1', operation='capture',
            evidence_root=str(self.evidence), handoff=str(self.handoff), **{
                k: self.inputs[k] for k in ('assembly', 'identities', 'binding_key', 'profiles')},
            request=self.file('capture-request.json', custody.canonical(self.capture_request)),
            grant=self.file('capture-grant.json', custody.canonical(self.capture_grant)))
        self.save_capture_package()

    def save_capture_package(self):
        self.capture_package_path.write_bytes(custody.canonical(self.capture_package))
        self.capture_package_sha = custody.sha(self.capture_package_path.read_bytes())

    def capture_backend_factory(self, selected_runtime, lease, **kwargs):
        role, f = kwargs['role'], self
        self.factory_calls.append(('capture_backend', role))
        class Backend:
            def __init__(self):
                self.inner = prior.prior.LeasedFlashBackend(f.devices[role], f.capture_log, f.clock, lease)
                self.counts = {}
                self.authority = copy.deepcopy(kwargs['authority'])
            def claim(self, binding, deadline):
                self.inner.claim(binding, deadline)
                return self.guard(binding, deadline)
            def guard(self, binding, deadline):
                value = self.inner.guard(binding, deadline)
                return rom.CaptureAdmission(value.device_binding, value.model, value.flash_bytes,
                    'verified-read-only', value.layout_sha256, value.boot_selection, value.rom_held)
            def read(self, offset, size, deadline):
                name = next(n for n, span in custody.SPANS.items() if span == (offset, size))
                raw = self.inner.read(offset, size, deadline)
                self.counts[name] = self.counts.get(name, 0) + 1
                return raw
            @property
            def pins(self):
                return {n: custody.descriptor(f.devices[role].flash[n]) for n in self.counts}
            @property
            def complete(self):
                return set(self.counts) == set(custody.SPANS) and all(v >= 2 for v in self.counts.values())
            def reset_original(self, deadline):
                f.capture_log.append((role, 'capture_reset', None, deadline))
                if f.capture_fault == ('reset', role):
                    raise OSError('PRIVATE ambiguous original reset')
                return self.inner.reset_original(deadline)
            def close(self):
                if f.capture_fault == ('close', role):
                    return False
                # Capture close proves worker handles ended; ROM custody and
                # its in-process read-only owner remain until transfer/reset.
                return True
            def assert_idle(self):
                return f.capture_fault != ('close', role)
        backend = Backend()
        self.capture_backends.append(backend)
        return backend

    def backend_factory(self, *args, **kwargs):
        backend = super().backend_factory(*args, **kwargs)
        role = kwargs['role']
        claim, used = backend.claim, [False]
        def checked_claim(binding, deadline):
            if self.claim_fault == role and not used[0]:
                used[0] = True
                raise OSError('PRIVATE first candidate claim failure')
            return claim(binding, deadline)
        backend.claim = checked_claim
        return backend

    def wait_handoff(self, seconds):
        self.clock.value += seconds
        if self.handoff_fault == 'absent':
            self.clock.value = 110
            return
        raw = custody.canonical(dict(path=str(self.package_path), **runtime.descriptor(self.package_path.read_bytes())))
        if not self.handoff.exists():
            self.handoff.write_bytes(raw)
        if self.handoff_fault == 'preflight':
            self.package_path.write_bytes(b'private-invalid-package')

    def run_capture(self, **kwargs):
        return capture_runner.run(self.capture_package_path, self.capture_package_sha, 'capture',
            utc=self.utc, monotonic=self.clock, runtime_factory=self.runtime_factory,
            lease_factory=self.lease_factory, capture_backend_factory=self.capture_backend_factory,
            candidate_backend_factory=self.backend_factory, operator_factory=self.operator_factory,
            view_factory=self.view_factory, wait=self.wait_handoff, **kwargs)


class CaptureRunnerTests(unittest.TestCase):
    def fixture(self):
        f = Fixture()
        self.addCleanup(f.cleanup)
        return f

    def unused_candidate(self, f):
        self.assertFalse((f.evidence / ('enrollment-candidate-used-' + '1' * 32)).exists())

    def test_import_constructor_and_package_preflight_are_inert(self):
        f = self.fixture()
        original_import = builtins.__import__
        def guarded(name, *args, **kwargs):
            if name.split('.')[0] in ('serial', 'esptool', 'ctypes'):
                self.fail('native/SDK import in preflight')
            return original_import(name, *args, **kwargs)
        with patch('builtins.__import__', guarded), patch('subprocess.run', side_effect=AssertionError('child')):
            importlib.reload(capture_runner)
            prepared = capture_runner.preflight(f.capture_package_path, f.capture_package_sha,
                utc=f.utc, monotonic=f.clock)
        self.assertEqual(prepared.operation, 'capture')
        self.assertAlmostEqual(prepared.capture_deadline, f.clock() + 200 - f.utc(), delta=.0001)
        self.assertEqual(f.factory_calls, [])
        self.assertEqual(list(f.evidence.iterdir()), [])
        self.assertFalse(f.handoff.exists())

    def test_actual_composed_capture_handoff_trial_and_release_share_one_lease(self):
        f = self.fixture()
        result = f.run_capture()
        self.assertEqual(result.outcome, 'passed', result)
        self.assertIsNone(result.first_failure)
        self.assertIsNone(result.cleanup_failure)
        self.assertTrue(result.lease_released)
        self.assertTrue(result.capture.capture_complete)
        self.assertTrue(result.capture.custody_released)
        self.assertEqual(len(f.leases), 1)
        self.assertEqual(sum(c[0] == 'runtime' for c in f.factory_calls), 1)
        self.assertFalse(any(e[1] == 'capture_reset' for e in f.capture_log))
        self.assertEqual({r['owner'] for r in result.capture.roles.values()}, {'candidate-released'})
        for backend in f.capture_backends:
            self.assertTrue(all(v >= 3 for v in backend.counts.values()))
            self.assertLessEqual(backend.authority['capture_deadline'], 110)
        self.assertTrue(all(d.flash[n] == d.original[n] for d in f.devices.values() for n in custody.SPANS))

    def test_preexisting_and_reparse_handoff_refuse_before_any_factory(self):
        f = self.fixture()
        f.handoff.write_bytes(b'{}')
        with self.assertRaisesRegex(runner.RunnerError, '^handoff_used$'):
            f.run_capture()
        self.assertEqual(f.factory_calls, [])
        self.unused_candidate(f)

    def test_either_active_custody_marker_refuses_inert_preflight(self):
        for active in (capture.ACTIVE, custody.ACTIVE):
            with self.subTest(active=active):
                f = self.fixture()
                (f.evidence / active).write_bytes(b'{}')
                with self.assertRaisesRegex(runner.RunnerError, '^custody_held$'):
                    capture_runner.preflight(f.capture_package_path, f.capture_package_sha,
                        utc=f.utc, monotonic=f.clock)
                self.assertEqual(f.factory_calls, [])
                self.unused_candidate(f)

    def test_candidate_preflight_factory_and_expired_wait_safely_release_capture_owned_pair(self):
        for fault in ('preflight', 'factory', 'absent'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'factory': f.factory_fault = 'PRIVATE factory failure'
                else: f.handoff_fault = fault
                result = f.run_capture()
                self.assertEqual(result.outcome, 'failed', result)
                self.assertTrue(result.capture.custody_released)
                self.assertTrue(result.lease_released)
                self.unused_candidate(f)
                self.assertEqual({e[0] for e in f.capture_log if e[1] == 'capture_reset'}, {'A', 'B'})

    def test_used_journal_and_active_failures_leave_unclaimed_roles_capture_owned(self):
        for fault in ('used', 'journal', 'active'):
            with self.subTest(fault=fault):
                f = self.fixture()
                once, save = custody._once, custody.Journal.save
                def fail_once(path, raw):
                    if ((fault == 'used' and path.name.startswith('enrollment-candidate-used-'))
                        or (fault == 'active' and path.name == custody.ACTIVE)):
                        raise OSError('PRIVATE record failure')
                    return once(path, raw)
                def fail_save(journal, state):
                    if fault == 'journal': raise OSError('PRIVATE candidate journal failure')
                    return save(journal, state)
                with patch.object(custody, '_once', fail_once), patch.object(custody.Journal, 'save', fail_save):
                    result = f.run_capture()
                self.assertTrue(result.capture.custody_released, result)
                self.assertTrue(result.lease_released)
                self.assertEqual({e[0] for e in f.capture_log if e[1] == 'capture_reset'}, {'A', 'B'})
                self.assertFalse(any(e[1] in ('write', 'boot_candidate') for e in f.log))

    def test_failed_A_claim_transfers_only_A_and_releases_B_before_owner_ack(self):
        f = self.fixture()
        f.claim_fault = 'A'
        result = f.run_capture()
        self.assertEqual(result.outcome, 'failed', result)
        self.assertTrue(result.capture.custody_released)
        self.assertTrue(result.lease_released)
        self.assertEqual(result.capture.roles['A']['owner'], 'candidate-released')
        self.assertEqual(result.capture.roles['B']['owner'], 'capture')
        self.assertEqual([e[0] for e in f.capture_log if e[1] == 'capture_reset'], ['B'])
        self.assertFalse(any(e[0] == 'B' and e[1] == 'claim' for e in f.log))

    def test_failed_B_claim_leaves_original_restore_with_candidate_owner(self):
        f = self.fixture()
        f.claim_fault = 'B'
        result = f.run_capture()
        self.assertEqual(result.outcome, 'failed', result)
        self.assertTrue(result.capture.custody_released)
        self.assertTrue(result.lease_released)
        self.assertFalse(any(e[1] == 'capture_reset' for e in f.capture_log))

    def test_stale_receipt_drift_and_handoff_change_refuse_before_candidate_used(self):
        for fault in ('stale', 'receipt', 'changed', 'deleted'):
            with self.subTest(fault=fault):
                f = self.fixture()
                activate = f.operator_factory
                def factory(*args, **kwargs):
                    instance = activate(*args, **kwargs)
                    original_activate = instance.activate
                    def changed(*a, **k):
                        result = original_activate(*a, **k)
                        if fault == 'stale':
                            raw = f.devices['B'].flash['ota0_prefix']
                            f.devices['B'].flash['ota0_prefix'] = b'\x01' + raw[1:]
                        elif fault == 'receipt':
                            (f.evidence / ('enrollment-original-' + '4' * 32 + '-pins.json')).write_bytes(b'{}')
                        elif fault == 'changed': f.handoff.write_bytes(b'{}')
                        elif fault == 'deleted': f.handoff.unlink()
                        return result
                    instance.activate = changed
                    return instance
                f.operator_factory = factory
                result = f.run_capture()
                self.assertNotEqual(result.outcome, 'passed')
                self.unused_candidate(f)
                self.assertFalse(any(e[1] in ('write', 'boot_candidate') for e in f.log))

    def test_ambiguous_capture_reset_and_candidate_reset_have_no_cross_owner_retry(self):
        f = self.fixture()
        f.factory_fault, f.capture_fault = 'PRIVATE factory failure', ('reset', 'A')
        result = f.run_capture()
        self.assertEqual(result.outcome, 'held', result)
        self.assertIsNotNone(result.first_failure)
        self.assertIsNotNone(result.cleanup_failure)
        self.assertEqual([e[0] for e in f.capture_log if e[1] == 'capture_reset'].count('A'), 1)
        self.unused_candidate(f)
        g = self.fixture()
        g.reset_fault_role = 'A'
        result = g.run_capture()
        self.assertEqual(result.outcome, 'held', result)
        self.assertFalse(any(e[1] == 'capture_reset' for e in g.capture_log))

    def test_released_session_and_late_candidate_grant_cannot_be_used(self):
        for fault in ('released', 'expired'):
            with self.subTest(fault=fault):
                f = self.fixture()
                sessions = []
                original_session = capture_runner.CaptureSession
                def session(*a, **k):
                    value = original_session(*a, **k); sessions.append(value); return value
                operator_factory = f.operator_factory
                def factory(*a, **k):
                    instance = operator_factory(*a, **k)
                    activate = instance.activate
                    def changed(*args, **kwargs):
                        if fault == 'expired': f.utc.value = 200
                        result = activate(*args, **kwargs)
                        if fault == 'released': sessions[0].release()
                        return result
                    instance.activate = changed
                    return instance
                f.operator_factory = factory
                with patch.object(capture_runner, 'CaptureSession', session):
                    result = f.run_capture()
                self.assertEqual(result.outcome, 'failed', result)
                self.unused_candidate(f)
                self.assertTrue(result.capture.custody_released)
                self.assertFalse(any(e[1] in ('write', 'boot_candidate') for e in f.log))

    def test_duplicate_candidate_identities_refuse_without_spending_candidate_grant(self):
        f = self.fixture()
        wait = f.wait_handoff
        def supplied(seconds):
            f.rebind_input('identities', {'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': {
                'A': prior.prior.IDENTITIES['A'], 'B': prior.prior.IDENTITIES['A']}})
            wait(seconds)
        f.wait_handoff = supplied
        result = f.run_capture()
        self.assertEqual(result.outcome, 'failed', result)
        self.unused_candidate(f)
        self.assertTrue(result.capture.custody_released)

    def test_new_release_only_package_recovers_unadopted_held_originals(self):
        f = self.fixture()
        prepared = capture_runner.preflight(f.capture_package_path, f.capture_package_sha,
            utc=f.utc, monotonic=f.clock)
        selected_runtime = f.runtime_factory(f.assembly.manifest_path, f.assembly.manifest_sha256,
            f.disk.private, monotonic=f.clock)
        lease = f.lease_factory(f.disk.private, monotonic=f.clock)
        session = capture.CaptureSession(f.disk.private, f.evidence, prepared.request,
            prepared.grant_raw, prepared.grant_sha256, runtime=selected_runtime, lease=lease,
            identities=dict(prepared.identities), binding_key=prepared.binding_key,
            profiles=dict(prepared.profiles), utc=f.utc, monotonic=f.clock,
            backend_factory=f.capture_backend_factory)
        self.assertEqual(session.capture().outcome, 'captured')
        # Explicit synthetic process exit releases only the OS file lock. The
        # saved journal and ROM modes continue to require a new release grant.
        lease._file.close(); lease._file = None
        grant = dict(f.capture_grant, attempt='5' * 32, operation='release', origin_attempt='4' * 32)
        f.capture_package.update(operation='release', handoff=None,
            grant=f.file('release-grant.json', custody.canonical(grant)))
        f.save_capture_package()
        result = capture_runner.run(f.capture_package_path, f.capture_package_sha, 'release',
            utc=f.utc, monotonic=f.clock, runtime_factory=f.runtime_factory,
            lease_factory=f.lease_factory, capture_backend_factory=f.capture_backend_factory)
        self.assertEqual(result.outcome, 'released', result)
        self.assertTrue(result.lease_released)
        self.assertTrue(result.capture.custody_released)
        self.unused_candidate(f)
        self.assertFalse(any(e[1] in ('write', 'boot_candidate') for e in f.log))

    def test_late_final_lease_close_or_release_journal_cannot_publish_success(self):
        for fault in ('lease', 'journal'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'lease':
                    factory = f.lease_factory
                    def lease_factory(*a, **k):
                        lease = factory(*a, **k)
                        close, count = lease.close, [0]
                        def delayed():
                            result = close()
                            count[0] += 1
                            if count[0] == 2: f.clock.value = 202
                            return result
                        lease.close = delayed
                        return lease
                    f.lease_factory = lease_factory
                    result = f.run_capture()
                else:
                    save = capture._Journal.save
                    def delayed(journal, state):
                        result = save(journal, state)
                        if state['phase'] == 'released': f.clock.value = 202
                        return result
                    with patch.object(capture._Journal, 'save', delayed):
                        result = f.run_capture()
                self.assertNotEqual(result.outcome, 'passed', result)
                self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.cleanup_failure)
                if result.receipt_path is not None:
                    self.assertNotIn(custody.decode(result.receipt_path.read_bytes())['outcome'], ('passed', 'released'))

    def test_lost_failure_response_and_torn_outer_receipt_preserve_cause_without_retry(self):
        for fault in ('response', 'receipt'):
            with self.subTest(fault=fault):
                f = self.fixture()
                f.handoff_fault = 'preflight'
                write_once = runner._write_once
                def torn(path, raw, *, on_create=None):
                    if fault == 'receipt' and path.name.startswith('enrollment-original-runner-') and path.suffix == '.pending':
                        with path.open('xb') as stream:
                            if on_create is not None: on_create(path)
                            stream.write(raw[:12])
                        raise runner.RunnerError('receipt_failed')
                    return write_once(path, raw, on_create=on_create)
                with patch.object(runner, '_write_once', torn):
                    result = f.run_capture()
                self.assertEqual(result.outcome, 'failed', result)
                self.assertTrue(result.capture.custody_released)
                self.assertTrue(result.lease_released)
                self.unused_candidate(f)
                self.assertEqual(result.first_failure, ('handoff', 'file_changed'))
                self.assertIsNotNone(result.receipt_path)
                receipt = custody.decode(result.receipt_path.read_bytes())
                self.assertEqual(receipt['first_failure'], ['handoff', 'file_changed'])
                self.assertEqual(receipt['outcome'], 'failed')
                if fault == 'receipt':
                    self.assertEqual(receipt['cleanup_failure'], ['receipt', 'receipt_failed'])
                    self.assertTrue(result.receipt_path.with_suffix('.unaccepted').exists())
                before = result.receipt_path.read_bytes()
                calls = len(f.factory_calls)
                # Model loss of the previous returned response: durable used
                # state and receipt prevent reconstructing/retrying capture.
                with self.assertRaisesRegex(runner.RunnerError, '^grant_used$'):
                    f.run_capture()
                self.assertEqual(len(f.factory_calls), calls)
                self.assertEqual(result.receipt_path.read_bytes(), before)


if __name__ == '__main__':
    unittest.main(verbosity=2)
