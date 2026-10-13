"""Synthetic host checks only; no physical receipts, grants or authorization issued."""
from copy import deepcopy
import importlib.util
import hmac
import hashlib
import io
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools' / 'enrollment_candidate_recovery_diagnostic.py'
spec = importlib.util.spec_from_file_location('diagnostic_under_test', SOURCE)
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)


class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.private = Path(self.temp.name) / '.private'
        self.old, self.output, self.policy = self.private / 'old', self.private / 'output', self.private / 'capsule' / 'policy'
        for p in (self.old, self.output, self.policy): p.mkdir(parents=True)
        for i in range(39): (self.old / f'legacy-{i}.json').write_bytes(b'preserved')
        pins = {}
        for name in d.SOURCE_NAMES:
            path = self.policy / name
            path.write_bytes(b'# synthetic host fixture; never imported\n')
            pins['tools/' + name] = {'bytes': path.stat().st_size, 'sha256': d.sha(path.read_bytes())}
        self.pins = self.put('pins', {'schema': 'OT-CANDIDATE-SOURCE-PINS-1', 'files': pins})
        self.pin_patch = patch.object(d, 'REVIEWED_V18_SOURCE_PINS_SHA256', self.pins['sha256'])
        self.pin_patch.start(); self.addCleanup(self.pin_patch.stop)
        self.manifest_ref = self.put('manifest', {'root': str(self.policy.parent),
            'worktree': str(self.private.parent),
            'files': {'policy/' + Path(n).name: p for n, p in pins.items()}})
        runtime_patch = patch.object(d, 'REVIEWED_V18_RUNTIME_SHA256', self.manifest_ref['sha256'])
        runtime_patch.start(); self.addCleanup(runtime_patch.stop)
        preserved = {'schema': 'OT-LEGACY-SESSION-PRESERVATION-1', 'session': str(self.old),
                     'before_equals_after': True, 'files': {str(p): d.reference(p, self.private) for p in self.old.iterdir()}}
        self.legacy = self.put('preserved', preserved)
        self.disposition = self.put('disposition', {'schema': 'OT-MANUAL-RECOVERY-DISPOSITION-1',
            'status': 'manual_recovery_observed_baseline_unresolved', 'session': str(self.old),
            'candidate_eligible': False, 'automatic_custody_released': False, 'references': {}})
        self.identities = {'A': '123456789abc', 'B': '223456789abc'}
        self.request = {'schema': 'OT-RETAINED-ORIGINAL-REQUEST-1', 'runtime_sha256': self.manifest_ref['sha256'],
            'roles': {r: {'device_binding': hmac.new(b'x' * 32, b'OT218-DEVICE\0' + self.identities[r].encode(), hashlib.sha256).hexdigest(),
                         'profile_evidence_sha256': '4' * 64} for r in ('A', 'B')}, 'retained': {}}
        originals = {n: {'bytes': 1, 'sha256': '5' * 64} for n in d.IMMUTABLE | {'nvs'}}
        self.comparator = {'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': self.request['runtime_sha256'],
            'roles': {r: {'device_binding': self.request['roles'][r]['device_binding'], 'originals': originals}
                      for r in ('A', 'B')}}
        self.grant = {'schema': 'OT-ORIGINAL-CAPTURE-GRANT-1', 'attempt': '6' * 32,
            'request_sha256': d.sha(d.canonical(self.request)), 'runtime_sha256': self.request['runtime_sha256'],
            'operation': 'capture', 'origin_attempt': None, 'attempt_count': 1, 'actions': d.ACTIONS,
            'issued_utc': 1000, 'capture_expires_utc': 3100, 'cleanup_expires_utc': 4600}
        self.package = {'schema': 'OT-RETAINED-RECOVERY-DIAGNOSTIC-PACKAGE-1',
            'private_root': str(self.private), 'evidence_root': str(self.output), 'source_root': str(self.policy),
            'source_pins': self.pins, 'assembly': self.put('assembly', {
                'schema': 'OT-CANDIDATE-ASSEMBLY-1', 'root': str(self.policy.parent),
                'source_root': str(self.private.parent), 'source_pins': self.pins, 'sources': pins,
                'runtime': self.manifest_ref}),
            'capture_request': self.put('request', self.request), 'capture_grant': self.put('grant', self.grant),
            'identities': self.put('identities', {'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': self.identities}),
            'profiles': self.put('profiles', {'schema': 'OT-CANDIDATE-PROFILES-1', 'roles': {
                r: {'model': 'heltec_v4_esp32s3', 'device_binding': self.request['roles'][r]['device_binding'],
                    'flash_bytes': 16777216, 'evidence_sha256': '4' * 64} for r in ('A', 'B')}}),
            'binding_key': self.raw('key', b'x' * 32), 'comparator_request': self.put('comparator', self.comparator),
            'legacy_preservation': self.legacy, 'legacy_disposition': self.disposition,
            'legacy_gate_module': self.raw('gate', b'#fixture'), 'controller_sha256': d.sha(SOURCE.read_bytes())}
        self.package_ref = self.put('package', self.package)
        for name, ref in (('REVIEWED_QUARANTINE_SHA256', self.disposition),
                          ('REVIEWED_PRESERVATION_SHA256', self.legacy),
                          ('REVIEWED_GATE_SHA256', self.package['legacy_gate_module'])):
            fixture_patch = patch.object(d, name, ref['sha256'])
            fixture_patch.start(); self.addCleanup(fixture_patch.stop)
        self.auth = {'schema': 'OT-RECOVERY-DIAGNOSTIC-AUTHORIZATION-1', 'scope': d.SCOPE,
            'owner_approved': True, 'package_sha256': self.package_ref['sha256'],
            'controller_sha256': self.package['controller_sha256'],
            'capture_grant_sha256': self.package['capture_grant']['sha256'], 'capture_attempt': self.grant['attempt'],
            'legacy_disposition_sha256': self.disposition['sha256'],
            'identities_sha256': self.package['identities']['sha256'], 'profiles_sha256': self.package['profiles']['sha256'],
            'actions': d.ACTIONS, 'issued_utc': 1000, 'capture_expires_utc': 3100, 'cleanup_expires_utc': 4600,
            'one_use': True, 'fresh_identity_profiles_reviewed': True,
            'rom_entry_and_guarded_original_boot_authorized': True, 'candidate_operations_authorized': False}
        self.auth_ref = self.put('auth', self.auth)

    def raw(self, name, raw):
        path = self.private / (name + '.json')
        path.write_bytes(raw)
        return d.reference(path, self.private)

    def put(self, name, value): return self.raw(name, d.canonical(value) + b'\n')

    def admit(self):
        return d._admit(self.package_ref['path'], self.package_ref['sha256'],
                        self.auth_ref['path'], self.auth_ref['sha256'], True,
                        utc=lambda: 1100, monotonic=lambda: 500)

    def test_manifest_pinned_support_files_admit_and_recursive_inventory_refuses_extra_missing(self):
        support = self.policy / 'support' / 'helper.py'
        support.parent.mkdir()
        support.write_bytes(b'# unchanged base support fixture\n')
        manifest = d.decode(d.read_ref(self.manifest_ref, self.private))
        manifest['files']['policy/support/helper.py'] = d.reference(support, self.private)
        del manifest['files']['policy/support/helper.py']['path']
        self.manifest_ref = self.put('manifest', manifest)
        d.REVIEWED_V18_RUNTIME_SHA256 = self.manifest_ref['sha256']
        self.request['runtime_sha256'] = self.manifest_ref['sha256']
        self.comparator['runtime_sha256'] = self.manifest_ref['sha256']
        self.grant.update(runtime_sha256=self.manifest_ref['sha256'],
                          request_sha256=d.sha(d.canonical(self.request)))
        assembly = d.decode(d.read_ref(self.package['assembly'], self.private))
        assembly['runtime'] = self.manifest_ref
        self.package.update(assembly=self.put('assembly', assembly),
                            capture_request=self.put('request', self.request),
                            comparator_request=self.put('comparator', self.comparator),
                            capture_grant=self.put('grant', self.grant))
        self.package_ref = self.put('package', self.package)
        d.inspect(self.package_ref['path'], self.package_ref['sha256'])
        extra = support.parent / 'extra.py'
        extra.write_bytes(b'# unreviewed nested extra\n')
        with self.assertRaisesRegex(d.DiagnosticError, 'diagnostic_source_tuple_changed'):
            d.inspect(self.package_ref['path'], self.package_ref['sha256'])
        extra.unlink()
        support.write_bytes(b'# modified support fixture\n')
        with self.assertRaisesRegex(d.DiagnosticError, 'diagnostic_reference_changed'):
            d.inspect(self.package_ref['path'], self.package_ref['sha256'])
        support.unlink()
        with self.assertRaisesRegex(d.DiagnosticError, 'diagnostic_source_tuple_changed'):
            d.inspect(self.package_ref['path'], self.package_ref['sha256'])

    def test_default_and_boolean_alone_refuse_before_import_or_marker(self):
        with patch.object(d, '_load_dependencies', side_effect=AssertionError('must not import')):
            with self.assertRaisesRegex(d.DiagnosticError, 'execution_not_authorized'):
                d.execute('missing', '0' * 64)
            with self.assertRaisesRegex(d.DiagnosticError, 'authorization_missing'):
                d.execute('missing', '0' * 64, approved_device_execution=True)
        self.assertFalse(any(self.output.iterdir()))

    def test_expired_invalid_or_mismatched_authorization_refuses_before_marker(self):
        for field, value in (('owner_approved', False), ('package_sha256', '0' * 64),
            ('controller_sha256', '0' * 64), ('capture_attempt', '0' * 32), ('actions', ['write']),
            ('candidate_operations_authorized', True), ('capture_expires_utc', 3101),
            ('one_use', False), ('fresh_identity_profiles_reviewed', False)):
            self.auth_ref = self.put('auth', dict(self.auth, **{field: value}))
            with self.assertRaises(d.DiagnosticError): self.admit()
            self.assertFalse(any(self.output.iterdir()))
        self.auth_ref = self.put('auth', self.auth)
        with self.assertRaisesRegex(d.DiagnosticError, 'authorization_expired'):
            d._admit(self.package_ref['path'], self.package_ref['sha256'], self.auth_ref['path'],
                     self.auth_ref['sha256'], True, utc=lambda: 3100, monotonic=lambda: 500)
        self.assertFalse(any(self.output.iterdir()))

    def test_source_legacy_and_grant_changes_refuse_before_admission(self):
        for path in (self.policy / 'enrollment_candidate_rom_adapter.py', self.old / 'legacy-1.json'):
            before = path.read_bytes(); path.write_bytes(b'changed')
            with self.assertRaises(d.DiagnosticError): self.admit()
            self.assertFalse(any(self.output.iterdir())); path.write_bytes(before)
        self.package['capture_grant'] = self.put('grant', dict(self.grant, actions=['candidate_write']))
        self.package_ref = self.put('package', self.package)
        with self.assertRaises(d.DiagnosticError): self.admit()
        self.assertFalse(any(self.output.iterdir()))

    def test_inspect_is_inert_and_admission_one_use_preserves_original_ceilings(self):
        with patch.object(d, '_load_dependencies', side_effect=AssertionError('must not import')):
            d.inspect(self.package_ref['path'], self.package_ref['sha256'])
        self.assertFalse(any(self.output.iterdir()))
        prepared = self.admit()
        self.assertEqual(prepared['capture_deadline'], 2500)
        self.assertEqual(prepared['cleanup_deadline'], 4000)
        with self.assertRaises(d.DiagnosticError): self.admit()
        with self.assertRaisesRegex(d.DiagnosticError, 'isolated_caller_required'):
            d._load_dependencies(prepared)

    def payload(self, prepared, role='A', operation='guard'):
        grant = prepared['grant']
        return {'operation': operation, 'mode': 'capture-only', 'identity': self.identities[role],
            'authority': {'schema': 'OT-CANDIDATE-CAPTURE-AUTHORITY-1', 'runtime_sha256': self.request['runtime_sha256'],
             'request_sha256': grant['request_sha256'], 'grant_sha256': prepared['package']['capture_grant']['sha256'],
             'attempt': grant['attempt'], 'role': role, 'device_binding': self.request['roles'][role]['device_binding'],
             'actions': d.ACTIONS, 'capture_deadline': prepared['capture_deadline'], 'cleanup_deadline': prepared['cleanup_deadline']},
            'observed': deepcopy(self.comparator['roles'][role]['originals'])}

    def test_runtime_whitelist_authority_reset_one_use_and_validated_proofs(self):
        prepared, calls = self.admit(), []
        def invoke(payload, deadline):
            calls.append(payload['operation'])
            return {'ok': True, 'closed': True, 'admission': {'boot_selection': 'verified-factory', 'layout_sha256': '5' * 64}}
        wrapped = d.GuardedRuntime(SimpleNamespace(invoke=invoke), prepared, self.identities)
        for op in ('write', 'boot_candidate', 'restart_candidate', 'route', 'hold_rom'):
            with self.assertRaises(d.DiagnosticError): wrapped.invoke(self.payload(prepared, operation=op), 2000)
        for field, value in (('mode', 'normal'), ('identity', 'other')):
            payload = self.payload(prepared); payload[field] = value
            with self.assertRaises(d.DiagnosticError): wrapped.invoke(payload, 2000)
        payload = self.payload(prepared); payload['authority']['actions'] = ['write']
        with self.assertRaises(d.DiagnosticError): wrapped.invoke(payload, 2000)
        self.assertFalse(calls)
        wrapped.invoke(self.payload(prepared, operation='reset_original'), 3000)
        self.assertEqual(len(wrapped.proofs), 1)
        self.assertEqual(set(wrapped.proofs[0]['immutable_five']), d.IMMUTABLE)
        with self.assertRaisesRegex(d.DiagnosticError, 'reset_replay_refused'):
            wrapped.invoke(self.payload(prepared, operation='reset_original'), 3000)
        self.assertEqual(calls, ['reset_original'])

    def test_invalid_worker_answer_creates_no_proof_and_uncertain_reset_never_replays(self):
        prepared = self.admit()
        wrapped = d.GuardedRuntime(SimpleNamespace(invoke=lambda *args: {'ok': False}), prepared, self.identities)
        with self.assertRaisesRegex(d.DiagnosticError, 'worker_proof_invalid'):
            wrapped.invoke(self.payload(prepared, operation='reset_original'), 3000)
        self.assertFalse(wrapped.proofs)
        with self.assertRaisesRegex(d.DiagnosticError, 'reset_replay_refused'):
            wrapped.invoke(self.payload(prepared, operation='reset_original'), 3000)

    def synthetic_modules(self, prepared, scenario):
        events, outer = [], self
        rows = {r: {'owner': 'capture', 'entered': True, 'handles_closed': True,
                    'original_boot_allowed': True, 'pins': deepcopy(self.comparator['roles'][r]['originals']),
                    'reset_intent': True} for r in ('A', 'B')}
        class Runtime:
            def __init__(self, *args, **kwargs): events.append('runtime_constructor')
            def invoke(self, payload, deadline):
                events.append('invoke_' + payload['operation'] + '_' + payload['authority']['role'])
                if scenario == 'uncertain_reset' and payload['operation'] == 'reset_original': raise RuntimeError('uncertain')
                return {'ok': True, 'closed': True, 'admission': {'boot_selection': 'verified-factory', 'layout_sha256': '5' * 64}}
        class Lease:
            def __init__(self, *args, **kwargs): events.append('lease_constructor')
            def close(self): events.append('lease_close'); return scenario != 'uncertain_reset'
        class Session:
            def __init__(self, *args, **kwargs):
                events.append('session_constructor'); self.runtime = kwargs['runtime']; self.started = False
                self._result = SimpleNamespace(outcome='not_started', capture_complete=False, roles={}, cleanup_failure=None,
                                                journal_path=None)
            @property
            def result(self): return self._result
            def capture(self):
                events.append('capture')
                if scenario == 'start_failure': raise RuntimeError('before state')
                self.started = True
                if scenario == 'partial_A':
                    partial = deepcopy(rows); partial['B']['entered'] = False
                    self._result = SimpleNamespace(outcome='held', capture_complete=False, roles=partial,
                        cleanup_failure=None, journal_path=None)
                    raise RuntimeError('partial')
                self._result = SimpleNamespace(outcome='captured', capture_complete=True, roles=rows,
                    cleanup_failure=None, journal_path=None)
                return self._result
            def verify(self, *args, **kwargs):
                events.append('verify')
                if scenario in ('verify_failure', 'deadline_failure'): raise RuntimeError('freshness')
                return True
            def release(self):
                events.append('release')
                for role in ('A', 'B'):
                    if not self._result.roles[role]['entered']: continue
                    try: self.runtime.invoke(outer.payload(prepared, role, 'reset_original'), 3000)
                    except Exception: self._result.cleanup_failure = ('release', 'uncertain'); self._result.roles[role]['original_boot_allowed'] = False
                if scenario == 'cleanup_failure': self._result.cleanup_failure = ('release', 'failed')
                journal = {'phase': 'released' if self._result.cleanup_failure is None else 'held',
                    'capture_complete': self._result.capture_complete, 'candidate': None,
                    'cleanup_failure': self._result.cleanup_failure, 'roles': self._result.roles,
                    'request': prepared['request'], 'grant_sha256': prepared['package']['capture_grant']['sha256'],
                    'attempt': prepared['grant']['attempt']}
                path = prepared['output'] / ('enrollment-original-' + prepared['grant']['attempt'] + '.json')
                path.write_bytes(d.canonical(journal)); self._result.journal_path = path
                return self._result
        modules = {'enrollment_candidate_original_capture': SimpleNamespace(CaptureSession=Session,
            validate_request=lambda x: x, validate_grant=lambda *a: None),
            'enrollment_candidate_rom_adapter': SimpleNamespace(Runtime=Runtime, HardwareLease=Lease,
                identity=lambda x: x, DeviceProfile=lambda **kw: kw),
            'enrollment_candidate_runtime': SimpleNamespace(verify_assembly=lambda *a, **kw:
                SimpleNamespace(root=self.policy.parent, manifest_sha256=self.request['runtime_sha256'], manifest_path=self.private / 'manifest'))}
        return modules, events

    def run_synthetic(self, scenario):
        prepared = self.admit()
        modules, events = self.synthetic_modules(prepared, scenario)
        before = {p: p.read_bytes() for p in self.old.iterdir()}
        receipt_ref = d._run_admitted(prepared, modules)
        self.assertEqual(before, {p: p.read_bytes() for p in self.old.iterdir()})
        self.assertIn('lease_close', events)
        self.assertLessEqual(events.count('release'), 1)
        for role in ('A', 'B'): self.assertLessEqual(events.count('invoke_reset_original_' + role), 1)
        return receipt_ref, d.decode(d.read_ref(receipt_ref, self.private)), events

    def test_start_failure_closes_lease_without_release_or_reset(self):
        ref, receipt, events = self.run_synthetic('start_failure')
        self.assertNotIn('release', events)
        self.assertFalse(receipt['capture_complete'])
        self.assertFalse(receipt['proofs'])

    def test_verify_and_deadline_failures_terminal_cleanup_once(self):
        ref, receipt, events = self.run_synthetic('verify_failure')
        self.assertEqual(events.count('release'), 1)
        self.assertFalse(receipt['freshness_verified'])
        self.assertFalse(receipt['candidate_eligible'])
        self.assertEqual(receipt['first_failure'], 'diagnostic_capture_or_freshness_failed')

    def test_deadline_failure_terminal_cleanup_once(self):
        ref, receipt, events = self.run_synthetic('deadline_failure')
        self.assertEqual(events.count('release'), 1)
        self.assertFalse(receipt['freshness_verified'])

    def test_uncertain_reset_preserves_uncertainty_and_cannot_be_consumed(self):
        ref, receipt, events = self.run_synthetic('uncertain_reset')
        self.assertFalse(receipt['lease_closed'])
        self.assertIsNotNone(receipt['cleanup_failure'])
        self.assertFalse(receipt['proofs'])
        owner, review = self.observations(ref)
        with self.assertRaises(d.DiagnosticError):
            d.consume_receipt(ref, owner, review, self.output / 'linkage.json', self.private)

    def test_cleanup_failure_never_consumed(self):
        ref, receipt, events = self.run_synthetic('cleanup_failure')
        owner, review = self.observations(ref)
        with self.assertRaises(d.DiagnosticError):
            d.consume_receipt(ref, owner, review, self.output / 'linkage.json', self.private)

    def test_partial_capture_and_uncertain_reset_cannot_be_consumed(self):
        ref, receipt, events = self.run_synthetic('partial_A')
        owner, review = self.observations(ref)
        with self.assertRaisesRegex(d.DiagnosticError, 'receipt_unresolved'):
            d.consume_receipt(ref, owner, review, self.output / 'linkage.json', self.private)
        self.assertFalse((self.output / 'linkage.json').exists())

    def observations(self, ref):
        owner = self.put('owner', {'schema': 'OT-RECOVERY-DIAGNOSTIC-OWNER-OBSERVATION-1',
            'diagnostic_receipt_sha256': ref['sha256'],
            'roles': {'A': 'usual_screen_owner_confirmed', 'B': 'usual_screen_owner_confirmed'}})
        review = self.put('review', {'schema': 'OT-RECOVERY-DIAGNOSTIC-INDEPENDENT-REVIEW-1',
            'accepted': True, 'diagnostic_receipt_sha256': ref['sha256'], 'owner_observation_sha256': owner['sha256'],
            'controller_sha256': self.package['controller_sha256'], 'source_pins_sha256': self.pins['sha256'],
            'scope': 'new_diagnostic_evidence_linkage_only'})
        return owner, review

    def test_reviewed_new_evidence_linkage_never_releases_legacy_or_candidates(self):
        ref, receipt, events = self.run_synthetic('success')
        owner, review = self.observations(ref)
        linkage = d.consume_receipt(ref, owner, review, self.output / 'linkage.json', self.private)
        value = d.decode(d.read_ref(linkage, self.private))
        self.assertEqual(value['status'], 'fresh_diagnostic_custody_settled')
        self.assertIs(value['candidate_eligible'], False)
        self.assertIs(value['automatic_legacy_custody_released'], False)

    def test_missing_proof_owner_or_review_refuses_consumer(self):
        ref, receipt, events = self.run_synthetic('success')
        for field, value in (('capture_complete', False), ('proofs', []), ('cleanup_failure', ['release', 'uncertain']),
                             ('candidate_eligible', True), ('lease_closed', False)):
            changed = self.put('changed-receipt', dict(receipt, **{field: value}))
            owner, review = self.observations(changed)
            with self.assertRaises(d.DiagnosticError):
                d.consume_receipt(changed, owner, review, self.output / 'linkage.json', self.private)
        owner, review = self.observations(ref)
        badowner = self.put('owner-bad', {'schema': 'OT-RECOVERY-DIAGNOSTIC-OWNER-OBSERVATION-1', 'roles': {}})
        with self.assertRaises(d.DiagnosticError): d.consume_receipt(ref, badowner, review, self.output / 'linkage.json', self.private)
        badreview = self.put('review-bad', {'accepted': False})
        with self.assertRaises(d.DiagnosticError): d.consume_receipt(ref, owner, badreview, self.output / 'linkage.json', self.private)
        self.assertFalse((self.output / 'linkage.json').exists())

    def repin_receipt(self, value):
        path = self.output / 'diagnostic-receipt.json'
        path.write_bytes(d.canonical(value) + b'\n')
        return d.reference(path, self.private)

    def test_wrong_attempt_auth_reset_pin_and_forged_proof_refuse_consumer(self):
        ref, baseline, events = self.run_synthetic('success')
        for change in ('attempt', 'authorization', 'reset_pin', 'reset_attempted', 'guard_proof'):
            value = deepcopy(baseline)
            if change == 'attempt': value['capture_attempt'] = '0' * 32
            elif change == 'authorization':
                value['authorization'] = self.put('forged-auth', dict(self.auth, scope='candidate_execute'))
            elif change == 'reset_pin':
                value['proofs'][0]['reset_current_nvs'] = {'bytes': 12288, 'sha256': '0' * 64}
            elif change == 'reset_attempted': value['reset_attempted'] = ['A']
            else: value['proofs'][0]['safe_current_nvs_guarded'] = False
            changed = self.repin_receipt(value)
            owner, review = self.observations(changed)
            with self.assertRaises(d.DiagnosticError):
                d.consume_receipt(changed, owner, review, self.output / 'linkage.json', self.private)
        self.assertFalse((self.output / 'linkage.json').exists())

    def test_wrong_journal_attempt_and_unentered_role_refuse(self):
        ref, baseline, events = self.run_synthetic('success')
        journal_path = Path(baseline['diagnostic_journal']['path'])
        original = d.decode(journal_path.read_bytes())
        for change in ('attempt', 'unentered'):
            journal = deepcopy(original)
            if change == 'attempt': journal['attempt'] = '0' * 32
            else: journal['roles']['B']['entered'] = False
            journal_path.write_bytes(d.canonical(journal))
            value = deepcopy(baseline)
            value['diagnostic_journal'] = d.reference(journal_path, self.private)
            value['roles'] = journal['roles']
            changed = self.repin_receipt(value)
            owner, review = self.observations(changed)
            with self.assertRaises(d.DiagnosticError):
                d.consume_receipt(changed, owner, review, self.output / 'linkage.json', self.private)

    def test_repinning_source_or_history_cannot_waive_reviewed_pins(self):
        for field in ('source_pins', 'legacy_preservation', 'legacy_disposition', 'legacy_gate_module'):
            original = self.package[field]
            self.package[field] = self.put('forged-' + field, {'forged': True})
            changed = self.put('package', self.package)
            with self.assertRaises(d.DiagnosticError): d.inspect(changed['path'], changed['sha256'])
            self.package[field] = original
        self.assertFalse(any(self.output.iterdir()))

    def test_execute_cli_failure_never_claims_zero_device_actions(self):
        buffer = io.StringIO()
        with patch.object(d, 'execute', side_effect=RuntimeError('after entry')), redirect_stdout(buffer):
            self.assertEqual(d.main(['--execute', '--package', 'not-read', '--package-sha256', '0' * 64,
                         '--authorization', 'not-read', '--authorization-sha256', '0' * 64]), 1)
        self.assertEqual(d.decode(buffer.getvalue())['device_actions'], 'unknown_may_have_started')

    def test_consumer_rejects_rebound_comparator_identity(self):
        ref, receipt, events = self.run_synthetic('success')
        comparator = deepcopy(self.comparator)
        comparator['roles']['A']['device_binding'] = '0' * 64
        package = dict(self.package, comparator_request=self.put('forged-comparator', comparator))
        package_ref = self.put('forged-package', package)
        auth_ref = self.put('forged-comparator-auth', dict(self.auth, package_sha256=package_ref['sha256']))
        receipt.update(package=package_ref, authorization=auth_ref)
        changed = self.repin_receipt(receipt)
        owner, review = self.observations(changed)
        with self.assertRaisesRegex(d.DiagnosticError, 'receipt_unresolved'):
            d.consume_receipt(changed, owner, review, self.output / 'linkage.json', self.private)

    def launcher_module(self):
        source = SOURCE.with_name('enrollment_candidate_recovery_diagnostic_launcher.py')
        spec = importlib.util.spec_from_file_location('launcher_under_test', source)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module

    def test_launcher_ceiling_wait_does_not_kill_or_detach_cleanup_owner(self):
        launcher = self.launcher_module()
        class Child:
            def __init__(self): self.waits = []; self.returncode = 0
            def communicate(self, timeout=None):
                self.waits.append(timeout)
                if timeout is not None: raise subprocess.TimeoutExpired('synthetic', timeout)
                return b'', b''
            def kill(self): raise AssertionError('must never kill cleanup owner')
            def terminate(self): raise AssertionError('must never terminate cleanup owner')
        child = Child()
        self.assertTrue(launcher.wait_without_kill(child, 10))
        self.assertEqual(child.waits, [10, None])

    def test_launcher_altered_interpreter_refuses_before_process_launch(self):
        launcher = self.launcher_module()
        prepared = self.admit()
        assembly = d.decode(prepared['data']['assembly'])
        manifest = d.decode(d.read_ref(assembly['runtime'], self.private))
        for name in ('python.exe', 'python314._pth', 'esptool.cfg'):
            path = self.policy.parent / name; path.write_bytes(b'synthetic pinned executable content')
            manifest['files'][name] = {k: d.reference(path, self.private)[k] for k in ('bytes', 'sha256')}
        assembly['runtime'] = self.put('launcher-fixture-manifest', manifest)
        prepared['data']['assembly'] = d.canonical(assembly)
        namespace = {'decode': d.decode, 'read_ref': d.read_ref}
        launcher.verify_capsule_files(prepared, namespace)
        (self.policy.parent / 'python.exe').write_bytes(b'altered')
        with patch.object(launcher.subprocess, 'Popen', side_effect=AssertionError('must not launch')):
            with self.assertRaises(d.DiagnosticError): launcher.verify_capsule_files(prepared, namespace)

    def test_nvs_offline_counts_expose_no_bytes_keys_or_values(self):
        before = self.raw('before-nvs', b'a' * 12288)
        after = self.raw('after-nvs', b'b' + b'a' * 12287)
        counts = d.summarize_nvs_drift(before, after, self.private)
        self.assertEqual(counts['changed_bytes'], 1)
        self.assertEqual(counts['changed_pages'], 1)
        self.assertEqual(counts['classification'], 'divergent_not_new_baseline')
        self.assertIs(counts['retained_baseline_changed'], False)
        self.assertFalse(any(k in counts for k in ('data', 'keys', 'values', 'raw')))

    def test_default_cli_is_inert_subprocess(self):
        result = subprocess.run([sys.executable, str(SOURCE)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout.strip(), '{"outcome":"refused","device_actions":0,"candidate_eligible":false}')

    def test_default_launcher_and_flag_only_are_inert(self):
        launcher = SOURCE.with_name('enrollment_candidate_recovery_diagnostic_launcher.py')
        for extra in ([], ['--execute']):
            result = subprocess.run([sys.executable, str(launcher), *extra], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout.strip(), '{"outcome":"refused_or_unresolved","candidate_eligible":false}')

    def test_host_dry_authorization_does_not_spend_marker(self):
        prepared = d._admit(self.package_ref['path'], self.package_ref['sha256'], self.auth_ref['path'],
                     self.auth_ref['sha256'], True, utc=lambda: 1100, monotonic=lambda: 500, spend_authorization=False)
        self.assertFalse(any(self.output.iterdir()))
        self.admit()


if __name__ == '__main__':
    unittest.main()
