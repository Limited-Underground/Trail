"""Host-only recovery disposition refusals; no SDK or device dependencies."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[2] / 'tools' / 'enrollment_candidate_recovery_disposition.py'
spec = importlib.util.spec_from_file_location('recovery_disposition_under_test', SOURCE)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.private = Path(self.temp.name) / '.private'
        self.session = self.private / 'legacy-session'
        self.recovery = self.private / 'new-recovery'
        self.session.mkdir(parents=True)
        self.recovery.mkdir()
        self.attempt, self.candidate = '1' * 32, '2' * 32
        self.pid = 43210
        self.destination = self.recovery / 'disposition.json'
        self.lock = self.private / 'enrollment-candidate-hardware.lock'
        self.lock.write_bytes(b'\0')
        retained = {}
        for key in gate.PROVENANCE:
            path = self.private / ('provenance-' + key + '.json')
            path.write_bytes(b'{"historical":true}\n')
            retained[key] = gate.reference(path, self.private)
        request = {'schema': 'OT-RETAINED-ORIGINAL-REQUEST-1', 'runtime_sha256': '3' * 64,
                   'roles': {r: {'device_binding': str(i) * 64,
                   'profile_evidence_sha256': '6' * 64} for i, r in enumerate(('A', 'B'), 4)},
                   'retained': retained}
        self.capture_request = self.private / 'capture-request.json'
        self.capture_request.write_bytes(gate.canonical(request) + b'\n')
        request_sha = gate.sha(gate.canonical(request))
        roles = {}
        for role in ('A', 'B'):
            pins = {}
            for name, count in gate.SPANS.items():
                raw = role.encode() * count
                path = self.session / f'enrollment-original-{self.attempt}-{role}-{name}.bin'
                path.write_bytes(raw)
                pins[name] = {'bytes': len(raw), 'sha256': gate.sha(raw)}
            roles[role] = {'owner': 'capture', 'entered': True, 'handles_closed': True,
                           'original_boot_allowed': False, 'reset_intent': True, 'pins': pins}
        self.put('active_marker', {'attempt': self.attempt, 'request_sha256': request_sha})
        self.put('pins', {'schema': 'OT-ORIGINAL-CAPTURE-PINS-1', 'attempt': self.attempt,
             'request_sha256': request_sha, 'runtime_sha256': request['runtime_sha256'],
             'roles': {r: dict(request['roles'][r], originals=roles[r]['pins']) for r in ('A', 'B')}})
        c_request = {'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': request['runtime_sha256'],
                     'roles': {r: {'device_binding': request['roles'][r]['device_binding'],
                                   'originals': roles[r]['pins']} for r in ('A', 'B')}}
        self.put('candidate_request', c_request)
        for key, attempt, value in (('capture', self.attempt, request), ('candidate', self.candidate, c_request)):
            self.put(key + '_grant', {'attempt': attempt, 'request_sha256': gate.sha(gate.canonical(value))})
            self.put(key + '_package', {'request': gate.reference(self.capture_request, self.private)
                if key == 'capture' else self.ref('candidate_request'),
                'grant': self.ref(key + '_grant'), 'evidence_root': str(self.session)})
        self.put('handoff', self.ref('candidate_package'))
        self.put('handoff_receipt', {'schema': 'OT-ORIGINAL-CAPTURE-HANDOFF-1',
                 'candidate_package': self.ref('candidate_package'),
                 'handoff': {k: self.ref('handoff')[k] for k in ('bytes', 'sha256')}})
        self.put('journal', {'schema': 'OT-ORIGINAL-CAPTURE-JOURNAL-1', 'attempt': self.attempt,
             'candidate': None, 'capture_complete': True, 'cleanup_failure': ['release_A', 'rom_adapter_refused'],
             'first_failure': ['freshness_A_nvs', 'capture_changed'], 'grant_sha256': self.ref('capture_grant')['sha256'],
             'phase': 'held', 'receipt': {k: self.ref('pins')[k] for k in ('bytes', 'sha256')},
             'request': request, 'roles': roles})
        self.put('capture_result', {'schema': 'OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1', 'attempt': self.attempt,
             'operation': 'capture', 'outcome': 'held', 'capture_complete': True,
             'capture_custody_released': False, 'lease_released': False, 'request_sha256': request_sha,
             'grant_sha256': self.ref('capture_grant')['sha256'], 'package_sha256': self.ref('capture_package')['sha256'],
             'first_failure': ['freshness_A_nvs', 'capture_changed'],
             'capture_cleanup_failure': ['release_A', 'rom_adapter_refused']})
        self.path('candidate_events').write_bytes(b'')
        self.put('candidate_result', {'schema': 'OT-CANDIDATE-RUNNER-RESULT-1', 'attempt': self.candidate,
             'operation': 'execute', 'outcome': 'failed', 'custody': None, 'lease_released': False,
             'owner_observed': False, 'request_sha256': gate.sha(gate.canonical(c_request)),
             'grant_sha256': self.ref('candidate_grant')['sha256'], 'package_sha256': self.ref('candidate_package')['sha256'],
             'first_failure': ['ownership', 'capture_changed'],
             'events': {k: self.ref('candidate_events')[k] for k in ('bytes', 'sha256')}})
        self.put('owner_observation', {'schema': 'OT-OWNER-MANUAL-RECOVERY-OBSERVATION-1', 'task': 'OT-0238c',
             'authorization': 'Owner approved sequential manual observation.',
             'observations': [{'role': r, 'usual_screen': 'owner_confirmed'} for r in ('A', 'B')],
             'limits': {'candidate_installation': 'not_reached', 'physical_acceptance': 'not_earned',
                       'byte_restoration': 'not_verified_by_manual_screen_check', 'automated_custody': 'unresolved',
                       'new_trial_authorized': False}})
        self.process = self.recovery / 'predecessor.json'
        self.process.write_bytes(gate.canonical({'schema': 'OT-PREDECESSOR-PROCESS-EVIDENCE-1',
            'pid': self.pid, 'session': str(self.session), 'capture_attempt': self.attempt,
            'observed_dead': True, 'identity_basis': 'prior_observed_owner_launch_pid'}) + b'\n')

    def path(self, key):
        return self.session / gate._names(self.attempt, self.candidate)[key]

    def put(self, key, value):
        self.path(key).write_bytes(gate.canonical(value) + b'\n')

    def ref(self, key):
        return gate.reference(self.path(key), self.private)

    def create(self):
        with patch.object(gate, 'require_process_dead', return_value=True), patch.object(
                gate, 'probe_hardware_lock', return_value={'path': str(self.lock), 'bytes': 1,
                'sha256': gate.sha(b'\0'), 'os_byte_lock_observed_idle': True, 'content_unchanged': True}):
            return gate.create_disposition(self.private, self.session, self.attempt,
                   self.candidate, self.process, self.pid, self.destination)

    def assert_create_refused(self):
        with self.assertRaises(gate.RecoveryDispositionError):
            self.create()
        self.assertFalse(self.destination.exists())

    def test_valid_sidecar_preserves_legacy_and_gate_refuses(self):
        before = {p: p.read_bytes() for p in self.session.iterdir()}
        ref = self.create()
        value = gate.decode(self.destination.read_bytes())
        self.assertEqual(value['status'], gate.STATUS)
        self.assertIs(value['candidate_eligible'], False)
        self.assertEqual(before, {p: p.read_bytes() for p in self.session.iterdir()})
        with self.assertRaisesRegex(gate.RecoveryDispositionError, '^recovery_baseline_unresolved$'):
            gate.require_candidate_entry_allowed(self.destination, ref['sha256'], self.private)

    def test_candidate_ownership_adoption_transfer_refused(self):
        baseline = gate.decode(self.path('journal').read_bytes())
        for change in ('candidate', 'owner', 'phase', 'boot', 'handles', 'intent'):
            row = deepcopy(baseline)
            if change == 'candidate': row['candidate'] = {'adopted': True}
            elif change == 'owner': row['roles']['A']['owner'] = 'candidate'
            elif change == 'phase': row['phase'] = 'transfer-prepared'
            elif change == 'boot': row['roles']['A']['original_boot_allowed'] = True
            elif change == 'handles': row['roles']['A']['handles_closed'] = False
            elif change == 'intent': row['roles']['A']['reset_intent'] = False
            self.put('journal', row)
            self.assert_create_refused()
        self.put('journal', baseline)

    def test_pending_and_candidate_active_or_journal_refused(self):
        for name in ('anything.pending', 'anything.interrupted', 'enrollment-candidate-active.json',
                     f'enrollment-candidate-{self.candidate}.json',
                     f'enrollment-original-{self.attempt}-A.transfer',
                     f'enrollment-original-{self.attempt}-B.transfer'):
            path = self.session / name
            path.write_bytes(b'{}')
            self.assert_create_refused()
            path.unlink()

    def test_provenance_and_pinned_original_tampering_refused(self):
        journal = gate.decode(self.path('journal').read_bytes())
        for path in (Path(journal['request']['retained']['closure_review']['path']),
                     self.session / f'enrollment-original-{self.attempt}-A-nvs.bin'):
            original = path.read_bytes()
            path.write_bytes(b'changed')
            self.assert_create_refused()
            path.write_bytes(original)

    def test_runner_work_or_owner_unconfirmed_refused(self):
        for key, field, replacement in (('candidate_result', 'custody', {}),
               ('candidate_result', 'owner_observed', True), ('capture_result', 'lease_released', True),
               ('owner_observation', 'observations', [{'role': 'A', 'usual_screen': 'unknown'},
                                                    {'role': 'B', 'usual_screen': 'owner_confirmed'}])):
            original = gate.decode(self.path(key).read_bytes())
            changed = dict(original, **{field: replacement})
            self.put(key, changed)
            self.assert_create_refused()
            self.put(key, original)

    def test_missing_process_or_wrong_pid_refused(self):
        value = gate.decode(self.process.read_bytes())
        self.process.write_bytes(gate.canonical(dict(value, pid=self.pid + 1)))
        self.assert_create_refused()

    def test_unknown_predecessor_is_recorded_without_dead_claim_and_still_blocks(self):
        self.pid = None
        self.process.write_bytes(gate.canonical({'schema': 'OT-PREDECESSOR-PROCESS-EVIDENCE-1',
            'pid': None, 'session': str(self.session), 'capture_attempt': self.attempt,
            'observed_dead': False, 'identity_basis': 'unknown_no_durable_pid_record'}) + b'\n')
        with patch.object(gate, 'require_process_dead', side_effect=AssertionError('unknown PID must not be probed')):
            with patch.object(gate, 'probe_hardware_lock', return_value={'path': str(self.lock),
                 'bytes': 1, 'sha256': gate.sha(b'\0'), 'os_byte_lock_observed_idle': True,
                 'content_unchanged': True}):
                ref = gate.create_disposition(self.private, self.session, self.attempt,
                        self.candidate, self.process, self.pid, self.destination)
        value = gate.decode(self.destination.read_bytes())
        self.assertIsNone(value['predecessor_pid'])
        self.assertIs(value['predecessor_observed_dead'], False)
        with self.assertRaisesRegex(gate.RecoveryDispositionError, '^recovery_baseline_unresolved$'):
            gate.require_candidate_entry_allowed(self.destination, ref['sha256'], self.private)

    def test_live_predecessor_refusal_creates_no_sidecar(self):
        with patch.object(gate, 'require_process_dead', side_effect=gate.RecoveryDispositionError('recovery_predecessor_live')):
            with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_predecessor_live'):
                gate.create_disposition(self.private, self.session, self.attempt,
                    self.candidate, self.process, self.pid, self.destination)
        self.assertFalse(self.destination.exists())

    def test_changed_disposition_or_false_release_claim_refused(self):
        ref = self.create()
        value = gate.decode(self.destination.read_bytes())
        for key, replacement in (('candidate_eligible', True), ('automatic_custody_released', True),
                                 ('byte_equality', 'equal'), ('status', 'reconciled')):
            changed = gate.canonical(dict(value, **{key: replacement})) + b'\n'
            self.destination.write_bytes(changed)
            with self.assertRaises(gate.RecoveryDispositionError):
                gate.require_candidate_entry_allowed(self.destination, gate.sha(changed), self.private)
        with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_disposition_changed'):
            gate.require_candidate_entry_allowed(self.destination, ref['sha256'], self.private)

    def test_changed_legacy_after_disposition_gate_refuses(self):
        ref = self.create()
        self.path('active_marker').write_bytes(b'{}')
        with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_evidence_changed'):
            gate.require_candidate_entry_allowed(self.destination, ref['sha256'], self.private)

    def test_authoritative_transfer_after_sidecar_gate_refuses(self):
        ref = self.create()
        for role in ('A', 'B'):
            path = self.session / f'enrollment-original-{self.attempt}-{role}.transfer'
            path.write_bytes(b'{"schema":"OT-ORIGINAL-TRANSFER-1"}')
            with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_custody_unresolved'):
                gate.require_candidate_entry_allowed(self.destination, ref['sha256'], self.private)
            path.unlink()

    def test_sidecar_cannot_overwrite_or_live_in_legacy(self):
        self.create()
        self.assert_create_refused_existing()
        self.destination = self.session / 'new-disposition.json'
        self.assert_create_refused()

    def assert_create_refused_existing(self):
        original = self.destination.read_bytes()
        with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_output_invalid'):
            self.create()
        self.assertEqual(self.destination.read_bytes(), original)

    def test_duplicate_json_keys_refused(self):
        with self.assertRaises(gate.RecoveryDispositionError):
            gate.decode(b'{"candidate_eligible":false,"candidate_eligible":true}')

    def test_entry_gate_precedes_workflow_import_in_subprocess(self):
        ref = self.create()
        probe = self.recovery / 'entry-probe.py'
        probe.write_text('import importlib.util,sys\n'
            's=importlib.util.spec_from_file_location("host_gate",sys.argv[1])\n'
            'm=importlib.util.module_from_spec(s);s.loader.exec_module(m)\n'
            'try:\n'
            ' m.require_candidate_entry_allowed(sys.argv[2],sys.argv[3],sys.argv[4])\n'
            'except m.RecoveryDispositionError as e:\n'
            ' assert str(e)=="recovery_baseline_unresolved"\n'
            ' assert not any(n.startswith("enrollment_candidate_owner_workflow") for n in sys.modules)\n'
            ' print("refused_before_workflow_import");sys.exit(0)\n'
            'raise AssertionError("workflow would become reachable")\n', encoding='utf-8')
        result = subprocess.run([sys.executable, str(probe), str(SOURCE), str(self.destination),
                                ref['sha256'], str(self.private)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'refused_before_workflow_import')

    @unittest.skipUnless(os.name == 'nt', 'Windows byte-lock API required')
    def test_actual_host_lock_probe_idle_busy_missing_changed(self):
        import msvcrt
        before = self.lock.read_bytes()
        self.assertTrue(gate.probe_hardware_lock(self.private)['content_unchanged'])
        self.assertEqual(self.lock.read_bytes(), before)
        with self.lock.open('r+b', buffering=0) as handle:
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            try:
                with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_hardware_lock_busy'):
                    gate.probe_hardware_lock(self.private)
            finally:
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        self.lock.write_bytes(b'changed')
        with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_lock_invalid'):
            gate.probe_hardware_lock(self.private)
        self.lock.unlink()
        with self.assertRaises(gate.RecoveryDispositionError):
            gate.probe_hardware_lock(self.private)
        self.assertFalse(self.lock.exists())

    @unittest.skipUnless(os.name == 'nt', 'Windows process query required')
    def test_actual_host_live_process_refused(self):
        with self.assertRaisesRegex(gate.RecoveryDispositionError, 'recovery_predecessor_live'):
            gate.require_process_dead(os.getpid())


if __name__ == '__main__':
    unittest.main()
