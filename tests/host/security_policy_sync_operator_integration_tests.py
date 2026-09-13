"""Actual OT-201 custody/worker/ROM child/Endpoint; synthetic physical boundary.

Optional --candidate reads the exact admitted BIN for synthetic-device tests.
No real device, serial module, process launcher or network is invoked.
"""
from pathlib import Path
from types import SimpleNamespace
import contextlib,copy,hashlib,importlib,io,json,sys,tempfile,time,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import security_policy_sync_operator as op
import security_policy_sync_backup_operator as backupop
import security_policy_sync_bundle as bundle
import security_policy_sync_execution as execution
import security_policy_backup as backup
import security_policy_hardware as hardware
import security_policy_diagnostics as diagnostics
import security_policy_input_timing as timing
import security_policy_sync_readback_tests as nvs
import security_policy_sync_readback as readback

CANDIDATE = None
OUTPUT = ROOT/'.private/ot201-operator'

def raw(obj):return json.dumps(obj,sort_keys=True,separators=(',',':')).encode()
def sha(value):return hashlib.sha256(value).hexdigest()
class Tests(unittest.TestCase):
 def setUp(self):
  OUTPUT.mkdir(parents=True,exist_ok=True)
  temp=tempfile.TemporaryDirectory(prefix='composed-',dir=OUTPUT);self.addCleanup(temp.cleanup);self.root=Path(temp.name).resolve();self.private=self.root/'.private';self.private.mkdir()
  self.ctx={'manifest':{'worktree':str(self.root),'root':str(self.private/'capsule'),'files':{},'powershell':{'path':'synthetic-pwsh'}},'path':self.private/'manifest.json','sha':'a'*64}
  for name in bundle.SOURCE_PATHS:
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/name).read_bytes());self.ctx['manifest']['files']['policy/'+p.name]={'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())}
  report=json.loads((ROOT/bundle.REPORT).read_bytes())
  for name in report['source_pins']:
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/name).read_bytes())
  p=self.root/bundle.REPORT;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/bundle.REPORT).read_bytes())
  self.candidate=self.root/'candidate.bin'
  if CANDIDATE is not None:
   candidate=CANDIDATE.read_bytes()
   self.assertEqual((len(candidate),sha(candidate)),(bundle.CANDIDATE_SIZE,bundle.CANDIDATE_SHA))
  else:candidate=b'C'*bundle.CANDIDATE_SIZE
  self.candidate.write_bytes(candidate)
  self.flash={};self.trace=[];self.child_errors=[];self.now=time.monotonic();self.empty=False;self.failed_observation=False;self.replay=False;self.fail_preflight=False
  self.close_ambiguous=False;self.fail_restore=False;self.current_child=None;self.bad_observation_image=False
  self.observation_reads=[];self.observation_requests=[];self.failed_read_replays=0
  self.diagnostic=nvs.diagnostic(8,0)
  self.data={'schema':'OT190-BACKUP-REQUEST-1','runtime_sha256':self.ctx['sha'],'finish':'hold','roles':[]}
  for i,role in enumerate('AB'):
   row={'role':role,'private_route':'COM'+str(i+1),'private_identity':str(i+1)*12,'expected':{}}
   for name,(offset,size) in backup.SPANS.items():
    value=b'\xff'*size if name=='nvs' else bytes([65+i if name=='application' else len(name)])*size
    self.flash[role,offset]=value
    if name!='nvs':row['expected'][name]={'bytes':size,'sha256':sha(value)}
   self.data['roles'].append(row)
  self.original=copy.deepcopy(self.flash)
  self.records=[SimpleNamespace(device=r['private_route'],vid=0x303a,pid=0x1001,serial_number=r['private_identity']) for r in self.data['roles']]
  for module in (bundle,backup.execution.bundle):
   for name,key in [('PARTITION_SHA','partition'),('OTA_SHA','ota')]:self.install(patch.object(module,name,self.data['roles'][0]['expected'][key]['sha256']))
  if CANDIDATE is None:self.install(patch.object(bundle,'CANDIDATE_SHA',sha(self.candidate.read_bytes())))
  self.install(patch.object(hardware.RomTransport,'__init__',lambda transport,private_root:setattr(transport,'private_root',private_root)))
  self.timing_marks=[];self.timing_fault=False
  actual_backend=op.make_backend
  def create(ctx,bindings,transport):
   b=actual_backend(ctx,bindings,transport);b.inventory=lambda:self.records;b.monotonic=lambda:max(self.now,time.monotonic())
   mark=b.input_timing.mark
   def observed(role,event):
    self.timing_marks.append((role,event));return mark(role,event)
   b.input_timing.mark=observed
   if self.timing_fault:
    def failed_clock():raise RuntimeError('private timing failure')
    b.input_timing._clock=failed_clock
   transport.open=lambda route:self.handle(route)
   return b
  self.install(patch.object(op,'make_backend',side_effect=create))
  actual_hw=hardware.Backend
  def hw(*args,**kwargs):
   b=actual_hw(*args,**kwargs);b.inventory=lambda:self.records;b.monotonic=lambda:max(self.now,time.monotonic());return b
  self.install(patch.object(hardware,'Backend',side_effect=hw))
  original_import=importlib.import_module
  self.install(patch.object(op.importlib,'import_module',side_effect=lambda n:SimpleNamespace(main=self.device) if n=='esptool' else original_import(n)))
  self.install(patch.object(op,'audit_loaded_modules',return_value=True))
  self.install(patch.object(op.subprocess,'run',side_effect=self.dispatch))
 def install(self,p):p.start();self.addCleanup(p.stop)
 def store(self,name,value):
  p=self.private/name;p.write_bytes(raw(value));return p,sha(p.read_bytes())
 def handle(self,route):
  t=self;role=next(r['role'] for r in self.data['roles'] if r['private_route']==route);self.now=time.monotonic();self.trace.append(('open',role))
  class Handle:
   is_open=True;timeout=.25;write_timeout=.5
   def write(h,value):
    t.trace.append(('RUN',role));h.reply=b'' if t.empty else b'SEC_EVAL1 ot187-policy-v0 '+value.strip().split()[-1]+b' pass\n';return len(value)
   def read(h,size):
    t.now+=max(.001,min(h.timeout,.25));result=h.reply;h.reply=b'';return result
   def close(h):
    t.trace.append(('close',role))
    if t.close_ambiguous:raise RuntimeError('synthetic close ambiguity')
    h.is_open=False
  return Handle()
 def device(self,argv):
  role=next(r['role'] for r in self.data['roles'] if r['private_route']==argv[3]);cmd=argv[11:];self.trace.append((cmd[0],role,*cmd[1:3]))
  if cmd[0]=='read-mac':print('MAC: '+next(r['private_identity'] for r in self.data['roles'] if r['role']==role))
  elif cmd[0]=='flash-id':print('ESP32-S3\nDetected flash size: 16MB')
  elif cmd[0]=='read-flash':
   if self.current_child.get('purpose') is not None:
    self.observation_reads.append(role)
    if self.failed_observation:raise RuntimeError('synthetic observation read failure after claim')
   Path(cmd[3]).write_bytes(self.flash[role,int(cmd[1],0)])
  elif cmd[0]=='write-flash':
   offset=int(cmd[3],0);value=Path(cmd[4]).read_bytes()
   if self.fail_restore and value==self.original[role,offset]:
    self.fail_restore=False;raise RuntimeError('synthetic restoration write failure')
   self.flash[role,offset]=value
  elif cmd[0]=='run':
   if self.flash[role,65536]!=self.original[role,65536]:self.flash[role,53248]=self.diagnostic
   else:self.trace.append(('original_reset',role))
  else:raise AssertionError('unapproved device command')
 def dispatch(self,args,**kwargs):
  mode=args[args.index('-Mode')+1];p=Path(args[args.index('-Request')+1]);h=args[args.index('-RequestSha256')+1];child=json.loads(p.read_bytes())
  if self.bad_observation_image and child.get('purpose') is not None:
   intent_path=Path(child['purpose']['path']);intent=json.loads(intent_path.read_bytes())
   intent['image_binding']['sha256']='0'*64;intent_path.write_bytes(raw(intent))
   child['purpose']['sha256']=sha(intent_path.read_bytes());p.write_bytes(raw(child));h=sha(p.read_bytes())
  if self.fail_preflight and mode=='SyncRom' and child['argv'][11]=='read-flash':
   self.fail_preflight=False;return SimpleNamespace(returncode=1,stdout=b'',stderr=b'')
  if child.get('purpose') is not None and child['argv'][11]=='read-flash':
   self.observation_requests.append(copy.deepcopy(child))
  output=io.StringIO()
  self.current_child=child
  try:
   with contextlib.redirect_stdout(output):
    (op.run_rom if mode=='SyncRom' else backupop.run_rom)(self.ctx,p,h)
   if self.replay and child['argv'][11]=='run':
    copied,hh=self.store('copied-reset.json',child)
    with self.assertRaises(Exception):(op.run_rom if mode=='SyncRom' else backupop.run_rom)(self.ctx,copied,hh)
   return SimpleNamespace(returncode=0,stdout=output.getvalue().encode(),stderr=b'')
  except Exception as error:
   self.child_errors.append(type(error).__name__+':'+str(error))
   if self.failed_observation and child.get('purpose') is not None and child['argv'][11]=='read-flash':
    # Replay through the actual child admission while the restore-intent phase
    # is still current. A copied request cannot obtain a second physical read.
    before=list(self.trace);replay,hh=self.store(f'failed-read-copy-{len(self.observation_reads)}.json',child)
    with self.assertRaises(Exception):op.run_rom(self.ctx,replay,hh)
    self.assertEqual(before,self.trace);self.failed_read_replays+=1
   return SimpleNamespace(returncode=1,stdout=b'',stderr=b'')
  finally:self.current_child=None
 def backup_request(self,operation='capture',origin=None):
  rp,rh=self.store('backup-request.json',self.data);now=int(time.time());attempt=('1' if operation=='capture' else '3')*32
  g={'schema':'OT190-BACKUP-GRANT-1','attempt':attempt,'request_sha256':backup.digest(self.data),'runtime_sha256':self.ctx['sha'],'operation':operation,'origin_attempt':origin,'attempt_count':1,'radio_allowed':False,'actions':['rom_read','rom_hold'] if operation=='capture' else ['rom_read','original_reset'],'issued_utc':now-1,'expires_utc':now+600}
  gp,gh=self.store(operation+'-grant.json',g)
  return self.store(operation+'-request.json',{'schema':'OT201-SYNC-BACKUP-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':operation,'request_path':str(rp),'request_sha256':rh,'grant_path':str(gp),'grant_sha256':gh,'origin_attempt':origin,'execution_request':None})
 def prepare(self):
  p,h=self.backup_request();self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'held')
  roles=backup.inspect_handoff(self.root,self.data,'1'*32)
  self.package=bundle.freeze(self.root,self.candidate,roles,runtime_sha256=self.ctx['sha']);pp,ph=self.store('package.json',self.package);now=int(time.time())
  grant={'schema':execution.SCHEMAS['execute'],'attempt':'2'*32,'package_sha256':bundle.digest(self.package),'runtime_sha256':self.ctx['sha'],'operation':'execute','origin_attempt':None,'attempt_count':1,'radio_allowed':False,'actions':execution.ACTIONS,'issued_utc':now-1,'expires_utc':now+600}
  gp,gh=self.store('stage-grant.json',grant)
  ep,eh=self.store('stage-request.json',{'schema':'OT201-SYNC-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':'execute','package_path':str(pp),'package_sha256':ph,'grant_path':str(gp),'grant_sha256':gh,'origin_attempt':None})
  rp,rh=self.store('backup-request.json',self.data)
  return self.store('handoff.json',{'schema':'OT201-SYNC-BACKUP-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':'handoff','request_path':str(rp),'request_sha256':rh,'grant_path':None,'grant_sha256':None,'origin_attempt':'1'*32,'execution_request':{'path':str(ep),'sha256':eh}})
 def test_actual_handoff_child_endpoint_restore_both(self):
  p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'pass',self.child_errors)
  self.assertEqual(self.flash,self.original);self.assertTrue(all(r['stage_observation']['available'] for r in result['roles']))
  self.assertEqual(self.observation_reads,['A','B'])
  for row in result['roles']:
   observed=row['stage_observation']
   self.assertEqual((observed['stage'],observed['error'],observed['input_status']),('send_return','none','stage_result_recorded'))
   self.assertEqual(observed['input']['terminal_reason'],'none')
   capture=self.private/f"ot201-sync-{row['role']}-{'2'*32}.bin"
   self.assertEqual(capture.read_bytes(),self.diagnostic)
   custody=json.loads(execution.observation_path(self.root,row['role'],'2'*32,'receipt.json').read_bytes())
   self.assertEqual(custody['projection'],observed)
   self.assertEqual(custody['capture']['sha256'],sha(self.diagnostic))
   self.assertEqual(custody['package_sha256'],bundle.digest(self.package))
   self.assertEqual(custody['runtime_sha256'],self.ctx['sha'])
   image=bundle.image_binding(self.package)
   self.assertEqual(custody['image_binding'],{'contract':image.contract,'target':image.target,'sha256':image.sha256})
  self.assertLess(self.trace.index(('original_reset','A')),self.trace.index(('open','B')))
  self.assertEqual(self.timing_marks,[(role,event) for role in 'AB' for event in timing.EVENTS])
  self.assertEqual([r['status'] for r in result['input_timing']],['available','available'])
 def test_real_backend_timing_fault_preserves_physical_sequence(self):
  self.doCleanups()  # Each composed fixture must own unstacked physical seams.
  outcomes=[]
  for failed in (False,True):
   fixture=Tests('test_actual_handoff_child_endpoint_restore_both');fixture.setUp()
   try:
    fixture.timing_fault=failed;p,h=fixture.prepare();result=backupop.run_operator(fixture.ctx,p,h)
    self.assertEqual(result['status'],'pass',fixture.child_errors);self.assertEqual(fixture.flash,fixture.original)
    self.assertEqual(fixture.timing_marks,[(role,event) for role in 'AB' for event in timing.EVENTS])
    self.assertEqual([r['status'] for r in result['input_timing']],['unavailable' if failed else 'available']*2)
    outcomes.append(fixture.trace)
   finally:fixture.doCleanups()
  self.assertEqual(outcomes[0],outcomes[1])
 def test_failed_a_receipt_restores_then_untouched_b_release_and_reset_replay(self):
  self.empty=True;self.replay=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'evaluation_failed',self.child_errors)
  self.assertNotIn(('open','B'),self.trace);p,h=self.backup_request('reset','1'*32)
  self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'reset_nvs_stale',self.child_errors);self.assertEqual(self.flash,self.original)
 def test_observation_failure_preserves_restore(self):
  self.failed_observation=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'pass',self.child_errors);self.assertEqual(self.flash,self.original)
  self.assertTrue(all(not r['stage_observation']['available'] for r in result['roles']))
  self.assertEqual(self.observation_reads,['A','B'])
  self.assertEqual(self.failed_read_replays,2)
  for role in 'AB':
   self.assertTrue(execution.observation_path(self.root,role,'2'*32,'claimed.json').is_file())
   self.assertFalse((self.private/f"ot201-sync-{role}-{'2'*32}.bin").exists())
  before=list(self.trace)
  for i,child in enumerate(self.observation_requests):
   p,h=self.store(f'failed-observation-replay-{i}.json',child)
   with self.assertRaises(Exception):op.run_rom(self.ctx,p,h)
  self.assertEqual(before,self.trace)
 def test_direct_execute_and_bad_runtime_never_mutate(self):
  self.prepare();p=self.private/'stage-request.json';before=list(self.trace)
  with self.assertRaises(Exception):op.run_operator(self.ctx,p,sha(p.read_bytes()))
  self.assertEqual(before,self.trace)
 def test_first_restore_nvs_requires_purpose_and_latest_grant(self):
  events=['candidate_boot_intent','restore_intent'];argv=['']*11+['read-flash','0xd000','12288','unused']
  with self.assertRaises(Exception):op.admit_observation_purpose({'operation':'execute','_phase':'restore_intent'},events,argv,None)
  op.admit_observation_purpose({'operation':'execute','_phase':'restore_intent'},events+['restore_intent'],argv,None)
  g=SimpleNamespace(attempt='2'*32,raw_sha256='a'*64);j=SimpleNamespace(events=[{'event':'created','attempt':'1'*32,'grant':'b'*64},{'event':'recovery_authorized','attempt':'3'*32,'grant':'c'*64}])
  with self.assertRaises(Exception):op.admit_journal_authority({'operation':'recover'},g,j)
 def rewrite_stage(self,handoff,change_grant=None,change_package=None):
  outer=json.loads(handoff.read_bytes());ep=Path(outer['execution_request']['path']);request=json.loads(ep.read_bytes())
  if change_grant:
   gp=Path(request['grant_path']);grant=json.loads(gp.read_bytes());change_grant(grant);gp.write_bytes(raw(grant));request['grant_sha256']=sha(gp.read_bytes())
  if change_package:
   pp=Path(request['package_path']);package=json.loads(pp.read_bytes());change_package(package);pp.write_bytes(raw(package));request['package_sha256']=sha(pp.read_bytes())
  ep.write_bytes(raw(request));outer['execution_request']['sha256']=sha(ep.read_bytes());handoff.write_bytes(raw(outer));return sha(handoff.read_bytes())
 def test_legacy_grant_refuses_before_custody_consumption(self):
  p,h=self.prepare();h=self.rewrite_stage(p,change_grant=lambda g:g.update(schema='OT188-EXECUTE-GRANT-1'));before=list(self.trace)
  with self.assertRaises(Exception):backupop.run_operator(self.ctx,p,h)
  self.assertEqual(before,self.trace);self.assertEqual(len(backup.inspect_handoff(self.root,self.data,'1'*32)),2)
  self.assertFalse(execution.used_path(self.root,'2'*32).exists())
 def test_full_role_mismatch_refuses_before_custody_consumption(self):
  p,h=self.prepare();h=self.rewrite_stage(p,change_package=lambda q:q['roles'][0].update(private_route='COM99'));before=list(self.trace)
  with self.assertRaises(Exception):backupop.run_operator(self.ctx,p,h)
  self.assertEqual(before,self.trace);self.assertEqual(len(backup.inspect_handoff(self.root,self.data,'1'*32)),2)
 def test_expired_hold_refuses_before_custody_consumption(self):
  p,h=self.prepare();before=list(self.trace);inspect=backup.inspect_handoff
  with patch.object(backup,'inspect_handoff',side_effect=lambda root,data,origin:inspect(root,data,origin,utc=lambda:time.time()+7200)):
   with self.assertRaises(Exception):backupop.run_operator(self.ctx,p,h)
  self.assertEqual(before,self.trace);self.assertEqual(len(inspect(self.root,self.data,'1'*32)),2)
 def test_changed_snapshot_refuses_before_custody_consumption(self):
  p,h=self.prepare();Path(self.package['roles'][1]['nvs']['path']).write_bytes(b'X'*12288);before=list(self.trace)
  with self.assertRaises(Exception):backupop.run_operator(self.ctx,p,h)
  self.assertEqual(before,self.trace);self.assertTrue((self.private/backup.ACTIVE).exists());self.assertFalse(execution.used_path(self.root,'2'*32).exists())
 def test_no_write_interruption_guarded_reconciliation(self):
  p,h=self.prepare();self.fail_preflight=True;result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'reconciliation_required')
  p,h=self.backup_request('reset','1'*32);self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'reset_nvs_stale',self.child_errors)
  self.assertFalse(any(row[0]=='write-flash' for row in self.trace));self.assertEqual(self.flash,self.original);self.assertFalse((self.private/'security-policy-active.lock').exists())
 def test_two_key_waiting_prefix_never_becomes_evaluation_success(self):
  self.empty=True
  self.diagnostic=nvs.diagnostic(3,0,nvs.input_record(discarded=193,first_bytes=32,flags=19,terminal=19,first_reason=13,delay=2))
  p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h)
  self.assertEqual(result['status'],'evaluation_failed',self.child_errors)
  self.assertEqual(len(result['roles']),1);row=result['roles'][0]
  self.assertEqual(row['evaluation'],'capture_failed');self.assertTrue(row['restored'])
  observed=row['stage_observation'];self.assertTrue(observed['available'])
  self.assertEqual((observed['stage'],observed['error'],observed['input_status']),('waiting','none','committed_before_stage_result'))
  self.assertEqual(observed['input']['discarded_bytes'],193)
  self.assertEqual(observed['input']['first_rejected_frame_bytes'],32)
  self.assertEqual((observed['input']['terminal_reason'],observed['input']['first_frame_reason']),('sync_limit','invalid_length'))
  self.assertNotIn(('open','B'),self.trace);self.assertEqual(self.observation_reads,['A'])
  self.assertEqual(self.flash,self.original)
  p,h=self.backup_request('reset','1'*32)
  self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'reset_nvs_stale',self.child_errors)
  self.assertEqual(self.flash,self.original)
  self.assertFalse((self.private/'security-policy-active.lock').exists())
 def test_exact_terminal_and_first_reason_survive_stage4_alias(self):
  self.empty=True
  self.diagnostic=nvs.diagnostic(4,16,nvs.input_record(discarded=288,first_bytes=95,flags=17,terminal=19,first_reason=12,delay=0))
  p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h)
  self.assertEqual(result['status'],'evaluation_failed',self.child_errors)
  observed=result['roles'][0]['stage_observation'];self.assertTrue(observed['available'])
  self.assertEqual((observed['stage'],observed['error']),('input_result','loop_limit'))
  self.assertEqual((observed['input']['terminal_reason'],observed['input']['first_frame_reason']),('sync_limit','buffer_limit'))
  self.assertEqual((observed['input']['discarded_bytes'],observed['input']['first_rejected_frame_bytes']),(288,95))
  self.assertEqual(self.flash,self.original)
 def test_image_mismatch_refuses_before_decode_and_custody_consumption(self):
  p,h=self.prepare();h=self.rewrite_stage(p,change_package=lambda q:q['candidate'].update(sha256='0'*64))
  before=list(self.trace)
  with patch.object(readback,'decode',side_effect=AssertionError('decoder must not run')) as decode:
   with self.assertRaises(Exception):backupop.run_operator(self.ctx,p,h)
   decode.assert_not_called()
  self.assertEqual(before,self.trace)
  self.assertEqual(len(backup.inspect_handoff(self.root,self.data,'1'*32)),2)
  self.assertFalse(execution.used_path(self.root,'2'*32).exists())
 def test_ambiguous_close_blocks_all_later_rom_and_capture(self):
  self.close_ambiguous=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h)
  self.assertEqual(result['status'],'recovery_required',self.child_errors)
  first_close=self.trace.index(('close','A'))
  self.assertTrue(all(row==('close','A') for row in self.trace[first_close:]))
  self.assertEqual(self.observation_reads,[]);self.assertEqual(self.observation_requests,[])
  self.assertFalse(list(self.private.glob('ot201-sync-A-*.bin')))
  self.assertTrue((self.private/'security-policy-active.lock').exists())
  self.assertNotIn(('open','B'),self.trace)
 def test_observation_intent_image_mismatch_blocks_read_and_decode_but_restores(self):
  self.bad_observation_image=True;p,h=self.prepare()
  with patch.object(readback,'decode',side_effect=AssertionError('mismatched image must not decode')) as decode:
   result=backupop.run_operator(self.ctx,p,h)
   decode.assert_not_called()
  self.assertEqual(result['status'],'pass',self.child_errors)
  self.assertTrue(all(not r['stage_observation']['available'] for r in result['roles']))
  self.assertEqual(self.observation_reads,[]);self.assertEqual(self.flash,self.original)
 def recovery_request(self):
  request=json.loads((self.private/'stage-request.json').read_bytes());now=int(time.time())
  grant={'schema':execution.SCHEMAS['recover'],'attempt':'4'*32,'package_sha256':bundle.digest(self.package),
   'runtime_sha256':self.ctx['sha'],'operation':'recover','origin_attempt':'2'*32,'attempt_count':1,
   'radio_allowed':False,'actions':execution.RECOVERY_ACTIONS,'issued_utc':now-1,'expires_utc':now+600}
  gp,gh=self.store('restore-only-grant.json',grant)
  request.update(operation='recover',origin_attempt='2'*32,grant_path=str(gp),grant_sha256=gh)
  return self.store('restore-only-request.json',request)
 def test_restoration_only_recovery_needs_no_candidate_and_never_recaptures(self):
  self.fail_restore=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h)
  self.assertEqual(result['status'],'recovery_required',self.child_errors)
  self.assertEqual(self.observation_reads,['A']);before=len(self.trace)
  observed=list(self.observation_requests)
  self.candidate.unlink()  # Only this synthetic fixture's private candidate copy.
  p,h=self.recovery_request();result=op.run_operator(self.ctx,p,h)
  self.assertEqual(result['status'],'recovered',self.child_errors)
  self.assertEqual(self.flash,self.original)
  self.assertEqual(self.observation_requests,observed);self.assertEqual(self.observation_reads,['A'])
  self.assertFalse(any(row[0] in ('open','RUN') for row in self.trace[before:]))
  self.assertNotIn(('open','B'),self.trace)
  self.assertFalse((self.private/'security-policy-active.lock').exists())

if __name__=='__main__':
 if '--candidate' in sys.argv:
  at=sys.argv.index('--candidate');CANDIDATE=Path(sys.argv[at+1]).resolve();del sys.argv[at:at+2]
 unittest.main(verbosity=2)
