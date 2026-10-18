"""Recovery observation with actual host orchestration and synthetic devices.

No serial enumeration, SDK, firmware, cryptographic or physical acceptance.
The baseline source is retained in the approved private validation receipt;
existing suites provide fake helpers only and are not executed by this suite.
"""
import base64
import copy
import hashlib
import json
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_controller as core
import enrollment_candidate_operator as operator
import enrollment_candidate_runner as runner
import enrollment_candidate_usb_client as wire
import enrollment_candidate_operator_tests as prior

SCHEMA = 'OT-CANDIDATE-RECOVERY-SUMMARY-1'
CASES = ('recovery_after_A_commit', 'recovery_after_B_commit')
BASELINE = ROOT / '.private/ot0238c13-recovery-observation-20261014/validation.json'


class RecoveryObservationTests(unittest.TestCase):
    def fixture(self, case=CASES[0], module=prior, collect=True):
        f = module.Fixture(case=case)
        self.addCleanup(f.cleanup)
        f.world.clock.step = .000001
        f.instance.controller.tokens = f.world.token
        f.instance.passive.observation_clock = lambda: int(f.world.clock.value * 1e9)
        f.activate(deadline=300)
        events = runner._Events(f.root, '9' * 32, 'a' * 64, 'b' * 64,
            core.Clock(f.world.clock), 300, 400, case=case)
        original = f.instance.record
        def record(value):
            if value.get('schema') == SCHEMA and collect:
                self.assertTrue(f.instance.assert_idle())
                return events(value)
            return original(value)
        f.instance.record = record
        return f, events

    def summary(self, events):
        rows = [json.loads(line) for line in events.path.read_text().splitlines()]
        self.assertEqual(len(rows), 1)
        return rows[0]

    def value(self, events):
        return {k: self.summary(events)[k] for k in ('schema', 'budget', 'queries')}

    def test_complete_actual_producer_collector_both_cases_and_fragmentation(self):
        for case in CASES:
            for size in (1, 7, 64):
                with self.subTest(case=case, read_size=size):
                    f, events = self.fixture(case)
                    read = prior.SerialHandle.read
                    with patch.object(prior.SerialHandle, 'read', lambda h, n: read(h, min(n, size))):
                        result = f.run()
                    self.assertEqual((result.outcome, result.status_transfers), ('passed', 8))
                    value = self.value(events)
                    self.assertEqual(len(value['queries']), 9)
                    self.assertLessEqual(value['budget']['initial_allowance_ns'], 60_000_000_000)
                    for row in value['queries']:
                        self.assertEqual((row['boundary'], row['rejected'], row['generation']), ('returned', False, 2))
                        q = row['query']['transport']
                        self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
                        self.assertTrue(q['reply_parsed'] and q['reply_returned'])
                        self.assertIsNone(q['stream'])
                    raw = events.path.read_text()
                    self.assertLess(len(raw), 24000)
                    secrets = ('OTCAND1', 'COM1', 'COM2', 'COM3', 'COM4', 'device_binding', 'token', 'PRIVATE')
                    secrets += tuple(prior.IDENTITIES.values()) + (prior.KEY.hex(),)
                    for secret in secrets:
                        self.assertNotIn(secret, raw)
                    proofs = [command.split(' ', 2)[2].strip()
                        for handle in f.world.handles for frame in handle.writes
                        for command in (frame.decode(),)
                        if command.split(' ', 2)[1] in ('ACCEPTARCHIVE', 'RECOVERSIGN', 'RECOVERFINISH')]
                    self.assertGreaterEqual(len(proofs), 5)
                    for proof in proofs:
                        self.assertGreater(len(proof), 20)
                        self.assertNotIn(proof, raw)
                    with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                        events(value)

    def test_six_timeout_boundaries_freeze_distinct_first_rejection(self):
        for crossing in ('controller_precheck', 'pre_write_after_guard', 'reply_absent',
                'post_parse_after_guard', 'wrapper_postcheck', 'controller_postcheck'):
            with self.subTest(crossing=crossing):
                f, events = self.fixture()
                active, caps, returned = {}, [], []
                send = core.Controller._send_checked
                observed = operator.PassiveEndpoint._final_exchange
                operation = wire.Endpoint._query_operation
                exchange = wire.Endpoint.exchange
                read = prior.SerialHandle.read
                check = operator.PassiveEndpoints.check
                def selected(endpoint):
                    return endpoint.role == 'B' and endpoint.generation == 2 and active.get('B') == 'RECOVERFINISH'
                def sends(controller, role, command, deadline, row):
                    if controller.generations == 2 and role == 'B' and command.startswith('RECOVERFINISH '):
                        caps.append(deadline)
                        if crossing == 'controller_precheck': f.world.clock.value = deadline
                    return send(controller, role, command, deadline, row)
                def observe(endpoint, command, deadline, **kwargs):
                    active[endpoint.role] = command.split(' ', 1)[0]
                    try:
                        result = observed(endpoint, command, deadline, **kwargs)
                        if selected(endpoint) and crossing == 'controller_postcheck': f.world.clock.value = deadline
                        return result
                    finally:
                        active.pop(endpoint.role, None)
                        returned.clear()
                def exchanges(endpoint, command, deadline):
                    reply = exchange(endpoint, command, deadline)
                    if selected(endpoint): returned.append(True)
                    return reply
                def boundary(endpoint, name):
                    operation(endpoint, name)
                    if selected(endpoint) and crossing == name: f.world.clock.value = caps[-1]
                def reads(handle, count):
                    if handle.node.role == 'B' and handle.node.generation == 2 and active.get('B') == 'RECOVERFINISH' and crossing == 'reply_absent':
                        handle.received.clear()
                        f.world.clock.value = caps[-1]
                        return b''
                    return read(handle, count)
                def checks(owner, deadline):
                    if crossing == 'wrapper_postcheck' and returned and active.get('B') == 'RECOVERFINISH':
                        endpoint = next((e for e in owner.endpoints if e.role == 'B' and e.generation == 2), None)
                        if endpoint is not None and endpoint._query is not None and endpoint._query['reply_returned']:
                            f.world.clock.value = deadline
                    return check(owner, deadline)
                with patch.object(core.Controller, '_send_checked', sends), \
                        patch.object(operator.PassiveEndpoint, '_final_exchange', observe), \
                        patch.object(wire.Endpoint, '_query_operation', boundary), \
                        patch.object(wire.Endpoint, 'exchange', exchanges), \
                        patch.object(prior.SerialHandle, 'read', reads), \
                        patch.object(operator.PassiveEndpoints, 'check', checks):
                    result = f.run()
                self.assertEqual(result.first_failure, ('recoverfinish_B', 'deadline_expired'))
                self.assertTrue(result.handles_closed)
                row = self.summary(events)['queries'][-1]
                self.assertEqual((row['phase'], row['role'], row['rejected']), ('recoverfinish', 'B', True))
                frozen = copy.deepcopy(row)
                for endpoint in f.instance.passive.endpoints:
                    if endpoint._query is not None: endpoint._query['received_bytes'] = 0
                f.world.clock.value += 50
                self.assertEqual(frozen, f.instance.controller.recovery_observations[-1])
                if crossing == 'controller_precheck':
                    self.assertEqual(row['boundary'], 'precheck'); self.assertIsNone(row['query'])
                else:
                    q = row['query']['transport']
                    self.assertEqual(q['reply_parsed'], crossing in ('post_parse_after_guard', 'wrapper_postcheck', 'controller_postcheck'))
                    self.assertEqual(q['reply_returned'], crossing in ('wrapper_postcheck', 'controller_postcheck'))
                    self.assertEqual(q['write_attempted'], int(crossing != 'pre_write_after_guard'))
                    self.assertEqual(row['boundary'], 'postcheck' if crossing == 'controller_postcheck' else 'endpoint')
                    if crossing == 'wrapper_postcheck': self.assertEqual(row['query']['boundary'], 'postcheck')

    def test_first_rejection_early_in_recovery_is_terminal(self):
        for verb in ('ARCHIVE', 'RECOVERBEGIN', 'RECOVERSIGN', 'RECOVERFINISH'):
            with self.subTest(verb=verb):
                f, events = self.fixture()
                f.world.faults[('A', 2, verb)] = 'refused'
                result = f.run()
                self.assertEqual(result.first_failure, (verb.lower() + '_A', 'target_refused'))
                rows = self.summary(events)['queries']
                self.assertEqual((rows[-1]['phase'], rows[-1]['role'], rows[-1]['rejected']), (verb.lower(), 'A', True))
                self.assertTrue(rows[-1]['query']['transport']['reply_refused'])

    def test_shared_budget_origin_consumed_by_begin_and_prior_queries(self):
        f, events = self.fixture()
        write, send = prior.SerialHandle.write, core.Controller._send_checked
        deadlines = []
        def writes(handle, raw):
            verb = raw.decode().split(' ', 2)[1].strip()
            if handle.node.generation == 2:
                if verb == 'BEGIN': f.world.clock.value += 4
                elif verb in core.RECOVERY_PHASES and (handle.node.role, verb) != ('B', 'RECOVERFINISH'):
                    f.world.clock.value += 5
            return write(handle, raw)
        def sends(controller, role, command, deadline, row):
            if controller.generations == 2 and command.split(' ', 1)[0] in core.RECOVERY_PHASES: deadlines.append(deadline)
            return send(controller, role, command, deadline, row)
        with patch.object(prior.SerialHandle, 'write', writes), patch.object(core.Controller, '_send_checked', sends): result = f.run()
        self.assertEqual(result.outcome, 'passed')
        self.assertEqual(len(set(deadlines)), 1)
        value = self.value(events); last = value['queries'][-1]
        self.assertGreater(last['shared_sampled_elapsed_ns'], 48_000_000_000)
        self.assertLess(last['entry_allowance_ns'], 12_000_000_000)
        self.assertGreater(last['entry_allowance_ns'], 11_000_000_000)

    def test_invalid_authority_sample_marks_shared_elapsed_unknown(self):
        f, events = self.fixture()
        send = core.Controller._send_checked
        def sends(controller, role, command, deadline, row):
            if controller.generations == 2 and role == 'B' and command.startswith('RECOVERFINISH '):
                f.world.clock.value -= 1
            return send(controller, role, command, deadline, row)
        with patch.object(core.Controller, '_send_checked', sends): result = f.run()
        self.assertEqual(result.first_failure, ('recoverfinish_B', 'host_clock_invalid'))
        # The frozen controller receipt remains available even when the original
        # execution clock prevents a durable summary; no replacement authority.
        row = f.instance.controller.recovery_observations[-1]
        self.assertIsNone(row['shared_sampled_elapsed_ns'])
        self.assertEqual(row['diagnostic_failure'], 'authority_sample_invalid')

    def test_expired_execution_ceiling_never_borrows_restoration_budget(self):
        f, events = self.fixture()
        record = operator.CandidateOperator._record_recovery_summary
        called = []
        def expired(instance):
            self.assertTrue(instance.assert_idle())
            f.world.clock.value = instance.passive.deadline
            return record(instance)
        original = f.instance.record
        def records(value):
            if value.get('schema') == SCHEMA: called.append(True)
            return original(value)
        f.instance.record = records
        with patch.object(operator.CandidateOperator, '_record_recovery_summary', expired): result = f.run()
        self.assertEqual((result.outcome, result.first_failure, result.status_transfers), ('passed', None, 8))
        self.assertTrue(result.handles_closed)
        self.assertEqual(called, [])
        self.assertEqual(events.path.read_bytes(), b'')

    def test_observer_reentry_after_refusal_preserves_first_error(self):
        f, events = self.fixture()
        f.world.faults[('B', 2, 'RECOVERFINISH')] = 'refused'
        finish = operator._QueryObservation.finish
        def finishes(observation):
            if observation.row['phase'] == 'recoverfinish' and observation.row['role'] == 'B':
                endpoint = next(e for e in f.instance.passive.endpoints if e.role == 'B' and e.generation == 2)
                try: endpoint.exchange('RECOVERFINISH PRIVATE', 300)
                except wire.ClientError: pass
            return finish(observation)
        with patch.object(operator._QueryObservation, 'finish', finishes): result = f.run()
        self.assertEqual(result.first_failure, ('recoverfinish_B', 'target_refused'))
        self.assertTrue(result.handles_closed)
        self.assertTrue(all(not e._observing for e in f.instance.passive.endpoints))

    def test_scope_does_not_observe_other_cases_or_direct_verbs(self):
        for case in ('first', 'retained_rekey', 'cancel', 'revoke', 'reset_preparation'):
            with self.subTest(case=case):
                f, events = self.fixture(case)
                self.assertEqual(f.run().outcome, 'passed')
                self.assertEqual(f.instance.controller.recovery_observations, [])
                self.assertEqual(events.path.read_bytes(), b'')
        controller = core.Controller(None, None, None, record=lambda row: True)
        controller.generations = 2
        with self.assertRaises(Exception): controller._send('B', 'RECOVERFINISH PRIVATE', 300)
        self.assertEqual(controller.recovery_observations, [])

    def test_observer_and_record_faults_never_replace_owned_result(self):
        for fault in ('snapshot', 'finish', 'clock', 'record'):
            for refusal in (False, True):
                with self.subTest(fault=fault, refusal=refusal):
                    f, events = self.fixture()
                    if refusal: f.world.faults[('B', 2, 'RECOVERFINISH')] = 'refused'
                    snapshot, finish = operator.PassiveEndpoint.query_snapshot, operator._QueryObservation.finish
                    observed, active = operator.PassiveEndpoint._final_exchange, set()
                    def observe(endpoint, command, deadline, **kwargs):
                        if kwargs.get('recovery'): active.add(id(endpoint))
                        try: return observed(endpoint, command, deadline, **kwargs)
                        finally: active.discard(id(endpoint))
                    def snapshots(endpoint):
                        if id(endpoint) in active and fault == 'snapshot': raise RuntimeError('PRIVATE')
                        return snapshot(endpoint)
                    def finishes(observation):
                        if observation.row['phase'].upper() in core.RECOVERY_PHASES:
                            if fault == 'finish': raise RuntimeError('PRIVATE')
                            if fault == 'clock': observation.row['timing_valid'] = False
                        return finish(observation)
                    original = f.instance.record
                    def records(value):
                        if value.get('schema') == SCHEMA and fault == 'record': raise RuntimeError('PRIVATE')
                        return original(value)
                    f.instance.record = records
                    with patch.object(operator.PassiveEndpoint, 'query_snapshot', snapshots), \
                            patch.object(operator.PassiveEndpoint, '_final_exchange', observe), \
                            patch.object(operator._QueryObservation, 'finish', finishes): result = f.run()
                    self.assertEqual(result.first_failure, ('recoverfinish_B', 'target_refused') if refusal else None)
                    self.assertTrue(result.handles_closed)
                    if fault == 'record': self.assertEqual(events.path.read_bytes(), b'')
                    else:
                        self.assertNotIn('PRIVATE', events.path.read_text())
                        self.assertTrue(any(r['query']['diagnostic_failure'] in ('query_snapshot', 'query_finish', 'observation_clock_invalid')
                            for r in self.summary(events)['queries']))

    def test_observer_reentry_contained_and_barrier_cleared(self):
        f, events = self.fixture()
        original = operator._QueryObservation.sample
        invoked = []
        def sample(observation):
            if observation.row['phase'] == 'recoverfinish' and observation.row['role'] == 'B' and not invoked:
                invoked.append(True)
                endpoint = next(e for e in f.instance.passive.endpoints if e.role == 'B' and e.generation == 2)
                try: endpoint.exchange('RECOVERFINISH PRIVATE', 300)
                except wire.ClientError: pass
            return original(observation)
        with patch.object(operator._QueryObservation, 'sample', sample): result = f.run()
        self.assertEqual(result.first_failure, ('recoverfinish_B', 'lease_terminal'))
        self.assertTrue(result.handles_closed)
        self.assertTrue(all(not e._observing for e in f.instance.passive.endpoints))
        self.assertNotIn('PRIVATE', events.path.read_text())

    def test_malformed_metadata_refuses_before_durable_write(self):
        f, events = self.fixture(); self.assertEqual(f.run().outcome, 'passed')
        value = self.value(events)
        bads = []
        for name, bad in (('phase', 'PRIVATE'), ('role', 'C'), ('generation', True), ('generation', 1),
                ('shared_sampled_elapsed_ns', -1), ('entry_allowance_ns', True), ('boundary', 'PRIVATE'),
                ('diagnostic_failure', 'PRIVATE'), ('query', None)):
            row = copy.deepcopy(value); row['queries'][0][name] = bad; bads.append(row)
        row = copy.deepcopy(value); row['queries'][0]['private'] = 'PRIVATE'; bads.append(row)
        row = copy.deepcopy(value); row['queries'][0]['query']['transport']['private'] = 'PRIVATE'; bads.append(row)
        row = copy.deepcopy(value); row['queries'][1] = row['queries'][0]; bads.append(row)
        row = copy.deepcopy(value); row['queries'] *= 2; bads.append(row)
        row = copy.deepcopy(value); row['queries'][2]['role'] = 'A'; bads.append(row)
        row = copy.deepcopy(value); row['budget']['initial_allowance_ns'] = True; bads.append(row)
        row = copy.deepcopy(value); row['budget']['initial_allowance_ns'] = 60_000_000_001; bads.append(row)
        row = copy.deepcopy(value); row['schema'] = 'PRIVATE'; bads.append(row)
        before = events.path.read_bytes()
        for bad in bads:
            with self.subTest(bad=bad):
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): runner._recovery_queries(bad, CASES[0])
        for case in (None, 'first', 'retained_rekey', 'startup_A'):
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): runner._recovery_queries(value, case)
        self.assertEqual(events.path.read_bytes(), before)

    def test_wrapper_precheck_never_inherits_previous_transport(self):
        f, events = self.fixture()
        check = operator.PassiveEndpoints.check
        sent = core.Controller._send_checked
        active, injected = [], []
        def sends(controller, role, command, deadline, row):
            selected = controller.generations == 2 and role == 'B' and command.startswith('RECOVERFINISH ')
            if selected: active.append(True)
            try: return sent(controller, role, command, deadline, row)
            finally: active.clear()
        def checks(owner, deadline):
            if active and not injected:
                injected.append(True); f.world.clock.value = deadline
            return check(owner, deadline)
        with patch.object(core.Controller, '_send_checked', sends), patch.object(operator.PassiveEndpoints, 'check', checks): result = f.run()
        self.assertEqual(result.first_failure, ('recoverfinish_B', 'deadline_expired'))
        row = self.summary(events)['queries'][-1]
        self.assertEqual(row['query']['boundary'], 'owner_check')
        self.assertIsNone(row['query']['transport'])

    def test_real_baseline_wire_guard_clock_and_cleanup_invariance(self):
        receipt = json.loads(BASELINE.read_text())
        def load(name, path, source):
            module = types.ModuleType(name); module.__file__ = str(path)
            self.addCleanup(sys.modules.pop, name, None); sys.modules[name] = module
            exec(compile(source, str(path), 'exec'), module.__dict__)
            return module
        oldcore = load('c13_baseline_controller', ROOT/'tools/enrollment_candidate_controller.py',
            base64.b64decode(receipt['baseline_source_bytes_base64']['tools/enrollment_candidate_controller.py']))
        with patch.dict(sys.modules, {'enrollment_candidate_controller': oldcore}):
            oldoperator = load('c13_baseline_operator', ROOT/'tools/enrollment_candidate_operator.py',
                base64.b64decode(receipt['baseline_source_bytes_base64']['tools/enrollment_candidate_operator.py']))
            with patch.dict(sys.modules, {'enrollment_candidate_operator': oldoperator}):
                oldfixture = load('c13_baseline_fixture', Path(prior.__file__), Path(prior.__file__).read_bytes())
        for case in (*CASES, 'first', 'retained_rekey'):
            for refused in (False, True):
                traces = []
                for module, source in ((oldfixture, oldcore), (prior, core)):
                    f, events = self.fixture(case, module=module, collect=False)
                    if refused: f.world.faults[('B', 2 if case in (*CASES, 'retained_rekey') else 1, 'RECOVERFINISH' if case in CASES else 'COMMIT')] = 'refused'
                    clocks, queries, completed = [], [], []
                    original_clock = f.world.clock.__class__.__call__
                    original_send = source.Controller._send_checked
                    original_run = source.Controller.run
                    def clock(this):
                        result = original_clock(this)
                        if this is f.world.clock and not completed: clocks.append(result)
                        return result
                    def send(this, role, command, deadline, row):
                        queries.append((role, command.split(' ', 1)[0], deadline))
                        return original_send(this, role, command, deadline, row)
                    def run(this, *args):
                        result = original_run(this, *args)
                        completed.append(True)
                        return result
                    with patch.object(f.world.clock.__class__, '__call__', clock), patch.object(source.Controller, '_send_checked', send), \
                            patch.object(source.Controller, 'run', run): result = f.run()
                    traces.append((result.first_failure, result.status_transfers, result.handles_closed,
                        (len(clocks), hashlib.sha256(json.dumps(clocks).encode()).hexdigest()), queries,
                        f.runtime.calls, [event for event in f.world.events if event[0] in ('wire', 'open', 'passive_close', 'restart')],
                        [[*handle.writes] for handle in f.world.handles]))
                self.assertEqual(traces[0], traces[1], (case, refused))
                if case in CASES:
                    errors = []
                    for module in (oldfixture, prior):
                        f, _ = self.fixture(case, module=module, collect=False); f.run()
                        endpoint = next(e for e in f.instance.passive.endpoints if e.generation == 2)
                        try: endpoint.exchange(None, 300)
                        except BaseException as error: errors.append((type(error).__name__, error.args))
                    self.assertEqual(errors[0], errors[1])


if __name__ == '__main__': unittest.main(verbosity=2)
