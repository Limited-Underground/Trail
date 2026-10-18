"""Host-only reviewed visible launch/readiness; native UI and devices are doubled."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_interactive_launch as visible
import enrollment_candidate_visible_session as session
import enrollment_candidate_runtime as runtime
import enrollment_candidate_predevice_resume as resume
from enrollment_candidate_runtime_tests import AssemblyFixture
from enrollment_candidate_settings_successor_tests import LEGACY_POLICY_PINS, historical_source


SCOPE = dict(strategy=visible.STRATEGY, session_id=1, logon_sha256='a' * 64,
    input_desktop=True, ac_online=True, display_never=True, sleep_never=True,
    no_screensaver_or_inactivity_lock=True, work_area_sufficient=True)


class Clock:
    def __init__(self): self.value = 100.0
    def mono(self): return self.value
    def utc(self): return self.value + 900.0
    def sleep(self, seconds):
        assert seconds >= 0
        self.value += seconds


class Fixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / '.private'; self.root.mkdir()
        self.readiness = self.root / 'readiness'; self.readiness.mkdir()
        self.output = self.root / 'session'; self.output.mkdir()
        self.clock = Clock()
        self.bound = dict(readiness_root=str(self.readiness), session_output=str(self.output), session_nonce='a' * 32,
            assembly={'path': '/synthetic/assembly', 'bytes': 10, 'sha256': 'b' * 64})
        self.ref = {'path': '/synthetic/binding', 'bytes': 1, 'sha256': 'c' * 64}
        self.opened, self.clear = [], True
        self.event = 'confirm'
    def admit(self, path, sha): return self.bound, self.ref
    def scope(self): return copy.deepcopy(SCOPE)
    def backend(self):
        f = self
        class Backend:
            def open(self, text): f.opened.append(text); return True
            def poll(self): return f.event
            def clear(self): return f.clear
        return Backend()
    def probe(self, backend_factory=None):
        with patch.object(visible.secrets, 'choice', return_value='BIRD'), patch.object(visible.secrets, 'randbelow', return_value=0):
            return visible.probe('/synthetic/binding', 'c' * 64, admission=self.scope,
                monotonic=self.clock.mono, utc=self.clock.utc, sleep=self.clock.sleep,
                backend_factory=self.backend if backend_factory is None else backend_factory,
                binding_admitter=self.admit)
    def owner(self, proof=None, **changes):
        proof = self.probe() if proof is None else proof
        value = dict(schema='OT-NEUTRAL-OWNER-REPLY-1', proof=proof, source='fresh_human_chat_reply',
            received_utc=self.clock.utc(), observed_word='BIRD', difference=[1, 1], rows_comfortably_readable=True)
        value.update(changes)
        return visible.save_once(self.readiness / 'owner.json', value)
    def ready(self, **changes):
        owner = self.owner(**changes)
        return visible.attest('/synthetic/binding', 'c' * 64, owner, admission=self.scope,
            monotonic=self.clock.mono, utc=self.clock.utc, binding_admitter=self.admit)
    def require(self):
        return visible.require_ready('/synthetic/binding', 'c' * 64, admission=self.scope,
            monotonic=self.clock.mono, utc=self.clock.utc, binding_admitter=self.admit)
    def practice(self):
        return visible.practice('/synthetic/binding', 'c' * 64, readiness_checker=lambda *a: self.require(),
            admission=self.scope, monotonic=self.clock.mono, utc=self.clock.utc, sleep=self.clock.sleep,
            backend_factory=self.backend)
    def cleanup(self): self.temp.cleanup()


class VisibilityTests(unittest.TestCase):
    def fixture(self):
        f = Fixture(); self.addCleanup(f.cleanup); return f

    def test_imports_are_inert_no_serial_esptool_or_operational_constructor(self):
        self.assertNotIn('serial', sys.modules)
        self.assertNotIn('esptool', sys.modules)
        self.assertNotIn('subprocess', visible.SOURCE_NAMES)

    def test_real_neutral_sequence_waits60_and_saves_separate_non_authority_proof(self):
        f = self.fixture(); ref = f.probe(); proof = runtime.decode(visible.read_reference(ref))
        self.assertEqual(2, len(f.opened))
        self.assertTrue(all(len(text.split('\r\n')) <= 25 for text in f.opened))
        self.assertGreaterEqual(proof['finished_monotonic'] - proof['started_monotonic'], 60)
        self.assertFalse(proof['hardware_access']); self.assertFalse(proof['enrollment_authority'])
        self.assertTrue(all(t['explicit_click'] and t['cleared'] for t in proof['timings']))
        self.assertFalse(list(f.output.iterdir()))

    def test_owner_word_difference_and_comfort_are_all_required(self):
        for changes in ({'observed_word': 'PINE'}, {'difference': [2, 1]},
                        {'rows_comfortably_readable': False}, {'source': 'inferred_reply'}):
            with self.subTest(changes=changes):
                f = self.fixture()
                with self.assertRaises(visible.AdmissionError): f.ready(**changes)
                self.assertFalse((f.readiness / 'readiness.json').exists())

    def test_cancel_late_click_or_uncertain_clear_never_publishes_proof(self):
        for event, clear in (('cancel', True), ('confirm', False), ('error', True)):
            with self.subTest(event=event, clear=clear):
                f = self.fixture(); f.event, f.clear = event, clear
                with self.assertRaises(visible.AdmissionError): f.probe()
                self.assertFalse((f.readiness / 'neutral-proof.json').exists())

    def test_no_explicit_click_reaches_neutral300_timeout_and_clears(self):
        f = self.fixture(); f.event = None
        with self.assertRaises(Exception): f.probe()
        self.assertFalse((f.readiness / 'neutral-proof.json').exists())
        self.assertGreaterEqual(f.clock.value, 460)
        self.assertLessEqual(f.clock.value, 461)

    def delayed_backend(self, f, delays, cleared):
        delays = iter(delays)
        class Backend:
            def open(self, text):
                f.opened.append(text); self.delay = next(delays); return True
            def poll(self): f.clock.sleep(self.delay); return 'confirm'
            def clear(self): cleared.append(True); return True
        return Backend

    def test_delayed_neutral_word_and_comparison_attest_with_age_anchored_to_finish(self):
        for delays in ((0, 180), (275, 275)):
            with self.subTest(delays=delays):
                f = self.fixture(); cleared = []
                ref = f.probe(self.delayed_backend(f, delays, cleared))
                proof = runtime.decode(visible.read_reference(ref))
                self.assertEqual(list(delays), [t['elapsed_seconds'] for t in proof['timings']])
                self.assertEqual(60 + sum(delays), proof['finished_monotonic'] - proof['started_monotonic'])
                self.assertEqual([True, True], cleared)
                owner = f.owner(proof=ref)
                visible.attest('/synthetic/binding', 'c' * 64, owner, admission=f.scope,
                               monotonic=f.clock.mono, utc=f.clock.utc, binding_admitter=f.admit)
                f.clock.sleep(899)
                f.require()
                f.clock.sleep(1)
                with self.assertRaises(visible.AdmissionError): f.require()
                self.assertFalse(list(f.output.iterdir()))

    def test_neutral300_boundary_or_late_click_clears_without_publishing_proof(self):
        for delays in ((300, 0), (0, 300), (0, 301)):
            with self.subTest(delays=delays):
                f = self.fixture(); cleared = []
                with self.assertRaises(Exception): f.probe(self.delayed_backend(f, delays, cleared))
                self.assertEqual([True] * len(f.opened), cleared)
                self.assertFalse((f.readiness / 'neutral-proof.json').exists())
                self.assertFalse(list(f.output.iterdir()))

    def test_saved_neutral_timings_at_or_over300_are_rejected(self):
        f = self.fixture(); proof = runtime.decode(visible.read_reference(f.probe()))
        for elapsed in (300, 301):
            for index in (0, 1):
                changed = copy.deepcopy(proof); changed['timings'][index]['elapsed_seconds'] = elapsed
                with self.assertRaisesRegex(visible.AdmissionError, '^readiness_invalid$'):
                    visible._check_proof(changed, f.ref, f.scope, f.clock.mono, f.clock.utc)

    def test_actual_practice_retains120_shared_and60_code_deadlines(self):
        for delays in ((120, 0, 0, 0), (90, 30, 0, 0), (0, 0, 0, 60)):
            with self.subTest(delays=delays):
                f = self.fixture(); f.ready(); cleared = []
                with self.assertRaises(Exception):
                    visible.practice('/synthetic/binding', 'c' * 64, readiness_checker=lambda *a: f.require(),
                                     admission=f.scope, monotonic=f.clock.mono, utc=f.clock.utc,
                                     sleep=f.clock.sleep, backend_factory=self.delayed_backend(f, delays, cleared))
                self.assertTrue(cleared)
                self.assertFalse((f.readiness / 'practice.json').exists())
                self.assertFalse(list(f.output.iterdir()))

    def test_current_scope_power_and_session_change_refuses_before_dispatch(self):
        for changed in ({'session_id': 2}, {'logon_sha256': 'd' * 64}, {'input_desktop': False},
                        {'ac_online': False}, {'display_never': False}):
            f = self.fixture(); f.ready()
            f.scope = lambda changed=changed: dict(SCOPE, **changed)
            called = []
            with self.assertRaises(visible.AdmissionError):
                session.dispatch('ignored', 'c' * 64, 'route', readiness_checker=lambda *a: f.require(),
                    session_loader=lambda _: called.append('loaded'))
            self.assertEqual([], called)
            self.assertFalse(list(f.output.iterdir()))

    def test_absent_expired_clock_rollback_and_boot_clock_reset_fail_before_work(self):
        f = self.fixture()
        with self.assertRaises(Exception): f.require()
        f.ready()
        good = f.clock.value
        for now in (good - 1, good + visible.MAX_AGE, 1):
            f.clock.value = now
            with self.assertRaises(visible.AdmissionError): f.require()
        f.clock.value = good
        with self.assertRaises(visible.AdmissionError):
            visible.require_ready('ignored', 'c' * 64, admission=f.scope, monotonic=f.clock.mono,
                utc=lambda: f.clock.utc() - 1, binding_admitter=f.admit)

    def test_valid_attestation_and_four_step_practice_lower_bound_no_device_authority(self):
        f = self.fixture(); f.ready(); f.require()
        initial_admissions = []
        def checked(*args): initial_admissions.append(True); return f.require()
        ref = visible.practice('ignored', 'c' * 64, readiness_checker=checked, admission=f.scope,
            monotonic=f.clock.mono, utc=f.clock.utc, sleep=f.clock.sleep, backend_factory=f.backend)
        self.assertEqual(2, len(initial_admissions))
        record = runtime.decode(visible.read_reference(ref))
        self.assertEqual(['MY_ID', 'PEER_A', 'PEER_B', 'COMPARE_CODE'], [t['kind'] for t in record['timings']])
        self.assertEqual(12, record['modeled_gesture_seconds']); self.assertEqual(12, record['total_seconds'])
        self.assertTrue(record['human_only_lower_bound']); self.assertFalse(record['enrollment_feasibility_accepted'])
        self.assertFalse(record['hardware_access'])
        visible.require_practice(f.bound, 'c' * 64)
        practice_texts = f.opened[-4:]
        for text in practice_texts:
            self.assertLessEqual(len(text.split('\r\n')), 25)
            self.assertTrue(all(len(line) <= 76 for line in text.split('\r\n')))
            self.assertIn('EXAMPLE ONLY', text)
        self.assertIn('INVITER A MY ID OT-ID1', practice_texts[0])
        self.assertIn('MEMBER B MY ID OT-ID1', practice_texts[0])
        self.assertNotIn(visible.EXAMPLES[0], practice_texts[0])
        self.assertFalse(list(f.output.iterdir()))

    def test_all_initial_actions_gate_before_session_import_route_or_grant_write(self):
        for action in ('baseline', 'route', 'issue_capture', 'launch'):
            called = []
            def refuse(*args): raise visible.AdmissionError('readiness_expired')
            with self.assertRaises(visible.AdmissionError):
                session.dispatch('/wrong', 'c' * 64, action, readiness_checker=refuse,
                    session_loader=lambda value: called.append('loaded'))
            self.assertEqual([], called)

    def test_fixed_dispatch_rejects_arbitrary_action_and_owner_reply_on_operational_action(self):
        with self.assertRaises(visible.AdmissionError): session.dispatch('x', 'x', 'powershell')
        with self.assertRaises(visible.AdmissionError): session.dispatch('x', 'x', 'route', {'x': 1})

    def test_launch_preserves_exact_runtime_interface_and_only_adds_presentation_choice(self):
        f = self.fixture(); called = []
        result = visible.launch('bind', 'pin', 'capture', 'package', 'packagepin',
            readiness_checker=lambda *args: f.bound,
            runtime_launcher=lambda *args, **kwargs: called.append((args, kwargs)) or 'done')
        self.assertEqual('done', result)
        self.assertEqual((('/synthetic/assembly', 'b' * 64, 'capture', 'package', 'packagepin'),
                          {'bench_presentation': True}), called[0])
        for mode in ('release', 'recover', 'shell'):
            with self.assertRaises(visible.AdmissionError): visible.launch('x', 'x', mode, 'x', 'x')

    def test_unchanged_runtime_release_recover_have_no_dependency_on_visible_receipt(self):
        text = (ROOT / 'tools/enrollment_candidate_runtime.py').read_text()
        self.assertNotIn('require_ready', text)
        self.assertIn("'recover'", text); self.assertIn("'release'", text)


class RecoveryBindingFixture:
    """Exact-byte synthetic gate; operational admission always refuses."""
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        self.private = self.root / '.private'; self.private.mkdir()
        tools = self.root / 'tools'; tools.mkdir()
        for name in visible.RESUME_SOURCE_NAMES:
            raw = historical_source(name)
            if name in visible.SETTINGS_AFTER_PINS:
                # This fixture represents the historical cleanup runtime.
                # Distinguish its synthetic source pins from today's current
                # settings successor, whose exact pins force the new gate.
                raw += b'\n# synthetic historical-cleanup source fixture\n'
            (tools / name).write_bytes(raw)
        package = self.private / 'ot0238c-launcher-recovery-20261011'
        prep = package / 'prestage'; prep.mkdir(parents=True)
        recovery = package / 'recovery'; recovery.mkdir()
        self.output = package / 'sessions' / ('a' * 32); self.output.mkdir(parents=True)
        self.ready = package / 'readiness' / ('a' * 32); self.ready.mkdir(parents=True)
        template = prep / 'session.template.py'; template.write_bytes(b'# inert synthetic session\n')
        module = recovery / 'host-recovery-gate.py'
        module.write_bytes(b'class RecoveryDispositionError(RuntimeError): pass\n'
            b'def require_candidate_entry_allowed(path, sha256, private):\n'
            b'    raise RecoveryDispositionError("recovery_baseline_unresolved")\n')
        disposition = recovery / 'manual-recovery-observed-baseline-unresolved.json'
        disposition.write_bytes(b'{"candidate_entry_allowed":false,"synthetic":true}\n')
        self.gate = dict(schema=resume.RECOVERY_GATE_SCHEMA, module=visible.reference(module),
            disposition=visible.reference(disposition), expected_private_root=str(self.private))
        pins = {key:{k:self.gate[key][k] for k in ('bytes','sha256')} for key in ('module','disposition')}
        # The owner path compiles its exact pinned helper bytes into a separate
        # namespace. Substitute only this fixture's inert gate trust pins there;
        # a patch to the already imported module cannot affect that namespace.
        helper = tools / 'enrollment_candidate_predevice_resume.py'
        text = helper.read_text(encoding='utf8')
        for key in ('module','disposition'):
            text = text.replace('bytes='+str(resume.RECOVERY_GATE_PINS[key]['bytes']),
                'bytes='+str(pins[key]['bytes']))
            text = text.replace(resume.RECOVERY_GATE_PINS[key]['sha256'],pins[key]['sha256'])
        helper.write_text(text,encoding='utf8')
        self.item = dict(schema=resume.TRANSITION_MANIFEST_SCHEMA,
            runtime_transition=dict(kind=resume.CLEANUP_TRANSITION_KIND,recovery_gate=self.gate))
        self.predevice = prep / 'resume-manifest.json'
        self.write(self.predevice, self.item)
        self.binding = dict(schema='OT-VISIBLE-SESSION-BINDING-2',worktree=str(self.root),
            host=visible.reference(Path(sys.executable).resolve()), assembly={'unreachable':'SDK'},
            session=visible.reference(template),sources={name:runtime.descriptor((tools/name).read_bytes())
                for name in visible.RESUME_SOURCE_NAMES},review={'unreachable':'review'},
            readiness_root=str(self.ready),session_nonce='a'*32,session_output=str(self.output),
            predevice=visible.reference(self.predevice))
        self.binding_path = prep / 'binding.json'; self.rebind()
        self.patches = [patch.object(visible,'ROOT',self.root),patch.object(resume,'RECOVERY_GATE_PINS',pins)]
        for p in self.patches:p.start()
    def write(self,path,value):
        path.write_bytes(runtime.canonical(value)+b'\n');return visible.reference(path)
    def rebind(self):
        self.binding['predevice']=self.write(self.predevice,self.item)
        self.bound=self.write(self.binding_path,self.binding)
        return self.bound
    def close(self):
        for p in reversed(self.patches):p.stop()
        self.temp.cleanup()


class RecoveryAdmissionTests(unittest.TestCase):
    def fixture(self):
        f=RecoveryBindingFixture();self.addCleanup(f.close);return f
    def test_direct_admission_refuses_before_sdk_and_native_readiness(self):
        f=self.fixture()
        with patch.object(runtime,'verify_assembly',side_effect=AssertionError('SDK')),patch.object(
                visible,'native_admission',side_effect=AssertionError('native UI')):
            with self.assertRaisesRegex(visible.AdmissionError,'^recovery_baseline_unresolved$'):
                visible.admit_binding(f.bound['path'],f.bound['sha256'],
                    assembly_verifier=lambda *args:(_ for _ in ()).throw(AssertionError('SDK')))
        self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])
        self.assertNotIn('serial',sys.modules);self.assertNotIn('esptool',sys.modules)
    def test_schema1_downgrade_and_removed_transition_never_touch_sdk(self):
        for change in ('schema','removed_kind','renamed_kind','missing_predevice'):
            f=self.fixture()
            if change=='schema':
                f.binding['schema']='OT-VISIBLE-SESSION-BINDING-1';f.binding.pop('predevice')
                f.binding['sources'].pop(resume.NAME)
            if change=='removed_kind':f.item.pop('runtime_transition')
            if change=='renamed_kind':f.item['runtime_transition']['kind']='ot0238c-restart-diagnostic'
            if change=='missing_predevice':f.binding.pop('predevice')
            if change in ('schema','missing_predevice'):f.bound=f.write(f.binding_path,f.binding)
            else:f.rebind()
            with self.subTest(change=change),self.assertRaises(visible.AdmissionError):
                visible.admit_binding(f.bound['path'],f.bound['sha256'],
                    assembly_verifier=lambda *args:(_ for _ in ()).throw(AssertionError('SDK')))
            self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])
    def test_missing_repointed_and_substituted_gate_refuses_direct_admission(self):
        for change in ('missing','repointed','substituted','skip'):
            f=self.fixture();transition=f.item['runtime_transition']
            if change=='missing':transition.pop('recovery_gate')
            if change=='repointed':
                path=f.private/'other-module.py';path.write_bytes(Path(f.gate['module']['path']).read_bytes())
                f.gate['module']=visible.reference(path)
            if change=='substituted':Path(f.gate['module']['path']).write_bytes(b'def allow(): return True\n')
            if change=='skip':f.gate['skip']=True
            f.rebind()
            with self.subTest(change=change),self.assertRaisesRegex(visible.AdmissionError,'^recovery_gate_invalid$'):
                visible.admit_binding(f.bound['path'],f.bound['sha256'])
            self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])
    def test_fixed_cleanup_detectors_are_identical(self):
        import enrollment_candidate_owner_workflow as flow
        expected={name:pair['after'] for name,pair in resume.CLEANUP_TRANSITION_PINS.items()
            if name!='enrollment_candidate_runner.py'}
        self.assertEqual(visible.CLEANUP_AFTER_PINS,expected);self.assertEqual(flow.CLEANUP_AFTER_PINS,expected)

    def test_mixed_current_core_with_legacy_cleanup_route_refuses_before_dispatch(self):
        import enrollment_candidate_owner_workflow as flow
        for name in visible.SETTINGS_AFTER_PINS:
            f=self.fixture()
            path=f.root/'tools'/name
            path.write_bytes((ROOT/'tools'/name).read_bytes())
            f.binding['sources'][name]=runtime.descriptor(path.read_bytes());f.rebind()
            with self.subTest(name=name),patch.object(flow,'ROOT',f.root),\
                    patch.object(subprocess,'run',side_effect=AssertionError('dispatch')),\
                    patch.object(subprocess,'Popen',side_effect=AssertionError('dispatch')):
                with self.assertRaisesRegex(visible.AdmissionError,'^binding_invalid$'):
                    visible.admit_binding(f.bound['path'],f.bound['sha256'],
                        assembly_verifier=lambda *args:(_ for _ in ()).throw(AssertionError('SDK')))
                with self.assertRaisesRegex(flow.Refused,'^recovery_gate_invalid$'):
                    flow.require_cleanup_recovery(f.bound)
            self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])


class BindingTests(unittest.TestCase):
    def fixture(self, task='ot0238c-visible-launch-20261005'):
        f = AssemblyFixture(); self.addCleanup(f.cleanup)
        # Legacy positives use the preserved v16 sources. Today's cleanup
        # runtime is deliberately forbidden from claiming a schema1 binding.
        historical = ROOT / 'tests/host/fixtures/enrollment_candidate_legacy_sources'
        for name in set(visible.CLEANUP_AFTER_PINS) | set(LEGACY_POLICY_PINS):
            raw = historical_source(name) if name in LEGACY_POLICY_PINS else (historical / (name + '.fixture')).read_bytes()
            expected = (LEGACY_POLICY_PINS[name] if name in LEGACY_POLICY_PINS
                else resume.CLEANUP_TRANSITION_PINS[name]['before'])
            self.assertEqual(runtime.descriptor(raw), expected)
            (f.worktree / 'tools' / name).write_bytes(raw)
            f.sources['tools/' + name] = runtime.descriptor(raw)
        f.save_source_pins()
        assembly = f.assemble()
        for name in visible.SOURCE_NAMES:
            target = f.worktree / 'tools' / name
            if not target.exists(): target.write_bytes((ROOT / 'tools' / name).read_bytes())
        prep = f.private / task / 'prestage'; prep.mkdir(parents=True)
        template = prep / 'session.template.py'; template.write_bytes(b'# reviewed inert template\n')
        output = f.private / task / 'sessions' / ('a' * 32); output.mkdir(parents=True)
        readiness = f.private / 'neutral'; readiness.mkdir()
        binding = dict(schema='OT-VISIBLE-SESSION-BINDING-1', worktree=str(f.worktree),
            host=visible.reference(Path(sys.executable).resolve()), assembly=visible.reference(assembly.assembly_path),
            session=visible.reference(template), sources={n: runtime.descriptor((f.worktree / 'tools' / n).read_bytes()) for n in visible.SOURCE_NAMES},
            readiness_root=str(readiness), session_nonce='a' * 32, session_output=str(output))
        review = dict(schema='OT-VISIBLE-SESSION-REVIEW-1', binding_core_sha256=runtime.descriptor(runtime.canonical(binding))['sha256'],
            outcome='no_unresolved_findings', findings=[], hardware_access=False, live_grants_created=False)
        binding['review'] = visible.save_once(f.private / 'review.json', review)
        ref = visible.save_once(f.private / 'binding.json', binding)
        return f, binding, ref

    def test_real_binding_source_host_session_runtime_review_and_root_admission(self):
        f, value, ref = self.fixture()
        with patch.object(visible, 'ROOT', f.worktree):
            got, got_ref = visible.admit_binding(ref['path'], ref['sha256'])
        self.assertEqual(value, got); self.assertEqual(ref, got_ref)

    def test_exact_successor_template_parent_and_source17_are_required(self):
        for task in ('ot0238c-button-confirmation-20261005', 'ot0238c-readability-20261006',
                     'ot0238c-neutral-time-20261006', 'ot0238c-launch-binding-20261008',
                     'ot0238c-retained-preparation-20261009',
                     'ot0238c-restart-package-20261010',
                     'ot0238c-restart-package-20261010/unreviewed',
                     'ot0238c-restart-package-20261011',
                     'ot0238c-restart-package-20261010-sibling',
                     'ot0238c-retained-preparation-20261009/unreviewed',
                     'ot0238c-retained-preparation-20261010',
                     'ot0238c-retained-preparation-20261009-sibling',
                     'ot0238c-launch-binding-20261008/unreviewed',
                     'ot0238c-launch-binding-20261009',
                     'ot0238c-launch-binding-20261008-sibling',
                     'ot0238c-neutral-time-20261006/unreviewed',
                     'ot0238c-neutral-time-20261007',
                     'arbitrary-successor', 'ot0238c-readability-20261006/unreviewed',
                     'ot0238c-readability-20261007'):
            f,value,ref=self.fixture(task)
            with patch.object(visible,'ROOT',f.worktree):
                if task not in ('ot0238c-button-confirmation-20261005', 'ot0238c-readability-20261006',
                                'ot0238c-neutral-time-20261006', 'ot0238c-launch-binding-20261008',
                                'ot0238c-retained-preparation-20261009', 'ot0238c-restart-package-20261010'):
                    with self.assertRaisesRegex(visible.AdmissionError, '^session_invalid$'):
                        visible.admit_binding(ref['path'],ref['sha256'])
                else:
                    self.assertEqual(value,visible.admit_binding(ref['path'],ref['sha256'])[0])
                    self.assertEqual(len(value['sources']),len(visible.SOURCE_NAMES))
                    (f.worktree/'tools/enrollment_candidate_owner_workflow.py').write_text('changed')
                    with self.assertRaises(Exception):visible.admit_binding(ref['path'],ref['sha256'])

    def test_any_changed_helper_session_runtime_or_review_pin_refuses(self):
        for field in ('helper', 'session', 'assembly', 'review'):
            f, value, ref = self.fixture()
            path = (f.worktree / 'tools/enrollment_candidate_visible_session.py') if field == 'helper' else Path(value[field]['path'])
            path.write_bytes(path.read_bytes() + b'changed')
            with patch.object(visible, 'ROOT', f.worktree), self.assertRaises(Exception):
                visible.admit_binding(ref['path'], ref['sha256'])

    def test_wrong_root_session_path_host_and_output_are_refused(self):
        for field, bad in (('worktree', '/wrong'), ('session_output', '/wrong'), ('session_nonce', 'b' * 32)):
            f, value, ref = self.fixture(); value[field] = bad
            changed = f.private / ('bad-' + field + '.json')
            badref = visible.save_once(changed, value)
            with patch.object(visible, 'ROOT', f.worktree), self.assertRaises(Exception):
                visible.admit_binding(badref['path'], badref['sha256'])


class LockPolicyTests(unittest.TestCase):
    LOCAL = 'Control Panel\\Desktop'
    POLICY = 'Software\\Policies\\Microsoft\\Windows\\Control Panel\\Desktop'
    def snapshot(self, **fields):
        values = {('user', self.LOCAL, 'ScreenSaveActive'): ('1', 'sz'),
                  ('user', self.LOCAL, 'ScreenSaverIsSecure'): ('0', 'sz'),
                  ('user', self.LOCAL, 'ScreenSaveTimeOut'): ('60', 'sz')}
        values.update({('user', self.LOCAL, field): record for field, record in fields.items()})
        return values
    def read(self, values): return lambda hive, key, field: values.get((hive, key, field))

    def test_actual_none_snapshot_stale_active1_timeout60_no_executable_allows(self):
        self.assertTrue(visible.admit_lock_policy(self.read(self.snapshot()), True))

    def test_explicit_empty_executable_is_none_but_whitespace_is_malformed(self):
        self.assertTrue(visible.admit_lock_policy(self.read(self.snapshot(**{'SCRNSAVE.EXE': ('', 'sz')})), True))
        with self.assertRaises(visible.AdmissionError):
            visible.admit_lock_policy(self.read(self.snapshot(**{'SCRNSAVE.EXE': (' ', 'sz')})), True)

    def test_enabled_configured_nonpassword_or_password_saver_refuses(self):
        for secure in ('0', '1'):
            with self.assertRaises(visible.AdmissionError):
                visible.admit_lock_policy(self.read(self.snapshot(**{'SCRNSAVE.EXE': ('C:\\Windows\\System32\\example.scr', 'sz'),
                    'ScreenSaverIsSecure': (secure, 'sz')})), True)

    def test_dormant_password_preference_does_not_create_timer(self):
        values = self.snapshot(ScreenSaverIsSecure=('1', 'sz'))
        self.assertTrue(visible.admit_lock_policy(self.read(values), True))
        values[('user', self.LOCAL, 'SCRNSAVE.EXE')] = ('example.scr', 'sz')
        self.assertTrue(visible.admit_lock_policy(self.read(values), False))

    def test_policy_configured_executable_and_enforced_activation_cannot_be_ignored(self):
        for hive in ('user', 'machine'):
            values = self.snapshot()
            values[(hive, self.POLICY, 'SCRNSAVE.EXE')] = ('enforced.scr', 'sz')
            with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)
            values[(hive, self.POLICY, 'ScreenSaveActive')] = ('1', 'sz')
            with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), False)

    def test_configured_active_saver_refuses_even_with_zero_or_unknown_registry_timeout(self):
        values = self.snapshot(**{'SCRNSAVE.EXE': ('example.scr', 'sz'), 'ScreenSaveTimeOut': ('0', 'sz')})
        with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)
        del values[('user', self.LOCAL, 'ScreenSaveTimeOut')]
        with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)

    def test_typed_seconds_600_is_enabled_lock_and_zero_is_safe(self):
        key = ('machine', 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System', 'InactivityTimeoutSecs')
        values = self.snapshot(); values[key] = (0, 'dword')
        self.assertTrue(visible.admit_lock_policy(self.read(values), True))
        for record in ((600, 'dword'), ('0', 'sz'), (-1, 'dword'), (0x100000000, 'dword')):
            values[key] = record
            with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)

    def test_dynamic_lock_running_saver_and_read_errors_fail_closed(self):
        values = self.snapshot()
        values[('user', 'Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon', 'EnableGoodbye')] = (1, 'dword')
        with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)
        with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(self.snapshot()), True, True)
        def broken(*args): raise PermissionError('synthetic inaccessible key')
        with self.assertRaisesRegex(visible.AdmissionError, '^lock_policy_unavailable$'): visible.admit_lock_policy(broken, True)

    def test_malformed_local_or_policy_values_refuse_even_with_no_executable(self):
        for field, record in (('ScreenSaveActive', ('yes', 'sz')), ('ScreenSaverIsSecure', (2, 'dword')),
                              ('ScreenSaveTimeOut', ('sixty', 'sz')), ('SCRNSAVE.EXE', (1, 'dword'))):
            for key in (('user', self.LOCAL, field), ('machine', self.POLICY, field)):
                values = self.snapshot(); values[key] = record
                with self.assertRaises(visible.AdmissionError): visible.admit_lock_policy(self.read(values), True)


class ComparisonCoordinatesTests(unittest.TestCase):
    def blocks(self, text):
        lines = text.split('\r\n')
        blocks = []
        for title in ('EXAMPLE ONLY - LEFT:', 'EXAMPLE ONLY - RIGHT:'):
            start = lines.index(title) + 1
            blocks.append(lines[start:start + 5])
        return lines, blocks

    def test_both_column_headers_and_all_row_characters_align_in_fixed_font_cells(self):
        lines, blocks = self.blocks(visible._text('TEST_COMPARE', row=0, column=13))
        self.assertEqual(len(lines), 19)
        self.assertTrue(all(len(line) <= 57 for line in lines))
        for header, *rows in blocks:
            self.assertEqual('COL    ' + ' '.join(f'{column:02d}' for column in range(1, 9))
                             + '  | ' + ' '.join(f'{column:02d}' for column in range(9, 17)), header)
            self.assertEqual(57, len(header))
            for row_index, row in enumerate(rows, 1):
                self.assertTrue(row.startswith(f'ROW {row_index}  '))
                self.assertEqual(len(header), len(row))
                self.assertEqual('  | ', row[30:34])
                for column in range(16):
                    cell = 7 + 3 * column + (3 if column >= 8 else 0)
                    self.assertEqual(f'{column + 1:02d}', header[cell:cell + 2])
                    self.assertEqual(' ', row[cell])
                    self.assertIn(row[cell + 1], '0123456789ABCDEF')
                    self.assertEqual(str((column + 1) % 10), header[cell + 1])

    def test_every_planted_difference_survives_labels_at_its_exact_one_based_coordinate(self):
        for row in range(4):
            for column in range(16):
                with self.subTest(row=row + 1, column=column + 1):
                    _, blocks = self.blocks(visible._text('TEST_COMPARE', row=row, column=column))
                    decoded = [[''.join(line[8 + 3 * c + (3 if c >= 8 else 0)]
                                       for c in range(16)) for line in block[1:]] for block in blocks]
                    self.assertEqual(list(visible.EXAMPLES), decoded[0])
                    differences = [(r + 1, c + 1) for r in range(4) for c in range(16)
                                   if decoded[0][r][c] != decoded[1][r][c]]
                    self.assertEqual([(row + 1, column + 1)], differences)
                    self.assertIn('printed ROW (1-4) and COL (01-16)',
                                  visible._text('TEST_COMPARE', row=row, column=column))

    def test_labels_do_not_change_word_example_values_or_accept_malformed_rows(self):
        text = visible._text('TEST_WORD', word='BIRD')
        for row in visible.EXAMPLES: self.assertIn(row, text)
        self.assertNotIn('COL    01', text)
        for malformed in (visible.EXAMPLES[:3], list(visible.EXAMPLES[:3]) + ['0' * 15],
                          list(visible.EXAMPLES[:3]) + ['G' * 16]):
            with self.assertRaises(visible.AdmissionError): visible._labeled_hex_rows(malformed)

    def test_native_probe_requests_larger_bold_font_and_device_backend_keeps_its_font(self):
        import enrollment_candidate_private_view as private
        from enrollment_candidate_private_view_tests import NativeAPI
        fonts = []
        for backend_type, expected in ((private._NeutralBackend, (-24, 700)),
                                       (private._Win32Backend, (-18, 400))):
            api = NativeAPI()
            api.gdi.CreateFontW = lambda *args: fonts.append(args) or 55
            backend = backend_type(api_factory=lambda: api)
            try:
                text = (visible._text('TEST_COMPARE', row=1, column=3)
                        if backend_type is private._NeutralBackend
                        else 'PRIVATE DEVICE BUTTON CHECKPOINT\r\nsynthetic')
                self.assertTrue(backend.open(text))
                self.assertEqual(expected, (fonts[-1][0], fonts[-1][4]))
                self.assertEqual('Consolas', fonts[-1][-1])
                if backend_type is private._Win32Backend:
                    self.assertIsNone(backend._confirm)
                    self.assertEqual(3, api.create_count)
            finally:
                self.assertTrue(backend.clear())

    def test_probe_selects_neutral_backend_and_practice_keeps_existing_backend(self):
        import enrollment_candidate_private_view as private
        f = Fixture(); self.addCleanup(f.cleanup)
        opened = []
        class Neutral:
            def open(self, text): opened.append(text); return True
            def poll(self): return 'confirm'
            def clear(self): return True
        with patch.object(private, '_NeutralBackend', Neutral), \
             patch.object(private, '_Win32Backend', side_effect=AssertionError('device backend selected')), \
             patch.object(visible.secrets, 'choice', return_value='BIRD'), \
             patch.object(visible.secrets, 'randbelow', return_value=0):
            proof = visible.probe('/synthetic/binding', 'c' * 64, admission=f.scope,
                          monotonic=f.clock.mono, utc=f.clock.utc, sleep=f.clock.sleep,
                          binding_admitter=f.admit)
        self.assertEqual(2, len(opened))
        owner = f.owner(proof=proof)
        visible.attest('/synthetic/binding', 'c' * 64, owner, admission=f.scope,
                       monotonic=f.clock.mono, utc=f.clock.utc, binding_admitter=f.admit)
        with patch.object(private, '_Win32Backend', Neutral), \
             patch.object(private, '_NeutralBackend', side_effect=AssertionError('probe backend selected')):
            visible.practice('/synthetic/binding', 'c' * 64, readiness_checker=lambda *a: f.require(),
                             admission=f.scope, monotonic=f.clock.mono, utc=f.clock.utc, sleep=f.clock.sleep)
        self.assertEqual(6, len(opened))

    def test_instructions_name_the_actual_neutral_confirmation_buttons(self):
        import enrollment_candidate_private_view as private
        for text, caption in ((visible._text('TEST_WORD', word='BIRD'), 'Word sent - continue'),
                              (visible._text('TEST_COMPARE', row=1, column=3), 'Difference sent - continue')):
            self.assertEqual(caption, private._confirmation_caption(text))
            self.assertIn('Click ' + caption + '.', text)


if __name__ == '__main__': unittest.main()
