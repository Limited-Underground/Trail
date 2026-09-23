"""Actual production Endpoint/EnrolledBridge against host composed C++ target.
No hardware. Actual target SDK work runs once on a serialized oracle clock.
Async host writes return in model time before target completion; guard/read waits
overlap that work. Response availability is an explicit sensitivity assumption.
"""
import argparse
import json
import subprocess
import sys
import queue
import threading
import importlib.util
import hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
import enrolled_pair_bridge as production
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

class World:
    def __init__(self,exe,cost,airtime,guard,polling,negative,transport="sync",delay_us=0,chunk_bytes=64,chunk_gap_us=0,rpc_timeout=10,launch=None):
        self.child=subprocess.Popen(launch or [str(exe),str(cost),str(airtime)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        self.rpc_timeout=rpc_timeout
        self.lines=queue.Queue()
        def reader():
            for line in self.child.stdout:self.lines.put(line)
            self.lines.put("")
        threading.Thread(target=reader,daemon=True).start()
        try:
            if self.lines.get(timeout=self.rpc_timeout).strip()!="READY":raise RuntimeError("startup_not_ready")
        except BaseException:
            self.close();raise
        self.transport=transport;self.delay_us=delay_us;self.chunk_bytes=chunk_bytes;self.chunk_gap_us=chunk_gap_us;self.target_us=100000;self.actions=[];self.target_commands=0;self.empty_reads=0;self.partial_reads=0;self.overlap_us=0;self.guard_overlap_us=0
        self.us=100000;self.guard_us=guard;self.polling=polling;self.negative=negative
        self.events=[];self.pending=[];self.rows=[];self.last=None;self.injected=False;self.issued={};self.button_lateness=[];self.guard_calls=0;self.read_calls=0;self.read_wait_us=0;self.sdk_reads=0;self.immediate_holds=[]
    def rpc(self,line):
        try:
            self.child.stdin.write(line+"\n");self.child.stdin.flush()
            raw=self.lines.get(timeout=self.rpc_timeout).strip()
        except BaseException:
            self.close();raise
        if not raw.startswith("RESULT "):
            self.close()
            raise RuntimeError("target_adapter_failed: "+raw+"; child exit="+str(self.child.poll()))
        try:
            values=raw.split();previous_target=self.target_us;self.target_us=int(values[1]);assert self.target_us>=previous_target
            self.us=max(self.us,self.target_us) if self.transport=="sync" else self.us
            self.sdk_reads+=int(values[2]);self.last=values[1:15]
            if len(values)<21:raise RuntimeError("target_adapter_shape")
            if line.startswith("CMD "):self.target_commands+=1
            self.rejections={"A":values[15:18],"B":values[18:21]}
            return bytes.fromhex(values[21]) if len(values)>21 else b""
        except BaseException:
            self.close();raise
    def advance(self,delta):
        target=self.us+max(0,int(delta))
        # Merge external inputs by timestamp. Target execution is nonpreemptive;
        # edges inside an oracle command are observed at its completion boundary.
        while (self.actions and self.actions[0][0]<=target) or (self.pending and self.pending[0][0]<=target):
            if self.actions and (not self.pending or self.actions[0][0]<=self.pending[0][0]):
                at,action=self.actions.pop(0)
            else:
                at,role,level=self.pending.pop(0)
                self.button_lateness.append(max(0,self.us-at,self.target_us-at))
                action=f"BUTTON {role} {level}"
            self.us=max(at,self.us)
            self.rpc("ADV "+str(max(at,self.target_us,self.us)))
            self.rpc(action)
        self.us=max(target,self.us)
        self.rpc("ADV "+str(max(target,self.target_us,self.us)))
    def clock(self):return self.us/1e6
    def sleep(self,seconds):self.advance(round(seconds*1e6))
    def notify(self,event):
        self.events.append(event)
        if event=="physical_comparison_and_buttons_required":
            if self.polling:
                self.pending=sorted([(self.us+4935931-555000,0,1),(self.us+4935931,0,0),(self.us+10620977-821000,1,1),(self.us+10620977,1,0)])
            else:
                for role in (0,1):
                    self.rpc(f"BUTTON {role} 0")
                    # Preserve synchronous semantics: wait600ms after press
                    # processing; async RPC advances target time, not host time.
                    raw_press_edge=self.target_us
                    self.rpc(f"BUTTON {role} 1")
                    # Press admission occurs after durable refresh_offer; release
                    # uses the pre-work sample. No work follows pressed_at_.
                    pressed_at=self.target_us
                    release_at=pressed_at+600000
                    assert self.target_us<=release_at,"button tick exceeded declared hold"
                    self.advance(release_at-self.us)
                    assert self.target_us==release_at,"release missed exact target hold"
                    self.rpc(f"BUTTON {role} 0")
                    self.immediate_holds.append(dict(role="AB"[role],raw_press_edge_us=raw_press_edge,admitted_press_at_us=pressed_at,released_at_us=release_at,observed_hold_us=release_at-pressed_at,raw_high_us=release_at-raw_press_edge))
    def guard(self):
        if getattr(self,"authority_pause",None) is not None:
            role=self.authority_pause;self.authority_pause=None
            self.advance(61000000);self.rpc(f"BUTTON {role} 0")
        self.guard_calls+=1
        self.guard_overlap_us+=max(0,min(self.us+self.guard_us,self.target_us)-self.us)
        self.advance(self.guard_us)
        return not (self.negative=="identity_loss" and self.injected)
    def close(self):
        # Cleanup must never mask constructor/RPC errors, including broken pipes.
        child=getattr(self,"child",None)
        if child is None:return
        try:
            if child.poll() is None:child.terminate()
            child.wait(timeout=2)
        except (OSError,subprocess.TimeoutExpired):
            try:child.kill();child.wait(timeout=2)
            except (OSError,subprocess.TimeoutExpired):pass
        for stream in (child.stdin,child.stdout,child.stderr):
            try:stream.close()
            except (OSError,ValueError):pass

class Serial:
    def __init__(self,world,role):self.w=world;self.role=role;self.is_open=True;self.data=bytearray();self.timeout=.1;self.chunks=[];self.tail_us=0
    @property
    def in_waiting(self):
        while self.chunks and self.chunks[0][0]<=self.w.us:
            _,raw=self.chunks.pop(0);self.data.extend(raw)
        return len(self.data)
    def write(self,raw):
        if raw==b"\n":return 1
        command=raw.decode().strip();verb=command.split()[1]
        start=self.w.us
        if not self.w.injected and verb=="RFFINISH" and self.w.negative in ("identity_loss","late_authority","malformed_RFREADY","rearm_failure"):
            self.w.injected=True
            if self.w.negative=="late_authority":self.w.advance(61000000)
            if self.w.negative=="rearm_failure":self.w.rpc("REARMFAIL")
        response=self.w.rpc(f"CMD {self.role} {command}")
        if self.w.negative=="malformed_RFREADY" and verb=="RFFINISH":response=b"OTENROLL1 RFFINISH 1 1 0 0 0 7\n"
        if verb=="RFFINISH" and not self.w.injected and self.w.negative in ("context_loss_response","expiry_response","authority_expiry_response"):
            self.w.injected=True
            if self.w.negative=="context_loss_response":
                self.w.actions.append((max(self.w.us,self.w.target_us)+1000,f"LOSS {self.role}"))
                self.w.delay_us=max(self.w.delay_us,200000)
            elif self.w.negative=="authority_expiry_response":
                self.w.delay_us=62000000;self.w.authority_pause=self.role
            else:self.w.delay_us=61000000
        if verb=="TIME" and response.startswith(b"OTENROLL1 ID "):
            self.w.issued["AB"[self.role]]=int(response.decode().split()[-1])*1000
        if self.w.transport=="sync":self.data.extend(response)
        else:
            ready=max(self.w.target_us+self.w.delay_us,self.tail_us)
            for offset in range(0,len(response),self.w.chunk_bytes):
                self.chunks.append((ready,response[offset:offset+self.w.chunk_bytes]))
                self.tail_us=ready;ready+=self.w.chunk_gap_us

        self.w.rows.append(dict(role="AB"[self.role],command=verb,start_us=start,end_us=self.w.us,target_end_us=self.w.target_us,response_available_us=self.tail_us if self.w.transport!="sync" else self.w.us,response=response.decode().strip() if verb in ("RFREADY","RFFINISH","RFPOLL","STATUS","RFSTAT","CLOSE") else "redacted",target=self.w.last))
        return len(raw)
    def read(self,count):
        self.w.read_calls+=1
        available=self.in_waiting
        start=self.w.us
        if not available:
            end=start+round(self.timeout*1e6)
            if self.chunks:end=min(end,self.chunks[0][0])
            self.w.overlap_us+=max(0,min(end,self.w.target_us)-start)
            self.w.read_wait_us+=end-start;self.w.advance(end-start)
            available=self.in_waiting
        result=bytes(self.data[:count]);del self.data[:count]
        self.w.empty_reads+=not result
        self.w.partial_reads+=bool(result) and len(result)<count
        return result
    def close(self):self.is_open=False

def run(args,mode,negative="none"):
    bridge_module=production
    if getattr(args,"bridge_source",None):
        spec=importlib.util.spec_from_file_location("enrolled_pair_bridge_before",args.bridge_source)
        bridge_module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=bridge_module;spec.loader.exec_module(bridge_module)
    polling=mode in ("polling_only","combined")
    airtime=args.airtime_us if mode in ("delayed_only","combined") else 0
    world=World(args.exe,args.sdk_us,airtime,args.guard_us,polling,negative,args.transport,args.response_delay_us,args.chunk_bytes,args.chunk_gap_us)
    try:
        ends=[bridge_module.Endpoint(Serial(world,i),world.guard,monotonic=world.clock,enable_transport_timing=True,transport_clock_ns=lambda:world.us*1000) for i in (0,1)]
        bridge=bridge_module.EnrolledBridge(*ends,signer=Ed25519PrivateKey.from_private_bytes(bytes(range(32))),group=17,epoch=3,radio=True,notify=world.notify,sleep=world.sleep,enable_host_timing=True,timing_clock_ns=lambda:world.us*1000)
        error=None
        try:bridge()
        except Exception as exc:error=str(exc)
        result=dict(mode=mode,transport=args.transport,negative=negative,error=error,result=bridge.result,cleanup=bridge.cleanup_outcome,first_failure=bridge.first_failure,elapsed_us=world.us-100000,commands=world.rows,events=world.events,statistics=bridge.statistics,last_target=world.last,
          model_inputs=dict(sdk_get_us=args.sdk_us,guard_us=args.guard_us,airtime_model="SF7/BW125/CR4:5 explicit CRC, actual frame length" if airtime else "instantaneous",confirmation_offsets_us=[4935931,10620977] if polling else "two sequential600ms waits after accepted press processing before polling"),
          limitations="Host async write schedules actual target oracle result without advancing modeled host clock; real Python RPC remains blocking. Target A/B commands serialized; oracle state is internal until response availability. External button/context changes during target execution are applied at its completion boundary, not preemptively. No independent dual-core, NVS/crypto/flash model. Response chunk arrival unmeasured; delay/chunks are sensitivity inputs, not physical timestamps. SDK execution overlaps read/guard host time; no duplicate target execution. Button observations remain upper-bound proxies.")
        result["first_target_rejection_layer_now_deadline_ms"]=world.rejections
        result["async_accounting"]=dict(target_commands=world.target_commands,empty_reads=world.empty_reads,partial_reads=world.partial_reads,target_read_overlap_us=world.overlap_us,target_guard_overlap_us=world.guard_overlap_us,response_delay_us=world.delay_us,chunk_bytes=world.chunk_bytes,chunk_gap_us=world.chunk_gap_us)
        result["per_role_invitation_margin_us"]={"A":int(world.last[10])*1000-world.us,"B":int(world.last[12])*1000-world.us}
        result["observed_invitation_issued_deadline_ms"]={"A":world.last[9:11],"B":world.last[11:13]}
        result["delivered_statuses"]=sum(row["command"]=="RFPOLL" and row["response"].startswith("OTENROLL1 RF RECEIVED ") for row in world.rows)
        result["event_quantization_us"]={"max_button_edge_lateness":max(world.button_lateness,default=0),"max_radio_completion_observation_lateness":int(world.last[13])}
        result["negative_injected"]=world.injected
        result["immediate_hold_observations"]=world.immediate_holds
        source=getattr(args,"bridge_source",None) or ROOT/"tools/enrolled_pair_bridge.py"
        result["bridge_source_sha256"]=hashlib.sha256(source.read_bytes()).hexdigest()
        result["cost_accounting"]={"guard_calls":world.guard_calls,"guard_us":world.guard_calls*world.guard_us,"read_calls":world.read_calls,"empty_read_wait_us":world.read_wait_us,"sdk_reads":world.sdk_reads,"sdk_read_us":world.sdk_reads*args.sdk_us}
        result["secrets_cleared"]={"A":world.last[7]=="1","B":world.last[8]=="1"}
        assert world.target_commands==len(world.rows),"duplicate or missing target command"
        if negative=="authority_expiry_response":
            for detail in world.rejections.values():assert int(detail[1])>=int(detail[2])>0
        if negative!="none":
            assert world.injected and error is not None,"negative control did not reject"
            if negative=="identity_loss":
                assert bridge.cleanup_outcome=="protocol_unverified_handles_closed"
                assert world.rows[-1]["command"]=="RFFINISH","identity loss permitted later target command"
            else:
                assert world.last[7:9]==["1","1"],"negative cleanup did not clear both sessions"
            expected={"authority_expiry_response":"late_write","context_loss_response":"target_refused","expiry_response":"late_read","identity_loss":"passive_lease_invalid","late_authority":"late_write","malformed_RFREADY":"radio_statistics_invalid","rearm_failure":"target_refused"}[negative]
            assert error==expected,(negative,error)
        else:
            if args.transport=="sync":assert error is None and result["delivered_statuses"]==8 and bridge.cleanup_outcome=="verified","unexplained profile failure"
        return result
    finally:
        world.close()

def controls(exe):
    checks=[]
    # Discriminator: target press processing runs ahead of the async host clock.
    # The old host-relative advance produced admitted holds below500ms.
    w=object.__new__(World);w.events=[];w.polling=False;w.us=100000;w.target_us=200000
    w.actions=[];w.pending=[];w.immediate_holds=[]
    def button_rpc(line):
        if line.startswith("ADV "):w.target_us=int(line.split()[1])
        elif line.startswith("BUTTON "):w.target_us+=110770
    w.rpc=button_rpc
    w.notify("physical_comparison_and_buttons_required")
    assert len(w.immediate_holds)==2 and all(x["observed_hold_us"]==600000 for x in w.immediate_holds)
    assert all(x["raw_high_us"]==710770 for x in w.immediate_holds)
    checks.append(dict(control="immediate_hold_uses_target_press_completion",passed=True,observations=w.immediate_holds))
    world=World(exe,209,1,0,False,"none","async",150000,2,1000)
    try:
        world.rpc("CMD 0 OTENROLL1 HELLO")
        serial=Serial(world,0);before=world.us
        from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
        public=Ed25519PrivateKey.from_private_bytes(bytes(range(32))).public_key().public_bytes(Encoding.Raw,PublicFormat.Raw).hex().upper()
        serial.write(f"OTENROLL1 INIT 1 {public} 17\n".encode())
        assert world.us==before and world.target_us>before
        world.guard_us=1000;assert world.guard()
        assert world.guard_overlap_us==1000 and world.us==before+1000
        assert serial.in_waiting==0 and serial.read(64)==b""
        assert world.us==before+101000
        first=serial.read(64)
        while not first:first=serial.read(64)
        assert len(first)==2 and world.empty_reads>=1 and world.partial_reads==1
        assert world.overlap_us>0
        count=world.target_commands
        serial.write(b"OTENROLL1 STATUS\n")
        all_bytes=first
        while serial.chunks or serial.data:all_bytes+=serial.read(64)
        lines=all_bytes.splitlines()
        assert lines[0].startswith(b"OTENROLL1 ID ") and lines[1].startswith(b"OTENROLL1 STATUS "),lines
        assert world.target_commands==count+1 and world.target_commands==3
        assert world.us>=before+150000 and world.target_us>=before
        checks.append(dict(control="early_write_empty_partial_order_overlap_once",passed=True,overlap_us=world.overlap_us,guard_overlap_us=world.guard_overlap_us,commands=world.target_commands))
    finally:world.close()
    # Capture every real child, including constructors that never return a World.
    original=subprocess.Popen;children=[]
    def capture(*args,**kwargs):
        child=original(*args,**kwargs);children.append(child);return child
    subprocess.Popen=capture
    try:
        cases={"startup_eof":"pass", "startup_bad":"print('BAD',flush=True)",
               "startup_timeout":"import time;time.sleep(60)",
               "rpc_eof":"print('READY',flush=True)",
               "rpc_timeout":"import time;print('READY',flush=True);time.sleep(60)",
               "rpc_shape":"import sys;print('READY',flush=True);sys.stdin.readline();print('RESULT bad',flush=True)",
               "broken_stdin":"import time;print('READY',flush=True);time.sleep(60)"}
        for name,code in cases.items():
            w=None;error=None
            try:
                w=World(exe,0,0,0,False,"none",rpc_timeout=.1,launch=[sys.executable,"-c",code])
                if name=="broken_stdin":w.child.stdin.close()
                w.rpc("ADV 100000")
            except Exception as exc:error=type(exc).__name__
            finally:
                if w is not None:w.close()
            assert error is not None and children[-1].poll() is not None,name
            checks.append(dict(control=name,passed=True,original_error=error,reaped=True))
    finally:subprocess.Popen=original
    return checks

def main():
    p=argparse.ArgumentParser();p.add_argument("--exe",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    p.add_argument("--sdk-us",type=int,default=209);p.add_argument("--guard-us",type=int,default=14623);p.add_argument("--airtime-us",type=int,default=1)
    p.add_argument("--mode",choices=["baseline","polling_only","delayed_only","combined","all"],default="all")
    p.add_argument("--negative",default="none",choices=["none","identity_loss","late_authority","malformed_RFREADY","rearm_failure","context_loss_response","expiry_response","authority_expiry_response"])
    p.add_argument("--controls",action="store_true")
    p.add_argument("--bridge-source",type=Path,help="Exact private baseline bridge for paired host replay only")
    p.add_argument("--transport",choices=["sync","async"],default="async");p.add_argument("--response-delay-us",type=int,default=0);p.add_argument("--chunk-bytes",type=int,default=64);p.add_argument("--chunk-gap-us",type=int,default=0)
    a=p.parse_args();assert a.response_delay_us>=0 and a.chunk_bytes>0 and a.chunk_gap_us>=0;modes=["baseline","polling_only","delayed_only","combined"] if a.mode=="all" else [a.mode]
    if a.controls:
        a.output.write_text(json.dumps(controls(a.exe),indent=2)+"\n");return
    results=[run(a,m,a.negative) for m in modes];a.output.write_text(json.dumps(results,indent=2)+"\n")
    for r in results:print(json.dumps({k:r[k] for k in ("mode","negative","error","cleanup","elapsed_us","statistics")}))
if __name__=="__main__":main()
