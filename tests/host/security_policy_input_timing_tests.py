"""Independent monotonic observer tests; no hardware or wall-clock waiting."""
from pathlib import Path
import json
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import security_policy_input_timing as timing


class Tests(unittest.TestCase):
    def recorder(self, values):
        values = iter(values)
        return timing.Recorder(clock=lambda: next(values))
    def complete(self, recorder, role='A'):
        for event in timing.EVENTS:
            self.assertIsNone(recorder.mark(role, event))
    def test_both_roles_fixed_projection(self):
        recorder=self.recorder([0,10_000_000,13_000_000,18_000_000,20_000_000]*2)
        self.complete(recorder);self.complete(recorder,'B')
        for row in recorder.summary():
            self.assertEqual({k:row[k] for k in timing.DURATIONS},dict(reset_call_ms=10,
                reset_return_to_open_ms=3,open_ms=5,opened_to_run_ms=2,reset_return_to_run_ms=10))
            self.assertEqual(row['status'],'available')
    def test_partial_unavailable_without_extra_clock(self):
        calls=[];recorder=timing.Recorder(clock=lambda: calls.append(1) or 0)
        recorder.mark('A',timing.EVENTS[0]);recorder.summary();recorder.summary()
        self.assertEqual(len(calls),1);self.assertEqual(recorder.summary()[0]['status'],'unavailable')
    def test_duplicate_or_out_of_order_sticky(self):
        for events in [(timing.EVENTS[1],), (timing.EVENTS[0],)*2, (*timing.EVENTS,timing.EVENTS[-1])]:
            recorder=timing.Recorder(clock=lambda:0)
            for event in events:recorder.mark('A',event)
            self.assertEqual(recorder.summary()[0]['status'],'unavailable')
    def test_throwing_clock_no_exception_text(self):
        def clock():raise RuntimeError('SECRET DEVICE CHALLENGE')
        recorder=timing.Recorder(clock=clock);self.complete(recorder)
        self.assertNotIn('SECRET',json.dumps(recorder.summary()))
        self.assertEqual(recorder.summary()[0]['status'],'unavailable')
    def test_rollback_refused(self):
        recorder=self.recorder([10,9,20,30,40]);self.complete(recorder)
        self.assertEqual(recorder.summary()[0]['status'],'unavailable')
    def test_maximum_interval_and_overrun(self):
        for end,status in [(timing.MAX_INTERVAL_NS,'available'),(timing.MAX_INTERVAL_NS+1,'unavailable')]:
            recorder=self.recorder([0,0,0,0,end]);self.complete(recorder)
            self.assertEqual(recorder.summary()[0]['status'],status)
    def test_invalid_clock_values(self):
        for value in (True,-1,1.0,float('nan'),float('inf'),None,'private',timing.MAX_CLOCK_NS+1):
            recorder=self.recorder([value]*5);self.complete(recorder)
            self.assertEqual(recorder.summary()[0]['status'],'unavailable')
    def test_bad_role_and_event_no_leak(self):
        for role,event in [('PRIVATE','private'),([],None),('A',{}),('A','private')]:
            recorder=timing.Recorder(clock=lambda:0);recorder.mark(role,event)
            text=json.dumps(recorder.summary());self.assertNotIn('PRIVATE',text);self.assertNotIn('private',text)
    def test_one_bad_role_does_not_poison_other(self):
        recorder=timing.Recorder(clock=lambda:0);recorder.mark('A','bad');self.complete(recorder,'B')
        self.assertEqual([r['status'] for r in recorder.summary()],['unavailable','available'])
    def test_bookkeeping_failure_preserves_caller(self):
        recorder=timing.Recorder(clock=lambda:0);recorder._samples=None
        recorder.mark('A',timing.EVENTS[0]);self.assertEqual(recorder.summary()[0]['status'],'unavailable')
        self.assertEqual(timing.summary(object())[0]['status'],'unavailable')
    def test_zero_and_fractional_milliseconds_no_epochs(self):
        recorder=self.recorder([900000000000,900000000001,900000000002,900000000003,900000000004])
        self.complete(recorder);row=recorder.summary()[0]
        self.assertEqual(set(row),{'role','status',*timing.DURATIONS})
        self.assertTrue(all(row[k]==0 for k in timing.DURATIONS))


if __name__=='__main__':unittest.main()
