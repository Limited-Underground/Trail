"""No-device exact admission and shared restoration tests for corrected A."""
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gnss_observation_trial as engine
import run_connection_diagnostic_trial as operator
import connection_diagnostic_trial_tests as diagnostic

PROFILE = 'A_STACK_8192'
PINS = {
    'A': {'bytes': 593200, 'sha256': '4351080e407ae8bf33370737e9313ecfb8ec6865e5bb1ed076d2fd094bc85c72'},
    'B': {'bytes': 593232, 'sha256': '6db05a3453870dbcf6c4c9583cd66ef5c64f25bc5b06a7b4eb9b5ae74d161388'},
    PROFILE: {'bytes': 593200, 'sha256': '56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1'},
}


class ExactPinsTests(unittest.TestCase):
    def test_actual_corrected_descriptor_requires_distinct_profile(self):
        self.assertEqual(engine.CONNECTION_CANDIDATES, PINS)
        self.assertEqual(engine.CANDIDATE, {'bytes': 591456,
            'sha256': '2808d63b610619a69e14bd1809a56d921bded6efc2720fb08fd204aaaff9feb2'})
        request = {'schema': 'OT0101E-CONNECTION-REQUEST-1', 'profile': PROFILE,
            'runtime_sha256': '1' * 64, 'device_binding': '2' * 64,
            'candidate': PINS[PROFILE], 'protected': {
                'bootloader': {'bytes': 32768, 'sha256': '3' * 64},
                'partition': {'bytes': 4096, 'sha256': engine.PARTITION_SHA},
                'ota': {'bytes': 8192, 'sha256': engine.OTA_SHA}},
            'original_prefix': {'bytes': 589824, 'sha256': '4' * 64},
            'boot_compatibility': 'reviewed-ot0101e-image-header-compatibility',
            'case': 'capture-and-restore'}
        self.assertEqual(engine.validate_request(request), request)
        for update in ({'profile': 'A'}, {'profile': 'B'}, {'profile': 'unknown'},
                       {'candidate': PINS['A']}, {'candidate': PINS['B']}):
            with self.subTest(update=update), self.assertRaises(engine.TrialError):
                engine.validate_request(dict(request, **update))
        self.assertEqual(operator.OBSERVATION_SECONDS, 600)


class RetestEngineTests(diagnostic.EngineTests):
    def setUp(self):
        super().setUp()
        patch = mock.patch.dict(engine.CONNECTION_CANDIDATES,
                                {PROFILE: engine.descriptor(self.candidate)})
        patch.start()
        self.addCleanup(patch.stop)
        self.request.update(profile=PROFILE, runtime_sha256='3' * 64)

    def test_old_profile_grant_denied_before_claim(self):
        current = dict(self.request)
        self.request = dict(current, profile='A', runtime_sha256='1' * 64)
        old_grant = self.grant()
        self.request = current
        with self.assertRaises(engine.TrialError):
            engine.execute(self.root, current, *old_grant, self.candidate,
                           self.backend, lambda: None, utc=lambda: 150)
        self.assertEqual(self.backend.calls, [])

    def test_new_profile_remains_one_use(self):
        observed = {'schema': 'OT0101E-CONNECTION-OBSERVATION-1',
                    'result': 'timeout', 'capture': None}
        self.execute(lambda: observed)
        before = list(self.backend.calls)
        with self.assertRaises(engine.TrialError):
            self.execute(lambda: observed)
        self.assertEqual(self.backend.calls, before)
        self.assertEqual(self.backend.memory, self.originals)


class RetestBindingTests(diagnostic.BindingTests):
    def test_new_profile_binding_requires_its_exact_artifact(self):
        self.binding['profile'] = PROFILE
        with mock.patch.dict(engine.CONNECTION_CANDIDATES,
                             {PROFILE: engine.descriptor(b'SSS')}):
            with self.assertRaises(ValueError):
                self.verify()
            self.put('build/a.bin', b'SSS')
            self.assertEqual(self.verify()['profile'], PROFILE)
            self.binding['profile'] = 'A'
            with self.assertRaises(ValueError):
                self.verify()


if __name__ == '__main__':
    suite = unittest.TestSuite()
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(ExactPinsTests))
    # Existing composed cases execute with the new profile through the shared
    # production engine, including failure restoration and fresh recovery.
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(RetestEngineTests))
    suite.addTest(RetestBindingTests('test_new_profile_binding_requires_its_exact_artifact'))
    raise SystemExit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
