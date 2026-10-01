"""Actual runner/package/host-core composition with explicit synthetic I/O.

Old suites supply fake target/flash helpers only and are not executed. SDK,
device identity, native clicks and flash are simulated; none is real-device
authentication, screen observation or restoration evidence.
"""
import copy
from dataclasses import replace
import hashlib
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_runner as runner
import enrollment_candidate_runtime as runtime
import enrollment_candidate_custody as custody
import enrollment_candidate_controller as core
import enrollment_candidate_operator as operator
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_private_view as private
import enrollment_candidate_runtime_tests as runtime_fixture
import enrollment_candidate_operator_tests as prior
import enrollment_candidate_custody_tests as flash


class RunnerFixture:
    def __init__(self, case='first'):
        self.disk = runtime_fixture.AssemblyFixture()
        self.assembly = self.disk.assemble()
        self.old = prior.Fixture(case)
        self.clock = self.old.world.clock
        self.utc = prior.UTC()
        self.evidence = self.disk.private / 'evidence'; self.evidence.mkdir()
        self.images = dict(self.old.images)
        self.devices = {r: flash.Device(r, self.images) for r in ('A', 'B')}
        for r, device in self.devices.items(): device.binding = rom.opaque_identity(prior.KEY, prior.IDENTITIES[r])
        self.request, _ = prior.request_for(case, self.devices)
        self.request['runtime_sha256'] = self.assembly.manifest_sha256
        self.inputs, self.package = {}, {}
        self.package_path = self.disk.private / 'package.json'
        self.factory_calls, self.log, self.backend_instances, self.leases = [], [], [], []
        self.backend_fault, self.view_fault, self.factory_fault = None, None, None
        self.reset_fault_role, self.cleanup_clock = None, None
        self.current_lease, self.current_operator, self.current_view = None, None, None
        class RuntimeWithIdle(prior.FakeRuntime):
            def assert_idle(self):
                # Fake serial is synchronous; no child worker is outstanding.
                # Also refuse to hide an open synthetic application handle.
                return all(handle.is_open is False for handle in self.fixture.world.handles)
        self.old.runtime = RuntimeWithIdle(self.old)
        self.old.runtime.manifest_sha256 = self.assembly.manifest_sha256
        self.make_package('execute', '1' * 32)

    def file(self, name, raw):
        p = self.disk.private / name; p.write_bytes(raw)
        return dict(path=str(p), **runtime_fixture.pin(raw))
    def grant(self, mode, attempt, origin):
        return custody.canonical({'schema': 'OT-CANDIDATE-GRANT-1', 'attempt': attempt,
            'request_sha256': custody.sha(custody.canonical(self.request)),
            'runtime_sha256': self.request['runtime_sha256'], 'operation': mode,
            'origin_attempt': origin, 'attempt_count': 1, 'actions': custody.actions_for(self.request, mode),
            'issued_utc': 100, 'execute_expires_utc': 200, 'restore_expires_utc': 300})
    def make_package(self, mode, attempt, origin=None):
        self.inputs = {
            'assembly': dict(path=str(self.assembly.assembly_path), **runtime_fixture.pin(self.assembly.assembly_path.read_bytes())),
            'request': self.file('request.json', custody.canonical(self.request)),
            'grant': self.file('grant-' + attempt + '.json', self.grant(mode, attempt, origin)),
            'identities': self.file('identities.json', custody.canonical({'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': prior.IDENTITIES})),
            'binding_key': self.file('binding-key.bin', prior.KEY),
            'profiles': self.file('profiles.json', custody.canonical({'schema': 'OT-CANDIDATE-PROFILES-1', 'roles': {
                r: dict(model='heltec_v4_esp32s3', device_binding=d.binding, flash_bytes=16777216,
                        evidence_sha256=('a' if r == 'A' else 'b') * 64) for r, d in self.devices.items()}}))}
        self.package = dict(schema='OT-CANDIDATE-RUNNER-1', operation=mode,
            evidence_root=str(self.evidence), **self.inputs,
            images={name: self.file(name + '.bin', raw) for name, raw in self.images.items()})
        self.save_package()
    def save_package(self):
        self.package_path.write_bytes(custody.canonical(self.package))
        self.package_sha = custody.sha(self.package_path.read_bytes())
    def rebind_input(self, name, value):
        p = Path(self.package[name]['path']); raw = custody.canonical(value); p.write_bytes(raw)
        self.package[name] = dict(path=str(p), **runtime_fixture.pin(raw)); self.save_package()
    def preflight(self, **kwargs):
        return runner.preflight(self.package_path, self.package_sha, utc=self.utc,
                                monotonic=self.clock, **kwargs)
    def runtime_factory(self, path, sha, private_root, **kwargs):
        self.factory_calls.append(('runtime', sha))
        assert path == self.assembly.manifest_path and sha == self.assembly.manifest_sha256
        assert private_root == self.disk.private
        return self.old.runtime
    def lease_factory(self, private_root, **kwargs):
        self.factory_calls.append(('lease',))
        lease = rom.HardwareLease(private_root, **kwargs); self.leases.append(lease)
        self.current_lease = lease; return lease
    def backend_factory(self, selected_runtime, lease, **kwargs):
        role, recovery = kwargs['role'], kwargs['recovery_only']
        self.factory_calls.append(('backend', role, recovery))
        assert kwargs['request'] == self.request and kwargs['images'] == self.images
        assert kwargs['expected_identity'] == prior.IDENTITIES[role]
        f = self
        class Backend(prior.LeasedFlashBackend):
            def __init__(self):
                super().__init__(f.devices[role], f.log, f.clock, lease)
                if recovery: self.phase = 'restore'
                if not recovery and f.backend_fault and f.backend_fault[0] == role:
                    self.fault = f.backend_fault[1]
            def reset_original(self, deadline):
                if not recovery and f.reset_fault_role == role:
                    raise OSError('synthetic reset failure before any reset')
                return super().reset_original(deadline)
            def restart_candidate(self, deadline):
                # Explicit fake reset only. Actual lease enforces exclusivity;
                # runner owns the sequential pair/case/deadline choreography.
                assert f.request['case'] in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit')
                assert lease.assert_idle() and lease._state(self.device.binding)['mode'] == 'app-released'
                assert f.clock() < deadline and self.device.mode == 'candidate'
                lease.enter_rom(self.device.binding, deadline)
                self._record('restart_candidate', deadline=deadline)
                f.old.world.nodes[role].reset_volatile()
                lease.mark_boot(self.device.binding)
                if role == 'B': f.old.world.restart_count += 1
                return True
        backend = Backend(); self.backend_instances.append(backend); return backend
    def view_factory(self):
        self.factory_calls.append(('view',))
        if self.factory_fault: raise RuntimeError(self.factory_fault)
        f = self
        class HumanBackend:
            def __init__(self): self.text = None
            def open(self, text):
                self.text = text
                f.old.view.show(f.current_view._point)
                return True
            def poll(self):
                observed = f.old.view.poll()
                return 'confirm' if observed is not None else None
            def clear(self):
                self.text = None
                okay = f.old.view.close()
                return False if f.view_fault == 'clear' else okay
        self.current_view = private.WindowsPrivateView(backend_factory=HumanBackend, monotonic=self.clock)
        return self.current_view
    def operator_factory(self, selected_runtime, lease, identities, key, identity_binding, view, restart, **kwargs):
        self.factory_calls.append(('operator',))
        instance = operator.CandidateOperator(selected_runtime, lease, identities, key,
            identity_binding, view, restart, wait=self.old.wait, **kwargs)
        close, calls = instance.close, [0]
        def owned_close():
            calls[0] += 1
            result = close()
            if calls[0] == 2 and self.cleanup_clock is not None:
                self.clock.value = self.cleanup_clock
            return result
        instance.close = owned_close
        # The existing fake SerialHandle reads this owner for fresh generation.
        self.old.instance = instance
        self.current_operator = instance; return instance
    def run(self, mode=None, **kwargs):
        return runner.run(self.package_path, self.package_sha, mode or self.package['operation'],
            utc=self.utc, monotonic=self.clock, runtime_factory=self.runtime_factory,
            lease_factory=self.lease_factory, backend_factory=self.backend_factory,
            operator_factory=self.operator_factory, view_factory=self.view_factory, **kwargs)
    def end_simulated_process(self):
        # OS lock release models child process exit, not successful custody or
        # device-handle cleanup. Refuse to mask an actual fake passive owner.
        assert self.current_operator.assert_idle() and all(b.assert_idle() for b in self.backend_instances)
        if self.current_lease._file:
            self.current_lease._file.close(); self.current_lease._file = None
    def cleanup(self):
        if self.current_operator is not None: self.current_operator.close()
        for lease in self.leases:
            if lease._file is not None: lease._file.close(); lease._file = None
        self.old.cleanup(); self.disk.cleanup()


class RunnerTests(unittest.TestCase):
    def fixture(self, **kwargs):
        f = RunnerFixture(**kwargs); self.addCleanup(f.cleanup); return f

    def test_nonconsuming_preflight_repeats_and_returns_immutable_snapshot(self):
        f = self.fixture(); before = {p.name: p.read_bytes() for p in f.evidence.iterdir()}
        with patch.object(runner, 'Runtime', side_effect=AssertionError('preflight runtime construction')), patch.object(runner, '_view_factory', side_effect=AssertionError('preflight UI')):
            p = f.preflight(); q = f.preflight()
        self.assertEqual(p.request_raw, q.request_raw); self.assertEqual(p.grant_raw, q.grant_raw)
        self.assertEqual(p.images, q.images); self.assertEqual(p.assembly, q.assembly)
        self.assertEqual(p.package_sha256, q.package_sha256); self.assertEqual(f.factory_calls, [])
        self.assertGreaterEqual(q.execute_deadline, p.execute_deadline)
        self.assertLess(q.execute_deadline - p.execute_deadline, .0001)
        self.assertEqual({p.name: p.read_bytes() for p in f.evidence.iterdir()}, before)
        self.assertFalse((f.disk.private / 'enrollment-candidate-hardware.lock').exists())
        request = p.request; request['roles'].clear(); grant = p.grant; grant.clear()
        self.assertEqual(p.request, f.request); self.assertTrue(p.grant)
        self.assertNotIn(prior.IDENTITIES['A'], repr(p)); self.assertNotIn(prior.KEY.hex(), repr(p))
        self.assertIs(type(p.images), tuple); self.assertIs(type(p.profiles), tuple)

    def test_package_and_each_pinned_input_drift_refuse_before_ownership(self):
        for fault in ('package', 'request', 'grant', 'identities', 'binding_key', 'profiles', 'application', 'partition', 'assembly', 'source', 'runtime'):
            f = self.fixture()
            if fault == 'package': p = f.package_path
            elif fault in f.package['images']: p = Path(f.package['images'][fault]['path'])
            elif fault == 'source': p = f.disk.worktree / 'tools/enrollment_candidate_controller.py'
            elif fault == 'runtime': p = f.assembly.root / 'python.exe'
            else: p = Path(f.package[fault]['path'])
            p.write_bytes(b'changed PRIVATE inputs')
            with self.subTest(fault=fault), self.assertRaises(Exception): f.run()
            self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])

    def test_authority_identity_profile_and_recovery_binding_refuse_before_io(self):
        for fault in ('case-grant', 'runtime-pin', 'duplicate-identities', 'swapped-identities', 'profile-model', 'profile-size', 'expired', 'mode', 'recover-without-active'):
            f = self.fixture()
            if fault == 'case-grant':
                request = copy.deepcopy(f.request); request['case'] = 'revoke'; f.rebind_input('request', request)
            if fault == 'runtime-pin':
                request = copy.deepcopy(f.request); request['runtime_sha256'] = 'c' * 64
                f.request = request; f.make_package('execute', '1' * 32)
            if 'identities' in fault:
                values = dict(prior.IDENTITIES)
                values = {'A': values['B'], 'B': values['A']} if fault == 'swapped-identities' else {'A': values['A'], 'B': values['A']}
                f.rebind_input('identities', {'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': values})
            if fault.startswith('profile-'):
                obj = json.loads(Path(f.package['profiles']['path']).read_bytes())
                obj['roles']['A']['model' if fault == 'profile-model' else 'flash_bytes'] = 'ESP32-S3' if fault == 'profile-model' else 8388608
                f.rebind_input('profiles', obj)
            if fault == 'expired': f.utc.value = 200
            if fault == 'recover-without-active': f.make_package('recover', '2' * 32, '1' * 32)
            with self.subTest(fault=fault), self.assertRaises(Exception): f.run('recover' if fault == 'mode' else None)
            self.assertEqual(f.factory_calls, []); self.assertEqual(f.old.world.handles, [])

    def test_admission_snapshot_rechecked_after_preflight_before_factories(self):
        f = self.fixture(); original = runner.preflight
        def admitted(*args, **kwargs):
            result = original(*args, **kwargs)
            Path(f.package['grant']['path']).write_bytes(b'changed after admitted snapshot')
            return result
        with patch.object(runner, 'preflight', side_effect=admitted), self.assertRaises(Exception): f.run()
        self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])

    def test_actual_execute_closes_restores_and_durably_binds_private_ack(self):
        f = self.fixture(); result = f.run()
        self.assertEqual(result.outcome, 'passed'); self.assertTrue(result.owner_observed)
        self.assertTrue(result.custody.owner_confirmed and result.custody.custody_released and result.lease_released)
        self.assertIsNone(result.first_failure)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original' for d in f.devices.values()))
        self.assertTrue(all(h.is_open is False and h.close_calls == 1 for h in f.old.world.handles))
        self.assertFalse((f.evidence / custody.ACTIVE).exists())
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual(receipt['request_sha256'], custody.sha(custody.canonical(f.request)))
        events = [json.loads(line) for line in (f.evidence / ('enrollment-candidate-runner-' + '1' * 32 + '-events.jsonl')).read_bytes().splitlines()]
        self.assertEqual([e['sequence'] for e in events], list(range(len(events))))
        checkpoints = [e for e in events if e.get('schema') == 'OT-CANDIDATE-CHECKPOINT-1']
        acks = [e for e in events if e.get('schema') == 'OT-CANDIDATE-ACK-1']
        self.assertEqual(len(checkpoints), len(acks)); self.assertEqual(acks[-1]['kind'], 'usual_screen')
        self.assertTrue(all(c['token_sha256'] == a['token_sha256'] for c, a in zip(checkpoints, acks)))
        logs = result.receipt_path.read_text() + '\n'.join(json.dumps(e) for e in events)
        self.assertNotIn(prior.IDENTITIES['A'], logs)
        self.assertNotIn(prior.EXPECTED_A[0], logs)
        self.assertFalse(any('value' in e or 'token' in e or 'references' in e for e in events))
        calls = len(f.factory_calls)
        with self.assertRaises(Exception): f.preflight()
        self.assertEqual(len(f.factory_calls), calls)

    def test_raw_sdk_partition_is_normalized_to_exact_frozen_span_and_grant_pin(self):
        f = self.fixture()
        raw = b'SYNTHETIC-PARTITION'.ljust(3072, b'\xff')
        normalized = raw.ljust(4096, b'\xff')
        f.images['partition'] = normalized
        for device in f.devices.values():
            device.images = dict(f.images)
            device.candidate_layout_sha256 = custody.sha(normalized)
        f.request['images']['partition'] = custody.descriptor(normalized)
        f.make_package('execute', '1' * 32)
        f.package['images']['partition'] = f.file('raw-partition.bin', raw); f.save_package()
        p = f.preflight()
        self.assertEqual(dict(p.images)['partition'], normalized)
        self.assertEqual(p.request['images']['partition'], custody.descriptor(normalized))
        result = f.run(); self.assertEqual(result.outcome, 'passed')
        self.assertEqual(sum(e[1] == 'write' and e[2] == 'partition' for e in f.log), 2)
        self.assertEqual(Path(f.package['images']['partition']['path']).read_bytes(), raw)

    def test_same_case_restart_is_sequential_fresh_and_uses_original_ceiling(self):
        for case in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit'):
            f = self.fixture(case=case); result = f.run()
            with self.subTest(case=case):
                self.assertEqual(result.outcome, 'passed')
                resets = [e for e in f.log if e[1] == 'restart_candidate']
                self.assertEqual([e[0] for e in resets], ['A', 'B'])
                self.assertTrue(all(e[3] <= 110 for e in resets))
                self.assertEqual(f.old.world.commands().count('HELLO'), 4)
                self.assertEqual(len(f.old.world.handles), 4)
                self.assertTrue(all(h.close_calls == 1 for h in f.old.world.handles))
                self.assertEqual(sum(e[1] == 'write' for e in f.log), 6)

    def test_earlier_parent_execution_and_restore_ceilings_are_never_renewed(self):
        f = self.fixture(); result = f.run(execute_deadline=80, restore_deadline=190)
        self.assertEqual(result.outcome, 'passed')
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual((receipt['execute_deadline'], receipt['restore_deadline']), (80, 190))
        self.assertTrue(all(e[3] <= 190 for e in f.log if e[3] is not None))
        self.assertTrue(all(e[3] <= 80 for e in f.log if e[1] in ('write', 'boot_candidate')))
        self.assertTrue(all(e[3] <= 190 for e in f.log if e[1] in ('restore', 'reset_original')))
        self.assertTrue(all(show[3] <= (190 if show[0] == 'usual_screen' else 80) for show in f.old.view.shows))
        g = self.fixture(); g.clock.value = 80
        with self.assertRaises(Exception): g.run(execute_deadline=80, restore_deadline=190)
        self.assertEqual(g.factory_calls, [])

    def test_held_execution_fresh_recovery_restores_without_execute_activation(self):
        f = self.fixture(); f.backend_fault = ('B', ('restore', 'write', 'ota0_prefix', 'false_success'))
        first = f.run(); self.assertEqual(first.outcome, 'held')
        self.assertFalse(first.custody.custody_released)
        fault = first.first_failure; self.assertIsNotNone(fault)
        self.assertTrue((f.evidence / custody.ACTIVE).exists())
        f.end_simulated_process()
        f.backend_fault = None; f.make_package('recover', '2' * 32, '1' * 32)
        calls, writes = len(f.factory_calls), len(f.log)
        recovered = f.run()
        self.assertEqual(recovered.outcome, 'recovered'); self.assertTrue(recovered.owner_observed)
        self.assertEqual(recovered.first_failure, fault)
        self.assertFalse(recovered.custody.owner_confirmed)
        self.assertTrue(recovered.custody.custody_released and recovered.lease_released)
        tail = f.factory_calls[calls:]
        self.assertNotIn(('operator',), tail)
        self.assertEqual([c for c in tail if c[0] == 'backend'], [('backend', 'A', True), ('backend', 'B', True)])
        self.assertFalse(any(e[1] in ('write', 'boot_candidate', 'restart_candidate') for e in f.log[writes:]))
        self.assertTrue(all(d.flash == d.original for d in f.devices.values()))
        self.assertEqual(f.old.view.shows[-1][0], 'usual_screen')

    def test_unknown_native_exception_is_fixed_category_and_failed_clear_cannot_ack(self):
        f = self.fixture(); f.factory_fault = 'SECRET_WORD'
        result = f.run(); self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('ownership', 'runner_operation_failed'))
        self.assertNotIn('SECRET_WORD', result.receipt_path.read_text())
        g = self.fixture(); g.view_fault = 'clear'
        result = g.run(); self.assertNotEqual(result.outcome, 'passed')
        self.assertFalse(result.owner_observed)
        self.assertIsNotNone(result.first_failure)

    def test_recovery_accepts_verified_untouched_role_without_inventing_restore_flags(self):
        f = self.fixture()
        f.backend_fault = ('A', ('capture', 'read', 'application', 'disconnect'))
        f.reset_fault_role = 'A'
        first = f.run(); self.assertEqual(first.outcome, 'held')
        self.assertTrue(first.custody.roles['B']['settled_untouched'])
        self.assertFalse(first.custody.roles['B']['restore_verified'])
        self.assertFalse(first.custody.roles['B']['original_boot_allowed'])
        f.end_simulated_process(); f.backend_fault = None; f.reset_fault_role = None
        f.make_package('recover', '2' * 32, '1' * 32)
        restored = f.run()
        self.assertEqual(restored.outcome, 'recovered'); self.assertTrue(restored.owner_observed)
        self.assertTrue(restored.custody.roles['B']['settled_untouched'])
        self.assertFalse(restored.custody.roles['B']['restore_verified'])
        self.assertFalse(restored.custody.owner_confirmed)
        self.assertEqual(f.old.view.shows[-1][0], 'usual_screen')

    def test_cleanup_late_or_rollback_cannot_promote_success_and_capsules_are_not_evidence_roots(self):
        for value in (211, 0):
            f = self.fixture(); f.cleanup_clock = value
            with self.subTest(cleanup_clock=value):
                result = f.run()
                self.assertNotEqual(result.outcome, 'passed'); self.assertIsNotNone(result.first_failure)
        for root in ('base', 'assembled'):
            f = self.fixture()
            f.package['evidence_root'] = str(f.disk.base if root == 'base' else f.assembly.root)
            f.save_package()
            before = f.disk.snapshot_base() if root == 'base' else {
                p.relative_to(f.assembly.root).as_posix(): p.read_bytes()
                for p in f.assembly.root.rglob('*') if p.is_file()}
            with self.subTest(root=root), self.assertRaises(Exception): f.run()
            self.assertEqual(f.factory_calls, [])
            after = f.disk.snapshot_base() if root == 'base' else {
                p.relative_to(f.assembly.root).as_posix(): p.read_bytes()
                for p in f.assembly.root.rglob('*') if p.is_file()}
            self.assertEqual(after, before)

    def test_cli_ambient_origin_refusal_is_category_only_and_help_does_not_admit_inputs(self):
        f = self.fixture(); stream = io.StringIO()
        with patch('sys.stdout', stream):
            code = runner.main(['--mode', 'preflight', '--assembly', str(f.assembly.assembly_path),
                                '--assembly-sha256', f.assembly.assembly_sha256])
        self.assertEqual(code, 1)
        answer = json.loads(stream.getvalue())
        self.assertEqual(answer, {'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'preflight',
                                  'outcome': 'failed', 'category': 'isolated_caller_required'})
        self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])
        with patch.object(runner, 'verify_assembly', side_effect=AssertionError('help admission')), patch('sys.stdout', io.StringIO()):
            with self.assertRaises(SystemExit) as done: runner.main(['--help'])
        self.assertEqual(done.exception.code, 0)

    def test_result_write_late_or_rollback_preserves_positive_only_as_unaccepted(self):
        for value in (211, 0):
            f = self.fixture(); original = runner._write_once; advanced = [False]
            def persisted(path, raw, **kwargs):
                result = original(path, raw, **kwargs)
                if Path(path).suffix == '.pending' and not advanced[0]:
                    advanced[0] = True; f.clock.value = value
                return result
            with self.subTest(after_write_clock=value), patch.object(runner, '_write_once', side_effect=persisted):
                result = f.run()
                self.assertEqual(result.outcome, 'failed'); self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.receipt_path)
                accepted = json.loads(result.receipt_path.read_bytes())
                provisional = json.loads(result.receipt_path.with_suffix('.unaccepted').read_bytes())
                self.assertEqual(accepted['outcome'], 'failed')
                self.assertEqual(provisional['outcome'], 'passed')
                self.assertEqual(accepted['first_failure'], list(result.first_failure))

    def test_same_attempt_precore_failure_cannot_recompose_or_rewrite_first_receipt(self):
        f = self.fixture(); f.factory_fault = 'SECRET_WORD'
        first = f.run(); self.assertEqual(first.outcome, 'failed'); self.assertIsNone(first.custody)
        self.assertIsNotNone(first.receipt_path)
        before = {p.name: p.read_bytes() for p in f.evidence.iterdir()}
        calls = len(f.factory_calls); f.factory_fault = None
        with self.assertRaisesRegex(runner.RunnerError, '^runner_attempt_used$'): f.run()
        self.assertEqual(len(f.factory_calls), calls)
        self.assertEqual({p.name: p.read_bytes() for p in f.evidence.iterdir()}, before)

    def test_unknown_custody_fault_and_invalid_role_diagnostics_do_not_escape_collector(self):
        for invalid in (False, True):
            f = self.fixture()
            rows = {r: dict(restore_verified=True, original_boot_allowed=True,
                           handles_closed=True, settled_untouched=False) for r in ('A', 'B')}
            if invalid: rows['A']['diagnostic'] = 'SECRET_WORD'
            produced = custody.CustodyResult('1' * 32, 'failed', ('SECRET_STAGE', 'SECRET_WORD'), rows, True, False)
            with self.subTest(invalid_roles=invalid), patch.object(runner, 'execute', return_value=produced):
                result = f.run()
                self.assertNotEqual(result.outcome, 'passed'); self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.receipt_path)
                self.assertNotIn('SECRET_WORD', result.receipt_path.read_text())
                self.assertNotIn('SECRET_STAGE', result.receipt_path.read_text())
                if not invalid: self.assertEqual(result.first_failure, ('custody', 'runner_operation_failed'))


if __name__ == '__main__': unittest.main(verbosity=2)
