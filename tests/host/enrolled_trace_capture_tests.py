"""Host-only end-to-end TRACE collector checks using the actual C++ emitter."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import enrolled_pair_bridge as bridge
import enrolled_trial_operator as operator
from enrolled_trace_schema import TRACE_FIELDS, valid_target_trace
from enrolled_pair_bridge_tests import pair, run

class Serial:
    def __init__(self, raw, gaps=False):
        self.raw=bytearray(raw);self.is_open=True;self.calls=0;self.gaps=gaps
    def write(self, value):return len(value)
    def read(self, count):
        self.calls+=1
        if self.gaps and self.calls%3==0:return b''
        value=bytes(self.raw[:count]);del self.raw[:count];return value
    def close(self):self.is_open=False

class Clock:
    def __init__(self):self.value=0.
    def __call__(self):self.value+=.00001;return self.value

def endpoint(raw,gaps=False):
    return bridge.Endpoint(Serial(raw,gaps),lambda:True,monotonic=Clock())

def wire(fault=0):
    snapshot={k:0 for k in bridge.SNAPSHOT_FIELDS};snapshot['fault']=fault
    trace={k:0 for k in TRACE_FIELDS};trace['fault']=fault
    diag=('OTENROLL1 DIAG 1 '+' '.join(str(snapshot[k]) for k in bridge.SNAPSHOT_FIELDS)+'\n').encode()
    line=('OTENROLL1 TRACE 1 '+' '.join(str(trace[k]) for k in TRACE_FIELDS)+'\n').encode()
    return diag,line,trace

class Tests(unittest.TestCase):
    def test_trace_terminal_commit_and_legacy(self):
        diag,line,trace=wire()
        for raw,expected in ((diag+line+b'OTENROLL1 CLOSED 1\n',trace),(diag+b'OTENROLL1 CLOSED 1\n',None)):
            e=endpoint(raw,True);self.assertEqual(e.exchange('CLOSE','CLOSED',1),['1']);self.assertEqual(e.trace,expected)
    def test_refusal_commits_capture_before_raising(self):
        diag,line,trace=wire(3);e=endpoint(diag+line+b'OTENROLL1 REFUSED 1\n')
        with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):e.exchange('RFPOLL','RF',1)
        self.assertEqual(e.trace,trace);self.assertEqual(e.diagnostic['fault'],3)
    def test_timeout_retains_complete_trace_only_as_unconfirmed(self):
        diag,line,trace=wire(3);e=endpoint(diag+line)
        with self.assertRaises(bridge.BridgeError):e.exchange('RFPOLL','RF',.08)
        self.assertIsNone(e.trace);self.assertIsNone(e.diagnostic)
        self.assertEqual(e.unconfirmed_trace,trace)
        event={'operator':'enrolled_target_trace_partial','role':'B','trace':e.unconfirmed_trace}
        self.assertEqual(operator.operator_event(event),event)
    def test_invalid_or_incomplete_sequences_do_not_commit(self):
        diag,line,_=wire();_,other,_=wire(3)
        for raw in (line+b'OTENROLL1 CLOSED 1\n',diag+line+line+b'OTENROLL1 CLOSED 1\n',
                    diag+other+b'OTENROLL1 CLOSED 1\n',diag+line.replace(b'TRACE 1 ',b'TRACE 2 ')+b'OTENROLL1 CLOSED 1\n',
                    diag+line+b'OTENROLL1 STATUS 4 100 0\n',diag+line,diag+line[:-1],
                    diag+line.replace(b'\n',b'\r\n')+b'OTENROLL1 CLOSED 1\n'):
            with self.subTest(raw_size=len(raw)):
                e=endpoint(raw)
                with self.assertRaises(bridge.BridgeError):e.exchange('CLOSE','CLOSED',.08)
                self.assertIsNone(e.trace)
    def test_schema_and_operator_reject_extra_private_fields(self):
        _,_,trace=wire();event={'operator':'enrolled_target_trace','role':'A','trace':trace}
        self.assertEqual(operator.operator_event(event),event)
        for key,value in [('reason',26),('layer',10),('flags',8),('milestone_mask',64),('fault',True),('now_ms',-1),('now_ms',1<<64),('private','DO_NOT_LOG')]:
            v=dict(trace);v[key]=value;self.assertFalse(valid_target_trace(v))
            with self.assertRaises(ValueError):operator.operator_event(dict(event,trace=v))
        with self.assertRaises(ValueError):operator.operator_event(dict(event,private='DO_NOT_LOG'))
    def test_required_trace_and_forwarding(self):
        snapshot={k:0 for k in bridge.SNAPSHOT_FIELDS};_,_,trace=wire()
        for present in (False,True):
            a,b=pair();events=[]
            for e in (a,b):
                e.diagnostic=dict(snapshot)
                if present:e.trace=dict(trace)
            r=run(a,b,require_diagnostics=True,require_trace=True,notify=events.append)
            if present:
                self.assertTrue(r());self.assertEqual(len([x for x in events if type(x)is dict and x['operator']=='enrolled_target_trace']),2)
            else:
                with self.assertRaises(bridge.BridgeError):r()
            self.assertTrue(a.closed and b.closed)

class ChunkSerial(Serial):
    def __init__(self, raw, fragment=64, gaps=False):
        super().__init__(raw, gaps);self.fragment=fragment;self.requests=[]
    @property
    def in_waiting(self):return len(self.raw)
    def read(self, count):
        self.requests.append(count)
        return super().read(min(count,self.fragment))

class ChunkTests(unittest.TestCase):
    def test_fragmented_and_coalesced_trace_with_bounded_guards(self):
        diag,line,trace=wire(3);raw=diag+line+b'OTENROLL1 REFUSED 1\n'
        for fragment in (1,3,17,64):
            with self.subTest(fragment=fragment):
                serial=ChunkSerial(raw,fragment);calls=[]
                e=bridge.Endpoint(serial,lambda:calls.append(1) or True,monotonic=lambda:0.)
                with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):e.exchange('RFPOLL','RF',1)
                self.assertEqual(e.trace,trace)
                self.assertEqual(len(calls),3+2*serial.calls)
                self.assertTrue(all(1<=n<=64 for n in serial.requests))
                self.assertEqual(serial.calls,(len(raw)+fragment-1)//fragment)
    def test_byte_guard_cost_regression(self):
        diag,line,trace=wire();raw=diag+line+b'OTENROLL1 CLOSED 1\n'
        for chunked in (False,True):
            serial=ChunkSerial(raw) if chunked else Serial(raw);clock=[0.]
            def guard():clock[0]+=.01;return True
            e=bridge.Endpoint(serial,guard,monotonic=lambda:clock[0])
            if chunked:
                self.assertEqual(e.exchange('CLOSE','CLOSED',1),['1'])
                self.assertEqual(e.trace,trace);self.assertLess(clock[0],.2)
            else:
                with self.assertRaises(bridge.BridgeError):e.exchange('CLOSE','CLOSED',1)
                self.assertIsNone(e.trace)
    def test_coalesced_tail_survives_next_command(self):
        s=ChunkSerial(b'OTENROLL1 OK first\nOTENROLL1 OK second\n')
        e=bridge.Endpoint(s,lambda:True,monotonic=lambda:0.)
        self.assertEqual(e.exchange('HELLO','OK',1),['first'])
        self.assertEqual(e.exchange('STATUS','OK',1),['second'])
        self.assertEqual(s.calls,1);self.assertFalse(e.received)
    def test_post_read_identity_rejection_does_not_parse_chunk(self):
        s=ChunkSerial(b'OTENROLL1 OK yes\n');guards=[]
        def guard():guards.append(1);return len(guards)<5
        e=bridge.Endpoint(s,guard,monotonic=lambda:0.)
        with self.assertRaisesRegex(bridge.BridgeError,'passive_lease_invalid'):e.exchange('STATUS','OK',1)
        self.assertEqual(s.calls,1);self.assertFalse(e.received)
    def test_read_exception_still_checks_identity_and_stops(self):
        class Broken(ChunkSerial):
            def read(self,count):self.calls+=1;raise OSError('private detail')
        for identity_good in (False,True):
            s=Broken(b'x');guards=[]
            def guard():guards.append(1);return len(guards)<5 or identity_good
            e=bridge.Endpoint(s,guard,monotonic=lambda:0.)
            expected='serial_read_failed' if identity_good else 'passive_lease_invalid'
            with self.assertRaisesRegex(bridge.BridgeError,expected):e.exchange('STATUS','OK',1)
            self.assertEqual(len(guards),5);self.assertEqual(s.calls,1)
    def test_deadline_during_read_rejects_complete_response(self):
        clock=[0.]
        class Late(ChunkSerial):
            def read(self,count):clock[0]=2.;return super().read(count)
        s=Late(b'OTENROLL1 OK yes\n');e=bridge.Endpoint(s,lambda:True,monotonic=lambda:clock[0])
        with self.assertRaisesRegex(bridge.BridgeError,'late_read'):e.exchange('STATUS','OK',1)
        self.assertFalse(e.received)
    def test_invalid_available_and_oversized_reads_fail_closed(self):
        for available in (True,-1,1.5,1<<32):
            class Invalid(ChunkSerial):
                @property
                def in_waiting(self):return available
            s=Invalid(b'');e=bridge.Endpoint(s,lambda:True,monotonic=lambda:0.)
            with self.assertRaisesRegex(bridge.BridgeError,'serial_read_invalid'):e.exchange('STATUS','OK',1)
            self.assertEqual(s.calls,0)
        class Oversized(ChunkSerial):
            def read(self,count):return b'x'*(count+1)
        e=bridge.Endpoint(Oversized(b'x'),lambda:True,monotonic=lambda:0.)
        with self.assertRaisesRegex(bridge.BridgeError,'serial_read_invalid'):e.exchange('STATUS','OK',1)
    def test_overlong_line_and_partial_trace_preserve_failure(self):
        e=bridge.Endpoint(ChunkSerial(b'x'*768+b'\n'),lambda:True,monotonic=Clock())
        with self.assertRaisesRegex(bridge.BridgeError,'line_overflow'):e.exchange('STATUS','OK',1)
        diag,line,trace=wire(3)
        for tail in (b'',b'OTENROLL1 WRONG 1\n'):
            e=bridge.Endpoint(ChunkSerial(diag+line+tail,gaps=True),lambda:True,monotonic=Clock())
            with self.assertRaises(bridge.BridgeError):e.exchange('RFPOLL','RF',.08)
            self.assertIsNone(e.trace);self.assertEqual(e.unconfirmed_trace,trace)

def interop(executable):
    for case in ('success','expiry','clock','storage'):
        raw=subprocess.run([str(executable),'--emit-trace-case',case],capture_output=True,check=True).stdout
        assert raw.startswith(b'OTENROLL1 DIAG 1 ') and raw.count(b'\n')==3
        for fragment in (1,64):
            e=bridge.Endpoint(ChunkSerial(raw,fragment,gaps=True),lambda:True,monotonic=Clock())
            if case=='success':assert e.exchange('CLOSE','CLOSED',1)==['1']
            else:
                try:e.exchange('RFPOLL','RF',1)
                except bridge.BridgeError as error:assert error.args==('target_refused',)
                else:raise AssertionError('Expected fixture refusal')
            assert valid_target_trace(e.trace) and e.trace['fault']==e.diagnostic['fault']
            event=operator.operator_event({'operator':'enrolled_target_trace','role':'A','trace':e.trace})
            assert set(event)=={'operator','role','trace'}
    print('PASS 8 real target formatter / fragmented and chunked parser / operator trace cases')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--diagnostic-exe');args,rest=parser.parse_known_args()
    if args.diagnostic_exe:interop(args.diagnostic_exe)
    unittest.main(argv=[sys.argv[0],*rest])
