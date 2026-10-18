"""Persistent operator workflow tests. No device, authorization or serial access."""
import contextlib
import io
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests' / 'host')]
import gnss_lifecycle_handoff_cli as cli
import gnss_lifecycle_handoff as handoff
import gnss_lifecycle_observation as lifecycle
from gnss_lifecycle_handoff_tests import cp, ack, history


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name); self.now = 0
        self.current = cp('baseline')
        self.data = {'schema': 'OT0101E-GNSS-JOURNAL-1', 'attempt': 'a' * 32,
                     'request': {'runtime_sha256': 'b' * 64}, 'closed': False, 'lifecycle_cases': []}
        self.put('checkpoint', self.current); self.put('journal', self.data)

    def tearDown(self):
        self.temp.cleanup()

    def put(self, name, value):
        (self.root / name).write_text(json.dumps(value), encoding='utf-8')

    def run_cli(self, mode, *args, state='state'):
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            cli.main([mode, '--state', str(self.root / state), *args], clock=lambda: self.now)
        return stdout.getvalue()

    def begin(self):
        return self.run_cli('begin', '--checkpoint', str(self.root / 'checkpoint'), '--journal', str(self.root / 'journal'))

    def prepare(self, state='state', case='baseline'):
        self.put('fields', {field: True for field in lifecycle.FIELDS[case]})
        return self.run_cli('prepare', '--fields', str(self.root / 'fields'), state=state)

    def output(self, current, count, next_case, token):
        self.data['lifecycle_cases'] = history(count); self.put('journal', self.data)
        (self.root / 'output').write_text(json.dumps(ack(current, history(count))) + '\n' +
                                        json.dumps(cp(next_case, token)) + '\n', encoding='utf-8')

    def confirm(self, state='state', following='next'):
        return self.run_cli('confirm', '--output', str(self.root / 'output'),
                            '--next-state', str(self.root / following), state=state)

    def test_persisted_two_handoffs_and_hold_survive_new_invocations(self):
        self.begin(); line = self.prepare(); self.assertTrue(line.endswith('\n'))
        self.output(self.current, 1, 'phone_stopped', '2' * 32)
        self.confirm(); self.prepare('next', 'phone_stopped')
        self.output(cp('phone_stopped', '2' * 32), 2, 'phone_disconnected', '3' * 32)
        self.now = 10; self.confirm('next', 'retention')
        self.now = 129
        with self.assertRaises(ValueError): self.prepare('retention', 'phone_disconnected')
        self.now = 130
        self.assertTrue(self.prepare('retention', 'phone_disconnected').endswith('\n'))

    def test_echo_does_not_create_next_state(self):
        self.begin(); line = self.prepare()
        (self.root / 'output').write_text(line, encoding='utf-8')
        with self.assertRaises(ValueError): self.confirm()
        self.assertFalse((self.root / 'next').exists())

    def test_delayed_begin_and_submission_reject_original_expiry(self):
        self.now = 31; self.begin()
        self.now = 320
        with self.assertRaises(ValueError): self.prepare()
        self.assertFalse((self.root / 'state.submitted').exists())
        with self.assertRaises(ValueError):
            self.run_cli('begin', '--checkpoint', str(self.root / 'checkpoint'),
                         '--journal', str(self.root / 'journal'), state='late')
        self.assertFalse((self.root / 'late').exists())

    def test_expired_next_checkpoint_creates_no_confirmation_or_next_state(self):
        self.begin(); self.prepare(); self.output(self.current, 1, 'phone_stopped', '2' * 32)
        self.now = 181
        with self.assertRaises(ValueError): self.confirm()
        self.assertFalse((self.root / 'state.confirmed').exists())
        self.assertFalse((self.root / 'next').exists())

    def test_terminal_reply_cannot_extend_original_deadline(self):
        self.now = 31; self.begin(); self.now = 301
        with self.assertRaises(ValueError): self.run_cli('prepare', '--terminal', 'cancelled')
        self.assertFalse((self.root / 'state.submitted').exists())

    def test_uncertain_stdout_is_reserved_before_delivery(self):
        self.begin()
        self.put('fields', {field: True for field in lifecycle.FIELDS['baseline']})
        broken = mock.Mock(); broken.write.side_effect = OSError('test')
        with mock.patch.object(sys, 'stdout', broken), self.assertRaises(OSError):
            cli.main(['prepare','--state',str(self.root/'state'),'--fields',str(self.root/'fields')],clock=lambda:self.now)
        self.assertTrue((self.root / 'state.submitted').exists())
        with self.assertRaises(ValueError): self.prepare()

    def test_reopen_later_checkpoint_and_duplicate_prepare_refused(self):
        self.put('checkpoint', cp('phone_disconnected'))
        with self.assertRaises(ValueError): self.begin()
        self.put('checkpoint', self.current); self.begin(); self.prepare()
        with self.assertRaises(ValueError): self.begin()
        with self.assertRaises(ValueError): self.prepare()

    def test_changed_request_attempt_closed_and_backward_clock_refused(self):
        self.begin()
        for field, value in [('request', {}), ('attempt', 'c' * 32), ('closed', True)]:
            original = self.data[field]; self.data[field] = value; self.put('journal', self.data)
            with self.assertRaises(ValueError): self.prepare()
            self.data[field] = original
        self.put('journal', self.data); self.now = -1
        with self.assertRaises(ValueError): self.prepare()

    def test_terminal_can_only_end_observation(self):
        self.begin()
        with self.assertRaises(ValueError): self.run_cli('prepare', '--terminal', 'not_observed')
        reply = json.loads(self.run_cli('prepare', '--terminal', 'cancelled'))
        self.assertEqual(reply['result'], 'cancelled')
        self.output(self.current, 1, 'phone_stopped', '2' * 32)
        with self.assertRaises(ValueError): self.confirm()

    def test_existing_next_state_and_double_confirmation_refused(self):
        self.begin(); self.prepare(); self.output(self.current, 1, 'phone_stopped', '2' * 32)
        self.confirm()
        with self.assertRaises(ValueError): self.confirm(following='another')
        self.assertFalse((self.root / 'another').exists())

    def test_failed_next_state_write_reserves_confirmation_before_retry(self):
        self.begin(); self.prepare(); self.output(self.current, 1, 'phone_stopped', '2' * 32)
        original = cli.write_new
        def fail_next(path, value):
            if path.name == 'next': raise OSError('injected disk failure')
            original(path, value)
        with mock.patch.object(cli, 'write_new', side_effect=fail_next), self.assertRaises(OSError):
            self.confirm()
        self.assertTrue((self.root / 'state.confirmed').exists())
        with self.assertRaises(ValueError): self.confirm(following='another')
        self.assertFalse((self.root / 'another').exists())

    def test_final_ack_after_journal_closure_does_not_claim_restoration(self):
        current = cp('fresh_reconnect');guard = handoff.Handoff(current,clock=lambda:self.now)
        cli.write_new(self.root/'final',cli.save_guard(guard,self.data['attempt'],self.root/'journal',cli.request_hash(self.data)))
        self.prepare('final','fresh_reconnect')
        self.data['lifecycle_cases']=history(9);self.data['closed']=True;self.put('journal',self.data)
        (self.root/'output').write_text(json.dumps(ack(current,history(9)))+'\n',encoding='utf-8')
        result=json.loads(self.run_cli('confirm','--output',str(self.root/'output'),state='final'))
        self.assertEqual(result['state'],'observation_confirmed');self.assertFalse(result['restoration_confirmed'])

    def test_real_producer_stdin_uses_persisted_cli_through_all_stages(self):
        code = '''import sys,json
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import gnss_lifecycle_observation as l
from run_gnss_observation_trial import Lines
journal=Path(sys.argv[2]);data=json.loads(journal.read_text());now=[0];raw=Lines(sys.stdin)
class Input:
 def read(self,timeout):
  reply=raw.read(timeout);now[0]+=120 if reply['case']=='phone_disconnected' else 1;return reply
def record(case):
 data['lifecycle_cases'].append(case);journal.write_text(json.dumps(data))
print(json.dumps({'result':l.observe(Input(),lambda prefix:True,clock=lambda:now[0],on_case=record)}),flush=True)
'''
        process = subprocess.Popen([sys.executable, '-I','-S','-B','-u','-c',code,
                                    str(ROOT/'tools'), str(self.root/'journal')],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        watchdog = threading.Timer(10, process.kill); watchdog.start()
        state = None; accepted = None; count = 0
        try:
            while True:
                line = process.stdout.readline(); self.assertTrue(line)
                value = json.loads(line)
                if value.get('schema') == handoff.ACK:
                    accepted = line; count += 1; continue
                if value.get('schema') == handoff.CHECKPOINT:
                    next_state = value['case'] + '.json'
                    if state is None:
                        self.put('checkpoint', value)
                        self.run_cli('begin','--checkpoint',str(self.root/'checkpoint'),
                                     '--journal',str(self.root/'journal'),state=next_state)
                    else:
                        (self.root/'output').write_text(accepted+line,encoding='utf-8')
                        self.confirm(state, next_state)
                    state = next_state
                    if value['case'] == 'phone_disconnected': self.now += 120
                    reply = self.prepare(state, value['case']); accepted = None
                    process.stdin.write(reply); process.stdin.flush()
                elif 'result' in value:
                    (self.root/'output').write_text(accepted+line,encoding='utf-8')
                    result=json.loads(self.run_cli('confirm','--output',str(self.root/'output'),state=state))
                    self.assertEqual(result['state'],'observation_confirmed')
                    self.assertFalse(result['restoration_confirmed'])
                    self.assertEqual(value['result']['outcome'],'passed');break
            process.stdin.close(); self.assertEqual(process.wait(timeout=10),0);self.assertEqual(count,8)
        finally:
            watchdog.cancel()
            if process.poll() is None: process.kill();process.wait(timeout=10)
            for stream in (process.stdin,process.stdout,process.stderr):
                if not stream.closed: stream.close()


if __name__ == '__main__':
    unittest.main()
