"""Actual additive backup worker/controller composition with synthetic flash only."""
from pathlib import Path
from types import SimpleNamespace
import copy,hashlib,importlib,json,sys,tempfile,unittest,time
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
import security_policy_backup_operator as worker
import security_policy_backup as core

def encoded(value):return json.dumps(value,sort_keys=True,separators=(",",":")).encode()
def sha(raw):return hashlib.sha256(raw).hexdigest()
class Backend:
    def __init__(self,request):
        self.bindings=tuple(SimpleNamespace(**{k:r[k] for k in ("role","private_route","private_identity")}) for r in request["roles"])
        self.trace=[];self.flash={};self.fail=None
        for i,row in enumerate(request["roles"]):
            for offset,size in [(0,32768),(0x8000,4096),(0x9000,8192),(0xd000,12288),(0x10000,589824)]:self.flash[row["role"],offset]=bytes([(i if offset in (0xd000,0x10000) else 0)+offset//4096+1])*size
    def assert_idle(self):return True
    def read(self,role,offset,size):
        self.trace.append(("read",role,offset,size))
        if self.fail=="read":raise RuntimeError("private read failure")
        raw=self.flash[role,offset]
        return raw[:-1] if self.fail=="short" else raw
    def reset(self,role):
        self.trace.append(("reset",role))
        if self.fail=="reset":raise RuntimeError("private reset failure")
        self.flash[role,0xd000]=b"R"*12288
        return True
    def write(self,*args):raise AssertionError("backup may never write")
    def open(self,*args):raise AssertionError("backup may never open serial")
class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name).resolve();self.private=self.root/".private";self.private.mkdir()
        self.ctx={"manifest":{"worktree":str(self.root),"root":str(self.private/"capsule"),"files":{}},"path":self.private/"runtime.json","sha":"a"*64}
        self.data={"schema":"OT190-BACKUP-REQUEST-1","runtime_sha256":self.ctx["sha"],"finish":"hold","roles":[{"role":role,"private_route":"COM"+str(i+1),"private_identity":str(i+1)*12,"expected":{}} for i,role in enumerate(("A","B"))]}
        self.backend=Backend(self.data)
        for row in self.data["roles"]:
            for name,offset in [("application",0x10000),("bootloader",0),("partition",0x8000),("ota",0x9000)]:
                raw=self.backend.flash[row["role"],offset];row["expected"][name]={"bytes":len(raw),"sha256":sha(raw)}
        expected=self.data["roles"][0]["expected"]
        # Synthetic boot bytes; retain the actual strict equality checks.
        for name,key in [("PARTITION_SHA","partition"),("OTA_SHA","ota")]:
            patcher=patch.object(core.execution.bundle,name,expected[key]["sha256"])
            patcher.start();self.addCleanup(patcher.stop)
    def outer(self,operation="capture",origin=None,finish="hold"):
        self.data["finish"]=finish
        request_path,request_sha=self.store("request.json",self.data)
        now=int(time.time())
        grant={"schema":"OT190-BACKUP-GRANT-1","attempt":("1" if operation=="capture" else "2")*32,"request_sha256":core.digest(self.data),"runtime_sha256":self.ctx["sha"],"operation":operation,"origin_attempt":origin,"attempt_count":1,"radio_allowed":False,"actions":["rom_read","rom_hold"] if operation=="capture" and finish=="hold" else ["rom_read","original_reset"],"issued_utc":now-1,"expires_utc":now+600}
        grant_path,grant_sha=self.store(operation+"-grant.json",grant)
        outer={"schema":"OT190-OPERATOR-1","runtime_sha256":self.ctx["sha"],"operation":operation,"request_path":str(request_path),"request_sha256":request_sha,"grant_path":str(grant_path),"grant_sha256":grant_sha,"origin_attempt":origin,"execution_request":None}
        return self.store(operation+"-operator.json",outer)
    def run_outer(self,path,digest):
        import security_policy_hardware as hardware
        with patch.object(hardware,"Backend",return_value=self.backend),patch.object(worker,"make_transport",return_value=object()),patch.object(worker.operator,"audit_loaded_modules",return_value=True):
            return worker.run_operator(self.ctx,path,digest)
    def test_capture_has_no_fake_nvs_baseline_or_writes(self):
        path,digest=self.outer();result=self.run_outer(path,digest)
        self.assertEqual("capture",result["operation"]);self.assertIs(result["hardware_access"],True)
        self.assertTrue(self.backend.trace);self.assertFalse(any(row[0]=="reset" for row in self.backend.trace))
        self.assertNotIn("nvs",self.data["roles"][0]["expected"])
        self.assertNotIn("routeA",json.dumps(result))
    def test_runtime_mismatch_no_io(self):
        path,digest=self.outer();value=json.loads(path.read_bytes());value["runtime_sha256"]="b"*64;path.write_bytes(encoded(value))
        with self.assertRaises(Exception):self.run_outer(path,sha(path.read_bytes()))
        self.assertEqual([],self.backend.trace)
    def test_capture_grant_reuse_no_second_io(self):
        path,digest=self.outer();self.run_outer(path,digest);before=list(self.backend.trace)
        with self.assertRaises(Exception):self.run_outer(path,digest)
        self.assertEqual(before,self.backend.trace)
    def test_short_read_does_not_reset(self):
        self.backend.fail="short";path,digest=self.outer(finish="reset")
        try:self.run_outer(path,digest)
        except Exception:pass
        self.assertTrue(self.backend.trace);self.assertFalse(any(row[0]=="reset" for row in self.backend.trace))
    def test_capture_then_separate_reset(self):
        path,digest=self.outer();self.run_outer(path,digest)
        path,digest=self.outer("reset","1"*32)
        result=self.run_outer(path,digest);self.assertEqual("reset",result["operation"])
        self.assertEqual(2,sum(t[0]=="reset" for t in self.backend.trace))
        with self.assertRaises(Exception):core.inspect_handoff(self.root,self.data,"1"*32)
    def test_shared_execution_process_lock_excludes_backup(self):
        import security_policy_execution as execution
        path,digest=self.outer()
        @execution.single_process
        def holder(root):
            with self.assertRaises(Exception):self.run_outer(path,digest)
        holder(self.root);self.assertEqual([],self.backend.trace)

    def test_held_role_tuple_freezes_and_snapshot_corruption_refuses(self):
        import security_policy_bundle as bundle
        path,digest=self.outer();self.run_outer(path,digest)
        roles=core.inspect_handoff(self.root,self.data,"1"*32)
        expected=self.data["roles"][0]["expected"]
        with patch.object(bundle,"PARTITION_SHA",expected["partition"]["sha256"]),patch.object(bundle,"OTA_SHA",expected["ota"]["sha256"]):
            frozen=bundle._roles(self.root,roles,True)
        self.assertEqual(2,len(frozen));self.assertEqual(12288,frozen[0]["nvs"]["bytes"])
        self.assertTrue(all(isinstance(r["nvs"],Path) for r in roles))
        roles[0]["nvs"].write_bytes(b"X"*12288)
        with self.assertRaises(Exception):core.inspect_handoff(self.root,self.data,"1"*32)
        self.assertTrue((self.private/core.ACTIVE).exists())

    def test_expired_hold_and_execution_active_refuse_handoff(self):
        path,digest=self.outer();self.run_outer(path,digest)
        with self.assertRaises(Exception):core.inspect_handoff(self.root,self.data,"1"*32,utc=lambda:time.time()+7200)
        (self.private/"security-policy-active.lock").write_bytes(b"{}")
        with self.assertRaises(Exception):core.inspect_handoff(self.root,self.data,"1"*32)

    def test_child_admission_real_journal_no_write_or_early_reset(self):
        path,digest=self.outer();calls=[]
        prefix=["--chip","esp32s3","--port","COM1","--baud","115200","--before","default-reset","--after","no-reset","--no-stub"]
        def child(args):
            p,h=self.store("child.json",{"schema":"OT190-ROM-1","runtime_sha256":self.ctx["sha"],"operator_request":{"path":str(path),"sha256":digest},"argv":args})
            return worker.run_rom(self.ctx,p,h)
        original_import=importlib.import_module
        def imported(name):return SimpleNamespace(main=lambda args:calls.append(args)) if name=="esptool" else original_import(name)
        original_read=self.backend.read;exercised=[False]
        def read(role,offset,size):
            if not exercised[0]:
                exercised[0]=True;child(prefix+["read-mac"])
                for args in [prefix+["write-flash","--flash-size","16MB","0xd000","bad"],prefix[:9]+["hard-reset","--no-stub","run"]]:
                    with self.assertRaises(Exception):child(args)
            return original_read(role,offset,size)
        self.backend.read=read
        with patch.object(worker.importlib,"import_module",side_effect=imported),patch.object(worker.operator,"audit_loaded_modules",return_value=True):
            with self.assertRaises(Exception):child(prefix+["read-mac"])
            self.assertEqual([],calls)
            self.run_outer(path,digest)
            self.assertEqual([prefix+["read-mac"]],calls)
            with self.assertRaises(Exception):child(prefix+["read-mac"])

    def test_worker_handoff_snapshot_mismatch_before_consumption(self):
        import security_policy_bundle as bundle
        path,digest=self.outer();self.run_outer(path,digest)
        held=core.inspect_handoff(self.root,self.data,"1"*32);expected=self.data["roles"][0]["expected"]
        with patch.object(bundle,"PARTITION_SHA",expected["partition"]["sha256"]),patch.object(bundle,"OTA_SHA",expected["ota"]["sha256"]):
            roles=bundle._roles(self.root,held,True)
            pins={"policy/security_policy_"+suffix+".py":{"bytes":1,"sha256":"b"*64} for suffix in worker.operator.POLICY}
            self.ctx["manifest"]["files"]=pins
            package={"sources":{"tools/security_policy_"+suffix+".py":pins["policy/security_policy_"+suffix+".py"] for suffix in worker.operator.POLICY},"roles":copy.deepcopy(roles)}
            package["roles"][0]["nvs"]["sha256"]="c"*64
            package_path,package_sha=self.store("execution-package.json",package);grant_path,grant_sha=self.store("execution-grant.json",{})
            execute={"schema":"OT189-OPERATOR-1","runtime_sha256":self.ctx["sha"],"operation":"execute","package_path":str(package_path),"package_sha256":package_sha,"grant_path":str(grant_path),"grant_sha256":grant_sha,"origin_attempt":None}
            execute_path,execute_sha=self.store("execute.json",execute)
            outer=json.loads(path.read_bytes());outer.update(operation="handoff",grant_path=None,grant_sha256=None,origin_attempt="1"*32,execution_request={"path":str(execute_path),"sha256":execute_sha})
            handoff_path,handoff_sha=self.store("handoff.json",outer)
            before=list(self.backend.trace)
            with patch.object(bundle,"verify",return_value={"package_sha256":"d"*64,"roles":[]}),patch.object(core,"handoff",side_effect=AssertionError("must not consume")) as consume:
                with self.assertRaises(Exception):self.run_outer(handoff_path,handoff_sha)
                consume.assert_not_called()
            self.assertEqual(before,self.backend.trace)
            self.assertTrue((self.private/core.ACTIVE).exists())

    def test_positive_handoff_runs_real_execution_and_restores_both_roles(self):
        import security_policy_bundle as bundle
        import security_policy_execution as execution
        import security_policy_hardware as hardware
        from security_policy_execution_tests import Backend as ExecutionBackend
        capture_path,capture_sha=self.outer();self.run_outer(capture_path,capture_sha)
        held=core.inspect_handoff(self.root,self.data,"1"*32)
        roles=bundle._roles(self.root,held,True)
        pins={"policy/security_policy_"+suffix+".py":{"bytes":1,"sha256":"b"*64} for suffix in worker.operator.POLICY}
        self.ctx["manifest"]["files"]=pins
        package={"sources":{"tools/security_policy_"+suffix+".py":pins["policy/security_policy_"+suffix+".py"] for suffix in worker.operator.POLICY},"roles":roles}
        package_digest=bundle.digest(package)
        material={"package_sha256":package_digest,"candidate":b"synthetic candidate","roles":[]}
        for row in roles:
            raw=copy.deepcopy(row)
            for name in ("application","nvs"):raw[name]=Path(row[name]["path"]).read_bytes()
            material["roles"].append(raw)
        backend=ExecutionBackend(material);backend.flash=copy.deepcopy(self.backend.flash)
        package_path,package_sha=self.store("positive-package.json",package)
        now=int(time.time());attempt="3"*32
        grant={"schema":"OT188-GRANT-1","attempt":attempt,"package_sha256":package_digest,"operation":"execute","origin_attempt":None,"attempt_count":1,"radio_allowed":False,"actions":execution.ACTIONS,"issued_utc":now-1,"expires_utc":now+600}
        grant_path,grant_sha=self.store("positive-grant.json",grant)
        op={"schema":"OT189-OPERATOR-1","runtime_sha256":self.ctx["sha"],"operation":"execute","package_path":str(package_path),"package_sha256":package_sha,"grant_path":str(grant_path),"grant_sha256":grant_sha,"origin_attempt":None}
        op_path,op_sha=self.store("positive-execution.json",op)
        outer=json.loads(capture_path.read_bytes());outer.update(operation="handoff",grant_path=None,grant_sha256=None,origin_attempt="1"*32,execution_request={"path":str(op_path),"sha256":op_sha})
        path,digest=self.store("positive-handoff.json",outer)
        # Only artifact/build admission is synthetic; real role comparison,
        # FileAuthority, core handoff, process lock and execution journal run.
        with patch.object(bundle,"verify",side_effect=lambda *a,**kw:copy.deepcopy(material)),patch.object(hardware,"Backend",return_value=backend),patch.object(worker.operator,"make_transport",return_value=object()),patch.object(worker.operator,"audit_loaded_modules",return_value=True):
            result=worker.run_operator(self.ctx,path,digest)
        self.assertEqual("pass",result["status"]);self.assertEqual(2,len(result["roles"]))
        self.assertTrue(all(r["restored"] and r["evaluation"]=="pass" for r in result["roles"]))
        self.assertFalse((self.private/core.ACTIVE).exists());self.assertFalse((self.private/"security-policy-active.lock").exists())
        self.assertTrue((self.private/f"security-policy-grant-{attempt}.used").is_file())
        for row in material["roles"]:
            self.assertEqual(row["application"],backend.flash[row["role"],0x10000]);self.assertEqual(row["nvs"],backend.flash[row["role"],0xd000])
        trace=backend.trace;self.assertLess(trace.index(("original_reset","A",0)),next(i for i,t in enumerate(trace) if t[1]=="B"))
        with self.assertRaises(Exception):core.inspect_handoff(self.root,self.data,"1"*32)

    def store(self,name,obj):
        p=self.private/name;raw=encoded(obj);p.write_bytes(raw);return p,sha(raw)
if __name__=="__main__":unittest.main()
