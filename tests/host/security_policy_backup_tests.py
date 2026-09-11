from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import security_policy_backup as core

class Backend:
    def __init__(self,request,data):
        self.bindings=tuple(SimpleNamespace(**{k:r[k] for k in ('role','private_route','private_identity')}) for r in request['roles'])
        self.data=data; self.calls=[]; self.fail=None
    def assert_idle(self): return True
    def read(self,role,offset,size):
        self.calls.append(('read',role,offset,size))
        if self.fail == (role,offset): raise RuntimeError('synthetic')
        return self.data[role][offset]
    def reset(self,role): self.calls.append(('reset',role)); return True

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name); (self.root/'.private').mkdir()
        self.data={role:{off:bytes([i+1])*size for i,(name,(off,size)) in enumerate(core.SPANS.items())} for role in ('A','B')}
        self.request={'schema':'OT190-BACKUP-REQUEST-1','runtime_sha256':'f'*64,'finish':'hold','roles':[]}
        for i,role in enumerate(('A','B')):
            self.request['roles'].append({'role':role,'private_route':'COM'+str(i+1),'private_identity':f'{i+1:012x}',
              'expected':{n:{'bytes':size,'sha256':core.sha(self.data[role][off])} for n,(off,size) in core.SPANS.items() if n!='nvs'}})
        for name,constant in (('partition','PARTITION_SHA'),('ota','OTA_SHA')):
            patcher=patch.object(core.execution.bundle,constant,self.request['roles'][0]['expected'][name]['sha256'])
            patcher.start(); self.addCleanup(patcher.stop)
        self.backend=Backend(self.request,self.data)
        self.attempt='a'*32
    def authority(self,attempt=None,operation='capture',origin=None):
        attempt=attempt or self.attempt
        obj={'schema':'OT190-BACKUP-GRANT-1','attempt':attempt,'request_sha256':core.digest(self.request),
         'runtime_sha256':'f'*64,'operation':operation,'origin_attempt':origin,'attempt_count':1,'radio_allowed':False,
         'actions':['rom_read','rom_hold'] if operation=='capture' and self.request['finish']=='hold' else ['rom_read','original_reset'],
         'issued_utc':10,'expires_utc':100}
        raw=core.canonical(obj); path=self.root/'.private'/('grant-'+attempt+'.json'); path.write_bytes(raw)
        return core.FileAuthority(self.root,path,core.sha(raw),utc=lambda:20)
    def capture(self): return core.backup(self.root,self.request,self.authority(),self.backend,utc=lambda:20)
    def test_hold_complete_no_reset(self):
        result=self.capture(); self.assertEqual(result['status'],'held'); self.assertEqual(len(self.backend.calls),10)
        roles=core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:20)
        self.assertEqual(len(roles),2); self.assertEqual(roles[0]['nvs'].read_bytes(),self.data['A'][0xd000])
    def test_reset_marks_stale(self):
        self.request['finish']='reset'; result=self.capture()
        self.assertFalse(result['usable_for_handoff']); self.assertEqual(sum(c[0]=='reset' for c in self.backend.calls),2)
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:20)
    def test_consumed_grant(self):
        self.capture()
        with self.assertRaises(Exception): self.capture()
        self.assertEqual(len(self.backend.calls),10)
    def test_changed_original_no_reset(self):
        self.data['B'][0x10000]=b'x'*589824
        with self.assertRaises(Exception): self.capture()
        self.assertFalse(any(c[0]=='reset' for c in self.backend.calls))
    def test_partial_read_release(self):
        self.backend.fail=('B',0)
        with self.assertRaises(Exception): self.capture()
        self.backend.fail=None
        result=core.reset_backup(self.root,self.request,self.authority('b'*32,'reset',self.attempt),self.backend,origin_attempt=self.attempt)
        self.assertEqual(result['status'],'reset_nvs_stale'); self.assertEqual(sum(c[0]=='reset' for c in self.backend.calls),2)
    def test_expired_hold(self):
        self.capture()
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:101)
    def test_snapshot_tamper(self):
        self.capture(); core.path_for(self.root,self.attempt,'A-nvs.bin').write_bytes(b'x'*12288)
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:20)
    def test_handoff_once(self):
        self.capture(); core.handoff(self.root,self.request,self.attempt,execution_attempt='b'*32,package_sha256='c'*64,utc=lambda:20)
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:20)
        self.assertFalse((self.root/'.private'/core.ACTIVE).exists())
    def test_failed_snapshot_no_reset(self):
        with patch.object(core,'_snapshot',side_effect=OSError('synthetic')):
            with self.assertRaises(Exception): self.capture()
        self.assertFalse(any(c[0]=='reset' for c in self.backend.calls))
    def test_bad_authority_before_io(self):
        authority=self.authority(); authority.expected='0'*64
        with self.assertRaises(Exception): core.backup(self.root,self.request,authority,self.backend)
        self.assertFalse(self.backend.calls)
    def test_wrong_binding_before_io(self):
        self.backend.bindings[0].private_route='COM9'
        with self.assertRaises(Exception): self.capture()
        self.assertFalse(self.backend.calls)
    def test_external_active_blocks(self):
        (self.root/'.private'/'security-policy-active.lock').write_bytes(b'{}')
        with self.assertRaises(Exception): self.capture()
        self.assertFalse(self.backend.calls)
    def test_nested_process_blocks(self):
        @core.single_process
        def nested(root): return self.capture()
        with self.assertRaises(Exception): nested(self.root)
        self.assertFalse(self.backend.calls)
    def test_handoff_clock_rollback(self):
        self.capture()
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:9)
    def test_journal_unknown_field(self):
        self.capture(); path=core.Journal(self.root,self.attempt).path
        rows=[core.decode(x) for x in path.read_bytes().splitlines()]; rows[1]['unknown']=True
        path.write_bytes(b''.join(core.canonical(x)+b'\n' for x in rows))
        with self.assertRaises(Exception): core.inspect_handoff(self.root,self.request,self.attempt,utc=lambda:20)
    def test_release_untouched_legacy_preflight(self):
        self.capture(); trial='d'*32; package='e'*64
        core.handoff(self.root,self.request,self.attempt,execution_attempt=trial,package_sha256=package,utc=lambda:20)
        grant=core.execution.Grant(trial,package,'execute',None,'f'*64)
        journal=core.execution.Journal(self.root,trial,package,create=True,grant=grant)
        journal.add('preflight_intent','A')
        result=core.reset_backup(self.root,self.request,self.authority('b'*32,'reset',self.attempt),self.backend,origin_attempt=self.attempt)
        self.assertEqual(result['status'],'reset_nvs_stale')
        self.assertFalse((self.root/'.private'/'security-policy-active.lock').exists())
        self.assertTrue(core.path_for(self.root,'b'*32,'legacy-reconciled.json').exists())
        self.attempt='c'*32; self.capture()
    def test_release_rejects_candidate_touched(self):
        self.capture(); trial='d'*32; package='e'*64
        core.handoff(self.root,self.request,self.attempt,execution_attempt=trial,package_sha256=package,utc=lambda:20)
        grant=core.execution.Grant(trial,package,'execute',None,'f'*64)
        journal=core.execution.Journal(self.root,trial,package,create=True,grant=grant)
        journal.add('preflight_intent','A'); journal.add('preflight_verified','A'); journal.add('candidate_write_intent','A')
        self.backend.calls.clear()
        with self.assertRaises(Exception): core.reset_backup(self.root,self.request,self.authority('b'*32,'reset',self.attempt),self.backend,origin_attempt=self.attempt)
        self.assertFalse(self.backend.calls)
    def test_finished_restored_a_releases_only_untouched_b(self):
        self.capture(); trial='d'*32; package='e'*64
        core.handoff(self.root,self.request,self.attempt,execution_attempt=trial,package_sha256=package,utc=lambda:20)
        grant=core.execution.Grant(trial,package,'execute',None,'f'*64)
        journal=core.execution.Journal(self.root,trial,package,create=True,grant=grant)
        for event in ('preflight_intent','preflight_verified','candidate_write_intent','candidate_verified',
                      'candidate_boot_intent','candidate_booted','serial_open_intent'):
            journal.add(event,'A')
        journal.add('run_intent','A',challenge='1'*32)
        journal.add('evaluation','A',result='refused')
        for event in ('serial_closed','restore_intent','restore_verified','original_boot_intent','original_booted'):
            journal.add(event,'A')
        journal.release('evaluation_failed')
        self.backend.calls.clear()
        result=core.reset_backup(self.root,self.request,self.authority('b'*32,'reset',self.attempt),self.backend,origin_attempt=self.attempt)
        self.assertEqual(result['status'],'reset_nvs_stale')
        self.assertEqual([c for c in self.backend.calls if c[0]=='reset'],[('reset','B')])
        self.assertTrue(all(c[1]=='B' for c in self.backend.calls))
        self.assertEqual(len(self.backend.calls),5)
        self.assertTrue(all(c[0] in ('read','reset') for c in self.backend.calls))
    def test_child_rejects_old_release_grant(self):
        self.capture()
        journal=core.Journal(self.root,self.attempt)
        first=self.authority('b'*32,'reset',self.attempt)
        second=self.authority('c'*32,'reset',self.attempt)
        for authority in (first,second):
            grant=core._consume(self.root,self.request,authority,'reset',self.attempt)
            journal.add('release_authorized',grant=grant)
        journal.add('reset_verify',role='A')
        argv=['--chip','esp32s3','--port','COM1','--baud','115200','--before','default-reset','--after','no-reset','--no-stub','read-mac']
        with self.assertRaises(Exception):
            core.admit_child(self.root,self.request,first,argv,operation='reset',origin_attempt=self.attempt)
        self.assertEqual(core.admit_child(self.root,self.request,second,argv,operation='reset',origin_attempt=self.attempt),argv)
    def test_child_rejects_previous_role_phase(self):
        self.capture()
        journal=core.Journal(self.root,self.attempt)
        authority=self.authority('b'*32,'reset',self.attempt)
        grant=core._consume(self.root,self.request,authority,'reset',self.attempt)
        journal.add('release_authorized',grant=grant)
        journal.add('reset_verify',role='A')
        journal.add('reset_verify',role='B')
        argv=['--chip','esp32s3','--port','COM1','--baud','115200','--before','default-reset','--after','no-reset','--no-stub','read-mac']
        with self.assertRaises(Exception):
            core.admit_child(self.root,self.request,authority,argv,operation='reset',origin_attempt=self.attempt)
        argv[3]='COM2'
        self.assertEqual(core.admit_child(self.root,self.request,authority,argv,operation='reset',origin_attempt=self.attempt),argv)
    def test_reset_uncertain_no_retry(self):
        self.request['finish']='reset'
        self.backend.reset=lambda role: False
        with self.assertRaises(Exception): self.capture()
        self.backend.calls.clear()
        with self.assertRaises(Exception): core.reset_backup(self.root,self.request,self.authority('b'*32,'reset',self.attempt),self.backend,origin_attempt=self.attempt)
        self.assertFalse(self.backend.calls)

if __name__=='__main__': unittest.main()
