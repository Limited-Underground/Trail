"""Actual isolated Windows launcher/worker admission; synthetic esptool never opens devices."""
from pathlib import Path
import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import security_policy_input_bundle as bundle
import security_policy_input_runtime_bundle as runtime
import security_policy_input_execution as execution

def encoded(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def digest(raw):return hashlib.sha256(raw).hexdigest()
def pin(path):
    raw=path.read_bytes();return {'bytes':len(raw),'sha256':digest(raw)}

@unittest.skipUnless(sys.platform=='win32','actual Windows isolated Python/PowerShell boundary')
class IsolatedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.root=Path(cls.temp.name).resolve()
        cls.work=cls.root/'work';cls.private=cls.work/'.private';cls.capsule=cls.private/'synthetic-capsule'
        cls.capsule.mkdir(parents=True)
        base=Path(sys.base_prefix)
        for name in ('python.exe','python314.dll','python3.dll'):
            shutil.copyfile(base/name,cls.capsule/name)
        for name in ('Lib','DLLs'):
            shutil.copytree(base/name,cls.capsule/name,ignore=shutil.ignore_patterns('site-packages','__pycache__','*.pyc','*.pyo','*.pth','test','tests','idlelib','tkinter','turtledemo','ensurepip','pydoc_data'))
        for name in runtime.POLICY:
            source=ROOT/'tools'/name
            for destination in (cls.work/'tools'/name,cls.capsule/'policy'/name):
                destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(source.read_bytes())
        # Only third-party boundary is synthetic. All policy source bytes remain actual.
        for name,raw in {'esptool':b'__version__="5.3.1"\ndef main(argv):\n print("SYNTHETIC_BOUNDARY_REACHED")\n',
                         'serial':b'__version__="3.5"\n'}.items():
            p=cls.capsule/'packages'/name/'__init__.py';p.parent.mkdir(parents=True);p.write_bytes(raw)
        (cls.capsule/'python314._pth').write_bytes(runtime.PTH)
        (cls.capsule/'esptool.cfg').write_bytes(runtime.ESPTOOL_CONFIG)
        pwsh=shutil.which('pwsh')
        if not pwsh:raise RuntimeError('pwsh_required')
        found=subprocess.run([pwsh,'-NoProfile','-NonInteractive','-Command',"[Console]::Write([IO.Path]::Combine($PSHOME,'pwsh.exe'))"],check=True,capture_output=True,timeout=30)
        cls.pwsh=Path(found.stdout.decode('utf-8-sig').strip()).resolve()
        cls.manifest={'schema':runtime.SCHEMA,'root':str(cls.capsule),'worktree':str(cls.work),
            'files':{p.relative_to(cls.capsule).as_posix():pin(p) for p in cls.capsule.rglob('*') if p.is_file()},
            'versions':{'python':'3.14.6','esptool':'5.3.1','pyserial':'3.5'},
            'powershell':{'path':str(cls.pwsh),**pin(cls.pwsh)}}
        cls.manifest_path=cls.private/'synthetic-manifest.json';cls.manifest_path.write_bytes(encoded(cls.manifest));cls.runtime_sha=digest(cls.manifest_path.read_bytes())
        report=ROOT/bundle.REPORT
        for name in [bundle.REPORT,*json.loads(report.read_bytes())['source_pins']]:
            path=cls.work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes((ROOT/name).read_bytes())
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def save(self,name,value):
        path=self.private/name;path.write_bytes(encoded(value));return {'path':str(path),'sha256':digest(path.read_bytes())}
    def launch(self,mode,request,*,poison=False):
        ref=self.save('child-request.json',request)
        env=os.environ.copy()
        if poison:
            evil=self.root/'evil';evil.mkdir(exist_ok=True)
            (evil/'sitecustomize.py').write_text('raise RuntimeError("HOSTILE_STARTUP")')
            env.update(PYTHONPATH=str(evil),PYTHONHOME=str(evil),ESPTOOL_OPEN_PORT_ATTEMPTS='99',ESPTOOL_CFGFILE=str(evil/'bad.cfg'))
        return subprocess.run([str(self.pwsh),'-NoProfile','-NonInteractive','-File',str(self.capsule/'policy/Invoke-SecurityPolicyInputOperator.ps1'),
            '-Manifest',str(self.manifest_path),'-ManifestSha256',self.runtime_sha,'-Mode',mode,
            '-Request',ref['path'],'-RequestSha256',ref['sha256']],capture_output=True,timeout=90,env=env)
    def reached(self,result):
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn(b'SYNTHETIC_BOUNDARY_REACHED',result.stdout)
    def refused(self,result):
        self.assertNotEqual(result.returncode,0)
        self.assertNotIn(b'SYNTHETIC_BOUNDARY_REACHED',result.stdout+result.stderr)
    def roles(self):
        roles=[]
        for index,role in enumerate('AB'):
            app=self.private/f'{role}-app.bin';app.write_bytes(bytes([index+1])*589824)
            nvs=self.private/f'{role}-nvs.bin';nvs.write_bytes(b'\xff'*12288)
            protected={name:{'bytes':size,'sha256':bundle.PARTITION_SHA if name=='partition' else bundle.OTA_SHA if name=='ota' else '1'*64} for name,size in bundle.PROTECTED.items()}
            roles.append({'role':role,'private_route':f'COM{91+index}','private_identity':f'0123456789a{index}',
                'application':{'path':str(app),**pin(app)},'nvs':{'path':str(nvs),**pin(nvs)},'protected':protected})
        return roles
    def stage_request(self):
        roles=self.roles();package={'schema':bundle.SCHEMA,'runtime_sha256':self.runtime_sha,'root':str(self.work),'scope':bundle.SCOPE,
            'candidate':{'path':str(self.private/'absent-candidate.bin'),'bytes':bundle.CANDIDATE_SIZE,'sha256':bundle.CANDIDATE_SHA},
            'build_report':{'path':str(self.work/bundle.REPORT),**pin(self.work/bundle.REPORT)},'roles':roles,
            'sources':{name:pin(self.work/name) for name in bundle.SOURCE_PATHS}}
        bundle.verify(self.work,package,recovery=True)
        package_ref=self.save('stage-package.json',package);package_sha=bundle.digest(package)
        origin='a'*32;attempt='b'*32;now=int(time.time())
        grant={'schema':execution.SCHEMAS['recover'],'attempt':attempt,'package_sha256':package_sha,'operation':'recover',
            'origin_attempt':origin,'attempt_count':1,'radio_allowed':False,'actions':execution.RECOVERY_ACTIONS,
            'issued_utc':now-1,'expires_utc':now+300,'runtime_sha256':self.runtime_sha}
        grant_ref=self.save('stage-grant.json',grant)
        events=[{'seq':0,'event':'created','attempt':origin,'package_sha256':package_sha,'grant':'c'*64}]
        for event in ('preflight_intent','preflight_verified','candidate_write_intent','restore_intent'):
            events.append({'seq':len(events),'event':event,'role':'A'})
        events.append({'seq':len(events),'event':'recovery_authorized','attempt':attempt,'grant':grant_ref['sha256']})
        (self.private/f'security-policy-{origin}.jsonl').write_bytes(b''.join(encoded(e)+b'\n' for e in events))
        self.save('security-policy-active.lock',{'attempt':origin,'package_sha256':package_sha})
        self.save(f'ot198-input-grant-{attempt}.used',{'package_sha256':package_sha,'grant':grant_ref['sha256'],'runtime_sha256':self.runtime_sha})
        op=self.save('stage-op.json',{'schema':'OT198-INPUT-OPERATOR-1','runtime_sha256':self.runtime_sha,'operation':'recover',
            'package_path':package_ref['path'],'package_sha256':package_ref['sha256'],'grant_path':grant_ref['path'],
            'grant_sha256':grant_ref['sha256'],'origin_attempt':origin})
        return {'schema':'OT198-INPUT-ROM-1','runtime_sha256':self.runtime_sha,'operator_request':op,
            'argv':['--chip','esp32s3','--port','COM91','--baud','115200','--before','default-reset','--after','no-reset','--no-stub','read-mac'],'purpose':None}
    def backup_request(self):
        roles=self.roles();data={'schema':'OT190-BACKUP-REQUEST-1','runtime_sha256':self.runtime_sha,'finish':'hold',
            'roles':[{'role':r['role'],'private_route':r['private_route'],'private_identity':r['private_identity'],
                      'expected':{**r['protected'],'application':{k:r['application'][k] for k in ('bytes','sha256')}}} for r in roles]}
        attempt='d'*32;now=int(time.time());data_ref=self.save('backup-data.json',data)
        grant={'schema':'OT190-BACKUP-GRANT-1','attempt':attempt,'request_sha256':digest(encoded(data)),
            'runtime_sha256':self.runtime_sha,'operation':'capture','origin_attempt':None,'attempt_count':1,'radio_allowed':False,
            'actions':['rom_read','rom_hold'],'issued_utc':now-1,'expires_utc':now+300}
        grant_ref=self.save('backup-grant.json',grant)
        rows=[{'seq':0,'event':'created','request':data,'grant':{**grant,'raw_sha256':grant_ref['sha256']}},
              {'seq':1,'event':'read_intent','role':'A'}]
        (self.private/f'security-policy-backup-{attempt}-journal.jsonl').write_bytes(b''.join(encoded(e)+b'\n' for e in rows))
        self.save('security-policy-backup-active.lock',{'attempt':attempt,'request':digest(encoded(data))})
        self.save(f'security-policy-backup-{attempt}-grant.used',{'grant':grant_ref['sha256'],'request':digest(encoded(data))})
        op=self.save('backup-op.json',{'schema':'OT198-INPUT-BACKUP-OPERATOR-1','runtime_sha256':self.runtime_sha,'operation':'capture',
            'request_path':data_ref['path'],'request_sha256':data_ref['sha256'],'grant_path':grant_ref['path'],
            'grant_sha256':grant_ref['sha256'],'origin_attempt':None,'execution_request':None})
        return {'schema':'OT198-INPUT-BACKUP-ROM-1','runtime_sha256':self.runtime_sha,'operator_request':op,
            'argv':['--chip','esp32s3','--port','COM91','--baud','115200','--before','default-reset','--after','no-reset','--no-stub','read-mac']}
    def test_stage_rom_actual_isolation_and_stale_authority(self):
        req=self.stage_request();self.reached(self.launch('InputRom',req,poison=True))
        self.refused(self.launch('InputRom',req))  # Exact child replay is consumed.
        req['argv'][-1]='erase-flash';self.refused(self.launch('InputRom',req))
        req['argv'][-1]='read-mac';req['schema']='OT189-ROM-1';self.refused(self.launch('InputRom',req))
        req['schema']='OT198-INPUT-ROM-1';(self.private/('ot198-input-grant-'+'b'*32+'.used')).write_bytes(b'{}')
        self.refused(self.launch('InputRom',req))
    def test_backup_rom_actual_isolation_and_stale_phase(self):
        req=self.backup_request();self.reached(self.launch('InputBackupRom',req,poison=True))
        req['argv'][-1]='erase-flash';self.refused(self.launch('InputBackupRom',req))
        req['argv'][-1]='read-mac';req['schema']='OT190-ROM-1';self.refused(self.launch('InputBackupRom',req))
        req['schema']='OT198-INPUT-BACKUP-ROM-1';path=self.private/('security-policy-backup-'+'d'*32+'-journal.jsonl')
        with path.open('ab') as out:out.write(encoded({'seq':2,'event':'read_intent','role':'B'})+b'\n')
        self.refused(self.launch('InputBackupRom',req))

if __name__=='__main__':unittest.main()
