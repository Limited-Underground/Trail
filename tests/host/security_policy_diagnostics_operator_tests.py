"""Actual execution/diagnostic-backend composition; synthetic flash and serial only."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,time,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
import security_policy_execution as execution
import security_policy_hardware as hardware
import security_policy_diagnostics as diagnostics
import security_policy_execution_tests as execution_fixture

class Transport:
    def __init__(self,data,ports):
        self.data=data;self.ports=ports;self.now=0.0;self.calls=[];self.memory={};self.mode="pass";self.guard_failure_pending=False
        for row in data["roles"]:
            route=row["private_route"]
            self.memory[route,0x10000]=row["application"];self.memory[route,0xd000]=row["nvs"]
            for name,(offset,size) in execution.REGIONS.items():self.memory[route,offset]=bytes([len(name)])*size
    def clock(self):return self.now
    def inventory(self):
        if self.guard_failure_pending:
            self.guard_failure_pending=False
            changed=copy.deepcopy(self.ports);changed[0].serial_number="0"*12;return changed
        return self.ports
    def command(self,route,operation):
        self.calls.append(("command",route,tuple(operation)))
        row=next(r for r in self.data["roles"] if r["private_route"]==route)
        return ("MAC: "+row["private_identity"]+"\n").encode() if operation==["read-mac"] else b"ESP32-S3\nDetected flash size: 16MB\n"
    def read(self,route,offset,size):self.calls.append(("read",route,offset,size));return self.memory[route,offset]
    def write(self,route,offset,raw):self.calls.append(("write",route,offset));self.memory[route,offset]=raw;return True
    def reset(self,route):
        row=next(r for r in self.data["roles"] if r["private_route"]==route)
        kind="original_reset" if self.memory[route,0x10000]==row["application"] else "candidate_reset"
        self.calls.append((kind,route));return True
    def open(self,route):
        self.calls.append(("open",route));owner=self
        class Handle:
            is_open=True;timeout=0.25;write_timeout=0.5
            def __init__(self):self.chunks=[];self.reads=0
            def write(self,raw):
                challenge=raw.decode().strip().split()[-1]
                owner.memory[route,0xd000]=b"D"*12288
                self.chunks=[f"SEC_EVAL1 ot187-policy-v0 {challenge} pass\n".encode()]
                if owner.mode=="trailing" and route==owner.data["roles"][0]["private_route"]:self.chunks.append(b"PRIVATE_TRAILING_BYTES\n")
                return len(raw)
            def read(self,size):
                owner.now+=min(self.timeout,0.25);self.reads+=1
                if owner.mode=="exception" and route==owner.data["roles"][0]["private_route"]:raise RuntimeError("PRIVATE_TRANSPORT_EXCEPTION")
                if owner.mode=="guard" and self.reads==2 and route==owner.data["roles"][0]["private_route"]:owner.guard_failure_pending=True
                return self.chunks.pop(0) if self.chunks else b""
            def close(self):owner.calls.append(("close",route));self.is_open=False
        return Handle()

class Tests(unittest.TestCase):
    def setUp(self):
        execution_fixture.Tests.setUp(self)
        for i,row in enumerate(self.data["roles"]):row["private_route"]="COM"+str(i+1)
        self.bindings=tuple(hardware.RoleBinding(r["role"],r["private_route"],r["private_identity"]) for r in self.data["roles"])
        self.ports=[SimpleNamespace(device=b.private_route,serial_number=b.private_identity,vid=0x303A,pid=0x1001) for b in self.bindings]
        self.transport=Transport(self.data,self.ports)
        self.backend=diagnostics.make_backend(self.bindings,transport=self.transport,inventory=self.transport.inventory,monotonic=self.transport.clock)
    def store(self,name,value):
        path=self.root/".private"/name;raw=execution.canonical(value);path.write_bytes(raw);return path,execution.sha(raw)
    def prepare_operator(self,handoff=False):
        import security_policy_operator as operator
        import security_policy_backup as backup
        bundle=execution.bundle
        for name,key in [("PARTITION_SHA","partition"),("OTA_SHA","ota")]:
            patcher=patch.object(bundle,name,self.data["roles"][0]["protected"][key]["sha256"]);patcher.start();self.addCleanup(patcher.stop)
        ctx={"manifest":{"root":str(self.root/".private/capsule"),"worktree":str(self.root),"files":{}},"path":self.root/".private/runtime.json","sha":"e"*64}
        for suffix in (*operator.POLICY,"backup","backup_operator","diagnostics"):
            ctx["manifest"]["files"]["policy/security_policy_"+suffix+".py"]={"bytes":1,"sha256":"f"*64}
        role_inputs=[]
        for row in self.data["roles"]:
            out={k:row[k] for k in ("role","private_route","private_identity","protected")}
            for name in ("application","nvs"):
                path=self.root/".private"/(row["role"]+"-"+name+".bin");path.write_bytes(row[name]);out[name]=path
            role_inputs.append(out)
        backup_data=None
        if handoff:
            backup_data={"schema":"OT190-BACKUP-REQUEST-1","runtime_sha256":ctx["sha"],"finish":"hold","roles":[]}
            for row in self.data["roles"]:
                expected=copy.deepcopy(row["protected"]);expected["application"]={"bytes":len(row["application"]),"sha256":execution.sha(row["application"])}
                backup_data["roles"].append({**{k:row[k] for k in ("role","private_route","private_identity")},"expected":expected})
            now=int(time.time());grant={"schema":"OT190-BACKUP-GRANT-1","attempt":"2"*32,"request_sha256":backup.digest(backup_data),"runtime_sha256":ctx["sha"],"operation":"capture","origin_attempt":None,"attempt_count":1,"radio_allowed":False,"actions":["rom_read","rom_hold"],"issued_utc":now-1,"expires_utc":now+600}
            gp,gh=self.store("backup-grant.json",grant)
            backup.backup(self.root,backup_data,backup.FileAuthority(self.root,gp,gh),self.backend)
            role_inputs=backup.inspect_handoff(self.root,backup_data,"2"*32)
        roles=bundle._roles(self.root,role_inputs,True)
        package={"roles":roles,"sources":{"tools/security_policy_"+s+".py":ctx["manifest"]["files"]["policy/security_policy_"+s+".py"] for s in operator.POLICY}}
        # Fixture material's exact digest must match actual consumed authority.
        self.data["package_sha256"]=bundle.digest(package)
        pp,ph=self.store("operator-package.json",package);now=int(time.time())
        grant={"schema":"OT188-GRANT-1","attempt":self.attempt,"package_sha256":self.data["package_sha256"],"operation":"execute","origin_attempt":None,"attempt_count":1,"radio_allowed":False,"actions":execution.ACTIONS,"issued_utc":now-1,"expires_utc":now+600}
        gp,gh=self.store("operator-grant.json",grant)
        request={"schema":"OT189-OPERATOR-1","runtime_sha256":ctx["sha"],"operation":"execute","package_path":str(pp),"package_sha256":ph,"grant_path":str(gp),"grant_sha256":gh,"origin_attempt":None}
        rp,rh=self.store("operator.json",request)
        if handoff:
            bp,bh=self.store("backup-request.json",backup_data)
            request={"schema":"OT190-OPERATOR-1","runtime_sha256":ctx["sha"],"operation":"handoff","request_path":str(bp),"request_sha256":bh,"grant_path":None,"grant_sha256":None,"origin_attempt":"2"*32,"execution_request":{"path":str(rp),"sha256":rh}}
            rp,rh=self.store("handoff.json",request)
        return ctx,rp,rh

    def run_worker(self,handoff=False):
        import security_policy_operator as operator
        import security_policy_backup_operator as backup_operator
        ctx,path,digest=self.prepare_operator(handoff)
        factory=diagnostics.make_backend
        def configured(bindings,**kwargs):
            return factory(bindings,**kwargs,inventory=self.transport.inventory,monotonic=self.transport.clock)
        with patch.object(operator,"make_transport",return_value=self.transport), patch.object(operator,"audit_loaded_modules"), patch.object(diagnostics,"make_backend",side_effect=configured), patch.dict(execution.execute.__wrapped__.__kwdefaults__,{"monotonic":self.transport.clock}):
            return (backup_operator if handoff else operator).run_operator(ctx,path,digest)
    def assert_envelope(self,result,status):
        self.assertEqual(status,result["status"])
        envelope=result["receipt_diagnostics"]
        self.assertEqual("OT192-RECEIPT-DIAGNOSTICS-1",envelope["schema"])
        self.assertTrue(envelope["available"])
        self.assertTrue(envelope["roles"])
        self.assertNotIn("PRIVATE",json.dumps(envelope))
        return envelope["roles"]
    def test_operator_actual_dispatch_success(self):
        rows=self.assert_envelope(self.run_worker(),"pass")
        self.assertTrue(all(r["receipt_accepted"] and r["close_confirmed"] for r in rows))
        for role in "AB":self.assert_original(role)
    def test_operator_actual_dispatch_failure_status_preserved(self):
        self.transport.mode="trailing"
        rows=self.assert_envelope(self.run_worker(),"evaluation_failed")
        self.assertTrue(rows[0]["matching_receipt_observed"])
        self.assertFalse(rows[0]["receipt_accepted"])
        self.assert_original("A")
    def test_actual_backup_handoff_success(self):
        result=self.run_worker(handoff=True)
        self.assert_envelope(result,"pass")
        self.assertEqual("handoff",result["operation"])
        self.assertFalse(result["backup_release_check_required"])
        self.assertFalse((self.root/".private/security-policy-active.lock").exists())
        self.assertFalse((self.root/".private/security-policy-backup-active.lock").exists())
        for role in "AB":self.assert_original(role)
    def test_diagnostic_summary_failure_preserves_success(self):
        with patch.object(diagnostics,"summary",side_effect=RuntimeError("PRIVATE_SUMMARY")):
            result=self.run_worker()
        self.assertEqual("pass",result["status"])
        self.assertEqual({"schema":"OT192-RECEIPT-DIAGNOSTICS-1","available":False,"roles":[]},result["receipt_diagnostics"])
        self.assertNotIn("PRIVATE",json.dumps(result))

    def execute(self):
        challenges=iter(["a"*32,"b"*32])
        return execution.execute(self.root,{},execution_fixture.Authority(self.attempt),self.backend,monotonic=self.transport.clock,challenge_factory=lambda:next(challenges))
    def assert_original(self,role):
        row=next(r for r in self.data["roles"] if r["role"]==role);route=row["private_route"]
        self.assertEqual(row["application"],self.transport.memory[route,0x10000]);self.assertEqual(row["nvs"],self.transport.memory[route,0xd000])
    def test_success_sequential_restoration(self):
        result=self.execute();self.assertEqual("pass",result["status"])
        for role in "AB":self.assert_original(role)
        a,b=[r["private_route"] for r in self.data["roles"]];calls=self.transport.calls
        self.assertLess(calls.index(("original_reset",a)),next(i for i,c in enumerate(calls) if c[1]==b))
        self.assertTrue(diagnostics.summary(self.backend))
    def test_trailing_data_restores_a_and_stops_b(self):
        self.transport.mode="trailing";result=self.execute();self.assertEqual("evaluation_failed",result["status"]);self.assert_original("A")
        self.assertFalse(any(c[1]==self.data["roles"][1]["private_route"] for c in self.transport.calls))
        summary=diagnostics.summary(self.backend);self.assertTrue(summary[0]["matching_receipt_observed"]);self.assertFalse(summary[0]["receipt_accepted"]);self.assertTrue(summary[0]["close_confirmed"]);self.assertNotIn("PRIVATE_TRAILING",json.dumps(summary));self.assertTrue(summary)
    def test_guard_failure_after_receipt_is_retained_after_close(self):
        self.transport.mode="guard";self.assertEqual("evaluation_failed",self.execute()["status"]);self.assert_original("A")
        report=diagnostics.summary(self.backend)[0];self.assertTrue(report["matching_receipt_observed"]);self.assertFalse(report["receipt_accepted"]);self.assertTrue(report["close_confirmed"]);self.assertTrue(self.backend.assert_idle())
    def test_exception_never_leaks(self):
        self.transport.mode="exception";self.assertEqual("evaluation_failed",self.execute()["status"]);self.assert_original("A")
        text=json.dumps(diagnostics.summary(self.backend));self.assertNotIn("PRIVATE",text)
        for row in self.data["roles"]:self.assertNotIn(row["private_route"],text);self.assertNotIn(row["private_identity"],text)
if __name__=="__main__":unittest.main()
