"""No-device exact-input and collector/restoration handoff regressions."""
from pathlib import Path
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import connection_diagnostic_capture as collector
import gnss_observation_trial as engine
import run_connection_diagnostic_trial as operator
import gnss_observation_trial_tests as legacy_tests


class EngineTests(unittest.TestCase):
    def setUp(self):
        legacy_tests.TrialTests.setUp(self)
        self.addCleanup(mock.patch.stopall)
        mock.patch.dict(engine.CONNECTION_CANDIDATES,
                        {'A': engine.descriptor(self.candidate),
                         'B': engine.descriptor(b'B' * 591456)}, clear=True).start()
        self.request.update(schema='OT0101E-CONNECTION-REQUEST-1', profile='A',
                            case='capture-and-restore')

    def grant(self, operation='execute', number=1):
        raw, _ = legacy_tests.TrialTests.grant(self, operation, number)
        grant = engine.decode(raw)
        grant['schema'] = 'OT0101E-CONNECTION-GRANT-1'
        grant['actions'] = engine.CONNECTION_ACTIONS if operation == 'execute' else engine.RECOVERY_ACTIONS
        raw = engine.canonical(grant)
        return raw, engine.sha(raw)

    def execute(self, observed):
        return engine.execute(self.root, self.request, *self.grant(), self.candidate,
                              self.backend, observed, utc=lambda: 150)

    def test_typed_capture_summary_restores_application_and_full_nvs(self):
        observed = {'schema': 'OT0101E-CONNECTION-OBSERVATION-1',
                    'result': 'protocol_info_failure', 'capture': engine.descriptor(b'{}\n')}
        def observation():
            self.backend.memory['nvs'] = b'\x00' * engine.SPANS['nvs'][1]
            return observed
        result = self.execute(observation)
        self.assertEqual(result['observation'], observed)
        self.assertEqual(self.backend.memory, self.originals)
        self.assertFalse((self.root / '.private' / engine.ACTIVE).exists())
        journal = engine.Journal(self.root, f'{1:032x}').load()
        self.assertIn('connection_capture_intent', journal['events'])
        self.assertIn('connection_observed', journal['events'])
        self.assertEqual(self.backend.calls[-1], 'original_reset')

    def test_capture_exception_restores_without_second_attempt(self):
        def failure():
            raise OSError('private exception text')
        result = self.execute(failure)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.memory, self.originals)
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)

    def test_old_grant_and_other_profile_are_rejected_before_claim(self):
        raw, _ = self.grant()
        for update in ({'schema': 'OT0101E-GNSS-GRANT-1'}, {'actions': engine.ACTIONS}):
            changed = engine.canonical(dict(engine.decode(raw), **update))
            with self.assertRaises(engine.TrialError):
                engine.execute(self.root, self.request, changed, engine.sha(changed),
                               self.candidate, self.backend, lambda: None, utc=lambda: 150)
        other = dict(self.request, profile='B', candidate=engine.CONNECTION_CANDIDATES['B'])
        with self.assertRaises(engine.TrialError):
            engine.execute(self.root, other, *self.grant(), b'B' * 591456,
                           self.backend, lambda: None, utc=lambda: 150)
        with self.assertRaises(engine.TrialError):
            engine.validate_request(dict(self.request, profile='B'))
        self.assertEqual(self.backend.calls, [])

    def test_unreleased_capture_blocks_restore_and_fresh_recovery_claim(self):
        backend = self.backend
        guarded = operator.CaptureCustody(backend)
        self.backend = guarded
        def observe():
            guarded.serial_released = False
            raise OSError('collector did not reap')
        with self.assertRaises(engine.TrialError):
            self.execute(observe)
        self.assertEqual([x for x in backend.calls if x.startswith('write_')], ['write_application'])
        def still_live():
            raise ValueError('collector_live')
        fresh = operator.CaptureCustody(backend, released_check=still_live)
        before = list(backend.calls)
        with self.assertRaises(engine.TrialError):
            engine.recover(self.root, self.request, *self.grant('recover', 2), fresh, utc=lambda: 150)
        self.assertEqual(backend.calls, before)
        self.assertFalse(fresh.serial_released)
        self.assertTrue((self.root / '.private' / engine.ACTIVE).exists())
        fresh = operator.CaptureCustody(backend, released_check=lambda: None)
        result = engine.recover(self.root, self.request, *self.grant('recover', 3), fresh, utc=lambda: 150)
        self.assertTrue(result['restored'])
        self.assertEqual(backend.memory, self.originals)

    def test_unbounded_or_untyped_capture_summary_is_not_journaled(self):
        for observed in ({'schema': 'OT0101E-CONNECTION-OBSERVATION-1', 'result': 'not_ready', 'capture': None},
                         {'schema': 'OT0101E-CONNECTION-OBSERVATION-1', 'result': 'timeout',
                          'capture': {'bytes': 2097153, 'sha256': '0' * 64}},
                         {'schema': 'OT0101E-CONNECTION-OBSERVATION-1', 'result': 'timeout',
                          'capture': None, 'raw': 'private secret'}):
            with self.assertRaises(engine.TrialError):
                engine.validate_connection_observation(observed)


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.binding_path = self.root / '.private' / 'binding.json'
        self.binding_path.parent.mkdir()
        self.files = {}
        for name in operator.REQUIRED_FILES:
            self.put(name, name.encode())
        self.put('.private/registry.json', b'{"devices":[]}')
        self.put('.private/app.apk', b'exact app')
        self.put('build/a.bin', b'AAA')
        self.put('.private/runtime.json', b'{}')
        self.binding = {'schema': 'OT0101E-CONNECTION-OPERATOR-BINDING-1',
            'profile': 'A', 'candidate': 'build/a.bin', 'registry': '.private/registry.json',
            'runtime_manifest': '.private/runtime.json', 'runtime_sha256': self.files['.private/runtime.json'],
            'apk': {'path': '.private/app.apk', 'bytes': 9, 'sha256': self.files['.private/app.apk'],
                    'signer_sha256': 'a' * 64}, 'files': self.files}
        self.pins = mock.patch.dict(engine.CONNECTION_CANDIDATES,
                                   {'A': engine.descriptor(b'AAA'), 'B': engine.descriptor(b'BBB')})
        self.pins.start()
        self.addCleanup(self.pins.stop)

    def put(self, name, raw):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        self.files[name] = engine.sha(raw)

    def verify(self, recovery=False):
        raw = engine.canonical(self.binding)
        self.binding_path.write_bytes(raw)
        return operator.verify_binding(self.root, self.binding_path, engine.sha(raw), recovery)

    def test_exact_binding_and_profile_pin(self):
        self.assertEqual(self.verify()['profile'], 'A')
        self.binding['profile'] = 'B'
        with self.assertRaises(ValueError):
            self.verify()

    def test_actual_loaded_capture_dependencies_are_in_the_binding_inventory(self):
        operator.modules()
        loaded = set()
        for module in tuple(sys.modules.values()):
            filename = getattr(module, '__file__', None)
            if filename:
                path = Path(filename).resolve()
                if path.is_relative_to(ROOT / 'tools'):
                    loaded.add(path.relative_to(ROOT).as_posix())
        self.assertLessEqual(loaded, operator.REQUIRED_FILES)
        for name in ('tools/security_policy_bundle.py', 'tools/security_policy_capture.py'):
            self.assertIn(name, loaded)
            digest = self.files.pop(name)
            with self.assertRaises(ValueError):
                self.verify()
            self.files[name] = digest

    def test_verified_relative_binding_survives_a_different_worker_directory(self):
        binding = self.verify()
        digest = engine.sha(self.binding_path.read_bytes())
        original_cwd = Path.cwd()
        try:
            os.chdir(self.root)
            raw = operator.worker_payload(self.root, Path('.private/binding.json'), digest,
                                          binding, {'root': str(self.root / 'capsule')}, mode='probe')
        finally:
            os.chdir(original_cwd)
        capsule = self.root / 'capsule'
        capsule.mkdir()
        checked = subprocess.run([sys.executable, '-I', '-S', '-B', '-c',
            'import hashlib,json,pathlib,sys; q=json.load(sys.stdin); p=pathlib.Path(q["binding"]); '
            'print(p.is_absolute() and hashlib.sha256(p.read_bytes()).hexdigest()==q["binding_sha256"])'],
            input=raw, cwd=capsule, capture_output=True, timeout=10, check=False)
        self.assertEqual(checked.returncode, 0)
        self.assertEqual(checked.stdout.strip(), b'True')
        self.assertEqual(checked.stderr, b'')

    def test_actual_apk_bytes_and_transitive_collector_are_bound(self):
        (self.root / '.private/app.apk').write_bytes(b'other app')
        with self.assertRaises(ValueError):
            self.verify()
        (self.root / '.private/app.apk').write_bytes(b'exact app')
        self.binding['apk']['bytes'] += 1
        with self.assertRaises(ValueError):
            self.verify()
        self.binding['apk']['bytes'] -= 1
        (self.root / 'tools/gnss_counter_capture.py').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            self.verify()

    def test_recovery_needs_runtime_and_helpers_but_not_candidate_or_phone(self):
        for name in ('build/a.bin', '.private/app.apk', '.private/registry.json'):
            (self.root / name).unlink()
        self.verify(recovery=True)
        with self.assertRaises(FileNotFoundError):
            self.verify()
        (self.root / 'tools/connection_capture_custody.py').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            self.verify(recovery=True)


class Pipe:
    def __init__(self):
        self.items = queue.Queue()
        self.closed = False

    def readline(self, _limit):
        return self.items.get(timeout=5)

    def close(self):
        self.closed = True


class FakeProcess:
    def __init__(self, root, mode, trace):
        self.root, self.mode, self.trace = root, mode, trace
        self.stdin = self
        self.stdout, self.stderr = Pipe(), Pipe()
        self.returncode = None
        self.payload = None
        self.assigned = False

    def write(self, value):
        if self.payload is None:
            if not self.assigned:
                raise AssertionError('payload before lifetime assignment')
            self.trace.append('payload')
            self.payload = json.loads(value)
            if self.mode == 'failed_open':
                self.finish('open_error', armed=False)
            elif self.mode == 'oversized':
                self.stderr.items.put(b'S' * 4097)
            elif self.mode != 'hung':
                self.stderr.items.put(b'CONNECTION_CAPTURE_ARMED\n')
                if self.mode in ('natural', 'abnormal'):
                    self.finish('window_complete', code=7 if self.mode == 'abnormal' else 0)
        else:
            if value != b'STOP\n':
                raise AssertionError('unexpected control')
            self.trace.append('stop')
            self.finish('operator_stop')

    def finish(self, reason, *, armed=True, code=0):
        parser = collector.DiagnosticCapture(5)
        parser.feed(b'I (12) ot_bench: conn_diag event k=1 t=12 a=0 b=0\n', 20)
        result = parser.finish(5000 if reason == 'window_complete' else 30, reason)
        raw = engine.canonical(result) + b'\n'
        path = engine.path(self.root, 'connection-capture-' + self.payload['attempt'] + '.json')
        path.write_bytes(raw)
        self.stdout.items.put(json.dumps({'capture': engine.descriptor(raw)}).encode() + b'\n')
        self.returncode = code
        self.stdout.items.put(b'')
        self.stderr.items.put(b'')

    def flush(self):
        pass

    def poll(self):
        return self.returncode

    def kill(self):
        self.trace.append('kill')
        self.returncode = -9
        self.stdout.items.put(b'')
        self.stderr.items.put(b'')

    def wait(self, timeout):
        self.trace.append('reap')
        if self.mode == 'unreaped':
            raise subprocess.TimeoutExpired('private', timeout)
        return self.returncode

    def close(self):
        self.trace.append('stdin_close')


class FakeLifetime:
    def __init__(self, trace, fail=False):
        self.trace, self.fail = trace, fail

    def create_and_assign(self, root, attempt, child):
        self.trace.append('assigned')
        if self.fail:
            raise ValueError('private assignment details')
        child.assigned = True
        return self

    def close(self):
        self.trace.append('job_close')

    def require_released(self, root, attempt):
        self.trace.append('released_check')


class ObservationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / '.private').mkdir()
        self.trace, self.emitted = [], []
        self.phone_calls = 0
        self.custody = operator.CaptureCustody(mock.Mock())
        self.lifetime = FakeLifetime(self.trace)
        self.mode = 'normal'
        self.phone_result = 'cancelled'

    def emit(self, value, **kwargs):
        self.emitted.append(json.loads(value))
        self.trace.append('armed_prompt')

    def read(self, timeout):
        self.phone_calls += 1
        self.trace.append('phone_read')
        if self.mode in ('natural', 'abnormal'):
            # No phone result in these cases; the daemon exits after observation.
            threading.Event().wait(0.3)
            raise queue.Empty()
        return {'token': self.emitted[-1]['observation_token'], 'result': self.phone_result}

    def popen(self, *args, **kwargs):
        self.process = FakeProcess(self.root, self.mode, self.trace)
        return self.process

    def observe(self):
        return operator.observe(self.root, self.root / '.private/binding.json', 'a' * 64,
            {'files': {'tools/run_connection_diagnostic_trial.py': 'b' * 64}},
            {'root': str(self.root)}, 'c' * 32, '001122334455', self.custody, self,
            emit=self.emit, popen=self.popen, lifetime=self.lifetime)

    def test_arm_before_phone_one_result_close_and_reap_before_rom(self):
        result = self.observe()
        self.assertEqual(result['result'], 'cancelled')
        self.assertIsNotNone(result['capture'])
        self.assertEqual(self.phone_calls, 1)
        self.assertEqual(len(self.emitted), 1)
        self.assertLess(self.trace.index('assigned'), self.trace.index('payload'))
        self.assertLess(self.trace.index('armed_prompt'), self.trace.index('phone_read'))
        self.assertLess(self.trace.index('stop'), self.trace.index('reap'))
        self.assertLess(self.trace.index('reap'), self.trace.index('released_check'))
        self.custody.reverify('binding')
        self.custody.backend.reverify.assert_called_once()

    def test_terminal_result_keeps_short_tail_inside_same_window(self):
        self.phone_result = 'protocol_info_failure'
        result = self.observe()
        self.assertEqual(result['result'], 'protocol_info_failure')
        self.assertEqual(self.phone_calls, 1)
        self.assertEqual(self.trace.count('stop'), 1)

    def test_natural_armed_window_completion_without_phone_is_timeout(self):
        self.mode = 'natural'
        result = self.observe()
        self.assertEqual(result['result'], 'timeout')
        self.assertIsNotNone(result['capture'])
        self.assertNotIn('stop', self.trace)
        self.assertEqual(self.emitted, [])  # Queued ARM after child exit is stale.
        self.assertEqual(self.phone_calls, 0)

    def test_valid_capture_does_not_hide_abnormal_worker_exit(self):
        self.mode = 'abnormal'
        self.assertEqual(self.observe()['result'], 'capture_failed')

    def test_failed_open_never_invites_phone_or_retries(self):
        self.mode = 'failed_open'
        self.assertEqual(self.observe()['result'], 'capture_failed')
        self.assertEqual(self.phone_calls, 0)
        self.assertEqual(self.emitted, [])
        self.assertEqual(self.trace.count('payload'), 1)

    def test_watchdog_kills_and_reaps_before_releasing_custody(self):
        self.mode = 'hung'
        with mock.patch.object(operator, 'OBSERVATION_SECONDS', 0.04):
            result = self.observe()
        self.assertEqual(result['result'], 'timeout')
        self.assertIsNone(result['capture'])
        self.assertEqual(self.phone_calls, 0)
        self.assertLess(self.trace.index('kill'), self.trace.index('reap'))
        self.assertTrue(self.custody.serial_released)

    def test_unreaped_child_leaves_rom_calls_barred(self):
        self.mode = 'unreaped'
        with self.assertRaises(subprocess.TimeoutExpired):
            self.observe()
        self.assertFalse(self.custody.serial_released)
        with self.assertRaises(ValueError):
            self.custody.reverify('binding')
        self.custody.backend.reverify.assert_not_called()

    def test_oversized_child_output_is_bounded_and_sanitized(self):
        self.mode = 'oversized'
        result = self.observe()
        self.assertEqual(result['result'], 'capture_failed')
        self.assertNotIn('SSS', json.dumps(result))
        self.assertEqual(self.phone_calls, 0)
        self.assertIn('reap', self.trace)

    def test_failed_job_assignment_never_sends_bootstrap_payload(self):
        self.lifetime.fail = True
        result = self.observe()
        self.assertEqual(result['result'], 'capture_failed')
        self.assertNotIn('payload', self.trace)
        self.assertIn('kill', self.trace)
        self.assertIn('reap', self.trace)
        self.assertEqual(self.phone_calls, 0)

    def test_capture_descriptor_and_pre_read_size_are_checked(self):
        path = engine.path(self.root, 'connection-capture-' + 'a' * 32 + '.json')
        path.write_bytes(b'{}')
        with self.assertRaises(ValueError):
            operator.read_capture(self.root, 'a' * 32, {'capture': engine.descriptor(b'x')})
        path.write_bytes(b'x' * (operator.MAX_CAPTURE_BYTES + 1))
        with self.assertRaises(ValueError):
            operator.read_capture(self.root, 'a' * 32, {'capture': engine.descriptor(b'{}')})


if __name__ == '__main__':
    unittest.main()
