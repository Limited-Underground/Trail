"""Computer-only adapter regressions; actual operator/controller/parser/lease.

Fake serial, target, private view and flash are explicit dependencies. Invented
records and gestures are orchestration fixtures, not authentication, hardware,
SDK, display or restoration acceptance evidence. Existing core suites are
imported for their fake target/flash helpers only; their tests are not executed.
"""
import builtins
import importlib
from pathlib import Path
import struct
import sys
import tempfile
from types import MethodType, SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2] / 'tools'))
sys.path.insert(0, str(HERE.parent))
import enrollment_candidate_controller as core
import enrollment_candidate_custody as custody
import enrollment_candidate_operator as operator
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_usb_client as wire
import enrollment_candidate_controller_tests as target_fixture
import enrollment_candidate_custody_tests as flash_fixture

ROLES = ('A', 'B')
IDENTITIES = {'A': '010203040506', 'B': '111213141516'}
KEY = bytes(range(32))
RAW_IDS = {'A': bytes(range(1, 33)), 'B': bytes(range(33, 65))}
EXPECTED_A = ('0102030405060708', '090A0B0C0D0E0F10',
              '1112131415161718', '191A1B1C1D1E1F20')
EXPECTED_B = ('2122232425262728', '292A2B2C2D2E2F30',
              '3132333435363738', '393A3B3C3D3E3F40')


class UTC:
    def __init__(self, step=0):
        self.value, self.step = 100.0, step

    def __call__(self):
        self.value += self.step
        return self.value


def request_for(case='first', devices=None):
    images = {'application': b'isolated fake candidate', 'partition': b'C' * 4096}
    originals = (devices if devices is not None else {role: SimpleNamespace(original={
        name: bytes([base + index + 1]) * size
        for index, (name, (_, size)) in enumerate(flash_fixture.EXPECTED_SPANS.items())})
        for role, base in (('A', 32), ('B', 64))})
    request = {'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': 'c' * 64,
               'case': case, 'group': 17,
               'images': {name: custody.descriptor(raw) for name, raw in images.items()},
               'roles': {role: {'device_binding': rom.opaque_identity(KEY, IDENTITIES[role]),
                                'originals': {name: custody.descriptor(raw)
                                              for name, raw in device.original.items()}}
                         for role, device in originals.items()}}
    return request, images


def grant_for(request, attempt='1' * 32):
    raw = custody.canonical({'schema': 'OT-CANDIDATE-GRANT-1', 'attempt': attempt,
        'request_sha256': custody.sha(custody.canonical(request)),
        'runtime_sha256': request['runtime_sha256'], 'operation': 'execute',
        'origin_attempt': None, 'attempt_count': 1,
        'actions': custody.actions_for(request, 'execute'), 'issued_utc': 100,
        'execute_expires_utc': 200, 'restore_expires_utc': 300})
    return raw, custody.sha(raw)


def candidate(role):
    # Independently specified canonical target encoding, not a parser mirror.
    return wire.Reply('CANDIDATE', ((struct.pack('<II', 1, 1) + RAW_IDS[role]).hex(),))


def references():
    ref = operator.CapturedReferences()
    ref.begin(1)
    for role in ROLES:
        ref.capture(role, 1, candidate(role))
    return ref


def point(kind='transcript', roles=ROLES, deadline=2):
    return {'schema': 'OT-CANDIDATE-CHECKPOINT-1', 'kind': kind, 'roles': roles,
            'token': '2' * 32, 'deadline': deadline}


class PrivateView:
    """Explicit synthetic human input, kept separate from target observations."""
    def __init__(self, world=None):
        self.world, self.active = world, None
        self.shows, self.polls, self.closes = [], 0, 0
        self.show_fault, self.close_fault, self.ack_fault = None, None, None
        self.no_ack, self.no_gesture = None, None
        self.close_advance, self.on_show = None, None

    def show(self, prompt):
        self.active = prompt
        # Keep only safe metadata in persistent fake-view observations.
        self.shows.append((prompt.kind, prompt.roles, prompt.group, prompt.deadline))
        if self.world is not None:
            self.world.events.append(('show', prompt.kind, prompt.roles))
        if self.on_show is not None:
            self.on_show(prompt)
        if self.show_fault == 'raise':
            raise operator.OperatorError('PRIVATE rows in callback error')
        return self.show_fault != 'false'

    def poll(self):
        self.polls += 1
        p = self.active
        if self.no_ack == p.kind:
            return None
        if self.world is not None and self.no_gesture != p.kind:
            for role in p.roles:
                node = self.world.nodes[role]
                if p.kind == 'fingerprint_local':
                    node.require(node.visible_page == 'local')
                    node.local_observed = True
                elif p.kind == 'fingerprint':
                    node.require(node.visible_page == 'peer')
                    node.reviewed = True
                elif p.kind == 'transcript':
                    node.confirmed = True
                elif p.kind == 'reset_gesture':
                    node.reset_intent = True
        if self.ack_fault == 'raw_true':
            return True
        if self.ack_fault == 'wrong_token':
            return core.CheckpointAck('OT-CANDIDATE-ACK-1', p.kind, p.roles, 'f' * 32)
        if self.ack_fault == 'wrong_roles':
            return core.CheckpointAck('OT-CANDIDATE-ACK-1', p.kind, ('B',), p.token)
        if self.ack_fault == 'wrong_kind':
            return core.CheckpointAck('OT-CANDIDATE-ACK-1', 'usual_screen', p.roles, p.token)
        if self.ack_fault == 'wrong_schema':
            return core.CheckpointAck('other', p.kind, p.roles, p.token)
        return core.CheckpointAck('OT-CANDIDATE-ACK-1', p.kind, p.roles, p.token)

    def close(self):
        self.closes += 1
        self.active = None
        if self.close_advance is not None:
            self.world.clock.value = self.close_advance
        if self.close_fault == 'raise':
            raise RuntimeError('PRIVATE view close detail')
        return self.close_fault != 'false'


class SerialHandle(target_fixture.Handle):
    def __init__(self, fixture, kwargs):
        super().__init__(fixture.world, None, None)
        self.fixture, self.kwargs = fixture, dict(kwargs)
        self.port, self.is_open, self._dtr, self._rts = kwargs['port'], False, True, True
        self.timeout, self.write_timeout = kwargs['timeout'], kwargs['write_timeout']
        self.line_changes, self.open_calls = [], 0

    @property
    def dtr(self):
        return self._dtr

    @dtr.setter
    def dtr(self, value):
        self.line_changes.append(('dtr', value, self.is_open))
        self._dtr = value

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        self.line_changes.append(('rts', value, self.is_open))
        self._rts = value

    def open(self):
        if self.is_open or self.dtr is not False or self.rts is not False:
            raise AssertionError('reset-capable or repeated fake open')
        role = next(r for r in ROLES if self.fixture.world.routes[r] == self.port)
        self.node = self.world.nodes[role]
        self.node.generation = self.fixture.instance.passive.generation
        self.route = self.port
        self.open_calls += 1
        self.is_open = True
        self.close_failure = self.world.close_role == role
        self.close_once_failure = self.world.close_once_role == role
        if self.world.stale_generation == (role, self.node.generation):
            self.received.extend(b'OTCAND1 READY 1\n')
        self.world.events.append(('open', role, self.node.generation))

    def write(self, raw):
        fault = self.world.faults.get((self.node.role, self.node.generation,
                                      raw.decode().split(' ')[1].strip()))
        if fault == 'timeout':
            # No response: the real endpoint must expire without retransmission.
            self.world.events.append(('wire', self.node.role, self.node.generation, 'HELLO'))
            self.writes.append(raw)
            self.world.clock.value = self.fixture.deadline
            return len(raw)
        result = super().write(raw)
        if fault == 'rollback':
            self.world.clock.value -= 1
        return result

    def close(self):
        if self.node is None:
            self.close_calls += 1
            self.is_open = False
        else:
            super().close()


class FakeRuntime:
    """Runtime seam only. No ambient serial import or actual port enumeration."""
    def __init__(self, fixture):
        self.fixture, self.manifest_sha256 = fixture, 'c' * 64
        self.calls, self.verify_result = [], {'pinned': True}
        self.route_fault, self.ports_fault, self.reuse = None, None, None

    def verify(self, deadline):
        self.calls.append(('verify', deadline))
        if self.verify_result == 'raise':
            raise RuntimeError('PRIVATE manifest detail')
        return self.verify_result

    def route(self, value, deadline):
        self.calls.append(('route', deadline))
        return self.route_fault or self.fixture.world.routes[next(
            role for role in ROLES if IDENTITIES[role] == value)]

    def serial_api(self, deadline):
        self.calls.append(('serial_api', deadline))
        return self.serial_factory, self.comports

    def serial_factory(self, **kwargs):
        self.calls.append(('serial_construct', None))
        if self.reuse is not None:
            return self.reuse
        handle = SerialHandle(self.fixture, kwargs)
        self.fixture.world.handles.append(handle)
        return handle

    def comports(self):
        self.calls.append(('comports', None))
        ports = [SimpleNamespace(device=self.fixture.world.routes[role], vid=0x303a,
                                 pid=0x1001, serial_number=IDENTITIES[role]) for role in ROLES]
        if self.ports_fault == 'duplicate':
            ports.append(SimpleNamespace(**vars(ports[0])))
        elif self.ports_fault == 'wrong_pid':
            ports[0].pid = 0x9999
        elif self.ports_fault == 'swapped':
            ports[0].serial_number, ports[1].serial_number = ports[1].serial_number, ports[0].serial_number
        return ports


class ObservedOperator(operator.CandidateOperator):
    def run(self, case, group, deadline):
        self.supplied_run = deadline
        return super().run(case, group, deadline)

    def confirm_original(self, deadline):
        self.supplied_restore = deadline
        return super().confirm_original(deadline)


class Fixture:
    def __init__(self, case='first', *, utc_step=0):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / '.private'
        self.root.mkdir()
        self.world = target_fixture.World()
        self.world.routes = {'A': 'COM1', 'B': 'COM2'}
        self.utc = UTC(utc_step)
        for node in self.world.nodes.values():
            original = node.public
            def public(this, kind, index=0, original=original):
                if kind == 'CANDIDATE':
                    value = candidate(this.role).values[0]
                    this.values[(kind, index)] = value
                    return value
                return original(kind, index)
            node.public = MethodType(public, node)
        self.runtime, self.lease = FakeRuntime(self), rom.HardwareLease(self.root, monotonic=self.world.clock)
        self.view = PrivateView(self.world)
        self.request, self.images = request_for(case)
        self.grant_raw, self.grant_sha = grant_for(self.request)
        self.record_fault = None
        self.instance = ObservedOperator(self.runtime, self.lease, IDENTITIES, KEY,
            rom.opaque_identity, self.view, self.restart, record=self.record,
            monotonic=self.world.clock, wait=self.wait)
        self.deadline = 100.0

    def wait(self, seconds):
        self.world.clock.value += seconds

    def record(self, event):
        self.world.events.append(('record', dict(event)))
        return not (event.get('schema') == 'OT-CANDIDATE-ACK-1'
                    and event.get('kind') == self.record_fault)

    def restart(self, deadline):
        if not self.instance.assert_idle() or not self.lease.assert_idle():
            raise AssertionError('fake restart while passive owner unresolved')
        self.world.events.append(('restart', deadline))
        self.world.restart_count += 1
        for index, (role, node) in enumerate(self.world.nodes.items()):
            node.reset_volatile()
            self.world.routes[role] = 'COM' + str(3 + index)
        return True

    def activate(self, deadline=100, *, booted=True):
        self.deadline = deadline
        self.instance.activate(self.request, self.grant_raw, self.grant_sha,
                               deadline, utc=self.utc)
        if booted:
            for row in self.request['roles'].values():
                self.lease.enter_rom(row['device_binding'], deadline)
                self.lease.mark_boot(row['device_binding'])

    def run(self, deadline=None):
        return self.instance.run(self.request['case'], 17,
                                 self.deadline if deadline is None else deadline)

    def cleanup(self):
        self.instance.close()
        # Harness teardown only: release its OS lock even for intentionally held
        # fake custody. This is never asserted as verified device/ROM closure.
        if self.lease._file is not None:
            self.lease._file.close()
            self.lease._file = None
        self.temp.cleanup()


class LeasedFlashBackend(flash_fixture.Backend):
    """Fake flash with the actual passive/ROM lease, not duplicated lease logic."""
    def __init__(self, device, log, clock, lease):
        super().__init__(device, log, clock)
        self.lease = lease

    def claim(self, binding, deadline):
        self.lease.enter_rom(binding, deadline)
        return super().claim(binding, deadline)

    def boot_candidate(self, deadline):
        result = super().boot_candidate(deadline)
        self.lease.mark_boot(self.device.binding)
        return result

    def hold_rom(self, deadline):
        self.lease.enter_rom(self.device.binding, deadline)
        return super().hold_rom(deadline)

    def reset_original(self, deadline):
        result = super().reset_original(deadline)
        self.lease.mark_boot(self.device.binding, original=True)
        return result


class OperatorTests(unittest.TestCase):
    def fixture(self, *args, **kwargs):
        f = Fixture(*args, **kwargs)
        self.addCleanup(f.cleanup)
        return f

    def test_import_and_construction_are_inert(self):
        f = self.fixture()
        real_import = builtins.__import__
        def no_sdk(name, *args, **kwargs):
            if name == 'serial' or name.startswith(('serial.', 'esptool')):
                raise AssertionError('ambient SDK import')
            return real_import(name, *args, **kwargs)
        with patch('builtins.__import__', no_sdk):
            importlib.reload(operator)
            runtime = rom.Runtime(f.root / 'absent.json', 'c' * 64, f.root,
                subprocess_run=lambda *a, **k: self.fail('runner invoked at construction'))
            operator.CandidateOperator(f.runtime, f.lease, IDENTITIES, KEY,
                rom.opaque_identity, f.view, f.restart, record=f.record,
                monotonic=f.world.clock, wait=f.wait)
        self.assertTrue(runtime.assert_idle())
        self.assertEqual(f.runtime.calls, [])
        self.assertEqual(f.world.events, [])
        self.assertEqual(f.world.clock.value, 1.0)
        self.assertFalse((f.root / 'enrollment-candidate-hardware.lock').exists())

    def test_real_runtime_refuses_ambient_sdk_caller_before_import(self):
        f = self.fixture()
        private = f.root / '.private'
        private.mkdir()
        runtime = rom.Runtime(f.root / 'absent.json', 'c' * 64, private,
            manifest_verifier=lambda *args: {'worktree': str(f.root),
                'root': str(f.root / 'nonexistent-capsule'), 'files': {}},
            subprocess_run=lambda *a, **k: self.fail('isolated runner invoked'),
            monotonic=f.world.clock)
        real_import = importlib.import_module
        def no_serial(name, *args, **kwargs):
            if name == 'serial' or name.startswith('serial.'):
                self.fail('ambient SDK imported before caller admission')
            return real_import(name, *args, **kwargs)
        with patch('importlib.import_module', no_serial), self.assertRaisesRegex(
                rom.AdapterError, '^isolated_caller_required$'):
            runtime.serial_api(20)
        self.assertTrue(runtime.assert_idle())

    def test_source_reviewed_raw_identity_rows_lifetime_and_redaction(self):
        ref = references()
        saved = list(ref._keys.values())
        self.assertEqual(ref.local(), (('A', EXPECTED_A), ('B', EXPECTED_B)))
        with self.assertRaises(operator.OperatorError):
            ref.peer('A')
        ref.confirmed('fingerprint_local', ROLES)
        self.assertEqual(ref.peer('A'), (('B', EXPECTED_B),))
        ref.confirmed('fingerprint', ('A',))
        self.assertEqual(ref.peer('B'), (('A', EXPECTED_A),))
        ref.confirmed('fingerprint', ('B',))
        self.assertEqual(ref._keys, {})
        self.assertTrue(all(value == bytes(32) for value in saved))
        p = operator.PrivatePrompt('fingerprint', ('A',), '1' * 32, 10,
                                   ('inspect',), 17, (('B', EXPECTED_B),))
        self.assertNotIn(EXPECTED_B[0], repr(p))
        ref.begin(2)
        with self.assertRaises(operator.OperatorError):
            ref.capture('A', 1, candidate('A'))
        for bad in (wire.Reply('CANDIDATE', ('00' * 40,)),
                    wire.Reply('CANDIDATE', ((struct.pack('<II', 2, 1) + RAW_IDS['A']).hex(),)),
                    wire.Reply('CANDIDATE', (candidate('A').values[0].upper(),))):
            with self.subTest(reply_kind=bad.kind), self.assertRaises(operator.OperatorError):
                ref.capture('A', 2, bad)

    def test_checkpoint_requires_exact_typed_bound_ack(self):
        for fault in ('raw_true', 'wrong_token', 'wrong_roles', 'wrong_kind', 'wrong_schema'):
            with self.subTest(fault=fault):
                clock, ref, view = target_fixture.Clock(), references(), PrivateView()
                view.ack_fault = fault
                ui = operator.CheckpointUI(ref, view, group=17, monotonic=clock,
                                           wait=lambda seconds: None)
                with self.assertRaisesRegex(operator.OperatorError, '^human_confirmation_missing$'):
                    ui(point(), lambda: (wire.Reply('OK', ('CONFIRM',)),))
                self.assertEqual(view.closes, 1)
                self.assertIsNone(view.active)
                self.assertEqual(ref._keys, {})

    def test_sample_ok_without_ack_expires_without_advancement(self):
        f = self.fixture()
        f.view.no_ack = 'fingerprint'
        f.activate(1.15)
        result = f.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertGreater(f.view.polls, 1)
        self.assertNotIn('FINISH', f.world.commands())
        self.assertNotIn('COMMIT', f.world.commands())
        self.assertFalse(any(row[0] == 'record' and row[1].get('schema') == 'OT-CANDIDATE-ACK-1'
                             and row[1].get('kind') == 'fingerprint' for row in f.world.events))
        self.assertTrue(f.instance.assert_idle())

    def test_partial_private_show_and_failed_clear_are_terminal(self):
        for stage, fault in (('show', 'false'), ('show', 'raise'), ('close', 'false'), ('close', 'raise')):
            with self.subTest(stage=stage, fault=fault):
                ref, view, clock = references(), PrivateView(), target_fixture.Clock()
                setattr(view, stage + '_fault', fault)
                ui = operator.CheckpointUI(ref, view, group=17, monotonic=clock)
                with self.assertRaises(operator.OperatorError) as caught:
                    ui(point(), lambda: ())
                self.assertNotIn('PRIVATE', str(caught.exception))
                self.assertEqual(view.closes, 1)
                self.assertIsNone(view.active)
                self.assertEqual(ref._keys, {})
                with self.assertRaises(operator.OperatorError):
                    ui(point(), lambda: self.fail('terminal UI sampled again'))
                self.assertEqual(view.closes, 1)

    def test_actual_first_flow_durable_ack_and_private_owner_confirmation(self):
        f = self.fixture()
        seen_refs = []
        def inspect(prompt):
            if prompt.kind == 'fingerprint_local':
                self.assertEqual(prompt.references, (('A', EXPECTED_A), ('B', EXPECTED_B)))
            elif prompt.kind == 'fingerprint':
                other = 'B' if prompt.roles == ('A',) else 'A'
                self.assertEqual(prompt.references, ((other, EXPECTED_B if other == 'B' else EXPECTED_A),))
            seen_refs.append(bool(prompt.references))
            self.assertNotIn(EXPECTED_A[0], repr(prompt))
        f.view.on_show = inspect
        f.activate()
        result = f.run()
        self.assertEqual((result.outcome, result.status_transfers, result.checkpoints), ('passed', 8, 4))
        for index, row in enumerate(f.world.events):
            if row[0] == 'wire' and row[3] in ('FINISH', 'NEXTCONTROL'):
                expected_kind = 'fingerprint' if row[3] == 'FINISH' else 'transcript'
                self.assertTrue(any(prev[0] == 'record' and prev[1].get('schema') == 'OT-CANDIDATE-ACK-1'
                    and prev[1].get('kind') == expected_kind for prev in f.world.events[:index]))
        self.assertEqual(seen_refs, [True, True, True, False])
        self.assertNotIn(EXPECTED_A[0], repr(f.world.events))
        self.assertNotIn(EXPECTED_B[0], repr(f.world.events))
        wire_count = len(f.world.commands())
        f.utc.value = 201
        ack = f.instance.confirm_original(220)
        self.assertIs(type(ack), core.CheckpointAck)
        self.assertEqual(ack.kind, 'usual_screen')
        self.assertEqual(len(f.world.commands()), wire_count)
        self.assertEqual(f.view.shows[-1][0], 'usual_screen')

    def test_ack_record_failure_and_absent_target_gesture_do_not_pass(self):
        for fault in ('record', 'gesture'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'record':
                    f.record_fault = 'fingerprint'
                else:
                    f.view.no_gesture = 'fingerprint'
                f.activate()
                result = f.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertNotIn('COMMIT', f.world.commands())
                if fault == 'record':
                    self.assertNotIn('FINISH', f.world.commands())
                    self.assertEqual(result.first_failure[1], 'record_failed')
                else:
                    self.assertEqual(f.world.commands().count('FINISH'), 1)
                self.assertTrue(f.instance.assert_idle())

    def test_actual_lease_exact_handle_and_no_reset_open_configuration(self):
        f = self.fixture()
        f.activate()
        endpoint = f.instance.passive('A', 1, 90)
        h = endpoint.handle
        self.assertEqual(h.kwargs, {'port': None, 'baudrate': 115200, 'timeout': 0,
                                   'write_timeout': 0, 'exclusive': True})
        self.assertEqual(h.line_changes, [('dtr', False, False), ('rts', False, False)])
        self.assertFalse(f.lease.guard_passive(endpoint.token, object()))
        with self.assertRaises(rom.AdapterError):
            f.lease.begin_passive(endpoint.token.binding, 2, 90)
        with self.assertRaises(rom.AdapterError):
            f.lease.enter_rom(endpoint.token.binding, 90)
        self.assertEqual(endpoint.exchange('HELLO', 90), wire.Reply('READY', ('1',)))
        self.assertTrue(endpoint.close())
        self.assertEqual(h.close_calls, 1)
        self.assertTrue(endpoint.close())
        self.assertEqual(h.close_calls, 1)
        self.assertTrue(f.lease.assert_idle())

    def test_new_generations_have_fresh_handles_hello_and_no_reference_carry(self):
        f = self.fixture('retained_rekey')
        f.activate()
        result = f.run()
        self.assertEqual((result.outcome, result.generations, result.status_transfers), ('passed', 2, 16))
        self.assertEqual(len({id(h) for h in f.world.handles}), 4)
        for role in ROLES:
            for generation in (1, 2):
                self.assertEqual(f.world.commands(role, generation)[0], 'HELLO')
        self.assertEqual([p[0] for p in f.view.shows],
                         ['fingerprint_local', 'fingerprint', 'fingerprint', 'transcript', 'transcript'])
        self.assertEqual(f.instance.references.generation, 2)
        self.assertEqual(f.instance.references._keys, {})
        f.runtime.reuse = f.world.handles[0]
        old = f.runtime.reuse
        with self.assertRaises(operator.OperatorError):
            f.instance.passive('A', 3, 90)
        self.assertEqual((old.open_calls, old.close_calls), (1, 1))

    def test_stale_ready_and_partial_timeout_late_rollback_never_retry(self):
        for fault in ('stale', 'partial', 'timeout', 'late', 'rollback', 'route_swap'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault == 'stale':
                    f.world.stale_generation = ('A', 1)
                else:
                    f.world.faults[('A', 1, 'HELLO')] = fault
                f.activate(3)
                result = f.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(f.world.commands().count('HELLO'), 0 if fault == 'stale' else 1)
                self.assertNotIn('BEGIN', f.world.commands())
                self.assertEqual(f.world.handles[0].close_calls, 1)
                self.assertTrue(f.instance.assert_idle())

    def test_runtime_admission_and_route_guards_refuse_before_wire_io(self):
        for fault in ('false_manifest', 'none_manifest', 'manifest_throw', 'drift',
                      'bad_route', 'duplicate', 'wrong_pid', 'swapped', 'zero_generation'):
            with self.subTest(fault=fault):
                f = self.fixture()
                if fault in ('false_manifest', 'none_manifest', 'manifest_throw'):
                    f.runtime.verify_result = {'false_manifest': False, 'none_manifest': None,
                                               'manifest_throw': 'raise'}[fault]
                    with self.assertRaises(operator.OperatorError):
                        f.activate()
                    self.assertEqual(f.world.handles, [])
                    continue
                f.activate()
                if fault == 'drift':
                    f.runtime.manifest_sha256 = 'd' * 64
                elif fault == 'bad_route':
                    f.runtime.route_fault = 'COM0'
                elif fault != 'zero_generation':
                    f.runtime.ports_fault = fault
                with self.assertRaises(operator.OperatorError):
                    f.instance.passive('A', 0 if fault == 'zero_generation' else 1, 90)
                self.assertEqual(f.world.commands(), [])
                self.assertTrue(all(h.is_open is False and h.close_calls == 1 for h in f.world.handles))

    def test_failed_old_close_is_sticky_and_other_handle_closes(self):
        for case in ('run_close', 'direct_close', 'direct_release'):
            with self.subTest(case=case):
                f = self.fixture('retained_rekey')
                if case != 'direct_release':
                    f.world.close_once_role = 'A'
                f.activate()
                if case == 'run_close':
                    self.assertEqual(f.run().outcome, 'failed')
                else:
                    f.instance.passive('A', 1, 90)
                    f.instance.passive('B', 1, 90)
                    original_release = f.lease.release_passive
                    def release(token, handle):
                        if case == 'direct_release' and token.binding == f.request['roles']['A']['device_binding']:
                            return False
                        return original_release(token, handle)
                    with patch.object(f.lease, 'release_passive', release):
                        self.assertFalse(f.instance.passive.close())
                self.assertEqual(len(f.world.handles), 2)
                self.assertEqual([h.close_calls for h in f.world.handles], [1, 1])
                self.assertFalse(f.instance.assert_idle())
                self.assertFalse(f.instance.close())
                self.assertEqual([h.close_calls for h in f.world.handles], [1, 1])
                self.assertEqual(f.world.restart_count, 0)
                with self.assertRaises((operator.OperatorError, rom.AdapterError)):
                    f.instance.restart(90)
                with self.assertRaises(rom.AdapterError):
                    f.lease.enter_rom(f.request['roles']['A']['device_binding'], 90)

    def test_supplied_run_deadlines_clamp_and_expiry_never_open(self):
        for supplied in (80, 100.000001):
            with self.subTest(supplied=supplied):
                f = self.fixture()
                f.activate(100)
                calls_before_run = len(f.runtime.calls)
                result = f.run(supplied)
                self.assertEqual(result.outcome, 'passed')
                ceiling = min(supplied, f.instance.passive.deadline)
                self.assertTrue(all(d <= ceiling for kind, d in f.runtime.calls[calls_before_run:]
                                    if d is not None))
                self.assertTrue(all(p[3] <= ceiling for p in f.view.shows))
        f = self.fixture()
        f.activate()
        f.world.clock.value = f.instance.passive.deadline
        with self.assertRaises((operator.OperatorError, core.ControllerError)):
            f.run(200)
        self.assertEqual(f.world.handles, [])
        self.assertEqual(f.view.shows, [])

    def test_custody_operator_separate_sampling_keeps_original_ceilings(self):
        f = self.fixture(utc_step=.0000001)
        log = []
        devices = {r: flash_fixture.Device(r, f.images) for r in ROLES}
        for role, device in devices.items():
            device.binding = rom.opaque_identity(KEY, IDENTITIES[role])
        f.request, f.images = request_for(devices=devices)
        f.grant_raw, f.grant_sha = grant_for(f.request)
        backends = {r: LeasedFlashBackend(d, log, f.world.clock, f.lease)
                    for r, d in devices.items()}
        f.activate(200, booted=False)
        initial_execute, initial_restore = f.instance.passive.deadline, f.instance.passive.budget.restore
        def inspected(prompt):
            if prompt.kind == 'usual_screen':
                self.assertTrue(all(b.assert_idle() for b in backends.values()))
                self.assertTrue(all(d.flash == d.original for d in devices.values()))
                self.assertTrue(f.instance.assert_idle())
                self.assertLessEqual(prompt.deadline, initial_restore)
        f.view.on_show = inspected
        result = custody.execute(f.root, f.request, f.grant_raw, f.grant_sha,
            f.images, backends, f.instance, utc=f.utc, monotonic=f.world.clock)
        self.assertEqual(result.observation, 'passed', result.first_failure)
        self.assertIsNone(result.first_failure)
        self.assertTrue(result.custody_released)
        self.assertTrue(result.owner_confirmed)
        self.assertGreater(f.instance.supplied_run, initial_execute)
        self.assertGreater(f.instance.supplied_restore, initial_restore)
        self.assertEqual(f.instance.passive.deadline, initial_execute)
        self.assertEqual(f.instance.passive.budget.restore, initial_restore)
        self.assertTrue(all(p[3] <= initial_execute for p in f.view.shows[:-1]))
        self.assertTrue(f.lease.close())

    def test_usual_screen_expired_or_late_clear_has_no_ack_or_usb(self):
        for fault in ('expired', 'late_clear'):
            with self.subTest(fault=fault):
                f = self.fixture()
                f.activate()
                self.assertEqual(f.run().outcome, 'passed')
                count = len(f.world.commands())
                restore = f.instance.passive.budget.restore
                if fault == 'expired':
                    f.world.clock.value = restore
                else:
                    f.view.close_advance = restore
                with self.assertRaises((operator.OperatorError, core.ControllerError)):
                    f.instance.confirm_original(restore + .000001)
                self.assertEqual(len(f.world.commands()), count)
                self.assertFalse(any(row[0] == 'record' and row[1].get('schema') == 'OT-CANDIDATE-ACK-1'
                    and row[1].get('kind') == 'usual_screen' for row in f.world.events))


if __name__ == '__main__':
    unittest.main(verbosity=2)
