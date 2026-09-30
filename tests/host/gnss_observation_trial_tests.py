"""Host-only OT-0101e custody and observation regression tests."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gnss_observation_trial as trial
import run_gnss_observation_trial as operator


class Backend:
    def __init__(self, originals):
        self.memory = dict(originals)
        self.originals = None
        self.calls = []
        self.fail_write = None
        self.fail_read = None

    def claim(self, _binding):
        self.calls.append('claim')
        return True

    def reverify(self, _binding):
        return True

    def read(self, offset, _size):
        name = next(name for name, span in trial.SPANS.items() if span[0] == offset)
        if name == self.fail_read:
            raise OSError('prewrite read failure')
        return self.memory[name]

    def bind_originals(self, originals):
        self.originals = dict(originals)

    bind_recovery_originals = bind_originals

    def write(self, offset, raw):
        name = next(name for name, span in trial.SPANS.items() if span[0] == offset)
        self.calls.append('write_' + name)
        self.memory[name] = raw
        if self.fail_write == len([call for call in self.calls if call.startswith('write_')]):
            raise OSError('ambiguous write')

    def boot_candidate(self):
        self.calls.append('candidate_boot')
        return True

    def reset_original(self):
        self.calls.append('original_reset')
        return self.memory == self.originals

    def hold(self):
        self.calls.append('hold')


class TrialTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / '.private').mkdir()
        self.candidate = b'G' * 591456
        self.originals = {'bootloader': b'B' * 32768, 'partition': b'P' * 4096,
                          'ota': b'\xff' * 8192, 'application': b'A' * 733184,
                          'nvs': b'\xff' * 12288}
        self.backend = Backend(self.originals)
        self.patch = mock.patch.multiple(trial,
             CANDIDATE=trial.descriptor(self.candidate),
             PARTITION_SHA=trial.sha(self.originals['partition']),
             OTA_SHA=trial.sha(self.originals['ota']))
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.request = {'schema': 'OT0101E-GNSS-REQUEST-1',
                        'runtime_sha256': '1' * 64, 'device_binding': '2' * 64,
                        'candidate': dict(trial.CANDIDATE),
                        'protected': {name: trial.descriptor(self.originals[name])
                                      for name in ('bootloader', 'partition', 'ota')},
                        'original_prefix': trial.descriptor(self.originals['application'][:589824]),
                        'boot_compatibility': 'reviewed-ot0101e-image-header-compatibility',
                        'case': 'observe-and-restore'}

    def grant(self, operation='execute', number=1):
        obj = {'schema': 'OT0101E-GNSS-GRANT-1', 'attempt': f'{number:032x}',
               'request_sha256': trial.sha(trial.canonical(self.request)),
               'runtime_sha256': self.request['runtime_sha256'],
               'operation': operation,
               'origin_attempt': f'{1:032x}' if operation == 'recover' else None,
               'attempt_count': 1,
               'actions': trial.ACTIONS if operation == 'execute' else trial.RECOVERY_ACTIONS,
               'issued_utc': 100, 'expires_utc': 200}
        raw = trial.canonical(obj)
        return raw, trial.sha(raw)

    def execute(self, observation=lambda: 'fix_observed'):
        return trial.execute(self.root, self.request, *self.grant(), self.candidate,
                             self.backend, observation, utc=lambda: 150)

    def test_normal_observation_restores_exact_originals(self):
        result = self.execute()
        self.assertEqual(result['observation'], 'fix_observed')
        self.assertTrue(result['restored'])
        self.assertEqual(self.backend.memory, self.originals)
        self.assertIn('candidate_boot', self.backend.calls)
        self.assertFalse((self.root / '.private' / trial.ACTIVE).exists())

    def test_two_readings_and_clock_result_are_journaled_before_restore(self):
        observed = {'first_status': 'no_fix_observed',
                    'second_status': 'fix_observed', 'clock_advanced': True}
        result = self.execute(lambda: observed)
        self.assertEqual(result['observation'], observed)
        self.assertEqual(self.backend.memory, self.originals)

    def test_observation_timeout_still_restores(self):
        result = self.execute(lambda: 'timeout')
        self.assertEqual(result['observation'], 'timeout')
        self.assertEqual(self.backend.memory, self.originals)

    def test_phone_not_ready_still_restores(self):
        result = self.execute(lambda: 'connection_not_ready')
        self.assertEqual(result['observation'], 'connection_not_ready')
        self.assertEqual(self.backend.memory, self.originals)

    def test_ambiguous_candidate_write_restores_without_observation(self):
        self.backend.fail_write = 1
        observed = []
        result = self.execute(lambda: observed.append(True))
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(observed, [])
        self.assertEqual(self.backend.memory, self.originals)

    def test_restore_failure_retains_custody_until_fresh_recovery(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue((self.root / '.private' / trial.ACTIVE).exists())
        self.backend.fail_write = None
        result = trial.recover(self.root, self.request, *self.grant('recover', 2),
                               self.backend, utc=lambda: 150)
        self.assertTrue(result['restored'])
        self.assertEqual(self.backend.memory, self.originals)
        self.assertFalse((self.root / '.private' / trial.ACTIVE).exists())

    def test_nvs_restore_ambiguity_requires_fresh_recovery(self):
        self.backend.fail_write = 3
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue((self.root / '.private' / trial.ACTIVE).exists())
        self.backend.fail_write = None
        self.assertTrue(trial.recover(self.root, self.request, *self.grant('recover', 2),
                                      self.backend, utc=lambda: 150)['restored'])
        self.assertEqual(self.backend.memory, self.originals)

    def test_prewrite_read_failure_cannot_write_candidate(self):
        self.backend.fail_read = 'application'
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertFalse(any(x.startswith('write_') for x in self.backend.calls))
        self.assertTrue((self.root / '.private' / trial.ACTIVE).exists())
        self.backend.fail_read = None
        self.assertTrue(trial.recover(self.root, self.request, *self.grant('recover', 2),
                                      self.backend, utc=lambda: 150)['restored'])

    def test_wrong_candidate_and_ble_grant_refused_before_device_claim(self):
        with self.assertRaises(trial.TrialError):
            trial.execute(self.root, self.request, *self.grant(), b'X' + self.candidate[1:],
                          self.backend, lambda: 'fix_observed', utc=lambda: 150)
        self.assertEqual(self.backend.calls, [])
        raw, _ = self.grant()
        foreign = trial.decode(raw)
        foreign['schema'] = 'OT218-BLE-GRANT-1'
        bad = trial.canonical(foreign)
        with self.assertRaises(trial.TrialError):
            trial.validate_grant(self.request, bad, trial.sha(bad), 'execute', lambda: 150)

    def test_malformed_request_and_expired_grant_refused_before_device_claim(self):
        malformed = dict(self.request, case='confirm-and-restore')
        with self.assertRaises(trial.TrialError):
            trial.execute(self.root, malformed, *self.grant(), self.candidate,
                          self.backend, lambda: 'fix_observed', utc=lambda: 150)
        with self.assertRaises(trial.TrialError):
            trial.execute(self.root, self.request, *self.grant(), self.candidate,
                          self.backend, lambda: 'fix_observed', utc=lambda: 200)
        oversized = dict(self.request, extra='X' * 4096)
        with self.assertRaises(trial.TrialError):
            trial.validate_request(oversized)
        grant_raw, _ = self.grant()
        with self.assertRaises(trial.TrialError):
            trial.validate_grant(self.request, grant_raw + b'X' * 4096,
                                 trial.sha(grant_raw + b'X' * 4096), 'execute', lambda: 150)
        self.assertEqual(self.backend.calls, [])


class OperatorTests(unittest.TestCase):
    def test_privacy_safe_status_and_timeout(self):
        class Lines:
            def __init__(self, status, elapsed=121):
                self.status, self.elapsed, self.calls = status, elapsed, 0
                self.now = 0
            def read(self, _timeout):
                self.calls += 1
                if self.calls == 1:
                    return {'token': 'fixed', 'saved_owner_ready': True,
                            'clock_visible': True}
                if self.calls == 2:
                    return {'token': 'fixed', 'status': self.status}
                self.now = self.elapsed
                return {'token': 'fixed', 'status': 'fix_observed',
                        'clock_advanced': True}
        lines = Lines('no_fix_observed')
        self.assertEqual(operator.observe(lines, emit=lambda *_args, **_kw: None,
                                          clock=lambda: lines.now, token='fixed'),
                         {'first_status': 'no_fix_observed',
                          'second_status': 'fix_observed', 'clock_advanced': True})
        early = Lines('no_fix_observed', elapsed=119)
        self.assertEqual(operator.observe(early, emit=lambda *_args, **_kw: None,
                                          clock=lambda: early.now, token='fixed'), 'unavailable')
        class Timeout:
            def read(self, _timeout):
                raise operator.queue.Empty()
        self.assertEqual(operator.observe(Timeout(), emit=lambda *_args, **_kw: None,
                                          clock=lambda: 0, token='fixed'), 'timeout')
        class NotReady:
            def __init__(self):
                self.calls = 0
            def read(self, _timeout):
                self.calls += 1
                if self.calls > 1:
                    raise operator.queue.Empty()
                return {'token': 'fixed', 'saved_owner_ready': False,
                        'clock_visible': False}
        self.assertEqual(operator.observe(NotReady(), emit=lambda *_args, **_kw: None,
                                          clock=lambda: 0, token='fixed'),
                         'timeout')
        class Deadline:
            def __init__(self):
                self.now = 0
            def read(self, _timeout):
                self.now = 600
                return {'token': 'fixed', 'saved_owner_ready': False,
                        'clock_visible': False}
        deadline = Deadline()
        self.assertEqual(operator.observe(deadline, emit=lambda *_args, **_kw: None,
                                          clock=lambda: deadline.now, token='fixed'),
                         'timeout')
        class Recovered:
            def __init__(self):
                self.now = 0
                self.calls = 0
            def read(self, _timeout):
                self.calls += 1
                if self.calls == 1:
                    return {'token': 'fixed', 'saved_owner_ready': False,
                            'clock_visible': False}
                if self.calls == 2:
                    self.now = 10
                    return {'token': 'fixed', 'saved_owner_ready': True,
                            'clock_visible': True}
                if self.calls == 3:
                    return {'token': 'fixed', 'status': 'no_fix_observed'}
                self.now = 131
                return {'token': 'fixed', 'status': 'fix_observed',
                        'clock_advanced': True}
        recovered = Recovered()
        emitted = []
        self.assertEqual(operator.observe(recovered,
                                          emit=lambda value, **_kw: emitted.append(value),
                                          clock=lambda: recovered.now, token='fixed'),
                         {'first_status': 'no_fix_observed',
                          'second_status': 'fix_observed', 'clock_advanced': True})
        self.assertEqual([operator.json.loads(x)['next'] for x in emitted],
                         ['phone_ready', 'phone_retry', 'first_status', 'second_status'])
        class Cancelled:
            def read(self, _timeout):
                return {'token': 'fixed', 'cancelled': True}
        self.assertEqual(operator.observe(Cancelled(), emit=lambda *_args, **_kw: None,
                                          clock=lambda: 0, token='fixed'), 'cancelled')
        malformed = Lines({'coordinates': 'forbidden'})
        self.assertEqual(operator.observe(malformed,
                                          emit=lambda *_args, **_kw: None,
                                          clock=lambda: malformed.now, token='fixed'), 'unavailable')


if __name__ == '__main__':
    unittest.main()
