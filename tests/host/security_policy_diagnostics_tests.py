"""Differential diagnostics tests: same actual endpoint, device trace and clock calls."""
from pathlib import Path
import json
import sys
import unittest
from types import SimpleNamespace
import threading
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import security_policy_diagnostics as diagnostics
from security_policy_endpoint import Endpoint

CHALLENGE='0123456789abcdef0123456789abcdef'
RECEIPT=f'SEC_EVAL1 ot187-policy-v0 {CHALLENGE} pass\n'.encode()

class Handle:
    def __init__(self,case):
        self.case=case;self.trace=[];self.now=0.;self.is_open=True
        self.chunks={'malformed':[b'PRIVATE_RAW\n'],'trailing':[RECEIPT,b'x'],
                     'empty':[],'oversize':[b'x'*129],'fragment':[RECEIPT[:20],RECEIPT[20:]]}.get(case,[RECEIPT])[:]
        self.timeout=0.;self.write_timeout=0.;self.reads=0;self.after_read_clocks=0
    def clock(self):
        if self.reads:
            self.after_read_clocks+=1
            if self.case=='final_clock_crossing' and self.after_read_clocks==2:self.now=1.
        self.trace.append(('clock',self.now));return self.now
    def guard(self):
        self.trace.append(('guard',))
        if self.case=='read_late' and self.trace.count(('guard',))==4:self.now=1.
        if self.case=='prewrite':return False
        if self.case=='late_guard' and self.reads:return False
        return True
    def write(self,raw):
        self.trace.append(('write',raw,self.write_timeout))
        if self.case=='write_exception':raise RuntimeError('PRIVATE_EXCEPTION '+CHALLENGE)
        if self.case=='partial':return len(raw)-1
        if self.case=='oversize_write':return len(raw)+1
        if self.case=='boolean_write':return True
        if self.case=='negative_write':return -1
        return len(raw)
    def read(self,size):
        self.trace.append(('read',size,self.timeout));self.reads+=1
        self.now+=min(self.timeout,.01)
        if self.case=='read_exception':raise RuntimeError('PRIVATE_EXCEPTION')
        if self.case=='crossing':self.now=1.
        return self.chunks.pop(0) if self.chunks else b''
    def close(self):
        self.trace.append(('close',))
        if self.case=='close_failure':raise RuntimeError('PRIVATE_CLOSE')
        self.is_open=False

def exercise(case,observed):
    handle=Handle(case)
    endpoint=(diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
              if observed else Endpoint(handle,guard=handle.guard,monotonic=handle.clock))
    def call(function):
        try:return ('return',function())
        except Exception as error:return ('error',type(error),error.args)
    result=call(lambda:endpoint.run_once(CHALLENGE,.1))
    reuse=call(lambda:endpoint.run_once(CHALLENGE,.1)) if case=='reuse' else None
    close=call(endpoint.close)
    return handle,endpoint,(result,reuse,close)

class Tests(unittest.TestCase):
    def compare(self,case):
        original,_,expected=exercise(case,False)
        handle,endpoint,actual=exercise(case,True)
        self.assertEqual(actual,expected)
        self.assertEqual(handle.trace,original.trace)
        backend=SimpleNamespace(lock=threading.RLock(),_diagnostic_endpoints={'A':endpoint})
        rows=diagnostics.summary(backend)
        projection=json.dumps(rows)
        self.assertNotIn(CHALLENGE,projection)
        self.assertNotIn('PRIVATE',projection)
        self.assertNotIn('SEC_EVAL1',projection)
        self.assertEqual(rows[0]['close_confirmed'],case!='close_failure')
        return rows[0]
    def test_success(self):
        row=self.compare('success');self.assertTrue(row['receipt_accepted']);self.assertTrue(row['matching_receipt_observed'])
    def test_fragmented_success(self):self.assertTrue(self.compare('fragment')['receipt_accepted'])
    def test_prewrite_guard(self):self.assertEqual(self.compare('prewrite')['command_accepted_bytes'],0)
    def test_partial_write(self):self.assertEqual(self.compare('partial')['error'],'endpoint_partial_write')
    def test_no_data(self):self.assertEqual(self.compare('empty')['capture_error'],'receipt_timeout')
    def test_malformed(self):self.assertEqual(self.compare('malformed')['capture_error'],'receipt_invalid')
    def test_trailing(self):
        row=self.compare('trailing');self.assertTrue(row['matching_receipt_observed']);self.assertFalse(row['receipt_accepted']);self.assertEqual(row['capture_error'],'trailing_output')
    def test_deadline_crossing(self):
        row=self.compare('crossing');self.assertTrue(row['matching_receipt_observed']);self.assertFalse(row['receipt_accepted'])
    def test_postread_guard(self):
        row=self.compare('late_guard');self.assertTrue(row['matching_receipt_observed']);self.assertFalse(row['receipt_accepted']);self.assertEqual(row['error'],'endpoint_capture_refused');self.assertEqual(row['inner_endpoint_error'],'endpoint_admission_failed')
    def test_final_capture_clock_crossing(self):
        row=self.compare('final_clock_crossing');self.assertTrue(row['matching_receipt_observed']);self.assertFalse(row['receipt_accepted']);self.assertEqual(row['capture_error'],'late_output')
    def test_close_failure(self):self.assertEqual(self.compare('close_failure')['close_error'],'endpoint_close_unconfirmed')
    def test_write_exception(self):self.assertEqual(self.compare('write_exception')['error'],'endpoint_transport_failed')
    def test_read_exception(self):
        row=self.compare('read_exception')
        self.assertEqual(row['capture_error'],'read_failed')
        self.assertEqual(row['inner_endpoint_error'],'none')
    def test_inner_read_deadline(self):
        row=self.compare('read_late')
        self.assertEqual(row['capture_error'],'read_failed')
        self.assertEqual(row['inner_endpoint_error'],'endpoint_read_late')
    def test_inner_error_chain_unknown_and_cycle(self):
        outer=diagnostics.EndpointError('endpoint_capture_refused')
        middle=diagnostics.CaptureError('read_failed')
        unknown=RuntimeError('PRIVATE_UNKNOWN')
        inner=diagnostics.EndpointError('endpoint_read_late')
        outer.__context__=middle;middle.__context__=unknown;unknown.__context__=inner;inner.__context__=outer
        self.assertEqual(diagnostics._error_details(outer),('endpoint_capture_refused','read_failed','endpoint_read_late'))
        inner.args=('PRIVATE_UNKNOWN',)
        self.assertEqual(diagnostics._error_details(outer),('endpoint_capture_refused','read_failed','none'))
    def test_oversized_read(self):self.assertEqual(self.compare('oversize')['capture_error'],'read_invalid')
    def test_one_use(self):self.assertEqual(self.compare('reuse')['error'],'endpoint_consumed')
    def test_invalid_write_counts(self):
        for case in ('oversize_write','boolean_write','negative_write'):
            with self.subTest(case=case):self.assertEqual(self.compare(case)['command_accepted_bytes'],0)
    def test_unknown_summary_fallback(self):
        rows=diagnostics.summary(object());self.assertEqual(len(rows),2);self.assertTrue(all(not r['diagnostics_available'] for r in rows))
    def test_exception_context_bound_and_unknown(self):
        error=RuntimeError('PRIVATE_EXCEPTION')
        error.__context__=error
        self.assertEqual(diagnostics._codes(error),('unknown','none'))
    def test_summary_survives_removed_endpoint(self):
        row=self.compare('success');self.assertTrue(row['close_confirmed'])
    def policy_context(self,*extras):
        import security_policy_operator as operator
        return {'manifest':{'files':{'policy/security_policy_'+name+'.py':{} for name in operator.POLICY+extras}}}
    def test_runtime_policy_legacy_five(self):
        import security_policy_operator as operator
        self.assertEqual(operator.runtime_policy(self.policy_context()),operator.POLICY)
    def test_runtime_policy_paired_backup(self):
        import security_policy_operator as operator
        self.assertEqual(operator.runtime_policy(self.policy_context('backup','backup_operator')),operator.POLICY+('backup','backup_operator'))
    def test_runtime_policy_diagnostics(self):
        import security_policy_operator as operator
        self.assertEqual(operator.runtime_policy(self.policy_context('backup','backup_operator','diagnostics')),operator.POLICY+('backup','backup_operator','diagnostics'))
    def test_runtime_policy_incomplete_refused(self):
        import security_policy_operator as operator
        for extras in [('backup',),('backup_operator',),('diagnostics',),('backup','diagnostics'),('backup_operator','diagnostics')]:
            with self.subTest(extras=extras),self.assertRaises(operator.OperatorError):
                operator.runtime_policy(self.policy_context(*extras))
    def test_legacy_result_identity_preserved(self):
        import security_policy_operator as operator
        result={'status':'pass'}
        self.assertIs(operator.with_diagnostics(self.policy_context(),object(),result),result)
        self.assertIs(operator.with_diagnostics(self.policy_context('backup','backup_operator'),object(),result),result)
    def test_clock_rollback_elapsed_is_not_success(self):
        handle=Handle('empty')
        def clock():
            value=-1. if handle.reads else 0.
            handle.trace.append(('clock',value));return value
        endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=clock)
        with self.assertRaises(Exception):endpoint.run_once(CHALLENGE,.1)
        row=endpoint.observation.row
        self.assertEqual(row['elapsed_ms'],0)
        self.assertFalse(row['receipt_accepted'])
        self.assertNotEqual(row['error'],'none')
    def test_failing_row_cannot_mask_success_or_close(self):
        class FailRow(dict):
            def __setitem__(self,key,value):raise RuntimeError('PRIVATE_ROW')
        handle=Handle('success')
        endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
        endpoint.observation.row=FailRow(endpoint.observation.row)
        self.assertEqual(endpoint.run_once(CHALLENGE,.1),RECEIPT)
        self.assertTrue(endpoint.close())
        rows=diagnostics.summary(SimpleNamespace(lock=threading.RLock(),_diagnostic_endpoints={'A':endpoint}))
        self.assertFalse(rows[0]['diagnostics_available'])
    def test_final_success_and_close_assignment_failures_preserve_trace(self):
        for target in ('complete','receipt_accepted','close_confirmed'):
            with self.subTest(target=target):
                class FailSelected(dict):
                    def __setitem__(self,key,value):
                        if key==target or (target=='complete' and key=='stage' and value=='complete'):
                            raise RuntimeError('PRIVATE_FINALIZE')
                        super().__setitem__(key,value)
                original,_,_=exercise('success',False)
                handle=Handle('success')
                endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
                endpoint.observation.row=FailSelected(endpoint.observation.row)
                self.assertEqual(endpoint.run_once(CHALLENGE,.1),RECEIPT)
                self.assertTrue(endpoint.close())
                self.assertEqual(handle.trace,original.trace)
                self.assertTrue(endpoint.observation.disabled)
    def test_failing_row_cannot_mask_endpoint_error(self):
        class FailRow(dict):
            def __setitem__(self,key,value):raise RuntimeError('PRIVATE_ROW')
        handle=Handle('prewrite')
        endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
        endpoint.observation.row=FailRow(endpoint.observation.row)
        with self.assertRaisesRegex(diagnostics.EndpointError,'endpoint_admission_failed'):
            endpoint.run_once(CHALLENGE,.1)
        self.assertTrue(endpoint.close())
    def test_cleanup_failure_preserves_success(self):
        class FailClear(bytearray):
            def clear(self):raise RuntimeError('PRIVATE_CLEANUP')
        handle=Handle('success')
        endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
        original=endpoint.endpoint.run_once
        def run(challenge,deadline):
            result=original(challenge,deadline)
            endpoint.observation.pending=FailClear()
            return result
        endpoint.endpoint.run_once=run
        self.assertEqual(endpoint.run_once(CHALLENGE,.1),RECEIPT)
        self.assertTrue(endpoint.close())
        self.assertTrue(endpoint.observation.disabled)
        original,_,_=exercise('success',False)
        self.assertEqual(handle.trace,original.trace)
    def test_bookkeeping_failure_does_not_change_endpoint(self):
        original,_,expected=exercise('success',False)
        handle=Handle('success');endpoint=diagnostics.ObservedEndpoint(handle,role='A',guard=handle.guard,monotonic=handle.clock)
        endpoint.observation.row['read_calls']=object()
        self.assertEqual(endpoint.run_once(CHALLENGE,.1),RECEIPT);self.assertTrue(endpoint.close())
        self.assertEqual(handle.trace,original.trace)
        self.assertFalse(endpoint.observation.row['diagnostics_available'])

if __name__=='__main__':unittest.main()
