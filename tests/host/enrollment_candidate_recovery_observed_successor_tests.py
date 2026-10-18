"""Exact observed recovery-A host admission; isolated fixture data only."""
from pathlib import Path
from contextlib import contextmanager, ExitStack
import copy
import importlib.util
import json
import os
import subprocess
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_settings_successor as gate
import enrollment_candidate_runtime as runtime
import enrollment_candidate_interactive_launch as visible
import enrollment_candidate_predevice_resume as resume
import enrollment_candidate_owner_workflow as owner

spec = importlib.util.spec_from_file_location('_observed_legacy_fixtures',
    ROOT / 'tests/host/enrollment_candidate_settings_successor_tests.py')
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)


@contextmanager
def barriers():
    """File admission stays real; all operational callbacks must remain absent."""
    blocked = Mock(side_effect=AssertionError('forbidden operational dispatch'))
    with ExitStack() as stack:
        for name in ('Popen', 'run', 'call', 'check_call', 'check_output'):
            stack.enter_context(patch.object(subprocess, name, blocked))
        for name in ('system', 'startfile', 'spawnl', 'spawnle', 'spawnv', 'spawnve', 'posix_spawn'):
            if hasattr(os, name):
                stack.enter_context(patch.object(os, name, blocked))
        for name in ('native_admission', 'require_ready', 'require_continuity', 'require_practice'):
            stack.enter_context(patch.object(visible, name, blocked))
        stack.enter_context(patch.object(runtime, 'launch', blocked))
        stack.enter_context(patch.object(runtime, 'verify_assembly', blocked))
        stack.enter_context(patch.object(owner.Workflow, 'execute', blocked))
        yield blocked
        blocked.assert_not_called()
    assert not any(n == 'serial' or n.startswith(('serial.', 'esptool')) for n in sys.modules)


class ObservedFixture(legacy.RecoveryAFixture):
    """Real c13/profile bytes; only inherited historical fixture anchors relocate."""
    def __init__(self):
        super().__init__()
        self.recovery_profile_ref = self.profile_ref
        for name in gate.OBSERVATION_DELTA:
            self.raw(self.tools / name, (ROOT / 'tools' / name).read_bytes())
        evidence = self.private / 'ot0238c13-recovery-observation-20261014'
        evidence.mkdir()
        for name in ('validation.json', 'independent-review.json'):
            self.raw(evidence / name,
                (ROOT / '.private/ot0238c13-recovery-observation-20261014' / name).read_bytes())
        self.stage = self.private / gate.OBSERVED_STAGE
        self.stage.mkdir()
        self.prestage = self.stage / 'prestage'
        self.prestage.mkdir()
        self.output = self.stage / 'sessions' / ('f' * 32)
        self.output.mkdir(parents=True)
        self.readiness = self.stage / 'readiness' / ('f' * 32)
        self.readiness.mkdir(parents=True)
        self.profile_ref = self.raw(self.stage / 'successor-admission-profile.json',
            (ROOT / '.private' / gate.OBSERVED_STAGE / 'successor-admission-profile.json').read_bytes())
        # New trust constants remain unpatched in both the module and bound gate.
        self.profile, _, _, _ = gate.observed_successor_profile(self.private)
        self.delta = copy.deepcopy(self.profile['cumulative_sources'])
        assembly = json.loads(Path(self.assembly['path']).read_bytes())
        policy = Path(self.manifest['root']) / 'policy'
        for name, pair in gate.OBSERVATION_DELTA.items():
            raw = (ROOT / 'tools' / name).read_bytes()
            ref = self.raw(policy / name, raw)
            assert {k:ref[k] for k in ('bytes', 'sha256')} == pair['after']
            assembly['sources']['tools/' + name] = pair['after']
            self.manifest['files']['policy/' + name] = pair['after']
            self.binding['sources'][name] = pair['after']
        self.runtime = self.write(self.private / 'runtime.json', self.manifest)
        assembly['runtime'] = self.runtime
        assembly['source_pins'] = self.write(self.private / 'source-pins.json',
            dict(schema='OT-CANDIDATE-SOURCE-PINS-1', files=assembly['sources']))
        self.assembly = self.write(self.private / 'assembly.json', assembly)
        self.binding.update(assembly=self.assembly,
            session=self.raw(self.prestage / 'session.template.py', b'inert observed recovery-A fixture'),
            session_output=str(self.output), readiness_root=str(self.readiness))
        self.request = dict(self.historical, schema=gate.REQUEST, runtime_sha256=self.runtime['sha256'])
        self.requestref = self.write(self.prestage / 'capture-request.json', self.request)
        self.transition.update(kind=gate.OBSERVED_KIND, sources=self.delta, admission_profile=self.profile_ref,
            successor_assembly=self.assembly, successor_runtime=self.runtime)
        self.resolution.update(schema='OT-CURRENT-SETTINGS-SUCCESSOR-7',
            scope='fresh_current_nvs_capture_for_observed_recovery_after_A_commit',
            capture_request=self.requestref, successor_runtime=self.runtime,
            successor_session=self.binding['session'], successor_root=str(self.output))
        self.sourcereview.update(schema='OT-RECOVERY-A-OBSERVED-SUCCESSOR-SOURCE-REVIEW-1',
            capture_request=self.requestref, successor_runtime=self.runtime,
            admission_profile=self.profile_ref, sources=self.delta,
            reviewed_observation_delta=self.profile['reviewed_observation_delta'],
            observation_review=self.profile['observation_review'],
            observation_validation=self.profile['observation_validation'])
        self.fixed['runtime'] = self.runtime
        self.refresh_recovery_a()

    def bound_loader(self):
        # Existing historical evidence relocation is the legacy fixture seam.
        # Neither new profile/c13 review pin nor observation source pin is patched.
        text = (ROOT / 'tools' / gate.NAME).read_text()
        for name, ref in (('LINKAGE', self.linkage), ('DISPOSITION', self.disposition),
                ('PRESERVATION', self.preservation), ('FULL_PROFILE_PIN', self.prior_profile_ref),
                ('RECOVERY_A_PROFILE_PIN', self.recovery_profile_ref)):
            text = '\n'.join(name + ' = ' + repr({k:ref[k] for k in ('bytes', 'sha256')})
                if line.startswith(name + ' = ') else line for line in text.splitlines()) + '\n'
        ref = self.raw(self.tools / gate.NAME, text.encode())
        self.binding['sources'][gate.NAME] = {k:ref[k] for k in ('bytes', 'sha256')}
        self.refresh_recovery_a()
        return self.bound


class ObservedSuccessorTests(unittest.TestCase):
    def fixture(self):
        f = ObservedFixture()
        self.addCleanup(f.close)
        f.bound_loader()
        return f

    def assert_inert(self, f):
        self.assertEqual(list(f.output.iterdir()), [])
        self.assertEqual(list(f.readiness.iterdir()), [])

    def entries(self, f, *, positive=False, direct=True):
        bound = f.write(f.stage / 'binding.json', f.binding)
        assembly = type('Assembly', (), dict(worktree=f.root,
            root=Path(f.manifest['root']), manifest=f.manifest))()
        with patch.object(visible, 'ROOT', f.root), patch.object(owner, 'ROOT', f.root), barriers() as blocked:
            if direct:
                if positive:
                    self.assertTrue(f.admit())
                else:
                    with self.assertRaises(gate.SettingsSuccessorError):
                        f.admit()
            if positive:
                self.assertTrue(resume.require_recovery_gate(f.binding))
                self.assertIsNone(owner.require_cleanup_recovery(bound))
                admitted, current = visible.admit_binding(bound['path'], bound['sha256'],
                    assembly_verifier=lambda *args: assembly)
                self.assertEqual(admitted, f.binding)
                self.assertEqual(current, bound)
                self.assertEqual(resume.selection(f.item), ('recovery_after_A_commit', 1))
            else:
                for label, call, error in (
                        ('predevice', lambda: resume.require_recovery_gate(f.binding), visible.AdmissionError),
                        ('interactive', lambda: visible.admit_binding(bound['path'], bound['sha256'],
                            assembly_verifier=blocked), (visible.AdmissionError, runtime.RuntimeErrorFixed)),
                        ('owner', lambda: owner.require_cleanup_recovery(bound), owner.Refused)):
                    with self.subTest(entry=label), self.assertRaises(error):
                        call()
        self.assert_inert(f)

    def test_actual_profile_c13_review_validation_and_sources_without_trust_patches(self):
        with barriers():
            profile, ref, core_ref, archived = gate.full_successor_profile(ROOT / '.private', gate.OBSERVED_KIND)
        self.assertEqual(runtime.descriptor(Path(ref['path']).read_bytes()), gate.OBSERVED_PROFILE_PIN)
        self.assertEqual(profile['selection'], dict(case='recovery_after_A_commit', group=1))
        self.assertEqual(set(profile['cumulative_sources']),
            set(gate.CORE) | {'enrollment_candidate_runtime.py'} | set(gate.OBSERVATION_DELTA))
        for name, pair in gate.OBSERVATION_DELTA.items():
            self.assertEqual(runtime.descriptor((ROOT / 'tools' / name).read_bytes()), pair['after'])
            self.assertEqual(runtime.descriptor(legacy.historical_source(name)), pair['before'])
        self.assertEqual(core_ref['sha256'], gate.CORE_REVIEW_PIN['sha256'])
        self.assertEqual(archived['sources']['enrollment_candidate_controller.py'], gate.RECOVERY_A_CONTROLLER_PIN)
        self.assertEqual(set(gate.PACKAGE_CALLERS), {gate.NAME, resume.NAME,
            'enrollment_candidate_interactive_launch.py', 'enrollment_candidate_owner_workflow.py'})

    def test_exact_c13_route_reaches_four_real_file_gates_without_dispatch(self):
        self.entries(self.fixture(), positive=True)

    def test_each_single_c13_source_detects_stripped_schema_helper_transition_and_stage(self):
        for name, pair in gate.OBSERVATION_DELTA.items():
            with self.subTest(source=name):
                f = self.fixture()
                f.binding.update(schema='OT-VISIBLE-SESSION-BINDING-1', sources={name:pair['after']},
                    session={'path':str(f.private / 'unrelated/session.template.py')})
                f.binding.pop('predevice')
                self.entries(f, direct=False)
                f.close()
                self._cleanups.pop()

    def test_old_mixed_swapped_and_changed_c13_sources_refuse(self):
        names = list(gate.OBSERVATION_DELTA)
        faults = [(name, 'old') for name in names] + [(name, 'swapped') for name in names] + [(name, 'bytes') for name in names]
        for name, fault in faults:
            with self.subTest(source=name, fault=fault):
                f = self.fixture()
                if fault == 'old':
                    pin = gate.OBSERVATION_DELTA[name]['before']
                    f.raw(f.tools / name, legacy.historical_source(name))
                elif fault == 'swapped':
                    other = names[(names.index(name) + 1) % len(names)]
                    pin = gate.OBSERVATION_DELTA[other]['after']
                    f.raw(f.tools / name, (ROOT / 'tools' / other).read_bytes())
                else:
                    pin = runtime.descriptor(b'inert forged source')
                    f.raw(f.tools / name, b'inert forged source')
                f.binding['sources'][name] = pin
                f.delta[name]['after'] = pin
                f.refresh_recovery_a()
                self.entries(f)
                f.close()
                self._cleanups.pop()

    def test_wrong_case_group_runtime_and_deadline_values_refuse(self):
        faults = [('case',v) for v in ('first', 'retained_rekey', 'cancel', 'revoke', 'recovery_after_B_commit', None, True)]
        faults += [('group',v) for v in (True, False, 1.0, '1', 0, 2, None)]
        faults += [('runtime', {}), ('proposed_execute_seconds', 2101),
            ('proposed_execute_seconds', True), ('proposed_restore_seconds', 3599),
            ('proposed_restore_seconds', 3601)]
        for key, value in faults:
            with self.subTest(key=key, value=value):
                f = self.fixture()
                f.fixed[key] = value
                f.refresh_recovery_a()
                self.entries(f)
                f.close()
                self._cleanups.pop()

    def test_new_profile_and_c13_evidence_cannot_be_missing_changed_or_semantically_resealed(self):
        for target in ('profile', 'observation_review', 'observation_validation'):
            for fault in ('missing', 'changed', 'resealed'):
                with self.subTest(target=target, fault=fault):
                    f = self.fixture()
                    ref = (f.profile_ref if target == 'profile' else f.profile[target])
                    path = Path(ref['path'])
                    if fault == 'missing':
                        path.unlink()
                    elif fault == 'changed':
                        f.raw(path, path.read_bytes() + b'changed')
                    else:
                        value = json.loads(path.read_bytes())
                        if target == 'profile':
                            value['reviewed_observation_delta']['enrollment_candidate_controller.py']['after'] = gate.RECOVERY_A_CONTROLLER_PIN
                        elif target == 'observation_review':
                            value['findings'] = ['forged approval']
                        else:
                            value['final_matrix_passed'] = False
                        substituted = f.write(path, value)
                        if target == 'profile':
                            f.transition['admission_profile'] = substituted
                        else:
                            f.sourcereview[target] = substituted
                        f.refresh_recovery_a()
                    self.entries(f)
                    f.close()
                    self._cleanups.pop()

    def test_stripped_transition_wrong_kind_stage_and_old_route_downgrade_refuse(self):
        for fault in ('strip', 'missing_predevice', 'binding_schema', 'kind', 'old_kind', 'old_stage', 'arbitrary_stage'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'strip':
                    f.item.pop('runtime_transition')
                    f.close_manifest()
                elif fault == 'missing_predevice':
                    f.binding.pop('predevice')
                elif fault == 'binding_schema':
                    f.binding['schema'] = 'OT-VISIBLE-SESSION-BINDING-1'
                    f.binding.pop('predevice')
                elif fault in ('kind', 'old_kind'):
                    f.transition['kind'] = gate.FULL_KIND if fault == 'kind' else gate.RECOVERY_A_KIND
                    f.refresh_recovery_a()
                else:
                    stage = gate.RECOVERY_A_STAGE if fault == 'old_stage' else 'arbitrary'
                    parent = f.private / stage / 'prestage'
                    parent.mkdir(parents=True, exist_ok=True)
                    f.binding['session'] = f.raw(parent / 'session.template.py', b'inert relocated fixture')
                    f.refresh_recovery_a()
                self.entries(f, direct=fault not in ('strip', 'missing_predevice', 'binding_schema'))
                f.close()
                self._cleanups.pop()

    def test_selection_ref_and_future_source_review_must_close_exact_observation(self):
        for fault in ('fixed_reference', 'source_review_missing', 'source_review_findings',
                'source_review_observation', 'source_review_delta'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'fixed_reference':
                    f.item['successor']['fixed_inputs'] = f.write(f.stage / 'unbound-fixed.json', f.fixed)
                    f.close_manifest()
                elif fault == 'source_review_missing':
                    Path(f.reviewref['path']).unlink()
                else:
                    key = {'source_review_findings':'findings', 'source_review_observation':'observation_review',
                        'source_review_delta':'reviewed_observation_delta'}[fault]
                    f.sourcereview[key] = ['unresolved'] if key == 'findings' else {}
                    f.refresh_recovery_a()
                self.entries(f)
                f.close()
                self._cleanups.pop()

    def test_forged_future_binding_and_succession_reviews_refuse(self):
        for target in ('binding', 'succession'):
            for fault in ('hash', 'schema', 'findings', 'hardware', 'grant', 'missing'):
                with self.subTest(target=target, fault=fault):
                    f = self.fixture()
                    item = f.binding if target == 'binding' else f.item
                    path = Path(item['review']['path'])
                    if fault == 'missing':
                        path.unlink()
                    else:
                        value = json.loads(path.read_bytes())
                        if fault == 'hash':
                            value['binding_core_sha256' if target == 'binding' else 'manifest_core_sha256'] = '0' * 64
                        elif fault == 'schema':
                            value['schema'] = 'forged'
                        elif fault == 'findings':
                            value['findings'] = ['unresolved']
                        else:
                            value['hardware_access' if fault == 'hardware' else 'live_grants_created'] = True
                        item['review'] = f.write(path, value)
                        if target == 'succession':
                            f.close_binding()
                        else:
                            f.bound = f.write(f.stage / 'binding.json', f.binding)
                    self.entries(f)
                    f.close()
                    self._cleanups.pop()

    def test_consumed_predecessor_and_diagnostic_nvs_authority_cannot_be_adopted(self):
        for fault in ('consumed', 'physical_binding', 'diagnostic_nvs', 'release', 'replace', 'eligible', 'request_nvs'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'consumed':
                    f.raw(f.old_output / 'enrollment-candidate-active.json', b'inert consumed fixture')
                elif fault == 'physical_binding':
                    f.previous['binding'] = f.write(f.stage / 'physical-binding.json',
                        dict(f.old, schema='OT-VISIBLE-SESSION-BINDING-2'))
                elif fault == 'request_nvs':
                    f.request['roles']['A']['originals'] = {'nvs':dict(bytes=12288, sha256='0' * 64)}
                    f.requestref = f.write(Path(f.requestref['path']), f.request)
                    f.resolution['capture_request'] = f.requestref
                    f.sourcereview['capture_request'] = f.requestref
                else:
                    key = {'diagnostic_nvs':'diagnostic_nvs_adopted', 'release':'historical_custody_released',
                        'replace':'historical_baseline_replaced', 'eligible':'candidate_eligible'}[fault]
                    f.resolution[key] = True
                f.refresh_recovery_a()
                # Predecessor current-state exclusion belongs to outer entry guards.
                self.entries(f, direct=fault not in ('consumed', 'physical_binding'))
                f.close()
                self._cleanups.pop()


if __name__ == '__main__':
    assert sys.dont_write_bytecode and sys.flags.utf8_mode and not sys.flags.optimize
    unittest.main()
