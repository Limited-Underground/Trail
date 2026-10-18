"""Host-only lifecycle sequencing, authority and real custody-engine tests."""
from pathlib import Path
from contextlib import contextmanager
import copy
import json
import queue
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests' / 'host'))
import gnss_lifecycle_observation as lifecycle
import gnss_observation_trial as engine
import run_gnss_lifecycle_trial as runner
import gnss_observation_trial_tests as legacy
import connection_diagnostic_trial_tests as connection_tests


class Replies:
    def __init__(self, changes=None, delays=None):
        self.now = 0
        self.phase = None
        self.changes = changes or {}
        self.delays = delays or {}
        self.reads = []
        self.restarts = 0
        self.counter = 0
        self.completed = []

    def emit(self, raw, **_kwargs):
        self.phase = json.loads(raw)

    def read(self, timeout):
        case = self.phase['case']
        self.reads.append((case, timeout))
        self.now += self.delays.get(case, 120 if case == 'phone_disconnected' else 1)
        reply = {'case': case, 'token': self.phase['token']}
        reply.update({field: True for field in lifecycle.FIELDS[case]})
        change = self.changes.get(case)
        if callable(change):
            return change(self, reply)
        if change is not None:
            reply.update(change)
        return reply

    def token(self):
        self.counter += 1
        return f'{self.counter:032x}'

    def restart(self, prefix):
        lifecycle.validate_restart_prefix(prefix)
        self.restarts += 1
        self.now += 3
        return True

    def run(self, restart=None, record=None, available_seconds=2160):
        return lifecycle.observe(self, restart or self.restart, emit=self.emit,
            clock=lambda: self.now, token_factory=self.token,
            available_seconds=available_seconds, on_case=record or self.completed.append)


class ObservationTests(unittest.TestCase):
    def test_exact_order_phone_off_then_unknown_clock_before_reconnect(self):
        replies = Replies()
        result = replies.run()
        self.assertEqual(result['outcome'], 'passed')
        self.assertEqual([item['case'] for item in result['cases']], list(lifecycle.CASES))
        self.assertEqual(replies.restarts, 1)
        self.assertEqual(result['cases'], replies.completed)
        self.assertEqual(result['cases'][2]['elapsed_ms'] - result['cases'][1]['elapsed_ms'], 120000)

    def test_old_token_and_wrong_phase_cannot_advance(self):
        for change in ({'token': 'old'}, {'case': 'fresh_reconnect'}):
            replies = Replies({'phone_stopped': change})
            result = replies.run()
            self.assertEqual(result['outcome'], 'invalid_input')
            self.assertEqual(result['first_failure'], 'phone_stopped')
            self.assertEqual(replies.restarts, 0)

    def test_private_extra_fields_and_non_boolean_rejected(self):
        for change in ({'coordinates': 'must-not-be-recorded'}, {'protected_ready': 1}):
            replies = Replies({'baseline': change})
            result = replies.run()
            self.assertEqual(result['outcome'], 'invalid_input')
            self.assertNotIn('must-not-be-recorded', json.dumps(result))

    def test_disconnect_hold_starts_after_stopped_ack(self):
        for held, outcome in ((119, 'invalid_input'), (120, 'passed')):
            with self.subTest(held=held):
                replies = Replies(delays={'phone_stopped': 179, 'phone_disconnected': held})
                result = replies.run()
                self.assertEqual(result['outcome'], outcome)
                self.assertEqual(result['cases'][2]['elapsed_ms'] -
                                 result['cases'][1]['elapsed_ms'], held * 1000)
                if outcome != 'passed':
                    self.assertEqual(result['first_failure'], 'phone_disconnected')
                    self.assertEqual(replies.restarts, 0)

    def test_delayed_human_acknowledgements_complete_inside_fixed_windows(self):
        for delay in (61, 179):
            with self.subTest(delay=delay):
                delays = {'phone_stopped': delay, 'phone_disconnected': 299,
                          'clock_cleared': delay}
                replies = Replies(delays=delays)
                result = replies.run()
                self.assertEqual(result['outcome'], 'passed')
                self.assertEqual(replies.restarts, 1)
                for index, item in enumerate(result['cases']):
                    if item['case'] in delays:
                        self.assertEqual(item['elapsed_ms'] - result['cases'][index - 1]['elapsed_ms'],
                                         delays[item['case']] * 1000)

    def test_human_acknowledgement_at_case_expiry_cannot_advance(self):
        for case, delay in (('phone_stopped', 180), ('phone_disconnected', 300),
                            ('clock_cleared', 180)):
            with self.subTest(case=case):
                replies = Replies(delays={case: delay})
                result = replies.run()
                self.assertEqual(result['outcome'], 'timeout')
                self.assertEqual(result['first_failure'], case)
                self.assertEqual(result['cases'], replies.completed)
                self.assertEqual(result['cases'][-1]['elapsed_ms'] -
                                 result['cases'][-2]['elapsed_ms'], delay * 1000)
                self.assertEqual(replies.reads[-1][0], case)
                self.assertEqual(replies.restarts, 1 if case == 'clock_cleared' else 0)

    def test_human_acknowledgement_window_is_clipped_by_overall_deadline(self):
        replies = Replies(delays={'phone_stopped': 100})
        result = replies.run(available_seconds=101)
        self.assertEqual(result['outcome'], 'timeout')
        self.assertEqual(result['first_failure'], 'phone_stopped')
        self.assertEqual(result['elapsed_ms'], 101000)
        self.assertEqual(replies.reads[-1], ('phone_stopped', 100))
        self.assertEqual(replies.restarts, 0)

    def test_pending_baseline_does_not_reset_its_deadline(self):
        def pending(replies, reply):
            replies.now += 99
            reply['protected_ready'] = False
            return reply
        replies = Replies({'baseline': pending})
        result = replies.run()
        self.assertEqual(result['outcome'], 'timeout')
        self.assertEqual(len(replies.reads), 3)
        self.assertEqual(replies.restarts, 0)

    def test_natural_loss_not_observed_is_inconclusive(self):
        def absent(replies, _reply):
            return {'case': replies.phase['case'], 'token': replies.phase['token'], 'result': 'not_observed'}
        result = Replies({'gps_loss': absent}).run()
        self.assertEqual(result['outcome'], 'inconclusive')
        self.assertEqual(result['first_failure'], 'gps_loss')

    def test_gps_pending_does_not_turn_reception_delay_into_failure(self):
        def pending(replies, reply):
            replies.now += 299
            reply['gps_no_fix'] = False
            return reply
        result = Replies({'gps_loss': pending}).run()
        self.assertEqual(result['outcome'], 'inconclusive')
        self.assertEqual(result['first_failure'], 'gps_loss')

    def test_queue_timeout_and_cancellation_terminal(self):
        def timeout(_replies, _reply):
            raise queue.Empty()
        def cancelled(replies, _reply):
            return {'case': replies.phase['case'], 'token': replies.phase['token'], 'result': 'cancelled'}
        for change, outcome in ((timeout, 'timeout'), (cancelled, 'cancelled')):
            result = Replies({'phone_stopped': change}).run()
            self.assertEqual(result['outcome'], outcome)

    def test_region_mismatch_is_first_failure_after_exact_warm_restart(self):
        replies = Replies({'fresh_reconnect': {'region_matches_before': False}})
        result = replies.run()
        self.assertEqual(result['outcome'], 'failed')
        self.assertEqual(result['first_failure'], 'fresh_reconnect')
        self.assertEqual(replies.restarts, 1)

    def test_automatic_resync_or_phone_service_restart_stops_before_reconnect(self):
        for change in ({'clock_unknown': False}, {'phone_service_stopped': False}):
            replies = Replies({'clock_cleared': change})
            result = replies.run()
            self.assertEqual(result['first_failure'], 'clock_cleared')
            self.assertNotIn('fresh_reconnect', [case for case, _ in replies.reads])

    def test_restart_reserves_three_transport_calls_and_remaining_cases(self):
        for remaining, outcome in ((1079, 'timeout'), (1080, 'passed')):
            with self.subTest(remaining=remaining):
                replies = Replies()
                # The normal reply fixture takes 125s through restart_guard.
                available = 125 + remaining
                def restart(prefix):
                    self.assertEqual(available - replies.now, remaining)
                    return replies.restart(prefix)
                result = replies.run(restart=restart, available_seconds=available)
                self.assertEqual(result['outcome'], outcome)
                self.assertEqual(replies.restarts, 1 if outcome == 'passed' else 0)
                if outcome != 'passed':
                    self.assertEqual(result['first_failure'], 'warm_restart')

    def test_restart_overrun_preserves_terminal_case(self):
        replies = Replies()
        def overrun(_prefix):
            replies.now += 720
            return True
        result = replies.run(restart=overrun)
        self.assertEqual(result['first_failure'], 'warm_restart')
        self.assertEqual(result['outcome'], 'timeout')

    def test_terminal_elapsed_at_deadline_is_not_clipped(self):
        def deadline(replies, reply):
            replies.now = 2160
            return reply
        result = Replies({'baseline': deadline}).run()
        self.assertEqual(result['outcome'], 'timeout')
        self.assertEqual(result['elapsed_ms'], 2160000)
        self.assertEqual(result['cases'][0]['elapsed_ms'], 2160000)

    def test_fabricated_success_and_wrong_case_order_rejected(self):
        good = Replies().run()
        for mutate in (lambda value: value['cases'].pop(),
                       lambda value: value['cases'].reverse(),
                       lambda value: value.update(first_failure='baseline'),
                       lambda value: value['cases'][0].update(elapsed_ms=True)):
            value = copy.deepcopy(good)
            mutate(value)
            with self.assertRaises(ValueError):
                lifecycle.validate_result(value)


class ClockRestartHostReplies(Replies):
    def read(self, timeout):
        case = self.phase['case']
        self.reads.append((case, timeout))
        self.now += self.delays.get(case, 1)
        reply = {'case': case, 'token': self.phase['token']}
        reply.update({field: True for field in lifecycle.CLOCK_RESTART_HOST_FIELDS[case]})
        change = self.changes.get(case)
        if callable(change):
            return change(self, reply)
        if change is not None:
            reply.update(change)
        return reply

    def restart(self, prefix):
        lifecycle.validate_clock_restart_host_prefix(prefix)
        self.restarts += 1
        self.now += self.delays.get('warm_restart', 3)
        return True

    def run(self, restart=None, record=None, available_seconds=2160):
        return lifecycle.observe_clock_restart_host(self, restart or self.restart,
            emit=self.emit, clock=lambda: self.now, token_factory=self.token,
            available_seconds=available_seconds, on_case=record or self.completed.append)


class ClockRestartHostTests(unittest.TestCase):
    def test_complete_independent_scenario_has_one_guarded_restart_no_gps_or_retention(self):
        replies = ClockRestartHostReplies()
        result = replies.run()
        self.assertEqual(result['outcome'], 'passed')
        self.assertIs(result['host_only'], True)
        self.assertEqual([item['case'] for item in result['cases']],
                         list(lifecycle.CLOCK_RESTART_HOST_CASES))
        self.assertEqual(result['cases'], replies.completed)
        self.assertEqual(replies.restarts, 1)
        self.assertLess(result['elapsed_ms'], 120000)
        with self.assertRaises(ValueError):
            lifecycle.validate_result(result)

    def test_wrong_order_and_stale_token_refused_before_restart(self):
        for change in ({'case': 'fresh_reconnect'}, {'token': 'stale'}):
            replies = ClockRestartHostReplies({'restart_guard': change})
            result = replies.run()
            self.assertEqual(result['outcome'], 'invalid_input')
            self.assertEqual(result['first_failure'], 'restart_guard')
            self.assertEqual(replies.restarts, 0)

    def test_guard_requires_stopped_service_and_exact_boolean(self):
        for value, outcome in ((False, 'failed'), (1, 'invalid_input')):
            replies = ClockRestartHostReplies({'restart_guard': {'phone_service_stopped': value}})
            self.assertEqual(replies.run()['outcome'], outcome)
            self.assertEqual(replies.restarts, 0)

    def test_unknown_clock_and_phone_off_required_before_reconnect(self):
        for field in ('clock_unknown', 'phone_service_stopped'):
            replies = ClockRestartHostReplies({'clock_cleared': {field: False}})
            result = replies.run()
            self.assertEqual(result['outcome'], 'failed')
            self.assertEqual(result['first_failure'], 'clock_cleared')
            self.assertNotIn('fresh_reconnect', [case for case, _ in replies.reads])

    def test_fresh_ready_sync_visible_time_and_matching_region_are_each_required(self):
        for field in lifecycle.CLOCK_RESTART_HOST_FIELDS['fresh_reconnect']:
            with self.subTest(field=field):
                result = ClockRestartHostReplies({'fresh_reconnect': {field: False}}).run()
                self.assertEqual(result['outcome'], 'failed')
                self.assertEqual(result['first_failure'], 'fresh_reconnect')

    def test_baseline_sync_required_without_gps_prerequisite(self):
        replies = ClockRestartHostReplies({'baseline': {'clock_synchronized': False}})
        self.assertEqual(replies.run()['first_failure'], 'baseline')
        self.assertEqual(replies.restarts, 0)
        self.assertNotIn('gps_fix', lifecycle.CLOCK_RESTART_HOST_FIELDS['baseline'])

    def test_missing_guard_duplicate_restart_and_reordered_results_rejected(self):
        good = ClockRestartHostReplies().run()
        lifecycle.validate_clock_restart_host_prefix(good['cases'][:2])
        for prefix in ([], good['cases'][:1], good['cases'][:3],
                       list(reversed(good['cases'][:2]))):
            with self.assertRaises(ValueError):
                lifecycle.validate_clock_restart_host_prefix(prefix)
        for mutate in (lambda value: value['cases'].pop(1),
                       lambda value: value['cases'].insert(3, dict(value['cases'][2])),
                       lambda value: value['cases'].reverse()):
            result = copy.deepcopy(good)
            mutate(result)
            with self.assertRaises(ValueError):
                lifecycle.validate_clock_restart_host_result(result)

    def test_uncertain_synthetic_restart_is_never_retried(self):
        for fail in (False, RuntimeError('synthetic uncertainty')):
            replies = ClockRestartHostReplies()
            def restart(prefix):
                replies.restart(prefix)
                if isinstance(fail, Exception):
                    raise fail
                return fail
            result = replies.run(restart=restart)
            self.assertEqual(result['first_failure'], 'warm_restart')
            self.assertEqual(result['outcome'], 'failed')
            self.assertEqual(replies.restarts, 1)
            self.assertNotIn('clock_cleared', [case for case, _ in replies.reads])

    def test_each_measurement_expiry_is_terminal_and_elapsed_is_not_clipped(self):
        for case, budget in zip(lifecycle.CLOCK_RESTART_HOST_CASES,
                                lifecycle.CLOCK_RESTART_HOST_BUDGETS):
            with self.subTest(case=case):
                replies = ClockRestartHostReplies(delays={case: budget})
                result = replies.run()
                self.assertEqual(result['outcome'], 'timeout')
                self.assertEqual(result['first_failure'], case)
                previous = result['cases'][-2]['elapsed_ms'] if len(result['cases']) > 1 else 0
                self.assertEqual(result['cases'][-1]['elapsed_ms'] - previous, budget * 1000)

    def test_overall_expiry_and_restart_reserve_never_reset(self):
        replies = ClockRestartHostReplies(delays={'baseline': 10})
        self.assertEqual(replies.run(available_seconds=10)['first_failure'], 'baseline')
        replies = ClockRestartHostReplies()
        result = replies.run(available_seconds=1081)
        self.assertEqual(result['first_failure'], 'warm_restart')
        self.assertEqual(replies.restarts, 0)
        self.assertEqual(ClockRestartHostReplies().run(available_seconds=1082)['outcome'], 'passed')

    def test_queue_timeout_cancellation_and_malformed_input_release_sequence(self):
        def empty(_replies, _reply):
            raise queue.Empty()
        for change, outcome in ((empty, 'timeout'),
                                (lambda replies, reply: dict(reply, result='cancelled'), 'invalid_input'),
                                (lambda replies, reply: {'case': reply['case'],
                                 'token': reply['token'], 'result': 'cancelled'}, 'cancelled'),
                                ({'phone_service_stopped': 'yes'}, 'invalid_input')):
            replies = ClockRestartHostReplies({'restart_guard': change})
            self.assertEqual(replies.run()['outcome'], outcome)
            self.assertEqual(replies.restarts, 0)

    def test_invalid_budgets_and_backward_clock_refused(self):
        for budget in (True, float('nan'), float('inf'), 0, 2161):
            with self.assertRaises(ValueError):
                ClockRestartHostReplies().run(available_seconds=budget)
        replies = ClockRestartHostReplies({'restart_guard':
            lambda state, reply: setattr(state, 'now', -1) or reply})
        with self.assertRaises(ValueError):
            replies.run()


class EngineTests(unittest.TestCase):
    def setUp(self):
        legacy.TrialTests.setUp(self)
        self.profile_patch = mock.patch.dict(engine.CONNECTION_CANDIDATES,
            {'A_STACK_8192': engine.descriptor(self.candidate)})
        self.profile_patch.start()
        self.addCleanup(self.profile_patch.stop)
        self.request.update(schema='OT0101E-LIFECYCLE-REQUEST-1', profile='A_STACK_8192',
            case='lifecycle-and-restore', plan=copy.deepcopy(lifecycle.PLAN), approved_revision=2)

    def grant(self, operation='execute', number=1):
        raw, _ = legacy.TrialTests.grant(self, operation, number)
        obj = engine.decode(raw)
        obj.update(schema='OT0101E-LIFECYCLE-GRANT-1', expires_utc=3000,
                   actions=engine.LIFECYCLE_ACTIONS if operation == 'execute' else engine.RECOVERY_ACTIONS)
        raw = engine.canonical(obj)
        return raw, engine.sha(raw)

    def execute(self, replies=None, utc=lambda: 150):
        replies = replies or Replies()
        return engine.execute(self.root, self.request, *self.grant(), self.candidate,
            self.backend, lambda restart, record: replies.run(restart, record), utc=utc)

    def test_complete_lifecycle_uses_existing_restore_and_exact_originals(self):
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation']['outcome'], 'passed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)
        self.assertEqual(self.backend.memory, self.originals)
        journal = engine.Journal(self.root, f'{1:032x}').load()
        self.assertEqual(journal['lifecycle_cases'], result['observation']['cases'])
        self.assertTrue(journal['closed'])

    def test_old_grant_and_revision_one_refused_before_claim(self):
        raw, _ = self.grant()
        obj = engine.decode(raw)
        obj.update(schema='OT0101E-CONNECTION-GRANT-1', actions=engine.CONNECTION_ACTIONS)
        raw = engine.canonical(obj)
        with self.assertRaises(engine.TrialError):
            engine.execute(self.root, self.request, raw, engine.sha(raw), self.candidate,
                self.backend, lambda *_args: None, utc=lambda: 150)
        self.assertEqual(self.backend.calls, [])
        self.request['approved_revision'] = 1
        with self.assertRaises(engine.TrialError):
            self.execute()
        self.assertEqual(self.backend.calls, [])

    def test_old_plan_one_request_and_grant_refused_before_claim(self):
        old_request = copy.deepcopy(self.request)
        old_request['plan'].update(schema='OT0101E-LIFECYCLE-PLAN-1',
            case_seconds=[300, 60, 180, 300, 300, 30, 720, 60, 180],
            restart_reserve_seconds=960)
        raw, _ = self.grant()
        old_grant = engine.decode(raw)
        old_grant['request_sha256'] = engine.sha(engine.canonical(old_request))
        raw = engine.canonical(old_grant)
        observe = mock.Mock()
        for request, failure in ((old_request, 'request_invalid'),
                                 (self.request, 'authority_invalid')):
            with self.subTest(failure=failure):
                with self.assertRaisesRegex(engine.TrialError, failure):
                    engine.execute(self.root, request, raw, engine.sha(raw), self.candidate,
                        self.backend, observe, utc=lambda: 150)
                self.assertEqual(self.backend.calls, [])
                observe.assert_not_called()
                self.assertFalse((self.root / '.private' / engine.ACTIVE).exists())
                self.assertFalse((self.root / '.private' /
                                  f'gnss-observation-used-{1:032x}').exists())

    def test_changed_case_plan_or_other_profile_refused(self):
        for request in (dict(self.request, profile='A'),
                        dict(self.request, plan=dict(lifecycle.PLAN, observation_seconds=2161)),
                        dict(self.request, approved_revision=True)):
            with self.assertRaises(engine.TrialError):
                engine.validate_request(request)

    def test_clock_restart_host_plan_is_refused_before_claim_or_callback(self):
        request = copy.deepcopy(self.request)
        request['plan']['cases'] = list(lifecycle.CLOCK_RESTART_HOST_CASES)
        request['plan']['case_seconds'] = list(lifecycle.CLOCK_RESTART_HOST_BUDGETS)
        observe = mock.Mock()
        with self.assertRaisesRegex(engine.TrialError, 'request_invalid'):
            engine.execute(self.root, request, *self.grant(), self.candidate,
                           self.backend, observe, utc=lambda: 150)
        observe.assert_not_called()
        self.assertEqual(self.backend.calls, [])
        self.assertFalse((self.root / '.private' / engine.ACTIVE).exists())
        self.assertFalse((self.root / '.private' / f'gnss-observation-used-{1:032x}').exists())
        with self.assertRaises(ValueError):
            lifecycle.validate_result(ClockRestartHostReplies().run())

    def test_expiry_before_restart_restores_without_extra_boot(self):
        replies = Replies()
        result = self.execute(replies, utc=lambda: 3000 if replies.phase and
                              replies.phase['case'] == 'restart_guard' else 150)
        self.assertEqual(result['observation']['first_failure'], 'warm_restart')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertEqual(self.backend.memory, self.originals)

    def test_uncertain_warm_restart_is_not_retried_and_restores(self):
        original_boot = self.backend.boot_candidate
        def boot():
            value = original_boot()
            return value if self.backend.calls.count('candidate_boot') == 1 else False
        self.backend.boot_candidate = boot
        result = self.execute()
        self.assertEqual(result['observation']['first_failure'], 'warm_restart')
        self.assertEqual(result['observation']['outcome'], 'failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)
        self.assertEqual(self.backend.memory, self.originals)

    def test_failed_observation_preserved_through_restore(self):
        result = self.execute(Replies({'gps_recovery': {'clock_advanced': False}}))
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation']['first_failure'], 'gps_recovery')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)

    def test_restore_only_recovery_never_exposes_restart_callback(self):
        self.backend.fail_write = 2
        with self.assertRaises(engine.TrialError):
            self.execute()
        self.backend.fail_write = None
        count = self.backend.calls.count('candidate_boot')
        result = engine.recover(self.root, self.request, *self.grant('recover', 2),
                                self.backend, utc=lambda: 150)
        self.assertTrue(result['restored'])
        self.assertEqual(self.backend.calls.count('candidate_boot'), count)
        self.assertEqual(self.backend.memory, self.originals)

    def test_restart_before_durable_phase_prefix_is_refused(self):
        def early(restart, _record):
            restart([])
        result = engine.execute(self.root, self.request, *self.grant(), self.candidate,
            self.backend, early, utc=lambda: 150)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertTrue(result['restored'])

    def test_initial_and_warm_boot_diagnostics_retained_separately(self):
        original_boot = self.backend.boot_candidate
        def boot():
            result = original_boot()
            self.backend.startup_diagnostics = {'schema': 'OT225-STARTUP-1', 'status': 'window_complete',
                'markers': [], 'early_output_may_be_missing': True, 'bytes_read': 0,
                'elapsed_ms': 12000, 'capture_started_unix_ms': self.backend.calls.count('candidate_boot')}
            return result
        self.backend.boot_candidate = boot
        self.execute()
        state = engine.Journal(self.root, f'{1:032x}').load()
        self.assertEqual(set(state['startup_observations']), {'initial', 'warm'})
        self.assertNotEqual(state['startup_observations']['initial'], state['startup_observations']['warm'])

    def test_request_plan_types_and_second_restart_rejected(self):
        changed = copy.deepcopy(self.request)
        changed['plan']['phone_off_until_clock_cleared'] = 1
        with self.assertRaises(engine.TrialError):
            engine.validate_request(changed)
        def twice(restart, record):
            replies = Replies()
            result = replies.run(restart, record)
            with self.assertRaises(engine.TrialError):
                restart(result['cases'][:6])
            return result
        result = engine.execute(self.root, self.request, *self.grant(), self.candidate,
                                self.backend, twice, utc=lambda: 150)
        self.assertTrue(result['restored'])
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)


class ClockRestartCustodyTests(unittest.TestCase):
    """Real engine contract, exercised exclusively with synthetic host callbacks."""
    def setUp(self):
        EngineTests.setUp(self)
        self.request.update(schema=engine.CLOCK_RESTART_REQUEST, case='clock-restart-and-restore',
                            plan=engine.decode(engine.CLOCK_RESTART_PLAN_BYTES), approved_revision=1)

    def grant(self, operation='execute', number=1):
        raw, _ = EngineTests.grant(self, operation, number)
        obj = engine.decode(raw)
        obj.update(schema=engine.CLOCK_RESTART_GRANT,
                   actions=engine.CLOCK_RESTART_ACTIONS if operation == 'execute' else engine.RECOVERY_ACTIONS)
        raw = engine.canonical(obj)
        return raw, engine.sha(raw)

    def observer(self, changes=None):
        changes = changes or {}
        def observe(restart, record):
            cases = []
            elapsed = 0
            for case in engine.CLOCK_RESTART_CASES:
                elapsed += 3000 if case == 'warm_restart' else 1000
                values = {field: True for field in engine.CLOCK_RESTART_FIELDS[case]}
                values.update(changes.get(case, {}))
                status = 'passed' if all(values.values()) else 'failed'
                if case == 'warm_restart':
                    try:
                        if restart(copy.deepcopy(cases)) is not True:
                            status = 'failed'
                    except engine.TrialError:
                        status = 'failed'
                item = {'case': case, 'status': status, 'elapsed_ms': elapsed, 'observations': values}
                record(item)
                cases.append(item)
                if status != 'passed':
                    break
            return {'schema': engine.CLOCK_RESTART_OBSERVATION,
                    'request_sha256': engine.sha(engine.canonical(self.request)),
                    'outcome': cases[-1]['status'],
                    'first_failure': None if cases[-1]['status'] == 'passed' else cases[-1]['case'],
                    'cases': cases, 'elapsed_ms': elapsed}
        return observe

    def execute(self, observe=None, utc=lambda: 150, **kwargs):
        return engine.execute(self.root, self.request, *self.grant(), self.candidate,
                              self.backend, observe or self.observer(), utc=utc,
                              execution_mode=engine.CLOCK_RESTART_MODE, **kwargs)

    def assert_unclaimed(self):
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(list((self.root / '.private').glob('gnss-observation-*')), [])

    def test_distinct_exact_plan_and_explicit_selector_before_any_custody(self):
        for mode in ('retained', 'unknown', None):
            observe = mock.Mock()
            with self.assertRaisesRegex(engine.TrialError, 'execution_mode_invalid'):
                engine.execute(self.root, self.request, *self.grant(), self.candidate,
                               self.backend, observe, utc=lambda: 150, execution_mode=mode)
            observe.assert_not_called()
            self.assert_unclaimed()
        # Maintained callers omit the selector and therefore cannot admit this schema.
        with self.assertRaisesRegex(engine.TrialError, 'execution_mode_invalid'):
            engine.execute(self.root, self.request, *self.grant(), self.candidate,
                           self.backend, mock.Mock(), utc=lambda: 150)
        self.assert_unclaimed()
        old = dict(self.request, schema='OT0101E-LIFECYCLE-REQUEST-1',
                   case='lifecycle-and-restore', plan=copy.deepcopy(lifecycle.PLAN), approved_revision=2)
        with self.assertRaisesRegex(engine.TrialError, 'execution_mode_invalid'):
            engine.execute(self.root, old, *self.grant(), self.candidate, self.backend,
                           mock.Mock(), execution_mode=engine.CLOCK_RESTART_MODE)
        self.assert_unclaimed()

    def test_plan_and_revision_mutations_rejected_without_receipt(self):
        original = copy.deepcopy(self.request)
        for mutate in (lambda obj: obj['plan']['cases'].reverse(),
                       lambda obj: obj['plan'].update(schema=lifecycle.PLAN['schema']),
                       lambda obj: obj['plan'].update(phone_off_until_clock_cleared=1),
                       lambda obj: obj['plan']['case_seconds'].__setitem__(1, 31),
                       lambda obj: obj.update(approved_revision=2),
                       lambda obj: obj.update(approved_revision=True)):
            self.request = copy.deepcopy(original)
            mutate(self.request)
            with self.assertRaisesRegex(engine.TrialError, 'request_invalid'):
                self.execute()
            self.assert_unclaimed()
        self.assertEqual(engine.decode(engine.CLOCK_RESTART_PLAN_BYTES), original['plan'])

    def test_grant_schema_actions_request_runtime_and_raw_hash_must_match(self):
        original_raw, pin = self.grant()
        mutations = (
            {'schema': 'OT0101E-LIFECYCLE-GRANT-1'}, {'actions': engine.LIFECYCLE_ACTIONS},
            {'request_sha256': '0' * 64}, {'runtime_sha256': '0' * 64},
            {'attempt_count': 2}, {'expires_utc': 150})
        for change in mutations:
            obj = engine.decode(original_raw)
            obj.update(change)
            raw = engine.canonical(obj)
            with self.assertRaisesRegex(engine.TrialError, 'authority_invalid'):
                engine.execute(self.root, self.request, raw, engine.sha(raw), self.candidate,
                    self.backend, mock.Mock(), utc=lambda: 150, execution_mode=engine.CLOCK_RESTART_MODE)
            self.assert_unclaimed()
        with self.assertRaisesRegex(engine.TrialError, 'authority_invalid'):
            engine.execute(self.root, self.request, original_raw, '0' * 64, self.candidate,
                self.backend, mock.Mock(), utc=lambda: 150, execution_mode=engine.CLOCK_RESTART_MODE)
        self.assert_unclaimed()

    def test_complete_real_envelope_restores_all_exact_originals_and_closes_custody(self):
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation']['outcome'], 'passed')
        self.assertEqual(result['observation']['schema'], engine.CLOCK_RESTART_OBSERVATION)
        self.assertEqual(self.backend.memory, self.originals)
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)
        state = engine.Journal(self.root, f'{1:032x}').load()
        self.assertEqual(state['lifecycle_cases'], result['observation']['cases'])
        self.assertEqual(set(state['captures']), set(engine.SPANS))
        self.assertTrue(state['closed'])
        self.assertFalse((self.root / '.private' / engine.ACTIVE).exists())
        self.assertIn('clock_restart_observed', state['events'])
        with self.assertRaises(ValueError):
            lifecycle.validate_result(result['observation'])

    def test_restart_requires_two_passed_durable_rows_before_boot(self):
        def missing(restart, _record):
            restart([])
        result = self.execute(missing)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertEqual(self.backend.memory, self.originals)

    def test_supplied_prefix_cannot_replace_journaled_prefix(self):
        def mismatched(restart, record):
            prefix = []
            for case in engine.CLOCK_RESTART_CASES[:2]:
                row = {'case': case, 'status': 'passed', 'elapsed_ms': len(prefix) * 1000 + 1000,
                       'observations': {field: True for field in engine.CLOCK_RESTART_FIELDS[case]}}
                record(row)
                prefix.append(copy.deepcopy(row))
            prefix[0]['elapsed_ms'] = 999
            restart(prefix)
        result = self.execute(mismatched)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)

    def test_restart_loads_durable_prefix_and_copies_observer_rows(self):
        def observe(restart, record):
            def durable_record(row):
                record(row)
                persisted = engine.Journal(self.root, f'{1:032x}').load()['lifecycle_cases']
                self.assertEqual(persisted[-1], row)
                row['observations']['unexpected'] = True
            # Mutating the caller's row after record must not alter durable custody.
            value = self.observer()(restart, lambda row: durable_record(copy.deepcopy(row)))
            return value
        self.assertEqual(self.execute(observe)['observation']['outcome'], 'passed')

    def test_repeated_restart_is_refused_even_with_original_prefix(self):
        def twice(restart, record):
            def checked_restart(prefix):
                self.assertTrue(restart(prefix))
                # Still at the same durable two-row prefix: only one-use rejects this.
                with self.assertRaisesRegex(engine.TrialError, 'restart_already_used'):
                    restart(prefix)
                return True
            return self.observer()(checked_restart, record)
        self.assertEqual(self.execute(twice)['observation']['outcome'], 'passed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)

    def test_uncertain_restart_is_never_retried_and_restores(self):
        original = self.backend.boot_candidate
        def boot():
            original()
            return self.backend.calls.count('candidate_boot') == 1
        self.backend.boot_candidate = boot
        result = self.execute()
        self.assertEqual(result['observation']['first_failure'], 'warm_restart')
        self.assertEqual(result['observation']['outcome'], 'failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 2)
        self.assertEqual(self.backend.memory, self.originals)

    def test_unknown_clock_or_phone_running_stops_before_reconnect(self):
        for field in ('clock_unknown', 'phone_service_stopped'):
            # Pure admission checks cover independent values without repeating custody trials.
            value = self.observer({'clock_cleared': {field: False}})(lambda _: True, lambda _: None)
            engine.validate_clock_restart_result(value, self.request)
            self.assertEqual(value['first_failure'], 'clock_cleared')
            self.assertNotIn('fresh_reconnect', [item['case'] for item in value['cases']])
        value = self.execute(self.observer({'clock_cleared': {'clock_unknown': False}}))
        self.assertEqual(value['observation']['first_failure'], 'clock_cleared')
        self.assertEqual(self.backend.memory, self.originals)

    def test_order_missing_guard_duplicate_restart_false_unknown_and_nonbool_rejected(self):
        good = self.observer()(lambda _: True, lambda _: None)
        for mutate in (lambda value: value['cases'].pop(1),
                       lambda value: value['cases'].insert(3, copy.deepcopy(value['cases'][2])),
                       lambda value: value['cases'].reverse(),
                       lambda value: value['cases'][3]['observations'].update(clock_unknown=False),
                       lambda value: value['cases'][1]['observations'].update(phone_service_stopped=1),
                       lambda value: value.update(request_sha256='0' * 64)):
            value = copy.deepcopy(good)
            mutate(value)
            with self.assertRaises(engine.TrialError):
                engine.validate_clock_restart_result(value, self.request)

    def test_fabricated_warm_pass_without_engine_restart_restores_as_failed(self):
        def fabricate(_restart, record):
            return self.observer()(lambda _: True, record)
        result = self.execute(fabricate)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertEqual(self.backend.memory, self.originals)

    def test_host_only_result_and_rows_are_not_real_admission(self):
        host = ClockRestartHostReplies().run()
        with self.assertRaises(engine.TrialError):
            engine.validate_clock_restart_result(host, self.request)
        rewritten = {key: value for key, value in host.items() if key not in ('host_only', 'scenario')}
        rewritten.update(schema=engine.CLOCK_RESTART_OBSERVATION,
                         request_sha256=engine.sha(engine.canonical(self.request)))
        with self.assertRaises(engine.TrialError):
            engine.validate_clock_restart_result(rewritten, self.request)
        result = self.execute(lambda _restart, _record: host)
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.memory, self.originals)

    def test_case_and_overall_expiry_cannot_be_passed(self):
        good = self.observer()(lambda _: True, lambda _: None)
        for index, budget in enumerate(engine.CLOCK_RESTART_BUDGETS):
            value = copy.deepcopy(good)
            previous = value['cases'][index - 1]['elapsed_ms'] if index else 0
            value['cases'][index]['elapsed_ms'] = previous + budget * 1000
            # Validate this prefix to isolate each stage deadline.
            with self.assertRaises(engine.TrialError):
                engine.validate_clock_restart_cases(value['cases'][:index + 1])
        good['elapsed_ms'] = 2160000
        with self.assertRaises(engine.TrialError):
            engine.validate_clock_restart_result(good, self.request)

    def test_restart_grant_reserve_and_expiry_guards_preserved(self):
        # 1079s covers transport alone, but cannot cover the pinned 1080s reserve.
        result = self.execute(utc=lambda: 1921)
        self.assertEqual(result['observation']['first_failure'], 'warm_restart')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertEqual(self.backend.memory, self.originals)

    def test_expiry_after_durable_rows_before_return_is_not_admitted(self):
        current = [150]
        def delayed(restart, record):
            value = self.observer()(restart, record)
            current[0] = 3000
            return value
        result = self.execute(delayed, utc=lambda: current[0])
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.memory, self.originals)

    def test_failed_guard_journal_save_cannot_invoke_restart(self):
        save = engine.Journal.save
        def fail_guard(journal, state):
            if len(state.get('lifecycle_cases', [])) == 2:
                raise OSError('synthetic durable save failure')
            return save(journal, state)
        with mock.patch.object(engine.Journal, 'save', fail_guard):
            with self.assertRaisesRegex(engine.TrialError, 'custody_held_recovery_required'):
                self.execute()
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertTrue((self.root / '.private' / engine.ACTIVE).exists())

    def test_expiry_during_observation_restores_without_restart(self):
        current = [150]
        def observe(restart, record):
            current[0] = 3000
            return self.observer()(restart, record)
        result = self.execute(observe, utc=lambda: current[0])
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.calls.count('candidate_boot'), 1)
        self.assertEqual(self.backend.memory, self.originals)

    def test_restoration_failure_holds_custody_and_fresh_mode_bound_recovery_restores(self):
        self.backend.fail_write = 2
        with self.assertRaisesRegex(engine.TrialError, 'custody_held_recovery_required'):
            self.execute()
        self.assertTrue((self.root / '.private' / engine.ACTIVE).exists())
        self.backend.fail_write = None
        count = self.backend.calls.count('candidate_boot')
        with self.assertRaisesRegex(engine.TrialError, 'execution_mode_invalid'):
            engine.recover(self.root, self.request, *self.grant('recover', 2), self.backend, utc=lambda: 150)
        result = engine.recover(self.root, self.request, *self.grant('recover', 2), self.backend,
                                utc=lambda: 150, execution_mode=engine.CLOCK_RESTART_MODE)
        self.assertTrue(result['restored'])
        self.assertEqual(self.backend.calls.count('candidate_boot'), count)
        self.assertEqual(self.backend.memory, self.originals)
        self.assertFalse((self.root / '.private' / engine.ACTIVE).exists())


class BindingTests(unittest.TestCase):
    put = connection_tests.BindingTests.put
    # Synthetic fixture identities, never read from a device.
    IDENTITIES = {'OT-DEV-001': '001122334455', 'OT-DEV-002': '001122334466'}

    def setUp(self):
        connection_tests.BindingTests.setUp(self)
        self.binding.update(schema='OT0101E-LIFECYCLE-OPERATOR-BINDING-1', profile='A_STACK_8192')
        for name in runner.REQUIRED_FILES:
            self.put(name, name.encode())
        patch = mock.patch.dict(engine.CONNECTION_CANDIDATES,
                               {'A_STACK_8192': engine.descriptor(b'AAA')})
        patch.start()
        self.addCleanup(patch.stop)

    def verify(self, recovery=False):
        raw = engine.canonical(self.binding)
        self.binding_path.write_bytes(raw)
        return runner.verify_binding(self.root, self.binding_path, engine.sha(raw), recovery=recovery)

    def select(self, inventory_id):
        self.binding['schema'] = 'OT0101E-LIFECYCLE-OPERATOR-BINDING-' + (
            '1' if inventory_id is None else '2')
        self.binding.pop('inventory_id', None)
        if inventory_id is not None:
            self.binding['inventory_id'] = inventory_id

    def main_args(self, identity, *, mode='execute', ephemeral_identity=None):
        # Put Bench 2 first so selecting Bench 1 cannot rely on registry order.
        self.put('.private/registry.json', engine.canonical({'devices': [
            {'inventory_id': name, 'esp32s3_base_mac': self.IDENTITIES[name]}
            for name in sorted(self.IDENTITIES, reverse=True)]}))
        raw = engine.canonical(self.binding)
        self.binding_path.write_bytes(raw)
        pin = engine.sha(raw)
        _, _, _, transport = runner.modules()
        key = b'k' * 32
        request = {'schema': 'OT0101E-LIFECYCLE-REQUEST-1', 'profile': 'A_STACK_8192',
            'runtime_sha256': pin, 'device_binding': transport.opaque_identity(key, identity),
            'candidate': dict(engine.CONNECTION_CANDIDATES['A_STACK_8192']),
            'protected': {
                'bootloader': {'bytes': 32768, 'sha256': 'a' * 64},
                'partition': {'bytes': 4096, 'sha256': engine.PARTITION_SHA},
                'ota': {'bytes': 8192, 'sha256': engine.OTA_SHA}},
            'original_prefix': {'bytes': 589824, 'sha256': 'b' * 64},
            'boot_compatibility': 'reviewed-ot0101e-image-header-compatibility',
            'case': 'lifecycle-and-restore', 'plan': copy.deepcopy(lifecycle.PLAN),
            'approved_revision': 2}
        request_raw = engine.canonical(request)
        grant_raw = engine.canonical({'schema': 'OT0101E-LIFECYCLE-GRANT-1',
            'attempt': 'c' * 32, 'request_sha256': engine.sha(request_raw),
            'runtime_sha256': pin, 'operation': mode,
            'origin_attempt': 'd' * 32 if mode == 'recover' else None, 'attempt_count': 1,
            'actions': engine.RECOVERY_ACTIONS if mode == 'recover' else engine.LIFECYCLE_ACTIONS,
            'issued_utc': 100, 'expires_utc': 3000})
        ephemeral_raw = engine.canonical({'route': 'COM42',
            'identity': identity if ephemeral_identity is None else ephemeral_identity,
            'binding_key': key.hex()})
        args = [mode, '--binding', str(self.binding_path), '--binding-sha256', pin,
                '--original-control-verified', '--acceptance-revision-verified', '2']
        for name, value in (('request', request_raw), ('grant', grant_raw), ('ephemeral', ephemeral_raw)):
            path = self.root / '.private' / (name + '.json')
            path.write_bytes(value)
            args.extend(['--' + name, str(path), '--' + name + '-sha256', engine.sha(value)])
        return args

    @contextmanager
    def main_admission(self):
        modules = runner.modules()
        transport = modules[3]
        isolated = SimpleNamespace(flags=SimpleNamespace(isolated=True, no_site=True),
                                   dont_write_bytecode=True, stdin=None)
        with (mock.patch.object(runner, '__file__', str(self.root / 'tools/run_gnss_lifecycle_trial.py')),
              mock.patch.object(runner, 'modules', return_value=modules),
              mock.patch.object(runner, 'sys', isolated),
              mock.patch.object(runner.time, 'time', return_value=150),
              mock.patch.object(transport.Transport, 'claim',
                                side_effect=AssertionError('hardware claim prohibited')) as claim,
              mock.patch.object(transport, 'Transport', wraps=transport.Transport) as construct,
              mock.patch.object(engine, 'execute', return_value={'test_only': True}) as execute,
              mock.patch.object(engine, 'recover', return_value={'test_only': True}) as recover,
              mock.patch('builtins.print')):
            yield SimpleNamespace(construct=construct, execute=execute, recover=recover, claim=claim)

    def test_exact_lifecycle_inputs_and_distinct_binding(self):
        self.assertEqual(self.verify()['profile'], 'A_STACK_8192')
        self.binding['schema'] = 'OT0101E-CONNECTION-OPERATOR-BINDING-1'
        with self.assertRaises(ValueError):
            self.verify()

    def test_explicit_schema_requires_allowlisted_inventory_id(self):
        self.select('OT-DEV-001')
        for inventory_id in ('OT-DEV-001', 'OT-DEV-002'):
            self.binding['inventory_id'] = inventory_id
            self.assertEqual(self.verify()['inventory_id'], inventory_id)
        for inventory_id in ('OT-DEV-003', '', None, True, 1, []):
            with self.subTest(inventory_id=inventory_id):
                self.binding['inventory_id'] = inventory_id
                with self.assertRaises(ValueError):
                    self.verify()
        del self.binding['inventory_id']
        with self.assertRaises(ValueError):
            self.verify()
        self.select(None)
        self.binding['inventory_id'] = 'OT-DEV-001'
        with self.assertRaises(ValueError):
            self.verify()  # A schema-1 binding cannot override its fixed device.

    def test_execute_uses_explicit_selection_and_legacy_bench_two(self):
        for selected, expected in ((None, 'OT-DEV-002'), ('OT-DEV-001', 'OT-DEV-001'),
                                   ('OT-DEV-002', 'OT-DEV-002')):
            with self.subTest(selected=selected):
                self.select(selected)
                args = self.main_args(self.IDENTITIES[expected])
                with self.main_admission() as calls:
                    runner.main(args)
                    self.assertEqual(calls.construct.call_args.kwargs['expected_identity'],
                                     self.IDENTITIES[expected])
                    calls.construct.assert_called_once()
                    calls.execute.assert_called_once()
                    calls.recover.assert_not_called()
                    calls.claim.assert_not_called()

    def test_wrong_or_unknown_selection_refused_before_transport(self):
        for selected, other in ((None, 'OT-DEV-001'), ('OT-DEV-001', 'OT-DEV-002'),
                                ('OT-DEV-003', 'OT-DEV-001')):
            with self.subTest(selected=selected):
                self.select(selected)
                args = self.main_args(self.IDENTITIES[other])
                with self.main_admission() as calls:
                    with self.assertRaises(ValueError):
                        runner.main(args)
                    calls.construct.assert_not_called()
                    calls.execute.assert_not_called()
                    calls.recover.assert_not_called()
                    calls.claim.assert_not_called()

    def test_changed_selection_cannot_reuse_request_or_grant(self):
        for mode in ('execute', 'recover'):
            with self.subTest(mode=mode):
                self.select('OT-DEV-001')
                args = self.main_args(self.IDENTITIES['OT-DEV-001'], mode=mode)
                self.binding['inventory_id'] = 'OT-DEV-002'
                changed = engine.canonical(self.binding)
                self.binding_path.write_bytes(changed)
                args[args.index('--binding-sha256') + 1] = engine.sha(changed)
                with self.main_admission() as calls:
                    with self.assertRaises(ValueError):
                        runner.main(args)
                    calls.construct.assert_not_called()
                    calls.execute.assert_not_called()
                    calls.recover.assert_not_called()
                    calls.claim.assert_not_called()

    def test_recovery_retains_original_target_without_live_registry(self):
        for selected, expected in ((None, 'OT-DEV-002'), ('OT-DEV-001', 'OT-DEV-001')):
            with self.subTest(selected=selected):
                self.select(selected)
                args = self.main_args(self.IDENTITIES[expected], mode='recover')
                for relative in ('build/a.bin', '.private/app.apk', '.private/registry.json'):
                    path = self.root / relative
                    if path.exists():
                        path.unlink()
                with self.main_admission() as calls:
                    runner.main(args)
                    kwargs = calls.construct.call_args.kwargs
                    self.assertEqual(kwargs['expected_identity'], self.IDENTITIES[expected])
                    self.assertTrue(kwargs['recovery_only'])
                    self.assertFalse(kwargs['startup_diagnostics'])
                    calls.recover.assert_called_once()
                    calls.execute.assert_not_called()
                    calls.claim.assert_not_called()

    def test_recovery_rejects_changed_ephemeral_identity_before_claim(self):
        self.select('OT-DEV-001')
        args = self.main_args(self.IDENTITIES['OT-DEV-001'], mode='recover',
                              ephemeral_identity=self.IDENTITIES['OT-DEV-002'])
        _, _, _, transport = runner.modules()
        with self.main_admission() as calls:
            with self.assertRaises(transport.TransportError):
                runner.main(args)
            calls.construct.assert_called_once()
            calls.recover.assert_not_called()
            calls.execute.assert_not_called()
            calls.claim.assert_not_called()

    def test_missing_lifecycle_source_and_changed_candidate_refused(self):
        del self.files['tools/gnss_lifecycle_observation.py']
        with self.assertRaises(ValueError):
            self.verify()
        self.put('tools/gnss_lifecycle_observation.py', b'exact')
        self.put('build/a.bin', b'BBB')
        with self.assertRaises(ValueError):
            self.verify()

    def test_recovery_does_not_require_candidate_apk_or_live_registry(self):
        for relative in ('build/a.bin', '.private/app.apk', '.private/registry.json'):
            (self.root / relative).unlink()
        self.assertEqual(self.verify(recovery=True)['profile'], 'A_STACK_8192')
        with self.assertRaises(FileNotFoundError):
            self.verify()

    def test_registry_identity_helper_must_be_pinned_and_unchanged(self):
        relative = 'tools/gnss_counter_capture.py'
        pin = self.files.pop(relative)
        with self.assertRaises(ValueError):
            self.verify()
        self.files[relative] = pin
        (self.root / relative).write_bytes(b'changed executable helper')
        with self.assertRaises(ValueError):
            self.verify()


if __name__ == '__main__':
    unittest.main()
