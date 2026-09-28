"""Host-only lifecycle sequencing, authority and real custody-engine tests."""
from pathlib import Path
import copy
import json
import queue
import sys
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
    def __init__(self, changes=None):
        self.now = 0
        self.phase = None
        self.changes = changes or {}
        self.reads = []
        self.restarts = 0
        self.counter = 0
        self.completed = []

    def emit(self, raw, **_kwargs):
        self.phase = json.loads(raw)

    def read(self, timeout):
        case = self.phase['case']
        self.reads.append((case, timeout))
        self.now += 120 if case == 'phone_disconnected' else 1
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
        def early(replies, reply):
            replies.now -= 119
            return reply
        result = Replies({'phone_disconnected': early}).run()
        self.assertEqual(result['outcome'], 'invalid_input')
        self.assertEqual(result['first_failure'], 'phone_disconnected')

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
        replies = Replies()
        result = replies.run(available_seconds=1000)
        self.assertEqual(result['outcome'], 'timeout')
        self.assertEqual(result['first_failure'], 'warm_restart')
        self.assertEqual(replies.restarts, 0)

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

    def test_changed_case_plan_or_other_profile_refused(self):
        for request in (dict(self.request, profile='A'),
                        dict(self.request, plan=dict(lifecycle.PLAN, observation_seconds=2161)),
                        dict(self.request, approved_revision=True)):
            with self.assertRaises(engine.TrialError):
                engine.validate_request(request)

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


class BindingTests(unittest.TestCase):
    put = connection_tests.BindingTests.put

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

    def test_exact_lifecycle_inputs_and_distinct_binding(self):
        self.assertEqual(self.verify()['profile'], 'A_STACK_8192')
        self.binding['schema'] = 'OT0101E-CONNECTION-OPERATOR-BINDING-1'
        with self.assertRaises(ValueError):
            self.verify()

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
