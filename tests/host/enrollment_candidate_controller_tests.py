"""Computer-only controller tests using the unchanged OTCAND1 Endpoint.

The stateful fake devices check public-value forwarding and lifecycle ownership.
Their invented public records are not cryptographic or physical-device evidence.
No serial module, device enumeration, SDK, or network operation is used.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import enrollment_candidate_usb_client as wire
import enrollment_candidate_controller as controller


class Clock:
    def __init__(self):
        self.value = 1.0
        self.step = .000001

    def __call__(self):
        self.value += self.step
        return self.value


class Node:
    """Independent simulated device; sampling never supplies human authority."""
    def __init__(self, world, role):
        self.world, self.role = world, role
        self.committed = False
        self.reset_intent = False
        self.generation = 0
        self.reset_volatile()

    def reset_volatile(self):
        self.failed = False
        self.hello = False
        self.mode = None
        self.session = False
        self.local_shown = self.peer_shown = self.reviewed = self.confirmed = False
        self.local_observed = False
        self.visible_page = None
        self.peer_received = self.possession_accepted = self.mark_received = False
        self.signature = self.bound = self.ready = self.recovered = False
        self.archive_received = False
        self.frame_tx = self.frame_rx = self.control_tx = self.control_rx = 0
        self.values = {}

    @property
    def peer(self):
        return self.world.nodes['B' if self.role == 'A' else 'A']

    def public(self, kind, index=0):
        # Unique byte patterns across roles, records and fresh generations.
        prefix = bytes((1 if self.role == 'A' else 2, self.generation,
                        list(wire.VALUE_BYTES).index(kind) + 1, index))
        value = (prefix + bytes(wire.VALUE_BYTES[kind] - len(prefix))).hex()
        self.values[(kind, index)] = value
        return value

    def answer(self, kind, value):
        return ('OTCAND1 ' + kind + ' ' + value + '\n').encode('ascii')

    def ok(self, verb):
        return self.answer('OK', verb)

    def require(self, predicate):
        if not predicate:
            raise ValueError('simulated_target_refusal')

    def execute(self, command):
        parts = command.split(' ')
        verb, args = parts[0], parts[1:]
        try:
            if verb == 'RESETSTATUS':
                return self.answer('RESETSTATUS', '3 1' if self.reset_intent else '0 0')
            if verb == 'BOOTSTATUS':
                return self.answer('BOOTSTATUS', '0')
            self.require(not self.failed and not self.reset_intent)
            if verb == 'HELLO':
                self.hello = True
                return self.answer('READY', '1')
            self.require(self.hello)
            if verb == 'BEGIN':
                self.require(not self.session and int(args[1]) == (1 if self.role == 'A' else 2))
                self.mode = int(args[0])
                self.require(self.mode == 0 or self.committed or
                             (self.mode == 2 and self.peer.committed))
                self.session = True
                return self.ok(verb)
            if verb == 'CLOSE':
                self.session = False
                return self.answer('CLOSED', '1')
            self.require(self.session)
            if verb == 'CANCEL':
                self.session = False
                return self.answer('CLOSED', '1')
            if verb == 'REVOKE':
                self.require(self.committed)
                self.committed = False
                self.ready = False
                return self.answer('REVOKED', '1')
            if verb == 'EXPORT':
                self.require(self.mode == 0)
                return self.answer('CANDIDATE', self.public('CANDIDATE'))
            if verb == 'PEER':
                self.require(args[0] == self.peer.values.get(('CANDIDATE', 0)))
                self.peer_received = True
                return self.ok(verb)
            if verb in ('SHOWLOCAL', 'SHOWPEER'):
                self.require(self.peer_received)
                if verb == 'SHOWPEER':
                    self.require(self.local_shown and self.local_observed)
                setattr(self, 'local_shown' if verb == 'SHOWLOCAL' else 'peer_shown', True)
                self.visible_page = 'local' if verb == 'SHOWLOCAL' else 'peer'
                return self.ok(verb)
            if verb == 'POLL':
                return self.ok(verb)
            if verb == 'FINISH':
                self.require(self.local_shown and self.local_observed and self.peer_shown
                             and self.reviewed and self.visible_page == 'peer')
                return self.answer('OFFER', self.public('OFFER'))
            if verb == 'ARCHIVE':
                self.require(self.mode == 2)
                return self.answer('ARCHIVE', self.public('ARCHIVE') if self.committed else 'NONE')
            if verb == 'ACCEPTARCHIVE':
                self.require(self.mode == 2 and args[0] == self.peer.values.get(('ARCHIVE', 0)))
                self.archive_received = True
                return self.ok(verb)
            if verb == 'RECOVERBEGIN':
                self.require(self.mode == 2 and (self.committed or self.archive_received))
                return self.answer('RECOVERCHALLENGE', self.public('RECOVERCHALLENGE'))
            if verb == 'RECOVERSIGN':
                self.require(args[0] == self.peer.values.get(('RECOVERCHALLENGE', 0)))
                return self.answer('RECOVERRESPONSE', self.public('RECOVERRESPONSE'))
            if verb == 'RECOVERFINISH':
                self.require(args[0] == self.peer.values.get(('RECOVERRESPONSE', 0)))
                self.recovered = True
                self.committed = True
                return self.ok(verb)
            if verb == 'RETAINBEGIN':
                self.require(self.mode == 1 or (self.mode == 2 and self.recovered))
                return self.answer('RETAINCHALLENGE', self.public('RETAINCHALLENGE'))
            if verb == 'RETAINSIGN':
                self.require(args[0] == self.peer.values.get(('RETAINCHALLENGE', 0)))
                return self.answer('RETAINRESPONSE', self.public('RETAINRESPONSE'))
            if verb == 'RETAINFINISH':
                self.require(args[0] == self.peer.values.get(('RETAINRESPONSE', 0)))
                self.reviewed = True
                return self.answer('OFFER', self.public('OFFER'))
            if verb == 'POSSESS':
                self.require(args[0] == self.peer.values.get(('OFFER', 0)))
                return self.answer('POSSESSION', self.public('POSSESSION'))
            if verb == 'ACCEPTPOSSESS':
                self.require(args[0] == self.peer.values.get(('POSSESSION', 0)))
                self.possession_accepted = True
                return self.ok(verb)
            if verb == 'MARK':
                self.require(self.possession_accepted)
                return self.answer('MARK', self.public('MARK'))
            if verb == 'PEERMARK':
                self.require(args[0] == self.peer.values.get(('MARK', 0)))
                self.mark_received = True
                return self.ok(verb)
            if verb == 'INVITE':
                self.require(self.role == 'A' and self.mark_received and self.peer.mark_received)
                return self.answer('INVITATION', self.public('INVITATION'))
            if verb == 'SIGN':
                self.require(args[0] == self.world.nodes['A'].values.get(('INVITATION', 0)))
                self.signature = True
                return self.answer('SIGNATURE', self.public('SIGNATURE'))
            if verb == 'BIND':
                self.require(self.signature and args[0] == self.peer.values.get(('SIGNATURE', 0)))
                self.bound = True
                return self.ok(verb)
            if verb == 'NEXTFRAME':
                self.require(self.bound and self.frame_tx < (2 if self.role == 'A' else 1))
                self.frame_tx += 1
                return self.answer('FRAME', self.public('FRAME', self.frame_tx))
            if verb == 'FRAME':
                self.require(self.bound and args[0] == self.peer.values.get(('FRAME', self.frame_rx + 1)))
                self.frame_rx += 1
                return self.ok(verb)
            if verb == 'CONFIRM':
                self.require(self.frame_rx == (1 if self.role == 'A' else 2))
                return self.ok(verb)
            if verb == 'NEXTCONTROL':
                self.require(self.confirmed and self.control_tx < 2)
                self.control_tx += 1
                return self.answer('CONTROL', self.public('CONTROL', self.control_tx))
            if verb == 'CONTROL':
                self.require(self.confirmed and args[0] == self.peer.values.get(('CONTROL', self.control_rx + 1)))
                self.control_rx += 1
                return self.ok(verb)
            if verb == 'COMMIT':
                self.require(self.control_tx == self.control_rx == 2)
                self.committed = True
                return self.ok(verb)
            if verb == 'READY':
                self.require(self.committed and self.peer.committed)
                self.ready = True
                return self.ok(verb)
            if verb == 'SENDSTATUS':
                self.require(self.ready and self.committed)
                status = int(args[0])
                value = self.public('STATUS', status)
                self.world.records[value] = (self.role, self.generation, status)
                return self.answer('STATUS', value)
            if verb == 'STATUS':
                self.require(self.ready and self.committed)
                origin, generation, status = self.world.records.get(args[0], (None, None, None))
                self.require(origin == self.peer.role and
                             (generation == self.generation or self.world.accept_old_record))
                self.world.deliveries.append((origin, self.role, generation, status))
                return self.answer('VALUE', str(self.world.status_override or status))
            raise ValueError('unsupported_simulated_command')
        except (ValueError, KeyError, IndexError):
            self.failed = True
            return b'OTCAND1 REFUSED\n'


class Handle:
    def __init__(self, world, node, route):
        self.world, self.node, self.route = world, node, route
        self.is_open = True
        self.received = bytearray()
        self.timeout = self.write_timeout = None
        self.writes = []
        self.close_failure = False
        self.close_once_failure = False
        self.close_calls = 0
        self.read_size = 7

    @property
    def in_waiting(self):
        return len(self.received)

    def write(self, raw):
        command = raw.decode('ascii').removeprefix('OTCAND1 ').removesuffix('\n')
        verb = command.split(' ')[0]
        point = (self.node.role, self.node.generation, verb)
        self.world.events.append(('wire', *point))
        self.writes.append(raw)
        fault = self.world.faults.get(point)
        if fault == 'exception':
            raise OSError('private test transport details')
        answer = b'OTCAND1 REFUSED\n' if fault == 'refused' else self.node.execute(command)
        if fault == 'bad_response':
            answer = b'OTCAND1 READY 1\n'
        if fault == 'late':
            self.world.clock.value += 1000
        if fault == 'route_swap':
            self.world.routes[self.node.role] = 'changed-route'
        self.received.extend(answer)
        return len(raw) - 1 if fault == 'partial' else len(raw)

    def read(self, count):
        size = min(count, self.read_size)
        raw = bytes(self.received[:size])
        del self.received[:size]
        return raw

    def close(self):
        self.world.events.append(('passive_close', self.node.role, self.node.generation))
        self.close_calls += 1
        if self.close_failure or (self.close_once_failure and self.close_calls == 1):
            raise OSError('private test close details')
        self.is_open = False


class World:
    def __init__(self):
        self.clock = Clock()
        self.events, self.deliveries, self.handles = [], [], []
        self.records, self.faults, self.routes = {}, {}, {'A': 'route-A', 'B': 'route-B'}
        self.nodes = {role: Node(self, role) for role in ('A', 'B')}
        self.stale_generation = None
        self.receipt_fault = None
        self.sample_only = False
        self.sample_only_kind = None
        self.no_gesture_kind = None
        self.checkpoint_delay = {}
        self.accept_old_record = False
        self.status_override = None
        self.deadlines = []
        self.persist = True
        self.restart_count = 0
        self.token_count = 0
        self.close_role = None
        self.close_once_role = None
        self.startup_diagnostics = False
        self.startup_bytes = b'OTBOOT1 0 3 1\n'

    def token(self):
        self.token_count += 1
        return f'{self.token_count:032x}'

    def factory(self, role, generation, deadline):
        node = self.nodes[role]
        node.generation = generation
        handle = Handle(self, node, self.routes[role])
        handle.close_failure = self.close_role == role
        handle.close_once_failure = self.close_once_role == role
        if self.stale_generation == (role, generation):
            handle.received.extend(b'OTCAND1 READY 1\n')
        if self.startup_diagnostics:
            handle.received.extend(self.startup_bytes)
        self.handles.append(handle)
        self.events.append(('open', role, generation, deadline))
        class ObservedEndpoint(wire.Endpoint):
            def exchange(endpoint, command, absolute_deadline):
                self.deadlines.append((role, generation, command.split(' ')[0], absolute_deadline))
                return super().exchange(command, absolute_deadline)
        return ObservedEndpoint(handle, lambda: handle.is_open and self.routes[role] == handle.route,
                                monotonic=self.clock, startup_diagnostics=self.startup_diagnostics)

    def checkpoint(self, checkpoint, sample):
        kind, roles = checkpoint['kind'], tuple(checkpoint['roles'])
        self.events.append(('checkpoint', kind, roles, checkpoint['deadline']))
        if kind == 'reset_gesture' and not self.sample_only:
            for role in roles:
                self.nodes[role].reset_intent = True
        sampled = sample()
        self.events.append(('sample', kind, tuple(reply.kind for reply in sampled)))
        self.clock.value += self.checkpoint_delay.get(kind, 0)
        if self.sample_only or self.sample_only_kind == kind:
            return None
        if kind == 'fingerprint_local':
            for role in roles:
                self.nodes[role].require(self.nodes[role].visible_page == 'local')
                self.nodes[role].local_observed = True
        elif kind == 'fingerprint' and self.no_gesture_kind != kind:
            for role in roles:
                self.nodes[role].require(self.nodes[role].visible_page == 'peer')
                self.nodes[role].reviewed = True
        elif kind == 'transcript' and self.no_gesture_kind != kind:
            for role in roles:
                self.nodes[role].confirmed = True
        receipt = {'kind': kind, 'roles': roles, 'token': checkpoint['token'], 'confirmed': True}
        if self.receipt_fault:
            receipt.update(self.receipt_fault)
        return receipt

    def restart(self, deadline):
        if not all(handle.is_open is False for handle in self.handles):
            raise ValueError('fake_restart_with_live_handle')
        self.events.append(('restart', deadline))
        self.restart_count += 1
        for role, node in self.nodes.items():
            node.reset_volatile()
            self.routes[role] = 'route-' + role + '-' + str(self.restart_count)
        return True

    def record(self, event):
        self.events.append(('record', event))
        return self.persist

    def run(self, case='first', *, deadline=1000.0, group=17):
        self.startup_diagnostics = case == 'startup_A'
        instance = controller.Controller(self.factory, self.checkpoint, self.restart,
            monotonic=self.clock, token_factory=self.token, record=self.record)
        return instance.run(case, group, deadline)

    def commands(self, role=None, generation=None):
        return [row[3] for row in self.events if row[0] == 'wire' and
                (role is None or row[1] == role) and
                (generation is None or row[2] == generation)]


class ControllerTests(unittest.TestCase):
    def assert_passed(self, result):
        self.assertEqual(result.outcome, 'passed', result.first_failure)
        self.assertIsNone(result.first_failure)
        self.assertTrue(result.handles_closed)

    def test_construction_is_inert(self):
        world = World()
        controller.Controller(world.factory, world.checkpoint, world.restart,
            monotonic=world.clock, token_factory=world.token, record=world.record)
        self.assertEqual(world.events, [])
        self.assertEqual(world.clock.value, 1.0)

    def test_first_flow_uses_actual_parser_fragmented_values_and_eight_deliveries(self):
        world = World()
        result = world.run()
        self.assert_passed(result)
        self.assertEqual(result.status_transfers, 8)
        self.assertEqual(world.deliveries,
            [(origin, receiver, 1, status) for status in range(1, 5)
             for origin, receiver in (('A', 'B'), ('B', 'A'))])
        self.assertEqual(result.generations, 1)
        for role in ('A', 'B'):
            commands = world.commands(role)
            self.assertEqual(commands[0], 'HELLO')
            self.assertLess(commands.index('FINISH'), commands.index('POSSESS'))
            self.assertLess(commands.index('READY'), commands.index('SENDSTATUS'))
            self.assertEqual(commands[-1], 'CLOSE')
        self.assertEqual([row[1] for row in world.events if row[0] == 'checkpoint'],
                         ['fingerprint_local', 'fingerprint', 'fingerprint', 'transcript'])

    def test_each_operation_lease_requires_fresh_hello_and_bootstatus_before_begin(self):
        for case in ('first', 'retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit'):
            with self.subTest(case=case):
                world = World(); result = world.run(case)
                self.assert_passed(result)
                for generation in range(1, result.generations + 1):
                    for role in controller.ROLES:
                        self.assertEqual(world.commands(role, generation)[:3], ['HELLO', 'BOOTSTATUS', 'BEGIN'])

    def test_operation_bootstatus_contradiction_or_failure_stops_before_begin(self):
        for fault in ('nonzero', 'refused', 'partial', 'late'):
            with self.subTest(fault=fault):
                world = World()
                if fault == 'nonzero':
                    execute = world.nodes['B'].execute
                    world.nodes['B'].execute = lambda command: (b'OTCAND1 BOOTSTATUS 1\n'
                        if command == 'BOOTSTATUS' else execute(command))
                else:
                    world.faults[('B', 1, 'BOOTSTATUS')] = fault
                result = world.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(result.first_failure[0], 'bootstatus_B')
                self.assertNotIn('BEGIN', world.commands())
                self.assertTrue(result.handles_closed)

    def test_sampler_ok_without_typed_receipt_cannot_finish_review(self):
        world = World()
        world.sample_only_kind = 'fingerprint'
        result = world.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertTrue(result.handles_closed)
        self.assertIn('POLL', world.commands())
        self.assertNotIn('FINISH', world.commands())
        self.assertNotIn('COMMIT', world.commands())
        self.assertNotIn('CLOSE', world.commands())

    def test_typed_receipts_reject_wrong_token_kind_roles_and_false_confirmation(self):
        for receipt in ({'token': 'f' * 32}, {'kind': 'transcript'},
                        {'roles': ()}, {'confirmed': False}):
            with self.subTest(receipt=receipt):
                world = World()
                world.receipt_fault = receipt
                result = world.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertNotIn('FINISH', world.commands())
                self.assertNotIn('CLOSE', world.commands())

    def test_wire_fault_preserves_first_point_and_never_retries_or_cleans_via_wire(self):
        for fault in ('partial', 'exception', 'refused', 'bad_response', 'route_swap', 'late'):
            with self.subTest(fault=fault):
                world = World()
                world.faults[('B', 1, 'BEGIN')] = fault
                result = world.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(result.first_failure[0], 'begin_B')
                self.assertEqual(world.commands('B').count('BEGIN'), 1)
                self.assertNotIn('CLOSE', world.commands())
                self.assertNotIn('CANCEL', world.commands())
                self.assertTrue(result.handles_closed)

    def test_queued_stale_ready_is_not_a_hello_response(self):
        world = World()
        world.stale_generation = ('A', 1)
        result = world.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('hello_A', 'unsolicited_response'))
        self.assertEqual(world.commands(), [])

    def test_failed_passive_close_stays_unverified_and_attempts_other_close(self):
        world = World()
        world.close_role = 'A'
        world.faults[('B', 1, 'BEGIN')] = 'refused'
        result = world.run()
        self.assertEqual(result.first_failure, ('begin_B', 'target_refused'))
        self.assertFalse(result.handles_closed)
        self.assertTrue(world.handles[0].is_open)
        self.assertFalse(world.handles[1].is_open)
        self.assertEqual(world.restart_count, 0)

    def test_expired_original_deadline_prevents_wire_and_invalid_inputs_are_inert(self):
        world = World()
        result = world.run(deadline=1.0)
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(world.commands(), [])
        for case, group, deadline in [('unknown', 17, 1000.0), ('first', 0, 1000.0),
                                      ('first', True, 1000.0), ('first', 17, float('nan'))]:
            with self.subTest(case=case, group=group, deadline=deadline):
                world = World()
                result = world.run(case, group=group, deadline=deadline)
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(world.handles, [])

    def test_failed_evidence_write_cannot_be_reported_as_passed(self):
        world = World()
        world.persist = False
        result = world.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertTrue(result.handles_closed)
        self.assertNotIn('COMMIT', world.commands())

    def test_confirm_ok_without_receipt_cannot_send_activation_controls(self):
        world = World()
        world.sample_only_kind = 'transcript'
        result = world.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertGreaterEqual(world.commands().count('CONFIRM'), 4)
        self.assertNotIn('NEXTCONTROL', world.commands())
        self.assertNotIn('COMMIT', world.commands())

    def test_human_receipt_cannot_override_missing_device_gesture(self):
        for kind, first_denied in [('fingerprint', 'FINISH'), ('transcript', 'NEXTCONTROL')]:
            with self.subTest(kind=kind):
                world = World()
                world.no_gesture_kind = kind
                result = world.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(result.first_failure[1], 'target_refused')
                self.assertEqual(world.commands().count(first_denied), 1)
                self.assertNotIn('COMMIT', world.commands())
                self.assertNotIn('CLOSE', world.commands())

    def test_retained_restart_closes_all_old_handles_and_uses_fresh_hello(self):
        world = World()
        result = world.run('retained_rekey')
        self.assert_passed(result)
        self.assertEqual(result.status_transfers, 16)
        self.assertEqual(result.generations, 2)
        self.assertEqual(result.refusals, (('B', 'STATUS'),))
        self.assertEqual(world.restart_count, 1)
        before_restart = next(i for i, row in enumerate(world.events) if row[0] == 'restart')
        closes = [row for row in world.events[:before_restart] if row[0] == 'passive_close']
        self.assertEqual(closes, [('passive_close', 'A', 1), ('passive_close', 'B', 1)])
        for role in ('A', 'B'):
            commands = world.commands(role, 2)
            self.assertEqual(commands[0], 'HELLO')
            self.assertIn('RETAINBEGIN', commands)
            self.assertNotIn('EXPORT', commands)
        self.assertNotIn('CLOSE', world.commands('B', 2))
        self.assertEqual([row[2] for row in world.deliveries], [1] * 8 + [2] * 8)

    def test_restart_stale_ready_refuses_before_any_new_generation_command(self):
        world = World()
        world.stale_generation = ('A', 2)
        result = world.run('retained_rekey')
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('hello_A', 'unsolicited_response'))
        self.assertEqual(world.commands(generation=2), [])
        self.assertEqual(result.status_transfers, 8)
        self.assertTrue(result.handles_closed)

    def test_warm_restart_is_forbidden_when_an_old_handle_does_not_close(self):
        world = World()
        world.close_role = 'A'
        result = world.run('retained_rekey')
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure[1], 'handles_not_closed')
        self.assertEqual(world.restart_count, 0)
        self.assertEqual(world.commands(generation=2), [])
        self.assertFalse(result.handles_closed)

    def test_retained_old_record_must_actually_be_refused(self):
        world = World()
        world.accept_old_record = True
        result = world.run('retained_rekey')
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure[1], 'expected_refusal_missing')
        self.assertEqual(result.refusals, ())
        self.assertNotIn('CLOSE', world.commands('A', 2))
        self.assertNotIn('CLOSE', world.commands('B', 2))

    def test_both_partial_commit_recoveries_need_fresh_proofs_without_second_begin(self):
        for committed in ('A', 'B'):
            with self.subTest(committed=committed):
                world = World()
                result = world.run('recovery_after_' + committed + '_commit')
                self.assert_passed(result)
                self.assertEqual(result.status_transfers, 8)
                self.assertEqual(result.generations, 2)
                for role in ('A', 'B'):
                    first = world.commands(role, 1)
                    self.assertEqual(first.count('COMMIT'), int(role == committed))
                    self.assertNotIn('READY', first)
                    second = world.commands(role, 2)
                    self.assertEqual(second[0], 'HELLO')
                    self.assertEqual(second.count('BEGIN'), 1)
                    self.assertIn('ARCHIVE', second)
                    self.assertIn('RECOVERBEGIN', second)
                    self.assertLess(second.index('RECOVERFINISH'), second.index('RETAINBEGIN'))
                    self.assertLess(second.index('RETAINFINISH'), second.index('POSSESS'))
                self.assertEqual([row[2] for row in world.deliveries], [2] * 8)

    def test_cancel_revoke_and_reset_are_distinct_cases_and_close_healthy_peer(self):
        for case, retired, refusals in [('cancel', 'A', ()),
                                        ('revoke', 'A', (('A', 'SENDSTATUS'),)),
                                        ('reset_preparation', 'A', ())]:
            with self.subTest(case=case):
                world = World()
                result = world.run(case)
                self.assert_passed(result)
                self.assertEqual(result.refusals, refusals)
                self.assertNotIn('CLOSE', world.commands(retired))
                self.assertEqual(world.commands('B')[-1], 'CLOSE')
                self.assertEqual(world.restart_count, 0)
                if case == 'cancel':
                    self.assertEqual(result.status_transfers, 0)
                    self.assertNotIn('COMMIT', world.commands())
                elif case == 'reset_preparation':
                    self.assertTrue(world.nodes['A'].reset_intent)
                    self.assertEqual(world.commands('A')[-1], 'RESETSTATUS')
                    self.assertEqual(world.commands('A').count('RESETSTATUS'), 2)

    def test_status_echo_is_checked_after_parsing_and_is_not_counted_as_delivery(self):
        world = World()
        world.status_override = 8
        result = world.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('status_B', 'status_mismatch'))
        self.assertEqual(result.status_transfers, 0)
        self.assertEqual(len(world.deliveries), 1)

    def test_device_deadline_budgets_are_shared_and_not_refreshed_by_callbacks(self):
        world = World()
        self.assert_passed(world.run('recovery_after_A_commit'))
        for generation in (1, 2):
            rowset = [row for row in world.deadlines if row[1] == generation]
            begins = [row[3] for row in rowset if row[2] == 'BEGIN']
            self.assertEqual(len(set(begins)), 1)
            activation = [row[3] for row in rowset if row[2] in
                          ('MARK', 'INVITE', 'SIGN', 'BIND', 'FRAME', 'CONFIRM',
                           'NEXTCONTROL', 'CONTROL', 'COMMIT', 'READY', 'SENDSTATUS', 'STATUS')]
            self.assertEqual(len(set(activation)), 1)
            self.assertLessEqual(activation[0], begins[0])
        recovery = [row[3] for row in world.deadlines if row[1] == 2 and row[2] in
                    ('ARCHIVE', 'ACCEPTARCHIVE', 'RECOVERBEGIN', 'RECOVERSIGN', 'RECOVERFINISH')]
        self.assertEqual(len(set(recovery)), 1)

    def test_time_spent_at_each_human_checkpoint_uses_its_original_ceiling(self):
        for kind, forbidden in [('fingerprint', 'FINISH'), ('transcript', 'NEXTCONTROL')]:
            with self.subTest(kind=kind):
                world = World()
                world.checkpoint_delay[kind] = 121 if kind == 'fingerprint' else 61
                result = world.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(result.first_failure[1], 'deadline_expired')
                self.assertNotIn(forbidden, world.commands())
                self.assertNotIn('CLOSE', world.commands())

    def test_checkpoint_tokens_cannot_be_reused_and_controller_cannot_rerun(self):
        world = World()
        instance = controller.Controller(world.factory, world.checkpoint, world.restart,
            monotonic=world.clock, token_factory=lambda: 'a' * 32, record=world.record)
        result = instance.run('first', 17, 1000.0)
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure[1], 'checkpoint_token_invalid')
        before = list(world.events)
        with self.assertRaisesRegex(controller.ControllerError, 'controller_used'):
            instance.run('first', 17, 1000.0)
        self.assertEqual(world.events, before)

    def test_retained_sampler_is_inactive_in_later_gates_restart_and_completed_run(self):
        world = World()
        saved, probes = [], []

        def stale_probe(label):
            stage, event_count, clock = instance.stage, len(world.events), world.clock.value
            with self.assertRaisesRegex(controller.ControllerError, '^checkpoint_inactive$'):
                saved[0]()
            self.assertEqual(instance.stage, stage)
            self.assertEqual(len(world.events), event_count)
            self.assertEqual(world.clock.value, clock)
            probes.append(label)

        def checkpoint(point, sample):
            if saved:
                stale_probe((point['kind'], instance.generations))
            else:
                saved.append(sample)
            return world.checkpoint(point, sample)

        def record(event):
            if event.get('schema') == 'OT-CANDIDATE-ACK-1':
                stale_probe(('acknowledgement', instance.generations))
            return world.record(event)

        instance = controller.Controller(world.factory, checkpoint, world.restart,
            monotonic=world.clock, token_factory=world.token, record=record)
        self.assert_passed(instance.run('retained_rekey', 17, 1000.0))
        stale_probe(('completed', instance.generations))
        self.assertIn(('fingerprint', 1), probes)
        self.assertIn(('transcript', 1), probes)
        self.assertIn(('transcript', 2), probes)
        self.assertIn(('completed', 2), probes)

    def test_active_sampler_cannot_follow_a_replaced_lease_or_generation(self):
        world = World()
        probed = False

        def checkpoint(point, sample):
            nonlocal probed
            if not probed and point['kind'] == 'fingerprint':
                probed = True
                original = instance.endpoints['A']
                stage, count, clock = instance.stage, len(world.events), world.clock.value
                instance.endpoints['A'] = instance.endpoints['B']
                try:
                    with self.assertRaisesRegex(controller.ControllerError, '^checkpoint_inactive$'):
                        sample()
                finally:
                    instance.endpoints['A'] = original
                self.assertEqual((instance.stage, len(world.events), world.clock.value),
                                 (stage, count, clock))
                instance.generations += 1
                try:
                    with self.assertRaisesRegex(controller.ControllerError, '^checkpoint_inactive$'):
                        sample()
                finally:
                    instance.generations -= 1
                self.assertEqual((instance.stage, len(world.events), world.clock.value),
                                 (stage, count, clock))
            return world.checkpoint(point, sample)

        instance = controller.Controller(world.factory, checkpoint, world.restart,
            monotonic=world.clock, token_factory=world.token, record=world.record)
        self.assert_passed(instance.run('first', 17, 1000.0))
        self.assertTrue(probed)

    def test_usual_screen_is_separate_fresh_owner_receipt_without_usb_sampling(self):
        world = World()
        instance = controller.Controller(world.factory, world.checkpoint, world.restart,
            monotonic=world.clock, token_factory=world.token, record=world.record)
        with self.assertRaisesRegex(controller.ControllerError, '^handles_not_closed$'):
            instance.confirm_original(1000.0)
        self.assert_passed(instance.run('first', 17, 1000.0))
        commands = world.commands()
        acknowledgement = instance.confirm_original(1000.0)
        self.assertIs(type(acknowledgement), controller.CheckpointAck)
        self.assertEqual(acknowledgement.schema, 'OT-CANDIDATE-ACK-1')
        self.assertEqual(acknowledgement.kind, 'usual_screen')
        self.assertEqual(acknowledgement.roles, ('A', 'B'))
        self.assertEqual(acknowledgement.token, f'{world.token_count:032x}')
        self.assertEqual(world.commands(), commands)
        self.assertEqual([row for row in world.events if row[0] == 'sample'][-1],
                         ('sample', 'usual_screen', ()))
        world.receipt_fault = {'confirmed': False}
        with self.assertRaisesRegex(controller.ControllerError, '^human_confirmation_missing$'):
            instance.confirm_original(1000.0)
        self.assertEqual(world.commands(), commands)
        world.receipt_fault = None
        def reject_owner_ack(event):
            world.record(event)
            return not (event.get('schema') == 'OT-CANDIDATE-ACK-1' and
                        event.get('kind') == 'usual_screen')
        instance.record = reject_owner_ack
        with self.assertRaisesRegex(controller.ControllerError, '^record_failed$'):
            instance.confirm_original(1000.0)
        self.assertEqual(world.commands(), commands)

    def test_local_identity_pages_are_observed_before_peer_page_gestures(self):
        world = World()
        self.assert_passed(world.run())
        events = world.events
        local_gate = next(i for i, row in enumerate(events)
                          if row[:2] == ('checkpoint', 'fingerprint_local'))
        for role in ('A', 'B'):
            own_page = next(i for i, row in enumerate(events)
                            if row == ('wire', role, 1, 'SHOWLOCAL'))
            peer_page = next(i for i, row in enumerate(events)
                             if row == ('wire', role, 1, 'SHOWPEER'))
            gesture = next(i for i, row in enumerate(events)
                           if row == ('wire', role, 1, 'POLL'))
            finish = next(i for i, row in enumerate(events)
                          if row == ('wire', role, 1, 'FINISH'))
            self.assertLess(own_page, local_gate)
            self.assertLess(local_gate, peer_page)
            self.assertLess(peer_page, gesture)
            self.assertLess(gesture, finish)
        a_finish = next(i for i, row in enumerate(events) if row == ('wire', 'A', 1, 'FINISH'))
        b_peer = next(i for i, row in enumerate(events) if row == ('wire', 'B', 1, 'SHOWPEER'))
        self.assertLess(a_finish, b_peer)

    def test_transient_failed_close_is_not_retried_and_uncertainty_remains_sticky(self):
        world = World()
        world.close_once_role = 'A'
        instance = controller.Controller(world.factory, world.checkpoint, world.restart,
            monotonic=world.clock, token_factory=world.token, record=world.record)
        result = instance.run('retained_rekey', 17, 1000.0)
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure[1], 'handles_not_closed')
        self.assertFalse(result.handles_closed)
        self.assertEqual(world.handles[0].close_calls, 1)
        self.assertEqual(world.handles[1].close_calls, 1)
        self.assertFalse(world.handles[1].is_open)
        self.assertEqual(world.restart_count, 0)
        self.assertFalse(instance.close())
        self.assertFalse(instance.assert_idle())
        self.assertEqual(world.handles[0].close_calls, 1)


class StartupControllerTests(unittest.TestCase):
    """Diagnostic-only control using the real bounded, terminal wire client."""

    def instance(self, world, **overrides):
        world.startup_diagnostics = True
        options = dict(monotonic=world.clock, token_factory=world.token, record=world.record)
        options.update(overrides)
        return controller.Controller(world.factory, world.checkpoint, world.restart, **options)

    def records(self, world):
        return [row[1] for row in world.events if row[0] == 'record']

    def event(self, phase, value):
        return {'schema': 'OT-CANDIDATE-STARTUP-1', 'phase': phase,
                'role': 'A', 'generation': 1, 'value': value}

    def assert_probe_only(self, world, result):
        self.assertEqual((result.status_transfers, result.generations, result.checkpoints), (0, 1, 0))
        self.assertEqual([row[1:3] for row in world.events if row[0] == 'open'], [('A', 1)])
        self.assertEqual(world.commands('B'), [])
        self.assertTrue(set(world.commands()) <= {'HELLO', 'BOOTSTATUS'})
        self.assertEqual(world.restart_count, 0)
        self.assertEqual(world.token_count, 0)
        self.assertEqual(world.deliveries, [])
        self.assertFalse(any(row[0] == 'checkpoint' for row in world.events))
        self.assertLessEqual(len(self.records(world)), 2)

    def test_healthy_probe_uses_fragmented_startup_noise_and_no_enrollment(self):
        world = World()
        execute = world.nodes['A'].execute
        world.nodes['A'].execute = lambda command: (b'OTBOOT1 0 3 1\n' if command == 'HELLO' else b'') + execute(command)
        result = world.run('startup_A')
        self.assert_probe_only(world, result)
        self.assertEqual(world.commands(), ['HELLO', 'BOOTSTATUS'])
        self.assertEqual((result.outcome, result.first_failure, result.refusals), ('passed', None, ()))
        self.assertTrue(result.handles_closed)
        self.assertEqual(self.records(world), [self.event('hello', 'ready'), self.event('bootstatus', 0)])

    def test_exact_hello_refusal_allows_one_stage_read_and_remains_primary(self):
        for stage in range(10):
            with self.subTest(stage=stage):
                world = World()
                world.faults[('A', 1, 'HELLO')] = 'refused'
                world.nodes['A'].execute = lambda command: f'OTCAND1 BOOTSTATUS {stage}\n'.encode()
                instance = self.instance(world)
                result = instance.run('startup_A', 17, 1000)
                self.assert_probe_only(world, result)
                self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
                self.assertEqual(result.refusals, (('A', 'HELLO'),))
                self.assertEqual(result.outcome, 'failed')
                self.assertTrue(result.handles_closed)
                self.assertTrue(instance.endpoints['A'].failed)
                self.assertFalse(instance.endpoints['A'].ready)
                self.assertEqual(world.commands(), ['HELLO', 'BOOTSTATUS'])
                self.assertEqual(self.records(world), [self.event('hello', 'refused'), self.event('bootstatus', stage)])

    def test_nonrefusal_hello_faults_allow_close_only(self):
        faults = {'partial': 'partial_write', 'exception': 'serial_operation_failed',
                  'route_swap': 'identity_guard', 'late': 'deadline_expired',
                  'malformed': 'invalid_response', 'queued': 'unsolicited_response',
                  'timeout': 'deadline_expired'}
        for fault, category in faults.items():
            with self.subTest(fault=fault):
                world = World()
                if fault == 'queued':
                    world.stale_generation = ('A', 1)
                elif fault == 'malformed':
                    world.nodes['A'].execute = lambda command: b'OTCAND1 READY 01\n'
                elif fault == 'timeout':
                    world.nodes['A'].execute = lambda command: b''
                    factory = world.factory
                    def delayed_factory(*args):
                        endpoint = factory(*args)
                        read = endpoint.handle.read
                        def empty_read(count):
                            world.clock.value += .5
                            return read(count)
                        endpoint.handle.read = empty_read
                        return endpoint
                    world.factory = delayed_factory
                else:
                    world.faults[('A', 1, 'HELLO')] = fault
                result = world.run('startup_A')
                self.assert_probe_only(world, result)
                self.assertEqual(result.first_failure, ('boot_observation_A' if fault == 'queued' else 'hello_A', category))
                self.assertEqual(result.refusals, ())
                self.assertNotIn('BOOTSTATUS', world.commands())
                self.assertEqual(self.records(world), [])
                self.assertTrue(result.handles_closed)

    def test_only_exact_owned_refusal_enables_diagnostic_query(self):
        class SpoofedClientError(wire.ClientError):
            pass
        for error in (SpoofedClientError('target_refused'), controller.ControllerError('target_refused'),
                      RuntimeError('target_refused')):
            with self.subTest(error=type(error).__name__):
                world = World()
                factory = world.factory
                def fake_refusal(*args):
                    endpoint = factory(*args)
                    def exchange(command, deadline):
                        world.events.append(('wire', 'A', 1, command))
                        raise error
                    endpoint.exchange = exchange
                    return endpoint
                world.factory = fake_refusal
                result = world.run('startup_A')
                self.assert_probe_only(world, result)
                self.assertEqual(world.commands(), ['HELLO'])
                self.assertEqual(result.refusals, ())
                self.assertEqual(self.records(world), [])
                self.assertTrue(result.handles_closed)

    def test_secondary_boot_fault_cannot_replace_exact_hello_refusal(self):
        for fault in ('partial', 'exception', 'refused', 'route_swap', 'bad_response', 'timeout'):
            with self.subTest(fault=fault):
                world = World()
                world.faults[('A', 1, 'HELLO')] = 'refused'
                if fault == 'timeout':
                    world.nodes['A'].execute = lambda command: b''
                    factory = world.factory
                    def delayed_factory(*args):
                        endpoint = factory(*args)
                        read = endpoint.handle.read
                        def empty_read(count):
                            if world.commands().count('BOOTSTATUS'):
                                world.clock.value += .5
                            return read(count)
                        endpoint.handle.read = empty_read
                        return endpoint
                    world.factory = delayed_factory
                else:
                    world.faults[('A', 1, 'BOOTSTATUS')] = fault
                result = world.run('startup_A')
                self.assert_probe_only(world, result)
                self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
                self.assertEqual(result.refusals, (('A', 'HELLO'),))
                self.assertEqual(world.commands(), ['HELLO', 'BOOTSTATUS'])
                self.assertEqual(self.records(world), [self.event('hello', 'refused'), self.event('bootstatus_failed', 'failed')])
                self.assertTrue(result.handles_closed)

    def test_ready_then_nonzero_stage_is_recorded_and_rejected(self):
        for stage in range(1, 10):
            with self.subTest(stage=stage):
                world = World()
                execute = world.nodes['A'].execute
                world.nodes['A'].execute = lambda command: (f'OTCAND1 BOOTSTATUS {stage}\n'.encode()
                    if command == 'BOOTSTATUS' else execute(command))
                result = world.run('startup_A')
                self.assert_probe_only(world, result)
                self.assertEqual(result.first_failure, ('bootstatus_A', 'reply_invalid'))
                self.assertEqual(result.refusals, ())
                self.assertEqual(self.records(world), [self.event('hello', 'ready'), self.event('bootstatus', stage)])
                self.assertTrue(result.handles_closed)

    def test_ready_then_query_fault_is_primary_before_failed_observation(self):
        for fault, category in (('partial', 'partial_write'), ('exception', 'serial_operation_failed'),
                                ('refused', 'target_refused'), ('bad_response', 'unexpected_response')):
            with self.subTest(fault=fault):
                world = World()
                world.faults[('A', 1, 'BOOTSTATUS')] = fault
                result = world.run('startup_A')
                self.assert_probe_only(world, result)
                self.assertEqual(result.first_failure, ('bootstatus_A', category))
                self.assertEqual(result.refusals, ())
                self.assertEqual(self.records(world), [self.event('hello', 'ready'), self.event('bootstatus_failed', 'failed')])

    def test_record_failure_or_expiry_blocks_boot_query(self):
        for refused in (False, True):
            for fault in ('false', 'raise', 'expiry'):
                with self.subTest(refused=refused, fault=fault):
                    world = World()
                    if refused:
                        world.faults[('A', 1, 'HELLO')] = 'refused'
                    def record(event):
                        world.record(event)
                        if fault == 'raise':
                            raise RuntimeError('private record detail')
                        if fault == 'expiry':
                            world.clock.value = world.deadlines[0][3]
                        return fault != 'false'
                    result = self.instance(world, record=record).run('startup_A', 17, 1000)
                    self.assert_probe_only(world, result)
                    expected = ('hello_A', 'target_refused') if refused else ('hello_A',
                        'record_failed' if fault == 'false' else 'controller_operation_failed' if fault == 'raise' else 'deadline_expired')
                    self.assertEqual(result.first_failure, expected)
                    self.assertEqual(world.commands(), ['HELLO'])
                    self.assertTrue(result.handles_closed)

    def test_boot_observation_failure_preserves_primary_and_never_retries(self):
        for refused in (False, True):
            for query_failed in (False, True):
                world = World()
                if refused:
                    world.faults[('A', 1, 'HELLO')] = 'refused'
                if query_failed:
                    world.faults[('A', 1, 'BOOTSTATUS')] = 'partial'
                def record(event):
                    world.record(event)
                    return event['phase'] == 'hello'
                result = self.instance(world, record=record).run('startup_A', 17, 1000)
                expected = ('hello_A', 'target_refused') if refused else ('bootstatus_A',
                    'partial_write' if query_failed else 'record_failed')
                self.assertEqual(result.first_failure, expected)
                self.assert_probe_only(world, result)
                self.assertEqual(world.commands(), ['HELLO', 'BOOTSTATUS'])

    def test_startup_and_command_caps_include_opening_recording_and_original_deadline(self):
        for original, opening, record_delay in ((1000, 20, 3), (1000, 55, 4), (11, .1, .2)):
            with self.subTest(original=original, opening=opening):
                world = World()
                world.clock.value, world.clock.step = 10, 0
                factory = world.factory
                def slow_factory(*args):
                    endpoint = factory(*args)
                    world.clock.value += opening
                    return endpoint
                world.factory = slow_factory
                def record(event):
                    world.record(event)
                    if event['phase'] == 'hello':
                        world.clock.value += record_delay
                    return True
                result = self.instance(world, record=record).run('startup_A', 17, original)
                self.assert_probe_only(world, result)
                self.assertEqual(result.outcome, 'passed', result.first_failure)
                total = min(original, 70)
                self.assertEqual(next(row[3] for row in world.events if row[0] == 'open'), total)
                self.assertEqual([row[3] for row in world.deadlines],
                    [min(total, 10 + opening + 5), min(total, 10 + opening + record_delay + 5)])

    def test_late_ready_reply_cannot_enable_query_despite_live_total_cap(self):
        world = World()
        execute = world.nodes['A'].execute
        def late_hello(command):
            if command == 'HELLO':
                world.clock.value += 6
            return execute(command)
        world.nodes['A'].execute = late_hello
        result = world.run('startup_A')
        self.assert_probe_only(world, result)
        self.assertEqual(result.first_failure, ('hello_A', 'deadline_expired'))
        self.assertEqual(world.commands(), ['HELLO'])
        self.assertEqual(self.records(world), [])

    def test_failed_close_is_sticky_and_never_sends_protocol_close(self):
        for refused in (False, True):
            world = World()
            world.close_once_role = 'A'
            if refused:
                world.faults[('A', 1, 'HELLO')] = 'refused'
            instance = self.instance(world)
            result = instance.run('startup_A', 17, 1000)
            self.assert_probe_only(world, result)
            self.assertEqual(result.first_failure, ('hello_A', 'target_refused') if refused else ('cleanup', 'handles_not_closed'))
            self.assertFalse(result.handles_closed)
            self.assertEqual(world.handles[0].close_calls, 1)
            self.assertFalse(instance.close())
            self.assertEqual(world.handles[0].close_calls, 1)


if __name__ == '__main__':
    unittest.main()
