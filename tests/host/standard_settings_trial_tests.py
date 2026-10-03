"""Actual combined engine with inert physical/phone I/O doubles only."""
from pathlib import Path
import base64
import importlib.util
import json
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests' / 'host'))
import standard_settings_trial as trial
from ble_confirmation_trial_tests import namespace_image

PRODUCTION_CANDIDATE = dict(trial.CANDIDATE)
PRODUCTION_EXPECTED = dict(trial.EXPECTED)


def idle():
    return {'closed':True,'evidence_sha256':'b'*64}


class Backend:
    """No automatic success: mutate before failing, enforce complete custody."""
    def __init__(self, original):
        self.memory = dict(original)
        self.originals = None
        self.operations = []
        self.reads = []
        self.fail_read = None
        self.repeat_mismatch = None
        self.fail_write = None
        self.write_count = 0
        self.fail_boot = False
        self.fail_reset = False
        self.corrupt_final = None
        self.after_reset = None
        self.changed_identity = False
        self.after_candidate_verify = None
        self.closed = True
        self.recovery = False

    def claim(self, binding):
        self.operations.append('claim')
        return True

    def reverify(self, binding):
        self.operations.append('verify')
        assert self.closed, 'ROM must never race a live observation'
        return not self.changed_identity

    def _read(self, offset, size, state=False, bound=False):
        name = next(name for name, span in trial.SPANS.items() if span == (offset, size))
        assert name.startswith('state_') == state
        self.reads.append(name)
        if self.fail_read == (name, self.reads.count(name)):
            raise KeyboardInterrupt()
        raw = self.memory[name]
        if self.repeat_mismatch == name and self.reads.count(name) == 2:
            return bytes([raw[0] ^ 1]) + raw[1:]
        return raw

    def read(self, offset, size):
        return self._read(offset, size)

    def read_state_chunk(self, offset, size):
        assert self.originals is None
        return self._read(offset, size, True)

    def read_recovery_state_chunk(self, offset, size):
        assert self.originals is None
        self.operations.append('recovery_state_read')
        return self._read(offset, size, True)

    def read_bound_state_chunk(self, offset, size):
        assert self.originals is not None
        return self._read(offset, size, True, True)

    def bind_complete_originals(self, captured):
        assert set(captured) == set(trial.SPANS)
        if self.originals is not None:
            assert self.recovery and self.originals == captured, 'normal binding is one shot'
        self.originals = dict(captured)
        self.operations.append('bind_complete')
        return True

    def bind_complete_recovery_originals(self, captured):
        self.recovery = True
        return self.bind_complete_originals(captured)

    def _write(self, offset, raw, state):
        name = next(name for name, span in trial.SPANS.items() if span[0] == offset)
        assert self.originals is not None
        assert name in trial.RESTORABLE and name.startswith('state_') == state
        assert not self.recovery or raw == self.originals[name]
        if state:
            assert raw == self.originals[name]
        self.operations.append('write_' + name)
        self.write_count += 1
        self.memory[name] = raw
        if self.fail_write == self.write_count:
            raise OSError('private mutation failure')
        return True

    def write(self, offset, raw):
        return self._write(offset, raw, False)

    def restore_state_chunk(self, offset, raw):
        return self._write(offset, raw, True)

    def boot_candidate(self, *, before_boot):
        assert self.originals is not None and not self.recovery
        if self.after_candidate_verify:
            self.after_candidate_verify()
        before_boot()
        self.operations.append('candidate_boot')
        if self.fail_boot:
            raise TimeoutError('private boot detail')
        return True

    def reset_complete_original(self, *, before_reset):
        assert self.closed
        self.operations.append('final_sweep')
        if self.corrupt_final:
            self.memory[self.corrupt_final] = b'X' + self.memory[self.corrupt_final][1:]
        for name in trial.SPANS:
            assert self.memory[name] == self.originals[name]
        before_reset()
        self.operations.append('original_boot')
        if self.after_reset:
            self.after_reset()
        if self.fail_reset:
            raise TimeoutError('uncertain original restart')
        return True


class Observer:
    def __init__(self, backend):
        self.backend = backend
        self.contexts = []
        self.result = 'passed'
        self.raise_error = None
        self.closed_result = True
        self.close_result = True
        self.evidence = 'e' * 64
        self.effect = None
        self.close_calls = 0

    def __call__(self, context):
        self.contexts.append(context)
        self.backend.closed = False
        if self.effect:
            self.effect()
        if self.raise_error:
            raise self.raise_error
        self.backend.closed = self.closed_result
        return {'result': self.result, 'closed': self.closed_result,
                'challenge': context['challenge'], 'evidence_sha256': self.evidence}

    def close(self):
        self.close_calls += 1
        self.backend.closed = self.close_result
        return {'closed': self.close_result, 'evidence_sha256': 'f' * 64}


class EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.candidate = b'C' * 602096
        cls.original = {name: bytes([65 + index]) * size
                        for index, (name, (_, size)) in enumerate(trial.SPANS.items())}
        cls.original['ota'] = b'\xff' * 8192
        cls.original['nvs'] = b'\xff' * 12288

    def setUp(self):
        self.pins = mock.patch.multiple(trial, CANDIDATE=trial.descriptor(self.candidate),
            EXPECTED={name: trial.sha(self.original[name]) for name in trial.EXPECTED})
        self.pins.start()
        self.addCleanup(self.pins.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / '.private').mkdir()
        self.request = {'schema': 'OT0304-STANDARD-REQUEST-1', 'runtime_sha256': '1' * 64,
            'device_binding': '2' * 64, 'candidate': dict(trial.CANDIDATE),
            'protected': {name: trial.descriptor(self.original[name]) for name in trial.PROTECTED},
            'original_prefix': trial.descriptor(self.original['application']),
            'boot_compatibility': 'inferred-ot208-image-header-compatibility',
            'case': 'standard-settings-read-and-restore'}
        self.backend = Backend(self.original)
        self.observer = Observer(self.backend)
        self.clock = 150

    def grant(self, operation='execute', attempt=1, origin=1):
        obj = {'schema': 'OT0304-STANDARD-GRANT-1', 'attempt': f'{attempt:032x}',
            'request_sha256': trial.sha(trial.canonical(self.request)),
            'runtime_sha256': self.request['runtime_sha256'], 'operation': operation,
            'origin_attempt': f'{origin:032x}' if operation == 'recover' else None,
            'attempt_count': 1, 'actions': trial.ACTIONS if operation == 'execute' else trial.RECOVERY_ACTIONS,
            'issued_utc': 100, 'expires_utc': 200}
        raw = trial.canonical(obj)
        return raw, trial.sha(raw)

    def execute(self, **kwargs):
        kwargs.setdefault('initial_phone_idle',idle)
        return trial.execute(self.root, self.request, *self.grant(), self.candidate,
            self.backend, self.observer, utc=lambda: self.clock,
            challenge_factory=lambda: 'c' * 32, **kwargs)

    def recover(self, **kwargs):
        kwargs.setdefault('close_observation',self.observer.close)
        return trial.recover(self.root, self.request, *self.grant('recover', kwargs.pop('attempt', 2)),
            self.backend, utc=lambda: self.clock, **kwargs)

    def state_path(self):
        return self.root / '.private' / (trial.PREFIX + f'{1:032x}' + '.json')

    def state(self):
        return trial.decode(self.state_path().read_bytes())

    def held(self):
        return (self.root / '.private' / trial.ACTIVE).exists()

    def test_complete_capture_candidate_padding_restore_and_single_original_boot(self):
        self.observer.effect = lambda: self.backend.memory.update(
            nvs=b'N' * 12288, state_0=b'0' * 524288, state_1=b'1' * 524288)
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation'], 'passed')
        self.assertEqual(result['runtime_profile_capabilities'], 'unknown')
        self.assertEqual(self.backend.memory, self.original)
        self.assertFalse(self.held())
        self.assertEqual(self.backend.operations.count('original_boot'), 1)
        self.assertLess(self.backend.operations.index('candidate_boot'), self.backend.operations.index('original_boot'))
        for name in trial.SPANS:
            self.assertGreaterEqual(self.backend.reads.count(name), 2)
            self.assertEqual((self.root / '.private' / (trial.PREFIX + f'{1:032x}' + '-' + name + '.bin')).read_bytes(), self.original[name])
        state = self.state()
        self.assertEqual(state['state_aggregate'], trial.descriptor(self.original['state_0'] + self.original['state_1']))
        self.assertEqual(state['challenge'], 'c' * 32)
        self.assertEqual(state['evidence_sha256'], 'e' * 64)
        self.assertEqual(state['events'][-3:], ['original_boot_intent', 'original_booted', 'closed'])

    def test_unchanged_nvs_and_state_are_compared_without_restore_writes(self):
        self.execute()
        self.assertEqual([op for op in self.backend.operations if op.startswith('write_')],
                         ['write_application', 'write_application'])

    def test_failed_and_unavailable_observations_restore_separately(self):
        self.observer.result = 'failed'
        result = self.execute()
        self.assertEqual(result['observation'], 'failed')
        self.assertEqual(result['failure'], 'none')
        self.assertTrue(result['restored'])

    def test_pinned_identical_originals_reuse_after_fresh_doubled_equality(self):
        sources={}
        for name in (*trial.PROTECTED,'application'):
            source=self.root/'.private'/('prior-'+name+'.bin')
            source.write_bytes(self.original[name])
            sources[name]={'path':str(source),**trial.descriptor(self.original[name])}
        self.request['original_sources']=sources
        self.execute()
        state=self.state()
        for name in sources:
            self.assertEqual(state['sources'][name],sources[name]['path'])
            self.assertGreaterEqual(self.backend.reads.count(name),2)
            self.assertFalse((self.root/'.private'/(trial.PREFIX+f'{1:032x}'+'-'+name+'.bin')).exists())

    def test_old_nvs_and_state_are_reused_only_after_fresh_equality_otherwise_snapshotted(self):
        sources={}
        for name in ('nvs','state_0','state_1'):
            source=self.root/'.private'/('prior-'+name+'.bin')
            raw=self.original[name] if name=='state_0' else b'X'*trial.SPANS[name][1]
            source.write_bytes(raw)
            sources[name]={'path':str(source),**trial.descriptor(raw)}
        self.request['original_sources']=sources
        self.execute()
        state=self.state()
        self.assertEqual(state['sources']['state_0'],sources['state_0']['path'])
        for name in ('nvs','state_1'):
            self.assertNotEqual(state['sources'][name],sources[name]['path'])
            self.assertEqual(Path(state['sources'][name]).read_bytes(),self.original[name])

    def test_changed_reused_reference_refuses_recovery_before_claim(self):
        source=self.root/'.private'/'prior-application.bin'
        source.write_bytes(self.original['application'])
        self.request['original_sources']={'application':{'path':str(source),**trial.descriptor(self.original['application'])}}
        self.backend.fail_write=2
        with self.assertRaises(trial.TrialError):
            self.execute()
        source.write_bytes(b'X'*trial.SPANS['application'][1])
        before=list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertEqual(self.backend.operations,before)

    def test_nonprivate_relative_or_wrong_size_source_requests_refuse(self):
        for source in ('relative.bin',str(self.root/'outside.bin')):
            request=dict(self.request,original_sources={'application':{'path':source,**trial.descriptor(self.original['application'])}})
            with self.assertRaises(trial.TrialError):
                trial.validate_request(request)

    def test_private_dotdot_escape_source_is_refused_before_claim(self):
        source=str(self.root/'.private'/'..'/'..'/'outside.bin')
        self.request['original_sources']={'application':{'path':source,**trial.descriptor(self.original['application'])}}
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertEqual(self.backend.operations,[])

    def test_initial_phone_idle_missing_declined_or_exception_never_claims_rom(self):
        for callback in (None,lambda:{'closed':False,'evidence_sha256':'b'*64},
                         lambda:(_ for _ in ()).throw(KeyboardInterrupt())):
            with self.subTest(callback=callback),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);(root/'.private').mkdir();backend=Backend(self.original)
                with self.assertRaises(trial.TrialError):
                    trial.execute(root,self.request,*self.grant(),self.candidate,backend,Observer(backend),
                                  utc=lambda:150,initial_phone_idle=callback)
                self.assertEqual(backend.operations,[])

    def test_initial_phone_idle_proof_is_durable_and_grant_rechecked(self):
        def late_idle():
            self.clock=200
            return idle()
        with self.assertRaises(trial.TrialError):
            self.execute(initial_phone_idle=late_idle)
        self.assertEqual(self.backend.operations,[])
        self.assertEqual(self.state()['preflight_evidence_sha256'],'b'*64)

    def test_fresh_recovery_requires_current_phone_idle_even_before_observation(self):
        self.backend.fail_read=('nvs',1)
        with self.assertRaises(trial.TrialError):
            self.execute()
        before=list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.recover(close_observation=None)
        self.assertEqual(self.backend.operations,before)

    def actual_complete_transport(self, fail_original_boot=False):
        import ble_confirmation_trial_transport as transport
        memory={span:self.original[name] for name,span in trial.SPANS.items()}
        calls=[]
        def run_child(argv,**kwargs):
            request=json.loads(kwargs['input']);calls.append(request['operation'])
            reply={'ok':True}
            if request['operation'] in ('read','read_state','read_bound_state','read_recovery_state'):
                reply['data']=base64.b64encode(memory[request['offset'],request['size']]).decode()
            if request['operation'] in ('write','restore_state'):
                memory[request['offset'],request['size']]=base64.b64decode(request['data'])
            if fail_original_boot and request['operation']=='restart' and calls.count('restart')==2:
                # An original boot can advance storage before its reply is lost.
                memory[trial.SPANS['nvs']]=b'P'*12288
                return SimpleNamespace(returncode=1,stdout=b'{"ok":false}',stderr=b'')
            return SimpleNamespace(returncode=0,stdout=json.dumps(reply).encode(),stderr=b'')
        key=b'k'*32
        binding=transport.opaque_identity(key,'010203040506')
        self.request['device_binding']=binding
        backend=transport.CompleteCustodyTransport(manifest_path=self.root/'.private'/'manifest.json',
            manifest_sha256='a'*64,private_root=self.root/'.private',route='COM7',
            expected_identity='010203040506',opaque_binding=binding,binding_key=key,
            candidate=self.candidate+b'\xff'*(733184-len(self.candidate)),subprocess_run=run_child,
            manifest_verifier=lambda *_:{'root':str(self.root/'.private'/'capsule'),'worktree':str(self.root)})
        return backend,memory,calls

    def test_actual_complete_transport_composes_without_normal_rebinding(self):
        backend,memory,calls=self.actual_complete_transport()
        def observe(context):
            memory[trial.SPANS['nvs']]=b'N'*12288
            memory[trial.SPANS['state_0']]=b'0'*524288
            return {'result':'passed','closed':True,'challenge':context['challenge'],'evidence_sha256':'e'*64}
        result=trial.execute(self.root,self.request,*self.grant(),self.candidate,backend,observe,
                             utc=lambda:150,initial_phone_idle=idle)
        self.assertTrue(result['restored'])
        self.assertEqual(memory,{span:self.original[name] for name,span in trial.SPANS.items()})
        self.assertEqual(calls.count('restart'),2)
        self.assertIn('restore_state',calls)

    def test_actual_complete_transport_uncertain_boot_blocks_engine_recovery_before_worker(self):
        backend,memory,calls=self.actual_complete_transport(fail_original_boot=True)
        def observe(context):
            return {'result':'passed','closed':True,'challenge':context['challenge'],'evidence_sha256':'e'*64}
        with self.assertRaises(trial.TrialError):
            trial.execute(self.root,self.request,*self.grant(),self.candidate,backend,observe,
                          utc=lambda:150,initial_phone_idle=idle)
        before=list(calls)
        with self.assertRaisesRegex(trial.TrialError,'original_boot_reconciliation_required'):
            trial.recover(self.root,self.request,*self.grant('recover',2),backend,utc=lambda:150,
                          close_observation=idle)
        self.assertEqual(calls,before)
        self.assertEqual(memory[trial.SPANS['nvs']],b'P'*12288)

    def test_production_pins_and_inert_import(self):
        self.assertEqual(PRODUCTION_CANDIDATE['bytes'], 602096)
        self.assertEqual(PRODUCTION_CANDIDATE['sha256'], '33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193')
        self.assertEqual(trial.SPANS['application'], (0x10000, 733184))
        self.assertEqual(trial.SPANS['state_0'], (0xf00000, 524288))
        with mock.patch('subprocess.run', side_effect=AssertionError('device access')):
            spec = importlib.util.spec_from_file_location('inert_standard_trial', ROOT / 'tools' / 'standard_settings_trial.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        self.assertEqual(module.EXPECTED, PRODUCTION_EXPECTED)

    def test_candidate_drift_request_drift_and_expiry_refuse_before_claim(self):
        for change in ('candidate', 'request', 'expiry'):
            with self.subTest(change=change):
                if change == 'candidate':
                    candidate = b'X' + self.candidate[1:]
                    with self.assertRaises(trial.TrialError):
                        trial.execute(self.root, self.request, *self.grant(), candidate, self.backend, self.observer, utc=lambda:150,initial_phone_idle=idle)
                elif change == 'request':
                    request = dict(self.request, device_binding=False)
                    with self.assertRaises(trial.TrialError):
                        trial.execute(self.root, request, *self.grant(), self.candidate, self.backend, self.observer, utc=lambda:150,initial_phone_idle=idle)
                else:
                    self.clock = 200
                    with self.assertRaises(trial.TrialError):
                        self.execute()
                self.assertEqual(self.backend.operations, [])

    def test_grant_unknown_fields_boolean_counts_duplicate_json_and_wrong_actions_refuse(self):
        raw, _ = self.grant()
        value = trial.decode(raw)
        for edited in (dict(value, attempt_count=True), dict(value, actions=trial.RECOVERY_ACTIONS),
                       dict(value, private_payload='forbidden'), dict(value, origin_attempt='3'*32)):
            raw = trial.canonical(edited)
            with self.assertRaises(trial.TrialError):
                trial.validate_grant(self.request, raw, trial.sha(raw), 'execute', lambda:150)
        raw = b'{"schema":"x","schema":"y"}'
        with self.assertRaises(trial.TrialError):
            trial.validate_grant(self.request, raw, trial.sha(raw), 'execute', lambda:150)

    def test_each_double_capture_mismatch_holds_without_candidate(self):
        for name in trial.SPANS:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                root = Path(folder); (root / '.private').mkdir()
                backend = Backend(self.original); backend.repeat_mismatch = name
                with self.assertRaisesRegex(trial.TrialError, 'custody_held_prewrite'):
                    trial.execute(root, self.request, *self.grant(), self.candidate, backend, Observer(backend), utc=lambda:150,initial_phone_idle=idle)
                self.assertFalse(any(op.startswith('write_') or op.endswith('_boot') for op in backend.operations))

    def test_known_protected_application_and_reset_mismatches_stop_later_capture(self):
        for name in (*trial.PROTECTED,'application','nvs'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);(root/'.private').mkdir()
                backend=Backend(self.original)
                backend.memory[name]=(namespace_image(b'ot_reset_v1',b'intent_v1') if name=='nvs'
                    else b'X'+backend.memory[name][1:])
                with self.assertRaises(trial.TrialError):
                    trial.execute(root,self.request,*self.grant(),self.candidate,backend,Observer(backend),utc=lambda:150,initial_phone_idle=idle)
                self.assertEqual(backend.reads[-1],name)
                self.assertNotIn('state_0',backend.reads)

    def test_each_capture_interruption_recovery_completes_without_writes(self):
        for name in trial.SPANS:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                root = Path(folder); (root / '.private').mkdir()
                backend = Backend(self.original); backend.fail_read = (name,1)
                with self.assertRaisesRegex(trial.TrialError, 'custody_held_prewrite'):
                    trial.execute(root, self.request, *self.grant(), self.candidate, backend, Observer(backend), utc=lambda:150,initial_phone_idle=idle)
                backend.fail_read = None
                result = trial.recover(root, self.request, *self.grant('recover',2), backend, utc=lambda:150,close_observation=idle)
                self.assertTrue(result['restored'])
                self.assertFalse(any(op.startswith('write_') for op in backend.operations))
                self.assertEqual(backend.operations.count('original_boot'),1)

    def test_prewrite_deadline_after_capture_releases_exact_original_without_flash_write(self):
        original_bind = self.backend.bind_complete_originals
        def late_bind(captured):
            result = original_bind(captured); self.clock = 200; return result
        self.backend.bind_complete_originals = late_bind
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['failure'], 'candidate_failed')
        self.assertFalse(any(op.startswith('write_') for op in self.backend.operations))
        self.assertNotIn('candidate_boot', self.backend.operations)

    def test_deadline_at_actual_boot_effect_restores_candidate_without_boot(self):
        self.backend.after_candidate_verify = lambda: setattr(self, 'clock', 200)
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertNotIn('candidate_boot', self.backend.operations)
        self.assertEqual(len(self.observer.contexts),0)

    def test_ambiguous_candidate_write_and_boot_restore_once_without_observe(self):
        self.backend.fail_write = 1
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['failure'], 'candidate_failed')
        self.assertEqual(len(self.observer.contexts),0)
        self.assertEqual(self.backend.memory,self.original)

    def test_ambiguous_candidate_boot_restores_without_observe(self):
        self.backend.fail_boot=True
        result=self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['failure'],'candidate_failed')
        self.assertEqual(len(self.observer.contexts),0)

    def test_all_restore_write_interruptions_hold_then_fresh_exact_recovery(self):
        for failed_write in (2,3,4,5):
            with self.subTest(failed_write=failed_write),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);(root/'.private').mkdir()
                backend=Backend(self.original);observer=Observer(backend)
                observer.effect=lambda:backend.memory.update(nvs=b'N'*12288,state_0=b'0'*524288,state_1=b'1'*524288)
                backend.fail_write=failed_write
                with self.assertRaises(trial.TrialError):
                    trial.execute(root,self.request,*self.grant(),self.candidate,backend,observer,utc=lambda:150,initial_phone_idle=idle)
                backend.fail_write=None;backend.originals=None
                candidateboots=backend.operations.count('candidate_boot')
                self.assertTrue(trial.recover(root,self.request,*self.grant('recover',2),backend,utc=lambda:150,close_observation=idle)['restored'])
                self.assertEqual(backend.memory,self.original)
                self.assertEqual(backend.operations.count('candidate_boot'),candidateboots)

    def test_each_changed_state_half_restores_exact_only(self):
        self.observer.effect = lambda: self.backend.memory.update(state_1=b'X'*524288)
        self.execute()
        self.assertIn('write_state_1',self.backend.operations)
        self.assertNotIn('write_state_0',self.backend.operations)

    def test_interrupted_observation_requires_proven_close_before_any_rom(self):
        self.observer.raise_error = KeyboardInterrupt()
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(self.observer.close_calls,1)
        self.assertEqual(result['failure'],'observation_failed')
        self.assertEqual(result['observation'],'not_observed')

    def test_unknown_observation_closure_holds_no_rom_then_fresh_close_recovery(self):
        self.observer.raise_error = KeyboardInterrupt()
        self.observer.close_result = False
        with self.assertRaisesRegex(trial.TrialError,'observation_closure_required'):
            self.execute()
        self.assertTrue(self.held())
        before = list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertEqual(self.backend.operations,before)
        self.observer.close_result = True
        self.assertTrue(self.recover(attempt=3,close_observation=self.observer.close)['restored'])

    def test_observation_wrong_challenge_or_evidence_cannot_pass(self):
        self.observer.evidence = 'private identifier'
        result = self.execute()
        self.assertEqual(result['observation'],'not_observed')
        self.assertEqual(result['failure'],'observation_failed')
        self.assertNotIn(b'private identifier',self.state_path().read_bytes())

    def test_boolean_close_without_durable_evidence_cannot_release_rom(self):
        self.observer.raise_error=KeyboardInterrupt()
        self.observer.close=lambda:True
        with self.assertRaisesRegex(trial.TrialError,'observation_closure_required'):
            self.execute()
        self.assertNotIn('final_sweep',self.backend.operations)

    def test_lease_changed_after_observation_blocks_all_rom_recovery(self):
        def corrupt_lease():
            lease=self.root/'.private'/trial.ACTIVE
            value=trial.decode(lease.read_bytes());value['runtime_sha256']='a'*64
            lease.write_bytes(trial.canonical(value)+b'\n')
        self.observer.effect=corrupt_lease
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertNotIn('final_sweep',self.backend.operations)

    def test_restore_failure_fresh_restore_only_recovery_never_candidate(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue(self.held())
        self.backend.fail_write = None
        self.backend.originals = None
        before = self.backend.operations.count('candidate_boot')
        self.assertTrue(self.recover()['restored'])
        self.assertEqual(self.backend.operations.count('candidate_boot'),before)

    def test_protected_corruption_blocks_all_restore_writes(self):
        self.observer.effect = lambda: self.backend.memory.update(partition=b'X'*4096)
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertEqual([op for op in self.backend.operations if op.startswith('write_')],['write_application'])
        self.assertNotIn('original_boot',self.backend.operations)

    def test_independent_final_sweep_mismatch_blocks_boot_barrier(self):
        self.backend.corrupt_final = 'state_0'
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertNotIn('original_boot_intent',self.state()['events'])
        self.assertNotIn('original_boot',self.backend.operations)

    def test_uncertain_original_boot_refuses_recovery_before_claim_or_consumption(self):
        self.backend.fail_reset = True
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.backend.memory['nvs'] = b'P'*12288
        before = list(self.backend.operations)
        with self.assertRaisesRegex(trial.TrialError,'original_boot_reconciliation_required'):
            self.recover()
        self.assertEqual(self.backend.operations,before)
        self.assertFalse((self.root/'.private'/(trial.PREFIX+'used-'+f'{2:032x}')).exists())

    def test_booted_but_unclosed_journal_settles_without_claim_or_writes(self):
        real_unlink = Path.unlink
        def fail_active(path,*args,**kwargs):
            if path.name == trial.ACTIVE:
                raise OSError('disk close failed')
            return real_unlink(path,*args,**kwargs)
        with mock.patch.object(Path,'unlink',fail_active):
            with self.assertRaises(trial.TrialError):
                self.execute()
        self.assertIn('original_booted',self.state()['events'])
        self.backend.memory['nvs'] = b'P'*12288
        before = list(self.backend.operations)
        self.assertTrue(self.recover()['restored'])
        self.assertEqual(self.backend.operations,before)
        self.assertEqual(self.backend.memory['nvs'],b'P'*12288)

    def test_consumed_execute_and_recover_grants_cannot_replay(self):
        self.execute()
        before = list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertEqual(self.backend.operations,before)

    def test_pending_reset_marker_prevents_candidate_and_original_boot(self):
        self.backend.memory['nvs'] = namespace_image(b'ot_reset_v1',b'intent_v1')
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertFalse(any(op.startswith('write_') or op.endswith('_boot') for op in self.backend.operations))

    def test_capture_tamper_and_aggregate_tamper_refuse_recovery_before_claim(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        capture = self.root/'.private'/(trial.PREFIX+f'{1:032x}'+'-state_1.bin')
        capture.write_bytes(b'X'*524288)
        before = list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertEqual(self.backend.operations,before)

    def test_each_journal_intent_disk_failure_has_no_following_hardware_effect(self):
        for event in ('preflight_idle','claim_intent','candidate_write_intent','candidate_boot_intent',
                      'restore_application_intent','original_boot_intent','original_booted'):
            with self.subTest(event=event),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);(root/'.private').mkdir()
                backend=Backend(self.original);observer=Observer(backend)
                real_save=trial.Journal.save
                operations_at_failure=[]
                def failing_save(journal,state):
                    if state['events'] and state['events'][-1] == event:
                        operations_at_failure.extend(backend.operations)
                        journal.healthy=False
                        raise trial.TrialError('journal_write_failed')
                    return real_save(journal,state)
                with mock.patch.object(trial.Journal,'save',failing_save):
                    with self.assertRaises(trial.TrialError):
                        trial.execute(root,self.request,*self.grant(),self.candidate,backend,observer,utc=lambda:150,initial_phone_idle=idle)
                self.assertEqual(backend.operations,operations_at_failure)

    def test_partial_pending_capture_is_preserved_then_freshly_verified(self):
        real_replace=trial.os.replace
        replaced=0
        def fail_replace(src,dst):
            nonlocal replaced
            if str(src).endswith('.pending'):
                replaced+=1
                if replaced == 4:
                    raise OSError('snapshot replace failed')
            return real_replace(src,dst)
        with mock.patch.object(trial.os,'replace',fail_replace):
            with self.assertRaises(trial.TrialError):
                self.execute()
        self.assertTrue((self.root/'.private'/(trial.PREFIX+f'{1:032x}'+'.pending')).exists())
        self.assertTrue(self.recover()['restored'])
        self.assertTrue((self.root/'.private'/(trial.PREFIX+f'{2:032x}'+'.interrupted')).exists())
        self.assertFalse(any(op.startswith('write_') for op in self.backend.operations))

    def test_tampered_lease_and_invalid_journal_types_refuse_before_claim(self):
        self.backend.fail_read=('nvs',1)
        with self.assertRaises(trial.TrialError):
            self.execute()
        value=self.state();value['observation_closed']=1
        self.state_path().write_bytes(trial.canonical(value)+b'\n')
        before=list(self.backend.operations)
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertEqual(self.backend.operations,before)

    def test_unknown_duplicate_and_after_boot_write_journal_events_refuse(self):
        self.backend.fail_reset=True
        with self.assertRaises(trial.TrialError):
            self.execute()
        original=self.state()
        for events in (original['events']+['write_arbitrary'],original['events']+['candidate_write_intent'],
                       original['events']+['restoration_intent']):
            state=dict(original,events=events)
            self.state_path().write_bytes(trial.canonical(state)+b'\n')
            before=list(self.backend.operations)
            with self.assertRaises(trial.TrialError):
                self.recover()
            self.assertEqual(self.backend.operations,before)


if __name__ == '__main__':
    unittest.main()
