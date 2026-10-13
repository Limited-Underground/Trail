"""Actual acquisition/coordinator/candidate custody composition, synthetic I/O.

No physical devices, ports, installed SDK or native human interface are accessed.
The actual worker dispatch receives a synthetic SDK/flash transport; application
transport and acknowledgements are explicit doubles. Runtime source verification,
lease, journals, grants, runners, coordinator, custody and operator are real code.
"""
import builtins
import importlib
import io
import json
from pathlib import Path
import sys
import unittest
from contextlib import redirect_stdout
from types import SimpleNamespace
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
import enrollment_candidate_original_capture_tests as acquisition
import enrollment_candidate_rom_adapter_tests as sdk_fixture
import enrollment_candidate_budget_adoption_tests as adoption_fixture


class Fixture(prior.RunnerFixture):
    def __init__(self):
        super().__init__()
        self.identities = prior.prior.IDENTITIES
        self.sdks, self.calls = {}, []
        self.launch_seconds, self.fault, self.mutate_after = 0, None, None
        self.images['partition'] = sdk_fixture.table_from_csv(ROOT /
            'firmware/targets/heltec_v4_enrollment_candidate_eval/partitions.csv')
        for role, device in self.devices.items():
            device.original.update(partition=sdk_fixture.table_from_csv(ROOT /
                'firmware/targets/heltec_v4_bench/partitions.csv'),
                otadata=b'\xff' * 8192, nvs=b'\xff' * 12288)
            sdk = sdk_fixture.FakeSDK(device.original, self.clock)
            sdk.mac = self.identities[role]
            sdk.ports[0].serial_number = sdk.mac
            sdk.ports[0].device = self.old.world.routes[role]
            self.sdks[role] = sdk
            device.flash = sdk.memory
        self.request['images'] = {n: custody.descriptor(raw) for n, raw in self.images.items()}
        for role, device in self.devices.items():
            self.request['roles'][role]['originals'] = {
                n: custody.descriptor(raw) for n, raw in device.original.items()}
        self.make_package('execute', '1' * 32)
        self.rom_runtime = rom.Runtime(self.assembly.manifest_path,
            self.assembly.manifest_sha256, self.disk.private,
            subprocess_run=self.worker, monotonic=self.clock)
        self.rom_runtime.serial_api = self.old.runtime.serial_api
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
            'issued_utc': 100, 'capture_expires_utc': 2200, 'cleanup_expires_utc': 3700}
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

    def grant(self, mode, attempt, origin):
        value = custody.decode(super().grant(mode, attempt, origin))
        value.update(execute_expires_utc=2200, restore_expires_utc=3700)
        return custody.canonical(value)

    def runtime_factory(self, *args, **kwargs):
        super().runtime_factory(*args, **kwargs)
        return self.rom_runtime

    def worker(self, argv, **kwargs):
        query = json.loads(kwargs['input'])
        role = next(r for r, identity in self.identities.items() if identity == query['identity'])
        action = query['operation']
        mode = query.get('mode')
        target = self.capture_log if mode == 'capture-only' else self.log
        target.append((role, 'capture_reset' if action == 'reset_original' and
            mode == 'capture-only' else action, query.get('span'), query['deadline']))
        if action == 'reset_original' and ((mode == 'capture-only' and
            self.capture_fault == ('reset', role)) or
            (mode != 'capture-only' and self.reset_fault_role == role)):
            raise OSError('synthetic ambiguous original reset')
        return acquisition.Fixture.run(self, argv, **kwargs)

    def capture_backend_factory(self, selected_runtime, lease, **kwargs):
        role = kwargs['role']
        self.factory_calls.append(('capture_backend', role))
        backend = rom.CaptureROMBackend(selected_runtime, lease, **kwargs)
        close = backend.close
        backend.close = lambda: False if self.capture_fault == ('close', role) else close()
        self.capture_backends.append(backend)
        return backend

    def backend_factory(self, *args, **kwargs):
        role = kwargs['role']
        self.factory_calls.append(('backend', role, kwargs['recovery_only']))
        backend = rom.ROMBackend(*args, **kwargs)
        self.backend_instances.append(backend)
        claim, used = backend.claim, [False]
        def checked_claim(binding, deadline):
            self.log.append((role, 'claim', None, deadline))
            if self.claim_fault == role and not used[0]:
                used[0] = True
                raise OSError('PRIVATE first candidate claim failure')
            return claim(binding, deadline)
        backend.claim = checked_claim
        return backend

    def wait_handoff(self, seconds):
        self.clock.value += seconds
        if self.handoff_fault == 'absent':
            self.clock.value = 2110
            return
        raw = custody.canonical(dict(path=str(self.package_path), **runtime.descriptor(self.package_path.read_bytes())))
        if not self.handoff.exists():
            self.handoff.write_bytes(raw)
        if self.handoff_fault == 'preflight':
            self.package_path.write_bytes(b'private-invalid-package')

    def run_capture(self, mode='capture', **kwargs):
        return capture_runner.run(self.capture_package_path, self.capture_package_sha, mode,
            utc=self.utc, monotonic=self.clock, runtime_factory=self.runtime_factory,
            lease_factory=self.lease_factory, capture_backend_factory=self.capture_backend_factory,
            candidate_backend_factory=self.backend_factory, operator_factory=self.operator_factory,
            view_factory=self.view_factory, wait=self.wait_handoff, **kwargs)


class CaptureRunnerTests(unittest.TestCase):
    def test_bench_presentation_survives_capture_handoff_as_presentation_only(self):
        f = self.fixture()
        # This explicit host fixture supplies the final owner statement only
        # after actual composed restoration. It exercises the maintained file
        # provider; it is not a real human or hardware acceptance receipt.
        import enrollment_candidate_private_view as private
        actual_provider = private.OriginalScreenReplyView
        observations = []
        test = self
        class SyntheticFreshChatReply(actual_provider):
            def show(self, prompt):
                okay = super().show(prompt)
                request = json.loads(self.request_path.read_bytes())
                test.assertEqual(self.root, f.evidence)
                test.assertEqual(request['attempt'], '1' * 32)
                test.assertEqual(request['request_sha256'], custody.sha(custody.canonical(f.request)))
                test.assertEqual(request['grant_sha256'], f.inputs['grant']['sha256'])
                test.assertEqual(request['roles'], ['A', 'B'])
                test.assertTrue(f.current_operator.assert_idle())
                for role in ('A', 'B'):
                    test.assertTrue(any(row[0] == role and row[1] == 'reset_original' for row in f.log))
                reply = {key: request[key] for key in ('attempt', 'request_sha256', 'grant_sha256', 'roles', 'token_sha256')}
                reply.update(schema='OT-ORIGINAL-SCREEN-REPLY-1', source='fresh_human_chat_reply',
                    prompt_sha256=custody.sha(self.request_path.read_bytes()), received_utc=self.utc(),
                    both_original_screens_observed=True)
                self.runtime._write_once(self.reply_path, self.runtime.canonical(reply) + b'\n')
                observations.append(reply)
                return okay
        with patch.object(private, 'OriginalScreenReplyView', SyntheticFreshChatReply):
            result = f.run_capture(bench_presentation=True)
        self.assertEqual(len(observations), 1)
        self.assertEqual(result.candidate.outcome, 'passed')
        self.assertEqual(f.current_operator.human.role_labels,
                         {'A': 'Trail Bench', 'B': 'Trail Bench 2'})
        self.assertEqual(f.current_operator.passive.identities, prior.prior.IDENTITIES)

    def test_capture_cli_guards_after_binding_and_before_run_but_release_is_available(self):
        assembly = SimpleNamespace(assembly_sha256='1' * 64, assembly_path=Path('assembly.json'))
        for mode, allowed in (('capture', False), ('capture', True), ('capture-only', False),
                ('capture-only', True), ('release', False), ('capture-preflight', False)):
            trace = []
            def check_assembly(*args, **kwargs): trace.append('assembly'); return assembly
            def package(*args): trace.append('package'); return SimpleNamespace(assembly=assembly)
            def desktop():
                trace.append('desktop')
                if not allowed: raise runner.RunnerError('private_view_failed')
            def execute(*args, **kwargs):
                trace.append('run'); return SimpleNamespace(outcome='released', first_failure=None, cleanup_failure=None)
            output = io.StringIO()
            with self.subTest(mode=mode), patch.object(capture_runner, 'verify_assembly', check_assembly), \
                    patch.object(capture_runner, 'preflight', package), \
                    patch.object(capture_runner, '_require_input_desktop', desktop), \
                    patch.object(capture_runner, 'run', execute), redirect_stdout(output):
                code = capture_runner.main(['--mode', mode, '--assembly', 'assembly.json',
                    '--assembly-sha256', '1' * 64, '--package', 'package.json', '--package-sha256', '2' * 64])
            record = json.loads(output.getvalue())
            self.assertEqual(trace, ['assembly', 'package', 'desktop'] + (['run'] if allowed else []) if mode in ('capture', 'capture-only') else
                ['assembly', 'package', 'run'] if mode == 'release' else ['assembly', 'package'])
            self.assertEqual(code, 1 if mode in ('capture', 'capture-only') and not allowed else 0)
            self.assertEqual(record['category'], 'private_view_failed' if mode in ('capture', 'capture-only') and not allowed else None)

    def test_capture_default_native_guard_precedes_runtime_lease_and_grant_consumption(self):
        f = self.fixture()
        with patch.object(capture_runner, '_require_input_desktop',
                side_effect=runner.RunnerError('private_view_failed')) as check, \
                patch.object(capture_runner, 'Runtime') as unused:
            with self.assertRaisesRegex(runner.RunnerError, '^private_view_failed$'):
                capture_runner.run(f.capture_package_path, f.capture_package_sha, 'capture',
                    utc=f.utc, monotonic=f.clock,
                    runtime_factory=lambda *args, **kwargs: self.fail('runtime before desktop guard'),
                    lease_factory=lambda *args, **kwargs: self.fail('lease before desktop guard'))
            check.assert_called_once_with()
            unused.assert_not_called()
        self.assertFalse(f.factory_calls)
        self.assertFalse((f.evidence / ('enrollment-original-used-' + '4' * 32)).exists())

    def test_native_desktop_translation_keeps_native_details_out_of_launch_category(self):
        import enrollment_candidate_private_view as private
        for error in (private.ViewError('native_view_desktop_unavailable'), OSError('synthetic native detail')):
            with self.subTest(error=type(error).__name__), patch.object(private, 'ensure_input_desktop', side_effect=error):
                with self.assertRaisesRegex(runner.RunnerError, '^private_view_failed$'):
                    capture_runner._require_input_desktop()
        with patch.object(private, 'ensure_input_desktop', return_value=False):
            with self.assertRaisesRegex(runner.RunnerError, '^private_view_failed$'):
                capture_runner._require_input_desktop()

    def fixture(self):
        f = Fixture()
        self.addCleanup(f.cleanup)
        return f

    def unused_candidate(self, f):
        self.assertFalse((f.evidence / ('enrollment-candidate-used-' + '1' * 32)).exists())

    def test_retained_mode_requires_real_verification_and_labels_drift_cleanup(self):
        verified = SimpleNamespace(_verified=True, _release_records={})
        unverified = SimpleNamespace(_verified=False, _release_records={})
        result = SimpleNamespace(outcome='failed', first_failure=('freshness_A_nvs', 'capture_changed'), cleanup_failure=None)
        self.assertEqual(capture_runner._retained_mode(unverified, result), 'retained_mismatch_unresolved')
        self.assertEqual(capture_runner._retained_mode(None, None), 'retained_not_verified')
        result.first_failure = ('ownership', 'deadline_expired')
        self.assertEqual(capture_runner._retained_mode(unverified, result), 'retained_not_verified')

        result.cleanup_failure = ('release_A', 'release_nvs_changed')
        self.assertEqual(capture_runner._retained_mode(verified, result), 'retained_mismatch_unresolved')
        result.cleanup_failure = ('release_A', 'rom_operation_failed')
        self.assertEqual(capture_runner._retained_mode(verified, result), 'retained_not_verified')
        result.cleanup_failure = None
        result.outcome = 'released_with_nvs_drift'
        self.assertEqual(capture_runner._retained_mode(unverified, result), 'retained_mismatch_released_with_nvs_drift')
        result.outcome, result.first_failure = 'released', None
        self.assertEqual(capture_runner._retained_mode(verified, result), 'retained_verified')
        self.assertEqual(capture_runner._retained_mode(unverified, result), 'retained_not_verified')

    def test_current_settings_mode_requires_strict_new_capture_verification(self):
        session = SimpleNamespace(current_nvs=True, _verified=True)
        result = SimpleNamespace(capture_complete=True, first_failure=None, cleanup_failure=None)
        self.assertEqual(capture_runner._current_nvs_mode(session, result),
            'retained_firmware_current_nvs_verified')
        for field in ('current_nvs', '_verified'):
            changed = SimpleNamespace(**vars(session)); setattr(changed, field, False)
            self.assertEqual(capture_runner._current_nvs_mode(changed, result),
                'retained_firmware_current_nvs_not_verified')
        result.cleanup_failure = ('release', 'capture_changed')
        self.assertEqual(capture_runner._current_nvs_mode(session, result),
            'retained_firmware_current_nvs_not_verified')

    def current_settings_retry(self, f):
        """Build reviewed synthetic provenance from an actual composed prior trial."""
        initial = f.run_capture()
        self.assertEqual(initial.outcome, 'passed', initial)
        baseline = f.evidence
        paths = dict(source_journal=initial.capture.journal_path,
            restore_journal=baseline / ('enrollment-candidate-' + '1'*32 + '.json'),
            capture_result=initial.receipt_path,
            candidate_result=baseline / ('enrollment-candidate-runner-' + '1'*32 + '.json'),
            events=baseline / ('enrollment-candidate-runner-' + '1'*32 + '-events.jsonl'))
        owner = baseline / 'synthetic-owner.json'
        owner.write_bytes(custody.canonical(dict(source='fresh_human_reply',
            both_usual_screens_observed=True, computer_checkpoint_clicked=True)))
        paths['owner_ack'] = owner
        refs = {n:dict(path=str(p), **runtime.descriptor(p.read_bytes())) for n,p in paths.items()}
        review = baseline / 'synthetic-closure-review.json'
        review.write_bytes(custody.canonical(dict(outcome='closure_and_original_backup_integrity_verified',
            findings=[], references={Path(ref['path']).name:ref for ref in refs.values()},
            retained_originals=dict(eligible_as_verified_backup_inputs_for_reviewed_retained_retry=True))))
        refs['closure_review'] = dict(path=str(review), **runtime.descriptor(review.read_bytes()))
        f.capture_request = dict(f.capture_request, schema=capture.CURRENT_NVS_REQUEST, retained=refs)
        f.evidence = f.disk.private / 'current-settings-retry'; f.evidence.mkdir()
        f.handoff = f.evidence / 'candidate-handoff.json'
        f.capture_grant = dict(f.capture_grant, attempt='5'*32,
            request_sha256=custody.sha(custody.canonical(f.capture_request)))
        # The synthetic peer targets retain the enrollment established by the
        # prior trial, so the successor is the reviewed retained rekey case.
        f.request['case'] = 'retained_rekey'
        def firmware_boot(fixture, role, query):
            if query['operation'] in ('boot_candidate', 'restart_candidate'):
                # The SDK double handles flash/ROM; this separate target
                # double models the application firmware's volatile reboot.
                f.old.world.nodes[role].reset_volatile()
                if query['operation'] == 'restart_candidate' and role == 'B':
                    f.old.world.restart_count += 1
        f.mutate_after = firmware_boot
        f.make_package('execute', '1'*32)
        f.capture_package.update(evidence_root=str(f.evidence),handoff=str(f.handoff),
            request=f.file('capture-request-current.json',custody.canonical(f.capture_request)),
            grant=f.file('capture-grant-current.json',custody.canonical(f.capture_grant)))
        f.save_capture_package()
        return baseline

    def capture_only_retry(self, f):
        baseline = self.current_settings_retry(f)
        f.capture_package.update(operation='capture-only', handoff=None)
        f.save_capture_package()
        f.capture_log.clear(); f.log.clear(); f.factory_calls.clear()
        for sdk in f.sdks.values(): sdk.trace.clear()
        return baseline

    def run_capture_only(self, f):
        with patch.object(capture_runner, '_await_handoff', side_effect=AssertionError('handoff forbidden')) as handoff_wait, \
                patch.object(runner, 'run', side_effect=AssertionError('candidate forbidden')) as candidate_run:
            result = f.run_capture(mode='capture-only')
        candidate_run.assert_not_called()
        handoff_wait.assert_not_called()
        self.assertIsNone(result.candidate)
        self.assertFalse(f.handoff.exists())
        self.assertFalse((f.evidence / custody.ACTIVE).exists())
        self.assertFalse(any(e[0] == 'write' for sdk in f.sdks.values() for e in sdk.trace))
        return result

    def test_capture_only_current_settings_finish_releases_without_candidate_and_preserves_exact_bytes(self):
        f = self.fixture(); baseline = self.capture_only_retry(f)
        before = {p.name:p.read_bytes() for p in baseline.iterdir()}
        drift = adoption_fixture.RetainedOriginalTests().safe_drift()
        for sdk in f.sdks.values(): sdk.memory['nvs'] = drift
        current = {r:dict(sdk.memory) for r,sdk in f.sdks.items()}
        result = self.run_capture_only(f)
        self.assertEqual(result.outcome, 'released', result)
        self.assertTrue(result.capture.capture_complete)
        self.assertTrue(result.capture.custody_released)
        self.assertTrue(result.lease_released)
        self.assertFalse((f.evidence / capture.ACTIVE).exists())
        self.assertEqual(before, {p.name:p.read_bytes() for p in baseline.iterdir()})
        for role in custody.ROLES:
            self.assertEqual(current[role], f.sdks[role].memory)
            self.assertEqual(result.capture.roles[role]['pins']['nvs'], custody.descriptor(drift))
        self.assertEqual([e[0] for e in f.capture_log if e[1] == 'capture_reset'], ['A', 'B'])
        receipt = custody.decode(result.receipt_path.read_bytes())
        self.assertEqual(receipt['operation'], 'capture-only')
        self.assertEqual(receipt['original_mode'], 'retained_firmware_current_nvs_capture_only_released')
        self.assertIsNone(receipt['candidate_outcome'])
        calls = len(f.factory_calls)
        with self.assertRaisesRegex(runner.RunnerError, '^grant_used$'):
            f.run_capture(mode='capture-only')
        self.assertEqual(len(f.factory_calls), calls)

    def test_capture_only_requires_current_settings_null_handoff_and_exact_mode_before_factories(self):
        for fault in ('schema', 'handoff', 'mode', 'grant'):
            with self.subTest(fault=fault):
                f = self.fixture(); self.capture_only_retry(f)
                if fault == 'schema':
                    f.capture_request['schema'] = capture.RETAINED_REQUESTS[0]
                    f.capture_package['request'] = f.file('wrong-schema.json', custody.canonical(f.capture_request))
                elif fault == 'handoff': f.capture_package['handoff'] = str(f.handoff)
                elif fault == 'grant':
                    changed = dict(f.capture_grant, operation='capture-only')
                    f.capture_package['grant'] = f.file('wrong-grant.json', custody.canonical(changed))
                f.save_capture_package()
                expected = capture.CaptureError if fault == 'grant' else runner.RunnerError
                category = 'grant_invalid' if fault == 'grant' else 'handoff_invalid' if fault == 'handoff' else 'mode_invalid'
                with self.assertRaisesRegex(expected, '^' + category + '$'):
                    f.run_capture(mode='capture' if fault == 'mode' else 'capture-only')
                self.assertEqual(f.factory_calls, [])
                self.assertFalse((f.evidence / ('enrollment-original-used-' + '5'*32)).exists())

    def test_capture_only_partial_read_failure_never_promotes_capture_and_releases_safe_roles(self):
        f = self.fixture(); self.capture_only_retry(f)
        failed = [False]
        def fault(fixture, role, query):
            if role == 'B' and query['operation'] == 'guarded_read' and query.get('span') == 'nvs' and not failed[0]:
                failed[0] = True
                return True
            return False
        f.fault = fault
        result = self.run_capture_only(f)
        self.assertNotEqual(result.outcome, 'released', result)
        self.assertFalse(result.capture.capture_complete)
        self.assertIsNotNone(result.first_failure)
        self.assertNotEqual(custody.decode(result.receipt_path.read_bytes())['original_mode'],
            'retained_firmware_current_nvs_capture_only_released')

    def test_capture_only_ambiguous_reset_is_held_and_not_replayed(self):
        f = self.fixture(); self.capture_only_retry(f)
        f.capture_fault = ('reset', 'A')
        result = self.run_capture_only(f)
        self.assertEqual(result.outcome, 'held', result)
        self.assertFalse(result.capture.custody_released)
        self.assertIsNotNone(result.capture_cleanup_failure)
        self.assertEqual([e[0] for e in f.capture_log if e[1] == 'capture_reset'].count('A'), 1)
        self.assertTrue((f.evidence / capture.ACTIVE).exists())

    def test_capture_only_cleanup_expiry_lease_failure_and_torn_receipt_cannot_publish_success(self):
        for fault in ('expiry', 'lease', 'receipt'):
            with self.subTest(fault=fault):
                f = self.fixture(); self.capture_only_retry(f)
                save, factory, write_once = capture._Journal.save, f.lease_factory, runner._write_once
                def checked_save(journal, state):
                    answer = save(journal, state)
                    if fault == 'expiry' and state['phase'] == 'released': f.clock.value = 3612
                    return answer
                def checked_lease(*a, **k):
                    lease = factory(*a, **k)
                    close = lease.close
                    def failed_close():
                        closed = close()
                        return False if fault == 'lease' else closed
                    lease.close = failed_close
                    return lease
                def checked_write(path, raw, *, on_create=None):
                    if fault == 'receipt' and path.name.startswith('enrollment-original-runner-') and path.suffix == '.pending':
                        with path.open('xb') as stream:
                            if on_create is not None: on_create(path)
                            stream.write(raw[:12])
                        raise runner.RunnerError('receipt_failed')
                    return write_once(path, raw, on_create=on_create)
                f.lease_factory = checked_lease
                with patch.object(capture._Journal, 'save', checked_save), patch.object(runner, '_write_once', checked_write):
                    result = self.run_capture_only(f)
                self.assertNotEqual(result.outcome, 'released', result)
                self.assertIsNotNone(result.cleanup_failure)
                if result.receipt_path is not None:
                    receipt = custody.decode(result.receipt_path.read_bytes())
                    self.assertNotEqual(receipt['outcome'], 'released')
                    self.assertEqual(receipt['original_mode'], 'retained_firmware_current_nvs_capture_only_not_verified')

    def test_current_settings_composition_generates_handoff_from_fresh_pins_and_restores_them(self):
        f = self.fixture(); baseline = self.current_settings_retry(f)
        old_nvs = {r:f.devices[r].original['nvs'] for r in custody.ROLES}
        for role in custody.ROLES:
            # An erased safe page changes independently of the five immutable spans.
            f.sdks[role].memory['nvs'] = adoption_fixture.RetainedOriginalTests().safe_drift()
        before = {p.name:p.read_bytes() for p in baseline.iterdir()}
        wait = f.wait_handoff
        def handoff(seconds):
            receipt = custody.decode((f.evidence / ('enrollment-original-' + '5'*32 + '-pins.json')).read_bytes())
            for role in custody.ROLES:
                f.request['roles'][role]['originals'] = receipt['roles'][role]['originals']
            f.make_package('execute', '1'*32)
            wait(seconds)
        f.wait_handoff = handoff
        result = f.run_capture()
        self.assertEqual(result.outcome, 'passed', result)
        self.assertTrue(result.capture.custody_released)
        receipt = custody.decode(result.receipt_path.read_bytes())
        self.assertEqual(receipt['original_mode'], 'retained_firmware_current_nvs_verified')
        self.assertEqual(before, {p.name:p.read_bytes() for p in baseline.iterdir()})
        for role in custody.ROLES:
            self.assertNotEqual(f.devices[role].flash['nvs'], old_nvs[role])
            self.assertEqual(f.devices[role].flash['nvs'], adoption_fixture.RetainedOriginalTests().safe_drift())
            self.assertEqual(result.capture.roles[role]['pins']['nvs'],
                custody.descriptor(f.devices[role].flash['nvs']))

    def test_current_settings_old_candidate_nvs_pins_refuse_before_candidate_write(self):
        f = self.fixture(); self.current_settings_retry(f)
        for role in custody.ROLES:f.sdks[role].memory['nvs']=adoption_fixture.RetainedOriginalTests().safe_drift()
        start=len(f.log)
        result=f.run_capture()
        self.assertNotEqual(result.outcome,'passed',result)
        self.assertFalse(any(e[1] in ('write','boot_candidate') for e in f.log[start:]))
        self.assertTrue(result.capture.custody_released)

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
        self.assertAlmostEqual(prepared.capture_deadline, f.clock() + 2200 - f.utc(), delta=.0001)
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
            self.assertEqual(set(backend._counts), set(custody.SPANS))
            self.assertTrue(all(v == 3 for v in backend._counts.values()))
            self.assertLessEqual(backend._authority['capture_deadline'], 2110)
        # Candidate claim/boot retain direct safe-NVS reads; acquisition counters
        # remain empty rather than being reconstructed from the adopted proof.
        self.assertTrue(all(not b._counts for b in f.backend_instances))
        self.assertTrue(all(type(b) is rom.CaptureROMBackend and b._proof.used
            for b in f.capture_backends))
        self.assertTrue(all(type(b) is rom.ROMBackend and b._adoption_token is not None
            for b in f.backend_instances))
        for role in custody.ROLES:
            for name in custody.SPANS:
                self.assertEqual(sum(q[:3] == (role, 'guarded_read', name)
                    for q in f.capture_log), 3)
        self.assertTrue(all(d.flash[n] == d.original[n] for d in f.devices.values() for n in custody.SPANS))

    def test_handoff_budget_refuses_before_candidate_grant_and_releases_actual_capture(self):
        f = self.fixture()
        wait = f.wait_handoff
        def supplied(seconds):
            wait(seconds)
            f.clock.value = 712 # Original execution ceiling 2110 leaves 1398.
        f.wait_handoff = supplied
        result = f.run_capture()
        self.assertEqual(result.first_failure, ('ownership', 'budget_insufficient'))
        self.assertEqual(result.outcome, 'failed', result)
        self.unused_candidate(f)
        self.assertTrue(result.capture.custody_released)
        self.assertTrue(result.lease_released)
        self.assertEqual({e[0] for e in f.capture_log if e[1] == 'capture_reset'}, {'A', 'B'})
        self.assertFalse(any(e[0] == 'write' for sdk in f.sdks.values() for e in sdk.trace))

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
                        if fault == 'expired': f.utc.value = 2200
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
                            if count[0] == 2: f.clock.value = 3612
                            return result
                        lease.close = delayed
                        return lease
                    f.lease_factory = lease_factory
                    result = f.run_capture()
                else:
                    save = capture._Journal.save
                    def delayed(journal, state):
                        result = save(journal, state)
                        if state['phase'] == 'released': f.clock.value = 3612
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
