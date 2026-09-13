"""Actual OT210 orchestration/authority/observation/restoration; synthetic storage only."""
from pathlib import Path
import copy,json,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import security_policy_invitation_execution as x
import security_policy_execution as old
import security_policy_input_timing as timing
import security_policy_execution_tests as prior
import security_policy_input_readback_tests as nvs
import security_policy_sync_readback_tests as sync_nvs

class Backend(prior.Backend):
    def __init__(self,data,owner):super().__init__(data);self.owner=owner;self.observe_calls=0;self.observe_fail=False
    def open(self,role):
        endpoint=super().open(role);backend=self
        class Wrapper:
            def run_once(self,challenge,deadline):
                try:return endpoint.run_once(challenge,deadline)
                finally:backend.flash[role,x.NVS_OFFSET]=getattr(backend, 'diagnostic', sync_nvs.diagnostic(8,0))
        return Wrapper()
    def read_observation(self,role,offset,size,purpose):
        self.observe_calls+=1;self.trace.append(('observe',role,offset))
        t=self.owner;grant=t.authority.validate(t.data['package_sha256'],operation='execute')
        journal=x.Journal(t.root,grant.attempt,t.data['package_sha256'])
        row=next(r for r in t.data['roles'] if r['role']==role)
        expected=x.bundle.image_binding(t.data['package'])
        x.admit_observation_child(t.root,t.data['package_sha256'],grant,journal,row,purpose,runtime_sha256=t.runtime,expected_image=expected,consume=False)
        x.admit_observation_child(t.root,t.data['package_sha256'],grant,journal,row,purpose,runtime_sha256=t.runtime,expected_image=expected)
        if self.observe_fail:raise RuntimeError('PRIVATE failure')
        return self.read(role,offset,size)

class Tests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name).resolve();(self.root/'.private').mkdir()
        self.runtime='e'*64;self.attempt='1'*32
        self.data={'package_sha256':'a'*64,'runtime_sha256':self.runtime,'candidate':b'candidate','roles':[],
                   'package':{'test_fixture':'synthetic storage, not an admitted candidate'}}
        self.image=x.readback.ImageBinding(x.readback.SYNC_CONTRACT,'heltec_v4_invitation_eval',x.sha(self.data['candidate']))
        image_patch=patch.object(x.bundle,'image_binding',return_value=self.image)
        image_patch.start();self.addCleanup(image_patch.stop)
        for i,role in enumerate('AB'):
            self.data['roles'].append({'role':role,'private_route':'COM'+str(i+1),'private_identity':str(i+1)*12,'application':bytes([65+i])*x.APP_SPAN,'nvs':nvs.fixture(),'protected':{k:{'bytes':size,'sha256':x.sha(bytes([len(k)])*size)} for k,(_,size) in x.REGIONS.items()}})
        self.backend=Backend(self.data,self)
        self.verifies=[]
        def verify(root,package,recovery=False):
            self.verifies.append(recovery);data=copy.deepcopy(self.data)
            if recovery:data.pop('candidate')
            return data
        p=patch.object(x.bundle,'verify',side_effect=verify);p.start();self.addCleanup(p.stop)
        self.authority=self.make_authority()
    def make_authority(self,*,attempt=None,operation='execute',origin=None,schema=None,runtime=None):
        runtime=runtime or self.runtime
        raw=x.canonical({'schema':schema or x.SCHEMAS[operation],'attempt':attempt or self.attempt,'package_sha256':self.data['package_sha256'],'runtime_sha256':runtime,'operation':operation,'origin_attempt':origin,'attempt_count':1,'radio_allowed':False,'actions':x.ACTIONS if operation=='execute' else x.RECOVERY_ACTIONS,'issued_utc':99,'expires_utc':200})
        path=self.root/'.private'/('grant-'+(attempt or self.attempt)+'.json');path.write_bytes(raw)
        return x.FileAuthority(self.root,path,x.sha(raw),runtime_sha256=runtime,utc=lambda:100)
    def execute(self):
        values=iter(['c'*32,'d'*32]);return x.execute(self.root,{},self.authority,self.backend,monotonic=lambda:0,challenge_factory=lambda:next(values))
    def recover(self):return x.recover(self.root,{},self.make_authority(attempt='2'*32,operation='recover',origin=self.attempt),self.backend,origin_attempt=self.attempt)
    def restored(self,role='A'):
        row=next(r for r in self.data['roles'] if r['role']==role)
        self.assertEqual(row['application'],self.backend.flash[role,x.APP_OFFSET]);self.assertEqual(row['nvs'],self.backend.flash[role,x.NVS_OFFSET])
    def test_composed_timing_is_observational_only(self):
        observations=[]
        for mode in ('absent','valid','clock_failure','bookkeeping_failure'):
            fixture=Tests('test_success_custody_a_before_b')
            fixture.setUp()
            try:
                marks=[];calls=[]
                def clock():
                    calls.append(1)
                    if mode=='clock_failure':raise RuntimeError('private clock failure')
                    return len(calls)*1000000
                recorder=timing.Recorder(clock=clock)
                original_mark=recorder.mark
                def mark(role,event):
                    marks.append((role,event))
                    if mode=='bookkeeping_failure':raise RuntimeError('private marker failure')
                    return original_mark(role,event)
                recorder.mark=mark
                if mode!='absent':fixture.backend.input_timing=recorder
                result=fixture.execute()
                fixture.restored('A');fixture.restored('B')
                observations.append((result['status'],list(fixture.backend.trace)))
                if mode!='absent':
                    self.assertEqual(marks,[(role,event) for role in 'AB' for event in timing.EVENTS])
                    expected='available' if mode=='valid' else 'unavailable'
                    self.assertEqual([row['status'] for row in recorder.summary()],[expected]*2)
                if mode=='valid':self.assertEqual(len(calls),10)
            finally:fixture.doCleanups()
        self.assertTrue(all(value==observations[0] for value in observations))

    def test_success_custody_a_before_b(self):
        result=self.execute();self.assertEqual(result['status'],'pass');self.assertEqual(self.backend.observe_calls,2)
        for role in 'AB':self.restored(role)
        self.assertTrue(all(r['stage_observation']['available'] for r in result['roles']))
        trace=self.backend.trace;self.assertLess(trace.index(('original_reset','A',0)),next(i for i,t in enumerate(trace) if t[1]=='B'))
        for role in 'AB':
            p=x.observation_path(self.root,role,self.attempt,'receipt.json');receipt=json.loads(p.read_bytes())
            capture=Path(receipt['capture']['path']);self.assertEqual(x.sha(capture.read_bytes()),receipt['capture']['sha256'])
            self.assertEqual(receipt['runtime_sha256'],self.runtime)
            self.assertEqual(receipt['image_binding'],x.image_descriptor(self.image))
            self.assertEqual(receipt['projection']['input_status'],'stage_result_recorded')
    def test_failed_a_restores_stops_b(self):
        self.backend.fail='partial_run';result=self.execute();self.assertEqual(result['status'],'evaluation_failed');self.restored()
        self.assertFalse(any(t[1]=='B' for t in self.backend.trace));self.assertEqual(self.backend.observe_calls,1)
    def test_partial_candidate_never_observed(self):
        self.backend.fail='partial_candidate';self.assertEqual(self.execute()['status'],'evaluation_failed');self.restored();self.assertEqual(self.backend.observe_calls,0)
    def test_changed_initial_nvs_no_write(self):
        self.backend.flash['A',x.NVS_OFFSET]=b'X'*x.NVS_SPAN
        self.assertEqual(self.execute()['status'],'reconciliation_required');self.assertFalse(any(t[0]=='write' for t in self.backend.trace))
    def test_preexisting_namespace_refuses_all_io(self):
        self.data['roles'][1]['nvs']=nvs.diagnostic()
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(self.backend.trace,[])
    def test_observation_failure_restoration_success(self):
        self.backend.observe_fail=True;result=self.execute();self.assertEqual(result['status'],'pass')
        self.assertFalse(result['roles'][0]['stage_observation']['available']);self.restored()
        claim=x.decode(x.observation_path(self.root,'A',self.attempt,'claimed.json').read_bytes())
        self.assertEqual(claim['schema'],'OT210-INVITATION-OBSERVATION-CLAIM-1')
        self.assertEqual(self.backend.observe_calls,2)
    def test_custody_write_failure_not_recovery(self):
        original=x.exclusive
        def write(path,value):
            if str(path).endswith('.receipt.json'):raise OSError('PRIVATE')
            return original(path,value)
        with patch.object(x,'exclusive',side_effect=write):result=self.execute()
        self.assertEqual(result['status'],'pass');self.assertFalse(result['roles'][0]['stage_observation']['available']);self.restored()
    def test_intent_failure_no_observation_still_restores(self):
        original=x.exclusive
        def write(path,value):
            if str(path).endswith('.intent.json'):raise OSError('PRIVATE')
            return original(path,value)
        with patch.object(x,'exclusive',side_effect=write):result=self.execute()
        self.assertEqual(result['status'],'pass');self.assertEqual(self.backend.observe_calls,0);self.restored()
    def test_close_failure_blocks_observation_restore(self):
        self.backend.fail='close';self.assertEqual(self.execute()['status'],'recovery_required');self.assertEqual(self.backend.observe_calls,0)
        trace=self.backend.trace;at=trace.index(('close','A',0));self.assertFalse(any(t[0] in ('read','write','observe','original_reset') for t in trace[at+1:]))
    def test_unknown_lease_new_backend_cannot_recover(self):
        self.backend.fail='close';self.execute();self.backend=Backend(self.data,self)
        with self.assertRaises(x.ExecutionError):self.recover()
        self.assertEqual(self.backend.trace,[])
    def test_missing_candidate_recovery_no_recapture(self):
        self.backend.fail='restore_write';self.assertEqual(self.execute()['status'],'recovery_required');before=self.backend.observe_calls
        self.backend.fail=None;self.assertEqual(self.recover()['status'],'recovered');self.assertEqual(self.backend.observe_calls,before);self.assertTrue(self.verifies[-1]);self.restored()
    def test_crash_after_restore_intent_no_recapture(self):
        original=x.Journal.add
        def add(journal,event,*args,**kwargs):
            result=original(journal,event,*args,**kwargs)
            if event=='restore_intent':raise KeyboardInterrupt()
            return result
        with patch.object(x.Journal,'add',add):
            with self.assertRaises(KeyboardInterrupt):self.execute()
        self.assertEqual(self.backend.observe_calls,0)
        self.assertEqual(self.recover()['status'],'recovered');self.assertEqual(self.backend.observe_calls,0);self.restored()
    def test_postreset_ambiguity_never_rewrites_old_nvs(self):
        original=x.Journal.add
        def add(journal,event,*args,**kwargs):
            if event=='original_booted':raise KeyboardInterrupt()
            return original(journal,event,*args,**kwargs)
        with patch.object(x.Journal,'add',add):
            with self.assertRaises(KeyboardInterrupt):self.execute()
        before=list(self.backend.trace)
        with self.assertRaises(x.ExecutionError):self.recover()
        self.assertEqual(before,self.backend.trace)
    def test_legacy_schema_and_authority_refused(self):
        for schema in ('OT188-GRANT-1','OT198-INPUT-EXECUTE-GRANT-1','OT196-STAGE-EXECUTE-GRANT-1','OT201-SYNC-EXECUTE-GRANT-1'):
            self.authority=self.make_authority(schema=schema)
            with self.assertRaises(x.ExecutionError):self.execute()
            self.assertEqual(self.backend.trace,[])
        self.authority=prior.Authority(self.attempt)
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(self.backend.trace,[])
    def test_runtime_mismatch_no_io(self):
        self.authority=self.make_authority(runtime='f'*64)
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(self.backend.trace,[])

    def test_new_schema_without_input_detail_write_authority_refuses_before_io(self):
        obj=x.decode(self.authority.path.read_bytes())
        obj['actions'].remove('diagnostic_input_detail_write')
        raw=x.canonical(obj);self.authority.path.write_bytes(raw)
        self.authority=x.FileAuthority(self.root,self.authority.path,x.sha(raw),runtime_sha256=self.runtime,utc=lambda:100)
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(self.backend.trace,[])
    def test_grant_reuse_refuses(self):
        self.execute();before=list(self.backend.trace)
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(before,self.backend.trace)
    def test_claim_refuses_second_actual_dataread(self):
        self.execute();row=self.data['roles'][0];grant=self.authority.validate(self.data['package_sha256'],operation='execute')
        path=x.observation_path(self.root,'A',self.attempt,'intent.json');purpose={'path':str(path),'sha256':x.sha(path.read_bytes())}
        with self.assertRaises(x.ExecutionError):x.admit_observation_child(self.root,self.data['package_sha256'],grant,x.Journal(self.root,self.attempt,self.data['package_sha256']),row,purpose,runtime_sha256=self.runtime,expected_image=self.image)

    def test_duplicate_claim_is_blocked_while_restore_intent_is_still_current(self):
        original=x.admit_observation_child
        duplicates=[]
        def admit(*args,**kwargs):
            result=original(*args,**kwargs)
            if kwargs.get('consume',True):
                with self.assertRaisesRegex(x.ExecutionError,'^observation_consumed$'):
                    original(*args,**kwargs)
                duplicates.append(args[4]['role'])
            return result
        with patch.object(x,'admit_observation_child',side_effect=admit):result=self.execute()
        self.assertEqual(result['status'],'pass');self.assertEqual(duplicates,['A','B'])
        self.assertEqual(self.backend.observe_calls,2)
        for role in 'AB':self.restored(role)

    def test_partial_input_prefix_is_observed_without_receipt_acceptance(self):
        self.backend.fail='partial_run'
        self.backend.diagnostic=sync_nvs.diagnostic(3)
        result=self.execute()
        self.assertEqual(result['status'],'evaluation_failed');self.restored()
        observed=result['roles'][0]['stage_observation']
        self.assertTrue(observed['available']);self.assertEqual(observed['stage'],'waiting')
        self.assertEqual(observed['input_status'],'committed_before_stage_result')
        self.assertEqual(observed['input']['terminal_reason'],'none')
        self.assertFalse(any(row[1]=='B' for row in self.backend.trace))
        self.assertEqual(self.backend.observe_calls,1)

    def test_exact_first_and_terminal_reasons_survive_restoration(self):
        self.backend.fail='partial_run'
        self.backend.diagnostic=sync_nvs.diagnostic(4,16,sync_nvs.input_record(193,31,19,19,13,1))
        observed=self.execute()['roles'][0]['stage_observation']
        self.assertTrue(observed['available']);self.restored()
        self.assertEqual((observed['error'],observed['input']['terminal_reason'],observed['input']['first_frame_reason']),
                         ('loop_limit','sync_limit','invalid_length'))
        self.assertEqual(observed['input']['discarded_bytes'],193)

    def test_stage_result_without_input_refuses_projection_but_restores(self):
        self.backend.diagnostic=nvs.diagnostic(8,0)
        result=self.execute();self.assertEqual(result['status'],'pass')
        for role in 'AB':self.restored(role)
        self.assertTrue(all(not row['stage_observation']['available'] for row in result['roles']))
        self.assertTrue(all(row['stage_observation']['input'] is None and row['stage_observation']['input_status'] is None for row in result['roles']))

    def test_candidate_bytes_mismatch_refuses_before_any_io(self):
        self.data['candidate']=b'changed candidate'
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(self.backend.trace,[])

    def test_wrong_image_binding_intent_refuses_data_read_then_restores(self):
        original=x.exclusive
        def write(path,value):
            if str(path).endswith('.intent.json'):
                value=copy.deepcopy(value);value['image_binding']['sha256']='f'*64
            return original(path,value)
        with patch.object(x,'exclusive',side_effect=write):result=self.execute()
        self.assertEqual(result['status'],'pass');self.restored()
        self.assertFalse(result['roles'][0]['stage_observation']['available'])
        self.assertEqual(self.backend.observe_calls,0)

    def test_child_image_mismatch_refuses_before_claim(self):
        original=x.admit_observation_child
        wrong=x.readback.ImageBinding(x.readback.SYNC_CONTRACT,self.image.target,'f'*64)
        def admit(*args,**kwargs):
            kwargs['expected_image']=wrong
            return original(*args,**kwargs)
        with patch.object(x,'admit_observation_child',side_effect=admit):result=self.execute()
        self.assertEqual(result['status'],'pass');self.restored()
        self.assertFalse(result['roles'][0]['stage_observation']['available'])
        self.assertFalse(x.observation_path(self.root,'A',self.attempt,'claimed.json').exists())

    def test_missing_durable_child_claim_cannot_be_available_custody(self):
        self.backend.read_observation=lambda role,offset,size,purpose:self.backend.read(role,offset,size)
        result=self.execute();self.assertEqual(result['status'],'pass');self.restored()
        self.assertFalse(result['roles'][0]['stage_observation']['available'])
        self.assertEqual(result['roles'][0]['stage_observation']['reason'],'custody_unavailable')
    def test_journal_failure_no_following_mutation(self):
        original=x.Journal.add
        def add(journal,event,*args,**kwargs):
            if event=='candidate_write_intent':journal.healthy=False;raise x.ExecutionError('journal_write_failed')
            return original(journal,event,*args,**kwargs)
        with patch.object(x.Journal,'add',add):result=self.execute()
        self.assertEqual(result['status'],'reconciliation_required');self.assertFalse(any(t[0]=='write' for t in self.backend.trace))
if __name__=='__main__':unittest.main()
