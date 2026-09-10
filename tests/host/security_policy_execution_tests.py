"""Adversarial actual-runner tests. Only package verification is replaced by fixture material."""
from pathlib import Path
from types import SimpleNamespace
import copy,tempfile,unittest,sys
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
import security_policy_execution as x

class Authority:
    def __init__(self,attempt):self.attempt=attempt
    def validate(self,package_sha256,*,operation,origin_attempt=None):
        return x.Grant(self.attempt,package_sha256,operation,origin_attempt,"f"*64)

class Backend:
    def __init__(self,data):
        self.data=data;self.bindings=[SimpleNamespace(role=r["role"],private_route=r["private_route"],private_identity=r["private_identity"]) for r in data["roles"]]
        self.flash={};self.trace=[];self.leased=None;self.fail=None;self.used_fail=False;self.original_resets=0
        for row in data["roles"]:
            role=row["role"];self.flash[role,x.APP_OFFSET]=row["application"];self.flash[role,x.NVS_OFFSET]=row["nvs"]
            for name,(offset,size) in x.REGIONS.items():self.flash[role,offset]=bytes([len(name)])*size
    def assert_idle(self):return self.leased is None
    def read(self,role,offset,size):
        if self.leased:raise RuntimeError("ROM_WITH_LEASE")
        self.trace.append(("read",role,offset));raw=self.flash[role,offset]
        if self.fail=="corrupt_restore_read" and offset==x.APP_OFFSET and self.used_fail:return b"?"*size
        return raw[:size]
    def write(self,role,offset,raw):
        if self.leased:raise RuntimeError("ROM_WITH_LEASE")
        self.trace.append(("write",role,offset));row=next(r for r in self.data["roles"] if r["role"]==role)
        candidate=offset==x.APP_OFFSET and raw!=row["application"]
        if candidate and self.fail=="partial_candidate" and not self.used_fail:
            self.used_fail=True;self.flash[role,offset]=raw[:50]+self.flash[role,offset][50:];return False
        if not candidate and self.fail=="restore_write" and not self.used_fail:
            self.used_fail=True;return False
        self.flash[role,offset]=raw
        if not candidate and offset==x.APP_OFFSET and self.fail=="corrupt_restore_read":self.used_fail=True
        return True
    def reset(self,role):
        if self.leased:raise RuntimeError("ROM_WITH_LEASE")
        row=next(r for r in self.data["roles"] if r["role"]==role)
        original=self.flash[role,x.APP_OFFSET]==row["application"]
        self.trace.append(("original_reset" if original else "candidate_reset",role,0))
        if original:
            self.original_resets+=1
            if self.fail=="postboot_journal":self.flash[role,x.NVS_OFFSET]=b"L"*x.NVS_SPAN
        return True
    def open(self,role):
        self.trace.append(("open",role,0));self.leased=role;owner=self
        class Endpoint:
            def run_once(self,challenge,deadline):
                owner.trace.append(("run",role,0));owner.flash[role,x.NVS_OFFSET]=b"D"*x.NVS_SPAN
                if owner.fail=="partial_run":raise RuntimeError("PRIVATE partial RUN")
                result="refused" if owner.fail=="refused" else "pass"
                return f"SEC_EVAL1 ot187-policy-v0 {challenge} {result}\n".encode()
        return Endpoint()
    def close(self,role):
        self.trace.append(("close",role,0))
        if self.fail=="close":return False
        self.leased=None;return True

class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);(self.root/".private").mkdir()
        self.data={"package_sha256":"a"*64,"candidate":b"candidate","roles":[]}
        for i,role in enumerate(["A","B"]):
            self.data["roles"].append({"role":role,"private_route":f"route{i}","private_identity":str(i+1)*12,"application":bytes([65+i])*x.APP_SPAN,"nvs":bytes([75+i])*x.NVS_SPAN,"protected":{name:{"bytes":size,"sha256":x.sha(bytes([len(name)])*size)} for name,(_,size) in x.REGIONS.items()}})
        self.backend=Backend(self.data);self.verify_calls=[]
        def verify(root,package,recovery=False):
            self.verify_calls.append(recovery);d=copy.deepcopy(self.data)
            if recovery:d.pop("candidate",None)
            return d
        self.patcher=patch.object(x.bundle,"verify",side_effect=verify);self.patcher.start();self.addCleanup(self.patcher.stop)
        self.attempt="1"*32
    def execute(self):
        challenges=iter(["a"*32,"b"*32])
        return x.execute(self.root,{},Authority(self.attempt),self.backend,monotonic=lambda:0,challenge_factory=lambda:next(challenges))
    def recover(self):return x.recover(self.root,{},Authority("2"*32),self.backend,origin_attempt=self.attempt)
    def writes(self):return [t for t in self.backend.trace if t[0]=="write"]
    def test_success_sequential_restore(self):
        self.assertEqual("pass",self.execute()["status"])
        trace=self.backend.trace;self.assertLess(trace.index(("original_reset","A",0)),next(i for i,t in enumerate(trace) if t[1]=="B"))
        for r in self.data["roles"]:
            self.assertEqual(r["application"],self.backend.flash[r["role"],x.APP_OFFSET]);self.assertEqual(r["nvs"],self.backend.flash[r["role"],x.NVS_OFFSET])
    def test_changed_initial_nvs_no_write(self):
        self.backend.flash["A",x.NVS_OFFSET]=b"X"*x.NVS_SPAN
        self.assertEqual("reconciliation_required",self.execute()["status"]);self.assertEqual([],self.writes())
    def test_partial_candidate_restored_without_candidate_boot(self):
        self.backend.fail="partial_candidate";self.assertEqual("evaluation_failed",self.execute()["status"])
        self.assertNotIn(("candidate_reset","A",0),self.backend.trace);self.assertIn(("original_reset","A",0),self.backend.trace)
    def test_partial_run_dirty_nvs_restored(self):
        self.backend.fail="partial_run";self.assertEqual("evaluation_failed",self.execute()["status"])
        self.assertEqual(self.data["roles"][0]["nvs"],self.backend.flash["A",x.NVS_OFFSET])
    def test_corrupt_restore_never_boots_original(self):
        self.backend.fail="corrupt_restore_read";self.assertEqual("recovery_required",self.execute()["status"]);self.assertEqual(0,self.backend.original_resets)
    def test_close_failure_no_later_rom(self):
        self.backend.fail="close";self.assertEqual("recovery_required",self.execute()["status"])
        trace=self.backend.trace;at=trace.index(("close","A",0));self.assertFalse(any(t[0] in ["read","write","original_reset"] for t in trace[at+1:]))
    def test_restore_failure_recovery_without_candidate(self):
        self.backend.fail="restore_write";self.assertEqual("recovery_required",self.execute()["status"])
        self.backend.fail=None;self.assertEqual("recovered",self.recover()["status"]);self.assertTrue(self.verify_calls[-1])
    def test_original_reset_then_journal_failure_cannot_rewrite_nvs(self):
        self.backend.fail="postboot_journal";original=x.Journal.add
        def add(j,event,*args,**kw):
            if event=="original_booted":j.healthy=False;raise x.ExecutionError("journal_write_failed")
            return original(j,event,*args,**kw)
        with patch.object(x.Journal,"add",add):self.assertEqual("recovery_required",self.execute()["status"])
        before=len(self.writes())
        with self.assertRaises(x.ExecutionError):self.recover()
        self.assertEqual(before,len(self.writes()));self.assertEqual(b"L"*x.NVS_SPAN,self.backend.flash["A",x.NVS_OFFSET])
    def test_grant_reuse(self):
        self.assertEqual("pass",self.execute()["status"]);before=len(self.writes())
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual(before,len(self.writes()))
    def test_wrong_backend_binding(self):
        self.backend.bindings[0].private_identity="0"*12
        with self.assertRaises(x.ExecutionError):self.execute()
        self.assertEqual([],self.backend.trace)
    def test_refusal_does_not_start_b(self):
        self.backend.fail="refused";self.assertEqual("evaluation_failed",self.execute()["status"]);self.assertFalse(any(t[1]=="B" for t in self.backend.trace))
    def test_journal_barrier_failure_prevents_mutation(self):
        original=x.Journal.add
        def add(j,event,*args,**kw):
            if event=="candidate_write_intent":j.healthy=False;raise x.ExecutionError("journal_write_failed")
            return original(j,event,*args,**kw)
        with patch.object(x.Journal,"add",add):self.assertEqual("reconciliation_required",self.execute()["status"])
        self.assertEqual([],self.writes())
    def fresh_backend(self):
        fresh=Backend(self.data)
        fresh.flash=copy.deepcopy(self.backend.flash)
        self.backend=fresh

    def test_fresh_backend_cannot_bypass_unconfirmed_close(self):
        self.backend.fail="close"
        self.assertEqual("recovery_required",self.execute()["status"])
        self.fresh_backend()
        with self.assertRaises(x.ExecutionError):self.recover()
        self.assertEqual([],self.backend.trace)

    def test_fresh_backend_cannot_bypass_uncertain_open(self):
        def uncertain(role):
            self.backend.leased=role
            self.backend.trace.append(("open",role,0))
            raise RuntimeError("PRIVATE open status unknown")
        self.backend.open=uncertain;self.backend.fail="close"
        self.assertEqual("recovery_required",self.execute()["status"])
        self.fresh_backend()
        with self.assertRaises(x.ExecutionError):self.recover()
        self.assertEqual([],self.backend.trace)

    def test_partial_run_confirmed_close_allows_fresh_recovery(self):
        self.backend.fail="partial_run"
        original=self.backend.write
        def write(role,offset,raw):
            if offset==x.APP_OFFSET and raw==self.data["roles"][0]["application"]:
                return False
            return original(role,offset,raw)
        self.backend.write=write
        self.assertEqual("recovery_required",self.execute()["status"])
        self.assertIsNone(self.backend.leased)
        self.fresh_backend()
        self.assertEqual("recovered",self.recover()["status"])
        self.assertEqual(self.data["roles"][0]["nvs"],self.backend.flash["A",x.NVS_OFFSET])

    def test_concurrent_invocation_has_no_io(self):
        @x.single_process
        def hold(root):
            with self.assertRaisesRegex(x.ExecutionError,"process_lease_unavailable"):
                self.execute()
        hold(self.root)
        self.assertEqual([],self.backend.trace)
        self.assertEqual([],self.verify_calls)

    def test_damaged_journals_refuse_before_hardware(self):
        self.backend.fail="restore_write"
        self.assertEqual("recovery_required",self.execute()["status"])
        self.backend.fail=None
        path=self.root/".private"/f"security-policy-{self.attempt}.jsonl"
        original=path.read_bytes();rows=[x.decode(line) for line in original.splitlines()]
        extra=copy.deepcopy(rows);extra[1]["unknown"]="PRIVATE"
        illegal=copy.deepcopy(rows);illegal[1]["event"]="original_booted"
        wrongrole=copy.deepcopy(rows);wrongrole[1]["role"]="B"
        for raw in [original[:-1],b"not-json\n",b"".join(x.canonical(r)+b"\n" for r in extra),b"".join(x.canonical(r)+b"\n" for r in illegal),b"".join(x.canonical(r)+b"\n" for r in wrongrole)]:
            with self.subTest(raw_size=len(raw)):
                path.write_bytes(raw);before=list(self.backend.trace)
                with self.assertRaises(x.ExecutionError):self.recover()
                self.assertEqual(before,self.backend.trace)
        path.write_bytes(original)

    def test_run_journal_failure_closes_without_rom(self):
        original=x.Journal.add
        def add(j,event,*args,**kw):
            if event=="run_intent":j.healthy=False;raise x.ExecutionError("journal_write_failed")
            return original(j,event,*args,**kw)
        with patch.object(x.Journal,"add",add):
            self.assertEqual("recovery_required",self.execute()["status"])
        self.assertIsNone(self.backend.leased)
        trace=self.backend.trace;at=trace.index(("open","A",0))
        self.assertEqual([("close","A",0)],trace[at+1:])
        self.assertEqual(1,len(self.writes()))

    def test_evaluation_journal_failure_prevents_restore(self):
        original=x.Journal.add
        def add(j,event,*args,**kw):
            if event=="evaluation":j.healthy=False;raise x.ExecutionError("journal_write_failed")
            return original(j,event,*args,**kw)
        with patch.object(x.Journal,"add",add):self.assertEqual("recovery_required",self.execute()["status"])
        self.assertEqual(1,len(self.writes()));self.assertEqual(0,self.backend.original_resets)
        self.assertIsNone(self.backend.leased)
        self.assertEqual(("close","A",0),self.backend.trace[-1])
if __name__=="__main__":unittest.main()
