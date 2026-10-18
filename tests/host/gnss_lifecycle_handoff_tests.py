"""No-device confirmation regressions, including the actual stdin reader/producer."""
from pathlib import Path
import copy
import json
import subprocess
import sys
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests' / 'host'))
import gnss_lifecycle_handoff as handoff
import gnss_lifecycle_observation as lifecycle
from gnss_lifecycle_trial_tests import Replies


def cp(case, token='1' * 32, *, emitted_at=0):
    return {'schema': handoff.CHECKPOINT, 'case': case, 'token': token,
            'seconds_available': lifecycle.BUDGETS[lifecycle.CASES.index(case)],
            'deadline_monotonic': emitted_at + lifecycle.BUDGETS[lifecycle.CASES.index(case)],
            'instruction': 'No-device test'}


def history(count):
    return [{'case': case, 'status': 'passed', 'elapsed_ms': (index + 1) * 130000}
            for index, case in enumerate(lifecycle.CASES[:count])]


def ack(current, cases):
    return {'schema': handoff.ACK, 'token': current['token'],
            **cases[lifecycle.CASES.index(current['case'])]}


class HandoffTests(unittest.TestCase):
    def setup_handoff(self, case='phone_stopped'):
        self.now = 0
        current = cp(case)
        guard = handoff.Handoff(current, clock=lambda: self.now)
        line = guard.prepare({field: True for field in lifecycle.FIELDS[case]})
        self.assertTrue(line.endswith('\n'))
        return guard, current, line

    def test_echo_cannot_accept_or_start_hold(self):
        guard, current, line = self.setup_handoff()
        with self.assertRaises(ValueError):
            guard.confirm(json.loads(line), cp('phone_disconnected', '2' * 32), history(2))
        self.assertIsNone(guard.accepted_at)
        self.assertIsNone(guard.next_received_at)

    def test_real_output_echo_fragments_and_ack_order(self):
        guard, current, line = self.setup_handoff()
        accepted = json.dumps(ack(current, history(2))) + '\n'
        next_line = json.dumps(cp('phone_disconnected', '2' * 32)) + '\n'
        for output in (line, line + accepted, next_line + accepted,
                       line + accepted + next_line[:-1], accepted + accepted + next_line):
            with self.assertRaises(ValueError): guard.confirm_output(output, history(2))
            self.assertIsNone(guard.accepted_at)
        following = guard.confirm_output('\x1b[?9001h' + line + accepted + next_line, history(2))
        self.assertEqual(following.current['case'], 'phone_disconnected')

    def test_ack_with_wrong_type_or_unbounded_output_refused(self):
        guard, current, _ = self.setup_handoff()
        for value in (True, 260000.0):
            accepted = ack(current, history(2)); accepted['elapsed_ms'] = value
            with self.assertRaises(ValueError): guard.confirm(accepted, cp('phone_disconnected', '2' * 32), history(2))
        with self.assertRaises(ValueError): guard.confirm_output('x' * 32769, history(2))

    def test_ack_requires_matching_durable_pass_and_fresh_next_case(self):
        for change in ('token', 'case', 'status', 'durable', 'next', 'next_token'):
            with self.subTest(change=change):
                guard, current, _ = self.setup_handoff()
                cases = history(2); accepted = ack(current, cases); following = cp('phone_disconnected', '2' * 32)
                if change == 'token': accepted['token'] = '3' * 32
                if change == 'case': accepted['case'] = 'baseline'
                if change == 'status': accepted['status'] = 'timeout'
                if change == 'durable': cases[-1]['elapsed_ms'] += 1
                if change == 'next': following = cp('gps_loss', '2' * 32)
                if change == 'next_token': following['token'] = current['token']
                with self.assertRaises(ValueError): guard.confirm(accepted, following, cases)
                self.assertIsNone(guard.accepted_at)

    def test_delayed_ack_starts_hold_at_receipt_not_submission(self):
        guard, current, _ = self.setup_handoff()
        self.now = 20
        following = guard.confirm(ack(current, history(2)), cp('phone_disconnected', '2' * 32), history(2))
        fields = {field: True for field in lifecycle.FIELDS['phone_disconnected']}
        self.now = 139.999
        with self.assertRaises(ValueError): following.prepare(fields)
        self.now = 140
        self.assertTrue(following.prepare(fields).endswith('\n'))

    def test_no_duplicate_submission_or_ack(self):
        guard, current, _ = self.setup_handoff()
        with self.assertRaises(ValueError): guard.prepare({field: True for field in lifecycle.FIELDS['phone_stopped']})
        accepted = ack(current, history(2)); following = cp('phone_disconnected', '2' * 32)
        guard.confirm(accepted, following, history(2))
        with self.assertRaises(ValueError): guard.confirm(accepted, following, history(2))

    def test_expired_or_backward_submission_refused(self):
        for now in (-1, 180, 181):
            current = cp('phone_stopped'); self.now = 0
            guard = handoff.Handoff(current, clock=lambda: self.now); self.now = now
            with self.assertRaises(ValueError): guard.prepare({field: True for field in lifecycle.FIELDS['phone_stopped']})

    def test_false_private_or_non_boolean_fields_cannot_pass(self):
        for fields in ({'phone_service_stopped': False, 'clock_visible': True},
                       {'phone_service_stopped': 1, 'clock_visible': True},
                       {'phone_service_stopped': True, 'clock_visible': True, 'private': 'forbidden'}):
            guard = handoff.Handoff(cp('phone_stopped'), clock=lambda: self.now)
            with self.assertRaises(ValueError): guard.prepare(fields)

    def test_restart_requires_automatic_durable_pass(self):
        guard, current, _ = self.setup_handoff('restart_guard')
        accepted = ack(current, history(6)); following = cp('clock_cleared', '2' * 32)
        with self.assertRaises(ValueError): guard.confirm(accepted, following, history(6))
        self.assertEqual(guard.confirm(accepted, following, history(7)).current['case'], 'clock_cleared')

    def test_delayed_begin_never_rebases_controller_deadline(self):
        current = cp('baseline')
        self.now = 31
        guard = handoff.Handoff(current, clock=lambda: self.now)
        self.now = 320
        with self.assertRaises(ValueError):
            guard.prepare({field: True for field in lifecycle.FIELDS['baseline']})
        with self.assertRaises(ValueError):
            handoff.Handoff(current, clock=lambda: self.now)

    def test_expired_following_checkpoint_cannot_be_confirmed(self):
        guard, current, _ = self.setup_handoff()
        self.now = 301
        with self.assertRaises(ValueError):
            guard.confirm(ack(current, history(2)), cp('phone_disconnected', '2' * 32), history(2))
        self.assertIsNone(guard.accepted_at)

    def test_receipt_hold_never_extends_absolute_upper_deadline(self):
        self.now = 200
        guard = handoff.Handoff(cp('phone_disconnected'), clock=lambda: self.now)
        self.now = 320
        with self.assertRaises(ValueError):
            guard.prepare({field: True for field in lifecycle.FIELDS['phone_disconnected']})

    def test_completed_restart_ack_can_outlive_old_case_deadline(self):
        guard, current, _ = self.setup_handoff('restart_guard')
        self.now = 600
        following = cp('clock_cleared', '2' * 32, emitted_at=600)
        self.assertEqual(guard.confirm(ack(current, history(6)), following, history(7)).current, following)

    def test_old_schema_and_invalid_deadline_refused(self):
        for deadline in (None, True, float('nan'), float('inf'), 0):
            current = cp('baseline'); current['deadline_monotonic'] = deadline
            with self.assertRaises(ValueError): handoff.Handoff(current, clock=lambda: 0)
        current = cp('baseline'); current['schema'] = 'OT0101E-LIFECYCLE-CHECKPOINT-1'
        with self.assertRaises(ValueError): handoff.Handoff(current, clock=lambda: 0)

    def test_separate_process_monotonic_uses_same_host_clock(self):
        info = time.get_clock_info('monotonic')
        self.assertTrue(info.monotonic); self.assertFalse(info.adjustable)
        before = time.monotonic()
        output = subprocess.check_output([sys.executable, '-I', '-S', '-B', '-c',
            'import time; print(time.monotonic())'], text=True, timeout=10)
        after = time.monotonic()
        self.assertLessEqual(before, float(output)); self.assertLessEqual(float(output), after)

    def test_final_ack_has_no_invented_checkpoint_or_restoration_claim(self):
        guard, current, _ = self.setup_handoff('fresh_reconnect')
        self.assertIsNone(guard.confirm(ack(current, history(9)), None, history(9)))

    def test_producer_ack_only_after_durable_pass_never_for_auto_restart(self):
        replies = Replies(); messages = []; durable = []
        original_emit = replies.emit
        def emit(raw, **kwargs):
            value = json.loads(raw)
            if value['schema'] == handoff.CHECKPOINT:
                budget = lifecycle.BUDGETS[lifecycle.CASES.index(value['case'])]
                self.assertEqual(value['deadline_monotonic'], min(2160, replies.now + budget))
            if value['schema'] == handoff.ACK:
                self.assertEqual(durable[-1], {key: value[key] for key in ('case', 'status', 'elapsed_ms')})
            messages.append(value); original_emit(raw, **kwargs)
        replies.emit = emit
        self.assertEqual(replies.run(record=durable.append)['outcome'], 'passed')
        self.assertEqual([m['case'] for m in messages if m['schema'] == handoff.ACK], list(lifecycle.FIELDS))

    def test_pending_and_terminal_replies_emit_no_passed_ack(self):
        replies = Replies({'phone_stopped': {'clock_visible': False}}); messages = []
        original_emit = replies.emit
        replies.emit = lambda raw, **kwargs: (messages.append(json.loads(raw)), original_emit(raw, **kwargs))
        result = replies.run()
        self.assertEqual(result['first_failure'], 'phone_stopped')
        self.assertEqual([m['case'] for m in messages if m['schema'] == handoff.ACK], ['baseline'])

    def test_actual_reader_and_producer_complete_with_guarded_handoffs(self):
        # Actual subprocess stdin/Lines and observe; only clock/restart are simulated.
        code = '''import sys,json
sys.path[:0]=sys.argv[1:3]
import gnss_lifecycle_observation as l
from run_gnss_observation_trial import Lines
now=[0]; durable=[]; raw=Lines(sys.stdin)
class Input:
 def read(self,timeout):
  reply=raw.read(timeout);now[0]+=120 if reply['case']=='phone_disconnected' else 1;return reply
def record(case):
 durable.append(case);print(json.dumps({'durable':durable}),flush=True)
result=l.observe(Input(),lambda prefix:True,clock=lambda:now[0],on_case=record)
print(json.dumps({'result':result}),flush=True)
'''
        process = subprocess.Popen([sys.executable, '-I', '-S', '-B', '-u', '-c', code,
                                    str(ROOT / 'tools'), str(ROOT / 'tests' / 'host')],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True)
        watchdog = threading.Timer(10, process.kill)
        watchdog.start()
        guard = None; accepted = None; cases = []; count = 0
        try:
            while True:
                line = process.stdout.readline()
                self.assertTrue(line, 'child exited before result')
                value = json.loads(line)
                if 'durable' in value: cases = value['durable']; continue
                if value.get('schema') == handoff.ACK: accepted = value; count += 1; continue
                if value.get('schema') == handoff.CHECKPOINT:
                    if guard is None: guard = handoff.Handoff(value, clock=lambda: self.now)
                    else: guard = guard.confirm(accepted, value, cases)
                    if value['case'] == 'phone_disconnected': self.now += 120
                    reply = guard.prepare({field: True for field in lifecycle.FIELDS[value['case']]})
                    process.stdin.write(reply); process.stdin.flush(); accepted = None
                elif 'result' in value:
                    self.assertIsNone(guard.confirm(accepted, None, cases))
                    self.assertEqual(value['result']['outcome'], 'passed'); break
            process.stdin.close(); self.assertEqual(process.wait(timeout=10), 0)
            self.assertEqual(count, 8)
        finally:
            watchdog.cancel()
            if process.poll() is None: process.kill(); process.wait(timeout=10)
            if not process.stdin.closed: process.stdin.close()
            process.stdout.close(); process.stderr.close()

    def setUp(self):
        self.now = 0


if __name__ == '__main__':
    unittest.main()
