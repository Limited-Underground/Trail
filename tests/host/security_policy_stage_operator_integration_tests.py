"""Actual stage custody/worker/ROM child/backend/Endpoint; synthetic physical boundary."""
from pathlib import Path
from types import SimpleNamespace
import contextlib,copy,hashlib,importlib,io,json,sys,tempfile,time,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import security_policy_stage_operator as op
import security_policy_stage_backup_operator as backupop
import security_policy_stage_bundle as bundle
import security_policy_stage_execution as execution
import security_policy_backup as backup
import security_policy_hardware as hardware
import security_policy_diagnostics as diagnostics
import security_policy_stage_readback_tests as nvs

def raw(obj):return json.dumps(obj,sort_keys=True,separators=(',',':')).encode()
def sha(value):return hashlib.sha256(value).hexdigest()
class Tests(unittest.TestCase):
 def setUp(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name).resolve();self.private=self.root/'.private';self.private.mkdir()
  self.ctx={'manifest':{'worktree':str(self.root),'root':str(self.private/'capsule'),'files':{},'powershell':{'path':'synthetic-pwsh'}},'path':self.private/'manifest.json','sha':'a'*64}
  for name in bundle.SOURCE_PATHS:
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/name).read_bytes());self.ctx['manifest']['files']['policy/'+p.name]={'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())}
  report=json.loads((ROOT/bundle.REPORT).read_bytes())
  for name in report['source_pins']:
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/name).read_bytes())
  p=self.root/bundle.REPORT;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/bundle.REPORT).read_bytes())
  self.candidate=self.root/'candidate.bin';self.candidate.write_bytes(b'C'*bundle.CANDIDATE_SIZE)
  self.flash={};self.trace=[];self.child_errors=[];self.now=time.monotonic();self.empty=False;self.failed_observation=False;self.replay=False;self.fail_preflight=False
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
  self.install(patch.object(bundle,'CANDIDATE_SHA',sha(self.candidate.read_bytes())))
  self.install(patch.object(hardware.RomTransport,'__init__',lambda transport,private_root:setattr(transport,'private_root',private_root)))
  actual_backend=op.make_backend
  def create(ctx,bindings,transport):
   b=actual_backend(ctx,bindings,transport);b.inventory=lambda:self.records;b.monotonic=lambda:max(self.now,time.monotonic())
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
   def close(h):h.is_open=False;t.trace.append(('close',role))
  return Handle()
 def device(self,argv):
  role=next(r['role'] for r in self.data['roles'] if r['private_route']==argv[3]);cmd=argv[11:];self.trace.append((cmd[0],role,*cmd[1:3]))
  if cmd[0]=='read-mac':print('MAC: '+next(r['private_identity'] for r in self.data['roles'] if r['role']==role))
  elif cmd[0]=='flash-id':print('ESP32-S3\nDetected flash size: 16MB')
  elif cmd[0]=='read-flash':Path(cmd[3]).write_bytes(self.flash[role,int(cmd[1],0)])
  elif cmd[0]=='write-flash':self.flash[role,int(cmd[3],0)]=Path(cmd[4]).read_bytes()
  elif cmd[0]=='run':
   if self.flash[role,65536]!=self.original[role,65536]:self.flash[role,53248]=nvs.diagnostic(8,0)
   else:self.trace.append(('original_reset',role))
  else:raise AssertionError('unapproved device command')
 def dispatch(self,args,**kwargs):
  mode=args[args.index('-Mode')+1];p=Path(args[args.index('-Request')+1]);h=args[args.index('-RequestSha256')+1];child=json.loads(p.read_bytes())
  if self.fail_preflight and mode=='StageRom' and child['argv'][11]=='read-flash':
   self.fail_preflight=False;return SimpleNamespace(returncode=1,stdout=b'',stderr=b'')
  if self.failed_observation and child.get('purpose') is not None and child['argv'][11]=='read-flash':return SimpleNamespace(returncode=1,stdout=b'',stderr=b'')
  output=io.StringIO()
  try:
   with contextlib.redirect_stdout(output):
    (op.run_rom if mode=='StageRom' else backupop.run_rom)(self.ctx,p,h)
   if self.replay and child['argv'][11]=='run':
    copied,hh=self.store('copied-reset.json',child)
    with self.assertRaises(Exception):(op.run_rom if mode=='StageRom' else backupop.run_rom)(self.ctx,copied,hh)
   return SimpleNamespace(returncode=0,stdout=output.getvalue().encode(),stderr=b'')
  except Exception as error:
   self.child_errors.append(type(error).__name__+':'+str(error));return SimpleNamespace(returncode=1,stdout=b'',stderr=b'')
 def backup_request(self,operation='capture',origin=None):
  rp,rh=self.store('backup-request.json',self.data);now=int(time.time());attempt=('1' if operation=='capture' else '3')*32
  g={'schema':'OT190-BACKUP-GRANT-1','attempt':attempt,'request_sha256':backup.digest(self.data),'runtime_sha256':self.ctx['sha'],'operation':operation,'origin_attempt':origin,'attempt_count':1,'radio_allowed':False,'actions':['rom_read','rom_hold'] if operation=='capture' else ['rom_read','original_reset'],'issued_utc':now-1,'expires_utc':now+600}
  gp,gh=self.store(operation+'-grant.json',g)
  return self.store(operation+'-request.json',{'schema':'OT196-STAGE-BACKUP-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':operation,'request_path':str(rp),'request_sha256':rh,'grant_path':str(gp),'grant_sha256':gh,'origin_attempt':origin,'execution_request':None})
 def prepare(self):
  p,h=self.backup_request();self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'held')
  roles=backup.inspect_handoff(self.root,self.data,'1'*32)
  self.package=bundle.freeze(self.root,self.candidate,roles,runtime_sha256=self.ctx['sha']);pp,ph=self.store('package.json',self.package);now=int(time.time())
  grant={'schema':execution.SCHEMAS['execute'],'attempt':'2'*32,'package_sha256':bundle.digest(self.package),'runtime_sha256':self.ctx['sha'],'operation':'execute','origin_attempt':None,'attempt_count':1,'radio_allowed':False,'actions':execution.ACTIONS,'issued_utc':now-1,'expires_utc':now+600}
  gp,gh=self.store('stage-grant.json',grant)
  ep,eh=self.store('stage-request.json',{'schema':'OT196-STAGE-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':'execute','package_path':str(pp),'package_sha256':ph,'grant_path':str(gp),'grant_sha256':gh,'origin_attempt':None})
  rp,rh=self.store('backup-request.json',self.data)
  return self.store('handoff.json',{'schema':'OT196-STAGE-BACKUP-OPERATOR-1','runtime_sha256':self.ctx['sha'],'operation':'handoff','request_path':str(rp),'request_sha256':rh,'grant_path':None,'grant_sha256':None,'origin_attempt':'1'*32,'execution_request':{'path':str(ep),'sha256':eh}})
 def test_actual_handoff_child_endpoint_restore_both(self):
  p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'pass',self.child_errors)
  self.assertEqual(self.flash,self.original);self.assertTrue(all(r['stage_observation']['available'] for r in result['roles']))
  self.assertLess(self.trace.index(('original_reset','A')),self.trace.index(('open','B')))
 def test_failed_a_restore_untouched_b_release_and_reset_replay(self):
  self.empty=True;self.replay=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'evaluation_failed',self.child_errors)
  self.assertNotIn(('open','B'),self.trace);p,h=self.backup_request('reset','1'*32)
  self.assertEqual(backupop.run_operator(self.ctx,p,h)['status'],'reset_nvs_stale',self.child_errors);self.assertEqual(self.flash,self.original)
 def test_observation_failure_preserves_restore(self):
  self.failed_observation=True;p,h=self.prepare();result=backupop.run_operator(self.ctx,p,h);self.assertEqual(result['status'],'pass',self.child_errors);self.assertEqual(self.flash,self.original)
  self.assertTrue(all(not r['stage_observation']['available'] for r in result['roles']))
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
if __name__=='__main__':unittest.main()
