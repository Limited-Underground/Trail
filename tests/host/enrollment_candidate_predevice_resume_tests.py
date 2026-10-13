"""Synthetic provenance/coordination fixtures only; no native process/UI/device call."""
from pathlib import Path
import copy, hashlib, json, sys, tempfile, unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests/host')]
import enrollment_candidate_runtime as runtime
import enrollment_candidate_interactive_launch as visible
import enrollment_candidate_predevice_resume as resume
import enrollment_candidate_owner_workflow as flow
from enrollment_candidate_owner_workflow_tests import Fixture as ParentFixture

class Fixture:
    def __init__(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name);self.private=self.root/'.private';self.private.mkdir()
        self.oldroot=self.private/'old';self.oldroot.mkdir()
        self.oldready=self.private/'oldready';self.oldready.mkdir()
        self.newroot=self.private/'new';self.newroot.mkdir()
        self.newready=self.private/'newready';self.newready.mkdir()
        self.now=100000.0;self.utc_offset=0;self.opened=0;self.process_calls=0;self.clear=True;self.event='confirm'
        self.scope=dict(strategy=visible.STRATEGY,session_id=1,logon_sha256='a'*64,
            input_desktop=True,ac_online=True,display_never=True,sleep_never=True,
            no_screensaver_or_inactivity_lock=True,work_area_sufficient=True)
        archive=self.private/'archive';archive.mkdir()
        oldpins={};newpins={};archives={}
        for name in visible.RESUME_SOURCE_NAMES:
            raw=('synthetic-old-'+name).encode()
            if name!=resume.NAME:
                path=archive/name;path.write_bytes(raw)
                archives[name]=visible.reference(path);oldpins[name]=runtime.descriptor(raw)
            newpins[name]=runtime.descriptor(('synthetic-new-'+name).encode()) if name in resume.CHANGED_EXTERNAL or name==resume.NAME else runtime.descriptor(raw)
        self.old=dict(schema='OT-VISIBLE-SESSION-BINDING-1',worktree=str(self.root),host={'fixture':'host'},
            assembly={'fixture':'same-v16'},session=self.write(self.private/'oldtemplate',{'synthetic':True}),
            sources=oldpins,review=None,readiness_root=str(self.oldready),session_output=str(self.oldroot),session_nonce='a'*32)
        self.old['review']=self.write(self.private/'old-binding-review.json',dict(schema='OT-VISIBLE-SESSION-REVIEW-1',
            binding_core_sha256=runtime.descriptor(runtime.canonical(visible.binding_core(self.old)))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        self.oldref=self.write(self.private/'old-binding.json',self.old)
        proof=self.write(self.oldready/'neutral-proof.json',dict(schema='OT-NEUTRAL-VISIBILITY-PROOF-1',binding=self.oldref,
            scope=self.scope,started_monotonic=97900,finished_monotonic=98000,finished_utc=98000,idle_seconds=60.0,
            timings=[dict(kind=k,elapsed_seconds=2,explicit_click=True,cleared=True) for k in ('TEST_WORD','TEST_COMPARE')],
            word_sha256=hashlib.sha256(b'BIRD').hexdigest(),difference=[1,1],hardware_access=False,enrollment_authority=False))
        owner=self.write(self.oldready/'owner-reply.json',dict(schema='OT-NEUTRAL-OWNER-REPLY-1',proof=proof,
            source='fresh_human_chat_reply',received_utc=98001,observed_word='BIRD',difference=[1,1],rows_comfortably_readable=True))
        ready=self.write(self.oldready/'readiness.json',dict(schema='OT-VISIBLE-READINESS-1',binding=self.oldref,
            proof=proof,owner=owner,hardware_authority=False,enrollment_acceptance=False))
        practice=self.write(self.oldready/'practice.json',dict(schema='OT-NEUTRAL-PRACTICE-1',outcome='completed',
            binding_sha256=self.oldref['sha256'],finished_utc=98200,timings=[dict(kind=k,elapsed_seconds=10,
            modeled_gestures=g,explicit_click=True,cleared=True) for k,g in zip(('MY_ID','PEER_A','PEER_B','COMPARE_CODE'),(2,1,1,2))],
            total_seconds=66.5,remaining_preparation_seconds=53.5,remaining_activation_seconds=10,
            modeled_gesture_seconds=12,human_only_lower_bound=True,transport_and_device_cost_unknown=True,
            enrollment_feasibility_accepted=False,hardware_access=False))
        start=self.write(self.oldroot/'owner-workflow-start.json',dict(schema='OT-OWNER-WORKFLOW-START-1',
            session_nonce=self.old['session_nonce'],binding=self.oldref))
        oldplan=dict(binding=self.oldref,session_nonce=self.old['session_nonce'],review=None)
        oldplan['review']=self.write(self.private/'old-plan-review.json',dict(schema='OT-OWNER-WORKFLOW-REVIEW-1',
            plan_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in oldplan.items() if k!='review'}))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        oldplanref=self.write(self.private/'old-plan.json',oldplan)
        self.value=dict(schema='OT-VISIBLE-SESSION-BINDING-2',worktree=str(self.root),host=self.old['host'],assembly=self.old['assembly'],
            session=self.write(self.private/'newtemplate',{'synthetic':True}),sources=newpins,review=None,
            readiness_root=str(self.newready),session_output=str(self.newroot),session_nonce='b'*32)
        successor={k:self.value[k] for k in ('session_nonce','readiness_root','session_output','session','assembly','sources')}
        successor.update(fixed_inputs=self.write(self.private/'fixed.json',{'case':'first','group':1,'runtime':{'sha256':'f'*64}}),
            procedure=self.write(self.private/'procedure.json',{'synthetic':True}),static_review=self.write(self.private/'static-review.json',{'synthetic':True}))
        self.item=dict(schema=resume.MANIFEST_SCHEMA,predecessor=dict(binding=self.oldref,plan=oldplanref,archives=archives,
            readiness=ready,proof=proof,owner=owner,practice=practice,inventory={'owner-workflow-start.json':start},
            observation=self.write(self.private/'observation.json',{'terminal':'UNKNOWN','synthetic':True}),terminal_outcome='UNKNOWN'),
            successor=successor,review=None)
        self.item['review']=self.write(self.private/'resume-review.json',dict(schema='OT-PREDEVICE-SUCCESSION-REVIEW-1',
            manifest_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in self.item.items() if k!='review'}))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        self.value['predevice']=self.write(self.private/'resume.json',self.item)
        self.bound=self.write(self.private/'new-binding.json',self.value)
        self.write(self.newroot/'owner-workflow-start.json',dict(schema='OT-OWNER-WORKFLOW-START-1',session_nonce='b'*32,binding=self.bound))
        self.approval=dict(schema=resume.INTENT_SCHEMA,approved=True,source='fresh_human_chat_reply',received_utc=99999,
            binding=self.bound,case='first',group=1,execute_seconds=2100,total_seconds=3600,roles_ready={'A':True,'B':True})
        self.write(self.newroot/'owner-approval-intent.json',self.approval)
        self.patch=patch.object(visible,'ROOT',self.root);self.patch.start()
    def write(self,path,value):
        path.write_bytes(runtime.canonical(value)+b'\n');return visible.reference(path)
    def close(self):self.patch.stop();self.temp.cleanup()
    def sleep(self,seconds):self.now+=seconds
    def native(self):return dict(self.scope)
    def process(self,previous):self.process_calls+=1;return True
    def backend(self):
        fixture=self
        class Backend:
            def open(self,text):fixture.opened+=1;fixture.text=text;return True
            def poll(self):return fixture.event
            def clear(self):return fixture.clear
        return Backend()
    def revalidate(self):return resume.revalidate(self.bound['path'],self.bound['sha256'],
        admission=self.native,process_checker=self.process,monotonic=lambda:self.now,utc=lambda:self.now+self.utc_offset,
        sleep=self.sleep,backend_factory=self.backend,binding_admitter=lambda *args:(self.value,self.bound))
    def require(self,fresh=True):return resume.require(self.value,self.bound,admission=self.native,
        monotonic=lambda:self.now,utc=lambda:self.now+self.utc_offset,fresh=fresh)

class ResumeTests(unittest.TestCase):
    def fixture(self):
        f=Fixture();self.addCleanup(f.close);return f
    def selected(self, f, case='retained_rekey', group=1):
        fixed={'case':case,'group':group,'runtime':{'sha256':'f'*64}}
        f.item['successor']['fixed_inputs']=f.write(f.private/'fixed.json',fixed)
        f.item['review']=f.write(f.private/'resume-review.json',dict(schema='OT-PREDEVICE-SUCCESSION-REVIEW-1',
            manifest_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in f.item.items() if k!='review'}))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        f.value['predevice']=f.write(f.private/'resume.json',f.item)
        f.bound=f.write(f.private/'new-binding.json',f.value)
        f.write(f.newroot/'owner-workflow-start.json',dict(schema='OT-OWNER-WORKFLOW-START-1',
            session_nonce='b'*32,binding=f.bound))
        f.approval.update(binding=f.bound,case=case,group=group)
        f.write(f.newroot/'owner-approval-intent.json',f.approval)
        return fixed
    def test_retained_selection_is_exact_in_readiness_and_derived_authorization(self):
        f=self.fixture();self.selected(f)
        ref=f.revalidate();self.assertEqual(f.require(),f.value)
        auth=json.loads((f.newroot/'owner-authorization.json').read_bytes())
        self.assertEqual((auth['case'],auth['group']),('retained_rekey',1))
        self.assertEqual(auth['visible_readiness'],ref)
        self.assertEqual((auth['execute_seconds'],auth['total_seconds']),(2100,3600))
    def test_revoke_selection_is_exact_in_readiness_and_derived_authorization(self):
        f=self.fixture();self.selected(f,'revoke')
        before={p.name:p.read_bytes() for p in f.oldready.iterdir()}
        ref=f.revalidate();self.assertEqual(f.require(),f.value)
        receipt=json.loads(Path(ref['path']).read_bytes())
        auth=json.loads((f.newroot/'owner-authorization.json').read_bytes())
        self.assertEqual((auth['case'],auth['group']),('revoke',1))
        self.assertEqual(auth['visible_readiness'],ref)
        self.assertEqual(auth['visible_binding_sha256'],f.bound['sha256'])
        self.assertEqual(auth['session_nonce'],f.value['session_nonce'])
        self.assertEqual(auth['fixed_inputs_sha256'],f.item['successor']['fixed_inputs']['sha256'])
        self.assertEqual((auth['execute_seconds'],auth['total_seconds']),(2100,3600))
        self.assertEqual(auth['roles_ready'],{'A':True,'B':True})
        self.assertEqual(receipt['approval'],visible.reference(f.newroot/'owner-approval-intent.json'))
        self.assertTrue(receipt['explicit_current_readability']);self.assertTrue(receipt['ready_now'])
        self.assertFalse(receipt['hardware_authority']);self.assertFalse(receipt['enrollment_acceptance'])
        self.assertEqual(before,{p.name:p.read_bytes() for p in f.oldready.iterdir()})
        self.assertEqual(f.opened,1);self.assertEqual(f.process_calls,2)
    def test_cross_case_approvals_refuse_before_popup(self):
        cases=('first','retained_rekey','revoke')
        for selected,approved in ((s,a) for s in cases for a in cases if s!=a):
            f=self.fixture();self.selected(f,selected)
            f.approval['case']=approved;f.write(f.newroot/'owner-approval-intent.json',f.approval)
            with self.subTest(selected=selected,approved=approved),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,0);self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_unknown_boolean_and_wrong_group_selections_refuse_before_popup(self):
        invalid_cases=('unknown',True,'cancel','reset_preparation','startup_A',
            'recovery_after_A_commit','recovery_after_B_commit')
        selections=[(case,1) for case in invalid_cases]
        selections.extend((case,group) for case in ('first','retained_rekey','revoke')
            for group in (True,1.0,'1',0,2,-1))
        for case,group in selections:
            f=self.fixture();self.selected(f,case,group)
            with self.subTest(case=case,group=group),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,0);self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_fixed_selection_changes_during_popup_refuse_without_authorization(self):
        for case in ('retained_rekey','revoke'):
            f=self.fixture();self.selected(f,case);original=f.backend
            def backend():
                value=original()
                def poll():
                    f.write(f.private/'fixed.json',{'case':'first','group':1,'runtime':{'sha256':'f'*64}})
                    return 'confirm'
                value.poll=poll;return value
            f.backend=backend
            with self.subTest(case=case),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,1);self.assertFalse((f.newroot/'owner-authorization.json').exists())
            self.assertEqual(list(f.newready.iterdir()),[])
    def test_revoke_intent_changes_during_popup_refuse_without_authorization(self):
        f=self.fixture();self.selected(f,'revoke');original=f.backend
        def backend():
            value=original()
            def poll():
                f.approval['case']='retained_rekey'
                f.write(f.newroot/'owner-approval-intent.json',f.approval)
                return 'confirm'
            value.poll=poll;return value
        f.backend=backend
        with self.assertRaisesRegex(visible.AdmissionError,'owner_authorization_changed'):f.revalidate()
        self.assertEqual(f.opened,1);self.assertFalse((f.newroot/'owner-authorization.json').exists())
        self.assertEqual(list(f.newready.iterdir()),[])
    def test_revoke_consumed_predecessor_refuses_before_popup(self):
        f=self.fixture();self.selected(f,'revoke')
        (f.oldroot/'capture-grant.json').write_bytes(b'inert consumed predecessor fixture')
        with self.assertRaisesRegex(visible.AdmissionError,'resume_device_state_present'):f.revalidate()
        self.assertEqual(f.opened,0);self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_revoke_readiness_cancel_clear_and_expiry_refuse_without_authorization(self):
        for kind in ('cancel','clear','approval_expired','native'):
            f=self.fixture();self.selected(f,'revoke');original=f.backend
            if kind=='cancel':f.event='cancel'
            if kind=='clear':f.clear=False
            def backend():
                value=original()
                def poll():
                    if kind=='approval_expired':f.now+=900
                    if kind=='native':f.scope['input_desktop']=False
                    return f.event
                value.poll=poll;return value
            f.backend=backend
            with self.subTest(kind=kind),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(list(f.newready.iterdir()),[])
            self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_old_expired_proof_is_preserved_and_fresh_check_precedes_authorization(self):
        f=self.fixture();before={p.name:p.read_bytes() for p in f.oldready.iterdir()}
        ref=f.revalidate();self.assertEqual(f.require(),f.value)
        self.assertEqual(before,{p.name:p.read_bytes() for p in f.oldready.iterdir()})
        auth=json.loads((f.newroot/'owner-authorization.json').read_bytes())
        self.assertEqual(auth['received_utc'],'1970-01-02T03:46:39+00:00')
        self.assertEqual(auth['visible_readiness'],ref);self.assertEqual(f.process_calls,2)
        self.assertNotIn('ROW',f.text);self.assertNotIn('Button',f.text)
    def test_any_prior_or_current_device_state_refuses_before_popup(self):
        for folder,name in (('oldroot','capture-grant.json'),('oldroot','launch-intent.json'),
            ('oldroot','enrollment-original-any.json'),('newroot','capture-grant.json'),('newroot','worker.json')):
            f=self.fixture();getattr(f,folder).joinpath(name).write_bytes(b'synthetic')
            with self.subTest(folder=folder,name=name),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,0)
    def test_strict_approval_fields_and_expiry_refuse_before_popup(self):
        fields=(('received_utc',99099),('roles_ready',{'A':1,'B':True}),('execute_seconds',2100.0),
                ('total_seconds',3600.0),('group',True),('source','inferred_reply'),
                ('approved',False),('binding',{'sha256':'a'*64}))
        for case,key,bad in ((case,key,bad) for case in ('first','retained_rekey','revoke') for key,bad in fields):
            f=self.fixture();self.selected(f,case)
            f.approval[key]=bad;f.write(f.newroot/'owner-approval-intent.json',f.approval)
            with self.subTest(case=case,key=key),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,0)
    def test_changed_historical_bytes_and_ui_source_refuse(self):
        for target in ('proof','practice'):
            f=self.fixture();Path(f.item['predecessor'][target]['path']).write_bytes(b'changed')
            with self.subTest(target=target),self.assertRaises(Exception):f.revalidate()
        f=self.fixture();f.value['sources']['enrollment_candidate_private_view.py']={'bytes':1,'sha256':'a'*64}
        with self.assertRaises(Exception):f.revalidate()
    def test_changed_logon_cancel_or_failed_clear_produces_no_current_receipt(self):
        for kind in ('logon','cancel','clear'):
            f=self.fixture()
            if kind=='logon':
                original=f.backend
                def backend():
                    value=original()
                    def poll():
                        f.scope['logon_sha256']='b'*64
                        return 'confirm'
                    value.poll=poll
                    return value
                f.backend=backend
            if kind=='cancel':f.event='cancel'
            if kind=='clear':f.clear=False
            with self.subTest(kind=kind),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(list(f.newready.iterdir()),[])
            self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_prior_session_and_reboot_do_not_change_historical_evidence_or_current_clocks(self):
        for changed in ('session','reboot'):
            f=self.fixture();before={p.name:p.read_bytes() for p in f.oldready.iterdir()}
            f.scope['session_id']=10;f.scope['logon_sha256']='b'*64
            if changed=='reboot':f.now=100;f.utc_offset=99900
            with self.subTest(changed=changed):
                ref=f.revalidate();self.assertEqual(f.require(),f.value)
            receipt=json.loads(Path(ref['path']).read_bytes())
            self.assertEqual(receipt['scope'],f.scope)
            self.assertEqual(receipt['finished_monotonic'],f.now)
            self.assertEqual(receipt['finished_utc'],100000)
            self.assertEqual(before,{p.name:p.read_bytes() for p in f.oldready.iterdir()})
    def test_history_never_uses_current_admission_or_creates_current_authority(self):
        f=self.fixture()
        def forbidden():raise AssertionError('historical validation used current context')
        self.assertEqual(resume.history(f.value,f.bound,forbidden,forbidden,forbidden),(f.item,f.old))
        self.assertEqual(list(f.newready.iterdir()),[])
        self.assertFalse((f.newroot/'owner-authorization.json').exists())
        with self.assertRaises(Exception):f.require()
    def test_current_native_loss_and_clock_rollback_cancel_before_current_receipt(self):
        for kind in ('native','utc','monotonic','approval_expired'):
            f=self.fixture();original=f.backend
            def backend():
                value=original()
                def poll():
                    if kind=='native':f.scope['input_desktop']=False
                    if kind=='utc':f.utc_offset=-10
                    if kind=='monotonic':f.now-=10
                    if kind=='approval_expired':f.now+=900
                    return 'confirm'
                value.poll=poll;return value
            f.backend=backend
            with self.subTest(kind=kind),self.assertRaises(Exception):f.revalidate()
            self.assertEqual(list(f.newready.iterdir()),[])
            self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_current_scope_loss_immediately_before_authorization_does_not_issue_authority(self):
        f=self.fixture();original=visible.save_once
        def save(path,value):
            result=original(path,value)
            if Path(path).name=='readiness.json':f.scope['session_id']+=1
            return result
        with patch.object(visible,'save_once',side_effect=save),self.assertRaises(Exception):f.revalidate()
        self.assertFalse((f.newroot/'owner-authorization.json').exists())
        with self.assertRaises(Exception):f.require()
    def test_consumption_after_grant_is_allowed_but_reissue_refuses(self):
        for case in ('first','retained_rekey','revoke'):
            f=self.fixture();self.selected(f,case);f.revalidate()
            auth=(f.newroot/'owner-authorization.json').read_bytes()
            (f.newroot/'capture-grant.json').write_bytes(b'synthetic')
            with self.subTest(case=case):
                self.assertEqual(f.require(),f.value)
                with self.assertRaises(Exception):f.revalidate()
            self.assertEqual(f.opened,1)
            self.assertEqual((f.newroot/'owner-authorization.json').read_bytes(),auth)
    def test_current900_expires_but_held_anchor_is_not_renewed(self):
        for case in ('first','retained_rekey','revoke'):
            f=self.fixture();self.selected(f,case);f.revalidate();anchor=resume.anchor(f.value);f.now+=900
            with self.subTest(case=case):
                with self.assertRaises(Exception):f.require()
                self.assertEqual(f.require(fresh=False),f.value)
            self.assertEqual(resume.anchor(f.value),anchor)
    def test_resumed_initial_launch_uses_new_anchor_then_held_original_ceiling(self):
        f=self.fixture();f.revalidate()
        ready=visible.reference(f.newready/'readiness.json')
        auth=visible.reference(f.newroot/'owner-authorization.json')
        request=f.write(f.newroot/'request.json',{'synthetic':True})
        grant=dict(attempt='d'*32,issued_utc=100000,capture_expires_utc=102100,cleanup_expires_utc=103600,request_sha256='e'*64)
        grant_ref=f.write(f.newroot/'capture-grant.json',grant)
        package=f.write(f.newroot/'capture-package.json',dict(schema='OT-ORIGINAL-CAPTURE-RUNNER-1',operation='capture',
            evidence_root=str(f.newroot),assembly=f.value['assembly'],grant=grant_ref,request=request))
        f.write(f.newroot/'launch-intent.json',dict(schema='OT0238C-LAUNCH-INTENT-1',utc='synthetic',package=package,
            one_attempt=True,visible_readiness=ready,visible_binding_sha256=f.bound['sha256'],capture_grant=grant_ref,
            started_utc=100001,started_monotonic=100001,owner_authorization=auth))
        f.write(f.newroot/'enrollment-original-active.json',dict(attempt=grant['attempt'],request_sha256=grant['request_sha256']))
        f.write(f.newroot/('enrollment-original-'+grant['attempt']+'.json'),dict(attempt=grant['attempt'],phase='held',capture_complete=True))
        f.now=100901
        def held():return visible.require_continuity(f.bound['path'],f.bound['sha256'],admission=f.native,
            monotonic=lambda:f.now,utc=lambda:f.now,binding_admitter=lambda *args:(f.value,f.bound),grant_validator=lambda *args:grant)
        self.assertEqual(held(),f.value)
        grant['capture_expires_utc']=102101
        with self.assertRaises(Exception):held()
    def test_future_expiry_is_persisted_and_duplicate_start_never_dispatches(self):
        f=ParentFixture();self.addCleanup(f.cleanup)
        original=f.run
        def run(args,**kwargs):
            action=args[args.index('--action')+1]
            if action=='practice':
                f.actions.append(action)
                return type('Result',(),dict(returncode=0,stderr=b'',stdout=f.result(action)))()
            return original(args,**kwargs)
        workflow=f.workflow();workflow.run=run
        with self.assertRaisesRegex(flow.Refused,'wait_expired'):workflow.execute()
        saved=json.loads((f.output/'owner-workflow-predevice-result.json').read_bytes())
        self.assertEqual(workflow.predevice_receipt,'saved')
        self.assertEqual(saved['phase'],'waiting_trial_authorization');self.assertFalse(saved['device_attempt_started'])
        self.assertNotIn('issue_capture',f.actions)
        before=list(f.actions)
        with self.assertRaises(FileExistsError):workflow.execute()
        self.assertEqual(before,f.actions)
    def test_process_uncertainty_refuses_before_popup(self):
        f=self.fixture()
        with patch.object(f,'process',return_value=False),self.assertRaises(Exception):f.revalidate()
        self.assertEqual(f.opened,0)
    def test_schema1_remains17_schema2_has18_pinned_helpers(self):
        self.assertEqual(len(visible.SOURCE_NAMES),17);self.assertEqual(len(visible.RESUME_SOURCE_NAMES),18)
    def test_parked_parent_has_one_entry_process_gate_and_no_probe_or_practice(self):
        f=ParentFixture();self.addCleanup(f.cleanup)
        f.save(f.binding,{'schema':'OT-VISIBLE-SESSION-BINDING-2'})
        f.plan['binding']=flow.reference(f.binding)
        old_sleep=f.sleep
        count=[0]
        def sleep(seconds):
            count[0]+=1
            if 'launch' in f.actions:old_sleep(seconds)
            else:f.now+=seconds
            if count[0]==5:f.save(f.output/'owner-approval-intent.json',{'synthetic':True})
        original=f.run
        def run(args,**kwargs):
            action=args[args.index('--action')+1]
            if action=='revalidate':
                f.actions.append(action);f.save(f.output/'owner-authorization.json',{'synthetic':True})
                return type('Result',(),dict(returncode=0,stderr=b'',stdout=flow.canonical(dict(
                    schema='OT-VISIBLE-SESSION-RESULT-1',action='revalidate',outcome='completed',category=None,receipt={'synthetic':True}))+b'\n'))()
            return original(args,**kwargs)
        workflow=f.workflow();workflow.sleep=sleep;workflow.run=run
        with patch.object(resume,'manifest',return_value=({},{})),patch.object(resume,'predecessor_idle') as guard:
            workflow.execute();self.assertEqual(guard.call_count,1)
        self.assertGreaterEqual(count[0],5);self.assertEqual(f.actions[0],'revalidate')
        self.assertNotIn('probe',f.actions);self.assertNotIn('practice',f.actions)

class TransitionFixture:
    """Metadata doubles only; no SDK files, import, inventory or verifier call."""
    def __init__(self, cleanup=False):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name);self.private=self.root/'.private';self.private.mkdir()
        self.review_path=self.private/'ot0238c-retained-diagnosis-20261010'/'independent-review.json'
        self.review_path.parent.mkdir()
        review=self.write(self.review_path,dict(schema='OT0238C-INDEPENDENT-RESTART-DIAGNOSTIC-REVIEW-1',
            outcome='no_unresolved_findings',findings=[]))
        expected_pins=resume.CLEANUP_TRANSITION_PINS if cleanup else resume.TRANSITION_PINS
        self.cleanup_paths={};cleanup_pins={}
        if cleanup:
            evidence_root=self.private/'ot0238c-capture-mismatch-20261011';evidence_root.mkdir()
            source_ref=self.write(evidence_root/'source-changes.json',[{'synthetic':'reviewed source delta'}])
            validation_ref=self.write(evidence_root/'child-validation.json',{'synthetic':'137 isolated tests'})
            cleanup_ref=self.write(evidence_root/'independent-source-review.json',dict(
                schema='OT-HOST-INDEPENDENT-REVIEW-1',outcome='approved_host_source_only',remaining_findings=[],
                source_manifest={k:source_ref[k] for k in ('path','sha256')},
                validation=dict({k:validation_ref[k] for k in ('path','sha256')},
                    tests=137,all_four_isolated_exit_codes=0)))
            for ref in (source_ref,validation_ref,cleanup_ref):
                name=Path(ref['path']).name;self.cleanup_paths[name]=Path(ref['path'])
                cleanup_pins[name]={k:ref[k] for k in ('bytes','sha256')}
            recovery_root=self.private/'ot0238c-launcher-recovery-20261011'/'recovery'
            recovery_root.mkdir(parents=True)
            module=recovery_root/'host-recovery-gate.py'
            module.write_bytes(b'class RecoveryDispositionError(RuntimeError): pass\n'
                b'def require_candidate_entry_allowed(path, sha256, private):\n'
                b'    raise RecoveryDispositionError("recovery_baseline_unresolved")\n')
            module_ref=visible.reference(module)
            disposition_ref=self.write(recovery_root/'manual-recovery-observed-baseline-unresolved.json',
                {'candidate_entry_allowed':False,'synthetic':'unresolved'})
            self.gate=dict(schema=resume.RECOVERY_GATE_SCHEMA,module=module_ref,disposition=disposition_ref,
                expected_private_root=str(self.private))
            self.gate_pins={key:{k:self.gate[key][k] for k in ('bytes','sha256')}
                for key in ('module','disposition')}
        self.base_root=self.private/'base';self.base_root.mkdir()
        self.base=dict(schema='OT212-DEADLINE-RUNTIME-1',root=str(self.base_root),worktree=str(self.root),
            files={'SDK/library':runtime.descriptor(b'inert SDK marker')},versions={'python':'fixture'},
            powershell={'fixture':'unchanged external host'})
        self.base_ref=self.write(self.private/'base.json',self.base)
        self.assemblies={};self.manifests={};self.bindings={}
        for side,which in (('predecessor','before'),('successor','after')):
            root=self.private/side;root.mkdir()
            sources={'tools/'+name:copy.deepcopy(expected_pins[name][which]) if name in expected_pins
                else runtime.descriptor(('unchanged '+name).encode()) for name in runtime.SOURCE_NAMES}
            pins=self.write(self.private/(side+'-pins.json'),dict(schema='OT-CANDIDATE-SOURCE-PINS-1',files=sources))
            files=dict(self.base['files']);files.update({'policy/'+Path(name).name:pin for name,pin in sources.items()})
            self.manifests[side]=dict(self.base,root=str(root),files=files)
            manifest_ref=self.write(self.private/(side+'-runtime.json'),self.manifests[side])
            self.assemblies[side]=dict(schema='OT-CANDIDATE-ASSEMBLY-1',root=str(root),source_root=str(self.root),
                base=self.base_ref,source_pins=pins,sources=sources,runtime=manifest_ref)
            assembly_ref=self.write(self.private/(side+'-assembly.json'),self.assemblies[side])
            self.bindings[side]=dict(worktree=str(self.root),assembly=assembly_ref,
                sources={Path(name).name:pin for name,pin in sources.items()})
        self.transition=dict(schema=resume.TRANSITION_SCHEMA,
            kind=resume.CLEANUP_TRANSITION_KIND if cleanup else 'ot0238c-restart-diagnostic',review=review,
            sources=copy.deepcopy(expected_pins),**{side+'_'+kind:
                self.bindings[side]['assembly'] if kind=='assembly' else self.assemblies[side]['runtime']
                for side in ('predecessor','successor') for kind in ('assembly','runtime')})
        self.patches=[patch.object(visible,'ROOT',self.root),patch.object(resume,'TRANSITION_REVIEW_PIN',
            {k:review[k] for k in ('bytes','sha256')})]
        if cleanup:
            self.transition['cleanup_review']=cleanup_ref
            self.transition['recovery_gate']=self.gate
            self.patches.append(patch.object(resume,'CLEANUP_EVIDENCE_PINS',cleanup_pins))
            self.patches.append(patch.object(resume,'RECOVERY_GATE_PINS',self.gate_pins))
        for p in self.patches:p.start()
    def write(self,path,value):
        path.write_bytes(runtime.canonical(value)+b'\n');return visible.reference(path)
    def repin(self,side='successor'):
        assembly=self.assemblies[side]
        assembly['source_pins']=self.write(Path(assembly['source_pins']['path']),
            dict(schema='OT-CANDIDATE-SOURCE-PINS-1',files=assembly['sources']))
        assembly['runtime']=self.write(Path(assembly['runtime']['path']),self.manifests[side])
        ref=self.write(Path(self.bindings[side]['assembly']['path']),assembly)
        self.bindings[side]['assembly']=ref
        self.transition[side+'_assembly']=ref;self.transition[side+'_runtime']=assembly['runtime']
    def check(self):
        return resume.runtime_transition(dict(runtime_transition=self.transition),
            self.bindings['predecessor'],self.bindings['successor'])
    def close(self):
        for p in reversed(self.patches):p.stop()
        self.temp.cleanup()


class RuntimeTransitionTests(unittest.TestCase):
    def fixture(self):
        f=TransitionFixture();self.addCleanup(f.close);return f
    def test_exact_two_file_transition_uses_metadata_only(self):
        f=self.fixture()
        with patch.object(runtime,'verify_assembly',side_effect=AssertionError('historical SDK inventory')):
            self.assertIsNone(f.check())
        self.assertNotIn('serial',sys.modules);self.assertNotIn('esptool',sys.modules)
    def test_coherent_extra_source_change_refuses(self):
        f=self.fixture();name='enrollment_candidate_private_view.py';pin=runtime.descriptor(b'changed UI')
        f.assemblies['successor']['sources']['tools/'+name]=pin
        f.bindings['successor']['sources'][name]=pin
        f.manifests['successor']['files']['policy/'+name]=pin;f.repin()
        with self.assertRaisesRegex(visible.AdmissionError,'resume_sources_changed'):f.check()
    def test_sdk_firmware_or_metadata_delta_refuses_even_when_rebound(self):
        for kind in ('SDK/library','firmware/image','versions'):
            f=self.fixture();changed=copy.deepcopy(f.base)
            if kind=='versions':changed['versions']['python']='changed'
            else:changed['files'][kind]=runtime.descriptor(b'changed SDK or firmware')
            f.assemblies['successor']['base']=f.write(f.private/'changed-base.json',changed)
            f.manifests['successor'].update({k:v for k,v in changed.items() if k not in ('root','files')})
            f.manifests['successor']['files'].update(changed['files']);f.repin()
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_wrong_transition_pin_size_hash_and_keyset_refuse(self):
        for kind in ('hash','size','boolean_size','missing','extra'):
            f=self.fixture();pins=f.transition['sources']
            if kind=='hash':pins['enrollment_candidate_runner.py']['after']['sha256']='a'*64
            if kind=='size':pins['enrollment_candidate_runner.py']['after']['bytes']+=1
            if kind=='boolean_size':pins['enrollment_candidate_runner.py']['after']['bytes']=True
            if kind=='missing':pins.pop('enrollment_candidate_rom_adapter.py')
            if kind=='extra':pins['enrollment_candidate_private_view.py']=copy.deepcopy(pins['enrollment_candidate_runner.py'])
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_source_pins_binding_runtime_and_base_cross_bindings_refuse(self):
        for kind in ('source_pins','binding','runtime','base'):
            f=self.fixture()
            if kind=='source_pins':
                f.assemblies['successor']['source_pins']=f.assemblies['predecessor']['source_pins']
                ref=f.write(Path(f.bindings['successor']['assembly']['path']),f.assemblies['successor'])
                f.bindings['successor']['assembly']=ref;f.transition['successor_assembly']=ref
            if kind=='binding':f.bindings['successor']['assembly']=f.bindings['predecessor']['assembly']
            if kind=='runtime':f.transition['successor_runtime']=f.transition['predecessor_runtime']
            if kind=='base':
                f.assemblies['successor']['base']=f.write(f.private/'same-base-new-ref.json',f.base);f.repin()
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_changed_root_source_root_schema_and_unknown_key_refuse(self):
        for kind in ('root','source_root','schema','extra'):
            f=self.fixture();assembly=f.assemblies['successor']
            if kind=='root':
                assembly['root']=f.assemblies['predecessor']['root'];f.manifests['successor']['root']=assembly['root']
            if kind=='source_root':assembly['source_root']=str(f.private)
            if kind=='schema':assembly['schema']='unknown'
            if kind=='extra':assembly['unreviewed']=True
            f.repin()
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_changed_review_bytes_repointed_review_and_review_pin_refuse(self):
        for kind in ('bytes','repoint','pin'):
            f=self.fixture()
            if kind=='bytes':f.review_path.write_bytes(f.review_path.read_bytes()+b' ')
            if kind=='repoint':
                other=f.private/'other-review.json';other.write_bytes(f.review_path.read_bytes())
                f.transition['review']=visible.reference(other)
            if kind=='pin':f.transition['review']['sha256']='a'*64
            with self.subTest(kind=kind),self.assertRaises(Exception):f.check()
    def test_v1_does_not_accept_changed_assembly_or_v2_without_transition(self):
        for kind in ('assembly','missing_transition','extra_transition'):
            f=Fixture();self.addCleanup(f.close)
            if kind=='assembly':
                f.value['assembly']={'fixture':'changed'};f.item['successor']['assembly']=f.value['assembly']
            if kind=='missing_transition':f.item['schema']=resume.TRANSITION_MANIFEST_SCHEMA
            if kind=='extra_transition':f.item['runtime_transition']={}
            f.item['review']=f.write(f.private/'resume-review.json',dict(schema='OT-PREDEVICE-SUCCESSION-REVIEW-1',
                manifest_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in f.item.items() if k!='review'}))['sha256'],
                outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
            f.value['predevice']=f.write(f.private/'resume.json',f.item)
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):resume.manifest(f.value)


class CleanupRuntimeTransitionTests(RuntimeTransitionTests):
    def fixture(self):
        f=TransitionFixture(cleanup=True);self.addCleanup(f.close);return f
    def test_exact_two_file_transition_uses_metadata_only(self):
        # Repeat the old transition's adversarial matrix for the fixed four-file
        # successor. No historical SDK inventory or device code may be imported.
        f=self.fixture();self.assertEqual(len(f.transition['sources']),4)
        with patch.object(runtime,'verify_assembly',side_effect=AssertionError('historical SDK inventory')):
            self.assertIsNone(f.check())
        self.assertNotIn('serial',sys.modules);self.assertNotIn('esptool',sys.modules)
    def test_cleanup_requires_both_reviews_and_exact_transition_shape(self):
        for kind in ('missing_cleanup','missing_restart','missing_gate','unknown_kind','old_kind_with_cleanup','extra_flag'):
            f=self.fixture()
            if kind=='missing_cleanup':f.transition.pop('cleanup_review')
            if kind=='missing_restart':f.transition.pop('review')
            if kind=='missing_gate':f.transition.pop('recovery_gate')
            if kind=='unknown_kind':f.transition['kind']='caller-approved-cleanup'
            if kind=='old_kind_with_cleanup':f.transition['kind']='ot0238c-restart-diagnostic'
            if kind=='extra_flag':f.transition['approved']=True
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_each_fixed_before_and_after_source_pin_refuses_coherent_substitution(self):
        for name in resume.CLEANUP_TRANSITION_PINS:
            for side in ('predecessor','successor'):
                f=self.fixture();pin=runtime.descriptor(b'unreviewed but coherently repinned')
                f.assemblies[side]['sources']['tools/'+name]=pin
                f.bindings[side]['sources'][name]=pin
                f.manifests[side]['files']['policy/'+name]=pin;f.repin(side)
                with self.subTest(name=name,side=side),self.assertRaises(visible.AdmissionError):f.check()
    def test_cleanup_review_source_and_validation_bytes_are_required(self):
        for name in ('independent-source-review.json','source-changes.json','child-validation.json'):
            for change in ('missing','bytes'):
                f=self.fixture();path=f.cleanup_paths[name]
                if change=='missing':path.unlink()
                else:path.write_bytes(path.read_bytes()+b' ')
                with self.subTest(name=name,change=change),self.assertRaises(Exception):f.check()
    def test_repointed_cleanup_review_with_same_bytes_refuses(self):
        f=self.fixture();path=f.private/'caller-review.json'
        path.write_bytes(f.cleanup_paths['independent-source-review.json'].read_bytes())
        f.transition['cleanup_review']=visible.reference(path)
        with self.assertRaisesRegex(visible.AdmissionError,'review_required'):f.check()
    def test_cleanup_review_semantics_remain_bound_after_trusted_fixture_repin(self):
        for field,bad in (('schema','unknown'),('outcome','approved_hardware'),('remaining_findings',['open']),
                          ('source_manifest',{'sha256':'a'*64}),('validation',{'tests':137})):
            f=self.fixture();path=f.cleanup_paths['independent-source-review.json']
            review=json.loads(path.read_bytes());review[field]=bad
            ref=f.write(path,review);f.transition['cleanup_review']=ref
            resume.CLEANUP_EVIDENCE_PINS[path.name]={k:ref[k] for k in ('bytes','sha256')}
            with self.subTest(field=field),self.assertRaisesRegex(visible.AdmissionError,'review_required'):f.check()
    def test_cleanup_successor_still_refuses_consumed_predecessor_before_popup(self):
        f=Fixture();self.addCleanup(f.close)
        f.item['schema']=resume.TRANSITION_MANIFEST_SCHEMA
        f.item['runtime_transition']={'kind':resume.CLEANUP_TRANSITION_KIND}
        f.item['review']=f.write(f.private/'resume-review.json',dict(schema='OT-PREDEVICE-SUCCESSION-REVIEW-1',
            manifest_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in f.item.items() if k!='review'}))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        f.value['predevice']=f.write(f.private/'resume.json',f.item)
        f.bound=f.write(f.private/'new-binding.json',f.value)
        f.write(f.newroot/'owner-workflow-start.json',dict(schema='OT-OWNER-WORKFLOW-START-1',
            session_nonce='b'*32,binding=f.bound))
        f.approval['binding']=f.bound;f.write(f.newroot/'owner-approval-intent.json',f.approval)
        (f.oldroot/'capture-grant.json').write_bytes(b'consumed device authority')
        with patch.object(resume,'runtime_transition'),self.assertRaisesRegex(
                visible.AdmissionError,'resume_device_state_present'):f.revalidate()
        self.assertEqual(f.opened,0);self.assertFalse((f.newroot/'owner-authorization.json').exists())
    def test_fixed_gate_structural_check_never_conveys_operational_admission(self):
        f=self.fixture();self.assertIsNone(f.check())
        value=dict(schema='OT-VISIBLE-SESSION-BINDING-2',sources=f.bindings['successor']['sources'],
            predevice=f.write(f.private/'predevice-gate.json',dict(schema=resume.TRANSITION_MANIFEST_SCHEMA,
                runtime_transition=f.transition)))
        with self.assertRaisesRegex(visible.AdmissionError,'^recovery_baseline_unresolved$'):
            resume.require_recovery_gate(value)
    def test_missing_repointed_substituted_gate_module_disposition_and_root_refuse(self):
        for kind in ('missing_gate','module_bytes','disposition_bytes','module_repoint','disposition_repoint',
                     'root','extra','skip_flag'):
            f=self.fixture();gate=f.transition['recovery_gate']
            if kind=='missing_gate':f.transition.pop('recovery_gate')
            if kind=='module_bytes':Path(gate['module']['path']).write_bytes(b'grant entry')
            if kind=='disposition_bytes':Path(gate['disposition']['path']).write_bytes(b'{"candidate_entry_allowed":true}')
            if kind in ('module_repoint','disposition_repoint'):
                key=kind.split('_')[0];path=f.private/('other-'+key)
                path.write_bytes(Path(gate[key]['path']).read_bytes());gate[key]=visible.reference(path)
            if kind=='root':gate['expected_private_root']=str(f.root)
            if kind=='extra':gate['approved']=True
            if kind=='skip_flag':gate['skip_recovery_hold']=True
            with self.subTest(kind=kind),self.assertRaises(visible.AdmissionError):f.check()
    def test_cleanup_after_pins_require_gate_when_transition_kind_is_removed_or_renamed(self):
        for kind in ('removed','renamed'):
            f=self.fixture();transition=copy.deepcopy(f.transition)
            if kind=='removed':transition={}
            else:transition['kind']='ot0238c-restart-diagnostic'
            value=dict(schema='OT-VISIBLE-SESSION-BINDING-2',sources=f.bindings['successor']['sources'],
                predevice=f.write(f.private/'predevice-gate.json',dict(schema=resume.TRANSITION_MANIFEST_SCHEMA,
                    runtime_transition=transition)))
            with self.subTest(kind=kind),self.assertRaisesRegex(visible.AdmissionError,'^recovery_gate_invalid$'):
                resume.require_recovery_gate(value)




class FullSuccessorWorkflowOrderingTests(unittest.TestCase):
    def test_refusal_precedes_owner_marker_grant_native_and_sdk_dispatch(self):
        from enrollment_candidate_settings_successor_tests import FullFixture
        for fault in ('missing_predevice_stripped_sources','stripped_kind','physical_predecessor',
                'v19_predecessor','consumed_schema1_predecessor'):
            f=FullFixture(portable=True);self.addCleanup(f.close);f.bound_loader()
            # Each fault starts from a closure admitted by the real gate;
            # unrelated missing private evidence cannot satisfy a refusal.
            positive_dispatch=unittest.mock.Mock(side_effect=AssertionError('forbidden SDK/native/action dispatch'))
            with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                    patch.object(visible,'native_admission',positive_dispatch),\
                    patch.object(runtime,'verify_assembly',positive_dispatch):
                self.assertTrue(resume.require_recovery_gate(f.binding))
                self.assertIsNone(flow.require_cleanup_recovery(f.bound))
            positive_dispatch.assert_not_called()
            self.assertEqual(list(f.output.iterdir()),[])
            self.assertEqual(list(f.readiness.iterdir()),[])
            if fault=='missing_predevice_stripped_sources':
                f.binding.pop('predevice');f.binding['sources']={}
            elif fault=='stripped_kind':
                f.transition.pop('kind');f.refresh_full()
            elif fault=='physical_predecessor':
                f.previous['binding']=f.write(f.stage/'capture-only-binding.json',dict(schema='OT-VISIBLE-SESSION-BINDING-2'))
                f.refresh_full()
            elif fault=='v19_predecessor':
                f.transition['predecessor_assembly']=f.assembly;f.refresh_full()
            else:
                (f.old_output/'capture-grant.json').write_bytes(b'inert forbidden historical device-state fixture')
            bound=f.write(f.stage/'binding.json',f.binding)
            plan=dict(binding=bound,session_output=str(f.output),readiness_root=str(f.readiness),session_nonce='f'*32)
            dispatch=unittest.mock.Mock(side_effect=AssertionError('forbidden SDK/native/grant/action dispatch'))
            before={p.name:p.read_bytes() for p in f.output.iterdir()}
            with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                    patch.object(visible,'native_admission',dispatch),patch.object(runtime,'verify_assembly',dispatch),\
                    patch.object(resume,'no_predecessor_process',dispatch):
                workflow=flow.Workflow(plan,run=dispatch,popen=dispatch,notify=lambda text:None)
                with self.subTest(fault=fault),self.assertRaisesRegex(flow.Refused,'^recovery_gate_invalid$'):
                    workflow.execute()
            dispatch.assert_not_called()
            self.assertEqual(before,{p.name:p.read_bytes() for p in f.output.iterdir()})
            for name in ('owner-workflow-start.json','owner-workflow-predevice-result.json',
                    'owner-authorization.json','capture-grant.json','enrollment-original-active.json','enrollment-candidate-active.json'):
                self.assertFalse((f.output/name).exists(),(fault,name))
            self.assertEqual(list(f.readiness.iterdir()),[])

if __name__=='__main__':
    assert sys.dont_write_bytecode
    unittest.main(verbosity=2)
