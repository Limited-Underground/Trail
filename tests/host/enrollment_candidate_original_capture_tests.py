"""Actual coordinator/ROM dispatch with synthetic SDK devices only; no USB IO."""
from pathlib import Path
from dataclasses import replace
import copy
import json
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_original_capture as capture
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_custody as custody
import enrollment_candidate_rom_adapter_tests as sdk_fixture


class Fixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        self.worktree = Path(self.temp.name)
        self.private = self.worktree / '.private'
        self.private.mkdir()
        self.root = self.private / 'originals'
        self.root.mkdir()
        self.clock = sdk_fixture.FakeClock()
        self.utc = lambda: 1000 + self.clock.value - 10
        self.key = b'K' * 32
        self.identities = {'A': '123456789abc', 'B': '223456789abc'}
        self.profiles, self.originals, self.sdks = {}, {}, {}
        for role in custody.ROLES:
            marker = b'A' if role == 'A' else b'B'
            originals = {'bootloader': marker * 32768,
                'partition': sdk_fixture.table_from_csv(ROOT / 'firmware/targets/heltec_v4_bench/partitions.csv'),
                'otadata': b'\xff' * 8192, 'nvs': b'\xff' * 12288,
                'application': marker * 733184, 'ota0_prefix': marker * 16384}
            self.originals[role] = originals
            sdk = sdk_fixture.FakeSDK(originals, self.clock)
            sdk.mac = self.identities[role]
            sdk.ports[0].serial_number = sdk.mac
            sdk.ports[0].device = 'COM19' if role == 'A' else 'COM20'
            self.sdks[role] = sdk
            self.profiles[role] = rom.DeviceProfile('heltec_v4_esp32s3',
                rom.opaque_identity(self.key, sdk.mac), 16777216, ('a' if role == 'A' else 'b') * 64)
        self.request = {'schema': 'OT-ORIGINAL-CAPTURE-REQUEST-1', 'runtime_sha256': '1' * 64,
            'roles': {r: {'device_binding': p.device_binding, 'profile_evidence_sha256': p.evidence_sha256}
                for r, p in self.profiles.items()}}
        self.manifest = {'worktree': str(self.worktree), 'root': str(self.private / 'synthetic-capsule'), 'files': {}}
        self.calls, self.fault, self.mutate_after = [], None, None
        self.launch_seconds = 0
        self.runtime = rom.Runtime(self.private / 'manifest.json', '1' * 64, self.private,
            subprocess_run=self.run, manifest_verifier=lambda *_: copy.deepcopy(self.manifest), monotonic=self.clock)
        self.lease = rom.HardwareLease(self.private, monotonic=self.clock)
        self.grant_raw = self.grant('capture', '1' * 32)
        self.session = self.make_session()

    def grant(self, operation, attempt, origin=None):
        return capture.canonical({'schema': 'OT-ORIGINAL-CAPTURE-GRANT-1', 'attempt': attempt,
            'request_sha256': capture.sha(capture.canonical(self.request)),
            'runtime_sha256': self.request['runtime_sha256'], 'operation': operation,
            'origin_attempt': origin, 'attempt_count': 1, 'actions': list(capture.ACTIONS),
            'issued_utc': 900, 'capture_expires_utc': 1200, 'cleanup_expires_utc': 1300})

    def make_session(self, **changes):
        values = dict(runtime=self.runtime, lease=self.lease, identities=self.identities,
            binding_key=self.key, profiles=self.profiles, utc=self.utc, monotonic=self.clock)
        values.update(changes)
        return capture.CaptureSession(self.private, self.root, self.request, self.grant_raw,
            capture.sha(self.grant_raw), **values)

    def run(self, argv, **kwargs):
        # Explicit synthetic process cost, not a measured physical duration.
        self.clock.value += self.launch_seconds
        query = json.loads(kwargs['input'])
        role = next(r for r, identity in self.identities.items() if identity == query['identity'])
        self.calls.append((role, query['operation'], query.get('span'), query['deadline']))
        if self.fault is not None and self.fault(self, role, query):
            return SimpleNamespace(returncode=1,
                stdout=json.dumps(sdk_fixture.DISPATCH['failure_answer'](
                    RuntimeError('synthetic worker failure'))).encode(), stderr=b'')
        sdk = self.sdks[role]
        try:
            answer = sdk_fixture.execute_request(query, sdk.rom, sdk, sdk.comports, self.clock,
                loader=sdk.loader, reset=sdk.reset)
            result = SimpleNamespace(returncode=0, stdout=json.dumps(answer).encode(), stderr=b'')
        except BaseException as error:
            result = SimpleNamespace(returncode=1,
                stdout=json.dumps(sdk_fixture.DISPATCH['failure_answer'](error)).encode(), stderr=b'')
        if self.mutate_after:
            self.mutate_after(self, role, query)
        return result

    def candidate(self):
        table = sdk_fixture.table_from_csv(ROOT / 'firmware/targets/heltec_v4_enrollment_candidate_eval/partitions.csv')
        return {'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': '1' * 64, 'case': 'first', 'group': 7,
            'images': {'application': capture.descriptor(b'host-only-candidate'), 'partition': capture.descriptor(table)},
            'roles': {r: {'device_binding': self.profiles[r].device_binding,
                'originals': {name: capture.descriptor(raw) for name, raw in self.originals[r].items()}}
                for r in custody.ROLES}}

    def verify(self, request=None, **changes):
        values = dict(runtime=self.runtime, lease=self.lease, identities=self.identities,
            binding_key=self.key, profiles=self.profiles, deadline=200)
        values.update(changes)
        return self.session.verify(request or self.candidate(), **values)

    def candidate_journal(self, claims=('A',), settled=()):
        root = self.private / 'candidate'
        root.mkdir(exist_ok=True)
        request, attempt = self.candidate(), '2' * 32
        state = {'schema': 'OT-CANDIDATE-JOURNAL-1', 'attempt': attempt, 'request': request,
            'observation': 'not_observed', 'first_failure': None, 'closed': False,
            'owner_confirmed': False, 'bridge_open': False, 'roles': {}}
        for role in custody.ROLES:
            row = {'captures': {}, 'events': ['claim_intent'] if role in claims else [],
                'restore_verified': False, 'original_boot_allowed': False,
                'handles_closed': False, 'settled_untouched': False}
            if role in settled:
                row.update(captures=request['roles'][role]['originals'], events=['claim_intent',
                    'six_span_sweep_verified', 'original_reset_intent', 'original_boot_verified',
                    'rom_handles_closed'], restore_verified=True, original_boot_allowed=True, handles_closed=True)
            state['roles'][role] = row
        (root / custody.ACTIVE).write_bytes(capture.canonical({'attempt': attempt,
            'request_sha256': capture.sha(capture.canonical(request))}))
        (root / ('enrollment-candidate-' + attempt + '.json')).write_bytes(capture.canonical(state))
        return root, request, attempt

    def release_recovery(self, **changes):
        raw = self.grant('release', '3' * 32, '1' * 32)
        values = dict(runtime=self.runtime, lease=self.lease, identities=self.identities,
            binding_key=self.key, profiles=self.profiles, utc=self.utc, monotonic=self.clock)
        values.update(changes)
        return capture.recover_release(self.private, self.root, self.request, raw, capture.sha(raw), **values)

    def close(self):
        # Synthetic failures intentionally leave durable custody held. Tear down
        # the test-only OS lock without claiming original release or replay.
        if self.lease._file is not None:
            self.lease._states = {}
            self.lease.close()
        self.temp.cleanup()


class CaptureTests(unittest.TestCase):
    def fixture(self):
        value = Fixture()
        self.addCleanup(value.close)
        return value

    def test_import_and_constructor_are_inert(self):
        f = self.fixture()
        self.assertEqual(f.calls, [])
        self.assertEqual(list(f.root.iterdir()), [])
        self.assertIsNone(f.lease._file)
        self.assertEqual(repr(f.session), '<OTCAND1 original capture session>')

    def test_actual_two_role_six_span_capture_persists_outputs_and_holds_rom(self):
        f = self.fixture()
        result = f.session.capture()
        self.assertEqual(result.outcome, 'captured')
        self.assertTrue(result.capture_complete)
        self.assertFalse(result.custody_released)
        self.assertTrue(f.session.held)
        self.assertFalse(f.lease.close())
        self.assertEqual({q[1] for q in f.calls}, {'guard', 'guarded_read'})
        for role in custody.ROLES:
            self.assertTrue(f.lease.holds_rom(f.profiles[role].device_binding))
            for name in custody.SPANS:
                self.assertEqual(sum(q[:3] == (role, 'guarded_read', name) for q in f.calls), 2)
                self.assertEqual(f.session._journal.original(role, name).read_bytes(), f.originals[role][name])
        role_order = [q[0] for q in f.calls]
        self.assertEqual(role_order, sorted(role_order))
        self.assertFalse(any(entry[0] in ('write', 'reset') for sdk in f.sdks.values() for entry in sdk.trace))
        pins = f.session.pins
        pins['A']['nvs']['sha256'] = '0' * 64
        self.assertNotEqual(pins, f.session.pins)
        receipt = capture.decode(result.pins_path.read_bytes())
        self.assertEqual(receipt['roles']['B']['originals']['ota0_prefix'], capture.descriptor(f.originals['B']['ota0_prefix']))

    def test_release_only_resets_originals_sequentially_without_flash_writes(self):
        f = self.fixture()
        f.session.capture()
        f.clock.value = 220 # Capture expired; cleanup remains valid.
        result = f.session.release()
        self.assertEqual(result.outcome, 'released')
        self.assertTrue(result.custody_released)
        self.assertFalse(f.session.held)
        self.assertTrue(f.lease.close())
        self.assertEqual([q[0] for q in f.calls if q[1] == 'reset_original'], ['A', 'B'])
        self.assertFalse(any(entry[0] == 'write' for sdk in f.sdks.values() for entry in sdk.trace))
        count = len(f.calls)
        f.session.release()
        self.assertEqual(len(f.calls), count)

    def test_freshness_actually_repeats_all_spans_and_is_one_use(self):
        f = self.fixture()
        f.session.capture()
        before = len(f.calls)
        self.assertTrue(f.verify())
        fresh = f.calls[before:]
        for role in custody.ROLES:
            for name in custody.SPANS:
                self.assertEqual(sum(q[:3] == (role, 'guarded_read', name) for q in fresh), 1)
        with self.assertRaises(capture.CaptureError): f.verify()
        self.assertTrue(f.session.release().custody_released)

    def test_changed_current_originals_refuse_freshness_and_safe_reset(self):
        f = self.fixture()
        f.session.capture()
        f.sdks['B'].memory['ota0_prefix'] = b'X' * 16384
        with self.assertRaises(capture.CaptureError): f.verify()
        result = f.session.release()
        self.assertFalse(result.custody_released)
        self.assertIsNotNone(result.first_failure)
        self.assertIsNotNone(result.cleanup_failure)
        self.assertEqual(sum(e[0] == 'reset' for e in f.sdks['B'].trace), 0)

    def test_persisted_original_receipt_and_journal_drift_refuse_handoff(self):
        for fault in ('raw', 'receipt', 'journal'):
            f = self.fixture()
            f.session.capture()
            path = f.session._journal.original('A', 'nvs') if fault == 'raw' else \
                f.session.result.pins_path if fault == 'receipt' else f.session.journal_path
            path.write_bytes(path.read_bytes() + b'changed')
            before = len(f.calls)
            with self.subTest(fault=fault), self.assertRaises(capture.CaptureError): f.verify()
            self.assertEqual(len(f.calls), before)

    def test_same_live_lease_runtime_profiles_key_and_request_are_required(self):
        for fault in ('runtime', 'lease', 'profile', 'identity', 'key', 'request'):
            f = self.fixture()
            f.session.capture()
            changes, request = {}, f.candidate()
            if fault == 'runtime': changes['runtime'] = object()
            if fault == 'lease': changes['lease'] = object()
            if fault == 'profile':
                changes['profiles'] = dict(f.profiles, A=rom.DeviceProfile('heltec_v4_esp32s3', f.profiles['A'].device_binding, 16777216, 'f' * 64))
            if fault == 'identity': changes['identities'] = dict(f.identities, A=f.identities['B'])
            if fault == 'key': changes['binding_key'] = b'Z' * 32
            if fault == 'request': request['roles']['A']['originals']['nvs']['sha256'] = 'f' * 64
            before = len(f.calls)
            with self.subTest(fault=fault), self.assertRaises(capture.CaptureError): f.verify(request, **changes)
            self.assertEqual(len(f.calls), before)

    def test_held_rom_is_required_not_just_idle_or_disk_receipt(self):
        f = self.fixture()
        f.session.capture()
        f.lease._state(f.profiles['B'].device_binding)['mode'] = 'original-ready'
        with self.assertRaises(capture.CaptureError): f.verify()

    def test_partial_failure_releases_originals_without_capture_promotion(self):
        f = self.fixture()
        failed = [False]
        def fault(_, role, query):
            if role == 'A' and query['operation'] == 'guarded_read' and query['span'] == 'ota0_prefix' and not failed[0]:
                failed[0] = True
                return True
            return False
        f.fault = fault
        result = f.session.capture()
        self.assertFalse(result.capture_complete)
        self.assertTrue(result.custody_released)
        self.assertEqual(result.outcome, 'failed')
        self.assertIsNotNone(result.first_failure)
        self.assertIsNone(result.cleanup_failure)
        self.assertEqual([q[0] for q in f.calls if q[1] == 'reset_original'], ['A'])
        self.assertFalse(any(q[0] == 'B' for q in f.calls))
        self.assertIsNone(result.pins_path)

    def test_primary_read_failure_and_cleanup_failure_remain_separate(self):
        f = self.fixture()
        f.sdks['A'].read_fault = 'application'
        f.sdks['A'].reset_fault = 5
        result = f.session.capture()
        self.assertFalse(result.custody_released)
        self.assertEqual(result.first_failure[0], 'capture_A_application')
        self.assertEqual(result.cleanup_failure[0], 'release_A')
        self.assertNotEqual(result.first_failure, result.cleanup_failure)

    def test_uncertain_original_reset_is_not_replayed_in_process_or_recovery(self):
        f = self.fixture()
        f.session.capture()
        f.sdks['A'].reset_fault = 5
        result = f.session.release()
        self.assertFalse(result.custody_released)
        calls = len(f.calls)
        f.sdks['A'].reset_fault = None
        f.session.release()
        self.assertEqual(len(f.calls), calls)
        with self.assertRaisesRegex(capture.CaptureError, 'original_boot_state_uncertain'):
            f.release_recovery()
        self.assertEqual(len(f.calls), calls)
        self.assertFalse((f.root / ('enrollment-original-used-' + '3' * 32)).exists())

    def test_grant_exact_scope_hash_expiry_and_replay_before_any_device_access(self):
        for fault in ('hash', 'actions', 'operation', 'expiry', 'duration', 'attempts'):
            f = self.fixture()
            grant = capture.decode(f.grant_raw)
            if fault == 'hash': grant['request_sha256'] = 'f' * 64
            if fault == 'actions': grant['actions'].append('write_flash')
            if fault == 'operation': grant['operation'] = 'execute'
            if fault == 'expiry': grant['capture_expires_utc'] = 999
            if fault == 'duration': grant['cleanup_expires_utc'] = 4501
            if fault == 'attempts': grant['attempt_count'] = 2
            f.grant_raw = capture.canonical(grant)
            f.session = f.make_session()
            with self.subTest(fault=fault), self.assertRaises(capture.CaptureError): f.session.capture()
            self.assertEqual(f.calls, [])
        f = self.fixture()
        f.session.capture()
        f.session.release()
        with self.assertRaisesRegex(capture.CaptureError, 'grant_used'): f.make_session().capture()

    def test_existing_original_or_candidate_custody_does_not_spend_capture_grant(self):
        for marker in (capture.ACTIVE, custody.ACTIVE):
            f = self.fixture()
            (f.root / marker).write_bytes(b'{"held":true}')
            with self.subTest(marker=marker), self.assertRaisesRegex(capture.CaptureError, 'custody_held'):
                f.session.capture()
            self.assertEqual(f.calls, [])
            self.assertFalse((f.root / ('enrollment-original-used-' + '1' * 32)).exists())

    def test_deadlines_are_fixed_clamped_and_cleanup_never_renews(self):
        f = self.fixture()
        f.session = f.make_session(capture_deadline=100, cleanup_deadline=150)
        f.session.capture()
        self.assertEqual(f.session.capture_deadline, 100)
        self.assertEqual(f.session.cleanup_deadline, 150)
        f.clock.value = 150
        result = f.session.release()
        self.assertFalse(result.custody_released)
        self.assertEqual(result.cleanup_failure[1], 'deadline_expired')
        self.assertFalse(any(q[1] == 'reset_original' for q in f.calls))

    def test_expired_freshness_fails_before_reads(self):
        f = self.fixture()
        f.session.capture()
        f.clock.value = 210
        count = len(f.calls)
        with self.assertRaises(capture.CaptureError): f.verify(deadline=300)
        self.assertEqual(len(f.calls), count)

    def test_earlier_candidate_freshness_deadline_reaches_every_actual_worker(self):
        f = self.fixture()
        f.session.capture()
        count = len(f.calls)
        self.assertTrue(f.verify(deadline=30))
        self.assertEqual({q[3] for q in f.calls[count:]}, {30})
        g = self.fixture()
        g.session.capture()
        def delay(fixture, role, query):
            if query['operation'] == 'guarded_read': fixture.clock.value = query['deadline']
        g.mutate_after = delay
        count = len(g.calls)
        with self.assertRaises(capture.CaptureError): g.verify(deadline=30)
        self.assertEqual({q[3] for q in g.calls[count:]}, {30})
        self.assertEqual(g.session.result.first_failure[0], 'freshness_A_bootloader')

    def test_actual_capture_and_freshness_reduce_launches_under_unchanged_deadlines(self):
        old = self.fixture(); old.launch_seconds = 1
        with mock.patch.object(rom.CaptureROMBackend, 'supports_guarded_read', False):
            self.assertTrue(old.session.capture().capture_complete)
            capture_launches = len(old.calls)
            self.assertTrue(old.verify())
        new = self.fixture(); new.launch_seconds = 1
        self.assertTrue(new.session.capture().capture_complete)
        new_capture_launches = len(new.calls)
        self.assertTrue(new.verify())
        self.assertEqual((capture_launches, len(old.calls) - capture_launches), (74, 36))
        self.assertEqual((new_capture_launches, len(new.calls) - new_capture_launches), (26, 12))
        self.assertEqual((old.clock.value, new.clock.value), (120, 48))
        self.assertEqual((old.session.capture_deadline, old.session.cleanup_deadline),
                         (new.session.capture_deadline, new.session.cleanup_deadline))
        for fixture in (old, new):
            for role in custody.ROLES:
                for name in custody.SPANS:
                    reads = [q for q in fixture.calls if q[0] == role
                             and q[1] in ('read', 'guarded_read') and q[2] == name]
                    self.assertEqual(len(reads), 3) # Two acquisition sweeps, one freshness sweep.
            self.assertFalse(any(entry[0] in ('write', 'reset')
                for sdk in fixture.sdks.values() for entry in sdk.trace))

    def test_atomic_capture_rejects_malformed_or_post_admission_without_fallback(self):
        for malformed in ('list', 'short', 'post'):
            f = self.fixture(); f.session.capture()
            backend = f.session._backends['A']
            admission = backend.guard(f.profiles['A'].device_binding, 200)
            value = (admission, f.originals['A']['bootloader'], admission)
            if malformed == 'list': value = list(value)
            elif malformed == 'short': value = (admission, value[1][:-1], admission)
            else: value = (admission, value[1], replace(admission, security='unknown'))
            calls = len(f.calls)
            with self.subTest(malformed=malformed), mock.patch.object(backend, 'guarded_read', return_value=value):
                with self.assertRaises(capture.CaptureError): f.verify()
            self.assertEqual(len(f.calls), calls)
            self.assertEqual(f.session.result.first_failure[0], 'freshness_A_bootloader')

    def test_capture_categories_require_exact_owned_type_and_fixed_argument(self):
        class Spoof(RuntimeError):
            def __str__(self): raise AssertionError('unknown exception text was inspected')
        class OwnedSubclass(rom.AdapterError): pass
        for error in (Spoof('deadline_expired'), OwnedSubclass('deadline_expired'),
                      rom.AdapterError('SECRET_WORD'), rom.AdapterError('deadline_expired', 'SECRET_WORD')):
            with self.subTest(type=type(error).__name__):
                self.assertEqual(capture._category(error), 'capture_operation_failed')
        self.assertEqual(capture._category(rom.AdapterError('rom_operation_failed')), 'rom_operation_failed')
        self.assertEqual(capture._category(capture.ControllerError('deadline_expired')), 'deadline_expired')

    def test_persistence_interruption_retains_custody_and_refuses_reconstruction(self):
        f = self.fixture()
        once = capture._once
        def fail(path, raw):
            if path.name.endswith('-A-nvs.bin'):
                with path.open('xb') as stream: stream.write(raw[:10])
                raise KeyboardInterrupt()
            return once(path, raw)
        with mock.patch.object(capture, '_once', fail):
            result = f.session.capture()
        self.assertFalse(result.capture_complete)
        # A reset can safely release a capture failure if the saved partial
        # file was never admitted as an original. It cannot produce handoff.
        self.assertTrue(result.custody_released)
        with self.assertRaises(capture.CaptureError): f.verify()
        with self.assertRaises(capture.CaptureError): f.make_session().capture()

    def test_pending_journal_interruption_cannot_reset_or_consume_release_grant(self):
        f = self.fixture()
        f.session.capture()
        f.session._journal.pending.write_bytes(b'{"interrupted":true}')
        count = len(f.calls)
        with self.assertRaisesRegex(capture.CaptureError, 'journal_pending'): f.release_recovery()
        self.assertEqual(len(f.calls), count)
        self.assertFalse((f.root / ('enrollment-original-used-' + '3' * 32)).exists())

    def test_new_release_grant_recovers_saved_rom_originals_but_never_handoff(self):
        f = self.fixture()
        f.session.capture()
        result = f.release_recovery()
        self.assertTrue(result.custody_released)
        self.assertEqual([q[0] for q in f.calls if q[1] == 'reset_original'], ['A', 'B'])
        self.assertFalse((f.root / capture.ACTIVE).exists())

    def test_interrupted_settled_marker_closure_recovers_without_claim_read_or_reset(self):
        f = self.fixture()
        f.session.capture()
        unlink = Path.unlink
        def fail_marker(path, *args, **kwargs):
            if path == f.root / capture.ACTIVE: raise OSError('synthetic marker unlink failure')
            return unlink(path, *args, **kwargs)
        with mock.patch.object(Path, 'unlink', fail_marker):
            result = f.session.release()
        self.assertFalse(result.custody_released)
        self.assertEqual(result.cleanup_failure[0], 'release_record')
        self.assertEqual(capture.decode(f.session.journal_path.read_bytes())['phase'], 'released')
        self.assertTrue((f.root / capture.ACTIVE).exists())
        self.assertTrue(f.lease.close())
        fresh_lease = rom.HardwareLease(f.private, monotonic=f.clock)
        calls = len(f.calls)
        def forbidden_backend(*args, **kwargs):
            raise AssertionError('closure-only recovery constructed a hardware backend')
        recovered = f.release_recovery(lease=fresh_lease, backend_factory=forbidden_backend)
        self.assertTrue(recovered.custody_released)
        self.assertEqual(len(f.calls), calls)
        self.assertFalse((f.root / capture.ACTIVE).exists())
        self.assertEqual(capture.decode(recovered.journal_path.read_bytes())['phase'], 'released')
        self.assertTrue(fresh_lease.close())

    def test_released_word_without_settled_proofs_cannot_consume_release_grant(self):
        f = self.fixture()
        f.session.capture()
        state = copy.deepcopy(f.session._state)
        state['phase'] = 'released'
        f.session.journal_path.write_bytes(capture.canonical(state))
        calls = len(f.calls)
        with self.assertRaises(capture.CaptureError): f.release_recovery()
        self.assertEqual(len(f.calls), calls)
        self.assertFalse((f.root / ('enrollment-original-used-' + '3' * 32)).exists())

    def test_interrupted_closure_requires_exact_matching_active_marker(self):
        f = self.fixture()
        f.session.capture()
        unlink = Path.unlink
        def fail_marker(path, *args, **kwargs):
            if path == f.root / capture.ACTIVE: raise OSError('synthetic marker unlink failure')
            return unlink(path, *args, **kwargs)
        with mock.patch.object(Path, 'unlink', fail_marker): f.session.release()
        (f.root / capture.ACTIVE).write_bytes(capture.canonical({'attempt': '1' * 32,
            'request_sha256': 'f' * 64}))
        calls = len(f.calls)
        with self.assertRaises(capture.CaptureError): f.release_recovery()
        self.assertEqual(len(f.calls), calls)
        self.assertFalse((f.root / ('enrollment-original-used-' + '3' * 32)).exists())

    def test_recovery_claim_or_saved_pin_comparison_failure_is_durably_held(self):
        for fault in ('claim', 'pin'):
            f = self.fixture()
            f.session.capture()
            if fault == 'claim': f.sdks['A'].security['flags'] = 1
            else: f.sdks['A'].memory['ota0_prefix'] = b'X' * 16384
            result = f.release_recovery()
            self.assertFalse(result.custody_released)
            self.assertIsNotNone(result.first_failure)
            self.assertIsNotNone(result.cleanup_failure)
            self.assertTrue((f.root / capture.ACTIVE).exists())
            self.assertFalse(any(q[1] == 'reset_original' for q in f.calls))
            durable = capture.decode(result.journal_path.read_bytes())
            self.assertEqual(durable['first_failure'], list(result.first_failure))

    def test_malformed_boot_or_candidate_release_ledgers_never_authorize_cleanup(self):
        for fault in ('boot-without-intent', 'false-candidate-release', 'candidate-request'):
            f = self.fixture()
            f.session.capture()
            state = copy.deepcopy(f.session._state)
            if fault == 'boot-without-intent':
                state['roles']['A']['original_boot_allowed'] = True
            else:
                f.verify()
                root, request, attempt = f.candidate_journal()
                f.session.transfer_role('A', root, request, attempt)
                state = copy.deepcopy(f.session._state)
                if fault == 'false-candidate-release': state['roles']['A']['owner'] = 'candidate-released'
                else: state['candidate']['request']['roles']['A']['originals']['nvs']['sha256'] = 'f' * 64
            f.session.journal_path.write_bytes(capture.canonical(state))
            count = len(f.calls)
            with self.subTest(fault=fault), self.assertRaises((capture.CaptureError, custody.Error)):
                f.release_recovery()
            self.assertEqual(len(f.calls), count)
            self.assertFalse((f.root / ('enrollment-original-used-' + '3' * 32)).exists())

    def test_transfer_requires_actual_candidate_active_journal_and_role_claim(self):
        f = self.fixture()
        f.session.capture()
        f.verify()
        root, request, attempt = f.candidate_journal(claims=('A',))
        with self.assertRaises(capture.CaptureError): f.session.transfer_role('B', root, request, attempt)
        self.assertTrue(f.session.transfer_role('A', root, request, attempt))
        with self.assertRaises(capture.CaptureError): f.session.transfer_role('A', root, request, attempt)
        self.assertEqual(f.session.result.roles['B']['owner'], 'capture')

    def test_used_marker_alone_never_transfers(self):
        f = self.fixture()
        f.session.capture()
        f.verify()
        root = f.private / 'candidate'
        root.mkdir()
        attempt = '2' * 32
        (root / ('enrollment-candidate-used-' + attempt)).write_bytes(b'{}')
        with self.assertRaises(capture.CaptureError): f.session.transfer_role('A', root, f.candidate(), attempt)
        self.assertEqual(f.session.result.roles['A']['owner'], 'capture')

    def test_mixed_adoption_releases_unclaimed_b_but_keeps_unresolved_a_marker(self):
        f = self.fixture()
        f.session.capture()
        f.verify()
        root, request, attempt = f.candidate_journal()
        f.session.transfer_role('A', root, request, attempt)
        result = f.session.release()
        self.assertFalse(result.custody_released)
        self.assertTrue(result.roles['B']['original_boot_allowed'])
        self.assertEqual([q[0] for q in f.calls if q[1] == 'reset_original'], ['B'])
        self.assertTrue((f.root / capture.ACTIVE).exists())
        count = len(f.calls)
        with self.assertRaisesRegex(capture.CaptureError, 'candidate_custody_unresolved'): f.release_recovery()
        self.assertEqual(len(f.calls), count)

    def test_settled_candidate_ledger_releases_marker_without_rebooting_adopted_a(self):
        f = self.fixture()
        f.session.capture()
        f.verify()
        root, request, attempt = f.candidate_journal()
        f.session.transfer_role('A', root, request, attempt)
        f.candidate_journal(settled=('A',))
        result = f.session.release()
        self.assertTrue(result.custody_released)
        self.assertEqual(result.roles['A']['owner'], 'candidate-released')
        self.assertEqual([q[0] for q in f.calls if q[1] == 'reset_original'], ['B'])

    def test_private_path_and_symlink_rejections_are_pre_device(self):
        f = self.fixture()
        outside = f.worktree / 'outside'
        outside.mkdir()
        f.session.root = outside
        with self.assertRaises(capture.CaptureError): f.session.capture()
        self.assertEqual(f.calls, [])
        g = self.fixture()
        link = g.private / 'link'
        try: link.symlink_to(g.root, target_is_directory=True)
        except OSError:
            # Windows junctions are equally rejected by the implementation.
            with mock.patch.object(Path, 'is_symlink', return_value=True):
                with self.assertRaises(capture.CaptureError): g.session.capture()
        else:
            g.session.root = link
            with self.assertRaises(capture.CaptureError): g.session.capture()
        self.assertEqual(g.calls, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
