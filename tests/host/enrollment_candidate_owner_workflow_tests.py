"""Mocked orchestration boundaries; no UI, grants, transport or device execution."""
import ast
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_owner_workflow as flow
import enrollment_candidate_visible_session as visible_session


class Fixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.ready = self.root / 'ready'; self.ready.mkdir()
        self.output = self.root / 'output'; self.output.mkdir()
        self.tools = self.root / 'tools'; self.tools.mkdir()
        self.module = self.tools / 'enrollment_candidate_owner_workflow.py'
        self.entry = self.tools / 'enrollment_candidate_visible_session.py'
        self.module.write_bytes(b'mocked module'); self.entry.write_bytes(b'mocked entry')
        self.host = self.root / 'python.exe'; self.host.write_bytes(b'mocked host')
        self.binding = self.root / 'binding.json'; self.binding.write_bytes(b'{}')
        self.plan = dict(host=flow.reference(self.host), entrypoint=flow.reference(self.entry),
                         module=flow.reference(self.module), binding=flow.reference(self.binding),
                         readiness_root=str(self.ready), session_output=str(self.output), session_nonce='b' * 32)
        self.now = 10000.0
        self.actions = []; self.notices = []; self.fail = None; self.release_change = None
        self.child_done = False; self.cleanup_waited = False; self.no_review = False
        self.worker_hang = False; self.premature = False
        self.prepare_exit_delay = 0
        self.trial_outcome = 'passed'
        self.grant = dict(attempt='a' * 32, issued_utc=self.now,
                          capture_expires_utc=self.now + 2100, cleanup_expires_utc=self.now + 3600)

    def cleanup(self): self.temp.cleanup()

    def save(self, path, value): path.write_bytes(flow.canonical(value) + b'\n')

    def result(self, action):
        value=dict(schema='OT-VISIBLE-SESSION-RESULT-1', action=action, outcome='completed', category=None)
        if action in ('probe','attest','practice','revalidate'): value['receipt']={'mocked':True}
        return flow.canonical(value) + b'\n'

    def run(self, args, **kwargs):
        action = args[args.index('--action') + 1]; self.actions.append(action)
        if self.fail == action:
            return SimpleNamespace(returncode=1, stdout=self.result(action), stderr=b'')
        if action == 'probe': self.save(self.ready / 'owner-reply.json', {'actual': 'mocked human fixture'})
        elif action == 'practice': self.save(self.output / 'owner-authorization.json', {'actual': 'mocked authorization'})
        elif action == 'issue_capture': self.save(self.output / 'capture-grant.json', self.grant)
        elif action == 'prepare_handoff':
            self.save(self.output / 'candidate-grant.json', dict(attempt='d'*32,execute_expires_utc=self.grant['capture_expires_utc'],
                                                               restore_expires_utc=self.grant['cleanup_expires_utc']))
            self.save(self.output / 'candidate-package.json', {'fixture': 'candidate'})
            self.save(self.output / 'handoff-admission.json',
                      {'utc':datetime.datetime.fromtimestamp(self.now,datetime.timezone.utc).isoformat(),
                       'candidate_package': flow.reference(self.output / 'candidate-package.json')})
            if not self.no_review:
                release = dict(schema='OT-OWNER-HANDOFF-REVIEW-RELEASE-1',
                               source='independent_current_handoff_review', outcome='no_unresolved_findings',
                               findings=[], session_nonce=self.plan['session_nonce'], binding=self.plan['binding'],
                               received_utc=self.now)
                for key, name in (('handoff_admission','handoff-admission.json'),
                                  ('candidate_package','candidate-package.json'),
                                  ('capture_grant','capture-grant.json'), ('candidate_grant','candidate-grant.json')):
                    release[key] = flow.reference(self.output / name)
                if self.release_change:
                    release.update(self.release_change)
                self.save(self.output / 'handoff-review-release.json', release)
            self.now += self.prepare_exit_delay
        elif action == 'publish_handoff': self.child_done = True
        return SimpleNamespace(returncode=0, stdout=b'{"phase":"fixture"}\n' + self.result(action), stderr=b'')

    def popen(self, args, **kwargs):
        self.actions.append('launch')
        self.assert_no_capture_pipe = kwargs['stdout'] is not subprocess.PIPE
        kwargs['stdout'].write(flow.canonical(dict(schema='OT-ORIGINAL-CAPTURE-LAUNCH-1',mode='capture',
                                outcome=self.trial_outcome,category='target_refused' if self.trial_outcome=='failed' else None))+b'\n'+self.result('launch'))
        self.save(self.output/('enrollment-original-runner-'+self.grant['attempt']+'.json'),
                  dict(schema='OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1',operation='capture',attempt=self.grant['attempt'],
                       outcome=self.trial_outcome,lease_released=True,capture_custody_released=True,
                       cleanup_failure=None,candidate_cleanup_failure=None,capture_cleanup_failure=None,
                       candidate_outcome=self.trial_outcome,first_failure=['candidate','target_refused'] if self.trial_outcome=='failed' else None))
        if not self.premature:
            self.save(self.output / ('enrollment-original-' + self.grant['attempt'] + '.json'),
                      dict(attempt=self.grant['attempt'], phase='held', capture_complete=True,
                           first_failure=None, cleanup_failure=None,
                           roles={role:dict(entered=True, handles_closed=True, owner='capture') for role in ('A','B')}))
        fixture = self
        class Child:
            returncode = 0
            def poll(self): return 0 if fixture.child_done or fixture.premature else None
            def wait(self): fixture.cleanup_waited = True; return 0
            def terminate(self): raise AssertionError('must never terminate')
            def kill(self): raise AssertionError('must never kill')
        return Child()

    def sleep(self, amount):
        self.now += amount
        # Existing worker restores on its own, including sibling refusal.
        if self.no_review:
            if self.now >= self.grant['capture_expires_utc']: self.child_done = True
        elif not self.worker_hang:
            self.child_done = True

    def workflow(self):
        return flow.Workflow(self.plan, run=self.run, popen=self.popen, monotonic=lambda:self.now,
                             utc=lambda:self.now, sleep=self.sleep, notify=self.notices.append)


class WorkflowTests(unittest.TestCase):
    def fixture(self):
        value = Fixture(); self.addCleanup(value.cleanup); return value

    def reviewed_plan(self, f, outcome='no_unresolved_findings'):
        private=f.root/'.private'; private.mkdir()
        ready=private/'ready'; ready.mkdir(); output=private/'output'; output.mkdir()
        plan=dict(f.plan,schema='OT-OWNER-WORKFLOW-PLAN-1',worktree=str(f.root),
                  readiness_root=str(ready),session_output=str(output))
        f.save(f.binding,dict(worktree=plan['worktree'],host=plan['host'],
                             session_nonce=plan['session_nonce'],readiness_root=str(ready),session_output=str(output)))
        plan['binding']=flow.reference(f.binding)
        review=private/'review.json'
        f.save(review,dict(schema='OT-OWNER-WORKFLOW-REVIEW-1',
                           plan_core_sha256=hashlib.sha256(flow.canonical(plan)).hexdigest(),
                           outcome=outcome,findings=[],hardware_access=False,live_grants_created=False))
        plan['review']=flow.reference(review)
        path=private/'plan.json'; f.save(path,plan)
        return path,flow.reference(path)['sha256'],plan

    def admit(self,f,path,pin):
        with patch.object(flow,'ROOT',f.root),patch.object(flow,'__file__',str(f.module)),patch.object(sys,'executable',str(f.host)):
            return flow.admit_plan(path,pin)

    def test_exact_reviewed_plan_admission_and_wrong_review_rejects(self):
        f=self.fixture(); path,pin,plan=self.reviewed_plan(f)
        self.assertEqual(self.admit(f,path,pin),plan)
        f=self.fixture(); path,pin,_=self.reviewed_plan(f,outcome='pending')
        with self.assertRaisesRegex(flow.Refused,'review_required'): self.admit(f,path,pin)

    def recovery_fixture(self):
        from enrollment_candidate_interactive_launch_tests import RecoveryBindingFixture
        f=RecoveryBindingFixture();self.addCleanup(f.close)
        self.enterContext(patch.object(flow,'ROOT',f.root))
        self.enterContext(patch.object(flow,'__file__',str(f.root/'tools/enrollment_candidate_owner_workflow.py')))
        return f

    def recovery_plan(self,f):
        plan=dict(schema='OT-OWNER-WORKFLOW-PLAN-1',worktree=str(f.root),host=f.binding['host'],
            entrypoint=flow.reference(f.root/'tools/enrollment_candidate_visible_session.py'),
            module=flow.reference(f.root/'tools/enrollment_candidate_owner_workflow.py'),binding=f.bound,
            session_nonce=f.binding['session_nonce'],readiness_root=str(f.ready),session_output=str(f.output))
        review=f.private/'owner-review.json'
        f.write(review,dict(schema='OT-OWNER-WORKFLOW-REVIEW-1',
            plan_core_sha256=hashlib.sha256(flow.canonical(plan)).hexdigest(),outcome='no_unresolved_findings',
            findings=[],hardware_access=False,live_grants_created=False))
        plan['review']=flow.reference(review);path=f.private/'owner-plan.json';ref=f.write(path,plan)
        return plan,path,ref['sha256']

    def test_cleanup_direct_plan_and_constructor_refuse_before_marker_or_dispatch(self):
        f=self.recovery_fixture();plan,path,pin=self.recovery_plan(f)
        with self.assertRaisesRegex(flow.Refused,'^recovery_baseline_unresolved$'):flow.admit_plan(path,pin)
        with patch.object(subprocess,'run',side_effect=AssertionError('dispatch')),patch.object(
                subprocess,'Popen',side_effect=AssertionError('dispatch')):
            with self.assertRaisesRegex(flow.Refused,'^recovery_baseline_unresolved$'):
                flow.Workflow(plan).execute()
        self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])

    def test_cleanup_schema1_downgrade_and_missing_gate_never_create_start_marker(self):
        for change in ('schema1','missing_gate','renamed_kind','removed_transition','missing_predevice','changed_helper'):
            f=self.recovery_fixture()
            if change=='schema1':
                f.binding['schema']='OT-VISIBLE-SESSION-BINDING-1';f.binding.pop('predevice')
                f.binding['sources'].pop('enrollment_candidate_predevice_resume.py')
            if change=='missing_gate':f.item['runtime_transition'].pop('recovery_gate')
            if change=='renamed_kind':f.item['runtime_transition']['kind']='ot0238c-restart-diagnostic'
            if change=='removed_transition':f.item.pop('runtime_transition')
            if change=='missing_predevice':f.binding.pop('predevice')
            if change=='changed_helper':(f.root/'tools/enrollment_candidate_predevice_resume.py').write_bytes(b'unreviewed')
            if change in ('schema1','missing_predevice'):f.bound=f.write(f.binding_path,f.binding)
            else:f.rebind()
            plan,path,pin=self.recovery_plan(f)
            with self.subTest(change=change):
                with self.assertRaisesRegex(flow.Refused,'^recovery_gate_invalid$'):flow.admit_plan(path,pin)
                with self.assertRaisesRegex(flow.Refused,'^recovery_gate_invalid$'):flow.Workflow(plan).execute()
            self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])

    def test_reviewed_plan_tampering_source_or_plan_bytes_rejects(self):
        for target in ('module','entrypoint','binding','plan','review'):
            f=self.fixture(); path,pin,plan=self.reviewed_plan(f)
            changed=path if target=='plan' else Path(plan[target]['path'])
            changed.write_bytes(b'changed')
            with self.assertRaises(flow.Refused): self.admit(f,path,pin)

    def test_duplicate_keys_and_nonfinite_json_rejects(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}'):
            with self.assertRaises(flow.Refused): flow.decode(raw)

    def test_success_async_handoff_then_review_then_publish_and_wait(self):
        f = self.fixture(); f.workflow().execute()
        self.assertEqual(f.actions, ['probe','attest','practice','baseline','route','issue_capture',
                                   'launch','status','prepare_handoff','publish_handoff'])
        self.assertTrue(f.assert_no_capture_pipe)
        self.assertTrue(f.cleanup_waited)
        self.assertFalse((f.output/'owner-workflow-predevice-result.json').exists())

    def resumed_execution(self,f,workflow):
        import enrollment_candidate_predevice_resume as resume
        f.save(f.binding,{'schema':'OT-VISIBLE-SESSION-BINDING-2'})
        f.plan['binding']=flow.reference(f.binding)
        f.save(f.output/'owner-approval-intent.json',{'synthetic':True})
        with patch.object(resume,'manifest',return_value=({},{})),patch.object(resume,'predecessor_idle'):
            workflow.execute()

    def test_typed_child_failure_keeps_bounded_stage_category_without_private_output(self):
        f=self.fixture();workflow=f.workflow()
        def run(args,**kwargs):
            f.actions.append('revalidate')
            raw=flow.canonical(dict(schema='OT-VISIBLE-SESSION-RESULT-1',action='revalidate',
                outcome='failed',category='desktop_changed'))+b'\n'
            return SimpleNamespace(returncode=1,stdout=raw,stderr=b'private stderr must not persist')
        workflow.run=run
        with self.assertRaisesRegex(flow.Refused,'^desktop_changed$'):self.resumed_execution(f,workflow)
        path=f.output/'owner-workflow-predevice-result.json';raw=path.read_bytes();receipt=flow.decode(raw)
        self.assertEqual(receipt['stage'],'revalidate');self.assertEqual(receipt['category'],'desktop_changed')
        self.assertEqual(receipt['schema'],'OT-OWNER-PREDEVICE-RESULT-2')
        self.assertFalse(receipt['device_attempt_started']);self.assertNotIn(b'private stderr',raw)
        self.assertEqual(f.actions,['revalidate']);self.assertEqual(workflow.predevice_receipt,'saved')
        before=raw
        with self.assertRaises(FileExistsError):workflow.execute()
        self.assertEqual(path.read_bytes(),before);self.assertEqual(f.actions,['revalidate'])

    def test_predevice_timeout_malformed_duplicate_and_unknown_category_are_sanitized(self):
        for kind,expected in (('timeout','action_timeout'),('malformed','output_invalid'),
            ('duplicate','output_invalid'),('private_category','action_refused'),('wrong_action','output_invalid')):
            f=self.fixture();workflow=f.workflow()
            def run(args,**kwargs):
                f.actions.append('revalidate')
                if kind=='timeout':raise subprocess.TimeoutExpired(args,330,output=b'private stdout',stderr=b'private stderr')
                row=dict(schema='OT-VISIBLE-SESSION-RESULT-1',action='revalidate',outcome='failed',category='desktop_changed')
                if kind=='private_category':row['category']='private identification data'
                if kind=='wrong_action':row['action']='probe'
                raw=flow.canonical(row)+b'\n'
                if kind=='duplicate':raw+=raw
                if kind=='malformed':raw=b'private stdout\n'
                return SimpleNamespace(returncode=1,stdout=raw,stderr=b'private stderr')
            workflow.run=run
            with self.subTest(kind=kind),self.assertRaisesRegex(flow.Refused,'^'+expected+'$'):
                self.resumed_execution(f,workflow)
            raw=(f.output/'owner-workflow-predevice-result.json').read_bytes()
            self.assertEqual(flow.decode(raw)['category'],expected)
            for sentinel in (b'private stdout',b'private stderr',b'private identification data'):
                self.assertNotIn(sentinel,raw)
            self.assertEqual(f.actions,['revalidate']);self.assertFalse((f.output/'capture-grant.json').exists())

    def test_terminal_receipt_write_failure_preserves_original_refusal_and_never_retries(self):
        f=self.fixture();workflow=f.workflow();opened=[];original=Path.open
        def run(args,**kwargs):
            return SimpleNamespace(returncode=1,stderr=b'',stdout=flow.canonical(dict(
                schema='OT-VISIBLE-SESSION-RESULT-1',action='revalidate',outcome='failed',category='readiness_expired'))+b'\n')
        def open_path(path,*args,**kwargs):
            if path.name=='owner-workflow-predevice-result.json':
                opened.append(path);raise PermissionError('private write error')
            return original(path,*args,**kwargs)
        workflow.run=run
        with patch.object(Path,'open',open_path),self.assertRaisesRegex(flow.Refused,'^readiness_expired$'):
            self.resumed_execution(f,workflow)
        self.assertEqual(workflow.predevice_receipt,'write_refused');self.assertEqual(len(opened),1)
        self.assertFalse((f.output/'owner-workflow-predevice-result.json').exists())

    def test_entrypoint_unknown_typed_exception_does_not_echo_arbitrary_message(self):
        import io
        stream=io.StringIO()
        args=['--binding','synthetic','--binding-sha256','a'*64,'--action','revalidate']
        with (patch.object(visible_session,'dispatch',side_effect=visible_session.visible.AdmissionError('private details')),
              patch.object(sys,'stdout',stream)):
            result=visible_session.main(args)
        self.assertEqual(result,1);row=flow.decode(stream.getvalue().encode())
        self.assertEqual(row['action'],'revalidate');self.assertEqual(row['category'],'visible_session_refused')
        self.assertNotIn('private',stream.getvalue())

    def test_parent_unknown_typed_message_is_sanitized_and_write_status_is_visible(self):
        import io
        stream=io.StringIO()
        fake=SimpleNamespace(closure=None,predevice_receipt='write_refused',
            execute=lambda:(_ for _ in ()).throw(flow.Refused('private details')))
        with (patch.object(flow,'admit_plan',return_value={}),patch.object(flow,'Workflow',return_value=fake),
              patch.object(sys,'stdout',stream)):
            result=flow.main(['--plan','synthetic','--plan-sha256','a'*64])
        self.assertEqual(result,1);row=flow.decode(stream.getvalue().encode())
        self.assertEqual(row['category'],'workflow_refused')
        self.assertEqual(row['predevice_receipt'],'write_refused');self.assertNotIn('private',stream.getvalue())

    def test_native_typed_predecessor_refusal_has_exact_stage_and_bounded_category(self):
        import enrollment_candidate_predevice_resume as resume
        f=self.fixture();workflow=f.workflow()
        f.save(f.binding,{'schema':'OT-VISIBLE-SESSION-BINDING-2'});f.plan['binding']=flow.reference(f.binding)
        with (patch.object(resume,'manifest',return_value=({},{})),
              patch.object(resume,'predecessor_idle',side_effect=visible_session.visible.AdmissionError('predecessor_process_unresolved')),
              self.assertRaisesRegex(Exception,'^predecessor_process_unresolved$')):
            workflow.execute()
        row=flow.decode((f.output/'owner-workflow-predevice-result.json').read_bytes())
        self.assertEqual(row['stage'],'predecessor_idle');self.assertEqual(row['category'],'predecessor_process_unresolved')
        self.assertEqual(row['observed_utc'],f.now);self.assertEqual(f.actions,[])

    def test_local_child_vocabulary_matches_entrypoint_without_project_import_at_bootstrap(self):
        self.assertEqual(flow.REFUSAL_CATEGORIES,visible_session.REFUSAL_CATEGORIES)
        tree=ast.parse((ROOT/'tools/enrollment_candidate_owner_workflow.py').read_text())
        imports=[node for node in tree.body if isinstance(node,(ast.Import,ast.ImportFrom))]
        self.assertFalse(any(isinstance(node,ast.ImportFrom) and (node.module or '').startswith('enrollment_candidate_')
                             for node in imports))

    def test_actual_action_selects_probe720_practice600_and_other330_watchdogs(self):
        f = self.fixture(); observed = []
        def run(args, **kwargs):
            action = args[args.index('--action') + 1]
            observed.append((action, kwargs['timeout']))
            return SimpleNamespace(returncode=0, stdout=f.result(action), stderr=b'')
        workflow = f.workflow(); workflow.run = run
        for action in flow.ACTIONS:
            workflow.action(action, handoff_review={'path': 'synthetic', 'sha256': 'a' * 64}
                            if action == 'publish_handoff' else None)
        self.assertEqual([(action, 720 if action == 'probe' else 600 if action == 'practice' else 330)
                          for action in flow.ACTIONS], observed)
        self.assertFalse(list(f.ready.iterdir()))
        self.assertFalse(list(f.output.iterdir()))

    def test_probe_action_watchdog_covers_actual550_second_word_and_comparison(self):
        from enrollment_candidate_interactive_launch_tests import Fixture as ProbeFixture
        import enrollment_candidate_runtime as runtime
        f = self.fixture(); probe_f = ProbeFixture(); self.addCleanup(probe_f.cleanup)
        delays = iter((275, 275)); cleared = []
        class Backend:
            def open(self, text): self.delay = next(delays); return True
            def poll(self): probe_f.clock.sleep(self.delay); return 'confirm'
            def clear(self): cleared.append(True); return True
        def run(args, **kwargs):
            self.assertEqual('probe', args[args.index('--action') + 1])
            proof = runtime.decode(runtime.regular(probe_f.probe(Backend)['path']).read_bytes())
            elapsed = proof['finished_monotonic'] - proof['started_monotonic']
            self.assertEqual(610, elapsed)
            self.assertGreater(kwargs['timeout'], elapsed)
            self.assertEqual(720, kwargs['timeout'])
            return SimpleNamespace(returncode=0, stdout=f.result('probe'), stderr=b'')
        workflow = f.workflow(); workflow.run = run; workflow.action('probe')
        self.assertEqual([True, True], cleared)
        self.assertFalse(list(probe_f.output.iterdir()))
        self.assertFalse(list(f.output.iterdir()))

    def test_dispatch_completed_exit_zero_with_trial_failure_is_not_success(self):
        f=self.fixture();f.trial_outcome='failed';workflow=f.workflow()
        with self.assertRaisesRegex(flow.Refused,'trial_failed_or_unknown'):workflow.execute()
        self.assertEqual(workflow.closure['trial_outcome'],'failed')
        self.assertEqual(workflow.closure['trial_category'],'target_refused')
        self.assertEqual(workflow.closure['first_failure'],['candidate','target_refused'])
        self.assertTrue(workflow.closure['custody_release_verified'])
        self.assertEqual(workflow.closure['restoration_acceptance'],'requires_independent_review')

    def test_review_written_after_admission_before_prepare_exit_is_valid(self):
        f=self.fixture(); f.prepare_exit_delay=5
        f.workflow().execute()
        self.assertIn('publish_handoff',f.actions)

    def test_publish_dispatch_receives_exact_review_reference(self):
        f=self.fixture(); original=f.run; received=[]
        def run(args,**kwargs):
            if args[args.index('--action')+1]=='publish_handoff':
                received.append((args[args.index('--handoff-review')+1],args[args.index('--handoff-review-sha256')+1]))
            return original(args,**kwargs)
        workflow=f.workflow(); workflow.run=run; workflow.execute()
        expected=flow.reference(f.output/'handoff-review-release.json')
        self.assertEqual(received,[(expected['path'],expected['sha256'])])

    def test_visible_entrypoint_review_ref_only_allowed_for_publish(self):
        for action in ('probe','prepare_handoff','status'):
            with self.assertRaises(Exception):
                visible_session.dispatch('unused','a'*64,action,handoff_review_ref={'path':'unused'})
        with self.assertRaises(Exception): visible_session.dispatch('unused','a'*64,'publish_handoff')

    def test_prepare_failure_never_publishes_but_waits_existing_cleanup(self):
        f = self.fixture(); f.fail = 'prepare_handoff'
        with self.assertRaises(flow.Refused): f.workflow().execute()
        self.assertNotIn('publish_handoff', f.actions)
        self.assertTrue(f.cleanup_waited)

    def test_wrong_nonce_binding_source_timestamp_or_package_review_refuses(self):
        changes = [dict(session_nonce='c'*32), dict(binding={'wrong':True}),
                   dict(source='preclock_review'), dict(received_utc=9999),
                   dict(candidate_package={'wrong':True}), dict(findings=['unresolved'])]
        for change in changes:
            with self.subTest(change=change):
                f = self.fixture(); f.release_change = change
                with self.assertRaises(flow.Refused): f.workflow().execute()
                self.assertNotIn('publish_handoff', f.actions)
                self.assertTrue(f.cleanup_waited)

    def test_missing_review_consumes_no_new_authority_and_restoration_still_waited(self):
        f = self.fixture(); f.no_review = True
        with self.assertRaises(flow.Refused): f.workflow().execute()
        self.assertNotIn('publish_handoff', f.actions)
        self.assertEqual(f.actions.count('issue_capture'),1)
        self.assertEqual(f.actions.count('launch'),1)
        self.assertTrue(f.cleanup_waited)

    def test_premature_worker_return_never_prepares(self):
        f = self.fixture(); f.premature = True
        with self.assertRaises(flow.Refused): f.workflow().execute()
        self.assertNotIn('prepare_handoff',f.actions)
        self.assertTrue(f.cleanup_waited)

    def test_replay_and_preexisting_human_files_refuse_before_probe(self):
        for filename in ('owner-workflow-start.json','owner-authorization.json','capture-grant.json'):
            f = self.fixture(); (f.output/filename).write_bytes(b'{}')
            with self.assertRaises(Exception): f.workflow().execute()
            self.assertEqual(f.actions,[])

    def test_changed_source_or_binding_refuses_before_dispatch(self):
        for key in ('host','entrypoint','module','binding'):
            f = self.fixture(); Path(f.plan[key]['path']).write_bytes(b'changed')
            with self.assertRaises(flow.Refused): f.workflow().execute()
            self.assertEqual(f.actions,[])

    def test_phase_failure_before_launch_never_starts_worker(self):
        for action in ('probe','attest','practice','baseline','route','issue_capture'):
            f = self.fixture(); f.fail=action
            with self.assertRaises(flow.Refused): f.workflow().execute()
            self.assertNotIn('launch',f.actions)

    def test_owner_reply_missing_times_out_without_fabrication(self):
        f=self.fixture()
        def run(args,**kwargs): return SimpleNamespace(returncode=0,stdout=f.result('probe'),stderr=b'')
        workflow=f.workflow(); workflow.run=run
        with self.assertRaises(flow.Refused): workflow.execute()
        self.assertFalse((f.ready/'owner-reply.json').exists())
        self.assertEqual(f.now,10300)

    def test_unresponsive_worker_reported_at_original_cleanup_ceiling_without_kill(self):
        f=self.fixture(); f.fail='prepare_handoff'; f.worker_hang=True
        with self.assertRaisesRegex(flow.Refused,'worker_cleanup_unresolved'): f.workflow().execute()
        self.assertEqual(f.now,f.grant['cleanup_expires_utc'])
        self.assertNotIn('publish_handoff',f.actions)

    def test_output_malformed_duplicate_terminal_wrong_action_large_or_partial_refuses(self):
        f=self.fixture()
        cases=[b'garbage\n',f.result('launch'),f.result('probe')+f.result('probe'),
               f.result('probe').rstrip(b'\n'),b' '*32769+b'\n',b'[]\n'+f.result('probe')]
        for raw in cases:
            with self.subTest(raw=raw[:30]):
                with self.assertRaises(Exception): flow.action_result(raw,'probe')
        self.assertEqual(flow.action_result(b'{"phase":"fine"}\n'+f.result('probe'),'probe')['action'],'probe')

    def test_changed_candidate_bytes_after_review_refuses(self):
        f=self.fixture(); workflow=f.workflow(); original=workflow.wait_file
        def wait(path,ceiling):
            result=original(path,ceiling)
            if path.name=='handoff-review-release.json':
                (f.output/'candidate-package.json').write_bytes(b'{"swapped":true}\n')
            return result
        workflow.wait_file=wait
        with self.assertRaises(flow.Refused): workflow.execute()
        self.assertNotIn('publish_handoff',f.actions)
        self.assertTrue(f.cleanup_waited)

    def publisher(self,f,mutation=None):
        """Execute only actual new-template publication functions with host stubs."""
        f.run(['--action','issue_capture'])
        f.run(['--action','prepare_handoff'])
        f.save(f.output/'capture-journal.json',{'fixture':'journal'})
        f.save(f.output/'capture-pins.json',{'fixture':'pins'})
        admission=flow.decode((f.output/'handoff-admission.json').read_bytes())
        admission.update(capture_journal=flow.reference(f.output/'capture-journal.json'),
                         capture_pins=flow.reference(f.output/'capture-pins.json'))
        f.save(f.output/'handoff-admission.json',admission)
        f.grant['request_sha256']='e'*64;f.save(f.output/'capture-grant.json',f.grant)
        f.save(f.output/'active.json',{'attempt':f.grant['attempt'],'request_sha256':f.grant['request_sha256']})
        release_path=f.output/'handoff-review-release.json';release=flow.decode(release_path.read_bytes())
        release['handoff_admission']=flow.reference(f.output/'handoff-admission.json')
        release['capture_grant']=flow.reference(f.output/'capture-grant.json');f.save(release_path,release)
        pinned=flow.reference(release_path)
        template=ROOT/'.private/ot0238c-button-confirmation-20261005/prestage/session.template.py'
        tree=ast.parse(template.read_text())
        funcs=[node for node in tree.body if isinstance(node,ast.FunctionDef)
               and node.name in ('checked_handoff_review','publish_handoff')]
        def preflight(*args):
            if mutation: mutation()
            return SimpleNamespace()
        ns=dict(OUT=f.output,VISIBLE_HANDOFF_REVIEW=pinned,VISIBLE_BINDING=str(f.binding),
                VISIBLE_BINDING_SHA256=f.plan['binding']['sha256'],ref=flow.reference,read_ref=flow.read_pin,
                require_visible_readiness=lambda:{'session_nonce':f.plan['session_nonce']},
                require_owner_authorization=lambda:None,inputs=lambda:None,handoff_reserve=lambda *args:None,
                candidate_runner=SimpleNamespace(preflight=preflight),capture=SimpleNamespace(ACTIVE='active.json'),
                custody=SimpleNamespace(canonical=flow.canonical),json=json,datetime=datetime,
                time=SimpleNamespace(time=lambda:f.now),os=__import__('os'))
        exec(compile(ast.Module(body=funcs,type_ignores=[]),str(template),'exec'),ns)
        return ns,release

    def test_publication_writes_only_reviewed_package_reference(self):
        f=self.fixture();ns,release=self.publisher(f)
        ns['publish_handoff']()
        self.assertEqual(flow.decode((f.output/'handoff.json').read_bytes()),release['candidate_package'])

    def test_swap_after_parent_acceptance_before_publication_is_refused(self):
        f=self.fixture();ns,_=self.publisher(f)
        ns['checked_handoff_review']()  # equivalent accepted parent snapshot
        f.save(f.output/'candidate-package.json',{'otherwise_valid':'changed'})
        admission=flow.decode((f.output/'handoff-admission.json').read_bytes())
        admission['candidate_package']=flow.reference(f.output/'candidate-package.json')
        f.save(f.output/'handoff-admission.json',admission)
        with self.assertRaises(Exception):ns['publish_handoff']()
        self.assertFalse((f.output/'handoff.json').exists())

    def test_swap_inside_preflight_before_atomic_publication_is_refused(self):
        f=self.fixture()
        def swap():f.save(f.output/'candidate-package.json',{'otherwise_valid':'changed'})
        ns,_=self.publisher(f,mutation=swap)
        with self.assertRaises(Exception):ns['publish_handoff']()
        self.assertFalse((f.output/'handoff.json').exists())


if __name__ == '__main__': unittest.main()
